#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../lib/glad/glad.h"

#include <cglm/cglm.h>

#define MAX_CHARACTERS_PER_ROW 500
#define MAX_ROWS 1000
#define ATLAS_WIDTH 32
#define ATLAS_HEIGHT 32

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
    int scrollOffset;
    // Pixel values for padding, [top, bottom, left, right]
    int windowPadding[4];
    // A pointer mapped to the SSBO shader context struct. This pointer is only valid for a
    // portion of the render loop, as the mapping occurs each frame.
    struct TextShaderContext *shaderContext;

    // Psuedo-terminal information
    int controlFd;

    // OpenGL information
    GLuint textProgramId;
    GLuint cursorProgramId;
    GLuint textVAO;
    GLuint cursorVAO;
    GLuint shaderContextId;
    GLuint atlasTextureId;
    GLuint paddingTransformLocation;
    GLuint cursorTransformLocation;
    GLuint cursorTimeLocation;

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
    int foregroundColor;
    int backgroundColor;
    int glyphIndicesRowOffset;
    int lineSpacing;
    int maxBelowBaseline;
};

struct TextShaderContext {
    // Number of rows to skip when reading from glyphIndices array. Used to implement a circular buffer.
    int glyphIndicesRowOffset;
    // Added as a simple method of aligning the C struct byte layout with the GLSL layout. GLSL expects the `atlasGlyphSize`
    // struct to be aligned at an 8 byte boundary, so this 4 byte int puts it at the same spot in the C struct.
    int padding0;
    // Pixel vector containing (advance, lineHeight) for glyphs in atlas texture.
    struct Vec2i atlasGlyphSize;
    // Pixel vector containing (advance, lineHeight) used when rendering to screen.
    struct Vec2i screenGlyphSize;
    // Pixel vector containing screen resolution.
    struct Vec2i screenSize;
    // Vector containing (columns, rows) for the screen grid.
    struct Vec2i screenTileSize;
    // Pixel vector containing the number of extra pixels on the right and bottom of the screen.
    // These areas do not fit a full glyph so are not used.
    struct Vec2i screenExcess;
    int glyphIndices[MAX_CHARACTERS_PER_ROW * MAX_ROWS];
    int glyphColors[MAX_CHARACTERS_PER_ROW * MAX_ROWS];
};
