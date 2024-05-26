#version 450

layout(std430, binding = 2) buffer TextShaderContext {
    int advance;
    int lineHeight;
    ivec2 screenSize;
    ivec2 screenTileSize;
    ivec2 atlasTileSize;
    int glyphIndices[1024 * 1024];
    ivec2 glyphOffsets[1024 * 1024];
} context;

uniform sampler2D glyphTexture;
uniform vec2 resolution;
out vec4 outColor;

void main() {
    // Stuff that should be inputs.
    vec3 textColor = vec3(1.0, 1.0, 1.0);

    // Setup constants
    ivec2 screenTileSize = ivec2(context.advance, context.lineHeight);
    ivec2 glyphTileSize = ivec2(context.advance, context.lineHeight);
    vec2 glyphTextureSize = context.atlasTileSize * screenTileSize;

    // Tile coordinate in range [0, num_rows/columns).
    ivec2 tile = ivec2(
        floor((gl_FragCoord.x - 0.5) / context.advance),
        floor(((gl_FragCoord.y - 0.5)) / context.lineHeight)
    );

    // 1d index of tile.
    int tileIndex = tile.y * context.screenTileSize.x + tile.x;

    // Find 2d tile coordinates of the corresponding glyph.
    int glyphIndex = context.glyphIndices[tileIndex];
    ivec2 glyphTile = ivec2(mod(glyphIndex, context.atlasTileSize.x), floor(glyphIndex / context.atlasTileSize.x));

    // Convert tile coordinate into the actual coordinate within glyph texture.
    vec2 tileOffset = mod(gl_FragCoord.xy, screenTileSize) / screenTileSize;
    vec2 glyphCoordinate = ((glyphTile * glyphTileSize) + (tileOffset * glyphTileSize)) / glyphTextureSize;

    // Sample the glyph atlas to set pixel color.
    vec4 glyphSample = vec4(1.0, 1.0, 1.0, texture(glyphTexture, glyphCoordinate).r);
    outColor = vec4(textColor, 1.0) * glyphSample;

    // Background checker pattern
    if (outColor.a == 0) {
        if (mod(tile.x, 2) == mod(tile.y, 2)) {
            outColor = vec4(0.1, 0.1, 0.0, 1.0);
        } else {
            outColor = vec4(0.0, 0.1, 0.1, 1.0);
        }
    }
};
