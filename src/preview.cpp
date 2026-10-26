// Offline software renderer.
#include "preview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#define STB_IMAGE_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace luxor {

void Framebuffer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = r; rgba[i+1] = g; rgba[i+2] = b; rgba[i+3] = a;
    }
}

namespace {

struct Rgba {
    float r, g, b, a;
};

inline void blend(Framebuffer& fb, int x, int y, const Rgba& c) {
    if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) return;
    uint8_t* p = &fb.rgba[(static_cast<size_t>(y) * fb.width + x) * 4];
    float a = c.a;
    p[0] = static_cast<uint8_t>(c.r * 255.0f * a + p[0] * (1.0f - a));
    p[1] = static_cast<uint8_t>(c.g * 255.0f * a + p[1] * (1.0f - a));
    p[2] = static_cast<uint8_t>(c.b * 255.0f * a + p[2] * (1.0f - a));
    p[3] = static_cast<uint8_t>(255.0f * a + p[3] * (1.0f - a));
}

void draw_textured(Framebuffer& fb, const Image& img, const std::string& ref,
                   float x, float y, float w, float h, float angle,
                   float u0, float v0, float u1, float v1,
                   const Rgba& tint) {
    (void)ref;
    if (!img.valid()) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f;
    float c = std::cos(angle), s = std::sin(angle);

    // Iterate over the destination bounding box and inverse-map each pixel.
    float ex = std::fabs(hw * c) + std::fabs(hh * s);
    float ey = std::fabs(hw * s) + std::fabs(hh * c);
    int x0 = static_cast<int>(std::floor(cx - ex));
    int x1 = static_cast<int>(std::ceil(cx + ex));
    int y0 = static_cast<int>(std::floor(cy - ey));
    int y1 = static_cast<int>(std::ceil(cy + ey));

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float dx = px + 0.5f - cx;
            float dy = py + 0.5f - cy;
            // Inverse rotation (transpose).
            float lx = dx * c + dy * s;
            float ly = -dx * s + dy * c;
            if (lx < -hw || lx >= hw || ly < -hh || ly >= hh) continue;
            float fu = (lx + hw) / w;
            float fv = (ly + hh) / h;
            float u = u0 + fu * (u1 - u0);
            float v = v0 + fv * (v1 - v0);
            int tx = static_cast<int>(u * img.width);
            int ty = static_cast<int>(v * img.height);
            if (tx < 0) tx = 0;
            if (tx >= img.width) tx = img.width - 1;
            if (ty < 0) ty = 0;
            if (ty >= img.height) ty = img.height - 1;
            const uint8_t* sp = &img.rgba[(static_cast<size_t>(ty) * img.width + tx) * 4];
            Rgba src{ sp[0] / 255.0f, sp[1] / 255.0f, sp[2] / 255.0f,
                      sp[3] / 255.0f };
            Rgba out{ src.r * tint.r, src.g * tint.g, src.b * tint.b,
                      src.a * tint.a };
            blend(fb, px, py, out);
        }
    }
}

}  // namespace

bool render_scene(const Scene& scene, const PathTable& paths,
                  const std::string& root, Framebuffer* fb, std::string* err) {
    fb->clear(0, 0, 0, 255);

    // Decoded texture cache, keyed by the resolved relative path.
    std::map<std::string, Image> cache;
    auto load = [&](const std::string& ref) -> const Image* {
        if (ref.empty()) return nullptr;
        auto it = cache.find(ref);
        if (it != cache.end()) return &it->second;
        std::string rel = paths.resolve(ref);
        if (rel.empty()) return nullptr;
        FILE* f = fopen((root + "/" + rel).c_str(), "rb");
        if (!f) return nullptr;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(sz > 0 ? sz : 0);
        size_t got = (sz > 0) ? fread(buf.data(), 1, static_cast<size_t>(sz), f) : 0;
        fclose(f);
        if (got == 0) return nullptr;
        Image img = decode_image(buf.data(), buf.size());
        if (!img.valid()) return nullptr;
        cache[rel] = std::move(img);
        return &cache[rel];
    };

    // Background fills the frame.
    if (!scene.background.empty()) {
        const Image* bg = load(scene.background);
        if (bg && bg->valid()) {
            for (int y = 0; y < fb->height; ++y) {
                int sy = static_cast<int>(static_cast<long>(y) * bg->height / fb->height);
                if (sy >= bg->height) sy = bg->height - 1;
                for (int x = 0; x < fb->width; ++x) {
                    int sx = static_cast<int>(static_cast<long>(x) * bg->width / fb->width);
                    if (sx >= bg->width) sx = bg->width - 1;
                    const uint8_t* sp = &bg->rgba[(static_cast<size_t>(sy) * bg->width + sx) * 4];
                    uint8_t* dp = &fb->rgba[(static_cast<size_t>(y) * fb->width + x) * 4];
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255;
                }
            }
        }
    }

    // Track overlay as thin 1px lines (debug aid).
    for (const auto& line : scene.track_lines) {
        float v[4] = {0,0,0,0};
        if (sscanf(line.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) continue;
        int steps = static_cast<int>(std::hypot(v[2]-v[0], v[3]-v[1]));
        if (steps <= 0) continue;
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            int px = static_cast<int>(v[0] + (v[2]-v[0]) * t);
            int py = static_cast<int>(v[1] + (v[3]-v[1]) * t);
            blend(*fb, px, py, Rgba{0.0f, 1.0f, 0.0f, 0.35f});
            blend(*fb, px+1, py, Rgba{0.0f, 1.0f, 0.0f, 0.35f});
        }
    }

    // Sprites.
    for (const auto& item : scene.items) {
        const Image* img = load(item.texture);
        if (!img || !img->valid()) continue;
        draw_textured(*fb, *img, item.texture, item.x, item.y, item.w, item.h,
                      item.angle, item.u0, item.v0, item.u1, item.v1,
                      Rgba{item.r, item.g, item.b, item.a});
    }

    if (err) err->clear();
    return true;
}

bool write_png(const Framebuffer& fb, const std::string& path, std::string* err) {
    if (!stbi_write_png(path.c_str(), fb.width, fb.height, 4, fb.rgba.data(),
                        fb.width * 4)) {
        if (err) *err = "failed writing " + path;
        return false;
    }
    return true;
}

}  // namespace luxor
