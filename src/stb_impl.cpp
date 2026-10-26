// Single translation unit that instantiates stb_image / stb_image_write.
//
// Keeping the implementations here avoids duplicate symbols when several
// translation units include the stb headers.
//
// stb's own structs trip -Wmissing-field-initializers; that is vendor code,
// not ours.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
