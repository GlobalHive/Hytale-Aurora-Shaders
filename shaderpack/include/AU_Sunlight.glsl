#ifndef AU_SUNLIGHT
#define AU_SUNLIGHT

#include "AU_Settings.glsl"

float AU_SunLuminance(vec3 colour)
{
    return dot(colour, vec3(0.299, 0.587, 0.114));
}

float AU_OutdoorSunlight(float skyLight, float daylight, float underwater)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    return smoothstep(0.08, 0.8, skyLight) * clamp(daylight, 0.0, 1.0) * (1.0 - clamp(underwater, 0.0, 1.0));
#else
    return 0.0;
#endif
}

vec3 AU_SunPalette(vec3 colour, float elevation, float daylight, float rain)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    float highSun = smoothstep(0.0, 0.65, elevation);
    vec3 tint = mix(vec3(1.28, 1.0, 0.72), vec3(1.04, 1.015, 0.95), highSun);
    tint *= AU_SunLuminance(colour) / AU_SunLuminance(tint);
    float amount = clamp(AU_POLISH_SUN_COLOUR, 0.0, 1.0) * clamp(daylight, 0.0, 1.0) * (1.0 - 0.8 * clamp(rain, 0.0, 1.0));
    return mix(colour, tint, amount);
#else
    return colour;
#endif
}

vec3 AU_OutdoorSkyFill(vec3 legacyAmbient, vec3 ambient, vec3 normal, vec3 zenith, vec3 horizon,
                       float outdoor, float rain, int subsurfaceMode)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    float facingSky = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(horizon, zenith, 0.35 + 0.65 * facingSky);
    vec3 tint = clamp(sky / max(AU_SunLuminance(sky), 0.0001), 0.75, 1.3);
    tint /= max(AU_SunLuminance(tint), 0.0001);
    float visibility = mix(0.45, 1.0, facingSky);
    if (subsurfaceMode == 1 || subsurfaceMode == 2) visibility = mix(visibility, 1.0, 0.5);
    vec3 directional = ambient * mix(vec3(1.0), tint, 0.6 * (1.0 - clamp(rain, 0.0, 1.0))) * visibility;
    return mix(legacyAmbient, directional, clamp(AU_POLISH_SKY_FILL, 0.0, 1.0) * outdoor);
#else
    return legacyAmbient;
#endif
}

vec3 AU_SunDiffuseShoulder(vec3 diffuse, float sunlightShare)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    float luminance = max(AU_SunLuminance(diffuse), 0.0);
    float excess = max(luminance - 0.65, 0.0);
    float shoulder = 0.65 + excess / (1.0 + excess * 0.35);
    float scale = luminance > 0.65 ? shoulder / luminance : 1.0;
    return diffuse * mix(1.0, scale, clamp(AU_POLISH_SUN_HIGHLIGHTS, 0.0, 1.0) * clamp(sunlightShare, 0.0, 1.0));
#else
    return diffuse;
#endif
}

float AU_FoliageSunWrap(float legacyWrap, float lightFacing, int subsurfaceMode, float outdoor)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    if (subsurfaceMode == 1 || subsurfaceMode == 2) {
        float wrap = max((lightFacing + 0.35) / 1.35, 0.0);
        float amount = clamp(AU_POLISH_FOLIAGE_SUN, 0.0, 1.0) * outdoor * (subsurfaceMode == 1 ? 0.5 : 1.0);
        return mix(legacyWrap, wrap, amount);
    }
#endif
    return legacyWrap;
}

float AU_FoliageSunPhase(float viewLight, int subsurfaceMode, float outdoor)
{
    float facing = clamp(viewLight, 0.0, 1.0);
    float legacyPhase = pow(facing, subsurfaceMode == 3 ? 6.0 : 4.0);
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    if (subsurfaceMode == 1 || subsurfaceMode == 2) {
        float broadPhase = facing * facing * facing;
        return mix(legacyPhase, broadPhase, clamp(AU_POLISH_FOLIAGE_SUN, 0.0, 1.0) * outdoor);
    }
#endif
    return legacyPhase;
}

vec3 AU_SunBounce(vec3 albedo, vec3 sunlight, float visibility, float skyLight, float daylight, float lightTime)
{
#if AU_POLISH_ENABLE && AU_POLISH_SUNLIGHT
    float exposure = smoothstep(0.0, 0.4, skyLight);
    float strength = mix(1.0, exposure * lightTime * max(AU_POLISH_SUN_BOUNCE, 0.0), clamp(daylight, 0.0, 1.0));
    return albedo * sunlight * (max(visibility, 0.0) * strength);
#else
    return albedo * sunlight * max(visibility, 0.0);
#endif
}

vec2 AU_SunShadowRadius(float depthDifference, mat4 shadowMatrix, vec2 tileSize)
{
    vec3 depthAxis = vec3(shadowMatrix[0][2], shadowMatrix[1][2], shadowMatrix[2][2]);
    float depthScale = length(depthAxis);
    vec2 planeScale = vec2(length(vec3(shadowMatrix[0][0], shadowMatrix[1][0], shadowMatrix[2][0])),
                           length(vec3(shadowMatrix[0][1], shadowMatrix[1][1], shadowMatrix[2][1])));
    float separation = max(depthDifference, 0.0) * 2.0 / max(depthScale, 0.000001);
    float angularRadius = tan(radians(clamp(AU_POLISH_SUN_DIAMETER, 0.1, 3.0) * 0.5));
    vec2 penumbra = separation * angularRadius * planeScale * tileSize * 0.5;
    return clamp(penumbra, vec2(0.6), vec2(AU_PCSS_SEARCH)) * AU_SHADOW_SOFTNESS;
}

#endif
