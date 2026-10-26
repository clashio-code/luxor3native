// Shared game loop.
#include "game.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
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
    ShooterState shooter;
    std::string section = track.sections.empty() ? "" : track.sections[0].name;
    for (const auto& s : level.spawn_order) {
        if (track.find_section(s)) { section = s; break; }
    }
    const TrackSection* play_section = track.find_section(section);
    if (section.empty() || !play_section) {
        log_line("game", "no usable spawn section for level");
        return 1;
    }
    // PROTOTYPE ONLY: this level-driven geometry heuristic is not verified as
    // the original game's shooter placement rule. See docs/GAMEPLAY_RULES.md.
    if (play_section->points.size() >= 2) {
        const Vec2& a = play_section->points[0];
        const Vec2& b = play_section->points[1];
        float mid_x = (a.x + b.x) * 0.5f;
        float tangent = std::atan2(b.y - a.y, b.x - a.x);
        float nx = -std::sin(tangent), ny = std::cos(tangent);
        float aim_x = (nx < 0.0f ? -nx : nx);
        float aim_y = (nx < 0.0f ? -ny : ny);
        float min_y = play_section->points[0].y;
        float max_y = min_y;
        for (const Vec2& point : play_section->points) {
            min_y = std::min(min_y, point.y);
            max_y = std::max(max_y, point.y);
        }
        float shoot_y = (min_y + max_y) * 0.5f;
        // Keep the shooter below the playfield while respecting the screen bounds.
        shooter.pos = {std::max(80.0f, std::min(720.0f, mid_x)),
                       std::max(-580.0f, std::min(-500.0f, shoot_y - 260.0f))};
        set_aim(&shooter, aim_x, aim_y);
    }
    ctx.shooter = &shooter;
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
    // The train walks the whole track through the section junctions, so it
    // never freezes at the end of the section it spawned on.
    TrainRoute route;
    if (!build_route(track, section, &route) || route.sections.empty()) {
        log_line("game", "no route from section '%s'", section.c_str());
        return 1;
    }
    log_line("game", "route: %zu sections from '%s'", route.sections.size(),
             route.sections[0].c_str());

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
    bool fire_down = false;
    bool swap_down = false;
    uint32_t last = SDL_GetTicks();
    long frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_SPACE)
                fire_shot(&shooter);
            if (ev.type == SDL_MOUSEMOTION) {
                float dx = static_cast<float>(ev.motion.x) * kLogicalWidth / 640.0f - shooter.pos.x;
                float dy = -static_cast<float>(ev.motion.y) * kLogicalHeight / 480.0f - shooter.pos.y;
                set_aim(&shooter, dx, dy);
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT)
                fire_shot(&shooter);

            if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_A && !fire_down) {
                fire_shot(&shooter);
                fire_down = true;
            }
            if (ev.type == SDL_CONTROLLERBUTTONUP &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_A)
                fire_down = false;
            if (ev.type == SDL_CONTROLLERBUTTONDOWN &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_X && !swap_down) {
                std::swap(shooter.loaded, shooter.next);
                swap_down = true;
            }
            if (ev.type == SDL_CONTROLLERBUTTONUP &&
                ev.cbutton.button == SDL_CONTROLLER_BUTTON_X)
                swap_down = false;
            // Right-stick aiming is polled below each frame.
            if (ev.type == SDL_JOYBUTTONDOWN) {
                if (ev.jbutton.button == 3) fire_shot(&shooter);
                if (ev.jbutton.button == 6) std::swap(shooter.loaded, shooter.next);
            }
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

        if (!game_controller && joystick) {
            if (SDL_JoystickNumButtons(joystick) > 3) {
                bool down = SDL_JoystickGetButton(joystick, 3) != 0;
                if (down && !fire_down) fire_shot(&shooter);
                fire_down = down;
            }
            if (SDL_JoystickNumButtons(joystick) > 6) {
                bool down = SDL_JoystickGetButton(joystick, 6) != 0;
                if (down && !swap_down) std::swap(shooter.loaded, shooter.next);
                swap_down = down;
            }
        }

        advance_train_route(&trains[0], track, tuning, &route, dt, 1);
        if (game_controller) {
            Sint16 ax = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTX);
            Sint16 ay = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_RIGHTY);
            if (std::abs(ax) > 6000 || std::abs(ay) > 6000)
                set_aim(&shooter, static_cast<float>(ax), -static_cast<float>(ay));
        } else if (joystick) {
            Sint16 ax = SDL_JoystickGetAxis(joystick, 2);
            Sint16 ay = SDL_JoystickGetAxis(joystick, 3);
            set_aim(&shooter, static_cast<float>(ax), -static_cast<float>(ay));
        }
        advance_shooter(&shooter, &trains, track, dt);

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
