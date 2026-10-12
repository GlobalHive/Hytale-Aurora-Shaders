#if AU_FORWARD
    AU_InitCommon();
    float auDither = GetDither(gl_FragCoord.xy);
    vec3 auForwardVL = vec3(0.0);   // light shafts in front of water we composite ourselves
    {
        vec3 auBlock = max(AU_ExtractBlockLight(fragStaticLight.rgb, fragStaticLight.a, normalWS, shadingMode != SHADING_REFLECTIVE), dynamicLight * 0.5);
        float auShadow = SampleShadowWS(fragPositionWS + normalWS * 0.35 + lightVec * 0.2, 1.0);
        outColor0.rgb = AU_DoLighting(albedo * AU_ALBEDO_MULT, normalWS, fragPositionWS, fragStaticLight.a, auBlock, auShadow,
                                      1.0, 0, hasBloom ? AU_EMISSION_STRENGTH : 0.0, 0.0, 1.0, false);
    }
#endif
