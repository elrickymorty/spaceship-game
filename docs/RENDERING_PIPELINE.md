# Rendering Pipeline — Visibility Buffer, GI, Volumetrics, Post

Aetherion is a **GPU-driven visibility-buffer renderer** with a transient frame graph. Shading is deferred to a material resolve that runs only on surviving pixels. This is the Nanite *idea* (don't pay for occluded or sub-pixel work) without Nanite's cooking toolchain or hardware RT.

---

## 1. Frame Graph (transient aliasing)

Passes are nodes. Resources are virtual until compile, then aliased onto a 256 MB ring of memoryless transients. Lifetimes are liveness-colored so the visbuffer, Hi-Z, and SSR history share physical tiles.

```
CullClusters  →  RasterVis   →  HZB
                              →  MaterialResolve →  CSM (cached)
                                                →  ProbeRelight   ─┐
                                                →  SSGI/SSR       ─┼→ DeferredLight
                                                →  FroxelInject   ─┘
DeferredLight → AerialPerspective → Transparents → TAA/FSR → DoF → Grain → Tonemap
```

Async compute: `CullClusters` overlaps last frame's `ProbeRelight`. `FroxelInject` overlaps `MaterialResolve`. Copy queue is independent.

---

## 2. Visibility Buffer

32-bit payload per pixel:

```
[ 31:8  cluster_id ][ 7:0 triangle_id ]
```

Software raster (compute, 64 threads / meshlet) writes with `atomicMin` on a 64-bit depth+payload. Hardware raster writes the same format through a thin VS/PS with `SV_PrimitiveID`.

Material resolve reconstructs:

```
bary = vis_barycentric(cluster, tri, pixel)          // from screen derivatives
P    = lerp(v0, v1, v2, bary)                        // object space
N    = normalize(lerp(n0, n1, n2, bary))
uv   = lerp(uv0, uv1, uv2, bary)
sample bindless heap[material.vt_root + uv]
```

G-buffer after resolve (half-res normals optional on low):

| RT | Format | Contents |
|---|---|---|
| A | R10G10B10A2 | albedo + flags |
| B | RG16F | octahedral normal |
| C | R8G8B8A8 | roughness, metallic, ao, anisotropy |
| D | R16F | linear depth (also in Hi-Z) |

---

## 3. Virtual Geometry (cluster DAG)

Geometric error ε of a cluster is the max quadric simplification error of its parent. LOD selection:

```
pixel_error = (ε * lod_scale * screen_h) / (2 * tan(fov/2) * view_z)
keep_leaf   = pixel_error ≤ τ          // τ = 0.5 px default, 1.0 on low
```

Parent and children are mutually exclusive (DAG cut). A persistent GPU buffer of 48-byte cluster headers is boundless; instances carry a 16-byte header (world matrix index, dag root, lod_scale, flags).

Instance count on a dense forest planet: ~200k grass + 30k rocks + 4k trees. After cone/frustum/Hi-Z: ~8–20k meshlets rasterized. SW path eats the grass.

---

## 4. Global Illumination (Lumen-class cues, probe cost)

### 4.1 Radiance cache (first bounce, local)

Spawn surfels from visbuffer where luminance variance is high and density < 2 / m². Each surfel: position, normal, radius, incoming SH2. Relight 4096 / frame by sampling the star + 8 nearest probes. Neighbour gather (8 taps) diffuses.

### 4.2 DDGI cascades (second bounce, world)

Three clipmaps centered on camera:

| Cascade | Probe grid | Spacing | Range |
|---|---|---|---|
| 0 | 32³ | 1.5 m | 48 m |
| 1 | 16³ | 6 m | 96 m |
| 2 | 8³ | 24 m | 192 m |

Each probe: 16×16 octahedral irradiance + 16×16 depth for visibility. Relight 8 probes/frame by cone-marching a 256³ SDF clipmap (8-bit, 0.5 m voxels near, 4 m far). That SDF is the *only* world representation GI ever traces. Triangles are never ray-traced on low-end.

### 4.3 SSGI / SSR

Half resolution, 8 marching steps, thickness 0.4 m, temporal blend 0.85. SSR uses the same marcher with a GGX-importance direction. Hits missing in the visbuffer fall back to probe irradiance — this is what prevents the “indoor black corners” look.

### 4.4 Energy

```
L = L_direct * shadow
  + albedo * (E_ssgi + E_probe * (1 - ss_confidence))
  + specular_env * (ssr + probe_spec * (1 - ssr_conf))
```

No light leaking through thin walls: probe depth test + surfel normal-weight.

---

## 5. Shadows

- **CSM** 3 cascades, 2048², exponential split (λ = 0.85). Static page cache: 16-bit depth atlas. Dynamic casters (player, fauna, vegetation wind) re-render into a small 1024² overlay, composited.
- **Contact shadows** 16 taps along light in view space, 0.3–2 m. Restores the “GTA 6 trailer” micro contact that CSM cannot.
- **Translucent** (leaves): dithered punch-through in cascade 0 only.

---

## 6. Atmosphere & Volumetric Fog

Per-planet LUTs cooked from Hillaire 2020:

- Transmittance 256×64
- Multi-scatter 32×32
- Sky-view 200×100 (rebuilt when sun moves > 0.2°)
- Aerial perspective 32×32×32 around camera

Froxel fog 160×90×48, slices exponential (`z = znear * (zfar/znear)^(s/S)`). Density:

```
σ(p) = σ_height * exp(-(h-h0)/H)
     + σ_biome  * dust(biome, p)
     + σ_local  * (exhaust | impact | waterfall)
```

Scattering uses the same Rayleigh/Mie coefficients as the sky LUT so the horizon never seams.

---

## 7. Water (Gerstner + LOD)

Oceans are a projected grid (camera-centered) of 256×256 on LOD0, Gerstner 6 waves + 2 FFT ripple tiles. Spectrum from planet wind (genome). Shore foam from depth × slope. Underwater: absorption e^(-β d) with β from algae (biome). Anisotropic GGX, tangent = flow.

Lakes share the same shader with a clip plane and a smaller spectrum.

---

## 8. Post — the photorealism layer

Order is load-bearing. Grain *after* DoF *after* TAA is what hides 1080p and virtual-texture pops.

1. **TAA / FSR 2-class upsample** — render 1920×1080 (or 1600×900 on low), output 2560×1440 or 4K. 16-tap history, luminance clamp, reactive mask from visbuffer change.
2. **Motion blur** — 180° cinematic shutter, 8 taps, velocity from camera + skinned.
3. **Cinematic DoF** — gather, anamorphic 1.3×, CoC from gameplay focus (cockpit vs helmet vs scan).
4. **Bloom** — 5-mip Karis, threshold 1.2, dirt mask optional.
5. **Color** — ACES fitted, per-planet white balance (star CCT).
6. **Film grain** — 16 mm, 0.04–0.08 intensity, chroma 0.35. *This is not decoration; it is the low-res hide.*
7. **CA / vignette** — 0.6 px, 0.12 vignette. Stop before “filter” territory.

---

## 9. CPU-Side Render Contract

The render thread is not allowed to:

- iterate triangles, vegetation instances, or lights beyond clustered bins
- allocate (frame arena only)
- wait on procgen (parent mip is always legal)
- issue more than **80** `vkCmdBindPipeline` and **400** instance records

All draws are indirect. CPU writes a residency list and a camera UBO. That is the entire per-frame CPU payload besides command buffer record.
