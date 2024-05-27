#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal.h"
#include "colors.h"

typedef unsigned char u8;

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
    .colorInput = COLOR_INPUT_NONE
};

static int handleStagePlainText(u8 byte, int *character);
static int handleStageEscape(u8 byte, int *character);
static int handleStageArguments(u8 byte, int *character);
static int executeCommandArgument();
static void executeC0ControlCode(u8 byte);

int processTextByte(u8 byte, int *character) {
    switch (state.currentStage) {
        case STAGE_PLAIN_TEXT:      return handleStagePlainText(byte, character);
        case STAGE_ESCAPE:          return handleStageEscape(byte, character);
        case STAGE_ARGUMENTS:        return handleStageArguments(byte, character);
        default:
            return 0;
    }
}

static void clearBuffer(struct Buffer *buffer) {
    buffer->position = 0;
    memset(buffer->data, 0, buffer->length);
}

static int handleStagePlainText(u8 byte, int *character) {
    if (byte == 0x1B) {
        state.currentStage = STAGE_ESCAPE;
        return 0;
    } else if ((byte >= 0x7 && byte <= 13) || byte == 0x7F) {
        executeC0ControlCode(byte);
        return 0;
    } else {
        *character = byte;
        return 1;
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
        case 0xA:
            renderContext.cursorPosition.x = 0;
            if (renderContext.cursorPosition.y > 0) {
                renderContext.cursorPosition.y -= 1;
            }
            printf("0xA: new cursor = (%d, %d)\n", renderContext.cursorPosition.x, renderContext.cursorPosition.y);
            break;
        case 0xD:
            renderContext.cursorPosition.x = 0;
            printf("0xD: new cursor = (%d, %d)\n", renderContext.cursorPosition.x, renderContext.cursorPosition.y);
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

    if (lastByte == 'm') {
        printf("executing csi m command: %s\n", state.argBuffer.data);
        int value = 0;
        int argumentIndex = 0;
        for (int i = 0; i < state.argBuffer.position; i++) {
            const u8 c = state.argBuffer.data[i];
            if (c == ';' || c == 'm') {
                updateGraphicsState(value, argumentIndex);
                value = 0;
                argumentIndex++;
            } else {
                value = (value * 10) + (c - 0x30);
            }
        }
    } else {
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
