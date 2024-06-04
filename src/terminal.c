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
#include "keys.h"
#include "commands.h"
#include "glyph.h"

struct Buffer {
    int length;
    unsigned char *data;
};

struct RenderContext renderContext;
extern FT_Face face;

static void errorCallback(int error, const char* description) {
    printf("GLFW error callback: (%d) %s\n", error, description);
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    int bufferKey = 0;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if ((mods & GLFW_MOD_SHIFT) | (mods & GLFW_MOD_CAPS_LOCK)) {
            key = keyShiftMapping[key];
        } else if (mods & GLFW_MOD_CONTROL) {
            key = keyControlMapping[key];
        } else {
            key = keyNormalMapping[key];
        }

        if (key != KEY_UNMAPPED) {
            bufferKey = 1;
        }
    }

    if (bufferKey) {
        for (int i = 0; i < 4; i++) {
            int byte = key & 0xFF;
            key = key >> 8;
            if (byte == 0) continue;

            int current = renderContext.keyBuffer.currentIndex;
            if (current >= renderContext.keyBuffer.length) {
                printf("Exceeded key input buffer.\n");
                exit(-1);
            }
            renderContext.keyBuffer.data[current] = byte;
            renderContext.keyBuffer.currentIndex += 1;
        }
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
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(window);

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

    glGenTextures(1, &renderContext.atlasTextureId);
    glBindTexture(GL_TEXTURE_2D, renderContext.atlasTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, ATLAS_WIDTH * renderContext.atlasGlyphSize.x, ATLAS_HEIGHT * renderContext.atlasGlyphSize.y, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

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
    return i;
}

void updateText(struct Buffer *buffer) {
    struct TextShaderContext *shaderContext = renderContext.shaderContext;

    // Should these be updated every time?
    // TODO: move these to the baseline font setup function
    shaderContext->atlasGlyphSize = renderContext.atlasGlyphSize;
    shaderContext->screenGlyphSize = renderContext.screenGlyphSize;

    for (int i = 0; i < buffer->length; i++) {
        if (buffer->data[i] == '\0') break;

        int codePoint, prevRowOffset = renderContext.glyphIndicesRowOffset;
        if (!processTextByte(buffer->data[i], &codePoint)) {
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

        int atlasPosition = getGlyphAtlasPosition(codePoint);

        int glyphIndex = ((renderContext.cursorPosition.y + renderContext.glyphIndicesRowOffset) % MAX_ROWS) * MAX_CHARACTERS_PER_ROW + renderContext.cursorPosition.x;
        shaderContext->glyphIndicesRowOffset = renderContext.glyphIndicesRowOffset;
        shaderContext->glyphIndices[glyphIndex] = atlasPosition;
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
    glViewport(0, 0, renderContext.screenSize.x, renderContext.screenSize.y);

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
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderContext.atlasTextureId);
    glBindVertexArray(renderContext.textVAO);
    glUseProgram(renderContext.textProgramId);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(renderContext.cursorVAO);
    glUseProgram(renderContext.cursorProgramId);
    glUniform1f(renderContext.cursorTimeLocation, (float) glfwGetTime());
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glUseProgram(0);

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
    renderContext.foregroundColor = 0x00FFFFFF;

    renderContext.keyBuffer = (struct KeyBuffer) {
        .currentIndex = 0,
        .length = 32,
        .data = malloc(sizeof(char) * 32)
    };
    renderContext.cursorPosition.x = 0;
    renderContext.cursorPosition.y = 0;

    initKeyMappings();
    char *fontPath = buildRelativePath("fonts/UbuntuMono-R.ttf");
    loadBaselineFont(fontPath);
    free(fontPath);
    renderSetup();
    initGlyphCache();
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
    free(shellOutputBuffer.data);
    freeGlyphCache();

    glfwDestroyWindow(renderContext.window);
    glfwTerminate();
    return 0;
}
