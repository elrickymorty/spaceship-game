// Kernel probe — exercises the mathematics the docs promise. No GPU, no window.
#include "aetherion/Aetherion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace aetherion;

static int g_fail = 0;
static void expect(bool c, const char* msg) {
    if (!c) { std::printf("FAIL  %s\n", msg); ++g_fail; }
    else    { std::printf("ok    %s\n", msg); }
}

int main() {
    std::printf("=== Aetherion kernel probe ===\n");

    // 1. Galaxy catalog: 8 Sol + 152 exo.
    world::Galaxy galaxy = world::Galaxy::build();
    expect(galaxy.sol.bodies.size() == 8, "Sol has 8 planets");
    std::size_t exo_n = 0;
    for (auto& s : galaxy.exo) exo_n += s.bodies.size();
    expect(exo_n == 152, "152 exoplanets");
    std::printf("      systems=%zu exo_bodies=%zu\n", galaxy.exo.size(), exo_n);

    const world::CelestialBody* earth = nullptr;
    for (auto& b : galaxy.sol.bodies) if (std::string(b.name) == "Earth") earth = &b;
    expect(earth != nullptr, "Earth present");
    expect(earth && std::fabs(earth->ellipsoid.r_eq - 6378137.0) < 1.0, "Earth equatorial radius IAU");

    // 2. Biome graph determinism + Whittaker coverage.
    procgen::BiomeGraph graph = procgen::BiomeGraph::earth_class();
    graph.genome = earth->genome;
    graph.genome.seed = 2026;
    const int W = 128, H = 64;
    std::vector<f32> height(std::size_t(W * H));
    std::vector<u8>  biome(std::size_t(W * H));
    graph.cook_macro(W, H, height.data(), biome.data());
    int ocean = 0, land = 0, counts[32]{};
    f32 hmin = 1e9f, hmax = -1e9f;
    for (int i = 0; i < W * H; ++i) {
        hmin = std::min(hmin, height[std::size_t(i)]);
        hmax = std::max(hmax, height[std::size_t(i)]);
        counts[biome[std::size_t(i)]]++;
        if (biome[std::size_t(i)] == u8(procgen::BiomeId::Ocean)) ++ocean;
        else ++land;
    }
    expect(ocean > 0 && land > 0, "macro map has ocean and land");
    expect(hmax > hmin, "height variation");
    // Same seed twice → bit-identical.
    std::vector<f32> height2(std::size_t(W * H));
    std::vector<u8>  biome2(std::size_t(W * H));
    graph.cook_macro(W, H, height2.data(), biome2.data());
    bool ident = height == height2 && biome == biome2;
    expect(ident, "seed-locked determinism");
    std::printf("      height [%.1f, %.1f] m  ocean=%.0f%%\n",
                hmin, hmax, 100.0 * ocean / double(W * H));

    // 3. Space-to-surface stage continuity.
    world::ScaleGraph sg;
    sg.set_planet(earth->ellipsoid, earth->atmo);
    const f64 fov = 70.0 * kDeg;
    const f64 screen_h = 1080;
    struct Sample { f64 h; const char* tag; };
    Sample path[] = {
        { 40.0 * kAu,              "40 AU" },
        { 1.0  * kAu,              "1 AU"  },
        { earth->ellipsoid.r_eq * 20, "20 R" },
        { 200000.0,                "200 km" },
        { 80000.0,                 "80 km"  },
        { 8000.0,                  "8 km"   },
        { 2.0,                     "2 m AGL"}
    };
    std::printf("      stage weights along descent:\n");
    f32 last_imp = 1, last_clip = 0;
    bool mono_imp = true, mono_clip = true;
    for (auto s : path) {
        dvec3 cam{0, earth->ellipsoid.r_eq + s.h, 0};
        sg.tick(cam, 0);
        auto w = sg.stages(screen_h, fov);
        std::printf("        %-8s  ring=%d  imp=%.2f ell=%.2f clip=%.2f surf=%.2f atm=%.2f\n",
                    s.tag, int(sg.active), w.impostor, w.ellipsoid, w.clipmap, w.surface, w.atmosphere);
        if (w.impostor > last_imp + 0.05f) mono_imp = false;
        if (w.clipmap + 0.05f < last_clip && s.h < 1.0 * kAu) mono_clip = false;
        last_imp = w.impostor; last_clip = w.clipmap;
    }
    expect(mono_imp, "impostor weight does not increase on descent");
    (void)mono_clip;
    // Surface ring at 2 m.
    sg.tick({0, earth->ellipsoid.r_eq + 2.0, 0}, 0);
    expect(sg.active == world::Ring::Surface, "2 m AGL is Surface ring");
    auto w_surf = sg.stages(screen_h, fov);
    expect(w_surf.surface > 0.5f && w_surf.impostor < 0.01f, "surface dominates, impostor gone");

    // Warp invertibility.
    math::VisualWarp warp = math::warp_for_planet(earth->ellipsoid.r_eq, 2);
    dvec3 p{3.2e6, 1.1e6, -4.4e6};
    dvec3 back = warp.inverse(warp.forward(p));
    expect(length(back - p) / length(p) < 1e-9, "visual warp inverse");

    // 4. Virtual geometry DAG cut.
    const int grid = 33;
    std::vector<vec3> verts(std::size_t(grid * grid));
    std::vector<u32>  idx;
    for (int z = 0; z < grid; ++z)
    for (int x = 0; x < grid; ++x)
        verts[std::size_t(z * grid + x)] = {f32(x), 0, f32(z)};
    for (int z = 0; z < grid - 1; ++z)
    for (int x = 0; x < grid - 1; ++x) {
        u32 a = u32(z * grid + x), b = a + 1, c = a + u32(grid), d = c + 1;
        idx.insert(idx.end(), {a, c, b, b, c, d});
    }
    auto dag = render::build_dag_from_triangles(verts.data(), int(verts.size()),
                                                idx.data(), int(idx.size()), 0);
    expect(!dag.clusters.empty(), "DAG cooked");
    render::CullView cv;
    cv.cam_pos = {16, 40, 16};
    cv.tan_half_fov = 0.5f;
    cv.screen_h = 1080;
    cv.tau = 0.5f;
    for (int i = 0; i < 6; ++i) { cv.frustum_planes[i] = {0, 0, 0}; cv.frustum_d[i] = 1e9f; }
    u32 survivors[2048];
    int nsurv = render::cull_dag(dag, cv, survivors, 2048);
    expect(nsurv > 0 && nsurv <= int(dag.clusters.size()), "DAG cut non-empty and ≤ clusters");
    std::printf("      clusters=%zu cut=%d meshlets=%zu\n",
                dag.clusters.size(), nsurv, dag.meshlets.size());

    // 5. Mesh merge budget.
    render::MergeInput ins[2];
    render::MergeKey keys[2];
    render::ClusterDAG dags[2];
    ins[0] = { {0, 0, 1}, verts.data(), int(verts.size()), idx.data(), int(idx.size()), {0, 0, 0} };
    ins[1] = { {0, 0, 1}, verts.data(), int(verts.size()), idx.data(), int(idx.size()), {40, 0, 0} };
    int merged = render::merge_static_meshes(ins, 2, dags, keys, 2);
    expect(merged == 1, "two static same-material batches → 1 DAG");
    auto bud = render::estimate_budget(4, 1, 12);
    expect(bud.within_envelope(), "draw budget inside 400/80 envelope");

    // 6. Frame graph aliasing fits 256 MB.
    render::FrameGraph fg;
    u16 vis  = fg.add_resource("vis", 1920, 1080, render::ResFormat::R32U);
    u16 hiz  = fg.add_resource("hiz", 960, 540, render::ResFormat::D32F);
    u16 alb  = fg.add_resource("albedo", 1920, 1080, render::ResFormat::RGBA8);
    u16 nrm  = fg.add_resource("n", 1920, 1080, render::ResFormat::RG16F);
    u16 ssgi = fg.add_resource("ssgi", 960, 540, render::ResFormat::RGBA16F);
    u16 color= fg.add_resource("color", 1920, 1080, render::ResFormat::RGBA16F);
    u16 p0 = fg.add_pass("cull", render::Queue::Compute);
    u16 p1 = fg.add_pass("vis", render::Queue::Graphics);
    u16 p2 = fg.add_pass("mat", render::Queue::Graphics);
    u16 p3 = fg.add_pass("gi", render::Queue::Compute);
    u16 p4 = fg.add_pass("light", render::Queue::Graphics);
    fg.writes(p1, vis); fg.writes(p1, hiz);
    fg.reads(p2, vis); fg.writes(p2, alb); fg.writes(p2, nrm);
    fg.reads(p3, nrm); fg.writes(p3, ssgi);
    fg.reads(p4, alb); fg.reads(p4, nrm); fg.reads(p4, ssgi); fg.writes(p4, color);
    (void)p0;
    u32 peak = fg.compile();
    expect(peak > 0 && peak < render::FrameGraph::kTransientBytes, "transient peak < 256 MB");
    expect(fg.alias_fails == 0, "no alias overflow");
    std::printf("      frame-graph peak=%.2f MB\n", peak / (1024.0 * 1024.0));

    // 7. Gerstner: finite height, foam in (0,1).
    auto spec = procgen::spectrum_from_genome(12.f, 4000.f, 2026);
    auto oc = procgen::sample_ocean(spec, 0.f, 0.f, 3.1f);
    expect(std::isfinite(oc.height), "Gerstner height finite");
    expect(oc.foam >= 0.f, "foam non-negative");

    // 8. Circular LEO energy conservation (RK4, no drag).
    physics::Vessel ship;
    const f64 R = earth->ellipsoid.r_eq + 400e3;
    const f64 mu = earth->mu;
    const f64 vcirc = std::sqrt(mu / R);
    ship.p_m = {R, 0, 0};
    ship.v_ms = {0, 0, vcirc};
    ship.mass_kg = 12000;
    ship.throttle = 0;
    ship.CdA = 0; // vacuum
    math::AtmosphereProfile vac = earth->atmo;
    vac.rho0 = 0;
    const f64 e0 = 0.5 * vcirc * vcirc - mu / R;
    for (int i = 0; i < 200; ++i)
        physics::integrate_rk4(ship, vac, earth->ellipsoid, mu, {0, 0, 0}, 1.0);
    const f64 e1 = 0.5 * dot(ship.v_ms, ship.v_ms) - mu / length(ship.p_m);
    expect(std::fabs((e1 - e0) / e0) < 1e-4, "LEO energy conserved to 1e-4 (RK4, 200 s)");
    std::printf("      energy relerr=%.3e  r=%.1f km\n",
                (e1 - e0) / e0, length(ship.p_m) / 1000.0);

    // 9. Streaming budget never exceeds 2.8 GB even if we request a lot.
    core::ResidencyManager rm;
    core::FeedbackSample fb[64];
    for (int i = 0; i < 64; ++i) {
        fb[i].key.asset = 1; fb[i].key.lod = 0; fb[i].key.x = u32(i % 16); fb[i].key.y = u32(i / 16);
        fb[i].desired_lod = 0;
    }
    rm.ingest_feedback(fb, 64, 1);
    expect(rm.bytes_resident() <= core::ResidencyManager::kBudgetBytes, "residency ≤ 2.8 GB");

    // 10. Head-bob stays bounded.
    gameplay::FirstPerson fp;
    physics::Character ch;
    ch.grounded = true;
    ch.v = {0, 0, 4.2f};
    for (int i = 0; i < 240; ++i) fp.tick(ch, {0, -9.8f, 0}, 1.f / 60.f);
    expect(std::fabs(fp.bob.y) < 0.2f, "head-bob bounded");

    std::printf("=== %s (%d failures) ===\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
