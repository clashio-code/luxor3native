// Offline software renderer: renders a Scene to a PNG without a GPU.
//
// This exists so the scene pipeline can be verified on a build host (CI) with
// no GPU and no GLES2 driver. The output must match what the GLES2 backend
// draws; both consume the same Scene.
#ifndef LUXOR_PREVIEW_H_
#define LUXOR_PREVIEW_H_

#include <string>

#include "scene.h"

namespace luxor {

// Renders `scene` into an RGBA8 buffer of width x height.
struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

// `root` is the game root that the PathTable was scanned from; resolved
// texture paths are relative to it.
bool render_scene(const Scene& scene, const PathTable& paths,
                  const std::string& root, Framebuffer* fb, std::string* err);

// Writes the framebuffer as a PNG.
bool write_png(const Framebuffer& fb, const std::string& path, std::string* err);

}  // namespace luxor

#endif  // LUXOR_PREVIEW_H_
