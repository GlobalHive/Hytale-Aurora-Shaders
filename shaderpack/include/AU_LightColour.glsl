#ifndef AU_LIGHT_COLOUR_INC
#define AU_LIGHT_COLOUR_INC

vec3 AU_BlendLocalLightColour(vec3 nativeLight, vec3 irradiance, float localEnergy)
{
    float nativePeak = max(nativeLight.r, max(nativeLight.g, nativeLight.b));
    float peak = max(irradiance.r, max(irradiance.g, irradiance.b));
    if (nativePeak <= 0.0001 || peak <= 0.0001) return nativeLight;
    float blend = smoothstep(0.015, 0.12, localEnergy) * AU_LOCAL_LIGHT_COLOR_BLEND;
    vec3 hue = sqrt(clamp(irradiance / peak, 0.0, 1.0));
    return mix(nativeLight, hue * nativePeak, blend);
}

vec3 AU_AddIndirectLight(vec3 surfaceColour, vec3 albedo, vec3 irradiance, bool linearTransport)
{
    if (!linearTransport) return surfaceColour + albedo * irradiance;
    vec3 linearColour = pow(max(surfaceColour, vec3(0.0)), vec3(2.2));
    linearColour += pow(max(albedo, vec3(0.0)), vec3(2.2)) * max(irradiance, vec3(0.0));
    return pow(linearColour, vec3(1.0 / 2.2));
}

#endif
