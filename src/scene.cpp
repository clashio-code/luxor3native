// Scene building and train motion.
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace luxor {

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

void build_scene(const SceneContext& ctx, const std::vector<Train>& trains,
                 float time_seconds, Scene* out) {
    (void)time_seconds;
    out->items.clear();
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
}

}  // namespace luxor
