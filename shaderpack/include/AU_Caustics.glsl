// Aurora Shaders for Hytale - water caustics.
// The derivative of two scrolling layers of the water normal map, used as the colour of
// sunlight passing through water: it scales the sun light reaching surfaces under water.

#ifndef AU_CAUSTICS
#define AU_CAUSTICS

#ifndef AU_HAS_CLOUDWATER
#define AU_HAS_CLOUDWATER
uniform sampler2D cloudWaterTex;
#endif

// 0.35 (dark) .. 1.0 (bright caustic lines)
float AU_GetCaustics(vec3 worldPos)
{
    // Caustics belong to the light passing through the water, i.e. they travel along the light.
    // Project the point along the light onto a horizontal plane so walls get the pattern the
    // light carries instead of vertical streaks from a straight-down projection.
    worldPos.xz -= lightVec.xz / max(lightVec.y, 0.2) * worldPos.y;
    vec2 causticWind = vec2(0.0, Time * 0.035);
    vec2 cPos1 = worldPos.xz * 0.08 + causticWind;
    vec2 cPos2 = worldPos.xz * 0.06 - causticWind;
    const float cMult = 14.0;
    const float offset = 0.001;
    float caustic = dot(texture(cloudWaterTex, cPos1 + vec2(offset, 0.0)).rg, vec2(cMult))
                  - dot(texture(cloudWaterTex, cPos1 - vec2(offset, 0.0)).rg, vec2(cMult))
                  + dot(texture(cloudWaterTex, cPos2 + vec2(0.0, offset)).rg, vec2(cMult))
                  - dot(texture(cloudWaterTex, cPos2 - vec2(0.0, offset)).rg, vec2(cMult));
    return clamp(caustic * 0.8 + 0.35, 0.0, 1.0) * 0.65 + 0.35;
}

// Multiplier for the direct sun light on a surface under water (averages ~1, bright lines).
float AU_CausticSunMult(vec3 worldPos)
{
    return mix(1.0, AU_GetCaustics(worldPos) * 1.55, AU_CAUSTICS_STRENGTH);
}

#endif // AU_CAUSTICS
