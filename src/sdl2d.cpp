// SDL2 software renderer backend.
#include "sdl2d.h"
#include "game.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace luxor {

struct Renderer2D::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::map<std::string, SDL_Texture*> textures;
    std::vector<uint8_t> scratch;
};

Renderer2D::Renderer2D() : impl_(new Impl()) {}

Renderer2D::~Renderer2D() {
    if (impl_) {
        for (auto& kv : impl_->textures)
            if (kv.second) SDL_DestroyTexture(kv.second);
        if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
        if (impl_->window) SDL_DestroyWindow(impl_->window);
        delete impl_;
    }
}

bool Renderer2D::init(int width, int height, std::string* err) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        if (err) *err = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }
    impl_->window = SDL_CreateWindow("Luxor3", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, width, height,
                                     SDL_WINDOW_SHOWN);
    if (!impl_->window) {
        if (err) *err = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }
    // SDL_RENDERER_ACCELERATED is preferred but not required: on a device
    // without DRM/GLES the driver may only offer the software renderer.
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1,
                                         SDL_RENDERER_ACCELERATED);
    if (!impl_->renderer) {
        impl_->renderer = SDL_CreateRenderer(impl_->window, -1,
                                             SDL_RENDERER_SOFTWARE);
    }
    if (!impl_->renderer) {
        if (err) *err = std::string("SDL_CreateRenderer: ") + SDL_GetError();
        return false;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(impl_->renderer, &info) == 0) {
        log_line("sdl2d", "renderer: %s (software=%d)", info.name,
                 info.flags & SDL_RENDERER_SOFTWARE ? 1 : 0);
    }
    log_line("sdl2d", "video driver: %s", SDL_GetCurrentVideoDriver());
    SDL_GL_GetDrawableSize(impl_->window, &drawable_w_, &drawable_h_);
    return true;
}

void Renderer2D::present(const Scene& scene, const PathTable& paths,
                         const std::string& root) {
    SDL_GetWindowSize(impl_->window, &drawable_w_, &drawable_h_);
    SDL_RenderSetLogicalSize(impl_->renderer, kLogicalWidth, kLogicalHeight);

    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);

    auto upload = [&](const std::string& ref) -> SDL_Texture* {
        if (ref.empty()) return nullptr;
        auto it = impl_->textures.find(ref);
        if (it != impl_->textures.end()) return it->second;
        std::string rel = paths.resolve(ref);
        if (rel.empty()) return nullptr;
        FILE* f = fopen((root + "/" + rel).c_str(), "rb");
        if (!f) return nullptr;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        impl_->scratch.resize(sz > 0 ? sz : 0);
        size_t got = (sz > 0) ? fread(impl_->scratch.data(), 1,
                                      static_cast<size_t>(sz), f) : 0;
        fclose(f);
        if (got == 0) return nullptr;
        Image img = decode_image(impl_->scratch.data(), impl_->scratch.size());
        if (!img.valid()) return nullptr;
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            img.rgba.data(), img.width, img.height, 32,
            img.width * 4, SDL_PIXELFORMAT_ABGR8888);
        if (!surf) return nullptr;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(impl_->renderer, surf);
        SDL_FreeSurface(surf);
        if (!tex) return nullptr;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        impl_->textures[ref] = tex;
        return tex;
    };

    if (!scene.background.empty()) {
        SDL_Texture* bg = upload(scene.background);
        if (bg) {
            SDL_Rect dst{0, 0, kLogicalWidth, kLogicalHeight};
            SDL_RenderCopy(impl_->renderer, bg, nullptr, &dst);
        }
    }

    for (const auto& item : scene.items) {
        SDL_Texture* tex = upload(item.texture);
        if (!tex) continue;
        SDL_SetTextureColorMod(tex, static_cast<Uint8>(item.r * 255.0f),
                                static_cast<Uint8>(item.g * 255.0f),
                                static_cast<Uint8>(item.b * 255.0f));
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(item.a * 255.0f));
        SDL_Rect dst;
        dst.w = static_cast<int>(item.w);
        dst.h = static_cast<int>(item.h);
        if (item.angle == 0.0f) {
            dst.x = static_cast<int>(item.x);
            dst.y = static_cast<int>(item.y);
            SDL_RenderCopy(impl_->renderer, tex, nullptr, &dst);
        } else {
            // SDL_RenderCopyEx rotates around the destination rectangle centre.
            dst.x = static_cast<int>(item.x + item.w * 0.5f - item.w * 0.5f);
            dst.y = static_cast<int>(item.y + item.h * 0.5f - item.h * 0.5f);
            SDL_RenderCopyEx(impl_->renderer, tex, nullptr, &dst,
                             item.angle * 180.0f / 3.14159265358979, nullptr,
                             SDL_FLIP_NONE);
        }
    }
    for (const auto& item : scene.overlay_items) {
        SDL_Texture* tex = upload(item.texture);
        if (!tex) continue;
        SDL_Rect dst{static_cast<int>(item.x), static_cast<int>(item.y),
                     static_cast<int>(item.w), static_cast<int>(item.h)};
        SDL_RenderCopyEx(impl_->renderer, tex, nullptr, &dst,
                         item.angle * 180.0f / 3.14159265358979, nullptr,
                         SDL_FLIP_NONE);
    }
    if (!scene.score_text.empty()) {
        SDL_Rect panel{8, 8, 105, 27};
        SDL_SetRenderDrawColor(impl_->renderer, 20, 13, 8, 200);
        SDL_RenderFillRect(impl_->renderer, &panel);
        static const uint8_t font[10][7] = {
            {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
            {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
            {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
            {14,17,17,15,1,1,14}
        };
        int x = 12;
        SDL_SetRenderDrawColor(impl_->renderer, 255, 238, 128, 255);
        for (char c : scene.score_text) {
            if (c == ' ') { x += 6; continue; }
            if (c >= '0' && c <= '9') {
                for (int y = 0; y < 7; ++y)
                    for (int col = 0; col < 5; ++col)
                        if (font[c - '0'][y] & (1 << (4 - col)))
                            SDL_RenderDrawPoint(impl_->renderer, x + col, 12 + y);
                x += 7;
            }
        }
    }
    SDL_RenderPresent(impl_->renderer);
}

// ---------------------------------------------------------------- game loop
namespace {
class Sdl2dPresenter : public Presenter {
public:
    bool init(int width, int height, std::string* err) {
        return renderer_.init(width, height, err);
    }
    void present(const Scene& scene, const PathTable& paths,
                 const std::string& root) {
        renderer_.present(scene, paths, root);
    }
private:
    Renderer2D renderer_;
};
}  // namespace

int run_sdl2d(int argc, char** argv) {
    Sdl2dPresenter presenter;
    return run_game(argc, argv, &presenter);
}

}  // namespace luxor
