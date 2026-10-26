// SDL2 software renderer backend.
//
// The RG40XXV has no DRM render node (/dev/dri is absent), so SDL2 cannot use
// the KMSDRM video driver. Depending on which driver muOS exposes (fbdev,
// X11, Wayland) a GLES2 context may or may not be creatable. This backend
// uses SDL_Renderer instead, which blits through whatever surface SDL gives
// us and therefore works on every driver.
//
// It consumes the same Scene as the GLES2 backend, so both stay in sync.
#ifndef LUXOR_SDL2D_H_
#define LUXOR_SDL2D_H_

#include <string>

#include "luxor.h"
#include "scene.h"

namespace luxor {

class Renderer2D {
public:
    Renderer2D();
    ~Renderer2D();

    bool init(int width, int height, std::string* err);
    void present(const Scene& scene, const PathTable& paths, const std::string& root);

    int drawable_w() const { return drawable_w_; }
    int drawable_h() const { return drawable_h_; }

private:
    Renderer2D(const Renderer2D&);
    Renderer2D& operator=(const Renderer2D&);

    struct Impl;
    Impl* impl_ = nullptr;
    int drawable_w_ = 0;
    int drawable_h_ = 0;
};

// Runs the game loop with the 2D renderer.
int run_sdl2d(int argc, char** argv);

}  // namespace luxor

#endif  // LUXOR_SDL2D_H_
