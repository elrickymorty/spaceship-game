# Biome Graph — Node Language and Planetary Ecology

Planets are not painted. They are *evaluated*. A biome graph is a deterministic DAG of noise, climate, hydrology, scatter, and fauna nodes. The same graph plus a `u64` seed plus a `PlanetGenome` produces the same 16k×8k height/biome maps on any machine.

Graphs are data (`config/biomes/*.json`). The evaluator is `aetherion::procgen::BiomeGraph`.

---

## 1. Evaluation domains

| Domain | Resolution | When |
|---|---|---|
| Planet macro | 4096×2048 equirect | cooked once per planet, compressed VT |
| Clipmap page | 257×257 | worker job when a ring page is requested |
| Scatter | 1 sample / 2 m (LOD0) | GPU indirect, density from page |

Macro evaluation is the source of climate and continental shape. Clipmap evaluation adds erosion detail and micro-height. Scatter never runs on the CPU for drawing; CPU only writes density pyramids.

---

## 2. Node catalog

Every node: `id`, `type`, `params`, `inputs[]`. Outputs are named (`height`, `mask`, `scalar`, `vec2`, `biome_id`, `density`). Types:

### 2.1 Noise

| Type | Outputs | Parameters (defaults) |
|---|---|---|
| `FBM` | height | octaves 8, lacunarity 2.02, gain 0.5, scale 4e-5, seed_ofs 0 |
| `RidgedMF` | height | octaves 6, gain 0.5, ridge_offset 1.0, scale 8e-5 |
| `Worley` | height, id | metric euclid, jitter 0.9, scale 2e-4 |
| `DomainWarp` | any | strength 1200 m, octaves 3, input field |
| `Terrace` | height | steps 12, sharpness 0.65 |
| `Billow` | height | octaves 5, gain 0.45 |

### 2.2 Climate

| Type | Meaning |
|---|---|
| `Temperature` | `T = T_eq * cos^k(lat) - Γ h + season(lat, tilt, day)` |
| `Moisture` | orographic: advection of ocean humidity against height gradient, rain shadow |
| `WindPrevailing` | from genome `coriolis * star_flux`, output vec2 |
| `Whittaker` | lookup (T, moisture) → biome_id (see table) |

Lapse rate `Γ = 6.5 K/km` Earth-class, scaled by `g / g_0 * c_p_air`.

### 2.3 Hydrology

| Type | Meaning |
|---|---|
| `HydraulicErosion` | 32 iterations, droplet lifetime 30, inertia 0.3, capacity 4, evaporation 0.02 |
| `ThermalErosion` | talus angle 35°, 8 iterations |
| `RiverGraph` | priority-flood depression fill, flow accumulation, channel width ∝ A^0.4 |
| `LakeFill` | basins where accumulation > τ and slope < 0.5° |
| `Coast` | signed distance to sea level, wave-cut terrace 1.5 m |

### 2.4 Surface

| Type | Meaning |
|---|---|
| `SlopeMask` | `smoothstep(a, b, 1 - n·up)` |
| `AltitudeBand` | `[h0, h1]` with 40 m feather |
| `Blend` | height min/max/add/lerp by mask |
| `POMHint` | amplitude for parallax, from high-frequency FBM |

### 2.5 Scatter & ecology

| Type | Meaning |
|---|---|
| `VegetationLayer` | species, density m⁻², min_slope, wind_coupled, lod_impostor_m |
| `RockLayer` | instanced mesh set, poisson radius |
| `FaunaField` | species, density km⁻², diet, flock, predator |
| `WindReactive` | vertex-wind amplitude, gust spectrum from `WindPrevailing` |

---

## 3. Whittaker table (biome_id)

Used by `Whittaker` node. Moisture 0–1, T in °C at sea-level equivalent.

|  | dry 0–0.15 | 0.15–0.4 | 0.4–0.7 | wet 0.7–1 |
|---|---|---|---|---|
| T > 22 | subtropical desert | savanna | tropical seasonal | rainforest |
| 8–22 | temperate desert | grassland | woodland | temperate forest |
| −5–8 | cold desert | shrub | boreal | boreal wet |
| T < −5 | ice | tundra | tundra wet | polar bog |

Oceans, lakes, beaches, peaks (h > snow_line(T)), and lava (genome volcanic) are overlays *after* Whittaker.

Snow line:

```
h_snow = max(0, (T_sea - 0) / Γ)     // 0 °C isotherm
```

---

## 4. Canonical Earth-class graph

```
FBM[continent] ──┐
RidgedMF[mount] ─┴─ Blend(add, 0.35) ─ DomainWarp ─ Terrace ─┐
                                                             ├─ HydraulicErosion ─ ThermalErosion ─ height
Worley[craton] ── mask ──────────────────────────────────────┘

height ─ Temperature
height ─ Moisture ← WindPrevailing
T, moisture ─ Whittaker ─ biome

height ─ RiverGraph ─ LakeFill ─ Coast ─ hydrology mask
biome + hydrology ─ VegetationLayer × N
biome ─ FaunaField × M
```

Arid super-Earths drop `RiverGraph` and raise `RidgedMF` gain. Tide-locked worlds replace `Temperature` with a day/night cosine around the substellar point. Gas-giant “surfaces” skip this graph entirely (band noise over ellipsoid).

---

## 5. Vegetation wind

GPU vertex, no CPU:

```
disp.xz = A * sin(k · xz - ω t + φ_instance)
        + A_g * gust(t, hash(id))
A = A0 * (1 - lod) * mask_canopy * wind_amp(biome)
```

LOD0 (camera < 25 m) adds 2 bone-like hinges (trunk, canopy). LOD1 is vertex only. LOD2 is an impostor (2-axis billboard, baked VT). Density is thinned by `pow(lod, 1.6)` so draw cost falls faster than visual density.

---

## 6. Fauna (context-aware, budgeted)

Not a sim of 10⁷ animals. Density fields from the graph. Runtime:

- **LOD0** (< 40 m): full skeletal, 12 max, behavior tree (graze, flee, hunt).
- **LOD1** (40–120 m): baked cycle, flocking (Reynolds: sep/align/coh) in a job.
- **LOD2**: impostor particles, no AI.

Predator/prey: a 64² influence map per 2 km page, updated 2 Hz on a worker. Prey steers down the gradient of predator scent; predators steer up prey density × line-of-sight (Hi-Z sampled once per agent). CPU cost: < 0.2 ms for 12 + 40 agents.

---

## 7. Gerstner oceans (node `OceanSpectrum`)

Genome supplies fetch, wind speed U, depth. Six Gerstner components:

```
P(x, t) = Σ_i Q_i A_i D_i cos(k_i D_i·x - ω_i t + φ_i)
h(x, t) = Σ_i A_i sin(k_i D_i·x - ω_i t + φ_i)
ω² = g k tanh(k depth)          // deep-water ≈ g k
Q_i = 1 / (k_i A_i N)           // prevent loops
```

Phases `φ_i` are hash(seed, i). LOD0 adds two tiled FFT displacement maps (256², 4 Hz update on compute). Foam: `max(0, Jacobian - 0.8)`.

---

## 8. Determinism

All noise is integer-hashed (`pcg3d` or `xxhash32` on grid coords + seed). No `rand()`, no `std::uniform` in the graph. GPU and CPU evaluators share constants. A planet screenshot on two machines at the same camera must match to a ULP in height and to a biome_id exactly.
