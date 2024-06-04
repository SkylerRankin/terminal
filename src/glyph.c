#include <stdio.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "../lib/glad/glad.h"

#include "terminal.h"
#include "glyph.h"

#define CACHE_SIZE 1024

static const int REPLACEMENT_CODEPOINT = 0xFFFD;

struct GlyphEntry;
struct GlyphEntry {
    int codePoint;
    unsigned short atlasPosition;
    struct GlyphEntry *previousLRUEntry;
    struct GlyphEntry *nextLRUEntry;
    struct GlyphEntry *nextCacheEntry;
};

struct GlyphEntry *glyphCacheBasePointer;
struct GlyphEntry *glyphCache[CACHE_SIZE];
struct GlyphEntry *lruStart;
struct GlyphEntry *lruEnd;

FT_Library library;
FT_Face face;

extern struct RenderContext renderContext;

/**
 * 32-bit FNV hash with XOR folding to reduce output to 10 bits.
*/
static int fnvHash10(int v) {
    int prime = 0x01000193;
    int hash = 0x811c9dc5;
    for (int i = 0; i < 32; i+=8) {
        hash = hash * prime;
        hash = hash ^ (v >> i);
    }
    return ((hash >> 10) ^ hash) & 0x3FF;
}

static int inRange(int v, int min, int max) {
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

void freeGlyphCache() {
    free(glyphCacheBasePointer);
}

void initGlyphCache() {
    glyphCacheBasePointer = malloc(sizeof(struct GlyphEntry) * CACHE_SIZE);

    // Initialize cache with empty entries
    struct GlyphEntry *prevEntry = 0;
    for (int atlasPosition = 0; atlasPosition < ATLAS_WIDTH * ATLAS_HEIGHT; atlasPosition++) {
        struct GlyphEntry *entry = glyphCacheBasePointer + atlasPosition;
        entry->codePoint = 0xFFFF0000 | atlasPosition;
        entry->atlasPosition = atlasPosition;
        entry->nextCacheEntry = 0;
        entry->previousLRUEntry = 0;
        entry->nextLRUEntry = 0;

        int hash = fnvHash10(entry->codePoint);
        if (glyphCache[hash]) {
            struct GlyphEntry *current = glyphCache[hash];
            while (current->nextCacheEntry) {
                current = current->nextCacheEntry;
            }
            current->nextCacheEntry = entry;
        } else {
            glyphCache[hash] = entry;
        }

        entry->previousLRUEntry = prevEntry;
        if (prevEntry) {
            prevEntry->nextLRUEntry = entry;
        } else {
            lruStart = entry;
        }
        prevEntry = entry;
    }

    lruEnd = prevEntry;

    // Pre-load cache with printable ascii characters
    for (int asciiCode = 0x20; asciiCode <= 0x7E; asciiCode++) {
        getGlyphAtlasPosition(asciiCode);
    }
}

void addCodePointToAtlas(unsigned int codePoint, unsigned short atlasPosition) {
    int atlasTileX = atlasPosition % ATLAS_WIDTH;
    int atlasTileY = (int) (atlasPosition / ATLAS_WIDTH);
    int glyphSizeX = renderContext.atlasGlyphSize.x;
    int glyphSizeY = renderContext.atlasGlyphSize.y;

    FT_UInt glyphIndex = FT_Get_Char_Index(face, codePoint);
    if (FT_Load_Glyph(face, glyphIndex, 0)) {
        printf("Error loading glyph for %d\n", glyphIndex);
        glyphIndex = FT_Get_Char_Index(face, REPLACEMENT_CODEPOINT);
        FT_Load_Glyph(face, glyphIndex, 0);
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    }
    FT_Bitmap *bitmap = &face->glyph->bitmap;

    unsigned int bitmapWidth = face->glyph->bitmap.width;
    unsigned int bitmapHeight = face->glyph->bitmap.rows;

    int xOffset = (int) face->glyph->metrics.horiBearingX >> 6;
    int yOffset = (int) (face->glyph->metrics.height - face->glyph->metrics.horiBearingY) >> 6;

    // TODO: reuse a pre-allocated char array
    char *flippedBitmap = malloc(sizeof(char) * glyphSizeX * glyphSizeY);
    memset(flippedBitmap, 0, glyphSizeX * glyphSizeY);
    for (int x = 0; x < bitmapWidth; x++) {
        for (int y = 0; y < bitmapHeight; y++) {
            int adjustedX = inRange(x + xOffset, 0, renderContext.atlasGlyphSize.x - 1);
            int adjustedY = inRange(y + renderContext.lineSpacing + renderContext.maxBelowBaseline - yOffset, 0, renderContext.atlasGlyphSize.y - 1);
            flippedBitmap[adjustedY * glyphSizeX + adjustedX] = bitmap->buffer[(bitmapHeight - y - 1) * bitmapWidth + x];
        }
    }

    glBindTexture(GL_TEXTURE_2D, renderContext.atlasTextureId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, atlasTileX * renderContext.atlasGlyphSize.x, atlasTileY * renderContext.atlasGlyphSize.y, glyphSizeX, glyphSizeY, GL_RED, GL_UNSIGNED_BYTE, flippedBitmap);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(flippedBitmap);
}

void loadBaselineFont(char *fontPath) {
    if (FT_Init_FreeType(&library)) {
        printf("Failed to init freetype.\n");
        exit(-1);
    }

    if (FT_New_Face(library, fontPath, 0, &face)) {
        printf("Failed to load font at path %s.\n", fontPath);
        exit(-1);
    }

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

    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
        printf("Failed to select unicode character map.\n");
        exit(-1);
    }

    FT_Pos maxWidth = 0, maxHeight = 0, maxAboveBaseline = 0, maxBelowBaseline = 0;
    for (int asciiCode = 0x20; asciiCode <= 0x7E; asciiCode++) {
        FT_UInt glyphIndex = FT_Get_Char_Index(face, asciiCode);
        FT_Load_Glyph(face, glyphIndex, 0);

        int pixelWidth = face->glyph->metrics.width >> 6;
        int pixelHeight = face->glyph->metrics.height >> 6;

        int aboveBaseline = face->glyph->metrics.horiBearingY >> 6;
        int belowBaseline = (face->glyph->metrics.height >> 6) - (face->glyph->metrics.horiBearingY >> 6);

        if (pixelWidth > maxWidth) maxWidth = pixelWidth;
        if (pixelHeight > maxHeight) maxHeight = pixelHeight;
        if (aboveBaseline > maxAboveBaseline) maxAboveBaseline = aboveBaseline;
        if (belowBaseline > maxBelowBaseline) maxBelowBaseline = belowBaseline;
    }

    renderContext.atlasGlyphSize = (struct Vec2i) {
        .x = face->size->metrics.max_advance >> 6,
        .y = face->size->metrics.height >> 6
    };

    renderContext.maxBelowBaseline = maxBelowBaseline;

    int lineSpacing = renderContext.atlasGlyphSize.y - maxHeight;
    renderContext.lineSpacing = lineSpacing;

    printf("Loaded baseline font at size %d from %s.\n", renderContext.atlasFontHeight, fontPath);
    printf("\tMax glyph dimension: (%ld, %ld)\n", maxWidth, maxHeight);
    printf("\tScreen glyph size:   (%d, %d)\n", renderContext.screenGlyphSize.x, renderContext.screenGlyphSize.y);
    printf("\tAtlas glyph size:    (%d, %d)\n", renderContext.atlasGlyphSize.x, renderContext.atlasGlyphSize.y);
    printf("\tMax below baseline:  %ld\n", maxBelowBaseline);
    printf("\tLine spacing:        %d\n", renderContext.lineSpacing);
    printf("\n");
}

/**
 * Moves an entry to the front of the LRU cache, marking it as the
 * most recently used entry.
*/
void markGlyphAsUsed(struct GlyphEntry *entry) {
    if (entry->codePoint == lruStart->codePoint) {
        return;
    }

    entry->previousLRUEntry = 0;
    entry->nextLRUEntry = lruStart;
    lruStart = entry;
}

void removeCacheEntry(int codePoint) {
    int hash = fnvHash10(codePoint);
    struct GlyphEntry *currentEntry = glyphCache[hash];
    if (currentEntry && currentEntry->codePoint == codePoint) {
        glyphCache[hash] = currentEntry->nextCacheEntry;
    } else {
        struct GlyphEntry *prevEntry = glyphCache[hash];
        while (currentEntry && currentEntry->codePoint != codePoint) {
            prevEntry = currentEntry;
            currentEntry = currentEntry->nextCacheEntry;
        }
        if (currentEntry && currentEntry->codePoint == codePoint) {
            prevEntry->nextCacheEntry = currentEntry->nextCacheEntry;
        } else {
            printf("failed to find entry for codePoint %x\n", codePoint);
        }
    }
}

void insertCacheEntry(struct GlyphEntry *entry, int hash) {
    if (glyphCache[hash] == 0) {
        glyphCache[hash] = entry;
    } else {
        struct GlyphEntry *currentEntry = glyphCache[hash];
        while (currentEntry->nextCacheEntry) {
            currentEntry = currentEntry->nextCacheEntry;
        }
        currentEntry->nextCacheEntry = entry;
    }
}

struct GlyphEntry* addGlyphToCache(int codePoint, int hash) {
    // Remove end of LRU chain and remove entry from cache
    struct GlyphEntry *deletedEntry = lruEnd;
    if (deletedEntry) {
        lruEnd->previousLRUEntry->nextLRUEntry = 0;
        lruEnd = lruEnd->previousLRUEntry;
        removeCacheEntry(deletedEntry->codePoint);
    }

    addCodePointToAtlas(codePoint, deletedEntry->atlasPosition);

    // Create new entry, occupies the same atlas position as deleted entry
    struct GlyphEntry *newEntry = deletedEntry;
    newEntry->codePoint = codePoint;
    newEntry->previousLRUEntry = 0;
    newEntry->nextLRUEntry = 0;
    newEntry->nextCacheEntry = 0;

    // Place new entry at front of LRU chain
    newEntry->nextLRUEntry = lruStart;
    lruStart->previousLRUEntry = newEntry;
    lruStart = newEntry;

    // Insert new entry into table
    insertCacheEntry(newEntry, hash);
    return newEntry;
}

int getGlyphAtlasPosition(unsigned int codePoint) {
    int hash = fnvHash10(codePoint);

    struct GlyphEntry *currentEntry = glyphCache[hash];
    while (currentEntry && currentEntry->codePoint != codePoint) {
        currentEntry = currentEntry->nextCacheEntry;
    }

    if (!currentEntry || codePoint != currentEntry->codePoint) {
        currentEntry = addGlyphToCache(codePoint, hash);
    }

    markGlyphAsUsed(currentEntry);
    return currentEntry->atlasPosition;
}
