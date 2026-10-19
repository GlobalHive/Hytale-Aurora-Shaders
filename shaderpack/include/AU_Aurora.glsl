// Aurora Shaders for Hytale - aurora (night sky, optional).
// Complementary's aurora borealis, ported as is (Complementary Reimagined/Unbound,
// lib/atmospherics/auroraBorealis.glsl, under the Complementary License Agreement this pack is
// distributed with). AU_AURORA_STYLE 1 = Reimagined's blocky style, 2 = Unbound's smooth style.
// Hytale has no moon phase or snowy-biome signal for shaders, so instead of Complementary's
// "full moon in snowy places" condition it shows on every clear night. Sky pass only.
// Requires AU_Common (sunVisibility, rainFactor, pow1_5, sqrt1, clamp01) and SceneData
// (Time, CameraPosition).

#ifndef AU_AURORA_INC
#define AU_AURORA_INC

#ifndef AU_HAS_NOISETEX
#define AU_HAS_NOISETEX
uniform sampler2D noisetex;
#endif

vec3 AU_GetAurora(vec3 dir, float dither)
{
#if AU_AURORA
    float VdotU = dir.y;
    float visibility = sqrt1(clamp01(VdotU * 1.5 - 0.225)) - sunVisibility - rainFactor;
    visibility *= 1.0 - VdotU * 0.9;

    if (visibility > 0.0) {
        vec3 aurora = vec3(0.0);

        vec3 wpos = dir;
             wpos.xz /= wpos.y;
        vec2 cameraPositionM = mod(CameraPosition.xz, 8192.0) * 0.0075;
             cameraPositionM.x += Time * 0.04 * AU_AURORA_SPEED;

        int sampleCount = 25;
        int sampleCountP = sampleCount + 5;

        float ditherM = dither + 5.0;
        float auroraAnimate = Time * 0.001 * AU_AURORA_SPEED;
        for (int i = 0; i < sampleCount; i++) {
            float current = pow2((float(i) + ditherM) / float(sampleCountP));

            vec2 planePos = wpos.xz * (0.8 + current) * 11.0 + cameraPositionM;
            #if AU_AURORA_STYLE == 1
                planePos = floor(planePos) * 0.0007;

                float noise = textureLod(noisetex, planePos, 0.0).b;
                noise = pow2(pow2(pow2(pow2(1.0 - 2.0 * abs(noise - 0.5)))));

                noise *= pow1_5(textureLod(noisetex, planePos * 100.0 + auroraAnimate, 0.0).b);
            #else
                planePos *= 0.0007;

                float noise = textureLod(noisetex, planePos, 0.0).r;
                noise = pow2(pow2(pow2(pow2(1.0 - 2.0 * abs(noise - 0.5)))));

                noise *= textureLod(noisetex, planePos * 3.0 + auroraAnimate, 0.0).b;
                noise *= textureLod(noisetex, planePos * 5.0 - auroraAnimate, 0.0).b;
            #endif

            float currentM = 1.0 - current;
            aurora += noise * currentM * mix(vec3(7.0, 2.2, 12.0), vec3(6.0, 16.0, 12.0), pow2(pow2(currentM)));
        }

        #if AU_AURORA_STYLE == 1
            aurora *= 1.3;
        #else
            aurora *= 1.8;
        #endif

        return aurora * visibility / float(sampleCount) * AU_AURORA_STRENGTH;
    }
#endif
    return vec3(0.0);
}

#endif // AU_AURORA_INC
