#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../lib/glad/glad.h"

struct KeyBuffer {
    int currentIndex;
    int length;
    unsigned char *data;
};

struct Vec2i { int x; int y; };

struct RenderContext {
    // Window information
    GLFWwindow *window;
    struct Vec2i screenSize;
    struct Vec2i screenTileSize;
    struct Vec2i cursorPosition;
    struct KeyBuffer keyBuffer;

    // OpenGL information
    GLuint programId;
    GLuint vaoId;
    GLuint shaderContextId;
    GLuint atlasTextureId;

    // Glyph atlas information
    int* characterAtlasMap;
    // User controlled font size, used as the line height of the rendered text.
    int fontSize;
    // Font size used when rendering glyphs to the atlas texture. These glyphs are sampled to render at the actual font size.
    int atlasFontHeight;
    // Pixel vector containing (advance, lineHeight) used when rendering to screen.
    struct Vec2i screenGlyphSize;
    // Pixel vector containing (advance, lineHeight) for glyphs in atlas texture.
    struct Vec2i atlasGlyphSize;
    // Vector containing the number of rows/columns in the atlas texture.
    struct Vec2i atlasTileSize;
    struct Vec2i *glyphOffsets;
    int foregroundColor;
    int backgroundColor;
};

struct TextShaderContext {
    // Pixel vector containing (advance, lineHeight) for glyphs in atlas texture.
    struct Vec2i atlasGlyphSize;
    // Pixel vector containing (advance, lineHeight) used when rendering to screen.
    struct Vec2i screenGlyphSize;
    // Pixel vector containing screen resolution.
    struct Vec2i screenSize;
    // Vector containing (columns, rows) for the screen grid.
    struct Vec2i screenTileSize;
    // Vector containing (columns, rows) for the atlas texture grid.
    struct Vec2i atlasTileSize;
    // Pixel vector containing the number of extra pixels on the right and bottom of the screen.
    // These areas do not fit a full glyph so are not used.
    struct Vec2i screenExcess;
    int glyphIndices[1024 * 1024];
    int glyphColors[1024 * 1024];
    struct Vec2i glyphOffsets[1024 * 1024];
};
