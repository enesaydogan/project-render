#ifdef USE_TINYGLTF
// Provide tinygltf and STB implementations in one translation unit.
// This must be compiled into the project to satisfy linker symbols.

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// Optional: reduce unused warnings or define other configuration macros here.

#include <tiny_gltf.h>

#endif // USE_TINYGLTF
