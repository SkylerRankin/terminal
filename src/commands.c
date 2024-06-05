#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal.h"
#include "colors.h"
#include "commands.h"

enum Stage {
    STAGE_PLAIN_TEXT,
    STAGE_ESCAPE,
    STAGE_ARGUMENTS,
    STAGE_ARG_SEPERATOR
};

enum SequenceType {
    SEQUENCE_TYPE_NONE,
    SEQUENCE_TYPE_CSI,
    SEQUENCE_TYPE_DCS,
    SEQUENCE_TYPE_OSC
};

enum CommandState {
    COMMAND_NONE,
    COMMAND_OSC_WINDOW_TITLE,
    COMMAND_OSC_NOT_SUPPORTED
};

enum ColorInputState {
    COLOR_INPUT_NONE,
    COLOR_INPUT_FG,
    COLOR_INPUT_BG,
    COLOR_INPUT_8Bit,
    COLOR_INPUT_24Bit
};

struct Buffer {
    int length;
    int position;
    u8 data[128];
};

struct ParsingState {
    enum Stage currentStage;
    enum SequenceType sequenceType;
    enum CommandState commandState;
    enum ColorInputState colorInput;
    struct Buffer argBuffer;
    int characterBuffer;
    int characterByteIndex;
    int bytesInCharacter;
};

extern struct RenderContext renderContext;

static struct ParsingState state = {
    .currentStage = STAGE_PLAIN_TEXT,
    .sequenceType = SEQUENCE_TYPE_NONE,
    .commandState = COMMAND_NONE,
    .argBuffer = (struct Buffer) {
        .length = 128,
        .position = 0
    },
    .colorInput = COLOR_INPUT_NONE,
    .characterBuffer = 0,
    .characterByteIndex = 0,
    .bytesInCharacter = 0
};

static int handleStagePlainText(u8 byte, int *character);
static int handleStageEscape(u8 byte, int *character);
static int handleStageArguments(u8 byte, int *character);
static int executeCommandArgument();
static void executeC0ControlCode(u8 byte);

int processTextByte(u8 byte, int *character) {
    switch (state.currentStage) {
        case STAGE_PLAIN_TEXT:  return handleStagePlainText(byte, character);
        case STAGE_ESCAPE:      return handleStageEscape(byte, character);
        case STAGE_ARGUMENTS:   return handleStageArguments(byte, character);
        default:
            return 0;
    }
}

static int utf8EncodingToCodepoint(unsigned int encoding) {
    // 1 byte encoding
    if ((encoding >> 7) == 0) {
        return encoding & 0x7F;
    }

    // 2 byte encoding
    if ((encoding >> 13) == 0x6) {
        return ((encoding & 0x1F00) >> 0x2) | (encoding & 0x3F);
    }

    // 3 byte encoding
    if ((encoding >> 0x14) == 0xE) {
        return ((encoding & 0xF0000) >> 0x4) | ((encoding & 0x3F00) >> 0x2) | (encoding & 0x3F);
    }

    // 4 byte encoding
    if ((encoding >> 0x1B) == 0x1E) {
        return ((encoding & 0x7000000) >> 0x6) | ((encoding & 0x3F0000) >> 0x4) | ((encoding & 0x3F00) >> 0x2) | (encoding & 0x3F);
    }

    printf("Cannot create codepoint for invalid UTF-8 encoding %08x.\n", encoding);
    return 0xFFFD;
}

static void clearBuffer(struct Buffer *buffer) {
    buffer->position = 0;
    memset(buffer->data, 0, buffer->length);
}

static int handleStagePlainText(u8 byte, int *character) {
    if (byte == 0x1B) {
        state.currentStage = STAGE_ESCAPE;
        return 0;
    } else if ((byte >= 0x7 && byte <= 0xD) || byte == 0x7F) {
        executeC0ControlCode(byte);
        return 0;
    } else if (state.characterByteIndex == 0) {
        if (((byte >> 7) & 0x1) == 0) {
            // 1 byte encoding
            *character = byte;
            return 1;
        } else if (((byte >> 5) & 0x7) == 0x6) {
            // 2 byte encoding
            state.bytesInCharacter = 2;
            state.characterByteIndex++;
            state.characterBuffer = byte & 0xFF;
            return 0;
        } else if (((byte >> 4) & 0xF) == 0xE) {
            // 3 byte encoding
            state.bytesInCharacter = 3;
            state.characterByteIndex++;
            state.characterBuffer = byte & 0xFF;
            return 0;
        } else if (((byte >> 3) & 0x1F) == 0x1E) {
            // 4 byte encoding
            state.bytesInCharacter = 4;
            state.characterByteIndex++;
            state.characterBuffer = byte & 0xFF;
            return 0;
        } else {
            printf("Invalid\n");
            return 1;
        }
    } else {
        if (((byte >> 6) & 0x3) != 0x2) {
            printf("Invalid UTF-8 encoded byte\n");
        }
        state.characterBuffer = (state.characterBuffer << 8) | byte;
        if (state.bytesInCharacter == state.characterByteIndex + 1) {
            *character = utf8EncodingToCodepoint(state.characterBuffer);
            state.characterBuffer = 0;
            state.characterByteIndex = 0;
            state.bytesInCharacter = 0;
            return 1;
        } else {
            state.characterByteIndex++;
            return 0;
        }
    }
}

static int handleStageEscape(u8 byte, int *character) {
    switch (byte) {
        case 0x5B:
            state.currentStage = STAGE_ARGUMENTS;
            state.sequenceType = SEQUENCE_TYPE_CSI;
            clearBuffer(&state.argBuffer);
            return 0;
        case 0x50:
            state.currentStage = STAGE_ARGUMENTS;
            state.sequenceType = SEQUENCE_TYPE_DCS;
            clearBuffer(&state.argBuffer);
            return 0;
        case 0x5D:
            state.currentStage = STAGE_ARGUMENTS;
            state.sequenceType = SEQUENCE_TYPE_OSC;
            clearBuffer(&state.argBuffer);
            return 0;
        default:
            // The escape byte was not followed by an additional sequence byte, so treat
            // this byte as if operating in STAGE_ARGUMENT.
            return handleStageArguments(byte, character);
    }
}

static int handleStageArguments(u8 byte, int *character) {
    state.argBuffer.data[state.argBuffer.position] = byte;
    state.argBuffer.position++;
    int endOfCommand = executeCommandArgument();
    if (endOfCommand) {
        clearBuffer(&state.argBuffer);
        state.currentStage = STAGE_PLAIN_TEXT;
    } else {
        state.currentStage = STAGE_ARGUMENTS;
    }
    return 0;
}

static void executeC0ControlCode(u8 byte) {
    switch (byte) {
        case 0x7: // Bell sound
            printf("~bell sound~\n");
            break;
        case 0x8: // Backspace
            if (renderContext.cursorPosition.x > 0) {
                renderContext.cursorPosition.x -= 1;
            }
            break;
        case 0x9: // Tab
            renderContext.cursorPosition.x = renderContext.cursorPosition.x - (renderContext.cursorPosition.x % 8) + 8;
            break;
        case 0xA: // Line feed
            renderContext.cursorPosition.x = 0;
            renderContext.cursorPosition.y += 1;

            // If cursor position has passed the bottom row, cursor remains at the last row and the
            // row offset is incremented.
            if (renderContext.cursorPosition.y >= renderContext.screenTileSize.y) {
                renderContext.cursorPosition.y = renderContext.screenTileSize.y - 1;
                renderContext.glyphIndicesRowOffset = (renderContext.glyphIndicesRowOffset + 1) % MAX_ROWS;
            }
            break;
        case 0xD: // Carriage return
            renderContext.cursorPosition.x = 0;
            break;
    }
}

static void updateGraphicsState(int command, int index) {
    // TODO: handle state.colorInput = COLOR_INPUT_8Bit, COLOR_INPUT_24Bit, etc

    if (command == 0) {
        renderContext.foregroundColor = COLORS_FG[7];
        renderContext.backgroundColor = COLORS_BG[7];
    } else if (command == 38 && index == 0) {
        state.colorInput = COLOR_INPUT_FG;
    } else if (command == 48 && index == 0) {
        state.colorInput = COLOR_INPUT_BG;
    } else if (command >= 30 && command <= 37) {
        renderContext.foregroundColor = COLORS_FG[command - 30];
    } else if (command >= 40 && command <= 47) {
        renderContext.backgroundColor = COLORS_BG[command - 40];
    } else if (command >= 90 && command <= 97) {
        renderContext.foregroundColor = COLORS_FG_BRIGHT[command - 90];
    } else if (command >= 100 && command <= 107) {
        renderContext.backgroundColor = COLORS_BG_BRIGHT[command - 100];
    } else {
        // other graphics command
        printf("unhandled graphics command: %d (index=%d)\n", command, index);
    }

}

static void eraseScreenRect(int xStart, int xEnd, int yStart, int yEnd) {
    for (int y = yStart; y <= yEnd; y++) {
        int rowOffset = ((y + renderContext.glyphIndicesRowOffset) % MAX_ROWS) * MAX_CHARACTERS_PER_ROW;
        for (int x = xStart; x <= xEnd; x++) {
            renderContext.shaderContext->glyphIndices[rowOffset + x] = 0;
        }
    }
}

/**
 * CSI commands start with ESC[ and are followed by the following sections:
 *      1. Bytes in the range 0x30 – 0x3F
 *      2. Bytes in the range 0x20 – 0x2F
 *      3. Bytes in the range 0x40 – 0x7E
*/
static int executeCSICommandArgument() {
    if (state.argBuffer.position == 0) {
        return 1;
    }

    u8 lastByte = state.argBuffer.data[state.argBuffer.position - 1];

    // CSI sequence is not yet terminated.
    if (lastByte < 0x40 || lastByte > 0x7E) {
        return 0;
    }

    // Some CSI commands contain a list of semi-colon-separated integers used as arguments.
    const int maxCSIArguments = 20;
    int numArgs = 0;
    int args[maxCSIArguments];
    if ((lastByte >= 'A' && lastByte <= 'H') || lastByte == 'J' || lastByte == 'K' || lastByte == 'S' || lastByte == 'T' || lastByte == 'm') {
        int argIndex = 0, value = 0, parsingValue = 0;
        for (int i = 0; i < state.argBuffer.position; i++) {
            const u8 c = state.argBuffer.data[i];
            if (c == ';' || c == lastByte) {
                if (parsingValue) {
                    args[argIndex] = value;
                    argIndex++;
                }
                value = 0;
                parsingValue = 0;

                if (argIndex >= maxCSIArguments) {
                    printf("CSI command buffer filled, commands contains more than %d integer arguments: %s\n", maxCSIArguments, state.argBuffer.data);
                    break;
                }
            } else {
                value = (value * 10) + (c - 0x30);
                parsingValue = 1;
            }
        }
        numArgs = argIndex;
    }

    switch (lastByte) {
        case 'A': { // Cursor up
            int n = numArgs == 0 ? 1 : args[0];
            renderContext.cursorPosition.y -= n;
            if (renderContext.cursorPosition.y < 0) {
                renderContext.cursorPosition.y = 0;
            }
            break;
        }
        case 'B': { // Cursor down
            int n = numArgs == 0 ? 1 : args[0];
            renderContext.cursorPosition.y += n;
            if (renderContext.cursorPosition.y > renderContext.screenTileSize.y - 1) {
                renderContext.cursorPosition.y = renderContext.screenTileSize.y - 1;
            }
            break;
        }
        case 'C': { // Cursor forward
            int n = numArgs == 0 ? 1 : args[0];
            renderContext.cursorPosition.x += n;
            if (renderContext.cursorPosition.x > renderContext.screenTileSize.x - 1) {
                renderContext.cursorPosition.x = renderContext.screenTileSize.x - 1;
            }
            break;
        }
        case 'D': { // Cursor back
            int n = numArgs == 0 ? 1 : args[0];
            renderContext.cursorPosition.x -= n;
            if (renderContext.cursorPosition.x < 0) {
                renderContext.cursorPosition.x = 0;
            }
            break;
        }
        case 'E': { // Cursor next line
            int n = numArgs == 0 ? 1 : args[0];
            if (renderContext.cursorPosition.y + n < renderContext.screenTileSize.y) {
                renderContext.cursorPosition.x = 0;
                renderContext.cursorPosition.y += n;
            }
            break;
        }
        case 'F': { // Cursor previous line
            int n = numArgs == 0 ? 1 : args[0];
            if (renderContext.cursorPosition.y - n >= 0) {
                renderContext.cursorPosition.x = 0;
                renderContext.cursorPosition.y -= n;
            }
            break;
        }
        case 'G': { // Cursor horizontal absolute
            int n = numArgs == 0 ? 0 : args[0];
            if (n < renderContext.screenTileSize.x) {
                renderContext.cursorPosition.x = n;
            }
            break;
        }
        case 'H': { // Cursor position
            // TODO: doesn't handle cases like CSI ;5H, which should use column 1 as the default x value.
            int x = numArgs == 1 ? args[0] - 1 : 0;
            int y = numArgs == 2 ? args[1] - 1 : 0;
            if (x < renderContext.screenTileSize.x && y < renderContext.screenTileSize.y) {
                renderContext.cursorPosition.x = x;
                renderContext.cursorPosition.y = y;
            }
            break;
        }
        case 'J': { // Erase in display
            int n = numArgs == 1 ? args[0] : 0;
            if (n == 0) {
                // Erase from cursor to end of screen
                eraseScreenRect(renderContext.cursorPosition.x, renderContext.screenTileSize.x - 1, renderContext.cursorPosition.y, renderContext.cursorPosition.y);
                eraseScreenRect(0, renderContext.screenTileSize.x - 1, 0, renderContext.cursorPosition.y - 1);
            } else if (n == 1) {
                // Erase from start of screen to cursor
                eraseScreenRect(0, renderContext.cursorPosition.x, renderContext.cursorPosition.y, renderContext.cursorPosition.y);
                eraseScreenRect(0, renderContext.screenTileSize.x - 1, renderContext.cursorPosition.y + 1, renderContext.screenTileSize.y - 1);
            } else if (n == 2) {
                // Erase whole screen
                eraseScreenRect(0, renderContext.screenTileSize.x - 1, 0, renderContext.screenTileSize.y - 1);
            } else if (n == 3) {
                // Erase whole screen and scrollback buffer
                eraseScreenRect(0, renderContext.screenTileSize.x - 1, 0, renderContext.screenTileSize.y - 1);
                // TODO: erase back buffer
            }
            break;
        }
        case 'K': { // Erase in line
            int n = numArgs == 1 ? args[0] : 0;
            if (n == 0) {
                // Erase from cursor to end of line
                eraseScreenRect(renderContext.cursorPosition.x, renderContext.screenTileSize.x - 1, renderContext.cursorPosition.y, renderContext.cursorPosition.y);
            } else if (n == 1) {
                // Erase from start of line to cursor
                eraseScreenRect(0, renderContext.cursorPosition.x, renderContext.cursorPosition.y, renderContext.cursorPosition.y);
            } else if (n == 2) {
                // Erase entire line
                eraseScreenRect(0, renderContext.screenTileSize.x - 1, renderContext.cursorPosition.y, renderContext.cursorPosition.y);
            }
            break;
        }
        case 'S': { // Scroll up
            int n = numArgs == 1 ? args[0] : 1;
            printf("CSI S (%d) not implemented\n", n);
            break;
        }
        case 'T': { // Scroll down
            int n = numArgs == 1 ? args[0] : 1;
            printf("CSI T (%d) not implemented\n", n);
            break;
        }
        case 'm': { // Graphics control
            if (numArgs == 0) {
                updateGraphicsState(0, 0);
            }
            for (int i = 0; i < numArgs; i++) {
                updateGraphicsState(args[i], i);
            }
            break;
        }
        default:
            printf("unsupported csi command: %s\n", state.argBuffer.data);
    }

    return 1;
}

static int executeDCSCommandArgument() {
    printf("executeDCSCommandArgument not implemented.\n");
    return 1;
}

/**
 * OSC commands start with ESC] and are terminated with BEL or ST.
*/
static int executeOSCCommandArgument() {
    if (state.argBuffer.position == 0) {
        return 1;
    }

    const u8 BEL = 0x7;
    const u8 ST = 0x9C;
    const u8 lastByte = state.argBuffer.data[state.argBuffer.position - 1];

    // OSC sequence is not yet terminated
    if (lastByte != BEL && lastByte != ST) {
        return 0;
    }

    if (state.argBuffer.data[0] == '0' && state.argBuffer.data[1] == ';') {
        const int byteOffset = 2;
        u8 *windowTitle = state.argBuffer.data + byteOffset;
        windowTitle[state.argBuffer.position - byteOffset - 1] = '\0';
        glfwSetWindowTitle(renderContext.window, (const char *) windowTitle);
    } else {
        printf("unsupported osc command: %s\n", state.argBuffer.data);
    }

    return 1;
}

static int executeCommandArgument() {
    switch (state.sequenceType) {
        case SEQUENCE_TYPE_CSI: return executeCSICommandArgument();
        case SEQUENCE_TYPE_DCS: return executeDCSCommandArgument();
        case SEQUENCE_TYPE_OSC: return executeOSCCommandArgument();
        default: return 0;
    }
}
