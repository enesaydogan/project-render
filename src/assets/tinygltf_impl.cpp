// Provide STB, TinyEXR, and TinyGLTF implementations in one translation unit.
// This must be compiled into the project to satisfy linker symbols.

#ifdef USE_TINYGLTF
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYEXR_IMPLEMENTATION
#include <tiny_gltf.h>
#include <tinyexr.h>
#else
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYEXR_IMPLEMENTATION
#include <stb_image.h>
#include <tinyexr.h>
#endif
