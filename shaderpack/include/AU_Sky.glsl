// Aurora Shaders for Hytale - sky.
// Procedural sky gradient, sunset glow, sun glare and stars.

#ifndef AU_SKY
#define AU_SKY

#include "AU_Common.glsl"

vec3 GetSky(float VdotU, float VdotS, float dither, bool doGlare, bool doGround)
{
    float VdotUmax0 = max(VdotU, 0.0);
    float VdotUmax0M = 1.0 - pow2(VdotUmax0);
    float VdotSML = sunVisibility > 0.5 ? VdotS : -VdotS;
#if AU_BIOME_ATMOSPHERE
    vec3 finalSky = AU_BiomeSky(VdotU, VdotS);
#else
    float nightFactorSqrt2 = sqrt2(nightFactor);
    float nightFactorM = sqrt2(nightFactorSqrt2) * 0.4;
    float VdotSM1 = pow2(max(VdotS, 0.0));
    float VdotSM2 = pow2(VdotSM1);
    float VdotSM3 = pow2(pow2(max(-VdotS, 0.0)));

    vec3 upColor = mix(nightUpSkyColor * (1.5 - 0.5 * nightFactorSqrt2 + nightFactorM * VdotSM3 * 1.5), dayUpSkyColor, sunFactor);
    vec3 middleColor = mix(nightMiddleSkyColor * (3.0 - 2.0 * nightFactorSqrt2), dayMiddleSkyColor * (1.0 + VdotSM2 * 0.3), sunFactor);
    vec3 downColor = mix(nightDownSkyColor, dayDownSkyColor, (sunFactor + sunVisibility) * 0.5);

    // Sky gradient
    float VdotUM1 = pow2(1.0 - VdotUmax0);
    VdotUM1 = pow(VdotUM1, (1.0 - VdotSM2 * 0.4) * (1.15 - 0.15 * rainFactor2));
    VdotUM1 = mix(VdotUM1, 1.0, rainFactor2 * 0.15);
    vec3 finalSky = mix(upColor, middleColor, VdotUM1);

    // Sunset colour
    float VdotUM2 = pow2(1.0 - abs(VdotU));
    VdotUM2 = VdotUM2 * VdotUM2 * (3.0 - 2.0 * VdotUM2);
    VdotUM2 *= (0.7 - nightFactorM + VdotSM1 * (0.3 + nightFactorM)) * invNoonFactor * sunFactor;
    finalSky = mix(finalSky, sunsetDownSkyColorP * (1.0 + VdotSM1 * 0.3), VdotUM2 * invRainFactor);

    // Sky ground with fake light scattering
    float VdotUM3 = min(max0(-VdotU + 0.08) / 0.35, 1.0);
    VdotUM3 = smoothstep1(VdotUM3);
    vec3 scatteredGroundMixer = vec3(VdotUM3 * VdotUM3, sqrt1(VdotUM3), sqrt3(VdotUM3));
    scatteredGroundMixer = mix(vec3(VdotUM3), scatteredGroundMixer, 0.75 - 0.5 * rainFactor);
    finalSky = mix(finalSky, downColor, scatteredGroundMixer);

    if (doGround) finalSky *= smoothstep1(pow2(1.0 + min(VdotU, 0.0)));
#endif

    if (IsCameraUnderwater > 0.5) finalSky = mix(finalSky * 3.0, waterFogColor, VdotUmax0M);

    // Sun / moon glare
    if (doGlare && 0.0 < VdotSML) {
        float glareScatter = 3.0 * (2.0 - clamp01(VdotS * 1000.0));
        glareScatter *= 1.0 - 0.75 * rainFactor2;
        float VdotSM4 = pow(abs(VdotS), glareScatter);

        float visfactor = 0.075;
        float glare = visfactor / (1.0 - (1.0 - visfactor) * VdotSM4) - visfactor;
        glare *= 0.7;

        float glareWaterFactor = IsCameraUnderwater * sunVisibility;
        vec3 glareColor = mix(vec3(0.38, 0.4, 0.5) * 0.3, vec3(1.5, 0.7, 0.3) + vec3(0.0, 0.5, 0.5) * noonFactor, sunVisibility);
#if AU_BIOME_ATMOSPHERE
        glareColor = max(SunColor.rgb, vec3(0.0)) * max(SunLightColor.a, 0.0);
#endif
        glareColor = glareColor + glareWaterFactor * vec3(7.0);
        glare *= 1.0 - 0.8 * rainFactor;
        glareColor = mix(glareColor, vec3(GetLuminance(glareColor)), 0.5 * rainFactor);

        finalSky += glareColor * glare * shadowTime;
    }

    finalSky = max(finalSky + (dither - 0.5) / 128.0, vec3(0.0));
    return finalSky;
}

float GetStarNoise(vec2 pos)
{
    return fract(sin(dot(pos, vec2(12.9898, 4.1414))) * 43758.54953);
}

vec3 GetStars(vec3 dirWS, float VdotU, float VdotS)
{
    if (VdotU < 0.0) return vec3(0.0);
    vec3 starCoord3 = dirWS / (dirWS.y + length(dirWS.xz) * 0.5);
    vec2 starCoord = starCoord3.xz;
    starCoord.x += 0.006 * Time;

    starCoord *= 0.2;
    float starFactor = 1024.0;
    starCoord = floor(starCoord * starFactor) / starFactor;

    float star = 1.0;
    star *= GetStarNoise(starCoord.xy);
    star *= GetStarNoise(starCoord.xy + 0.1);
    star *= GetStarNoise(starCoord.xy + 0.23);

    star -= 0.7;  // NIGHT_STAR_AMOUNT 2
    star = max0(star);
    star *= star;

    star *= min1(VdotU * 3.0) * max0(1.0 - pow(abs(VdotS) * 1.002, 100.0));
    star *= invRainFactor * pow2(pow2(invNoonFactor2)) * (1.0 - 0.5 * sunVisibility);

    return 40.0 * star * vec3(0.38, 0.4, 0.5);
}

#endif // AU_SKY
