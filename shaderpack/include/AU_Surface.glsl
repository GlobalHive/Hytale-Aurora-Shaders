#ifndef AU_SURFACE
#define AU_SURFACE

#include "AU_Settings.glsl"

vec2 AU_SurfaceFootprint(vec3 normal, vec3 albedo)
{
#if AU_POLISH_ENABLE
    vec3 normalDx = dFdx(normal), normalDy = dFdy(normal);
    vec3 colourWidth = fwidth(albedo);
    return vec2(max(dot(normalDx, normalDx), dot(normalDy, normalDy)),
                max(colourWidth.r, max(colourWidth.g, colourWidth.b)));
#else
    return vec2(0.0);
#endif
}

vec3 AU_TextureDefinition(vec3 albedo, float centreHeight, float meanHeight, float resolved)
{
#if AU_POLISH_ENABLE
    float contrast = clamp((centreHeight - meanHeight) / max(meanHeight, 0.12), -0.35, 0.35);
    float strength = clamp(AU_POLISH_TEXTURE_DETAIL, 0.0, 1.0) * clamp(resolved, 0.0, 1.0);
    return clamp(albedo * (1.0 + contrast * strength), 0.0, 1.0);
#else
    return albedo;
#endif
}

float AU_TextureSmoothness(float smoothness, uint materialClass, vec3 albedo, float colourFootprint)
{
#if AU_POLISH_ENABLE
    if (smoothness > 0.0 && (materialClass == 1u || materialClass == 2u)) {
        float luminance = dot(max(albedo, vec3(0.0)), vec3(0.299, 0.587, 0.114));
        float detail = smoothstep(0.08, 0.7, luminance) - 0.5;
        float resolved = 1.0 - smoothstep(0.08, 0.3, colourFootprint);
        float roughness = 1.0 - smoothness;
        roughness -= detail * resolved * AU_POLISH_ROUGHNESS * (materialClass == 2u ? 0.24 : 0.16);
        return 1.0 - clamp(roughness, 0.05, 1.0);
    }
#endif
    return smoothness;
}

float AU_SpecularSmoothness(float smoothness, float normalVariance)
{
#if AU_POLISH_ENABLE && AU_POLISH_SPECULAR_AA
    if (smoothness > 0.0) {
        float roughness = 1.0 - clamp(smoothness, 0.0, 0.95);
        float variance = clamp(normalVariance * 0.5, 0.0, 0.08);
        return 1.0 - sqrt(min(roughness * roughness + variance, 1.0));
    }
#endif
    return smoothness;
}

vec3 AU_DirectionalAmbient(vec3 ambient, vec3 normal, int subsurfaceMode)
{
#if AU_POLISH_ENABLE
    float facingSky = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float visibility = mix(0.72, 1.0, facingSky);
    vec3 tint = mix(vec3(1.07, 1.01, 0.94), vec3(0.96, 1.0, 1.07), facingSky);
    tint /= dot(tint, vec3(0.299, 0.587, 0.114));
    float strength = clamp(AU_POLISH_AMBIENT, 0.0, 1.0) * (subsurfaceMode == 1 || subsurfaceMode == 2 ? 0.5 : 1.0);
    return ambient * mix(vec3(1.0), tint * visibility, strength);
#else
    return ambient;
#endif
}

float AU_FoliageSpecular(int subsurfaceMode)
{
#if AU_POLISH_ENABLE
    if (subsurfaceMode == 1 || subsurfaceMode == 2)
        return 1.0 - clamp(AU_POLISH_FOLIAGE_MATTE, 0.0, 1.0) * 0.9;
#endif
    return 1.0;
}

#endif
