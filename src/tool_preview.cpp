// Offline preview: builds a small scene from a real level and writes a PNG.
//
//   ./luxor3_preview <game-root> <level.lvl> <out.png> [distance] [time]
//
// This exercises the whole non-GL pipeline: PathTable, level parsing, track
// loading, AnimDefs frame animation, texture decode and rasterization.
// `time` selects the animation frame (seconds); the default 0.0 draws each
// sprite's first frame.
#include "luxor.h"
#include "preview.h"
#include "scene.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace luxor;

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <game-root> <level.lvl> <out.png> [distance] [time]\n",
                argv[0]);
        return 2;
    }
    std::string root = argv[1];
    std::string level_path = argv[2];
    std::string out_path = argv[3];
    float forced_distance = (argc > 4) ? static_cast<float>(atof(argv[4])) : -1.0f;
    float time_seconds = (argc > 5) ? static_cast<float>(atof(argv[5])) : 0.0f;

    PathTable paths;
    paths.scan(root);
    log_line("preview", "path table: %zu files", paths.size());

    Level level;
    std::string err;
    if (!load_level(level_path, &level, &err)) {
        log_line("preview", "load_level failed: %s", err.c_str());
        return 1;
    }
    log_line("preview", "level: map=%s music=%s spawn=%s",
             level.map_path.c_str(), level.music.c_str(),
             level.spawn_order.empty() ? "(none)" : level.spawn_order[0].c_str());

    Track track;
    std::string track_path = root + "/" + level.map_path + "path.LX3TRK";
    if (!load_track(track_path, &track, &err)) {
        log_line("preview", "load_track failed: %s: %s", track_path.c_str(), err.c_str());
        return 1;
    }
    log_line("preview", "track: %zu verts, %zu sections",
             track.verts.size(), track.sections.size());

    // Pick the first spawn section that exists.
    std::string section = track.sections.empty() ? "" : track.sections[0].name;
    for (const auto& s : level.spawn_order) {
        if (track.find_section(s)) { section = s; break; }
    }
    const TrackSection* sec = track.find_section(section);
    if (!sec) {
        log_line("preview", "no usable section in %s", level.map_path.c_str());
        return 1;
    }
    log_line("preview", "using section %s (len %.1fpx)", section.c_str(), sec->length);

    // Resolve one ball texture per flavor so a color set shows up.
    SceneContext ctx;
    ctx.paths = &paths;
    ctx.track = &track;
    ctx.level = &level;
    ctx.ball_size = 32.0f;

    // AnimDefs.txt, when present, drives which frame each sprite shows.
    AnimationRegistry anims;
    if (!paths.resolve("data/AnimDefs.txt").empty()) {
        std::string anim_err;
        if (load_anim_defs(root + "/data/AnimDefs.txt", paths, &anims, &anim_err)) {
            ctx.anims = &anims;
            log_line("preview", "anim defs: %zu sequences",
                     anims.sequences.size());
        } else {
            log_line("preview", "anim defs failed to load: %s", anim_err.c_str());
        }
    }
    // Preferred texture per flavor, taken from the real game data. The
    // synthetic CI fixture only ships one ball, so fall back to it.
    const char* flavor_files[] = {
        "data/gfx/balls/wild/ball_0013.png",
        "data/gfx/balls/black/ball0007.png",
        "data/gfx/balls/blue/ball0008.png",
        "data/gfx/balls/green/ball0021.png",
        "data/gfx/balls/orange/ball0003.png",
        "data/gfx/balls/pink/ball0009.png",
        "data/gfx/balls/red/ball0004.png",
        "data/gfx/balls/teal/ball0025.png",
        "data/gfx/balls/white/ball0016.png",
        "data/gfx/balls/yellow/ball0025.png",
    };
    std::string fallback;
    for (int i = 0; i < static_cast<int>(Flavor::Count); ++i) {
        ctx.ball_texture[i] = paths.resolve(flavor_files[i]);
        if (ctx.ball_texture[i].empty()) {
            if (fallback.empty())
                fallback = paths.resolve("data/gfx/balls/red/ball0000.png");
            ctx.ball_texture[i] = fallback;
        }
    }

    // Build one train of spheres.
    Train train;
    train.section = section;
    train.distance = 120.0f;
    train.speed = 400.0f;
    const int kBalls = 10;
    for (int i = 0; i < kBalls; ++i) {
        GamePiece b;
        b.flavor = static_cast<Flavor>(i % static_cast<int>(Flavor::Count));
        b.radius = 16.0f;
        train.balls.push_back(b);
    }
    if (forced_distance >= 0.0f) train.distance = forced_distance;

    TrainTuning tuning;
    advance_train(&train, track, tuning, 0.0f, 1);  // positions only
    // place balls manually at the forced distance
    {
        float spacing = 32.0f;
        for (size_t i = 0; i < train.balls.size(); ++i) {
            float s = train.distance - static_cast<float>(i) * spacing;
            if (s < 0.0f) s = 0.0f;
            // World space (Y up). build_scene() converts to screen space.
            Vec2 p;
            if (track.point_at(section, s, &p)) train.balls[i].pos = p;
            float a;
            if (track.tangent_at(section, s, &a)) train.balls[i].angle = a;
        }
    }

    std::vector<Train> trains{train};
    Scene scene;
    build_scene(ctx, trains, time_seconds, &scene);
    log_line("preview", "scene: %zu sprites, background=%s",
             scene.items.size(), scene.background.empty() ? "(none)" : scene.background.c_str());

    Framebuffer fb;
    fb.width = kLogicalWidth;
    fb.height = kLogicalHeight;
    if (!render_scene(scene, paths, root, &fb, &err)) {
        log_line("preview", "render failed: %s", err.c_str());
        return 1;
    }
    if (!write_png(fb, out_path, &err)) {
        log_line("preview", "write failed: %s", err.c_str());
        return 1;
    }
    log_line("preview", "wrote %s (%dx%d)", out_path.c_str(), fb.width, fb.height);
    return 0;
}
