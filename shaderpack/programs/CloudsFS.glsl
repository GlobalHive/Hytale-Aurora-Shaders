#version 410 core

// Aurora Shaders for Hytale - clouds.
// Hytale's textured cloud layers are replaced by volumetric clouds.
// This pass is alpha blended over the sky after the sun and moon are drawn.

#include "SceneData_inc.glsl"
#include "Fog_inc.glsl"
#include "AU_Clouds.glsl"

in vec3 fragSpherePosition;
in vec2 fragTexCoords;
#if USE_MOOD_FOG
in vec4 fragFogInfo;
#endif

layout (location = 0) out vec4 outColor;

void main()
{
    AU_InitCommon();
    // Keep every vertex input active: the game looks up vertTexCoords and fails if it is
    // optimised away.
    if (fragTexCoords.x < -1.0e9 || fragSpherePosition.x < -1.0e9) discard;

#if AU_CLOUDS
    vec2 ndc = gl_FragCoord.xy * InvViewportSize * 2.0 - 1.0;
    vec4 p = InvViewProjectionMatrix * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w);

    float VdotU = dir.y;
    float VdotS = dot(dir, sunVec);
    float dither = GetDither(gl_FragCoord.xy);

    vec4 clouds = GetClouds(dir, 1000000000.0, 1.0, VdotS, VdotU, dither);
    if (clouds.a < 0.002) discard;
    outColor = vec4(clouds.rgb, clamp(clouds.a, 0.0, 1.0));
#else
    discard;
#endif
}
