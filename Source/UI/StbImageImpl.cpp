// stb_image is header-only - exactly one translation unit must define STB_IMAGE_IMPLEMENTATION
// before including it to get the actual function bodies. This file exists solely to be that
// TU; see BoardStatePanel.cpp for the actual usage (piece PNG loading).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
