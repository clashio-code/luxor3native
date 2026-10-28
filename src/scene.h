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

// ---------------------------------------------------------------- animation
// Picks the frame an animation shows at `time_seconds`. The original engine
// plays each sequence at its own `playbackRate` (frames per second), looping
// when `isLooping 1` and holding the final frame when `isLooping 0`.
// Returns 0 when `anim` has no frames.
size_t animation_frame_index(const Animation& anim, float time_seconds);

// ---------------------------------------------------------------- simulation
// Train motion constants, taken verbatim from the original GameVariables.txt.
struct TrainTuning {
    float start_speed = 800.0f;    // DefaultTrackStartSpeed fallback (not loaded yet)
    float end_speed = 2.0f;        // DefaultTrackEndSpeed fallback (not loaded yet)
    float acceleration = 200.0f;   // TrainAcceleration fallback
    float high_acceleration = 800.0f;
};

// Per-section speed overrides from pathGamePlayData.txt, the values the
// original engine actually uses when a section carries them (26 of 27 maps do;
// the GameVariables defaults apply only where a section names no speed).
// The difficulty TrainSpeedMultiplier from DifficultyGameVariables.txt scales
// everything (0.9/1.0/1.15/1.3, Easy..Insane).
struct SectionSpeeds {
    std::map<std::string, float> start_speed;   // by section name
    std::map<std::string, float> end_speed;
    float start_default = 800.0f;   // DefaultTrackStartSpeed
    float end_default = 2.0f;       // DefaultTrackEndSpeed
    float speed_multiplier = 1.0f;  // per-difficulty TrainSpeedMultiplier

    // Configured values for `section`. An empty map entry falls back to the
    // GameVariables default. The multiplier applies everywhere.
    void for_section(const std::string& section, float* start, float* end) const;

    // True when the map authors at least one speed, i.e. when the
    // DefaultTrackStartSpeed fallback does not apply.
    bool has_configured_speeds() const {
        return !start_speed.empty() || !end_speed.empty();
    }
};

// Reads per-section speeds out of the already-parsed map gameplay data and
// the GameVariables defaults. Both inputs are verified data sources.
bool build_section_speeds(const PathGameplayData& gameplay, const NameValueConfig& variables,
                          SectionSpeeds* out, std::string* err);

// Advances one train along its section. `distance` is arc length in pixels.
// The train accelerates from start_speed toward the point where it should
// stop, exactly like the original: a train slows as it nears the end of its
// section so it can be destroyed there.
// A train that walks the whole track: it keeps a list of the sections it
// still has to travel through, so it does not freeze at the end of the
// section it started on.
struct TrainRoute {
    std::vector<std::string> sections;   // remaining sections, in order
    size_t index = 0;                    // current section within `sections`
};

// Builds a route that starts at `start` and follows the track junctions.
// Returns false when the start section is unknown.
bool build_route(const Track& track, const std::string& start, TrainRoute* out);

// Target speed (px/s) for `section`, given where the train sits in it.
// The original authors each section with a StartSpeed and an EndSpeed in
// pathGamePlayData.txt and interpolate between them along the section; a
// section naming neither inherits the previous section's speed (INFERRED from
// the data layout, see docs/GAMEPLAY_RULES.md), and the first section of a
// route falls back to DefaultTrackStartSpeed.
float route_section_speed(const TrainRoute& route, size_t index,
                          const SectionSpeeds& speeds, float progress);

void advance_train(Train* train, const Track& track, const TrainTuning& tuning,
                   float dt, int trains_alive);
void advance_train_route(Train* train, const Track& track,
                         const TrainTuning& tuning, const SectionSpeeds& speeds,
                         TrainRoute* route, float dt, int trains_alive);
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
    // Animations parsed from data/AnimDefs.txt. When present, ball and shooter
    // sprites are drawn from their animation frames rather than a single fixed
    // texture. Optional: a scene built without it still renders.
    const AnimationRegistry* anims = nullptr;
    // Texture for each ball flavor, resolved to data-relative paths. Used when
    // `anims` lacks the flavor's `Ball_<flavor>` sequence.
    std::string ball_texture[static_cast<int>(Flavor::Count)];
    float ball_size = 32.0f;
    ShooterState* shooter = nullptr;
};

// Builds the draw list for the current simulation state.
void build_scene(const SceneContext& ctx, const std::vector<Train>& trains,
                 float time_seconds, Scene* out);

}  // namespace luxor

#endif  // LUXOR_SCENE_H_
