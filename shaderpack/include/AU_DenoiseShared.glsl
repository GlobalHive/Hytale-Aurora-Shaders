#ifndef AU_DENOISE_SHARED_INC
#define AU_DENOISE_SHARED_INC

vec2 AU_GIPreviousUV(vec3 playerPos)
{
    vec4 previousClip = ReprojectionMatrix * vec4(mat3(ViewMatrix) * playerPos, 1.0);
    if (previousClip.w <= 0.00001) return vec2(-1.0);
    return previousClip.xy / previousClip.w * 0.5 + 0.5 + uHookPreviousGeometryJitter * 0.5;
}

float AU_GIMotionPixels(vec2 currentUV, vec2 previousUV)
{
    vec2 jitterMotion = (uHookPreviousJitter - uHookSceneJitter) * 0.5;
    return length((previousUV - currentUV - jitterMotion) * ViewportSize);
}

float AU_GIHistoryWeight(float historyLength, vec2 currentUV, vec2 previousUV)
{
    return max(1.0 / max(historyLength, 1.0), 1.0 - AU_GI_MAX_HISTORY);
}

#endif
