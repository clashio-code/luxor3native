// SDL2 + OpenGL ES 2.0 presentation layer.
#ifndef LUXOR_SDL_GL_H_
#define LUXOR_SDL_GL_H_

#include <string>

#include "luxor.h"
#include "scene.h"

namespace luxor {

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Creates the window and the GLES2 context.
    bool init(int width, int height, std::string* err);

    // Presents one scene, scaling the 800x600 logical frame to the drawable.
    void present(const Scene& scene, const PathTable& paths, const std::string& root);

    int drawable_w() const { return drawable_w_; }
    int drawable_h() const { return drawable_h_; }

private:
    Renderer(const Renderer&);
    Renderer& operator=(const Renderer&);

    struct Impl;
    Impl* impl_ = nullptr;
    int drawable_w_ = 0;
    int drawable_h_ = 0;
};

// Opens a window, loads a level, and runs the game loop.
int run_sdl(int argc, char** argv);

}  // namespace luxor

#endif  // LUXOR_SDL_GL_H_
