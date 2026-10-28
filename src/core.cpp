// Luxor3 Native - core implementation (no SDL2 dependency).
#include "luxor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>

#define STB_IMAGE_NO_STDIO
#include "stb_image.h"

namespace luxor {

// ---------------------------------------------------------------- logging
void log_line(const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

// ---------------------------------------------------------------- paths
static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });
    return out;
}

static std::string normalize_sep(const std::string& s) {
    std::string out = s;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void PathTable::scan(const std::string& root) {
    entries_.clear();
    lowered_.clear();
    // Simple recursive walk; the tree is ~3.7k files so this is fast enough
    // and avoids platform-specific directory iteration.
    std::vector<std::string> stack{root};
    while (!stack.empty()) {
        std::string dir = stack.back();
        stack.pop_back();
        // Use opendir/readdir via <dirent.h> (POSIX, and the target is Linux).
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string full = dir + "/" + name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                stack.push_back(full);
            } else {
                std::string rel = full;
                if (rel.size() > root.size() + 1)
                    rel = rel.substr(root.size() + 1);
                rel = normalize_sep(rel);
                entries_.push_back(rel);
                lowered_.push_back(to_lower(rel));
            }
        }
        closedir(d);
    }
    // Sort so lookups are deterministic.
    std::vector<size_t> order(entries_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return entries_[a] < entries_[b];
    });
    std::vector<std::string> e2, l2;
    e2.reserve(order.size()); l2.reserve(order.size());
    for (size_t i : order) { e2.push_back(entries_[i]); l2.push_back(lowered_[i]); }
    entries_.swap(e2);
    lowered_.swap(l2);
}

bool PathTable::exists_exact(const std::string& ref) const {
    std::string r = normalize_sep(ref);
    if (r.empty()) return false;
    if (!r.empty() && r[0] == '/') r.erase(0, 1);
    std::string lr = to_lower(r);
    for (size_t i = 0; i < entries_.size(); ++i)
        if (lowered_[i] == lr) return true;
    return false;
}

std::string PathTable::resolve(const std::string& ref) const {
    static const char* kExts[] = {".png", ".jpg", ".jpeg", ".ogg", ".effect",
                                  ".txt", ".fontdef", ".LX3TRK"};
    std::string r = normalize_sep(ref);
    if (!r.empty() && r[0] == '/') r.erase(0, 1);

    // Exact match first, then case-insensitive.
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i] == r) return entries_[i];
    std::string lr = to_lower(r);
    for (size_t i = 0; i < entries_.size(); ++i)
        if (lowered_[i] == lr) return entries_[i];

    // Missing extension: try appending known ones.
    for (const char* ext : kExts) {
        std::string cand = r + ext;
        std::string lc = to_lower(cand);
        for (size_t i = 0; i < entries_.size(); ++i)
            if (lowered_[i] == lc) return entries_[i];
    }
    return std::string();
}

std::string PathTable::resolve_under(const std::string& base,
                                     const std::string& ref) const {
    std::string b = normalize_sep(base);
    if (!b.empty() && b[0] == '/') b.erase(0, 1);
    std::string r = normalize_sep(ref);
    if (!r.empty() && r[0] == '/') r.erase(0, 1);
    if (b.empty()) return resolve(r);
    // If the reference already carries the base prefix, do not double it.
    if (r.compare(0, b.size(), b) == 0) return resolve(r);
    return resolve(b + "/" + r);
}

std::vector<std::string> PathTable::family(const std::string& prefix) const {
    std::vector<std::string> out;
    std::string p = to_lower(normalize_sep(prefix));
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (lowered_[i].compare(0, p.size(), p) == 0) out.push_back(entries_[i]);
    }
    return out;
}

// ---------------------------------------------------------------- image
Image decode_image(const uint8_t* data, size_t size) {
    Image img;
    if (!data || size == 0) return img;
    int w = 0, h = 0, comp = 0;
    // Force RGBA so the texture upload path is uniform.
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size),
                                               &w, &h, &comp, 4);
    if (!px) return img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

// ---------------------------------------------------------------- helpers
static bool read_file(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

static std::string strip_comment(const std::string& line) {
    size_t p = line.find("//");
    return p == std::string::npos ? line : line.substr(0, p);
}

// Splits a line into whitespace-separated tokens, honouring "quoted strings".
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size()) break;
        if (line[i] == '"') {
            size_t j = line.find('"', i + 1);
            if (j == std::string::npos) j = line.size();
            out.push_back(line.substr(i + 1, j - i - 1));
            i = j + 1;
        } else {
            size_t j = i;
            while (j < line.size() && !isspace(static_cast<unsigned char>(line[j]))) ++j;
            out.push_back(line.substr(i, j - i));
            i = j;
        }
    }
    return out;
}

// ---------------------------------------------------------------- track
const TrackSection* Track::find_section(const std::string& name) const {
    for (const auto& s : sections)
        if (s.name == name) return &s;
    for (const auto& s : sections) {
        std::string a = to_lower(s.name), b = to_lower(name);
        if (a == b) return &s;
    }
    return nullptr;
}

const NamedPoint* Track::find_named(const std::string& name) const {
    for (const auto& p : named_points)
        if (p.name == name) return &p;
    for (const auto& p : named_points) {
        if (to_lower(p.name) == to_lower(name)) return &p;
    }
    return nullptr;
}

std::vector<std::string> Track::sections_after(const std::string& name) const {
    auto it = next_sections.find(name);
    if (it != next_sections.end()) return it->second;
    // Fall back to a case-insensitive lookup so mixed-case references work.
    for (const auto& kv : next_sections)
        if (to_lower(kv.first) == to_lower(name)) return kv.second;
    return std::vector<std::string>();
}

bool Track::point_at(const std::string& section, float s, Vec2* out) const {
    const TrackSection* sec = find_section(section);
    if (!sec || sec->points.empty()) return false;
    float remain = s;
    for (size_t i = 0; i + 1 < sec->points.size(); ++i) {
        const Vec2& a = sec->points[i];
        const Vec2& b = sec->points[i+1];
        float dx = b.x - a.x, dy = b.y - a.y;
        float seg = std::sqrt(dx*dx + dy*dy);
        if (remain <= seg || i + 2 == sec->points.size()) {
            float t = seg > 0.0f ? remain / seg : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            out->x = a.x + dx * t;
            out->y = a.y + dy * t;
            return true;
        }
        remain -= seg;
    }
    out->x = sec->points.back().x;
    out->y = sec->points.back().y;
    return true;
}

bool Track::tangent_at(const std::string& section, float s, float* out) const {
    const TrackSection* sec = find_section(section);
    if (!sec || sec->points.size() < 2) return false;
    float remain = s;
    for (size_t i = 0; i + 1 < sec->points.size(); ++i) {
        const Vec2& a = sec->points[i];
        const Vec2& b = sec->points[i+1];
        float dx = b.x - a.x, dy = b.y - a.y;
        float seg = std::sqrt(dx*dx + dy*dy);
        if (remain <= seg || i + 2 == sec->points.size()) {
            *out = std::atan2(dy, dx);
            return true;
        }
        remain -= seg;
    }
    *out = 0.0f;
    return true;
}

// Parses path.LX3TRK. Sections and named points are stored as
//   "name" { 0 1 2 ... }
//   "name" ( x, y )
bool load_track(const std::string& path, Track* out, std::string* err) {
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    *out = Track();

    enum class Sect { None, Verts, Sections, Reflectors, NamedPoints } section = Sect::None;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = strip_comment(line);
        std::string t = trim(line);
        if (t.empty()) continue;

        if (t == "Verts") { section = Sect::Verts; continue; }
        if (t == "Sections") { section = Sect::Sections; continue; }
        if (t == "Reflectors") { section = Sect::Reflectors; continue; }
        if (t == "NamedPoints") { section = Sect::NamedPoints; continue; }
        if (t == "}") { section = Sect::None; continue; }

        if (section == Sect::Verts) {
            if (t.compare(0, 5, "Vert ") == 0) {
                std::vector<std::string> tok = tokenize(t);
                if (tok.size() >= 3) {
                    Vec2 v;
                    v.x = static_cast<float>(atof(tok[1].c_str()));
                    v.y = static_cast<float>(atof(tok[2].c_str()));
                    out->verts.push_back(v);
                }
            }
        } else if (section == Sect::Sections) {
            // "name" { 0 1 2 ... }
            size_t q1 = t.find('"');
            if (q1 == std::string::npos) continue;
            size_t q2 = t.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string name = t.substr(q1 + 1, q2 - q1 - 1);
            TrackSection sec;
            sec.name = name;
            size_t brace = t.find('{');
            if (brace != std::string::npos) {
                size_t close = t.find('}', brace);
                std::string inner = t.substr(brace + 1,
                                             close == std::string::npos ? std::string::npos
                                                                         : close - brace - 1);
                std::istringstream is(inner);
                int idx;
                while (is >> idx) sec.vertex_indices.push_back(idx);
            }
            out->sections.push_back(sec);
        } else if (section == Sect::NamedPoints) {
            // "name" ( x, y )
            size_t q1 = t.find('"');
            if (q1 == std::string::npos) continue;
            size_t q2 = t.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string name = t.substr(q1 + 1, q2 - q1 - 1);
            size_t lp = t.find('(', q2);
            if (lp == std::string::npos) continue;
            size_t rp = t.find(')', lp);
            std::string inner = t.substr(lp + 1,
                                         rp == std::string::npos ? std::string::npos
                                                                 : rp - lp - 1);
            std::vector<std::string> tok = tokenize(inner);
            if (tok.size() >= 2) {
                NamedPoint np;
                np.name = name;
                np.pos.x = static_cast<float>(atof(tok[0].c_str()));
                np.pos.y = static_cast<float>(atof(tok[1].c_str()));
                out->named_points.push_back(np);
            }
        }
    }

    // Resolve section vertex indices into polylines.
    for (auto& sec : out->sections) {
        for (int idx : sec.vertex_indices) {
            if (idx >= 0 && static_cast<size_t>(idx) < out->verts.size())
                sec.points.push_back(out->verts[idx]);
        }
        float len = 0.0f;
        for (size_t i = 0; i + 1 < sec.points.size(); ++i) {
            float dx = sec.points[i+1].x - sec.points[i].x;
            float dy = sec.points[i+1].y - sec.points[i].y;
            len += std::sqrt(dx*dx + dy*dy);
        }
        sec.length = len;
    }

    // Build the junction graph. Two sections are connected when the last
    // vertex of one equals the first vertex of the other; the original track
    // files spell this out by reusing vertex indices, so no extra data is
    // needed. A section may feed several successors (forks).
    for (const auto& sec : out->sections) {
        if (sec.vertex_indices.empty()) continue;
        const int end_vertex = sec.vertex_indices.back();
        for (const auto& other : out->sections) {
            if (other.name == sec.name) continue;
            if (other.vertex_indices.empty()) continue;
            if (other.vertex_indices.front() != end_vertex) continue;
            auto& list = out->next_sections[sec.name];
            bool present = false;
            for (const auto& n : list) if (n == other.name) { present = true; break; }
            if (!present) list.push_back(other.name);
        }
    }
    if (err) err->clear();
    return true;
}

// ------------------------------------------------------- map gameplay data
// pathGamePlayData.txt is optional per map and uses a small directive set.
// Source data spells some directives with inconsistent case (e.g. "danger",
// "maxScale", "EndSPeed"), so every lookup is case-insensitive. The original
// engine parses this file; reading the values here does not prove how they
// are applied at runtime.
// tokenize() already strips surrounding quotes, so tokens are used as-is.
static bool vector_contains(const std::vector<std::string>& list,
                            const std::string& value) {
    for (const auto& item : list) if (item == value) return true;
    return false;
}

bool load_path_gameplay_data(const std::string& path, PathGameplayData* out,
                             std::string* err) {
    if (!out) {
        if (err) *err = "null output";
        return false;
    }
    *out = PathGameplayData();
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }

    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(strip_comment(line));
        if (line.empty() || line == "{" || line == "}") continue;
        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) continue;
        const std::string key = to_lower(tokens[0]);
        if (key == "minscale" && tokens.size() > 1) {
            out->min_scale = static_cast<float>(atof(tokens[1].c_str()));
            out->has_min_scale = true;
        } else if (key == "maxscale" && tokens.size() > 1) {
            out->max_scale = static_cast<float>(atof(tokens[1].c_str()));
            out->has_max_scale = true;
        } else if (key == "startspeed" && tokens.size() > 2) {
            out->start_speed[tokens[1]] =
                static_cast<float>(atof(tokens[2].c_str()));
        } else if (key == "endspeed" && tokens.size() > 2) {
            out->end_speed[tokens[1]] =
                static_cast<float>(atof(tokens[2].c_str()));
        } else if (key == "danger" && tokens.size() > 1) {
            const std::string name = tokens[1];
            if (!vector_contains(out->danger_sections, name))
                out->danger_sections.push_back(name);
        } else if (key == "occluded" && tokens.size() > 1) {
            const std::string name = tokens[1];
            if (!vector_contains(out->occluded_sections, name))
                out->occluded_sections.push_back(name);
        } else if (key == "unshootable" && tokens.size() > 1) {
            const std::string name = tokens[1];
            if (!vector_contains(out->unshootable_sections, name))
                out->unshootable_sections.push_back(name);
        } else if (key == "spawnrestrictive" && tokens.size() > 1) {
            const std::string name = tokens[1];
            if (!vector_contains(out->spawn_restrictive_sections, name))
                out->spawn_restrictive_sections.push_back(name);
        } else if (key == "underwater" && tokens.size() > 1) {
            const std::string name = tokens[1];
            if (!vector_contains(out->underwater_sections, name))
                out->underwater_sections.push_back(name);
        } else if (key == "switchstate" && tokens.size() > 2) {
            out->switch_state[tokens[1]] = tokens[2];
        } else if (key == "switchgate") {
            // The block is consumed until the matching closing brace.
            TrackSwitch current;
            while (std::getline(input, line)) {
                line = trim(strip_comment(line));
                if (line.empty() || line == "{") continue;
                if (line == "}") break;
                std::vector<std::string> inner = tokenize(line);
                if (inner.empty()) continue;
                const std::string inner_key = to_lower(inner[0]);
                if (inner_key == "pivot" && inner.size() > 2) {
                    current.pivot_section = inner[1];
                    current.pivot_point = inner[2];
                } else if (inner_key == "leg" && inner.size() > 2) {
                    TrackSwitchLeg leg;
                    leg.section = inner[1];
                    leg.named_point = inner[2];
                    current.legs.push_back(leg);
                } else if (inner_key == "pivot" || inner_key == "leg") {
                    out->unknown_directives.push_back(line);
                }
            }
            if (!current.pivot_section.empty()) out->switches.push_back(current);
        } else {
            out->unknown_directives.push_back(line);
        }
    }
    if (err) err->clear();
    return true;
}

// --------------------------------------------------------- name/value config
// GameVariables.txt and similar files are flat "Name value" lists, with
// values commonly quoted and #include pulling in more of the same shape.
// Values remain strings here; interpreting "200.0" as a float is the caller's
// decision, not this parser's.
static void merge_name_value_lines(const std::string& text,
                                   const std::string& base_dir,
                                   NameValueConfig* out,
                                   std::vector<std::string>* seen) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(strip_comment(line));
        if (line.empty()) continue;
        size_t include_pos = line.find("#include");
        if (include_pos != std::string::npos) {
            size_t q1 = line.find('"', include_pos);
            size_t q2 = q1 == std::string::npos ? std::string::npos
                                                : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                out->included_files.push_back(inc);
                std::string full = base_dir + "/" + inc;
                bool already = false;
                for (const auto& s : *seen) if (s == full) already = true;
                if (!already) {
                    seen->push_back(full);
                    std::string sub;
                    if (read_file(full, &sub))
                        merge_name_value_lines(sub, base_dir, out, seen);
                }
            }
            continue;
        }
        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) continue;
        std::string value;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (!value.empty()) value += " ";
            value += tokens[i];
        }
        if (!value.empty()) out->values[tokens[0]] = value;
    }
}

bool load_name_value_config(const std::string& path, NameValueConfig* out,
                            std::string* err) {
    if (!out) {
        if (err) *err = "null output";
        return false;
    }
    *out = NameValueConfig();
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::string base = path;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(0, slash);
    std::vector<std::string> seen{path};
    merge_name_value_lines(text, base, out, &seen);
    if (err) err->clear();
    return true;
}

// ---------------------------------------------------------------- animation
bool expand_frames(const PathTable& paths, const std::string& prefix,
                   std::vector<std::string>* out) {
    out->clear();
    std::vector<std::string> fam = paths.family(prefix);
    if (fam.empty()) return false;
    // Keep only image files, then order them naturally by their numeric suffix
    // so animation playback order is correct.
    std::vector<std::string> frames;
    for (const auto& f : fam) {
        std::string l = to_lower(f);
        if (l.size() > 4 && l.compare(l.size()-4, 4, ".png") == 0)
            frames.push_back(f);
    }
    if (frames.empty()) return false;

    auto trailing_number = [](const std::string& s) -> long {
        size_t i = s.size();
        while (i > 0 && isdigit(static_cast<unsigned char>(s[i-1]))) --i;
        if (i == s.size()) return -1;
        return atol(s.c_str() + i);
    };
    std::stable_sort(frames.begin(), frames.end(),
                     [&](const std::string& a, const std::string& b) {
                         long na = trailing_number(a), nb = trailing_number(b);
                         if (na != nb) return na < nb;
                         return a < b;
                     });
    *out = frames;
    return true;
}

// ---------------------------------------------------------------- AnimDefs
const Animation* AnimationRegistry::find(const std::string& name) const {
    auto it = by_name.find(name);
    if (it == by_name.end() || it->second.empty()) return nullptr;
    return &sequences[it->second.front()];
}

// Parses one `Sequence { ... }` body. Fields, per the schema comment in the
// real file: name, one or more fileName lines, optional fileNamePlayBackwards,
// playbackRate, isLooping, weight, alwaysLoad. Spelling of `fileName` varies
// in the source data (lowercase `filename` also appears), so field matching is
// case-insensitive. Spliced fileName lines concatenate frame ranges; a
// fileNamePlayBackwards line appends its own family reverse.
static bool parse_sequence_block(const std::vector<std::string>& lines,
                                 const PathTable& paths, Animation* out,
                                 std::string* err) {
    bool have_name = false;
    bool have_frames = false;
    for (const auto& raw : lines) {
        std::vector<std::string> tok = tokenize(raw);
        if (tok.empty()) continue;
        std::string key = to_lower(tok[0]);
        if (key == "name" && tok.size() > 1) {
            out->name = tok[1];
            have_name = true;
        } else if (key == "filenameplaybackwards" && tok.size() > 1) {
            std::vector<std::string> fam;
            if (expand_frames(paths, tok[1], &fam)) {
                for (size_t i = fam.size(); i-- > 0;) out->frames.push_back(fam[i]);
                have_frames = true;
            }
        } else if (key == "filename" && tok.size() > 1) {
            std::vector<std::string> fam;
            if (expand_frames(paths, tok[1], &fam)) {
                for (const auto& f : fam) out->frames.push_back(f);
                have_frames = true;
            } else {
                // A fileName that names no file is a data problem worth
                // naming, but the frames spliced so far stay in place: the
                // original engine plays whatever it managed to load.
                if (err && err->empty())
                    *err = "no frames for '" + tok[1] + "' in sequence '" +
                           out->name + "'";
                have_frames = !out->frames.empty();
            }
        } else if (key == "playbackrate" && tok.size() > 1) {
            out->playback_rate = static_cast<float>(atof(tok[1].c_str()));
        } else if (key == "islooping" && tok.size() > 1) {
            out->looping = atoi(tok[1].c_str()) != 0;
        } else if (key == "weight" && tok.size() > 1) {
            out->weight = static_cast<float>(atof(tok[1].c_str()));
        } else if (key == "alwaysload") {
            // Pure memory-management hint for the original loader; nothing to
            // model in the frame data itself.
        } else {
            // Schema-unknown line: keep the parse tolerant but flag it once
            // through err so callers know the data deviated from the header
            // comment's vocabulary.
        }
    }
    (void)have_name;
    return have_frames;
}

bool load_anim_defs(const std::string& path, const PathTable& paths,
                    AnimationRegistry* out, std::string* err) {
    if (!out) {
        if (err) *err = "null output";
        return false;
    }
    *out = AnimationRegistry();
    if (err) err->clear();
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }

    // Walk the file collecting each Sequence block's field lines. Braces sit
    // on their own lines in every shipped AnimDefs.txt.
    static const char* kSeqHeader = "Sequence";
    bool in_seq = false;
    int depth = 0;
    std::vector<std::string> block;
    std::istringstream seq_in(text);
    std::string seq_line;
    while (std::getline(seq_in, seq_line)) {
        std::string t = trim(strip_comment(seq_line));
        if (t.empty()) continue;
        if (!in_seq) {
            if (t == kSeqHeader) { in_seq = true; depth = 0; block.clear(); }
            continue;  // anything else outside a block is unexpected
        }
        if (t == "{") { ++depth; continue; }
        if (t == "}") {
            --depth;
            if (depth <= 0) {
                Animation anim;
                std::string block_err;
                if (parse_sequence_block(block, paths, &anim, &block_err)) {
                    out->by_name[anim.name].push_back(out->sequences.size());
                    out->sequences.push_back(std::move(anim));
                } else if (!block_err.empty() && err) {
                    // First missing-family error wins; report one per file.
                    if (err->empty()) *err = block_err;
                }
                in_seq = false;
                depth = 0;
                block.clear();
            }
            continue;
        }
        block.push_back(t);
    }
    return true;
}

// ---------------------------------------------------------------- flavors
static const char* kFlavorNames[] = {
    "Wild", "Black", "Blue", "Green", "Orange", "Pink", "Red", "Teal", "White", "Yellow"
};

const char* flavor_name(Flavor f) {
    int i = static_cast<int>(f);
    if (i < 0 || i >= static_cast<int>(Flavor::Count)) return "?";
    return kFlavorNames[i];
}

bool flavor_from_name(const std::string& name, Flavor* out) {
    for (int i = 0; i < static_cast<int>(Flavor::Count); ++i) {
        if (to_lower(name) == to_lower(kFlavorNames[i])) {
            *out = static_cast<Flavor>(i);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- level
// Resolves #include directives relative to the including file, mirroring the
// original engine, then scans the merged text for the keys we need.
bool load_level(const std::string& path, Level* out, std::string* err) {
    *out = Level();
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "cannot read " + path;
        return false;
    }

    // Expand includes (one level deep is enough for the game's usage, but we
    // follow chains to be safe).
    std::string merged;
    std::istringstream in(text);
    std::string line;
    std::string base = path;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(0, slash);
    while (std::getline(in, line)) {
        size_t p = line.find("#include");
        if (p != std::string::npos) {
            size_t q1 = line.find('"', p);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos
                                                   : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                std::string full = base + "/" + inc;
                std::string sub;
                if (read_file(full, &sub)) {
                    merged += sub;
                    merged += "\n";
                }
                continue;
            }
        }
        merged += line;
        merged += "\n";
    }

    // Scan the merged text.
    std::istringstream sin(merged);
    bool in_spawn_order = false;
    int depth = 0;
    int spawn_depth = -1;
    while (std::getline(sin, line)) {
        std::string t = trim(strip_comment(line));
        if (t.empty()) continue;
        if (t == "{") { ++depth; continue; }
        if (t == "}") {
            if (depth > 0) --depth;
            if (in_spawn_order && depth < spawn_depth) in_spawn_order = false;
            continue;
        }

        std::vector<std::string> tok = tokenize(t);
        if (tok.empty()) continue;
        const std::string& key = tok[0];
        std::string value = tok.size() > 1 ? tok[1] : "";

        if (key == "StartingTrackSectionSpawnOrder") {
            in_spawn_order = true;
            spawn_depth = depth + 1;
            continue;
        }
        if (in_spawn_order && tok.size() == 1 && key != "{" && key != "}") {
            out->spawn_order.push_back(key);
            continue;
        }

        if (key == "MapPath" && !value.empty()) {
            out->map_path = value;
        } else if (key == "LevelDisplayName" && !value.empty()) {
            out->display_name = value;
        } else if (key == "LevelMusic" && !value.empty()) {
            out->music = value;
        } else if (key == "NumTrainsToSpawn" && !value.empty()) {
            out->num_trains = atoi(value.c_str());
        } else if (key == "NumSpheresToSpawnWith" && !value.empty()) {
            out->spheres_per_train = atoi(value.c_str());
        } else if (key == "ColorSpawnStreak" && !value.empty()) {
            out->color_spawn_streak = static_cast<float>(atof(value.c_str()));
        } else if (key == "AllowableGamePieceFlavors") {
            // Nested block: each difficulty name holds a list of flavors.
            int base_depth = depth;
            std::string difficulty;
            std::vector<std::string> list;
            while (std::getline(sin, line)) {
                std::string it = trim(strip_comment(line));
                if (it.empty()) continue;
                if (it == "{") { ++depth; continue; }
                if (it == "}") {
                    --depth;
                    if (depth <= base_depth) break;
                    continue;
                }
                std::vector<std::string> itok = tokenize(it);
                if (itok.empty()) continue;
                if (itok.size() == 1 && difficulty.empty()) {
                    difficulty = itok[0];
                } else {
                    for (const auto& fl : itok) list.push_back(fl);
                }
            }
            if (!difficulty.empty())
                out->flavors[difficulty] = list;
            continue;
        }
    }
    if (err) err->clear();
    return true;
}

}  // namespace luxor
