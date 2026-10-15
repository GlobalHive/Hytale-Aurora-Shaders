#ifndef AU_DENOISE_LEGACY_INC
#define AU_DENOISE_LEGACY_INC
#include "AU_Temporal.glsl"
#include "AU_DenoiseShared.glsl"
uniform int uHookPTPass;
uniform sampler2D auGIHist;
uniform sampler2D auGIHistPos;
#if AU_VOXELS && AU_PT_LIGHTS
uniform sampler2D auGIHistLight;
#endif
layout(rgba16f) uniform image2D auGIOut;
layout(rgba32f) uniform image2D auGIOutPos;
#if AU_VOXELS && AU_PT_LIGHTS
layout(rgba16f) uniform image2D auGIOutLight;
#endif
#if AU_DEBUG_CLASS == 9
vec3 auGIDiagnostic = vec3(0.0);
#endif

float AU_GILegacyMetadata(float age, float occlusion, float guide, bool firstPerson)
{
    uint encodedGuide = uint(round(clamp(log2(1.0 + max(guide, 0.0)) / 16.0, 0.0, 1.0) * 16383.0));
    uint bits = uint(clamp(age, 0.0, 63.0)) | (uint(round(clamp(occlusion, 0.0, 1.0) * 255.0)) << 6)
              | (encodedGuide << 14) | (firstPerson ? 1u << 29 : 0u) | (1u << 30);
    return uintBitsToFloat(bits);
}

void AU_GIQuadFilter(vec3 position, vec3 normal, bool emissive, inout vec3 gi,
                     inout float occlusion, inout float skyVis, inout vec3 directLight)
{
#if AU_GI_SPATIAL_FILTER
    vec2 direction = vec2(0.5) - vec2(ivec2(gl_FragCoord.xy) & ivec2(1));
    vec3 deltaX = dFdx(position), deltaY = dFdy(position);
    bool sameSurface = length(fwidth(normal)) < AU_GI_NORMAL_THRESHOLD && fwidth(float(emissive)) < 0.5
        && abs(dot(deltaX, normal)) + abs(dot(deltaY, normal)) < 0.015
        && max(length(deltaX), length(deltaY)) < 0.1 + length(position) * 0.003;
    vec4 lighting = vec4(gi, skyVis);
    vec4 horizontal = lighting + direction.x * dFdx(lighting);
    vec4 filtered = horizontal + direction.y * dFdy(horizontal);
    float horizontalAO = occlusion + direction.x * dFdx(occlusion);
    float filteredAO = horizontalAO + direction.y * dFdy(horizontalAO);
    vec3 horizontalLight = directLight + direction.x * dFdx(directLight);
    vec3 filteredLight = horizontalLight + direction.y * dFdy(horizontalLight);
    if (sameSurface) {
        gi = mix(gi, max(filtered.rgb, vec3(0.0)), AU_GI_SPATIAL_STRENGTH);
        skyVis = mix(skyVis, max(filtered.a, 0.0), AU_GI_SPATIAL_STRENGTH);
        occlusion = mix(occlusion, clamp(filteredAO, 0.0, 1.0), AU_GI_SPATIAL_STRENGTH);
        directLight = mix(directLight, max(filteredLight, vec3(0.0)), AU_GI_SPATIAL_STRENGTH);
    }
#endif
}

void AU_GIAccumulate(vec2 uv, vec3 playerPos, vec3 normal, inout vec3 gi, inout float occlusion, inout float skyVis, inout vec3 directLight, float lightGuide)
{
    vec3 worldPosition = playerPos + CameraPosition;
    vec2 previousUV = AU_GIPreviousUV(playerPos);
    vec3 viewPosition = mat3(ViewMatrix) * playerPos;
    bool firstPerson = -viewPosition.z < AU_NEAR_IGNORE;
    if (firstPerson) {
        worldPosition = viewPosition;
        normal = normalize(mat3(ViewMatrix) * normal);
        previousUV = uv;
    }
    float historyLength = 0.0, historyAO = 1.0;
    vec4 historyRadiance = vec4(0.0);
    vec3 historyLight = vec3(0.0);
    float previousGuide = 0.0;
    if (uHookHistoryValid != 0 && all(greaterThan(previousUV, vec2(0.0))) && all(lessThan(previousUV, vec2(1.0)))) {
        ivec2 size = textureSize(auGIHistPos, 0);
        ivec2 previousPixel = ivec2(previousUV * vec2(size));
        float distanceToCamera = length(playerPos);
        float planeTolerance = AU_GI_PLANE_TOLERANCE + distanceToCamera * 0.002;
        float reachSquared = pow2(AU_GI_HISTORY_REACH + distanceToCamera * 0.02);
        float softening = pow2(0.01 + distanceToCamera * 0.002);
        float weightSum = 0.0, lengthSum = 0.0, occlusionSum = 0.0;
        for (int sampleIndex = 0; sampleIndex < AU_GI_HISTORY_TAPS; sampleIndex++) {
#if AU_GI_HISTORY_TAPS == 1
            ivec2 samplePixel = clamp(previousPixel, ivec2(0), size - 1);
#elif AU_GI_HISTORY_TAPS == 4
            ivec2 samplePixel = clamp(ivec2(floor(previousUV * vec2(size) - 0.5)) + ivec2(sampleIndex & 1, sampleIndex >> 1), ivec2(0), size - 1);
#else
            ivec2 samplePixel = clamp(previousPixel + ivec2(sampleIndex % 3 - 1, sampleIndex / 3 - 1), ivec2(0), size - 1);
#endif
            vec4 historyPosition = texelFetch(auGIHistPos, samplePixel, 0);
            uint metadata = floatBitsToUint(historyPosition.w);
            if (((metadata & (1u << 29)) != 0u) != firstPerson) continue;
            float age = float(metadata & 63u);
            vec3 difference = historyPosition.xyz - worldPosition;
            float distanceSquared = dot(difference, difference);
            if (age < 1.0 || abs(dot(difference, normal)) > planeTolerance || distanceSquared > reachSquared) continue;
            float sampleWeight = 1.0 / (softening + distanceSquared);
            weightSum += sampleWeight;
            lengthSum += age * sampleWeight;
            occlusionSum += float((metadata >> 6) & 255u) / 255.0 * sampleWeight;
            historyRadiance += texelFetch(auGIHist, samplePixel, 0) * sampleWeight;
#if AU_VOXELS && AU_PT_LIGHTS
            vec4 oldLight = texelFetch(auGIHistLight, samplePixel, 0);
            historyLight += oldLight.rgb * sampleWeight;
#endif
            previousGuide += float((metadata >> 14) & 16383u) * sampleWeight;
        }
        if (weightSum > 0.0) {
            historyLength = floor(lengthSum / weightSum + 0.5);
            historyAO = occlusionSum / weightSum;
            historyRadiance /= weightSum;
            historyLight /= weightSum;
            previousGuide = exp2(previousGuide / weightSum * (16.0 / 16383.0)) - 1.0;
        }
    }
#if AU_VOXELS
    historyLength = min(historyLength + 1.0, min(AU_PT_HISTORY, 63.0));
#else
    historyLength = min(historyLength + 1.0, min(AU_SSGI_HISTORY, 63.0));
#endif
    float change = abs(previousGuide - lightGuide) / max(max(previousGuide, lightGuide), 0.05);
    if (change > AU_GI_LIGHT_REJECTION) historyLength = 1.0;
    float currentWeight = AU_GIHistoryWeight(historyLength, uv, previousUV);
#if AU_DEBUG_CLASS == 9
    auGIDiagnostic = vec3(currentWeight, historyLength / 24.0, min(change, 1.0));
#endif
    vec4 acc = mix(historyRadiance, vec4(gi, skyVis), currentWeight);
    occlusion = mix(historyAO, clamp(occlusion, 0.0, 1.0), currentWeight);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    imageStore(auGIOut, pixel, acc);
    imageStore(auGIOutPos, pixel, vec4(worldPosition, AU_GILegacyMetadata(historyLength, occlusion, lightGuide, firstPerson)));
#if AU_VOXELS && AU_PT_LIGHTS
    directLight = mix(historyLight, directLight, currentWeight);
    imageStore(auGIOutLight, pixel, vec4(directLight, lightGuide));
#endif
    gi = acc.rgb;
    skyVis = acc.a;
}
#endif
