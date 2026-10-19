// Aurora Shaders for Hytale - fog.
// Atmospheric, border, water and cave fog.

#ifndef AU_FOG
#define AU_FOG

#include "AU_Sky.glsl"

#ifndef AU_ATM_FOG_ALTITUDE
#define AU_ATM_FOG_ALTITUDE 90.0
#endif

float GetEyeBrightness()
{
    // Hytale's temporally smoothed screen-average sky light, with the sky itself counted as
    // open (programs/SunOcclusionDownsampleFS.glsl): ~Minecraft's eyeBrightness.
    return clamp(SceneBrightness.r, 0.0, 1.0);
}

float AU_ApplyBiomeFog(inout vec3 color, vec4 fog)
{
    float amount = clamp01(fog.a);
#if !AU_ATMOSPHERIC_FOG
    if (IsCameraUnderwater < 0.5) amount = 0.0;
#endif
    float density = max(AU_ATM_FOG_MULT, 0.0) / 0.95;
    amount = density > 0.0 ? 1.0 - pow(max(1.0 - amount, 0.0), density) : 0.0;
    color = mix(color, max(fog.rgb, vec3(0.0)), amount);
    return amount;
}

vec3 GetAtmFogColor(float altitudeFactorRaw, float VdotS)
{
    float nightFogMult = 2.5 - 0.625 * max(pow2(pow2(altitudeFactorRaw)), rainFactor);
    float dayNightFogBlend = pow(invNightFactor, 4.0 - VdotS - 2.5 * sunVisibility2);
    return mix(nightUpSkyColor * (nightFogMult - dayNightFogBlend * nightFogMult),
               dayDownSkyColor * (0.9 + 0.3 * noonFactor),
               dayNightFogBlend);
}

float GetAtmFogAltitudeFactor(float altitude)
{
    const float atmFogSRATA = AU_ATM_FOG_ALTITUDE + 0.1;
    const float atmFogCRFTM = 60.0;
    float altitudeFactor = pow2(1.0 - clamp(altitude - atmFogSRATA, 0.0, atmFogCRFTM) / atmFogCRFTM);
    altitudeFactor = mix(altitudeFactor, 1.0, rainFactor * 0.2);
    return altitudeFactor;
}

void DoAtmosphericFog(inout vec3 color, vec3 playerPos, float lViewPos, float VdotS, float dither)
{
#if AU_ATMOSPHERE_MODEL == 1 && !AU_BIOME_ATMOSPHERE
    float fog = AU_AtmosphereFogAmount(playerPos, lViewPos, GetEyeBrightness());
    float viewUp = playerPos.y / max(lViewPos, 0.0001);
    color = mix(color, GetSky(viewUp, VdotS, 0.5, false, false), fog);
#else
    float renDisFactor = min1(192.0 / GetRenderDistance()) / AU_ATM_FOG_DISTANCE;
    float fog = 1.0 - exp(-pow(lViewPos * (0.001 - 0.0007 * rainFactor), 2.0 - rainFactor2) * lViewPos * renDisFactor);

    float altitudeFactorRaw = GetAtmFogAltitudeFactor(playerPos.y + CameraPosition.y);
    float altitudeFactor = altitudeFactorRaw * 0.9 + 0.1;

    fog *= AU_ATM_FOG_MULT - 0.1 - 0.15 * invRainFactor;

    const float atmFogCRFTM = 60.0;
    float cameraAltitudeFactor = GetAtmFogAltitudeFactor(CameraPosition.y + 0.25 * atmFogCRFTM);
    altitudeFactor = mix(altitudeFactor, 1.0, 0.8 * cameraAltitudeFactor);
    altitudeFactor *= 1.0 - 0.5 * cameraAltitudeFactor * invRainFactor;

    fog *= 0.2 + 0.8 * sqrt2(GetEyeBrightness());
    fog *= altitudeFactor;

    if (fog > 0.01 + 0.0025 * dither) {
        fog = clamp(fog, 0.0, 1.0);
        color = mix(color, GetAtmFogColor(altitudeFactorRaw, VdotS), fog);
    }
#endif
}

// Returns the fog amount (the sky fade) and blends `color` towards the sky.
float DoBorderFog(inout vec3 color, float lPos, float VdotU, float VdotS, float dither)
{
    float fog = lPos / GetRenderDistance();
    fog = pow2(pow2(fog));
    fog = pow2(pow2(fog));
    fog = 1.0 - exp(-3.0 * fog);

    if (fog > 0.01 + 0.0025 * dither) {
        fog = clamp(fog, 0.0, 1.0);
        vec3 fogColorM = GetSky(VdotU, VdotS, dither, true, false);
        color = mix(color, fogColorM, fog);
        return fog;
    }
    return 0.0;
}

float GetWaterFog(float lViewPos)
{
    float fog = lViewPos * AU_WATER_FOG_MULT / 48.0;
    fog *= fog;
    return 1.0 - exp(-fog);
}

// Underground: no sky light reaching the eye and below the surface (Hytale's terrain sits
// around y 100-140, higher than Minecraft's).
float GetCaveFactor()
{
    return (1.0 - GetEyeBrightness()) * clamp((AU_CAVE_ALTITUDE - CameraPosition.y) / 30.0, 0.0, 1.0);
}

// openness: 1 where this point itself is open to the sky. The cave factor alone follows Hytale's
// screen-average sky light, which eases over about a second, so stepping out of a cave left the
// bright landscape in cave fog (and cave haze) for that long. Only what is cut off from the sky
// gets cave fog, so the outdoors seen from a cave - or right after leaving it - stays clear.
float AU_Openness(float skyLightHere)
{
    return smoothstep(0.3, 0.9, skyLightHere);
}

void DoCaveFog(inout vec3 color, float lViewPos, float openness)
{
    float caveFactor = GetCaveFactor() * (1.0 - openness);
    vec3 caveFogColor = vec3(0.13, 0.13, 0.15) * (0.7 + 0.3 * vsBrightness);
    float fog = caveFactor * (0.9 - 0.9 * exp(-lViewPos * 0.015));
    color = mix(color, caveFogColor, fog);
}

// Bloom fog: distant scenery feeds more into the bloom at
// night, in rain, in caves and underwater, which reads as glowing haze.
float GetBloomFog(float lViewPos, float openness)
{
    float bloomFog = pow2(pow2(1.0 - exp(-lViewPos * (0.02 + 0.04 * IsCameraUnderwater))));
    float mult;
    if (IsCameraUnderwater < 0.5) {
        mult = (rainFactor2 * 8.0 + 3.0 * (1.0 - sunFactor)) * GetEyeBrightness() + GetCaveFactor() * (1.0 - openness) * 14.0;
    } else {
        mult = 14.0;
    }
    mult *= AU_BLOOM_STRENGTH * 8.33333;
    return 1.0 + bloomFog * mult;
}

// Full fog stack for an opaque surface at positionFromCameraWS.
float DoAUFog(inout vec3 color, vec3 playerPos, float dither, float skyLightHere)
{
    float lViewPos = length(playerPos);
    vec3 nPlayerPos = playerPos / max(lViewPos, 0.0001);
    float VdotU = nPlayerPos.y;
    float VdotS = dot(nPlayerPos, sunVec);
    float skyFade = 0.0;

    DoCaveFog(color, lViewPos, AU_Openness(skyLightHere));
#if AU_ATMOSPHERIC_FOG
    DoAtmosphericFog(color, playerPos, lViewPos, VdotS, dither);
#endif
#if AU_BORDER_FOG
    skyFade = DoBorderFog(color, max(length(playerPos.xz), abs(playerPos.y)), VdotU, VdotS, dither);
#endif
    if (IsCameraUnderwater > 0.5) color = mix(color, waterFogColor, GetWaterFog(lViewPos));
    return skyFade;
}

#endif // AU_FOG
