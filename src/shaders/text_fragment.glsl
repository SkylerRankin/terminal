#version 430

#define MAX_CHARACTERS_PER_ROW 500
#define MAX_ROWS 1000
#define ATLAS_WIDTH 32
#define ATLAS_HEIGHT 32

layout(std430, binding = 2) buffer TextShaderContext {
    int glyphIndicesRowOffset;
    ivec2 atlasGlyphSize;
    ivec2 screenGlyphSize;
    ivec2 screenSize;
    ivec2 screenTileSize;
    ivec2 screenExcess;
    uint glyphIndices[MAX_CHARACTERS_PER_ROW * MAX_ROWS];
    uint glyphColors[MAX_CHARACTERS_PER_ROW * MAX_ROWS];
} context;

uniform sampler2D glyphTexture;
uniform vec2 windowPadding;
out vec4 outColor;

void main() {
    vec2 screenPosition = vec2(
        gl_FragCoord.x - windowPadding.x,
        gl_FragCoord.y - windowPadding.y
    );

    // Setup constants
    vec2 atlasTileSize = vec2(ATLAS_WIDTH, ATLAS_HEIGHT);
    vec2 glyphTextureSize = atlasTileSize * context.atlasGlyphSize;
    vec2 pixelPosition = vec2(
        screenPosition.x,
        screenPosition.y - context.screenExcess.y
    );

    // Tile coordinate in range [0, num_rows/columns).
    ivec2 tile = ivec2(
        floor((pixelPosition.x - 0.5) / context.screenGlyphSize.x),
        context.screenTileSize.y - floor(((pixelPosition.y - 0.5)) / context.screenGlyphSize.y) - 1
    );

    // Mask for pixels that are not within a full glyph tile.
    int outOfBoundsMask = 1 - max(
        max(0, sign(tile.x - context.screenTileSize.x + 1)),
        max(0, 1 - sign(int(screenPosition.y) - context.screenExcess.y + 1))
    );

    // 1d index of tile.
    int tileIndex = (int(mod(tile.y + context.glyphIndicesRowOffset, MAX_ROWS))) * MAX_CHARACTERS_PER_ROW + tile.x;

    // Find 2d tile coordinates of the corresponding glyph.
    uint glyphIndex = context.glyphIndices[tileIndex];
    ivec2 glyphTile = ivec2(mod(glyphIndex, atlasTileSize.x), floor(glyphIndex / atlasTileSize.x));

    // Convert tile coordinate into the actual coordinate within glyph texture.
    vec2 tileOffset = mod(pixelPosition.xy, context.screenGlyphSize) / vec2(context.screenGlyphSize);
    vec2 glyphCoordinate = ((glyphTile * context.atlasGlyphSize) + (tileOffset * context.atlasGlyphSize)) / glyphTextureSize;

    // Sample the glyph atlas to set pixel color.
    vec3 textColor = vec3(
        float((context.glyphColors[tileIndex] >> 16) & 0xFF) / 0xFF,
        float((context.glyphColors[tileIndex] >> 8) & 0xFF) / 0xFF,
        float((context.glyphColors[tileIndex] >> 0) & 0xFF) / 0xFF
    );
    vec4 glyphSample = vec4(1.0, 1.0, 1.0, texture(glyphTexture, glyphCoordinate).r);
    outColor = vec4(textColor, 1.0) * glyphSample * outOfBoundsMask;

    // Background checker pattern
    int render_checkered_background = 0;
    if (render_checkered_background > 0 && outColor.a == 0) {
        if (mod(tile.x, 2) == mod(tile.y, 2)) {
            outColor = vec4(0.1, 0.1, 0.0, 1.0);
        } else {
            outColor = vec4(0.0, 0.1, 0.1, 1.0);
        }
    }

    // outColor = vec4(tile.x / float(context.screenTileSize.x), tile.y / float(context.screenTileSize.y), 0.0, 1.0);
    // outColor = vec4(glyphTile.x / float(atlasTileSize.x), glyphTile.y / float(atlasTileSize.y), 0.0, 1.0);
    // outColor = vec4(tileOffset.x, tileOffset.y, 0.0, 1.0);
    // outColor = vec4(outOfBoundsMask, 0, 0, 1.0);
    // if (outColor.a == 0) outColor = vec4(0, tileIndex / float(MAX_CHARACTERS_PER_ROW) / 50, 0.0, 1.0);
    // outColor = vec4(texture(glyphTexture, gl_FragCoord.xy / context.screenSize.xy).r, 0.0, 0.0, 1.0);
};
