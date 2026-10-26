// Scene description shared by every rendering backend.
//
// The GLES2 backend and the offline preview backend consume the same scene,
// so a preview PNG can never drift from what the device draws.
#ifndef LUXOR_SCENE_H_
#define LUXOR_SCENE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "luxor.h"

namespace luxor {

// Identifies a texture by its resolved path inside the data tree. Backends map
// the path to their own handle type.
struct DrawItem {
    std::string texture;   // resolved path, e.g. "data/gfx/balls/red/ball0007.png"
    float x = 0.0f;        // logical pixels, origin top-left
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float angle = 0.0f;    // radians, around the item's centre
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct Scene {
    std::vector<DrawItem> items;
    std::vector<DrawItem> overlay_items;  // projectiles and shooter, drawn above train
    int score = 0;
    std::string score_text;
    // Background is drawn first, stretched to the logical frame.
    std::string background;
    // Debug overlay: the track polyline, drawn as thin quads.
    std::vector<std::string> track_lines;   // "x1,y1,x2,y2" per segment
};

// ---------------------------------------------------------------- simulation
// Train motion constants, taken verbatim from the original GameVariables.txt.
struct TrainTuning {
    float start_speed = 800.0f;    // DefaultTrackStartSpeed
    float end_speed = 2.0f;        // DefaultTrackEndSpeed
    float acceleration = 200.0f;   // TrainAcceleration
    float high_acceleration = 800.0f;
};

// Advances one train along its section. `distance` is arc length in pixels.
// The train accelerates from start_speed toward the point where it should
// stop, exactly like the original: a train slows as it nears the end of its
// section so it can be destroyed there.
void advance_train(Train* train, const Track& track, const TrainTuning& tuning,
                   float dt, int trains_alive);

// First playable shooter loop: projectiles travel toward the train, insert a
// ball at contact, and remove runs of three or more matching colors.
struct Projectile {
    Vec2 pos;
    Vec2 velocity;
    Flavor flavor = Flavor::Yellow;
    float radius = 10.0f;
    float age = 0.0f;
};
struct ShooterState {
    Vec2 pos{400.0f, -500.0f};  // world coordinates
    float angle = 0.785398163f;  // radians, world Y up; points into play area
    float aim_x = 0.70710678f;
    float aim_y = 0.70710678f;
    Flavor loaded = Flavor::Red;
    Flavor next = Flavor::Blue;
    int score = 0;
    std::vector<Projectile> shots;
};
void fire_shot(ShooterState* shooter, float speed = 700.0f);
void set_aim(ShooterState* shooter, float x, float y);
void advance_shooter(ShooterState* shooter, std::vector<Train>* trains,
                     const Track& track, float dt);

struct SceneContext {
    PathTable* paths = nullptr;
    const Track* track = nullptr;
    const Level* level = nullptr;
    // Texture for each ball flavor, resolved to data-relative paths.
    std::string ball_texture[static_cast<int>(Flavor::Count)];
    float ball_size = 32.0f;
    ShooterState* shooter = nullptr;
};

// Builds the draw list for the current simulation state.
void build_scene(const SceneContext& ctx, const std::vector<Train>& trains,
                 float time_seconds, Scene* out);

}  // namespace luxor

#endif  // LUXOR_SCENE_H_
