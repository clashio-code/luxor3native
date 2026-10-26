// Luxor3 Native - entry point.
//
// Native reimplementation of Luxor 3 for Linux ARM64 handhelds (Anbernic
// RG40XXV / muOS). The original Luxor3.exe is never executed.
//
// The device image has no /dev/dri, so which renderer can run is decided at
// runtime, not compile time:
//   LUXOR_RENDERER=gl   OpenGL ES 2.0 backend (needs an X11/Wayland/EGL
//                       video driver with GLES support)
//   LUXOR_RENDERER=2d   SDL_Renderer backend (always available, recommended
//                       on this device)
//   (unset)             try GL first, fall back to 2D
//
// Build: see ../BUILD.md
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "luxor.h"
#include "sdl2d.h"
#ifdef LUXOR_HAVE_GL
#include "sdl_gl.h"
#endif

int main(int argc, char** argv) {
    const char* want = getenv("LUXOR_RENDERER");
    bool force_gl = want && strcmp(want, "gl") == 0;

#ifdef LUXOR_HAVE_GL
    if (!want || strcmp(want, "2d") != 0) {
        int rc = luxor::run_sdl(argc, argv);
        // run_sdl returns non-zero on init failure; fall through to 2D so a
        // missing GLES driver does not kill the port.
        if (rc == 0 || force_gl) return rc;
        luxor::log_line("main", "GL backend failed (%d), falling back to SDL2D", rc);
    }
#else
    if (force_gl) {
        luxor::log_line("main", "GL backend not compiled in; using 2D");
    }
#endif
    return luxor::run_sdl2d(argc, argv);
}
