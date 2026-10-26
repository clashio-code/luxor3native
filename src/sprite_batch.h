// Batched textured-quad renderer for OpenGL ES 2.0.
//
// The original game used OpenGL 1.x immediate mode (glBegin/glEnd per
// sprite). On GLES2 that path is gone, so quads are accumulated into one
// vertex buffer and flushed in a single draw call per texture.
#ifndef LUXOR_SPRITE_BATCH_H_
#define LUXOR_SPRITE_BATCH_H_

#include <cstdint>
#include <vector>

namespace luxor {

struct Texture;

class SpriteBatch {
public:
    SpriteBatch();
    ~SpriteBatch();

    // Owns the GL shader program. Returns false if the context is unusable.
    bool init();

    // Uploads a texture from RGBA8 pixels. Returns a texture handle (>0) or 0.
    uint32_t create_texture(int width, int height, const uint8_t* rgba);

    void destroy_texture(uint32_t tex);

    // Draws one sprite. Coordinates are in logical pixels with the origin at
    // the top-left and Y growing downwards (matching the game's data).
    void draw(uint32_t tex, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1,
              float r, float g, float b, float a);

    // Draws a sprite rotated around its own centre.
    void draw_rotated(uint32_t tex, float cx, float cy, float w, float h,
                      float angle_radians,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a);

    // Sets the orthographic projection for the logical frame.
    void set_viewport(int width, int height);

    // Flushes accumulated quads.
    void flush();

private:
    struct Vertex {
        float x, y;      // logical pixel position
        float u, v;      // texture coords
        float r, g, b, a;
    };

    void bind_texture(uint32_t tex);

    uint32_t program_ = 0;
    uint32_t vbo_ = 0;
    int attr_pos_ = -1;
    int attr_uv_ = -1;
    int attr_color_ = -1;
    int uni_mvp_ = -1;
    int uni_tex_ = -1;

    std::vector<Vertex> vertices_;
    uint32_t current_tex_ = 0;
    bool has_current_ = false;
    float proj_[16] = {0};
};

}  // namespace luxor

#endif  // LUXOR_SPRITE_BATCH_H_
