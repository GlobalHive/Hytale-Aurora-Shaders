#ifndef AU_TEMPORAL_INC
#define AU_TEMPORAL_INC

uniform mat4 uHookPreviousViewProjection;
uniform mat4 uHookCurrentProjection;
uniform vec3 uHookPreviousCamera;
uniform vec2 uHookSceneJitter;
uniform vec2 uHookPreviousJitter;
uniform vec2 uHookGeometryJitter;
uniform vec2 uHookPreviousGeometryJitter;
uniform int uHookHistoryValid;

vec3 AU_ViewPosition(vec2 uv, float linearDepth)
{
    vec2 projected = uv * 2.0 - 1.0 - uHookGeometryJitter;
    float viewDepth = -linearDepth;
    mat4 projection = uHookCurrentProjection;
    if (abs(projection[0][0]) < 0.00001 || abs(projection[1][1]) < 0.00001) projection = ProjectionMatrix;
    return vec3(-viewDepth * (projected + vec2(projection[2][0], projection[2][1]))
                / vec2(projection[0][0], projection[1][1]), viewDepth);
}

vec2 AU_PreviousUV(vec3 relativeWorldPosition, bool resolvedHistory)
{
    vec3 previousPosition = relativeWorldPosition + CameraPosition - uHookPreviousCamera;
    vec4 previousClip = uHookPreviousViewProjection * vec4(previousPosition, 1.0);
    if (previousClip.w <= 0.00001) return vec2(-1.0);
    vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5 + uHookPreviousGeometryJitter * 0.5;
    return resolvedHistory ? previousUV - uHookPreviousJitter * 0.5 : previousUV;
}

vec2 AU_PreviousSkyUV(vec3 direction, bool resolvedHistory)
{
    vec4 previousClip = uHookPreviousViewProjection * vec4(direction, 0.0);
    if (previousClip.w <= 0.00001) return vec2(-1.0);
    vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5 + uHookPreviousGeometryJitter * 0.5;
    return resolvedHistory ? previousUV - uHookPreviousJitter * 0.5 : previousUV;
}

#endif
