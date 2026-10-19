#ifndef AU_ATMOSPHERE_GLSL
#define AU_ATMOSPHERE_GLSL

#include "AU_Common.glsl"

#ifndef AU_ATM_FOG_ALTITUDE
#define AU_ATM_FOG_ALTITUDE 90.0
#endif

vec3 AU_AtmosphereSky(float viewUp, float viewSun, bool ground)
{
    float elevation = clamp(viewUp, 0.0, 1.0);
    float alignment = clamp(viewSun, -1.0, 1.0);
    float density = exp(-clamp(CameraPosition.y - AU_ATM_FOG_ALTITUDE, -512.0, 4096.0) / 1600.0);
    float viewMass = inversesqrt(elevation * elevation + 0.016);
    float sunHeight = max(sunVec.y, 0.0);
    float sunMass = inversesqrt(sunHeight * sunHeight + 0.003);
    sunMass = mix(sunMass, min(sunMass, 3.0), smoothstep(0.0, 0.8, elevation));
    vec3 rayleigh = vec3(0.055, 0.13, 0.30);
    float mie = mix(0.012, 0.065, rainFactor);
    vec3 extinction = rayleigh + mie;
    vec3 transmission = exp(-extinction * viewMass * density);
    vec3 ozoneDepth = vec3(0.006, 0.055, 0.002) * (sunMass + viewMass) * 0.8;
    vec3 sunlight = exp(-extinction * sunMass * density * 0.65 - ozoneDepth);
    float rayleighPhase = 0.75 * (1.0 + alignment * alignment);
    float miePhase = 0.0612 / pow(max(1.49 - 1.4 * alignment, 0.09), 1.5);
    vec3 scattering = (1.0 - transmission) * (rayleigh * rayleighPhase + mie * miePhase) / extinction;
    vec3 daylight = scattering * sunlight * 3.6;
    vec3 skyTint = clamp(dayUpSkyColor / max(GetLuminance(dayUpSkyColor), 0.001), vec3(0.2), vec3(2.5));
    daylight += skyTint * (0.035 + 0.035 * rainFactor) * (1.0 - transmission);
    vec3 twilightFill = mix(dayMiddleSkyColor, dayUpSkyColor, smoothstep(0.0, 0.6, elevation));
    daylight = mix(daylight, twilightFill, 0.35 * (1.0 - smoothstep(0.0, 0.35, sunVec.y)));
    daylight = mix(daylight, vec3(GetLuminance(daylight)) * vec3(0.91, 0.96, 1.05), rainFactor * 0.7);
    float horizon = pow2(1.0 - elevation);
    vec3 night = mix(nightUpSkyColor, nightMiddleSkyColor * 0.8, horizon);
    float daylightWeight = smoothstep(-0.18, 0.035, sunVec.y);
    vec3 sky = mix(night, daylight, daylightWeight);
    vec3 lowerSky = mix(nightDownSkyColor, daylight * 0.65, daylightWeight);
    sky = mix(sky, lowerSky, smoothstep(0.0, 0.5, -viewUp));
    if (ground) sky *= smoothstep(0.0, 1.0, clamp(1.0 + viewUp, 0.0, 1.0));
    return max(sky, vec3(0.0));
}

float AU_AtmosphereFogAmount(vec3 playerPos, float distanceToSurface, float eyeBrightness)
{
    float cameraHeight = CameraPosition.y - AU_ATM_FOG_ALTITUDE;
    float surfaceHeight = cameraHeight + playerPos.y;
    float cameraDensity = exp(-clamp(cameraHeight, -60.0, 480.0) / 120.0);
    float surfaceDensity = exp(-clamp(surfaceHeight, -60.0, 480.0) / 120.0);
    float heightDensity = sqrt(cameraDensity * surfaceDensity);
    float outdoor = smoothstep(0.02, 0.5, clamp(eyeBrightness, 0.0, 1.0));
    float density = max(AU_ATM_FOG_MULT, 0.0) * mix(0.0006, 0.0024, rainFactor);
    float opticalDepth = max(distanceToSurface, 0.0) * density * heightDensity * outdoor / max(AU_ATM_FOG_DISTANCE, 0.01);
    return 1.0 - exp(-opticalDepth);
}

#endif
