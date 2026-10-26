// Tiny native probe to run on the RG40XXV/muOS image.
//
// It reports SDL video drivers and input devices. Build on-device:
//   cc device_probe.c -o device_probe $(pkg-config --cflags --libs sdl2) -lGLESv2
// If pkg-config is unavailable:
//   cc device_probe.c -o device_probe -I/usr/include/SDL2 -lSDL2 -lGLESv2
// Run:
//   ./device_probe
//   for d in x11 wayland kmsdrm fbdev dummy; do SDL_VIDEODRIVER=$d ./device_probe; done
#include <SDL.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static int probe_video(const char *requested) {
    if (requested && *requested) SDL_setenv("SDL_VIDEODRIVER", requested, 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_version v;
    SDL_GetVersion(&v);
    printf("linked SDL: %u.%u.%u\n", v.major, v.minor, v.patch);
    printf("video driver: %s\n", SDL_GetCurrentVideoDriver());
    int n = SDL_GetNumVideoDrivers();
    printf("compiled drivers:");
    for (int i = 0; i < n; ++i) printf(" %s", SDL_GetVideoDriver(i));
    printf("\n");

    // Test GLES2 context creation without failing the rest of the probe.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window *w = SDL_CreateWindow("probe", 0, 0, 320, 240,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!w) {
        printf("GL window: unavailable (%s)\n", SDL_GetError());
    } else {
        SDL_GLContext gl = SDL_GL_CreateContext(w);
        if (!gl) {
            printf("GLES2 context: unavailable (%s)\n", SDL_GetError());
        } else {
            int dw = 0, dh = 0;
            SDL_GL_GetDrawableSize(w, &dw, &dh);
            printf("GLES2 context: OK, drawable=%dx%d\n", dw, dh);
            printf("GL_VENDOR: %s\n", (const char*)glGetString(GL_VENDOR));
            printf("GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
            printf("GL_VERSION: %s\n", (const char*)glGetString(GL_VERSION));
            SDL_GL_DeleteContext(gl);
        }
        SDL_DestroyWindow(w);
    }

    printf("joysticks: %d\n", SDL_NumJoysticks());
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        printf("  [%d] %s (gamecontroller=%d)\n", i,
               SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
        if (SDL_IsGameController(i)) {
            char *mapping = SDL_GameControllerMappingForIndex(i);
            printf("      mapping: %s\n", mapping ? mapping : "(none)");
            SDL_free(mapping);
        }
    }
    printf("touch devices: %d\n", SDL_GetNumTouchDevices());
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv) {
    return probe_video(argc > 1 ? argv[1] : NULL);
}
