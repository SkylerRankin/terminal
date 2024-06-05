#pragma once

struct Vec2i8 { unsigned char x; unsigned char y; };

void initGlyphCache();
void freeGlyphCache();
void loadBaselineFont(char *fontPath);
int getGlyphAtlasPosition(unsigned int codePoint);
