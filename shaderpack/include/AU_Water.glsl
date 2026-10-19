// Aurora Shaders for Hytale - water colour (the Enhanced water model, tools/mapchunk/water.glsl).
// Light is absorbed per colour along its path through the water: red first, then green, with
// Hytale's water tint (biome water colour) as the colour that survives longest.

#ifndef AU_WATER_INC
#define AU_WATER_INC

// Absorption per block of water for a water tint.
vec3 AU_WaterAbsorption(vec3 tint)
{
    vec3 hue = tint / max(max(tint.r, max(tint.g, tint.b)), 0.001);
    return (vec3(0.30, 0.06, 0.035) + 0.12 * -log(clamp(hue, vec3(0.05), vec3(1.0))))
         * AU_WATER_FOG_MULT / max(AU_WATER_CLARITY, 0.05);
}

// Light left after `distance` blocks of water.
vec3 AU_WaterTransmittance(vec3 absorption, float distance)
{
    return exp(-absorption * clamp(distance, 0.0, 512.0));
}

// The light reaching the bed `depth` blocks below the surface comes in at the sun's angle (and
// as softer sky light from all over), so it travels further than the depth.
float AU_WaterBedPath(float depth, vec3 toLight)
{
    return clamp(depth, 0.0, 256.0) * mix(1.5, 1.0 / max(toLight.y, 0.3), 0.5);
}

#endif
