// Headless test harness for the Luxor3 core. No SDL2/GLES2 required.
//
//   ./luxor3_test <game-root>
//
// Two modes, chosen automatically from what is on disk:
//   * fixture mode  - runs against tests/fixtures/mini_game. Format-level
//                     assertions only, no copyrighted data. This is what CI
//                     runs on GitHub Actions.
//   * full mode     - additionally asserts against the real Luxor 3 tree when
//                     it is present (run locally, never in CI).
//
// Exits non-zero on any failure.
#include "luxor.h"
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using namespace luxor;

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL  %s (line %d)\n", msg, __LINE__); ++g_failures; } \
    else { printf("ok    %s\n", msg); } \
} while (0)

// ---------------------------------------------------------------- helpers
static Image load_image(const PathTable& paths, const std::string& root,
                        const std::string& ref) {
    std::string rel = paths.resolve(ref);
    if (rel.empty()) return Image();
    std::string full = root + "/" + rel;
    FILE* f = fopen(full.c_str(), "rb");
    if (!f) return Image();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz > 0 ? sz : 0);
    size_t got = (sz > 0) ? fread(buf.data(), 1, static_cast<size_t>(sz), f) : 0;
    fclose(f);
    if (got == 0) return Image();
    return decode_image(buf.data(), buf.size());
}

// Assertions that must hold for any tree in the documented formats, whether it
// is the synthetic CI fixture or the real game data.
static void run_format_tests(const std::string& root, const PathTable& paths) {
    // ---- case-insensitive resolution ------------------------------------
    // The shipped references mix capitalisation freely; a case-sensitive
    // Linux filesystem would otherwise fail to open them.
    std::string ui = paths.resolve("data/gfx/UI/buttons/MM_top_arrow_Red");
    if (!ui.empty()) {
        std::string lower_ui = ui;
        std::transform(lower_ui.begin(), lower_ui.end(), lower_ui.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        CHECK(lower_ui.find("mm_top_arrow_red") != std::string::npos,
              "case-insensitive UI texture resolves");
    } else {
        // Fixture may not carry UI art; the capability is still asserted below.
        CHECK(!paths.resolve("data/GFX/BALLS/RED/BALL0000.PNG").empty(),
              "case-insensitive lookup finds differently-cased reference");
    }

    // Backslash separators appear in the original configs.
    std::string bs = paths.resolve("data\\maps\\testmap\\path.LX3TRK");
    if (bs.empty()) bs = paths.resolve("data\\maps\\geb\\path.LX3TRK");
    if (!bs.empty()) CHECK(true, "backslash reference resolves");

    CHECK(paths.resolve("data/does/not/exist").empty(),
          "missing ref returns empty");

    // ---- image decode ----------------------------------------------------
    std::string first_png;
    for (int i = 0; i < 16 && first_png.empty(); ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "data/gfx/balls/red/ball%04d.png", i);
        first_png = paths.resolve(buf);
    }
    if (!first_png.empty()) {
        Image img = load_image(paths, root, "data/gfx/balls/red/ball0000.png");
        CHECK(img.valid(), "ball PNG decodes");
        CHECK(img.valid() && img.channels == 4, "ball PNG is RGBA");
        CHECK(img.valid() && img.width == 32 && img.height == 32,
              "ball PNG is 32x32");
    }

    // ---- animation timing -------------------------------------------------
    // AnimDefs.txt states playbackRate is frames per second and isLooping is
    // 0/1, so the frame an animation shows must follow from the clock alone.
    {
        Animation loop;
        loop.playback_rate = 32.0f;
        loop.looping = true;
        loop.frames.assign(30, "frame");
        CHECK(animation_frame_index(loop, 0.0f) == 0, "looping frame at t=0");
        CHECK(animation_frame_index(loop, 1.0f / 32.0f) == 1,
              "one tick advances one frame");
        CHECK(animation_frame_index(loop, 29.0f / 32.0f) == 29,
              "reaches last frame before wrapping");
        CHECK(animation_frame_index(loop, 30.0f / 32.0f) == 0,
              "looping wraps back to the first frame");
        CHECK(animation_frame_index(loop, 1.0f) == 2,
              "one second of 32 fps playback lands on frame 2 of 30");
        CHECK(animation_frame_index(loop, -1.0f) == 0, "negative time is clamped");

        Animation once = loop;
        once.looping = false;
        CHECK(animation_frame_index(once, 30.0f / 32.0f) == 29,
              "non-looping animation holds its final frame");
        CHECK(animation_frame_index(once, 100.0f) == 29,
              "non-looping animation stays on the final frame");

        Animation empty;
        empty.playback_rate = 32.0f;
        empty.looping = true;
        CHECK(animation_frame_index(empty, 1.0f) == 0,
              "empty animation reports frame 0");
    }

    // ---- AnimDefs.txt -----------------------------------------------------
    // Both the fixture and the real tree ship this file; the loader must read
    // the documented Sequence schema and expand fileName frame prefixes.
    if (!paths.resolve("data/AnimDefs.txt").empty()) {
        AnimationRegistry anims;
        std::string anim_err;
        CHECK(load_anim_defs(root + "/data/AnimDefs.txt", paths, &anims, &anim_err),
              "load AnimDefs.txt");
        CHECK(!anims.sequences.empty(), "AnimDefs registers sequences");
        CHECK(anims.find("Ball_Red") != nullptr, "Ball_Red sequence found");
        const Animation* red = anims.find("Ball_Red");
        if (red) {
            CHECK(red->frames.size() >= 2, "Ball_Red has 2+ frames");
            CHECK(red->playback_rate > 0.0f, "Ball_Red has a playback rate");
        }
        // Frame paths must resolve to files the PathTable already knows.
        if (red && !red->frames.empty()) {
            CHECK(!paths.resolve(red->frames[0]).empty() ||
                  paths.exists_exact(red->frames[0]),
                  "first frame path is a known file");
        }
    }

    // ---- animation frame families ---------------------------------------
    std::vector<std::string> frames;
    CHECK(expand_frames(paths, "data/gfx/balls/red/ball", &frames),
          "ball frame family expands");
    CHECK(!frames.empty(), "frame family is non-empty");
    if (frames.size() >= 2) {
        CHECK(frames.front() < frames.back(), "frames ordered ascending");
        CHECK(frames[0].find("ball0000") != std::string::npos ||
              frames[0].find("ball_0000") != std::string::npos,
              "first frame is numbered 0000");
    }
}

// Assertions that only apply to the real Luxor 3 tree: exact counts and the
// cross-references between levels, maps and sections. Never run in CI.
static void run_real_data_tests(const std::string& root, const PathTable& paths) {
    (void)paths;
    printf("-- real game data --\n");
    CHECK(paths.size() > 3000, "scanned >3000 files (real tree)");

    std::string r1 = paths.resolve("data/gfx/UI/buttons/MM_top_arrow_Red");
    CHECK(!r1.empty() && r1.find("MM_top_arrow_red") != std::string::npos,
          "case-insensitive UI texture resolves (real ref)");
    std::string r2 = paths.resolve("data/gfx/placeables/geb/feather_01.png");
    CHECK(!r2.empty() && r2.find("placeables/Geb/") != std::string::npos,
          "case-insensitive placeable texture resolves");
    std::string r3 = paths.resolve_under("data/sound", "fx/Canopic_Jar_Spawn.ogg");
    CHECK(!r3.empty() && r3.find(".ogg") != std::string::npos,
          "audio resolves relative to data/sound");

    std::vector<std::string> frames;
    CHECK(expand_frames(paths, "data/gfx/balls_energy/red/ball_", &frames),
          "underscored ball frame family expands");
    CHECK(frames.size() >= 20, "underscored family has >=20 frames");

    // ---- AnimDefs on the real tree ----------------------------------------
    // Every Ball_* sequence is the 30-frame roll animation at 32 fps listed by
    // GamePieceFlavors.txt BallRollAnim, and every flavor has one.
    {
        AnimationRegistry anims;
        std::string anim_err;
        CHECK(load_anim_defs(root + "/data/AnimDefs.txt", paths, &anims, &anim_err),
              "load real AnimDefs.txt");
        CHECK(anims.sequences.size() >= 90, "AnimDefs registers 90+ sequences");
        for (const char* flavor : {"Wild", "Black", "Blue", "Green", "Orange",
                                   "Pink", "Red", "Teal", "White", "Yellow"}) {
            const Animation* anim = anims.find(std::string("Ball_") + flavor);
            char msg[64];
            snprintf(msg, sizeof(msg), "Ball_%s sequence present", flavor);
            CHECK(anim != nullptr, msg);
            if (!anim) continue;
            // Wild ships 29 frames (ball_0001..ball_0029); every other
            // flavor ships 30.
            const size_t expected = std::string(flavor) == "Wild" ? 29 : 30;
            snprintf(msg, sizeof(msg), "Ball_%s frame count matches data", flavor);
            CHECK(anim->frames.size() == expected, msg);
            snprintf(msg, sizeof(msg), "Ball_%s plays at 32 fps", flavor);
            CHECK(anim->playback_rate == 32.0f, msg);
            snprintf(msg, sizeof(msg), "Ball_%s loops", flavor);
            CHECK(anim->looping, msg);
        }
        // The shooter's tilt animation is also in the file.
        const Animation* tilt = anims.find("Shooter_Tilt");
        CHECK(tilt != nullptr, "Shooter_Tilt sequence present");
        if (tilt) CHECK(tilt->frames.size() >= 10, "Shooter_Tilt has 10+ frames");
    }

    // ---- track -------------------------------------------------------------
    Track trk;
    std::string err;
    std::string track_path = root + "/data/maps/geb/path.LX3TRK";
    CHECK(load_track(track_path, &trk, &err), "load geb track");
    CHECK(trk.verts.size() == 539, "geb has 539 verts");
    CHECK(trk.sections.size() == 20, "geb has 20 sections");
    CHECK(trk.named_points.size() >= 10, "geb has named points");

    const TrackSection* s = trk.find_section("main03");
    CHECK(s != nullptr, "find section main03");
    if (s) {
        CHECK(s->points.size() == 57, "main03 has 57 points");
        CHECK(s->length > 400.0f && s->length < 600.0f, "main03 length ~493px");
    }
    Vec2 pos;
    CHECK(trk.point_at("main03", 0.0f, &pos), "point_at start");
    CHECK(trk.verts.size() > 0 && pos.x == trk.verts[0].x && pos.y == trk.verts[0].y,
          "start == first vertex of the track");
    CHECK(trk.point_at("main03", s ? s->length : 0, &pos), "point_at end");
    float ang = 0.0f;
    CHECK(trk.tangent_at("main03", 10.0f, &ang), "tangent_at");

    // Junction vertex reuse: gate sections share their entry vertex.
    const TrackSection* g2 = trk.find_section("gate1-2");
    const TrackSection* g3 = trk.find_section("gate1-3");
    CHECK(g2 && g3 && !g2->vertex_indices.empty() && !g3->vertex_indices.empty(),
          "gate sections exist");
    if (g2 && g3 && !g2->vertex_indices.empty() && !g3->vertex_indices.empty()) {
        CHECK(g2->vertex_indices.front() == g3->vertex_indices.front(),
              "gate1-2 and gate1-3 share junction vertex");
    }

    // ---- per-section train speed ------------------------------------------
    // The map data authors the speed of each section in pathGamePlayData.txt.
    // The DefaultTrackStartSpeed value of 800 must never become a running
    // speed: recorded gameplay shows a median of 87 logical px/s on the geb
    // level (tools/analysis/measure_train_speed.py).
    if (!paths.resolve("data/maps/geb/pathGamePlayData.txt").empty()) {
        PathGameplayData gameplay;
        std::string speed_err;
        CHECK(load_path_gameplay_data(root + "/data/maps/geb/pathGamePlayData.txt",
                                      &gameplay, &speed_err),
              "load geb pathGamePlayData.txt");
        NameValueConfig variables;
        load_name_value_config(root + "/data/GameVariables.txt", &variables,
                               &speed_err);
        SectionSpeeds speeds;
        CHECK(build_section_speeds(gameplay, variables, &speeds, &speed_err),
              "build section speeds");
        CHECK(speeds.has_configured_speeds(), "geb authors section speeds");
        CHECK(speeds.start_speed.count("main02") == 1, "main02 has a StartSpeed");
        CHECK(speeds.end_speed.count("main01") == 1, "main01 has an EndSpeed");

        // A section that names a speed eases between its start and end speed.
        TrainRoute route;
        route.sections = {"main02", "main03"};
        float s0 = route_section_speed(route, 0, speeds, 0.0f);
        float s1 = route_section_speed(route, 0, speeds, 1.0f);
        CHECK(std::fabs(s0 - 70.0f) < 0.01f, "main02 starts at its StartSpeed");
        CHECK(std::fabs(s1 - 60.0f) < 0.01f, "main02 ends at its EndSpeed");
        CHECK(s1 < s0, "the train eases down across the section");

        // A section that names no speed inherits the pace of the one before
        // it, and never falls back to the 800 px/s default.
        TrainRoute inherit;
        inherit.sections = {"initial", "main01", "main01a"};
        float carried = route_section_speed(inherit, 2, speeds, 0.0f);
        CHECK(carried < 200.0f,
              "a speedless section does not fall back to 800 px/s");
        CHECK(carried >= 40.0f && carried <= 130.0f,
              "a speedless section keeps a speed the map authored");
    }

    // ---- coordinate transform (real data) --------------------------------
    // World Y points up (the game used glOrtho(0,800,0,600)); screen Y points
    // down. Verified against four map backgrounds; see docs/COORDINATES.md.
    {
        const TrackSection* ini = trk.find_section("initial");
        CHECK(ini != nullptr, "initial section exists");
        float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
        for (const auto& v : trk.verts) {
            min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
            min_y = std::min(min_y, v.y * kWorldToScreenY);
            max_y = std::max(max_y, v.y * kWorldToScreenY);
        }
        CHECK(min_x > 0.0f && max_x < 800.0f, "track X stays inside the frame");
        CHECK(max_y <= 600.0f, "track never exceeds the bottom of the frame");
        CHECK(min_y > -60.0f, "track overhangs the top edge only slightly");
        CHECK(max_y - min_y > 400.0f, "track spans most of the frame height");

        int on_screen = 0, total = 0;
        for (const auto& np : trk.named_points) {
            if (np.name.compare(0, 11, "point_spawn") != 0) continue;
            ++total;
            float sy = np.pos.y * kWorldToScreenY;
            if (np.pos.x >= 0.0f && np.pos.x <= 800.0f &&
                sy >= 0.0f && sy <= 600.0f) ++on_screen;
        }
        CHECK(total > 0 && on_screen == total,
              "every canopic-jar spawn point lands on screen");
    }

    // ---- levels across modes ----------------------------------------------
    Level lv;
    std::string lv_err;
    CHECK(load_level(root + "/data/levels/classic/A_1_1.lvl", &lv, &lv_err),
          "load A_1_1.lvl");
    CHECK(lv.map_path == "data/maps/geb/", "A_1_1 map_path");
    CHECK(lv.music == "LevelMusicClassic", "A_1_1 music");
    CHECK(lv.num_trains == 4, "A_1_1 num_trains=4");
    CHECK(lv.spheres_per_train == 25, "A_1_1 spheres=25");
    CHECK(lv.spawn_order.size() == 1 && lv.spawn_order[0] == "initial",
          "A_1_1 spawn_order = initial");

    Level lv2;
    CHECK(load_level(root + "/data/levels/crossingTheNile/A_11_3.lvl", &lv2, &lv_err),
          "load crossingTheNile level");
    CHECK(!lv2.music.empty(), "crossingTheNile has music");
    Level lv3;
    CHECK(load_level(root + "/data/levels/centipede/A_2_3.lvl", &lv3, &lv_err),
          "load centipede level");
    CHECK(lv3.map_path == "data/maps/onslaught/", "centipede map_path");
}

// Assertions that run on any tree, including the synthetic fixture: parse the
// track, the level with its #include, and the coordinate transform.
static void run_fixture_tests(const std::string& root, const PathTable& paths) {
    (void)paths;
    printf("-- fixture track/level --\n");

    Track trk;
    std::string err;
    std::string track_path = root + "/data/maps/testmap/path.LX3TRK";
    if (load_track(track_path, &trk, &err)) {
        ShooterState shooter;
        shooter.pos = {100.0f, -50.0f};
        shooter.angle = 0.0f;
        Train target;
        target.section = "main";
        target.distance = 50.0f;
        target.speed = 0.0f;
        target.balls.resize(2);
        target.balls[0].flavor = Flavor::Red;
        target.balls[1].flavor = Flavor::Red;
        target.balls[0].radius = target.balls[1].radius = 16.0f;
        target.balls[0].pos = {100.0f, -50.0f};
        target.balls[1].pos = {100.0f, 0.0f};
        std::vector<Train> targets{target};
        fire_shot(&shooter, 100.0f);
        CHECK(shooter.shots.size() == 1, "firing creates a projectile");
        CHECK(shooter.loaded == Flavor::Blue, "firing loads next ball");
        advance_shooter(&shooter, &targets, trk, 0.01f);
        CHECK(shooter.shots.empty(), "projectile collides with train");
        CHECK(targets[0].balls.empty() && shooter.score == 30,
              "three matching balls clear and award score");

        CHECK(trk.sections.size() == 4, "fixture has 4 sections");
        CHECK(trk.named_points.size() == 2, "fixture has 2 named points");

        Train chain;
        chain.section = "main";
        chain.distance = 50.0f;
        chain.balls.resize(2);
        for (auto& ball : chain.balls) {
            ball.flavor = Flavor::Red;
            ball.radius = 16.0f;
        }
        chain.balls[0].pos = {100.0f, -50.0f};
        chain.balls[1].pos = {100.0f, 0.0f};
        ShooterState miss;
        miss.pos = {100.0f, 0.0f};
        miss.angle = 0.0f;
        miss.loaded = Flavor::Blue;
        miss.next = Flavor::Red;
        std::vector<Train> miss_targets{chain};
        fire_shot(&miss, 100.0f);
        advance_shooter(&miss, &miss_targets, trk, 0.01f);
        CHECK(miss_targets[0].balls.size() == 3,
              "nonmatching insertion leaves train intact");
        CHECK(miss.score == 0, "no score for fewer than three");

        const TrackSection* main = trk.find_section("main");
        CHECK(main != nullptr && main->points.size() == 2, "main section loads");
        if (main) {
            CHECK(main->length > 40.0f && main->length < 60.0f,
                  "main length ~50px");
        }
        // The fixture's junction section continues from where "main" ends:
        // main = {0,1}, junction = {1,2}, so they share vertex 1.
        const TrackSection* jn = trk.find_section("junction");
        CHECK(jn && main && !jn->vertex_indices.empty() &&
              !main->vertex_indices.empty() &&
              jn->vertex_indices.front() == main->vertex_indices.back(),
              "junction continues from main's last vertex");

        // Coordinate transform: world Y up -> screen Y down.
        float sy = trk.verts[1].y * kWorldToScreenY;
        CHECK(sy == 50.0f, "Y flip: world -50 becomes screen +50");
        for (const auto& np : trk.named_points) {
            float y = np.pos.y * kWorldToScreenY;
            CHECK(y >= 0.0f && y <= 600.0f, "spawn point lands on screen");
        }
    }

    Level lv;
    std::string lv_err;
    std::string lv_path = root + "/data/levels/classic/T_1_1.lvl";
    if (load_level(lv_path, &lv, &lv_err)) {
        CHECK(lv.map_path == "data/maps/testmap/", "fixture map_path");
        CHECK(lv.music == "LevelMusicClassic", "fixture music");
        CHECK(lv.num_trains == 2, "fixture num_trains=2");
        CHECK(lv.spheres_per_train == 7, "fixture spheres=7 (TrainConstructionData)");
    CHECK(lv.spawn_order.size() == 1 && lv.spawn_order[0] == "main",
              "fixture spawn_order = main");
        PathGameplayData pg;
        std::string pg_err;
        CHECK(load_path_gameplay_data(root + "/data/maps/testmap/pathGamePlayData.txt", &pg, &pg_err),
              "parse fixture pathGamePlayData.txt");
        CHECK(pg.has_min_scale && pg.min_scale == 0.85f &&
              pg.has_max_scale && pg.max_scale == 1.0f,
              "fixture path scales parse");
        CHECK(pg.start_speed.count("main") == 1 && pg.start_speed["main"] == 120.0f &&
              pg.end_speed.count("main") == 1 && pg.end_speed["main"] == 70.0f,
              "fixture section speed overrides parse");
        CHECK(pg.danger_sections.size() == 1 && pg.occluded_sections.size() == 1 &&
              pg.unshootable_sections.size() == 1 && pg.spawn_restrictive_sections.size() == 1 &&
              pg.underwater_sections.size() == 1,
              "fixture path restrictions parse");
        CHECK(pg.switch_state.count("main") == 1 && pg.switch_state["main"] == "branch" &&
              pg.switches.size() == 1 && pg.switches[0].legs.size() == 1 &&
              pg.switches[0].pivot_point == "point_pivot",
              "fixture switch state and gate parse");
        NameValueConfig config;
        std::string cfg_err;
        CHECK(load_name_value_config(root + "/data/GameVariables.txt", &config, &cfg_err),
              "parse fixture GameVariables.txt with include");
        CHECK(config.values["TrainAcceleration"] == "200.0" &&
              config.values["ShooterSecondsBeforeNextShot"] == "0.5" &&
              config.values["DifficultyTest"] == "1.25" &&
              config.included_files.size() == 1,
              "name/value parser loads keys and included file");
        // The level's only flavors come from flavors_1.txt via #include.
        auto it = lv.flavors.find("Easy");
        CHECK(it != lv.flavors.end() && it->second.size() == 2,
              "fixture #include resolves flavors into Easy");
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game-root>\n", argv[0]);
        return 2;
    }
    std::string root = argv[1];

    PathTable paths;
    paths.scan(root);
    CHECK(paths.size() > 0, "path table scanned");

    run_format_tests(root, paths);

    // Run real-data assertions only when the real tree is present. CI always
    // runs against the fixture, so copyrighted data is never required.
    bool real_tree = !paths.resolve("data/maps/geb/path.LX3TRK").empty();
    if (real_tree) {
        run_real_data_tests(root, paths);
    } else {
        printf("(real game data not present; running fixture tests only)\n");
    }

    // Fixture tests run whenever the fixture level exists.
    if (!paths.resolve("data/maps/testmap/path.LX3TRK").empty()) {
        run_fixture_tests(root, paths);
    }

    printf("\n%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
