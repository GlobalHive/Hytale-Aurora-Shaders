// Aurora Shaders for Hytale - "RTX" preset extras.
// Screen-space global illumination and reflections. The radiance source is the previous
// frame's opaque scene (Hytale's mipmapped refraction copy, shared by the hook as
// uHookPrevScene), reprojected with Hytale's ReprojectionMatrix.

#ifndef AU_RTX
#define AU_RTX

#include "AU_Sky.glsl"

uniform sampler2D uHookPrevScene;

vec2 AU_ProjectToScreen(vec3 positionFromCameraWS)
{
    vec4 clip = ViewProjectionMatrix * vec4(positionFromCameraWS, 1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

// View depth (blocks) of the opaque scene at uv; -1 for sky. DeferredFS gets Hytale's linear Z
// buffer (view depth / far) when USE_LINEAR_Z is set - comparing that against hardware depth
// made every ray "hit" on its first step, so GI and reflections never found anything.
float AU_SceneLinAt(sampler2D depthTex, vec2 uv)
{
    float d = textureLod(depthTex, uv, 0.0).r;
    if (d >= 1.0) return -1.0;
#if USE_LINEAR_Z
    return d * FarClip;
#else
    return GetLinearDepthFromDepthHW(d, ProjectionMatrix);
#endif
}

// View depth (blocks) of a camera-relative world position.
float AU_LinOf(vec3 positionFromCameraWS)
{
    return -(ViewMatrix * vec4(positionFromCameraWS, 1.0)).z;
}

vec3 AU_PrevSceneAt(vec3 positionFromCameraWS, float lod)
{
    vec3 posVS = (ViewMatrix * vec4(positionFromCameraWS, 1.0)).xyz;
    vec4 prev = ReprojectionMatrix * vec4(posVS, 1.0);
    vec2 uv = prev.xy / prev.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec3(-1.0);
    return textureLod(uHookPrevScene, uv, lod).rgb;
}

// Cosine-weighted hemisphere direction around n.
vec3 AU_HemisphereDir(vec3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float a = 6.2831853 * u2;
    vec3 t = normalize(abs(n.y) < 0.99 ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    return normalize(t * (r * cos(a)) + b * (r * sin(a)) + n * sqrt(max(1.0 - u1, 0.0)));
}

// Light leaving the surface at a screen position this frame, for one GI bounce. Defined by
// the including program (it needs that program's G-buffer samplers).
vec3 AU_GIHitLight(vec2 uv);

// Screen-space diffuse GI: short rays in the normal hemisphere; on a hit, gather the light that
// surface receives this frame (AU_GIHitLight). Gathering the previous frame's final image
// instead fed the GI back into itself: dense green canopies amplified frame after frame into
// glowing lime trees. Noise control (the TAA only gets ~10 frames, fewer while
// moving): the rays are stratified over the hemisphere (cosine rings x golden-angle spiral,
// rotated per pixel and frame) instead of a correlated random sequence, occlusion fades with the
// hit distance instead of switching per ray, and the radiance comes from a blurred mip, clamped.
vec3 AU_GetSSGI(sampler2D depthTex, vec3 playerPos, vec3 normal, vec3 geometricNormal, float dither, out float occlusion)
{
    const int rays = AU_RTX_GI_RAYS;
    const int steps = 8;
    float radius = AU_RTX_GI_RADIUS * (1.0 + length(playerPos) * 0.02);
#if AU_SSGI_SELF_HIT_FIX
    vec3 planeNormal = dot(geometricNormal, normal) < 0.0 ? -geometricNormal : geometricNormal;
    vec3 origin = playerPos + planeNormal * 0.05;
#else
    vec3 origin = playerPos + normal * 0.05;
#endif
    // Blue noise (two channels independent of dither) for the stratum jitter and rotation.
    vec2 pn = GetDither2(gl_FragCoord.xy);
    float jitter = pn.x;
    float rot = pn.y;
    vec3 gi = vec3(0.0);
    float occluded = 0.0;
    for (int r = 0; r < rays; r++) {
        float u1 = (float(r) + jitter) / float(rays);
        float u2 = fract(float(r) * 0.618034 + rot);
        vec3 dir = AU_HemisphereDir(normal, u1, u2);
#if AU_SSGI_SELF_HIT_FIX
        float belowSurface = min(dot(dir, planeNormal) - 0.01, 0.0);
        dir = normalize(dir - planeNormal * belowSurface);
#endif
        for (int s = 1; s <= steps; s++) {
            float t = radius * pow(float(s) / float(steps), 1.6);
            vec3 p = origin + dir * t;
            vec2 uv = AU_ProjectToScreen(p);
            if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
            float lin1 = AU_SceneLinAt(depthTex, uv);
            float lin2 = AU_LinOf(p);
            if (lin1 > 0.0 && lin2 > lin1) {
#if AU_SSGI_SELF_HIT_FIX
                vec3 scenePosition = p * (lin1 / max(lin2, 0.0001));
                if (abs(dot(scenePosition - playerPos, planeNormal)) < 0.03) continue;
#endif
                // Thickness check; ignore the first-person hand / held item (it is in the depth buffer).
                if (lin1 > AU_NEAR_IGNORE && lin2 - lin1 < radius * 0.75) {
                    float near = 1.0 - t / radius;
                    // Occlusion only from close geometry: long rays are for the bounce colour,
                    // and would otherwise darken whole cliff faces.
                    occluded += max(0.0, 1.0 - t / AU_RTX_AO_RADIUS);
                    gi += min(AU_GIHitLight(uv), vec3(1.5)) * (0.5 + 0.5 * near);
                }
                break;
            }
        }
    }
    occlusion = 1.0 - occluded / float(rays);
    return gi / float(rays);
}

// Screen-space reflection with sky fallback. Returns rgb + confidence (a).
vec4 AU_GetSSR(sampler2D depthTex, vec3 playerPos, vec3 normal, float roughness, float dither)
{
    vec3 nView = normalize(playerPos);
    vec3 dir = reflect(nView, normal);
    float lViewPos = length(playerPos);
    const int steps = AU_RTX_SSR_STEPS;
    float stepLen = 0.6 + lViewPos * 0.05;
    vec3 p = playerPos + normal * 0.05 + dir * stepLen * dither;
    for (int i = 0; i < steps; i++) {
        p += dir * stepLen;
        stepLen *= 1.18;
        vec2 uv = AU_ProjectToScreen(p);
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float lin1 = AU_SceneLinAt(depthTex, uv);
        float lin2 = AU_LinOf(p);
        if (lin1 > 0.0 && lin2 > lin1) {
            if (lin1 < AU_NEAR_IGNORE) continue;
            if (lin2 - lin1 > stepLen * 2.0 + 0.5) continue;   // passed behind a thin object
            vec3 hitPos = p * (lin1 / lin2);
            vec3 rad = AU_PrevSceneAt(hitPos, roughness * 5.0);
            if (rad.r < 0.0) break;
            vec2 edge = abs(uv - 0.5) * 2.0;
            float fade = 1.0 - pow(max(edge.x, edge.y), 6.0);
            return vec4(rad, fade);
        }
    }
    return vec4(0.0);
}

#endif // AU_RTX
