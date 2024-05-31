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
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "terminal.h"
#include "commands.h"

struct Buffer {
    int length;
    unsigned char *data;
};

struct RenderContext renderContext;

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
        } else if (key == GLFW_KEY_TAB) {
            key = 0x9;
        } else if (key == GLFW_KEY_BACKSPACE) {
            key = 0x8;
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

        if ((key >= 32 && key <= 126) || key == 0xD || key == 0x3 || key == 0x9 || key == 0x8) {
            bufferKey = 1;
        }
    }

    if (bufferKey) {
        int current = renderContext.keyBuffer.currentIndex;
        if (current >= renderContext.keyBuffer.length) {
            printf("Exceeded key input buffer.\n");
            exit(-1);
        }
        renderContext.keyBuffer.data[current] = key;
        renderContext.keyBuffer.currentIndex += 1;
    }
}

static void scrollCallback(GLFWwindow *window, double xOffset, double yOffset) {
    // TODO: this doesn't work for non-integer scroll values

    int updateShaderBuffer = 0;
    if (yOffset < 0 && renderContext.scrollOffset > 0) {
        renderContext.scrollOffset -= 1;
        updateShaderBuffer = 1;
    } else if (yOffset > 0 && renderContext.scrollOffset < MAX_ROWS - renderContext.screenTileSize.y) {
        renderContext.scrollOffset += 1;
        updateShaderBuffer = 1;
    }

    if (updateShaderBuffer) {
        renderContext.shaderContext->glyphIndicesRowOffset = renderContext.glyphIndicesRowOffset - renderContext.scrollOffset;
    }
}

static int intMax(int a, int b) {
    return a > b ? a : b;
}

static int intMin(int a, int b) {
    return a < b ? a : b;
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

GLuint generateGlyphAtlas(GLuint shaderContextId) {
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

    if (FT_Set_Pixel_Sizes(face, 0, renderContext.fontSize)) {
        printf("Failed to set font size.\n");
        exit(-1);
    }
    renderContext.screenGlyphSize = (struct Vec2i) {
        .x = face->size->metrics.max_advance >> 6,
        .y = face->size->metrics.height >> 6
    };

    if (FT_Set_Pixel_Sizes(face, 0, renderContext.atlasFontHeight)) {
        printf("Failed to set font size.\n");
        exit(-1);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderContext.shaderContextId);
    GLvoid *ssboPointer = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct TextShaderContext), GL_MAP_WRITE_BIT);
    struct TextShaderContext *shaderContext = (struct TextShaderContext*) ssboPointer;

    FT_Pos maxWidth = 0, maxHeight = 0, maxAboveBaseline = 0, maxBelowBaseline = 0;
    for (int asciiCode = 33; asciiCode <= 126; asciiCode++) {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, asciiCode);
        FT_Load_Glyph(face, glyphIndex, 0);

        shaderContext->glyphOffsets[asciiCode - 33].x = face->glyph->metrics.horiBearingX >> 6;
        shaderContext->glyphOffsets[asciiCode - 33].y = face->glyph->metrics.horiBearingY >> 6;

        int pixelWidth = face->glyph->metrics.width >> 6;
        int pixelHeight = face->glyph->metrics.height >> 6;

        int aboveBaseline = face->glyph->metrics.horiBearingY >> 6;
        int belowBaseline = (face->glyph->metrics.height >> 6) - (face->glyph->metrics.horiBearingY >> 6);

        if (pixelWidth > maxWidth) maxWidth = pixelWidth;
        if (pixelHeight > maxHeight) maxHeight = pixelHeight;
        if (aboveBaseline > maxAboveBaseline) maxAboveBaseline = aboveBaseline;
        if (belowBaseline > maxBelowBaseline) maxBelowBaseline = belowBaseline;
    }
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    renderContext.atlasGlyphSize = (struct Vec2i) {
        .x = face->size->metrics.max_advance >> 6,
        .y = face->size->metrics.height >> 6
    };

    int lineSpacing = renderContext.atlasGlyphSize.y - maxHeight;
    renderContext.lineSpacing = lineSpacing;

    printf(
        "Max glyph dimensions are (%ld, %ld), screen glyph size is (%d, %d), atlas glyph size is (%d, %d).\n",
        maxWidth, maxHeight, renderContext.screenGlyphSize.x, renderContext.screenGlyphSize.y, renderContext.atlasGlyphSize.x, renderContext.atlasGlyphSize.y);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, renderContext.atlasTileSize.x * renderContext.atlasGlyphSize.x, renderContext.atlasTileSize.y * renderContext.atlasGlyphSize.y, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int nextTileX = 1;
    int nextTileY = 0;
    char *flippedBitmap = malloc(sizeof(char) * renderContext.atlasGlyphSize.x * renderContext.atlasGlyphSize.y);

    for (int asciiCode = 33; asciiCode <= 126; asciiCode++) {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, asciiCode);
        FT_Load_Glyph(face, glyphIndex, 0);
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        }
        FT_Bitmap *bitmap = &face->glyph->bitmap;

        unsigned int bitmapWidth = face->glyph->bitmap.width;
        unsigned int bitmapHeight = face->glyph->bitmap.rows;

        int xOffset = (int) face->glyph->metrics.horiBearingX >> 6;
        int yOffset = (int) (face->glyph->metrics.height - face->glyph->metrics.horiBearingY) >> 6;
        memset(flippedBitmap, 0, renderContext.atlasGlyphSize.x * renderContext.atlasGlyphSize.y);
        for (int x = 0; x < bitmapWidth; x++) {
            for (int y = 0; y < bitmapHeight; y++) {
                int adjustedX = x + xOffset;
                int adjustedY = intMin(y + lineSpacing + maxBelowBaseline - yOffset, renderContext.atlasGlyphSize.y - 1);
                flippedBitmap[adjustedY * renderContext.atlasGlyphSize.x + adjustedX] = bitmap->buffer[(bitmapHeight - y - 1) * bitmapWidth + x];
            }
        }

        // Each glyph is written to the bottom left of a rectangle whose width is the horizontal advance and whose height is the line height.
        // The vertical spacing between lines occupies the bottom n pixels of each tile.
        glTexSubImage2D(GL_TEXTURE_2D, 0, nextTileX * renderContext.atlasGlyphSize.x, nextTileY * renderContext.atlasGlyphSize.y, renderContext.atlasGlyphSize.x, renderContext.atlasGlyphSize.y, GL_RED, GL_UNSIGNED_BYTE, flippedBitmap);
        renderContext.characterAtlasMap[asciiCode] = nextTileY * renderContext.atlasTileSize.x + nextTileX;
        nextTileX += 1;
        if (nextTileX >= renderContext.atlasTileSize.x) {
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

void spawnShell() {
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
    renderContext.controlFd = controlFd;
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

void renderSetup() {
    if (glfwInit() == GLFW_FALSE) {
        printf("Failed to init GLFW.\n");
        exit(-1);
    }

    glfwSetErrorCallback(errorCallback);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(960, 480, "terminal", 0, 0);
    if (!window) {
        printf("GLFW failed to create window.\n");
        exit(-1);
    }
    renderContext.window = window;
    glfwSetWindowSizeLimits(window, 300, 20, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    // Background color
    glClearColor(0.156, 0.172, 0.203, 1.0);

    // Enable blending so text glyphs can cut out background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  

    float rectangleVertices[] = {
		1, 1, 0,
		1, -1, 0,
		-1, -1, 0,
        -1, 1, 0,
		1, 1, 0,
        -1, -1, 0
	};

    // Setup vertices and shaders for text area

    GLuint vao, vbo, shaderContextId;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &shaderContextId);
    renderContext.textVAO = vao;
    renderContext.shaderContextId = shaderContextId;

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVertices), rectangleVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderContextId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(struct TextShaderContext), 0, GL_DYNAMIC_COPY);

    const int shaderContextIndex = 2;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, shaderContextIndex, shaderContextId);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    renderContext.atlasTextureId = generateGlyphAtlas(shaderContextId);

    const GLuint textVertexShader = compileShader("shaders/text_vertex.glsl", GL_VERTEX_SHADER);
    const GLuint textFragmentShader = compileShader("shaders/text_fragment.glsl", GL_FRAGMENT_SHADER);

    renderContext.textProgramId = glCreateProgram();
    glAttachShader(renderContext.textProgramId, textVertexShader);
    glAttachShader(renderContext.textProgramId, textFragmentShader);
    glLinkProgram(renderContext.textProgramId);
    glUseProgram(renderContext.textProgramId);

    GLint vertexPositionLocation = glGetAttribLocation(renderContext.textProgramId, "vertexPosition");
    glEnableVertexAttribArray(vertexPositionLocation);
    glVertexAttribPointer(vertexPositionLocation, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*) 0);

    renderContext.paddingTransformLocation = glGetUniformLocation(renderContext.textProgramId, "paddingTransform");

    glUniform2f(glGetUniformLocation(renderContext.textProgramId, "windowPadding"), renderContext.windowPadding[2], renderContext.windowPadding[0]);

    // Setup vertices and shaders for cursor

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    renderContext.cursorVAO = vao;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectangleVertices), rectangleVertices, GL_STATIC_DRAW);

    const GLuint cursorVertexShader = compileShader("shaders/cursor_vertex.glsl", GL_VERTEX_SHADER);
    const GLuint cursorFragmentShader = compileShader("shaders/cursor_fragment.glsl", GL_FRAGMENT_SHADER);

    renderContext.cursorProgramId = glCreateProgram();
    glAttachShader(renderContext.cursorProgramId, cursorVertexShader);
    glAttachShader(renderContext.cursorProgramId, cursorFragmentShader);
    glLinkProgram(renderContext.cursorProgramId);
    glUseProgram(renderContext.cursorProgramId);

    vertexPositionLocation = glGetAttribLocation(renderContext.cursorProgramId, "vertexPosition");
    glEnableVertexAttribArray(vertexPositionLocation);
    glVertexAttribPointer(vertexPositionLocation, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*) 0);

    renderContext.cursorTransformLocation = glGetUniformLocation(renderContext.cursorProgramId, "cursorTransform");
    renderContext.cursorTimeLocation = glGetUniformLocation(renderContext.cursorProgramId, "time");

    glUseProgram(0);
    glBindVertexArray(0);
}

void sendKeyInputToShell() {
    renderContext.keyBuffer.data[renderContext.keyBuffer.currentIndex] = '\0';
    write(renderContext.controlFd, renderContext.keyBuffer.data, renderContext.keyBuffer.currentIndex);

    int logBytes = 0;
    if (logBytes) {
        printf("writing %d bytes to control: [", renderContext.keyBuffer.currentIndex);
        for (int i = 0; i < renderContext.keyBuffer.currentIndex; i++) {
            if (isprint(renderContext.keyBuffer.data[i])) {
                printf("%c", renderContext.keyBuffer.data[i]);
            } else {
                printf("\\x%x", renderContext.keyBuffer.data[i]);
            }
        }
        printf("]\n");
    }

    renderContext.keyBuffer.currentIndex = 0;
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

    // if (i > 0) {
    //     printf("Reading %d bytes from control: [", i);
    //     for (int j = 0; j < i; j++) {
    //         if (isprint(buffer->data[j])) {
    //             printf("%c", buffer->data[j]);
    //         } else {
    //             printf("\\x%x", buffer->data[j]);
    //         }
    //     }
    //     printf("]\n");
    // }

    return i;
}

void updateText(struct Buffer *buffer) {
    struct TextShaderContext *shaderContext = renderContext.shaderContext;

    // Should these be updated every time?
    shaderContext->atlasGlyphSize = renderContext.atlasGlyphSize;
    shaderContext->screenGlyphSize = renderContext.screenGlyphSize;
    shaderContext->atlasTileSize = renderContext.atlasTileSize;

    for (int i = 0; i < buffer->length; i++) {
        if (buffer->data[i] == '\0') break;

        int character, prevRowOffset = renderContext.glyphIndicesRowOffset;
        if (!processTextByte(buffer->data[i], &character)) {
            // processTextByte can update the row offset in the case of a newline command. In this case, the previous text
            // at the next row is cleared.
            if (renderContext.glyphIndicesRowOffset != prevRowOffset) {
                int newRowIndex = ((renderContext.cursorPosition.y + renderContext.glyphIndicesRowOffset) % MAX_ROWS) * MAX_CHARACTERS_PER_ROW;
                memset(&shaderContext->glyphIndices[newRowIndex], 0, MAX_CHARACTERS_PER_ROW * sizeof(int));
            }
            continue;
        }

        // Reset scroll offset to jump back to current line when there are printed characters.
        renderContext.scrollOffset = 0;

        int glyphIndex = ((renderContext.cursorPosition.y + renderContext.glyphIndicesRowOffset) % MAX_ROWS) * MAX_CHARACTERS_PER_ROW + renderContext.cursorPosition.x;
        shaderContext->glyphIndicesRowOffset = renderContext.glyphIndicesRowOffset;
        shaderContext->glyphIndices[glyphIndex] = renderContext.characterAtlasMap[character];
        shaderContext->glyphColors[glyphIndex] = renderContext.foregroundColor;

        renderContext.cursorPosition.x++;

        // TODO: this is kind of performing line wrapping...should that be handled here?
        if (renderContext.cursorPosition.x >= renderContext.screenTileSize.x) {
            renderContext.cursorPosition.x = 0;
            renderContext.cursorPosition.y++;
        }
    }
}

void updatePaddingTransform() {
    const float desiredWidth = renderContext.screenSize.x - renderContext.windowPadding[2] - renderContext.windowPadding[3];
    const float desiredHeight = renderContext.screenSize.y - renderContext.windowPadding[0] - renderContext.windowPadding[1];

    const float scaleF[3] = {
        desiredWidth / renderContext.screenSize.x,
        desiredHeight / renderContext.screenSize.y,
        1
    };

    float ux = 1 / (desiredWidth / 2);
    float uy = 1 / (desiredHeight / 2);

    const float translateF[3] = {
        -ux * (renderContext.screenSize.x / 2 - (desiredWidth / 2) - renderContext.windowPadding[2]),
        uy * (renderContext.screenSize.y / 2 - (desiredHeight / 2) - renderContext.windowPadding[0]),
        0
    };

    mat4 mat;
    glm_mat4_identity(mat);
    glm_scale(mat, (float*) scaleF);
    glm_translate(mat, (float*) translateF);
    glUniformMatrix4fv(renderContext.paddingTransformLocation, 1, GL_FALSE, (GLfloat*) mat);
}

void updateCursorTransform() {
    const float tileWidth = renderContext.screenGlyphSize.x;
    const float tileHeight = renderContext.screenGlyphSize.y;

    const float cursorWidth = tileWidth;
    const float cursorHeight = tileHeight - renderContext.lineSpacing;

    const float scaleF[3] = {
        cursorWidth / renderContext.screenSize.x,
        cursorHeight / renderContext.screenSize.y,
        1
    };

    float ux = 1 / (cursorWidth / 2);
    float uy = 1 / (cursorHeight / 2);

    const float translateF[3] = {
        -ux * (renderContext.screenSize.x / 2 - (tileWidth / 2) - renderContext.windowPadding[2] - renderContext.cursorPosition.x * tileWidth),
        uy * ((renderContext.screenSize.y / 2) - (tileHeight / 2) - renderContext.windowPadding[0] - renderContext.cursorPosition.y * tileHeight + (renderContext.lineSpacing / 2.0)),
        0
    };

    mat4 mat;
    glm_mat4_identity(mat);
    glm_scale(mat, (float*) scaleF);
    glm_translate(mat, (float*) translateF);

    glUseProgram(renderContext.cursorProgramId);
    glUniformMatrix4fv(renderContext.cursorTransformLocation, 1, GL_FALSE, (GLfloat*) mat);
}

void onWindowResize(int newWidth, int newHeight) {
    renderContext.screenSize.x = newWidth;
    renderContext.screenSize.y = newHeight;

    newWidth -= renderContext.windowPadding[2] + renderContext.windowPadding[3];
    newHeight -= renderContext.windowPadding[0] + renderContext.windowPadding[1];

    renderContext.screenTileSize = (struct Vec2i) {
        .x = newWidth / renderContext.screenGlyphSize.x,
        .y = newHeight / renderContext.screenGlyphSize.y
    };

    struct Vec2i screenExcess = {
        .x = newWidth % renderContext.screenGlyphSize.x,
        .y = newHeight % renderContext.screenGlyphSize.y
    };

    if (renderContext.cursorPosition.x >= renderContext.screenTileSize.x) {
        renderContext.cursorPosition.x = renderContext.screenTileSize.x - 1;
    }

    if (renderContext.cursorPosition.y >= renderContext.screenTileSize.y) {
        renderContext.cursorPosition.y = renderContext.screenTileSize.y - 1;
    }

    updatePaddingTransform();

    // Update size related information in the shader context.
    renderContext.shaderContext->screenSize = renderContext.screenSize;
    renderContext.shaderContext->screenTileSize = renderContext.screenTileSize;
    renderContext.shaderContext->screenExcess = screenExcess;

    // Inform pseudo-terminal of new window size
    struct winsize windowSize = {
        .ws_col = renderContext.screenTileSize.x,
        .ws_row = renderContext.screenTileSize.y
    };
    ioctl(renderContext.controlFd, TIOCGWINSZ, &windowSize);

    printf("Window size update: (%d, %d), screen tile size = (%d, %d), excess = (%d, %d), cursor = (%d, %d)\n",
        renderContext.screenSize.x, renderContext.screenSize.y, renderContext.screenTileSize.x, renderContext.screenTileSize.y,
        screenExcess.x, screenExcess.y, renderContext.cursorPosition.x, renderContext.cursorPosition.y);
}

void render() {
    glViewport(0, 0, renderContext.screenSize.x, renderContext.screenSize.y);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderContext.atlasTextureId);
    glBindVertexArray(renderContext.textVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(renderContext.cursorVAO);
    glUseProgram(renderContext.cursorProgramId);
    glUniform1f(renderContext.cursorTimeLocation, (float) glfwGetTime());
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);

    glfwSwapBuffers(renderContext.window);
}

int main(int argc, char** argv) {
    renderContext.screenSize.x = 0;
    renderContext.screenSize.y = 0;
    renderContext.windowPadding[0] = 10;
    renderContext.windowPadding[1] = 10;
    renderContext.windowPadding[2] = 10;
    renderContext.windowPadding[3] = 20;
    renderContext.fontSize = 16;
    /*
    fontSize is used to control the actual screen-space size of the rendered text. atlasFontHeight is used to control
    the size of text rendered to the atlas texture. I thought that rendering high resolution glyphs to the atlas texture
    would allow for sampling at smaller font sizes, but the text looks worse in this situation. Having the atlas font size
    match the rendered font size looks much better.

    Would be nice to do the sampling so that the font size can be changed without recreating the atlas texture.
    */
    renderContext.atlasFontHeight = 16;
    renderContext.atlasTileSize = (struct Vec2i) { .x = 12, .y = 12 };
    renderContext.characterAtlasMap = (int*) malloc(sizeof(int) * renderContext.atlasTileSize.x * renderContext.atlasTileSize.y);
    renderContext.foregroundColor = 0x00FFFFFF;

    renderContext.keyBuffer = (struct KeyBuffer) {
        .currentIndex = 0,
        .length = 1024,
        .data = 0
    };
    renderContext.keyBuffer.data = malloc(sizeof(char) * renderContext.keyBuffer.length);
    renderContext.glyphOffsets = malloc(sizeof(struct Vec2i) * renderContext.atlasTileSize.x * renderContext.atlasTileSize.y);
    renderContext.cursorPosition.x = 0;
    renderContext.cursorPosition.y = 0;

    renderSetup();
    spawnShell();

    struct Buffer shellOutputBuffer;
    shellOutputBuffer.length = 1024;
    shellOutputBuffer.data = malloc(sizeof(char) * shellOutputBuffer.length);

    while (!glfwWindowShouldClose(renderContext.window)) {
        if (renderContext.keyBuffer.currentIndex > 0) {
            sendKeyInputToShell();
        }

        glUseProgram(renderContext.textProgramId);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderContext.shaderContextId);
        GLvoid *ssboPointer = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(struct TextShaderContext), GL_MAP_WRITE_BIT);
        renderContext.shaderContext = (struct TextShaderContext*) ssboPointer;

        glfwPollEvents();

        int width, height;
        glfwGetFramebufferSize(renderContext.window, &width, &height);
        if (width != renderContext.screenSize.x || height != renderContext.screenSize.y) {
            onWindowResize(width, height);
        }

        int bytesRead = pollShell(renderContext.controlFd, &shellOutputBuffer);
        struct Vec2i previousCursorPosition = renderContext.cursorPosition;
        if (bytesRead > 0) {
            updateText(&shellOutputBuffer);
        }

        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        if (renderContext.cursorPosition.x != previousCursorPosition.x || renderContext.cursorPosition.y != previousCursorPosition.y) {
            updateCursorTransform();
        }

        render();
    }

    free(renderContext.characterAtlasMap);
    free(renderContext.keyBuffer.data);
    free(renderContext.glyphOffsets);
    free(shellOutputBuffer.data);

    glfwDestroyWindow(renderContext.window);
    glfwTerminate();
    return 0;
}
