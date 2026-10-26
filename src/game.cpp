// Shared game loop.
#include "game.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace luxor {

int run_game(int argc, char** argv, Presenter* p) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game-root> [level.lvl]\n", argv[0]);
        return 2;
    }
    std::string root = argv[1];
    std::string level_path = root + "/data/levels/classic/A_1_1.lvl";
    if (argc > 2) level_path = argv[2];

    long max_frames = -1;
    if (const char* env = getenv("LUXOR_MAX_FRAMES"))
        max_frames = atol(env);

    PathTable paths;
    paths.scan(root);
    log_line("game", "loaded %zu data files", paths.size());

    Level level;
    std::string err;
    if (!load_level(level_path, &level, &err)) {
        log_line("game", "load_level: %s", err.c_str());
        return 1;
    }
    Track track;
    if (!load_track(root + "/" + level.map_path + "path.LX3TRK", &track, &err)) {
        log_line("game", "load_track: %s", err.c_str());
        return 1;
    }
    log_line("game", "level map=%s music=%s sections=%zu",
             level.map_path.c_str(), level.music.c_str(), track.sections.size());

    if (!p->init(kLogicalWidth, kLogicalHeight, &err)) {
        log_line("game", "renderer init failed: %s", err.c_str());
        return 1;
    }

    SceneContext ctx;
    ctx.paths = &paths;
    ctx.track = &track;
    ctx.level = &level;
    ctx.ball_size = 32.0f;
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
    for (int i = 0; i < static_cast<int>(Flavor::Count); ++i)
        ctx.ball_texture[i] = paths.resolve(flavor_files[i]);

    std::string section = track.sections.empty() ? "" : track.sections[0].name;
    for (const auto& s : level.spawn_order) {
        if (track.find_section(s)) { section = s; break; }
    }

    Train train;
    train.section = section;
    train.speed = 400.0f;
    for (int i = 0; i < 10; ++i) {
        GamePiece b;
        b.flavor = static_cast<Flavor>(i % static_cast<int>(Flavor::Count));
        b.radius = 16.0f;
        train.balls.push_back(b);
    }
    std::vector<Train> trains{train};
    TrainTuning tuning;

    int joysticks = SDL_NumJoysticks();
    if (joysticks > 0)
        log_line("input", "%d joystick(s): %s%s", joysticks,
                 SDL_JoystickNameForIndex(0),
                 SDL_IsGameController(0) ? " [gamecontroller]" : "");
    if (SDL_GetNumTouchDevices() > 0)
        log_line("input", "%d touch device(s)", SDL_GetNumTouchDevices());

    // SDL only delivers controller events for opened controllers. Keep the
    // handheld's controller open for the full loop so the Select+Start exit
    // chord works reliably.
    SDL_GameController* game_controller = nullptr;
    SDL_Joystick* joystick = nullptr;
    if (joysticks > 0) {
        if (SDL_IsGameController(0)) {
            game_controller = SDL_GameControllerOpen(0);
            if (!game_controller)
                log_line("input", "SDL_GameControllerOpen failed: %s", SDL_GetError());
        } else {
            joystick = SDL_JoystickOpen(0);
            if (!joystick)
                log_line("input", "SDL_JoystickOpen failed: %s", SDL_GetError());
        }
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);

    bool running = true;
    bool select_down = false;
    bool start_down = false;
    uint32_t last = SDL_GetTicks();
    long frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;

            if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
                (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK ||
                 ev.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
                log_line("input", "controller button down: %u", ev.cbutton.button);
            }

            // muOS maps its Select button to SDL_CONTROLLER_BUTTON_BACK and
            // Start to SDL_CONTROLLER_BUTTON_START. Pressing both together is
            // a reliable in-game exit chord for a handheld port.
            if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                select_down = true;
                if (start_down) {
                    log_line("input", "Select+Start exit chord");
                    running = false;
                }
            }
            if (ev.type == SDL_CONTROLLERBUTTONUP &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                select_down = false;
            }
            if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                start_down = true;
                if (select_down) {
                    log_line("input", "Select+Start exit chord");
                    running = false;
                }
            }
            if (ev.type == SDL_CONTROLLERBUTTONUP &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                start_down = false;
            }
            if (ev.type == SDL_JOYBUTTONDOWN) {
                // Fallback for devices where SDL has no controller mapping:
                // the probe reports Select=back (button 9), Start=button 10.
                if (ev.jbutton.button == 9) {
                    select_down = true;
                    if (start_down) running = false;
                } else if (ev.jbutton.button == 10) {
                    start_down = true;
                    if (select_down) running = false;
                }
            }
            if (ev.type == SDL_JOYBUTTONUP) {
                if (ev.jbutton.button == 9) select_down = false;
                if (ev.jbutton.button == 10) start_down = false;
            }
        }
        uint32_t now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last = now;

        advance_train(&trains[0], track, tuning, dt, 1);

        Scene scene;
        build_scene(ctx, trains, static_cast<float>(now) / 1000.0f, &scene);
        p->present(scene, paths, root);

        if (max_frames >= 0 && ++frame >= max_frames) {
            log_line("game", "reached LUXOR_MAX_FRAMES=%ld", max_frames);
            running = false;
            continue;
        }
        SDL_Delay(16);
    }
    if (game_controller) SDL_GameControllerClose(game_controller);
    if (joystick) SDL_JoystickClose(joystick);
    return 0;
}

}  // namespace luxor
