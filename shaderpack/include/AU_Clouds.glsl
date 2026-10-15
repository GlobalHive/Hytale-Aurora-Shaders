// Aurora Shaders for Hytale - volumetric clouds.
// Two ray-marched cloud layers shaped by a tiling noise texture.

#ifndef AU_CLOUDS_GLSL
#define AU_CLOUDS_GLSL

#include "AU_Sky.glsl"

uniform sampler2D noisetex;

float Noise3D(vec3 p, float lod)
{
    p.z = fract(p.z) * 128.0;
    float iz = floor(p.z);
    float fz = fract(p.z);
    vec2 a_off = vec2(23.0, 29.0) * (iz) / 128.0;
    vec2 b_off = vec2(23.0, 29.0) * (iz + 1.0) / 128.0;
    float a = textureLod(noisetex, p.xy + a_off, lod).r;
    float b = textureLod(noisetex, p.xy + b_off, lod).r;
    return mix(a, b, fz);
}

float Noise3D(vec3 p)
{
    return Noise3D(p, 0.0);
}

const float cloudStretch = 16.0;
const float cloudNarrowness = 0.00012;
const float cloudTallness = cloudStretch * 2.0;

vec3 cloudAmbientColor, cloudLightColor;

void InitCloudColors()
{
    vec3 cloudRainColor = mix(nightMiddleSkyColor, dayMiddleSkyColor, sunFactor);
    cloudAmbientColor = mix(ambientColor * (sunVisibility2 * (0.55 + vec3(0.2, 0.1, 0.0) * noonFactor) + 0.35), cloudRainColor * 0.5, rainFactor);
    cloudLightColor = mix(lightColor * (1.3 + vec3(0.1, -0.1, -0.3) * noonFactor), cloudRainColor * 0.45, noonFactor * rainFactor);
#if AU_BIOME_ATMOSPHERE
    vec3 nativeTint = AU_BiomeCloudColour().rgb;
    cloudAmbientColor = nativeTint * 0.55;
    cloudLightColor = nativeTint * 0.45;
#endif
}

float GetCloudWind()
{
    return 0.0006 * Time * AU_CLOUD_SPEED;
}

float GetCloudNoise(vec3 tracePos, float cloudAltitude, float lTracePosXZ, float cloudPlayerPosY, float footprint)
{
    vec3 tracePosM = tracePos.xyz * cloudNarrowness;
    float wind = GetCloudWind();
    float noise = 0.0;
    float currentPersist = 1.0;
    float total = 0.0;

    const int sampleCount = 4;
    const float persistance = 0.5;
    float noiseMult = 1.14; // CLOUD_QUALITY 2, main (deferred1) path

    for (int i = 0; i < sampleCount; i++) {
        float lod = log2(max(footprint * cloudNarrowness * float(textureSize(noisetex, 0).x), 1.0));
        noise += Noise3D(tracePosM - vec3(0.0, 0.0, wind), lod) * currentPersist;
        total += currentPersist;
        tracePosM *= 3.0;
        wind *= 0.5;
        footprint *= 3.0;
        currentPersist *= persistance;
    }
    noise = pow2(noise / total);

    noiseMult *= 0.8
               + 0.1 * clamp01(-cloudPlayerPosY / cloudTallness)
               + 0.40 * rainFactor;
    noise *= noiseMult * AU_CLOUD_AMOUNT;

    float threshold = clamp(abs(cloudAltitude - tracePos.y) / cloudStretch, 0.001, 0.999);
    threshold = pow2(pow2(pow2(threshold)));
    return noise - (threshold * 0.2 + 0.25);
}

float GetCloudNoise(vec3 tracePos, float cloudAltitude, float lTracePosXZ, float cloudPlayerPosY)
{
    return GetCloudNoise(tracePos, cloudAltitude, lTracePosXZ, cloudPlayerPosY, 0.0);
}

vec4 GetVolumetricClouds(float cloudAltitude, float distanceThreshold, float skyFade, float skyMult0,
                         vec3 cameraPos, vec3 nPlayerPos, float lViewPosM, float VdotS, float VdotU, float dither)
{
    vec4 volumetricClouds = vec4(0.0);

    float higherPlaneAltitude = cloudAltitude + cloudStretch;
    float lowerPlaneAltitude  = cloudAltitude - cloudStretch;

    float lowerPlaneDistance  = (lowerPlaneAltitude - cameraPos.y) / nPlayerPos.y;
    float higherPlaneDistance = (higherPlaneAltitude - cameraPos.y) / nPlayerPos.y;
    float minPlaneDistance = max(min(lowerPlaneDistance, higherPlaneDistance), 0.0);
    float maxPlaneDistance = max(lowerPlaneDistance, higherPlaneDistance);
    if (maxPlaneDistance < 0.0) return vec4(0.0);
    float planeDistanceDif = maxPlaneDistance - minPlaneDistance;

    float aboveClouds = clamp01((cameraPos.y - lowerPlaneAltitude) * 0.01);
    float skyFadeM = mix(skyFade, 1.0, 0.4 * max(aboveClouds, rainFactor2));

    float stepMult = AU_CLOUD_STEP;
    int sampleCount = int(planeDistanceDif / stepMult + dither + 1.0);
    sampleCount = min(sampleCount, 96);
    vec3 traceAdd = nPlayerPos * stepMult;
    vec3 tracePos = cameraPos + minPlaneDistance * nPlayerPos;
    tracePos += traceAdd * dither;
    tracePos.y -= traceAdd.y;

    float VdotSM1 = max0(sunVisibility > 0.5 ? VdotS : -VdotS);
    float VdotSM1M = VdotSM1 * invRainFactor;
    float VdotSM2 = pow2(VdotSM1) * abs(sunVisibility - 0.5) * 2.0;
    float VdotSM3 = VdotSM2 * (2.5 + rainFactor) + 1.5 * rainFactor;
    float VdotSM4 = pow(VdotSM1M, 100.0) * sunVisibility;

    vec3 cloudSkyColor = GetSky(VdotU, VdotS, dither, IsCameraUnderwater < 0.5, false);

    for (int i = 0; i < sampleCount; i++) {
        tracePos += traceAdd;
        if (abs(tracePos.y - cloudAltitude) > cloudStretch) break;

        vec3 cloudPlayerPos = tracePos - cameraPos;
        float lTracePos = length(cloudPlayerPos);
        float lTracePosXZ = length(cloudPlayerPos.xz);
        float cloudMult = 1.0;
        if (lTracePosXZ > distanceThreshold) break;
        if (lTracePos > lViewPosM) {
            if (skyFade < 0.7) continue;
            else cloudMult = skyMult0;
        }

        float cloudNoise = GetCloudNoise(tracePos, cloudAltitude, lTracePosXZ, cloudPlayerPos.y);

        if (cloudNoise > 0.00001) {
            // Opacity is tuned for 32-block steps; scale for our step size.
            float opacityFactor = min1(cloudNoise * 8.0) * (stepMult / 32.0);

            float cloudShading = 1.0 - (higherPlaneAltitude - tracePos.y) / cloudTallness;
            cloudShading *= 1.0 + 0.2 * VdotSM3 * (1.0 - opacityFactor) + VdotSM4;

            vec3 colorSample = cloudAmbientColor * (0.4 + 0.6 * cloudShading) + cloudLightColor * cloudShading;
            float distanceRatio = (distanceThreshold - lTracePosXZ) / distanceThreshold;
            float cloudDistanceFactor = clamp(distanceRatio, 0.0, 0.8) * 1.25;
            float cloudFogFactor = pow2(pow1_5(clamp(distanceRatio, 0.0, 1.0))) * 0.69;
            cloudFogFactor = mix(1.0, cloudFogFactor, skyFadeM);
            colorSample *= 1.0 - 0.2 * (1.0 - skyFadeM) * max(sunVisibility2, nightFactor);
            colorSample = mix(cloudSkyColor, colorSample, cloudFogFactor * 0.69);

            volumetricClouds.rgb = mix(volumetricClouds.rgb, colorSample, 1.0 - min1(volumetricClouds.a));
            volumetricClouds.a += opacityFactor * pow(cloudDistanceFactor, 0.5 + 10.0 * pow(abs(VdotSM1M), 90.0)) * cloudMult;

            if (volumetricClouds.a > 0.9) {
                volumetricClouds.a = 1.0;
                break;
            }
        }
    }

    volumetricClouds.a *= 0.5 + 0.5 * skyFadeM;
    return volumetricClouds;
}

// Shadow of the volumetric cloud layer on a surface (1 = no cloud), sampling the same noise
// the sky renders, where the light ray crosses the middle of the layer.
float CloudShadowDistance(float viewerHeight, float sampleHeight, float lightElevation)
{
    const float curvatureRadius = 65536.0;
    float height = max(sampleHeight - viewerHeight, 0.0);
    float radialDistance = max(curvatureRadius + viewerHeight, 1.0);
    float projectedRadius = radialDistance * max(lightElevation, 0.0);
    float shellDifference = height * (2.0 * radialDistance + height);
    return shellDifference / max(sqrt(projectedRadius * projectedRadius + shellDifference) + projectedRadius, 0.0001);
}

float GetCloudShadow(vec3 playerPos)
{
#if AU_CLOUDS && AU_CLOUD_SHADOWS
    float horizonWeight = smoothstep(0.0, 0.08, lightVec.y);
    if (horizonWeight <= 0.0) return 1.0;
    float modFactor = floor(1.0 / cloudNarrowness);
    vec3 cameraPos = vec3(mod(CameraPosition.x, modFactor), CameraPosition.y, mod(CameraPosition.z, modFactor));
    vec3 worldPos = playerPos + cameraPos;
    float lowerHeight = max(worldPos.y, AU_CLOUD_ALT1 - cloudStretch);
    float upperHeight = AU_CLOUD_ALT1 + cloudStretch;
    if (lowerHeight >= upperHeight) return 1.0;
    float entryDistance = CloudShadowDistance(worldPos.y, lowerHeight, lightVec.y);
    float exitDistance = CloudShadowDistance(worldPos.y, upperHeight, lightVec.y);
    const int sampleCount = 4;
    float sampleSpan = (exitDistance - entryDistance) / float(sampleCount);
    float layerWeight = (upperHeight - lowerHeight) / cloudTallness;
    float opticalDepth = 0.0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
        float fraction = (float(sampleIndex) + 0.5) / float(sampleCount);
        float alt = mix(lowerHeight, upperHeight, fraction);
        float t = CloudShadowDistance(worldPos.y, alt, lightVec.y);
        vec3 cloudPos = worldPos + lightVec * t;
        cloudPos.y = alt;
        float footprint = max(t * 0.0093, max(sampleSpan, 8.0));
        float noise = GetCloudNoise(cloudPos, AU_CLOUD_ALT1, 0.0, 0.0, footprint);
        opticalDepth += max(noise, 0.0) * layerWeight * (2.0 / float(sampleCount));
    }
    float shadow = exp(-3.0 * opticalDepth);
#if AU_BIOME_ATMOSPHERE
    return mix(1.0, shadow, horizonWeight * AU_CLOUD_SHADOW_STRENGTH * AU_BiomeCloudColour().a);
#else
    return mix(1.0, shadow, horizonWeight * AU_CLOUD_SHADOW_STRENGTH);
#endif
#else
    return 1.0;
#endif
}

vec4 GetClouds(vec3 nPlayerPos, float lViewPos, float skyFade, float VdotS, float VdotU, float dither)
{
    InitCloudColors();
    float lViewPosM = lViewPos < GetRenderDistance() * 1.5 ? lViewPos - 1.0 : 1000000000.0;
    float skyMult0 = pow2(skyFade * 3.333333 - 2.333333);

    // Keep the trace coordinates small (the noise tiles every 1 / cloudNarrowness blocks).
    float modFactor = floor(1.0 / cloudNarrowness);
    vec3 cameraPos = vec3(mod(CameraPosition.x, modFactor), CameraPosition.y, mod(CameraPosition.z, modFactor));

    vec4 clouds = GetVolumetricClouds(AU_CLOUD_ALT1, AU_CLOUD_DISTANCE, skyFade, skyMult0,
                                      cameraPos, nPlayerPos, lViewPosM, VdotS, VdotU, dither);
    return clouds;
}

#endif
