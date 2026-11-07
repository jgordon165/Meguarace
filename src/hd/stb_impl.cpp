// stb single-header libraries (public domain / MIT), compiled once here.
// Only PNG reading and writing, for the Real-ESRGAN scratch frames.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
