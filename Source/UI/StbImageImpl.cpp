// stb_image is a header-only library - exactly one translation unit in the whole program must
// define STB_IMAGE_IMPLEMENTATION before including it to get the actual function bodies (every
// other file just includes stb_image.h normally to get the declarations). This file exists
// solely to be that one TU; see BoardStatePanel.cpp for the actual usage (piece PNG loading).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
