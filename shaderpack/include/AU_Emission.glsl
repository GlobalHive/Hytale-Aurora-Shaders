#ifndef AU_EMISSION_INC
#define AU_EMISSION_INC
uniform sampler2DArray uHookEmission;
uniform int uHookEmissionReady;
vec3 AU_NativeEmission(ivec3 tile, vec3 tint)
{
#if AU_NATIVE_EMISSION
    if (uHookEmissionReady == 0) return vec3(0.0);
    for (int variant = 0; variant < 16; variant++) {
        ivec3 address = ivec3(tile.xy, tile.z * 32 + variant * 2);
        vec4 emission = texelFetch(uHookEmission, address, 0);
        if (emission.a < 0.5) break;
        vec4 expectedTint = texelFetch(uHookEmission, address + ivec3(0, 0, 1), 0);
        vec3 difference = abs(tint - expectedTint.rgb);
        if (expectedTint.a < 0.5 || max(difference.r, max(difference.g, difference.b)) < 0.08)
            return emission.rgb * AU_NATIVE_EMISSION_STRENGTH;
    }
#endif
    return vec3(0.0);
}
#endif
