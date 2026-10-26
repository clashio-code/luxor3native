// Shared game loop, independent of the presentation backend.
#ifndef LUXOR_GAME_H_
#define LUXOR_GAME_H_

#include <string>

#include "luxor.h"
#include "scene.h"

namespace luxor {

// Presentation backend: GLES2 (sdl_gl.h) or SDL_Renderer 2D (sdl2d.h).
// The device has no /dev/dri, so which one can create a window+GL context is
// decided at runtime, not compile time.
class Presenter {
public:
    virtual ~Presenter() {}
    virtual bool init(int width, int height, std::string* err) = 0;
    virtual void present(const Scene& scene, const PathTable& paths,
                         const std::string& root) = 0;
};

// Loads <game-root>/<level>, runs the frame loop, presents via `p`.
// Environment:
//   LUXOR_MAX_FRAMES=<n>  exit after n frames (smoke testing)
int run_game(int argc, char** argv, Presenter* p);

}  // namespace luxor

#endif  // LUXOR_GAME_H_
