// Luxor3 Native - shared declarations.
//
// The core (asset loading, track geometry, game state) is deliberately free of
// SDL2 so it can be unit-tested headless on a build host. SDL2 + GLES2 are
// only the presentation layer (see sdl_gl.h).
#ifndef LUXOR_H_
#define LUXOR_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace luxor {

// Original art resolution. The RG40XXV panel is 640x480 (4:3), same aspect
// ratio, so the logical frame maps onto it with a uniform 0.8x downscale.
constexpr int kLogicalWidth = 800;
constexpr int kLogicalHeight = 600;

// The original game drew with glOrtho(0, 800, 0, 600), i.e. world Y points
// UP. Track data follows that convention: y = -20 is near the top of the
// screen and y = -485 near the bottom. Our screen space has Y pointing DOWN,
// so the flip is a simple negation. Verified against the geb map artwork:
// with screen_y = -world_y the track polyline follows the carved stone
// channels exactly; any other offset puts the track off-canvas.
constexpr float kWorldToScreenY = -1.0f;

// ---------------------------------------------------------------- logging
void log_line(const char* tag, const char* fmt, ...);

// ---------------------------------------------------------------- paths
// All asset paths in the original data use mixed separators and inconsistent
// capitalisation (e.g. "data\gfx\UI\buttons\MM_top_arrow_Red" while the file
// on disk is "data/gfx/UI/buttons/MM_top_arrow_red.png"). Windows tolerated
// that; a case-sensitive Linux filesystem does not. Every lookup therefore
// goes through a case-insensitive resolver.
class PathTable {
public:
    // Scans <root> recursively and records every file path, indexed by its
    // lower-cased form.
    void scan(const std::string& root);

    // Resolves a data-relative reference such as
    // "data/gfx/UI/buttons/MM_top_arrow_Red" to a real file on disk.
    // Tries, in order: exact path, case-insensitive match, and the same with
    // a missing image/audio extension appended. Returns "" when unresolved.
    std::string resolve(const std::string& ref) const;

    // Resolves a reference relative to a base directory inside the tree, e.g.
    // resolve_under("data/sound", "fx/Canopic_Jar_Spawn.ogg"). Audio and some
    // other references are relative to a sub-root rather than to the tree root.
    std::string resolve_under(const std::string& base, const std::string& ref) const;

    // True when the reference names an existing file without needing an
    // extension probe.
    bool exists_exact(const std::string& ref) const;

    // All files whose lower-cased path starts with `prefix` (used to expand an
    // animation frame family such as "data/gfx/balls/red/ball").
    std::vector<std::string> family(const std::string& prefix) const;

    size_t size() const { return entries_.size(); }

private:
    std::vector<std::string> entries_;   // real relative paths
    std::vector<std::string> lowered_;   // parallel, lower-cased
};

// ---------------------------------------------------------------- image
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;         // 4 = RGBA
    std::vector<uint8_t> rgba; // width*height*4, top-down
    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Decodes PNG/JPEG from memory. Returns an invalid Image on failure.
Image decode_image(const uint8_t* data, size_t size);

// ---------------------------------------------------------------- track
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct TrackSection {
    std::string name;
    std::vector<int> vertex_indices;
    std::vector<Vec2> points;      // resolved polyline
    float length = 0.0f;           // arc length in pixels
};

struct NamedPoint {
    std::string name;
    Vec2 pos;
};

struct Track {
    std::vector<Vec2> verts;
    std::vector<TrackSection> sections;
    std::vector<NamedPoint> named_points;
    // Track section junctions: section-name -> section names sharing its end vertex.
    // Derived from the source vertex indices while parsing LX3TRK.
    std::map<std::string, std::vector<std::string>> next_sections;

    const TrackSection* find_section(const std::string& name) const;
    std::vector<std::string> sections_after(const std::string& name) const;
    const NamedPoint* find_named(const std::string& name) const;

    // Position along a section at arc-length `s` pixels from its start.
    // Returns false when the section is unknown.
    bool point_at(const std::string& section, float s, Vec2* out) const;
    // Tangent direction (radians) at arc-length `s`.
    bool tangent_at(const std::string& section, float s, float* out) const;
};

bool load_track(const std::string& path, Track* out, std::string* err);

// Map-specific gameplay metadata from pathGamePlayData.txt. The source file
// provides per-section speed overrides, switch routing, and restrictions.
// Parsing these values does not by itself reproduce the original runtime rules.
struct TrackSwitchLeg {
    std::string section;
    std::string named_point;
};
struct TrackSwitch {
    std::string pivot_section;
    std::string pivot_point;
    std::vector<TrackSwitchLeg> legs;
};
struct PathGameplayData {
    float min_scale = 0.0f;
    float max_scale = 0.0f;
    bool has_min_scale = false;
    bool has_max_scale = false;
    std::map<std::string, float> start_speed;
    std::map<std::string, float> end_speed;
    std::vector<std::string> danger_sections;
    std::vector<std::string> occluded_sections;
    std::vector<std::string> unshootable_sections;
    std::vector<std::string> spawn_restrictive_sections;
    std::vector<std::string> underwater_sections;
    std::map<std::string, std::string> switch_state;
    std::vector<TrackSwitch> switches;
    std::vector<std::string> unknown_directives;
};
bool load_path_gameplay_data(const std::string& path, PathGameplayData* out,
                             std::string* err);

// Generic name/value config parser for GameVariables.txt and similar files.
// Values stay textual intentionally; callers must interpret types per key.
struct NameValueConfig {
    std::map<std::string, std::string> values;
    std::vector<std::string> included_files;
};
bool load_name_value_config(const std::string& path, NameValueConfig* out,
                            std::string* err);

// ---------------------------------------------------------------- animation
// An animation is a list of frames plus playback metadata, exactly as the
// original AnimDefs.txt describes it.
struct Animation {
    std::string name;
    std::vector<std::string> frames;   // resolved file paths
    float playback_rate = 24.0f;        // frames per second
    bool looping = false;
    float weight = 1.0f;
};

// The animation registry read from data/AnimDefs.txt. The original file holds
// anonymous `Sequence { ... }` blocks; several blocks may share a name, in
// which case the engine picks one at random, weighted by `weight`.
struct AnimationRegistry {
    // Every sequence in file order, duplicates included.
    std::vector<Animation> sequences;
    // Sequence indices grouped by name, in file order.
    std::map<std::string, std::vector<size_t>> by_name;

    // The first sequence registered under `name`, or nullptr.
    const Animation* find(const std::string& name) const;
};

// Parses AnimDefs.txt. `paths` resolves each `fileName` prefix into the
// concrete frame files it names. Returns false when the file cannot be read.
bool load_anim_defs(const std::string& path, const PathTable& paths,
                    AnimationRegistry* out, std::string* err);

// Expands one "fileName" prefix into its frame files. The original stores
// frames as <prefix>NNNN.png; the numbering width and separator vary
// (ball0000.png vs ball_0000.png), so the family is matched by prefix.
bool expand_frames(const PathTable& paths, const std::string& prefix,
                   std::vector<std::string>* out);

// ---------------------------------------------------------------- game state
enum class Flavor : uint8_t {
    Wild = 0, Black, Blue, Green, Orange, Pink, Red, Teal, White, Yellow, Count
};

const char* flavor_name(Flavor f);
bool flavor_from_name(const std::string& name, Flavor* out);

struct GamePiece {
    Flavor flavor = Flavor::Yellow;
    Vec2 pos;
    float angle = 0.0f;
    float radius = 16.0f;
};

// A train is a run of balls travelling along a track section.
struct Train {
    std::string section;
    float distance = 0.0f;       // arc length travelled along the section
    float speed = 0.0f;          // pixels per second
    std::vector<GamePiece> balls;
};

struct Level {
    std::string map_path;                   // e.g. "data/maps/geb/"
    std::string display_name;              // e.g. "#geb"
    std::string music;                     // e.g. "LevelMusicClassic"
    std::vector<std::string> spawn_order;  // track sections trains start from
    int num_trains = 4;
    int spheres_per_train = 25;
    float color_spawn_streak = 0.5f;
    // Allowed ball flavors per difficulty. The key is the difficulty name
    // ("Easy", "Medium", "Hard", "Insane"); the value is the flavor list.
    // These come from the flavors_N.txt files pulled in by #include.
    std::map<std::string, std::vector<std::string>> flavors;
};

bool load_level(const std::string& path, Level* out, std::string* err);

int run(int argc, char** argv);

}  // namespace luxor

#endif  // LUXOR_H_
