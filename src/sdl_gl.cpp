// SDL2 + OpenGL ES 2.0 presentation layer.
#include "sdl_gl.h"
#include "game.h"

#include <SDL.h>
#include <GLES2/gl2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "sprite_batch.h"

namespace luxor {

struct Renderer::Impl {
    SDL_Window* window = nullptr;
    SDL_GLContext ctx = nullptr;
    SpriteBatch batch;
    uint32_t score_texture = 0;
    std::map<std::string, uint32_t> textures;
    std::vector<uint8_t> scratch;
};

Renderer::Renderer() : impl_(new Impl()) {}

Renderer::~Renderer() {
    if (impl_) {
        if (impl_->ctx) SDL_GL_DeleteContext(impl_->ctx);
        if (impl_->window) SDL_DestroyWindow(impl_->window);
        delete impl_;
    }
}

bool Renderer::init(int width, int height, std::string* err) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        if (err) *err = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    impl_->window = SDL_CreateWindow("Luxor3", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, width, height,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!impl_->window) {
        if (err) *err = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }
    impl_->ctx = SDL_GL_CreateContext(impl_->window);
    if (!impl_->ctx) {
        if (err) *err = std::string("SDL_GL_CreateContext: ") + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(impl_->window, impl_->ctx);

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    log_line("gl", "vendor=%s renderer=%s version=%s",
             vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
    log_line("gl", "video driver=%s", SDL_GetCurrentVideoDriver());

    if (!impl_->batch.init()) {
        if (err) *err = "failed to create the sprite batch shader";
        return false;
    }
    SDL_GL_GetDrawableSize(impl_->window, &drawable_w_, &drawable_h_);
    return true;
}

void Renderer::present(const Scene& scene, const PathTable& paths,
                       const std::string& root) {
    SDL_GL_GetDrawableSize(impl_->window, &drawable_w_, &drawable_h_);

    // The logical frame is 800x600; the device panel is 640x480 (same 4:3
    // aspect), so a uniform scale fits exactly. The projection below maps the
    // logical frame onto the whole drawable.
    impl_->batch.set_viewport(drawable_w_, drawable_h_);

    glViewport(0, 0, drawable_w_, drawable_h_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    auto upload = [&](const std::string& ref) -> uint32_t {
        if (ref.empty()) return 0;
        auto it = impl_->textures.find(ref);
        if (it != impl_->textures.end()) return it->second;
        std::string rel = paths.resolve(ref);
        if (rel.empty()) return 0;
        FILE* f = fopen((root + "/" + rel).c_str(), "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        impl_->scratch.resize(sz > 0 ? sz : 0);
        size_t got = (sz > 0) ? fread(impl_->scratch.data(), 1,
                                      static_cast<size_t>(sz), f) : 0;
        fclose(f);
        if (got == 0) return 0;
        Image img = decode_image(impl_->scratch.data(), impl_->scratch.size());
        if (!img.valid()) return 0;
        uint32_t tex = impl_->batch.create_texture(img.width, img.height,
                                                   img.rgba.data());
        impl_->textures[ref] = tex;
        return tex;
    };

    // Background first, stretched to the full frame.
    if (!scene.background.empty()) {
        uint32_t tex = upload(scene.background);
        if (tex) {
            impl_->batch.draw(tex, 0, 0, static_cast<float>(kLogicalWidth),
                              static_cast<float>(kLogicalHeight),
                              0, 0, 1, 1, 1, 1, 1, 1);
        }
    }

    for (const auto& item : scene.items) {
        uint32_t tex = upload(item.texture);
        if (!tex) continue;
        if (item.angle != 0.0f) {
            impl_->batch.draw_rotated(tex, item.x + item.w * 0.5f,
                                      item.y + item.h * 0.5f, item.w, item.h,
                                      item.angle, item.u0, item.v0, item.u1, item.v1,
                                      item.r, item.g, item.b, item.a);
        } else {
            impl_->batch.draw(tex, item.x, item.y, item.w, item.h,
                              item.u0, item.v0, item.u1, item.v1,
                              item.r, item.g, item.b, item.a);
        }
    }
    for (const auto& item : scene.overlay_items) {
        uint32_t tex = upload(item.texture);
        if (!tex) continue;
        impl_->batch.draw_rotated(tex, item.x + item.w * 0.5f, item.y + item.h * 0.5f,
                                  item.w, item.h, item.angle, item.u0, item.v0,
                                  item.u1, item.v1, item.r, item.g, item.b, item.a);
    }
    if (!scene.score_text.empty()) {
        static const uint8_t font[11][7] = {
            {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
            {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
            {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
            {14,17,17,15,1,1,14}, {0,0,0,0,0,0,0}
        };
        if (!impl_->score_texture) {
            uint8_t pixels[32 * 16 * 4] = {};
            for (int digit = 0; digit < 10; ++digit)
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 5; ++x)
                        if (font[digit][y] & (1 << (4 - x))) {
                            size_t off = (static_cast<size_t>(y) * 32 + digit * 3 + x) * 4;
                            pixels[off] = pixels[off+1] = pixels[off+2] = pixels[off+3] = 255;
                        }
            impl_->score_texture = impl_->batch.create_texture(32, 16, pixels);
        }
        float x = 12.0f;
        for (char c : scene.score_text) {
            if (c == ' ') { x += 6.0f; continue; }
            if (c >= '0' && c <= '9') {
                int d = c - '0';
                float u0 = static_cast<float>(d * 3) / 32.0f;
                impl_->batch.draw(impl_->score_texture, x, 12.0f, 10.0f, 14.0f,
                                  u0, 0.0f, u0 + 5.0f/32.0f, 7.0f/16.0f,
                                  1.0f, 1.0f, 0.65f, 1.0f);
                x += 11.0f;
            }
        }
    }
    impl_->batch.flush();
    SDL_GL_SwapWindow(impl_->window);
}

// ---------------------------------------------------------------- game loop
namespace {
class GlPresenter : public Presenter {
public:
    bool init(int width, int height, std::string* err) {
        return renderer_.init(width, height, err);
    }
    void present(const Scene& scene, const PathTable& paths,
                 const std::string& root) {
        renderer_.present(scene, paths, root);
    }
private:
    Renderer renderer_;
};
}  // namespace

int run_sdl(int argc, char** argv) {
    GlPresenter presenter;
    return run_game(argc, argv, &presenter);
}

}  // namespace luxor
