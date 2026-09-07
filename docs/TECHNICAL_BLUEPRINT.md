# Aetherion Engine — Technical Blueprint

**Classification:** Engine architecture / rendering / world simulation
**Visual bar:** Cinematic PBR, fully dynamic lighting, filmic post
**Hardware bar:** 4-core CPU, 8 GB RAM, GTX 1060-class or integrated Xe/RDNA2
**Frame target:** 16.6 ms GPU, < 4 ms CPU render-thread, 60 Hz locked

---

## 0. Design Thesis

Photorealism on low-end hardware is a *perception* problem, not a path-tracing problem.

Unreal Lumen and Nanite are reference *looks*. Recreating their cost on a 65 W laptop is impossible. Recreating their *image* is not: a visibility-buffer pipeline, virtual geometry with cluster DAGs, cascaded probe GI, screen-space traces, froxel fog, and temporal upscaling produce the same spatial cues (contact shadows, bounced light, dense silhouettes, atmospheric depth) at a fraction of the bandwidth.

Three non-negotiable rules:

1. **The CPU never walks triangles.** Geometry is clustered offline, culled and rasterized on GPU.
2. **RAM holds the working set, not the universe.** Virtual textures, clipmap heightfields, and meshlet pages stream against frustum + proximity. Everything else is a handle.
3. **Scale is a graph, not a float.** No single coordinate system spans a galaxy and a fingerprint. Nested frames with floating origin and logarithmic depth make the space-to-dirt transition a continuous function, not a load.

---

## 1. System Map

```
┌─────────────────────────────────────────────────────────────────┐
│                         GAME THREAD                             │
│  Input → Vessel / FPS controller → ScaleGraph rebase            │
│  Ecosystem tick (budgeted) → POI / scan queries                 │
└───────────────┬────────────────────────────┬────────────────────┘
                │ jobs                       │ camera + origin
┌───────────────▼────────────┐  ┌────────────▼────────────────────┐
│        JOB SYSTEM          │  │         RENDER THREAD           │
│  procgen pages             │  │  FrameGraph compile             │
│  hydrology, erosion        │  │  residency update (async DMA)   │
│  fauna steering            │  │  upload: meshlet pages, mips    │
│  physics islands           │  │  record: indirect draw args     │
│  mesh-merge bake           │  └────────────┬────────────────────┘
└────────────────────────────┘               │
                                ┌────────────▼────────────────────┐
                                │              GPU                │
                                │  Cull → SW/HW raster → VisBuf   │
                                │  Material → Shadows → GI        │
                                │  Volumetrics → Transparents     │
                                │  Post (TAA/FSR, DoF, grain)     │
                                └─────────────────────────────────┘
```

---

## 2. Coordinate Hierarchy (Large World Coordinates)

| Ring | Unit | Type | Origin | Contents |
|---|---|---|---|---|
| Galaxy | parsec | `double3` | current star | 152 exoplanet systems + Sol |
| System | AU | `double3` | current barycenter | stars, planets, rings, ships in cruise |
| Orbital | km | `double3` | planet center | atmosphere, stations, approach |
| Surface | m | `float3` | floating origin under camera | clipmaps, actors, fluids |

Every frame the **ScaleGraph** chooses the finest ring whose unit keeps camera-relative positions inside `[-2^14, 2^14]` in float. When the camera crosses a ring boundary, all resident actors are rebased. Physics continues in double in the parent ring; only rendering and character motion use the float ring.

See `docs/SPACE_TO_PLANET_TRANSITION.md` for the warp, clipmap, and atmosphere-handoff mathematics.

---

## 3. Frame Budget (Low-Tier Envelope)

| Pass | ms (GPU) | Notes |
|---|---|---|
| Cluster cull + visbuffer | 2.4 | 1 compute cull, 1 SW raster, 1 HW raster |
| Material resolve | 1.8 | bindless PBR, 4 material classes |
| Cascaded shadows (3) | 1.6 | cached static pages + dynamic only |
| GI (probes + SSGI + SSR) | 2.0 | 1/2 res SSGI, probe relight 8/frame |
| Volumetric fog + sky | 1.4 | 160×90×48 froxels, temporal |
| Transparents / hair / SSS | 0.8 | screen-space SSS, anisotropic water |
| Temporal upsample ×2 | 1.2 | FSR 2-class, render at 1080p→up |
| DoF, motion blur, grain, tonemap | 1.4 | filmic, grain hides upsample |
| UI / hologram | 0.6 | SDF text + additive volume |
| **Total** | **~13.2** | 3.4 ms headroom for spikes |

CPU render thread: residency + command recording < 4 ms. Game thread: 8 ms including physics job wait.

RAM working set ceiling: **2.8 GB** textures + geo pages, **400 MB** probes/SDF clipmap, **200 MB** CPU systems. Remainder is OS + audio.

---

## 4. Rendering — What We Implement vs. What We Fake

| UE5 / GTA 6 cue | Aetherion stand-in | Why it reads the same |
|---|---|---|
| Nanite | Meshlet DAG + visbuffer + SW raster for < 16 px tris | Silhouette density, no CPU draw spam |
| Lumen | Radiance cache (surfels) + DDGI cascades + SSGI/SSR | 1st bounce local, 2nd bounce from probes |
| VSM / virtual shadows | Cached CSM pages + screen-space contact | Stable large-scale + crisp contact |
| Megascans photogrammetry | Virtual textures, POM, microfacet variation | Pore-scale on camera-facing pages only |
| Cinematic camera | TAA + FSR, anamorphic DoF, 180° shutter blur, 16 mm grain | Hides 1080p and mip pops |
| Atmospheric scattering | Hillaire 2020 LUT + froxel fog | Planet from space *and* on foot |

We do **not** ship hardware RT. A DXR/VK_KHR_ray_query path exists as a compile flag for high-end, replacing SSGI with HW probes. Low-end default is compute.

Full pipeline: `docs/RENDERING_PIPELINE.md`.

---

## 5. Geometry — Virtualized Meshlet DAG

Offline (content cook):

1. Mesh → meshlets of ≤ 64 tris / 64 verts (cache-line aligned).
2. Meshlets grouped by 8–32 into *clusters*. Clusters simplified (quadric) into parent clusters → DAG.
3. Each cluster stores: bounding sphere, normal cone, geometric error ε (object space), material id, page offset.

Runtime (every frame, GPU):

```
for each instance:
    project ε * lod_scale against pixel error threshold τ (0.5 px)
    cone-cull, frustum-cull, Hi-Z occlude
    surviving leaves → raster bin
        if projected area < 16 px² → software raster (64-lane compute)
        else → HW raster into 32-bit visibility buffer (cluster<<8 | tri)
```

No vertex shader shading. Material pass fetches visbuffer, reconstructs barycentrics, samples bindless textures. Draw count is O(instances of unique materials), not O(triangles).

Static world chunks that survive merge (see §8) become a single instance with a combined DAG.

---

## 6. Lighting

**Direct:** clustered forward-adjacent deferred. One directional (star) + local clustered lights in 64³ froxels.

**Shadows:** 3-cascade CSM, 2048², cached. Pages that contain only static geometry are invalidated only on time-of-day quanta (4 minutes). Contact shadows (16 taps) restore high-frequency.

**GI:**

- *Radiance cache* — world-space surfels spawned on visbuffer (max 64k). Relight 4k/frame.
- *DDGI* — 3 clipmap cascades (32³, 16³, 8³) of octahedral probes. Trace against a 2-band SH distance-field clipmap, not triangles.
- *SSGI / SSR* — half-res, 8-tap, temporally accumulated. Fills the last 10 m.

**Atmosphere:** Hillaire 2020. Transmittance + multi-scatter LUTs per planet (cooked from Rayleigh/Mie/ozone profiles). Aerial perspective is a 32³ volume around the camera.

**Volumetric fog:** 160×90×48 froxels, exponential slice distribution. Participating media density from height fog + biome dust + engine exhaust. Temporal reprojection with 0.9 history.

**SSS:** screen-space diffusion profile (Jensen) for organic materials. Cheap, reads as skin/leaves under the cinematic grade.

**Anisotropy:** Kajiya-Kay / GGX-aniso for water glints and brushed hull metal. Tangent from screen derivatives + flow map on oceans.

---

## 7. Materials (PBR, Four Permutations)

Shader permutation explosion kills low-end. Four uber-shaders, bindless textures, all features as material *flags* in a 32-bit word:

| Class | Features |
|---|---|
| `LitOpaque` | GGX, POM (4–16 steps by distance), detail tiled 3×, height-blend |
| `LitOrganic` | + SSS profile, fuzz (cloth/leaf), wind vertex |
| `LitMetal` | + anisotropy, clearcoat (helmets, wet rock) |
| `LitWater` | Gerstner + FFT ripple LOD0, GGX-aniso, absorption, foam |

Parallax occlusion mapping is the terrain “depth” cheat: 4 steps beyond 20 m, 16 steps in the 2 m near field. Combined with photogrammetry albedos and a 16 mm grain layer, 2k virtual-texture pages read as 8k.

---

## 8. Memory & Draw-Call Discipline

### Texture streaming

Virtual texture 128k×128k logical, 128 px tiles, 8-page cache lines. Feedback buffer from last frame’s visbuffer mip requests. CPU residency manager:

- Promote: tiles in frustum ∩ (distance < d_mip(lod))
- Demote: LRU outside a 1.4× frustum dilated cone
- Never stall the frame: missing tile shows parent mip (always resident at mip ≥ 8)

### Asset GC

Every 4 frames, a sweeper:

```
if (last_seen_frame < current - 45) && not pinned → unmap GPU, free CPU
```

Pinned: current planet clipmap ring 0–2, player body, weapon, ship interior.

### Mesh merge

Offline + runtime (when a procgen page freezes):

- Same material + same lightmap/probe chart + static → concatenate meshlets into one DAG.
- Target: **< 400 instances**, **< 80 unique pipelines** per frame on a planet surface.
- Vegetation and rocks are *not* merged; they are `DrawIndexedInstancedIndirect` with a 64-byte instance record (pos, yaw, scale, wind phase, vt atlas).

### Async compute

Overlap: cluster cull // shadow raster, probe relight // visbuffer, SSGI // volumetric inject. Three queues (graphics, compute, copy). Copy queue streams texture pages and meshlet pages independently.

---

## 9. World — 160 Planets

### Sol (8)

Cooked from IAU radii, JPL orbital elements, measured albedos, and published DEM where it exists (Earth SRTM/GEBCO subsampled, Mars MOLA, Mercury/Venus/Moon public DEMs). Gas giants are procedural band + storm noise over an ellipsoid, never a heightfield. Rings are particle + virtual-texture Keplerian disks (Saturn, Uranus, Jupiter faint, Neptune faint).

### Exoplanets (152)

Deterministic `u64` seed → `PlanetGenome`. Genome locks: mass, radius, star class, SMA, eccentricity, axial tilt, rotation period, volatile inventory, tectonic factor, biome graph id. Same seed always yields the same world on any machine.

Generator pipeline (worker jobs, page-based):

```
ellipsoid → continental noise (domain-warped FBM)
         → hydraulic erosion (32 cheap iterations, GPU)
         → climate (latitude, altitude lapse, rain shadow)
         → Whittaker biome assignment
         → hydrology graph (rivers, lakes, coasts)
         → scatter (vegetation, rocks, POI)
         → fauna density fields
```

See `docs/BIOME_GRAPH.md`.

---

## 10. Gameplay Systems (Kernel Hooks)

- **First-person body:** IK hands on stick/throttle/weapon, visible legs, spring-damper head-bob parameterized by gait and G-load.
- **Vessel:** Newtonian integrator, `F = T + D + g(r)`, no arcade damping unless assist is on. RCS, main, and atmospheric aero as separate force terms.
- **On-foot:** walk, sprint, vault, jetpack (limited Δv, fuel). Movement in surface ring.
- **Holographic map:** three nested SDFs — galaxy point cloud, system Kepler orrery, surface topographic scan. Waypoints persist across rings via ScaleGraph ids.

---

## 11. Seamless Transition Contract

From 40 AU to boot-on-regolith, the player never sees a load. The contract:

1. Planet angular size < 2 px → billboard impostor (prefiltered).
2. 2 px – 6° → tessellated ellipsoid, height as normal-map only.
3. 6° – atmosphere interface → spherical clipmap rings spawn, skybox fades.
4. Interface → 0 m AGL → floating origin rebases to tangent, ellipsoid becomes horizon mesh, clipmap is the world.

All four stages share the same planet seed and the same atmosphere LUT, so color and silhouette never pop. Mathematics in `docs/SPACE_TO_PLANET_TRANSITION.md`.

---

## 12. Threading Model

- **Game thread:** input, camera, scale rebase, gameplay FSM.
- **Render thread:** frame graph, residency, command buffers.
- **N-1 worker fibers** (N = hw threads): physics islands, procgen pages, fauna, mesh merge, GC.
- **GPU queues:** graphics, async compute, copy.

Procgen never runs on the game or render thread. A page becomes visible only after its `ready` fence, otherwise the parent clipmap mip stands in (same rule as textures).

---

## 13. What This Kernel Is Not

It is not a complete game. It does not allocate a swapchain, load a GLTF zoo, or pretend to be Unreal. It *is* the architecture and the mathematics that make a GTA-6-looking space game possible on a bad laptop — expressed as real C++ systems you can compile, test, and hang a renderer on.
