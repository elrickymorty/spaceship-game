# Aetherion Engine

**Photorealistic first-person space exploration on constrained hardware.**

Aetherion is a purpose-built engine architecture for a 160-world exploration title: eight scaled Solar System planets plus 152 seed-locked exoplanets. Visual target is contemporary cinematic PBR (UE5 / GTA 6 trailer language). Runtime target is 60 FPS on low-tier CPUs with modest RAM, achieved by GPU-driven rendering, virtualized geometry, aggressive streaming, and perceptual post-process rather than brute-force ray tracing.

This repository contains the **technical blueprint**, the **core C++ systems** that enforce the memory/CPU budget, the **node-based biome graph**, and the **mathematics of seamless space-to-surface travel**. It is an engine kernel, not a content dump.

## Documents

| Document | Contents |
|---|---|
| [Technical Blueprint](docs/TECHNICAL_BLUEPRINT.md) | Coordinate hierarchy, frame graph, budgets, system map |
| [Rendering Pipeline](docs/RENDERING_PIPELINE.md) | Visibility buffer, GI, shadows, volumetrics, materials, post |
| [Space-to-Planet Transition](docs/SPACE_TO_PLANET_TRANSITION.md) | Nested scale spaces, warp, clipmaps, atmosphere handoff |
| [Biome Graph](docs/BIOME_GRAPH.md) | Node types, Whittaker classification, hydrology, fauna |

## Architecture at a Glance

```
Galaxy (double, parsecs)
  └─ System  (double, AU)
       └─ Orbital (double, km)          ← ship, planets as ellipsoids
            └─ Planet tangent (float, m) ← on-foot, clipmaps, biomes
```

Zero loading screens. The camera never leaves a floating origin. Distant planets are impostors that morph into tessellated ellipsoids, then into clipmap terrain as altitude collapses. Depth is logarithmic. Physics and procgen run on worker fibers. The render thread only records GPU-driven draws.

## Build

Requires C++20. The kernel is API-agnostic (Vulkan preferred; DX12 backend sketched). No windowing or swapchain code lives here — those are platform adapters.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aetherion_probe
```

Without CMake:

```bash
g++ -std=c++20 -O2 -pthread -I engine/include \
  engine/src/core/*.cpp engine/src/render/*.cpp engine/src/world/*.cpp \
  engine/src/procgen/*.cpp engine/src/physics/*.cpp engine/src/gameplay/*.cpp \
  engine/src/ui/*.cpp engine/src/kernel_probe.cpp \
  -o aetherion_probe && ./aetherion_probe
```

The probe is the contract test: 8+152 worlds, seed-locked biomes, stage-weight continuity from 40 AU to 2 m AGL, DAG cut, 256 MB frame-graph aliasing, Gerstner, RK4 LEO energy, residency cap.

## Module Map

```
engine/include/aetherion/
  math/         double3, frames, log-depth, spherical clipmaps
  core/         jobs, arenas, streaming residency
  render/       visibility buffer, virtual geo, GI, fog, CSM, merge
  world/        scale graph, solar system, exoplanet catalog
  procgen/      biome node graph, hydrology, Gerstner, vegetation
  physics/      Newtonian vessel, character, jetpack
  gameplay/     first-person body, parkour
  ui/           holographic map (galaxy / system / surface)
```
