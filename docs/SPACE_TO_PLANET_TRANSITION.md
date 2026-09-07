# Space-to-Planet Transition — Mathematics

Zero loading screens. One camera. Four scale rings. The image of a planet is a continuous function of altitude; only the *representation* changes, and every representation shares the same seed, atmosphere LUT, and ellipsoid.

This document is the source of truth for `aetherion::world::ScaleGraph` and `aetherion::math::PlanetWarp`.

---

## 1. The Problem

Earth’s radius R = 6.371×10⁶ m. A galaxy is ~3×10²⁰ m. IEEE-754 float32 has 24 bits of mantissa: at 6,371 km from origin, the ULP is ~0.5 m. At 1 AU it is kilometers. You cannot walk, you cannot dock, and a linear depth buffer has no precision left for a cockpit.

So: **never store the world in one frame.** Store a graph of frames. Render in a warped visual space whose far sphere is finite.

---

## 2. Nested Frames

A `ScaleAnchor` is a rigid frame plus a unit:

```
X_parent = R_anchor * (s * X_local) + T_anchor
```

where `s` is meters-per-local-unit, `R` is a double quaternion, `T` a `double3`.

| Anchor | s | Typical \|X\| | Precision |
|---|---|---|---|
| Galaxy | 3.086e16 m / unit (parsec) | < 10³ | double |
| System | 1.496e11 m / unit (AU) | < 60 | double |
| Orbital | 1000 m / unit (km) | < 2×10⁵ | double |
| Surface | 1 m / unit | < 1.6×10⁴ (float-safe) | float, floating origin |

### 2.1 Floating origin

Every frame, if `\|camera_surface\| > 2048 m`, subtract `round(camera, 512 m)` from all resident surface transforms and from the surface-anchor translation. Physics islands rebase with the same delta. No visual hitch: the camera UBO is rebuilt from the new origin in the same frame.

### 2.2 Ring selection

```
ring = Surface   if  h <  h_atm * 1.4
     = Orbital   if  h <  40 R
     = System    if  inside Hill sphere of current star
     = Galaxy    otherwise
```

`h` is radial altitude above the ellipsoid. Hysteresis of 10 % prevents oscillation.

---

## 3. Logarithmic Depth

Linear depth `z/w` wastes bits near the far plane. We write:

```
d_log = log2(κ * w + 1) / log2(κ * far + 1)     // reverse-Z, 32-bit
```

with `κ = 1` in surface ring, `κ = 1e-5` in orbital (so 1 m and 10⁷ m both resolve). Combined with reverse-Z (`clear = 0`, closer = larger), 32-bit depth is enough for a cockpit canopy against a planet limb.

---

## 4. Visual Warp (finite-sky mapping)

Outerra / Space Engine trick. Positions in the current ring are mapped to a ball of radius ~1 before the view matrix, so the GPU never sees 1e7-unit translations.

Let `P` be camera-relative double, `ρ = |P|`.

```
P_vis = P * ( 1 / (1 + ρ / R_w) )           // R_w = 2 * R_planet in orbital
                                            // R_w = 1e6 m in system
```

Inverse (for picking / probes):

```
ρ_vis = |P_vis|
P     = P_vis / (1 - ρ_vis / R_w)           // ρ_vis < R_w always
```

Normals are not warped (we shade in world, not vis). Rasterization of very large triangles can silhouette-bend; we tessellate the ellipsoid to ≤ 0.3° of apparent error so the warp is locally affine per triangle.

---

## 5. Planet Representation vs. Angular Size

Let `α = 2 arcsin(R / r)` be the apparent angular diameter, `r = |camera - center|`.

| Stage | α or h | Representation | What fades |
|---|---|---|---|
| A Impostor | α < 2 px | Prefiltered billboard, SH2 lighting | — |
| B Ellipsoid | 2 px ≤ α < 6° | Tessellated cube-sphere, height as bump | Impostor α |
| C Clipmap spawn | 6° ≤ α, h > h_atm | 5 spherical clipmap rings + ellipsoid shell | Ellipsoid tess |
| D Atmosphere | h ≤ h_atm | Aerial perspective volume on, clipmaps dominate | Shell α |
| E Surface | h < 8 km | Floating origin, ellipsoid = horizon skirt | — |

Blends are optical, not geometric. We always render the *finer* representation as soon as it is resident; the coarser one is multiplied by `1 - occupancy`.

### 5.1 Impostor ↔ ellipsoid

```
w_ellip = smoothstep(1.5 px, 3.0 px, α_px)
```

Impostor is a 256² prefiltered color+normal. Lighting: star direction in view, SH2 from baked sky.

### 5.2 Ellipsoid tessellation

Cube-sphere (C1, Nowell 2008):

```
let p ∈ [-1,1]³ on a cube face, p² = (x², y², z²)
x' = x √(1 − y²/2 − z²/2 + y²z²/3)
y' = y √(1 − z²/2 − x²/2 + z²x²/3)
z' = z √(1 − x²/2 − y²/2 + x²y²/3)
X  = R_equator * (x', y', z' * (1 − f))          // f = flattening
```

Tessellation factor:

```
T = clamp( α_deg * 24, 8, 256 )                  // patches per face
```

Height on B is a normal map only (no displacement) so the limb stays a perfect ellipsoid — required for the atmosphere LUT, which assumes a sphere.

### 5.3 Spherical clipmaps (C–E)

Five nested grids, each 255×255, ring i covering radius `r_i = 48 m * 4^i` on the tangent plane, then projected onto the ellipsoid:

```
Q_tangent = (u, 0, v) * r_i / 127                 // camera-centered, snapped to grid
Q_world   = ellipsoid_project(origin + R_tangent * Q_tangent)
h         = height_tex(geodetic(Q_world))         // virtual texture
Q_final   = Q_world + n_ellip * h
```

Snapping: `origin` is quantized to `r_i / 127` so vertices are stable under camera motion (no swimming). Skirts of 1 quad hide T-junctions.

Ring 0 is always resident on D/E. Rings 1–4 stream. If a ring is not ready, ring i+1’s interior is *not* punched — the coarser ring shows through. This is the zero-load contract.

---

## 6. Atmosphere Handoff

The atmosphere LUT is valid from space *and* from the ground because it is a function of `(r, μ, μ_s, ν)` not of a skybox. The only thing that changes is the *aerial perspective volume* resolution and the froxel density.

Blend the space-view sky LUT into the ground-view sky LUT with:

```
k_atm = saturate( (h_atm * 1.2 - h) / (h_atm * 0.4) )
L_sky = mix(L_space_lut, L_ground_lut, k_atm)
```

Fog froxels fade in with the same `k_atm`. No seam: both LUTs share coefficients `(β_R, β_M, H_R, H_M, ozone)`.

For Earth-class:

```
H_R = 8 km,   β_R = (5.802, 13.558, 33.100) × 10⁻⁶ m⁻¹
H_M = 1.2 km, β_M = 21.0 × 10⁻⁶ m⁻¹,  g = 0.8
ozone peak 25 km
h_atm ≈ 100 km  (density < 1e-4 of sea level)
```

Exoplanets scale `H_R` with `T_air * R² / (M g_0)` from the genome so a super-Earth with a thick CO₂ envelope has a taller, paler sky without a special case.

---

## 7. Gravity and Inertia During the Handoff

Newtonian vessel in orbital ring:

```
a = -μ r_vec / |r|³  +  T/m  +  D/m
```

On-foot in surface ring, gravity is a constant `g n_ellip` plus a Coriolis term from planet spin:

```
a = g n + 2 v × ω_planet
```

The switch is at `h = h_atm`. Momentum is conserved by transforming `v_orbital` (double, km/s) into `v_surface` (float, m/s) at the exact rebase:

```
v_surface = R_tangentᵀ * (v_orbital * 1000 − ω × r)
```

No damping is introduced. If the player hits atmo at 7.8 km/s they burn. Aero force `½ ρ C_d A |v| v` uses the same density the fog uses (`ρ = ρ_0 e^{-h/H}`), so the visual and the physical atmosphere are one function.

---

## 8. Horizon Skirt (stage E)

Once the floating origin is on the surface, the planet ellipsoid is no longer a mesh the player can walk on — the clipmap is. The ellipsoid remains as a *horizon skirt*: a low-tessellation band from 8 km out to the limb, height-displaced with mip 8 of the same height VT. The skirt and clipmap ring 4 overlap by 512 m with a height-blend:

```
w = smoothstep(r_4 - 512, r_4 - 64, ρ_tangent)
h = mix(h_clipmap, h_skirt, w)
```

This is the Horizon Zero Dawn skybox-to-mesh idea, except the “skybox” is still a mesh (the ellipsoid) because we need a correct limb against the atmosphere LUT.

---

## 9. Continuity Proof (what we guarantee)

Let `I(h)` be the rendered image as a function of altitude.

1. **Silhouette continuity.** Stages A–E all use the same ellipsoid radii `(R_eq, R_pol)`. Apparent limb is C0.
2. **Radiometric continuity.** Atmosphere LUT coefficients are constant across stages. Sky luminance at the limb matches to < 2 % (LUT resolution limited).
3. **Height continuity.** Clipmap height VT and ellipsoid bump come from the same cooked page chain. Mip 8 of the VT *is* the ellipsoid bump.
4. **No hitch.** A stage becomes the *primary* representation only when its residency bit is set. Until then the previous stage remains opaque.

If any of those four fail, it is a bug, not a load screen.
