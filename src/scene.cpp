// Scene building and train motion.
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <string>

namespace luxor {

// PROTOTYPE ONLY: the input normalization and aim limits below have not been
// confirmed against Luxor 3 behavior. See docs/GAMEPLAY_RULES.md.
void set_aim(ShooterState* shooter, float x, float y) {
    if (!shooter) return;
    const float length = std::sqrt(x * x + y * y);
    if (length < 0.15f) return;  // ignore stick drift
    shooter->angle = std::atan2(y, x);
    const float limit = 1.570796327f - 0.08f;
    shooter->angle = std::max(-limit, std::min(limit, shooter->angle));
    shooter->aim_x = std::cos(shooter->angle);
    shooter->aim_y = std::sin(shooter->angle);
}

// PROTOTYPE ONLY: projectile speed, muzzle offset, ammo advance, collision,
// insertion, match threshold, and score have not been verified against the
// original game. See docs/GAMEPLAY_RULES.md.
void fire_shot(ShooterState* shooter, float speed) {
    if (!shooter || speed <= 0.0f) return;
    Projectile shot;
    shot.pos = {shooter->pos.x + shooter->aim_x * 24.0f,
                shooter->pos.y + shooter->aim_y * 24.0f};
    shot.flavor = shooter->loaded;
    shot.velocity = {shooter->aim_x * speed, shooter->aim_y * speed};
    shooter->shots.push_back(shot);
    shooter->loaded = shooter->next;
    shooter->next = static_cast<Flavor>((static_cast<int>(shooter->next) + 1) %
                                         static_cast<int>(Flavor::Count));
}

// PROTOTYPE ONLY: see docs/GAMEPLAY_RULES.md. The original separates ammo
// selection per mode (classic, puzzle, crossing-the-Nile, treasure hunter)
// and applies the level's allowed flavors; the cyclic enum advance here is
// not that behavior.
void advance_shooter(ShooterState* shooter, std::vector<Train>* trains,
                     const Track& track, float dt) {
    if (!shooter || !trains || dt <= 0.0f) return;
    for (auto it = shooter->shots.begin(); it != shooter->shots.end();) {
        const Vec2 previous = it->pos;
        it->pos.x += it->velocity.x * dt;
        it->pos.y += it->velocity.y * dt;
        it->age += dt;
        bool consumed = false;
        for (auto& train : *trains) {
            const TrackSection* section = track.find_section(train.section);
            if (!section) continue;
            float spacing = train.balls.empty() ? 32.0f : train.balls.front().radius * 2.0f;
            for (size_t i = 0; i < train.balls.size(); ++i) {
                GamePiece& ball = train.balls[i];
                float dx = ball.pos.x - it->pos.x;
                float dy = ball.pos.y - it->pos.y;
                float reach = ball.radius + it->radius;
                float segment_x = it->pos.x - previous.x;
                float segment_y = it->pos.y - previous.y;
                float segment_len2 = segment_x * segment_x + segment_y * segment_y;
                float projection = segment_len2 > 0.0f
                    ? std::max(0.0f, std::min(1.0f,
                        ((ball.pos.x - previous.x) * segment_x +
                         (ball.pos.y - previous.y) * segment_y) / segment_len2))
                    : 1.0f;
                dx = ball.pos.x - (previous.x + projection * segment_x);
                dy = ball.pos.y - (previous.y + projection * segment_y);
                if (dx * dx + dy * dy > reach * reach) continue;
                GamePiece added;
                added.flavor = it->flavor;
                added.radius = ball.radius;
                size_t insert_at = i;
                if (insert_at < train.balls.size() &&
                    (ball.pos.x - previous.x) * (it->pos.x - previous.x) +
                    (ball.pos.y - previous.y) * (it->pos.y - previous.y) < 0.0f && insert_at > 0)
                    --insert_at;
                float distance = train.distance - static_cast<float>(insert_at) * spacing;
                if (distance < 0.0f) { distance = 0.0f; insert_at = train.balls.size(); }
                float tangent = 0.0f;
                track.tangent_at(train.section, distance, &tangent);
                added.angle = tangent;
                if (insert_at == train.balls.size()) {
                    train.balls.push_back(added);
                } else {
                    train.balls.insert(train.balls.begin() + static_cast<std::ptrdiff_t>(insert_at), added);
                    train.distance += spacing;
                }
                for (size_t j = 0; j < train.balls.size(); ++j) {
                    float s = train.distance - static_cast<float>(j) * spacing;
                    if (s < 0.0f) s = 0.0f;
                    track.point_at(train.section, s, &train.balls[j].pos);
                    track.tangent_at(train.section, s, &train.balls[j].angle);
                }
                size_t left = insert_at, right = insert_at + 1;
                while (left > 0 && train.balls[left - 1].flavor == it->flavor) --left;
                while (right < train.balls.size() && train.balls[right].flavor == it->flavor) ++right;
                if (right - left >= 3) {
                    shooter->score += static_cast<int>(right - left) * 10;
                    train.balls.erase(train.balls.begin() + static_cast<std::ptrdiff_t>(left),
                                      train.balls.begin() + static_cast<std::ptrdiff_t>(right));
                    train.distance = std::max(0.0f, train.distance - spacing * static_cast<float>(right - left));
                    for (size_t j = 0; j < train.balls.size(); ++j) {
                        float s = std::max(0.0f, train.distance - static_cast<float>(j) * spacing);
                        track.point_at(train.section, s, &train.balls[j].pos);
                        track.tangent_at(train.section, s, &train.balls[j].angle);
                    }
                }
                consumed = true;
                break;
            }
            if (consumed) break;
        }
        bool offscreen = std::abs(it->pos.x) > 100.0f || std::abs(it->pos.y) > 700.0f;
        if (consumed || offscreen || it->age > 3.0f) it = shooter->shots.erase(it);
        else ++it;
    }
}

bool build_route(const Track& track, const std::string& start, TrainRoute* out) {
    // PROTOTYPE ONLY: choosing the first unvisited vertex-connected section
    // ignores SwitchState/SwitchGate and pathGamePlayData restrictions.
    if (!out) return false;
    out->sections.clear();
    out->index = 0;
    if (!track.find_section(start)) return false;
    out->sections.push_back(start);
    // Follow the junctions, never repeating a section, so a route terminates
    // even when the track graph has a cycle.
    std::string current = start;
    for (size_t guard = 0; guard < 64; ++guard) {
        std::vector<std::string> next = track.sections_after(current);
        bool moved = false;
        for (const auto& candidate : next) {
            bool already = false;
            for (const auto& s : out->sections)
                if (s == candidate) { already = true; break; }
            if (already) continue;
            out->sections.push_back(candidate);
            current = candidate;
            moved = true;
            break;
        }
        if (!moved) break;
    }
    return true;
}

namespace {

// Position of a point that is `s` pixels back along the route from the head of
// `route`, crossing section boundaries when needed.
bool route_point(const Track& track, const TrainRoute& route, float s, Vec2* out,
                 float* tangent) {
    if (route.sections.empty()) return false;
    size_t index = route.index;
    float remaining = s;
    while (true) {
        if (index >= route.sections.size()) {
            // Past the end of the route: clamp to its last point.
            index = route.sections.size() - 1;
            remaining = 0.0f;
        }
        const std::string& name = route.sections[index];
        const TrackSection* sec = track.find_section(name);
        if (!sec || sec->length <= 0.0f) {
            if (index + 1 >= route.sections.size()) return false;
            ++index;
            continue;
        }
        if (remaining <= sec->length || index + 1 >= route.sections.size()) {
            float local = std::min(remaining, sec->length);
            if (!track.point_at(name, local, out)) return false;
            if (tangent) {
                if (!track.tangent_at(name, local, tangent)) *tangent = 0.0f;
            }
            return true;
        }
        remaining -= sec->length;
        ++index;
    }
}

}  // namespace

void advance_train(Train* train, const Track& track, const TrainTuning& tuning,
                   float dt, int trains_alive) {
    if (!train || dt <= 0.0f) return;
    const TrackSection* sec = track.find_section(train->section);
    if (!sec || sec->length <= 0.0f) return;

    // Target speed falls off as the train approaches the end of its section,
    // so it arrives slow enough to be destroyed. This mirrors the original
    // DefaultTrackStartSpeed / DefaultTrackEndSpeed pair.
    const float remaining = sec->length - train->distance;
    float target = tuning.start_speed;
    if (remaining < 200.0f) {
        target = std::max(tuning.end_speed,
                          tuning.start_speed * (remaining / 200.0f));
    }
    // More trains alive => higher acceleration, per TrainHighAcceleration.
    float accel = (trains_alive > 2) ? tuning.high_acceleration : tuning.acceleration;

    if (train->speed < target) {
        train->speed = std::min(target, train->speed + accel * dt);
    } else if (train->speed > target) {
        // Deceleration uses the same magnitude for simplicity; the original
        // uses a separate constant not exposed in GameVariables.txt.
        train->speed = std::max(target, train->speed - accel * dt);
    }
    if (train->speed < 0.0f) train->speed = 0.0f;

    train->distance += train->speed * dt;
    if (train->distance > sec->length) train->distance = sec->length;

    // Reposition each ball along the section, spaced by the ball diameter.
    float spacing = train->balls.empty() ? 32.0f
                                         : train->balls[0].radius * 2.0f;
    for (size_t i = 0; i < train->balls.size(); ++i) {
        float s = train->distance - static_cast<float>(i) * spacing;
        if (s < 0.0f) s = 0.0f;
        Vec2 p;
        if (track.point_at(train->section, s, &p)) {
            train->balls[i].pos = p;
        }
        float ang = 0.0f;
        if (track.tangent_at(train->section, s, &ang)) {
            train->balls[i].angle = ang;
        }
    }
}

// Advances a train that walks a multi-section route. The train keeps moving
// across section boundaries instead of stopping at the end of its first
// section, which is what the original does.
void advance_train_route(Train* train, const Track& track,
                         const TrainTuning& tuning, TrainRoute* route, float dt,
                         int trains_alive) {
    if (!train || !route || dt <= 0.0f) return;
    if (route->sections.empty()) return;
    if (route->index >= route->sections.size()) route->index = 0;

    // Distance is measured along the whole route, so switching sections does
    // not move the train backwards.
    float head = 0.0f;
    for (size_t i = 0; i < route->index; ++i) {
        const TrackSection* sec = track.find_section(route->sections[i]);
        head += sec ? sec->length : 0.0f;
    }
    const TrackSection* current = track.find_section(route->sections[route->index]);
    if (!current || current->length <= 0.0f) return;
    const float local = train->distance;

    // The train slows down over the last stretch of the *route*, not just the
    // last stretch of the current section, so it arrives at a junction slowly.
    float total_length = 0.0f;
    for (const auto& name : route->sections) {
        const TrackSection* sec = track.find_section(name);
        if (sec) total_length += sec->length;
    }
    const float travelled = head + local;
    const float route_remaining = std::max(0.0f, total_length - travelled);
    float target = tuning.start_speed;
    if (route_remaining < 200.0f) {
        target = std::max(tuning.end_speed,
                          tuning.start_speed * (route_remaining / 200.0f));
    }
    float accel = (trains_alive > 2) ? tuning.high_acceleration : tuning.acceleration;
    if (train->speed < target)
        train->speed = std::min(target, train->speed + accel * dt);
    else
        train->speed = std::max(target, train->speed - accel * dt);
    if (train->speed < 0.0f) train->speed = 0.0f;

    train->distance += train->speed * dt;

    // Cross into the next section when the head runs past this one.
    while (train->distance > current->length) {
        if (route->index + 1 >= route->sections.size()) {
            train->distance = current->length;
            break;
        }
        train->distance -= current->length;
        ++route->index;
        const TrackSection* next = track.find_section(route->sections[route->index]);
        if (!next || next->length <= 0.0f) {
            --route->index;
            train->distance = current->length;
            break;
        }
        current = next;
    }
    if (train->distance > current->length) train->distance = current->length;
    train->section = current->name;

    // Place the balls along the route so the whole train follows the bends.
    const float spacing = train->balls.empty() ? 32.0f
                                               : train->balls[0].radius * 2.0f;
    const float route_head = head + train->distance;
    for (size_t i = 0; i < train->balls.size(); ++i) {
        float back = static_cast<float>(i) * spacing;
        float s = route_head - back;
        if (s < 0.0f) s = 0.0f;
        Vec2 p;
        float ang = 0.0f;
        if (route_point(track, *route, s, &p, &ang)) {
            train->balls[i].pos = p;
            train->balls[i].angle = ang;
        }
    }
}

void build_scene(const SceneContext& ctx, const std::vector<Train>& trains,
                 float time_seconds, Scene* out) {
    (void)time_seconds;
    out->items.clear();
    out->overlay_items.clear();
    out->score = ctx.shooter ? ctx.shooter->score : 0;
    char score_label[48];
    std::snprintf(score_label, sizeof(score_label), "SCORE %d", out->score);
    out->score_text = score_label;
    out->track_lines.clear();

    if (!ctx.level || !ctx.track) return;

    // Background: the map's back_01 image, if we can find it.
    if (ctx.paths && !ctx.level->map_path.empty()) {
        std::string bg = ctx.paths->resolve(
            ctx.level->map_path + "back_01.jpg");
        if (bg.empty()) {
            bg = ctx.paths->resolve(ctx.level->map_path + "back_01.png");
        }
        if (!bg.empty()) out->background = bg;
    }

    // Debug track overlay, so a preview shows the path even without artwork.
    // World Y points up; screen Y points down, so negate on conversion.
    if (ctx.track) {
        for (const auto& sec : ctx.track->sections) {
            for (size_t i = 0; i + 1 < sec.points.size(); ++i) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f,%.2f",
                         sec.points[i].x, sec.points[i].y * kWorldToScreenY,
                         sec.points[i+1].x, sec.points[i+1].y * kWorldToScreenY);
                out->track_lines.push_back(buf);
            }
        }
    }

    // Balls. Position is stored in world space; convert to screen space here,
    // the single place where the two spaces meet. Mirroring Y also mirrors the
    // tangent angle, so its sign must be flipped to keep sprites upright.
    for (const auto& train : trains) {
        for (const auto& ball : train.balls) {
            int fi = static_cast<int>(ball.flavor);
            if (fi < 0 || fi >= static_cast<int>(Flavor::Count)) continue;
            const std::string& tex = ctx.ball_texture[fi];
            if (tex.empty()) continue;
            DrawItem it;
            it.texture = tex;
            it.w = it.h = ctx.ball_size;
            float sy = ball.pos.y * kWorldToScreenY;
            it.x = ball.pos.x - it.w * 0.5f;
            it.y = sy - it.h * 0.5f;
            it.angle = -ball.angle;
            out->items.push_back(it);
        }
    }
    if (ctx.shooter) {
        auto draw_ball = [&](Flavor flavor, Vec2 pos, float size, float angle,
                             std::vector<DrawItem>* target) {
            int fi = static_cast<int>(flavor);
            if (fi < 0 || fi >= static_cast<int>(Flavor::Count)) return;
            if (ctx.ball_texture[fi].empty()) return;
            DrawItem item;
            item.texture = ctx.ball_texture[fi];
            item.w = item.h = size;
            item.x = pos.x - size * 0.5f;
            item.y = pos.y * kWorldToScreenY - size * 0.5f;
            item.angle = -angle;
            target->push_back(item);
        };
        const ShooterState& shooter = *ctx.shooter;
        draw_ball(shooter.loaded, shooter.pos, ctx.ball_size, shooter.angle, &out->overlay_items);
        draw_ball(shooter.next,
                  {shooter.pos.x - shooter.aim_x * 38.0f,
                   shooter.pos.y - shooter.aim_y * 38.0f},
                  ctx.ball_size * 0.72f, shooter.angle, &out->overlay_items);
        for (const auto& shot : shooter.shots)
            draw_ball(shot.flavor, shot.pos, shot.radius * 2.0f, 0.0f, &out->overlay_items);
    }
}

}  // namespace luxor
