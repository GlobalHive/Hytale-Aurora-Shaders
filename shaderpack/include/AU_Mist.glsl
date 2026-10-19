// Aurora Shaders for Hytale - ground mist (optional).
// Patches of mist anchored to the world that settle below an altitude and thin out above it -
// valleys, lakes and low ground - thickest at dawn and dusk and while the ground is wet after
// rain, burning off toward midday. A short march along the view ray samples the sun's shadow
// map, so morning light falls through the mist in beams and the mist glows around a low sun.
// Inside caves and rooms (little sky in view) it fades out. Blended like the atmospheric fog.
// Requires AU_Shadow (GetShadowCoord, uHookShadowMap) and AU_Sky (GetSky); call after
// AU_InitCommon().

#ifndef AU_MIST_INC
#define AU_MIST_INC

#ifndef AU_HAS_NOISETEX
#define AU_HAS_NOISETEX
uniform sampler2D noisetex;
#endif
#ifndef AU_HOOK_WETNESS
#define AU_HOOK_WETNESS
uniform float uHookWetness;
#endif

// How much mist there is now: dawn and dusk most, midday least, more while wet.
float AU_MistAmount()
{
    float lowSun = 1.0 - smoothstep(0.15, 0.6, abs(sunVec.y));
    float time = mix(AU_MIST_NOON, 1.0, lowSun);
    time = max(time, 0.6 * (1.0 - sunVisibility));   // a thinner mist at night
    return max(AU_MIST_DENSITY, 0.0) * time * (1.0 + 0.6 * clamp(uHookWetness, 0.0, 1.0)) * (1.0 - 0.7 * rainFactor);
}

float AU_MistDensityAt(vec3 worldPos, vec2 wind)
{
    float above = worldPos.y - AU_MIST_ALTITUDE;
    float height = exp(-max(above, 0.0) / max(AU_MIST_HEIGHT, 0.5));
    if (height < 0.01) return 0.0;
    vec2 xz = worldPos.xz * 0.004;
    float patches = texture(noisetex, xz + wind).g * 0.6 + texture(noisetex, xz * 3.3 - wind * 1.4 + worldPos.y * 0.004).b * 0.4;
    return height * smoothstep(0.42, 0.8, patches);
}

// rgb = the mist's colour, a = how much of what lies behind it the mist hides.
vec4 AU_GetMist(vec3 nPlayerPos, float lViewPos, float dither)
{
#if AU_MIST
    float amount = AU_MistAmount() * smoothstep(0.02, 0.4, GetEyeBrightness());
    if (amount <= 0.0 || IsCameraUnderwater > 0.5) return vec4(0.0);
    float rayEnd = min(lViewPos, AU_MIST_DISTANCE);
    // Skip rays that never come near the mist layer.
    float lowest = min(CameraPosition.y, CameraPosition.y + nPlayerPos.y * rayEnd);
    if (lowest > AU_MIST_ALTITUDE + 4.0 * AU_MIST_HEIGHT) return vec4(0.0);
    const int samples = AU_MIST_SAMPLES;
    float stepLength = rayEnd / float(samples);
    vec2 wind = vec2(Time * 0.0016, Time * 0.0007);
    float VdotL = dot(nPlayerPos, lightVec);
    // Mostly forward scattering (Henyey-Greenstein, g 0.6): the mist glows toward a low sun.
    float g = 0.6;
    float phase = 0.3 + 0.7 * (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * VdotL, 0.0001), 1.5) * 0.25;
    float transmittance = 1.0, lit = 0.0, total = 0.0;
    float lastShadow = 1.0;
    for (int i = 0; i < samples; i++) {
        float t = (float(i) + dither) * stepLength;
        vec3 position = nPlayerPos * t;
        // Thin right around the camera (you stand in a clear pocket and see the banks ahead).
        float density = AU_MistDensityAt(position + CameraPosition, wind) * amount * smoothstep(2.0, 24.0, t);
        if (density <= 0.0) continue;
        vec3 coord;
        float shadow = lastShadow;
        if (GetShadowCoord(position, coord)) { shadow = texture(uHookShadowMap, vec3(coord.xy, coord.z - 0.0002)); lastShadow = shadow; }
        float absorbed = transmittance * (1.0 - exp(-density * stepLength * 0.018));
        lit += absorbed * shadow;
        total += absorbed;
        transmittance *= exp(-density * stepLength * 0.018);
        if (transmittance < 0.01) break;
    }
    if (total <= 0.0) return vec4(0.0);
    float sunShare = lit / total;
    vec3 horizon = GetSky(0.04, dot(nPlayerPos, sunVec), 0.5, false, false);
    vec3 colour = horizon * (0.65 + 0.35 * sunShare) + lightColor * (sunShare * phase * shadowTime * 0.45);
    return vec4(colour, 1.0 - transmittance);
#else
    return vec4(0.0);
#endif
}

void AU_ApplyMist(inout vec3 color, vec3 playerPos, float dither)
{
#if AU_MIST
    float lViewPos = length(playerPos);
    vec4 mist = AU_GetMist(playerPos / max(lViewPos, 0.0001), lViewPos, dither);
    color = mix(color, mist.rgb, mist.a);
#endif
}

#endif // AU_MIST_INC
