// Aurora Shaders for Hytale - rainbows.
// Rainbows after rain (smooth style):
// a ring opposite the sun while the world is still wet but the rain has eased, morning and
// evening only. uHookWetness is Minecraft's wetness, computed by the hook.

#ifndef AU_RAINBOW
#define AU_RAINBOW

#ifndef AU_HOOK_WETNESS
#define AU_HOOK_WETNESS
uniform float uHookWetness;
#endif

// Gamma-space colour to add. lViewPos: distance to the surface (ignored for the sky).
vec3 AU_GetRainbow(vec3 nPlayerPos, float lViewPos, bool sky, float dither)
{
    vec3 rainbow = vec3(0.0);
#if AU_RAINBOWS
    float rainbowTime = min1(max0(SdotU - 0.1) / 0.15);
    rainbowTime = clamp(rainbowTime - pow2(pow2(pow2(noonFactor))) * 8.0, 0.0, 0.85);
    // Shown with wetness above 0.333. Hytale's weather fades out slowly, so the threshold is
    // lower to keep the rainbow up a while after the rain has visibly stopped.
    rainbowTime *= sqrt2(min1(max0(uHookWetness - 0.1) * 1.5)) * invRainFactor;
    if (IsCameraUnderwater > 0.5) rainbowTime *= sqrt1(max0(nPlayerPos.y));

    if (rainbowTime > 0.001) {
        const float rainbowLength = 300.0;
        float l = sky ? rainbowLength : lViewPos;
        float VdotL = dot(nPlayerPos, sunVec);

        float rainbowCoord = clamp01(1.0 - (VdotL + 0.75) / (0.0625 * AU_RAINBOW_DIAMETER));
        float rainbowFactor = rainbowCoord * (1.0 - rainbowCoord);
        rainbowFactor = pow2(pow2(rainbowFactor * 3.7));
        rainbowFactor *= pow2(min1(l / rainbowLength));
        rainbowFactor *= rainbowTime * (1.0 - GetCaveFactor());

        if (rainbowFactor > 0.0) {
            float rainbowCoordM = pow(rainbowCoord, 1.4 + max(rainbowCoord - 0.5, 0.0) * 1.6);
            rainbowCoordM = smoothstep(0.0, 1.0, rainbowCoordM) * 0.85;
            rainbowCoordM += (dither - 0.5) * 0.1;

            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55, 4.3, 2.2), 6.0) - 3.0) - 1.0, 0.0, 1.0);
            rainbowCoordM += 0.1;
            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55, 4.3, 2.2), 6.0) - 3.0) - 1.0, 0.0, 1.0);
            rainbowCoordM -= 0.2;
            rainbow += clamp(abs(mod(rainbowCoordM * 6.0 + vec3(-0.55, 4.3, 2.2), 6.0) - 3.0) - 1.0, 0.0, 1.0);
            rainbow /= 3.0;

            rainbow.r += pow2(max(rainbowCoord - 0.5, 0.0)) * (max(1.0 - rainbowCoord, 0.0)) * 26.0;
            rainbow = pow(rainbow, vec3(2.2)) * vec3(0.25, 0.075, 0.25) * 3.0;
            rainbow *= rainbowFactor;
        }
    }
#endif
    return rainbow;
}

#endif // AU_RAINBOW
