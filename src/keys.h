#pragma once

#include <GLFW/glfw3.h>

#define KEY_UNMAPPED 0xFF

/**
 * Contains a mapping from GLFW input keycodes to the byte values sent to the pseudo-terminal. Input keys
 * that require multiple bytes are encoded as little endian ints, so the least significant byte gets sent
 * first. Assumes a standard US keyboard layout.
 * Format is [GLFW key, normal key mapping, key + shift mapping, key + control mapping].
*/
const int INPUT_KEY_MAPPING[] = {
    GLFW_KEY_SPACE,         0x20, 0xFF, 0xFF,
    GLFW_KEY_APOSTROPHE,    0x27, 0x22, 0xFF,
    GLFW_KEY_COMMA,         0x2C, 0x3C, 0xFF,
    GLFW_KEY_MINUS,         0x2D, 0x5F, 0xFF,
    GLFW_KEY_PERIOD,        0x2E, 0x3E, 0xFF,
    GLFW_KEY_SLASH,         0x2F, 0x3F, 0xFF,
    GLFW_KEY_0,             0x30, 0x28, 0xFF,
    GLFW_KEY_1,             0x31, 0x21, 0xFF,
    GLFW_KEY_2,             0x32, 0x40, 0xFF,
    GLFW_KEY_3,             0x33, 0x23, 0xFF,
    GLFW_KEY_4,             0x34, 0x24, 0xFF,
    GLFW_KEY_5,             0x35, 0x25, 0xFF,
    GLFW_KEY_6,             0x36, 0x5E, 0xFF,
    GLFW_KEY_7,             0x37, 0x26, 0xFF,
    GLFW_KEY_8,             0x38, 0x2A, 0xFF,
    GLFW_KEY_9,             0x39, 0x28, 0xFF,
    GLFW_KEY_SEMICOLON,     0x3B, 0x3A, 0xFF,
    GLFW_KEY_EQUAL,         0x3D, 0x2B, 0xFF,
    GLFW_KEY_A,             0x61, 0x41, 0x01,
    GLFW_KEY_B,             0x62, 0x42, 0x02,
    GLFW_KEY_C,             0x63, 0x43, 0x03,
    GLFW_KEY_D,             0x64, 0x44, 0x04,
    GLFW_KEY_E,             0x65, 0x45, 0x05,
    GLFW_KEY_F,             0x66, 0x46, 0x06,
    GLFW_KEY_G,             0x67, 0x47, 0x07,
    GLFW_KEY_H,             0x68, 0x48, 0x08,
    GLFW_KEY_I,             0x69, 0x49, 0x09,
    GLFW_KEY_J,             0x6A, 0x4A, 0x0A,
    GLFW_KEY_K,             0x6B, 0x4B, 0x0B,
    GLFW_KEY_L,             0x6C, 0x4C, 0x0C,
    GLFW_KEY_M,             0x6D, 0x4D, 0x0D,
    GLFW_KEY_N,             0x6E, 0x4E, 0x0E,
    GLFW_KEY_O,             0x6F, 0x4F, 0x0F,
    GLFW_KEY_P,             0x70, 0x50, 0x10,
    GLFW_KEY_Q,             0x71, 0x51, 0x11,
    GLFW_KEY_R,             0x72, 0x52, 0x12,
    GLFW_KEY_S,             0x73, 0x53, 0x13,
    GLFW_KEY_T,             0x74, 0x54, 0x14,
    GLFW_KEY_U,             0x75, 0x55, 0x15,
    GLFW_KEY_V,             0x76, 0x56, 0x16,
    GLFW_KEY_W,             0x77, 0x57, 0x17,
    GLFW_KEY_X,             0x78, 0x58, 0x18,
    GLFW_KEY_Y,             0x79, 0x59, 0x19,
    GLFW_KEY_Z,             0x7A, 0x5A, 0x1A,
    GLFW_KEY_LEFT_BRACKET,  0x5B, 0x7B, 0xFF,
    GLFW_KEY_BACKSLASH,     0x5C, 0x7C, 0xFF,
    GLFW_KEY_RIGHT_BRACKET, 0x5D, 0x7D, 0xFF,
    GLFW_KEY_GRAVE_ACCENT,  0x60, 0x7E, 0xFF,
    GLFW_KEY_ESCAPE,        0xFF, 0xFF, 0xFF,
    GLFW_KEY_ENTER,         0x0D, 0x0D, 0x0D,
    GLFW_KEY_TAB,           0x09, 0x09, 0x09,
    GLFW_KEY_BACKSPACE,     0x08, 0x08, 0x08,
    GLFW_KEY_INSERT,        0xFF, 0xFF, 0xFF,
    GLFW_KEY_DELETE,        0xFF, 0xFF, 0xFF,
    GLFW_KEY_RIGHT,         0x435B1B, 0xFF, 0xFF,
    GLFW_KEY_LEFT,          0x445B1B, 0xFF, 0xFF,
    GLFW_KEY_DOWN,          0x425B1B, 0xFF, 0xFF,
    GLFW_KEY_UP,            0x415B1B, 0xFF, 0xFF
};

int keyNormalMapping[512] = {};
int keyControlMapping[512] = {};
int keyShiftMapping[512] = {};

static void initKeyMappings() {
    for (int i = 0; i < 232; i += 4) {
        int inputKey = INPUT_KEY_MAPPING[i];
        keyNormalMapping[inputKey] = INPUT_KEY_MAPPING[i + 1];
        keyShiftMapping[inputKey] = INPUT_KEY_MAPPING[i + 2];
        keyControlMapping[inputKey] = INPUT_KEY_MAPPING[i + 3];
    }
}
