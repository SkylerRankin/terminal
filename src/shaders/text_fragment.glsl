#version 430

layout(std430, binding = 2) buffer TextShaderContext {
    ivec2 atlasGlyphSize;
    ivec2 screenGlyphSize;
    ivec2 screenSize;
    ivec2 screenTileSize;
    ivec2 atlasTileSize;
    ivec2 screenExcess;
    uint glyphIndices[1024 * 1024];
    uint glyphColors[1024 * 1024];
    ivec2 glyphOffsets[1024 * 1024];
} context;

uniform sampler2D glyphTexture;
uniform vec2 resolution;
out vec4 outColor;

void main() {
    // Setup constants
    vec2 glyphTextureSize = context.atlasTileSize * context.atlasGlyphSize;
    vec2 pixelPosition = vec2(
        gl_FragCoord.x,
        gl_FragCoord.y - context.screenExcess.y
    );

    // Tile coordinate in range [0, num_rows/columns).
    ivec2 tile = ivec2(
        floor((pixelPosition.x - 0.5) / context.screenGlyphSize.x),
        floor(((pixelPosition.y - 0.5)) / context.screenGlyphSize.y)
    );

    // Mask for pixels that are not within a full glyph tile.
    int outOfBoundsMask = 1 - max(
        max(0, sign(tile.x - context.screenTileSize.x + 1)),
        max(0, 1 - sign(int(gl_FragCoord.y) - context.screenExcess.y + 1))
    );

    // 1d index of tile.
    int tileIndex = tile.y * context.screenTileSize.x + tile.x;

    // Find 2d tile coordinates of the corresponding glyph.
    uint glyphIndex = context.glyphIndices[tileIndex];
    ivec2 glyphTile = ivec2(mod(glyphIndex, context.atlasTileSize.x), floor(glyphIndex / context.atlasTileSize.x));

    // Convert tile coordinate into the actual coordinate within glyph texture.
    vec2 tileOffset = mod(pixelPosition.xy, context.screenGlyphSize) / vec2(context.screenGlyphSize);
    vec2 glyphOffset = context.glyphOffsets[glyphIndex] * 0; // Doesn't work yet
    vec2 glyphCoordinate = ((glyphTile * context.atlasGlyphSize) + (tileOffset * context.atlasGlyphSize) + glyphOffset) / glyphTextureSize;

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

    // outColor = vec4(tile.x / float(context.scleaccreenTileSize.x), tile.y / float(context.screenTileSize.y), 0.0, 1.0);
    // outColor = vec4(glyphTile.x / float(context.atlasTileSize.x), glyphTile.y / float(context.atlasTileSize.y), 0.0, 1.0);
    // outColor = vec4(tileOffset.x, tileOffset.y, 0.0, 1.0);
    // outColor = vec4(outOfBoundsMask, 0, 0, 1.0);
};
