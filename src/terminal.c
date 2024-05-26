#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../lib/glad/glad.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pty.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>


struct Buffer {
    int length;
    unsigned char *data;
};

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
    struct Vec2i glyphOffsets[1024 * 1024];
};

static void errorCallback(int error, const char* description) {
    printf("GLFW error callback: (%d) %s\n", error, description);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    int bufferKey = 0;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_ENTER) {
            key = 0xD;
        } else if (key >= 65 && key <= 90) {
            int shift = mods & GLFW_MOD_SHIFT;
            if (!shift) {
                key += 32;
            }
            
            if (mods & GLFW_MOD_CONTROL) {
                int offset = shift ? 64 : 96;
                key = key - offset;
            }
        }

        if ((key >= 32 && key <= 126) || key == 0xD || key == 0x3) {
            bufferKey = 1;
        }
    }

    if (bufferKey) {
        struct RenderContext *context = (struct RenderContext*) glfwGetWindowUserPointer(window);
        int current = context->keyBuffer.currentIndex;
        if (current >= context->keyBuffer.length) {
            printf("Exceeded key input buffer.\n");
            exit(-1);
        }
        context->keyBuffer.data[current] = key;
        context->keyBuffer.currentIndex += 1;
    }
}

int lastIndexOf(char *string, int size, char c) {
    for (int i = size; i >= 0; i--) {
        if (string[i] == c) {
            return i;
        }
    }
    return -1;
}

char* buildRelativePath(char *path) {
    char *pathBuffer = malloc(PATH_MAX);
    int bytes = readlink("/proc/self/exe", pathBuffer, PATH_MAX);
    if (bytes == -1) {
        printf("Failed to read proc/self/exe");
        exit(-1);
    }
    pathBuffer[bytes] = '\0';
    pathBuffer[lastIndexOf(pathBuffer, bytes, '/')] = '\0';
    sprintf(pathBuffer, "%s/%s%c", pathBuffer, path, '\0');

    return pathBuffer;
}

GLuint generateGlyphAtlas(GLuint shaderContextId, struct RenderContext *renderContext) {
    FT_Library library;
    if (FT_Init_FreeType(&library)) {
        printf("Failed to init freetype.\n");
        exit(-1);
    }

    char *fontPath = buildRelativePath("fonts/UbuntuMono-R.ttf");
    FT_Face face;
    if (FT_New_Face(library, fontPath, 0, &face)) {
        printf("Failed to load font at path %s.\n", fontPath);
        exit(-1);
    }
    free(fontPath);

    if (FT_Set_Pixel_Sizes(face, 0, renderContext->fontSize)) {
        printf("Failed to set font size.\n");
        exit(-1);
    }
    renderContext->screenGlyphSize = (struct Vec2i) {
        .x = face->size->metrics.max_advance >> 6,
        .y = face->size->metrics.height >> 6
    };

    if (FT_Set_Pixel_Sizes(face, 0, renderContext->atlasFontHeight)) {
        printf("Failed to set font size.\n");
        exit(-1);
    }
    renderContext->atlasGlyphSize = (struct Vec2i) {
        .x = face->size->metrics.max_advance >> 6,
        .y = face->size->metrics.height >> 6
    };

    FT_Pos maxWidth = 0, maxHeight = 0;
    for (int asciiCode = 33; asciiCode <= 126; asciiCode++) {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, asciiCode);
        FT_Load_Glyph(face, glyphIndex, 0);

        int pixelWidth = face->glyph->metrics.width >> 6;
        int pixelHeight = face->glyph->metrics.height >> 6;

        if (pixelWidth > maxWidth) maxWidth = pixelWidth;
        if (pixelHeight > maxHeight) maxHeight = pixelHeight;
    }
    printf(
        "Max glyph dimensions are (%ld, %ld), screen glyph size is (%d, %d), atlas glyph size is (%d, %d).\n",
        maxWidth, maxHeight, renderContext->screenGlyphSize.x, renderContext->screenGlyphSize.y, renderContext->atlasGlyphSize.x, renderContext->atlasGlyphSize.y);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, renderContext->atlasTileSize.x * renderContext->atlasGlyphSize.x, renderContext->atlasTileSize.y * renderContext->atlasGlyphSize.y, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int nextTileX = 1;
    int nextTileY = 0;
    char *flippedBitmap = malloc(sizeof(char) * renderContext->atlasGlyphSize.x * renderContext->atlasGlyphSize.y);

    for (int asciiCode = 33; asciiCode <= 126; asciiCode++) {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, asciiCode);
        FT_Load_Glyph(face, glyphIndex, 0);
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        }
        FT_Bitmap *bitmap = &face->glyph->bitmap;

        unsigned int bitmapWidth = face->glyph->bitmap.width;
        unsigned int bitmapHeight = face->glyph->bitmap.rows;

        renderContext->glyphOffsets[asciiCode - 33].x = face->glyph->metrics.horiBearingX;
        renderContext->glyphOffsets[asciiCode - 33].y = face->glyph->metrics.horiBearingY;

        memset(flippedBitmap, 0, renderContext->atlasGlyphSize.x * renderContext->atlasGlyphSize.y);
        for (int x = 0; x < bitmapWidth; x++) {
            for (int y = 0; y < bitmapHeight; y++) {
                flippedBitmap[y * renderContext->atlasGlyphSize.x + x] = bitmap->buffer[(bitmapHeight - y - 1) * bitmapWidth + x];
            }
        }

        // Each glyph is written to the bottom left of a rectangle whose width is the horizontal advance and whose height is the line height.
        glTexSubImage2D(GL_TEXTURE_2D, 0, nextTileX * renderContext->atlasGlyphSize.x, nextTileY * renderContext->atlasGlyphSize.y, renderContext->atlasGlyphSize.x, renderContext->atlasGlyphSize.y, GL_RED, GL_UNSIGNED_BYTE, flippedBitmap);
        renderContext->characterAtlasMap[asciiCode] = nextTileY * renderContext->atlasTileSize.x + nextTileX;
        nextTileX += 1;
        if (nextTileX >= renderContext->atlasTileSize.x) {
            nextTileX = 0;
            nextTileY += 1;
        }
    }

    free(flippedBitmap);
    FT_Done_Face(face);
    FT_Done_FreeType(library);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

int spawnShell() {
    int controlFd;
    int pid = forkpty(&controlFd, 0, 0, 0);
    if (pid == -1) {
        printf("forkpty failed.\n");
        exit(-1);
    } else if (pid == 0) {
        execl("/bin/bash", "/bin/bash");
        exit(0);
    } else {
        // Set the file descriptor to be non-blocking.
        // Unsure if this is needed. Using poll() to check when data is available might
        // be better, but hasn't worked yet.
        int flags = fcntl(controlFd, F_GETFL, 0);
        fcntl(controlFd, F_SETFL, flags | O_NONBLOCK);
    }
    return controlFd;
}

GLuint compileShader(char* shaderPath, GLenum shaderType) {
    char *path = buildRelativePath(shaderPath);
    FILE *file = fopen(path, "r");
    if (file == 0) {
        printf("Missing shader at %s.\n", path);
        exit(-1);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        printf("Failed to fseek to end of file at %s.\n", path);
        exit(-1);
    }

    long fileSize = ftell(file);
    if (fileSize == -1) {
        printf("Failed to ftell size of tile at %s.\n", path);
        exit(-1);
    }
    fseek(file, 0, SEEK_SET);

    char *fileBuffer = malloc(sizeof(char) * (fileSize + 1));
    int bytesWritten = fread(fileBuffer, sizeof(char), fileSize, file);
    fileBuffer[bytesWritten] = '\0';

    const GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, (const char *const *) &fileBuffer, 0);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        printf("Failed to compile shader at %s.\n", path);
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
        char *errorLog = malloc(sizeof(char) * maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, errorLog);
        printf("%s\n", errorLog);
        free(errorLog);
        exit(-1);
    }

    free(path);
    free(fileBuffer);
    fclose(file);

    return shader;
}

void renderSetup(struct RenderContext *renderContext) {
    if (glfwInit() == GLFW_FALSE) {
        printf("Failed to init GLFW.\n");
        exit(-1);
    }

    glfwSetErrorCallback(errorCallback);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(renderContext->screenSize.x, renderContext->screenSize.y, "terminal", 0, 0);
    if (!window) {
        printf("GLFW failed to create window.\n");
        exit(-1);
    }
    glfwSetWindowUserPointer(window, renderContext);

    glfwSetKeyCallback(window, keyCallback);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    // Background color
    glClearColor(0.184, 0.290, 0.380, 1.0);

    // Enable blending so text glyphs can cut out background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  

    float vertexData[] = {
		1, 1, 0, 1, 1,
		1, -1, 0, 1, 0,
		-1, -1, 0, 0, 0,
		-1, 1, 0, 0, 1
	};

	unsigned int indexData[] = {
		1, 3, 2,
		1, 0, 3
	};

    GLuint vao, vbo, ebo, shaderContextId;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &shaderContextId);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indexData), indexData, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderContextId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(struct TextShaderContext), 0, GL_DYNAMIC_COPY);

    const int shaderContextIndex = 2;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderContextIndex, shaderContextId);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    GLuint texture = generateGlyphAtlas(shaderContextId, renderContext);

    const GLuint vertexShader = compileShader("shaders/text_vertex.glsl", GL_VERTEX_SHADER);
    const GLuint fragmentShader = compileShader("shaders/text_fragment.glsl", GL_FRAGMENT_SHADER);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint vertexPositionLocation = glGetAttribLocation(program, "vertexPosition");
    glEnableVertexAttribArray(vertexPositionLocation);
    glVertexAttribPointer(vertexPositionLocation, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*) 0);

    renderContext->window = window;

    renderContext->programId = program;
    renderContext->vaoId = vao;
    renderContext->shaderContextId = shaderContextId;
    renderContext->atlasTextureId = texture;
}

void sendKeyInputToShell(int controlFd, struct RenderContext *context) {
    context->keyBuffer.data[context->keyBuffer.currentIndex] = '\0';
    write(controlFd, context->keyBuffer.data, context->keyBuffer.currentIndex);
    printf("writing %d bytes to control: [", context->keyBuffer.currentIndex);
    for (int i = 0; i < context->keyBuffer.currentIndex; i++) {
        if (isprint(context->keyBuffer.data[i])) {
            printf("%c", context->keyBuffer.data[i]);
        } else {
            printf("\\x%x", context->keyBuffer.data[i]);
        }
    }
    printf("]\n");

    context->keyBuffer.currentIndex = 0;
}

int pollShell(int controlFd, struct Buffer *buffer) {
    int i = 0;
    while (i < buffer->length - 1) {
        char c;
        int bytesRead = read(controlFd, &c, 1);
        if (bytesRead > -1) {
            buffer->data[i++] = c;
        } else {
            break;
        }
    }
    buffer->data[i] = 0;

    if (i > 0) {
        printf("Reading %d bytes from control: [", i);
        for (int j = 0; j < i; j++) {
            if (isprint(buffer->data[j])) {
                printf("%c", buffer->data[j]);
            } else {
                printf("\\x%x", buffer->data[j]);
            }
        }
        printf("]\n");
    }

    return i;
}

void updateText(struct RenderContext *context, struct Buffer *buffer) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, context->shaderContextId);
    GLvoid *ssboPointer = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct TextShaderContext), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    struct TextShaderContext *shaderContext = (struct TextShaderContext*) ssboPointer;

    // Should these be updated every time?
    shaderContext->atlasGlyphSize = context->atlasGlyphSize;
    shaderContext->screenGlyphSize = context->screenGlyphSize;
    shaderContext->atlasTileSize = context->atlasTileSize;

    for (int i = 0; i < buffer->length; i++) {
        if (buffer->data[i] == '\0') break;
        if (!isprint(buffer->data[i])) continue;
        int glyphIndex = context->cursorPosition.y * context->screenTileSize.x + context->cursorPosition.x;
        if (glyphIndex >= context->screenTileSize.x * context->screenTileSize.y) continue;

        shaderContext->glyphIndices[glyphIndex] = context->characterAtlasMap[buffer->data[i]];

        context->cursorPosition.x++;
        if (context->cursorPosition.x >= context->screenTileSize.x) {
            context->cursorPosition.x = 0;
            context->cursorPosition.y--;
        }

        if (context->cursorPosition.y < 0) {
            context->cursorPosition.x = 0;
            context->cursorPosition.y = context->screenTileSize.y - 1;
        }
    }

    // For debugging
    // for (int i = 0; i < 40; i++) {
    //     shaderContext->glyphIndices[i] = i;
    // }
    // for (int i = 100; i < context->screenTileSize.x * context->screenTileSize.y; i++) {
    //     shaderContext->glyphIndices[i] = 0;
    // }

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}

void onWindowResize(struct RenderContext* context, int newWidth, int newHeight) {
    glUniform2f(glGetUniformLocation(context->programId, "resolution"), newWidth, newHeight);
    context->screenSize.x = newWidth;
    context->screenSize.y = newHeight;

    context->screenTileSize = (struct Vec2i) {
        .x = newWidth / context->screenGlyphSize.x,
        .y = newHeight / context->screenGlyphSize.y
    };

    struct Vec2i screenExcess = {
        .x = newWidth % context->screenGlyphSize.x,
        .y = newHeight % context->screenGlyphSize.y
    };

    if (context->cursorPosition.x >= context->screenTileSize.x) {
        context->cursorPosition.x = context->screenTileSize.x - 1;
    }

    if (context->cursorPosition.y >= context->screenTileSize.y) {
        context->cursorPosition.y = context->screenTileSize.y - 1;
    }

    // Update size related information in the shader context.
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, context->shaderContextId);
    GLvoid *ssboPointer = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct TextShaderContext), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    struct TextShaderContext *shaderContext = (struct TextShaderContext*) ssboPointer;
    shaderContext->screenSize = context->screenSize;
    shaderContext->screenTileSize = context->screenTileSize;
    shaderContext->screenExcess = screenExcess;
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    printf("Window size update: (%d, %d), screen tile size = (%d, %d), excess = (%d, %d), cursor = (%d, %d)\n",
        context->screenSize.x, context->screenSize.y, context->screenTileSize.x, context->screenTileSize.y, screenExcess.x, screenExcess.y, context->cursorPosition.x, context->cursorPosition.y);
}

void render(struct RenderContext *context) {
    int width, height;
    glfwGetFramebufferSize(context->window, &width, &height);

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(context->programId);

    if (width != context->screenSize.x || height != context->screenSize.y) {
        onWindowResize(context, width, height);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, context->atlasTextureId);
    glBindVertexArray(context->vaoId);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glfwSwapBuffers(context->window);
    glfwPollEvents();
}

int main(int argc, char** argv) {
    struct RenderContext renderContext;

    renderContext.screenSize = (struct Vec2i) { .x = 640, .y = 480 };
    renderContext.fontSize = 40;
    /*
    fontSize is used to control the actual screen-space size of the rendered text. atlasFontHeight is used to control
    the size of text rendered to the atlas texture. I thought that rendering high resolution glyphs to the atlas texture
    would allow for sampling at smaller font sizes, but the text looks worse in this situation. Having the atlas font size
    match the rendered font size looks much better.

    Would be nice to do the sampling so that the font size can be changed without recreating the atlas texture.
    */
    renderContext.atlasFontHeight = 40;
    renderContext.atlasTileSize = (struct Vec2i) { .x = 12, .y = 12 };
    renderContext.characterAtlasMap = (int*) malloc(sizeof(int) * renderContext.atlasTileSize.x * renderContext.atlasTileSize.y);

    renderContext.keyBuffer = (struct KeyBuffer) {
        .currentIndex = 0,
        .length = 1024,
        .data = 0
    };
    renderContext.keyBuffer.data = malloc(sizeof(char) * renderContext.keyBuffer.length);
    renderContext.glyphOffsets = malloc(sizeof(struct Vec2i) * renderContext.atlasTileSize.x * renderContext.atlasTileSize.y);

    renderContext.cursorPosition.x = 0;
    renderContext.cursorPosition.y = 10;

    renderSetup(&renderContext);
    onWindowResize(&renderContext, renderContext.screenSize.x, renderContext.screenSize.y);
    int controlFd = spawnShell();

    struct Buffer shellOutputBuffer;
    shellOutputBuffer.length = 1024;
    shellOutputBuffer.data = malloc(sizeof(char) * shellOutputBuffer.length);

    while (!glfwWindowShouldClose(renderContext.window)) {
        if (renderContext.keyBuffer.currentIndex > 0) {
            sendKeyInputToShell(controlFd, &renderContext);
        }

        int bytesRead = pollShell(controlFd, &shellOutputBuffer);
        if (bytesRead > 0) {
            updateText(&renderContext, &shellOutputBuffer);
        }

        render(&renderContext);
    }

    free(renderContext.characterAtlasMap);
    free(renderContext.keyBuffer.data);
    free(renderContext.glyphOffsets);
    free(shellOutputBuffer.data);

    glfwDestroyWindow(renderContext.window);
    glfwTerminate();
    return 0;
}
