#ifndef AU_SSS_MASK_INC
#define AU_SSS_MASK_INC

#include "AU_Settings.glsl"

uniform sampler2DArray uHookSSSMasks;
uniform int uHookSSSReady;

vec2 AU_FoliageMask(vec2 coordinates, float page, vec2 atlasSize)
{
    if (uHookSSSReady == 0) return vec2(1.0, 0.0);
    ivec3 tile = ivec3(ivec2(coordinates * atlasSize / 32.0), int(page + 0.5));
    ivec3 mapSize = textureSize(uHookMaterials, 0);
    if (any(lessThan(tile, ivec3(0))) || any(greaterThanEqual(tile, mapSize))) return vec2(1.0, 0.0);
    uvec2 maskIndexBytes = uvec2(round(texelFetch(uHookMaterials, tile, 0).ba * 255.0));
    uint encoded = maskIndexBytes.x | (maskIndexBytes.y << 8);
    int layer = int(encoded & 32767u);
    if (layer == 0 || layer >= textureSize(uHookSSSMasks, 0).z) return vec2(1.0, 0.0);
    float mask = 1.0;
#if AU_SSS_TEXTURES
    ivec2 pixel = clamp(ivec2(fract(coordinates * atlasSize / 32.0) * 32.0), ivec2(0), ivec2(31));
    if ((encoded & 32768u) != 0u) pixel.y = 31 - pixel.y;
    mask = texelFetch(uHookSSSMasks, ivec3(pixel, layer), 0).r;
#endif
    return vec2(mix(1.0, mask, clamp(AU_SSS_MASK_STRENGTH, 0.0, 1.0)), 1.0);
}

float AU_FoliageDither(vec2 coordinates, uint seed)
{
    uvec2 cell = uvec2(ivec2(floor(coordinates * 128.0)));
    uint value = cell.x * 0x9e3779b9u ^ cell.y * 0x85ebca6bu ^ seed * 0xc2b2ae35u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (float(value & 16777215u) + 0.5) / 16777216.0;
}

bool AU_DiscardFoliageShadow(vec2 coordinates, uint seed, float mask)
{
    return AU_FoliageDither(coordinates, seed) < clamp(AU_SSS_SHADOW_DITHER, 0.0, 0.8) * clamp(mask, 0.0, 1.0);
}

#endif
