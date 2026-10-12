// Aurora Shaders for Hytale - world-space sun shadow lookups.
// Uses Hytale's cascaded shadow atlas (cascades side by side in X), shared by the hook
// from DeferredShadowFS onto texture unit 28 as uHookShadowMap.

#ifndef AU_SHADOW
#define AU_SHADOW

uniform sampler2DShadow uHookShadowMap;

// Returns false when the point is outside every cascade.
bool GetShadowCoord(vec3 positionFromCameraWS, out vec3 coord)
{
    for (int c = 0; c < AU_SHADOW_CASCADES; c++) {
        vec4 ls = SunShadowMatrix[c] * vec4(positionFromCameraWS + SunShadowCascadeCachedTranslations[c].xyz, 1.0);
        if (all(lessThan(abs(ls.xyz), vec3(0.995)))) {
            coord = ls.xyz * 0.5 + 0.5;
            const float scale = 1.0 / float(AU_SHADOW_CASCADES);
            coord.x = coord.x * scale + scale * float(c);
            return true;
        }
    }
    coord = vec3(0.0);
    return false;
}

// 1 = lit, 0 = shadowed. Outside the shadow range returns `fallback`.
float SampleShadowWS(vec3 positionFromCameraWS, float fallback)
{
    vec3 coord;
    if (!GetShadowCoord(positionFromCameraWS, coord)) return fallback;
    return texture(uHookShadowMap, vec3(coord.xy, coord.z - 0.0002));
}

#endif // AU_SHADOW
