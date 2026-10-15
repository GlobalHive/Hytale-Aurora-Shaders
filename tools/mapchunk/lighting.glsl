#if AU_FORWARD
    AU_InitCommon();
#if AU_VOXEL_WORLD
    if (shadingMode == SHADING_FULLBRIGHT) {
        ivec3 auEmitterTile = ivec3(ivec2(fragTexCoords * vec2(textureSize(uTexture, 0).xy) / 32.0), int(fragAtlasPages.x + 0.5));
        vec3 auNativeLight = AU_NativeEmission(auEmitterTile, fragTintColorFixed);
        vec3 auVisibleEmission = albedo * fragStaticLight.rgb;
        float auPeak = max(auVisibleEmission.r, max(auVisibleEmission.g, auVisibleEmission.b));
        float auMinimum = min(auVisibleEmission.r, min(auVisibleEmission.g, auVisibleEmission.b));
        if (max(auNativeLight.r, max(auNativeLight.g, auNativeLight.b)) < 0.001 && auMinimum < auPeak * 0.8)
            auNativeLight = vec3(1.0);
        AU_RecordEmitterAt(fragPositionWS + CameraPosition - normalWS * 0.04, auVisibleEmission, auMinimum < auPeak * 0.8);
    }
#endif
    float auDither = GetDither(gl_FragCoord.xy);
    vec3 auForwardVL = vec3(0.0);   // light shafts in front of water we composite ourselves
    bool auGlass = false;           // glass composited here (written opaque into the OIT pass)
    vec3 auHytaleLit = outColor0.rgb;   // Hytale's own lighting of this fragment
    int auSubsurface = 0;
#if AU_SUBSURFACE || AU_SSS
    if (!hasSSAO && !hasBloom) auSubsurface = max(abs(normalWS.x), max(abs(normalWS.y), abs(normalWS.z))) > 0.96 ? 2 : 1;
#endif
#if AU_SSS
#if ALPHA_TEST
    if (!hasBloom && shadingMode != SHADING_REFLECTIVE) {
        vec2 auMask = AU_FoliageMask(fragTexCoords, fragAtlasPages.x, vec2(textureSize(uTexture, 0).xy));
        if (auMask.y > 0.5) { auSubsurface = 2; auSSSMask = auMask.x; }
    }
#endif
    if (!hasBloom && shadingMode != SHADING_REFLECTIVE) {
        ivec3 materialTile = ivec3(ivec2(fragTexCoords * vec2(textureSize(uTexture, 0).xy) / 32.0), int(fragAtlasPages.x + 0.5));
        vec2 material = round(texelFetch(uHookMaterials, materialTile, 0).rg * 255.0);
        if (material.x == 1.0 && material.y == 6.0) auSubsurface = 3;
    }
#endif
    {
#if AU_SSS && AU_SSS_VOXEL == 1 && AU_VOXEL_WORLD
        if (auSubsurface >= 2 && !hasBloom && fragStaticLight.a > 0.02) {
            float auVolumeThickness;
            vec3 auScatteringDepth;
            vec4 auTransmission = AU_TraceSubsurface(fragPositionWS + CameraPosition - normalWS * 0.025, lightVec, albedo,
                                                    auSubsurface, auVolumeThickness, auScatteringDepth);
            auSSSVolumeValid = auTransmission.a > 0.5;
            if (auSSSVolumeValid) {
                auSSSVolumeTransmission = auTransmission.rgb * auSSSMask * SampleShadowWS(fragPositionWS + lightVec * AU_SSS_TRACE_DISTANCE, 1.0);
                auSSSVolumeLight = AU_SSSVolumeRadiance(auSSSVolumeTransmission, auScatteringDepth, normalWS, fragPositionWS, fragStaticLight.a, auSubsurface);
            }
        }
#endif
        vec3 auBlock = max(AU_ExtractBlockLight(fragStaticLight.rgb, fragStaticLight.a, normalWS, shadingMode != SHADING_REFLECTIVE), dynamicLight * 0.5);
        float auShadow = SampleShadowWS(fragPositionWS + normalWS * 0.35 + lightVec * 0.2, 1.0);
        outColor0.rgb = AU_DoLighting(albedo * AU_ALBEDO_MULT, normalWS, fragPositionWS, fragStaticLight.a, auBlock, auShadow,
                                      1.0, auSubsurface, hasBloom ? AU_EMISSION_STRENGTH : 0.0, 0.0, 1.0, auSubsurface == 1);
    }
#if ALPHA_BLEND
    // See-through emissive surfaces (the glowing panes Hytale puts in windows for the daylight
    // coming in, glowing fluids): Hytale's own colour, mapped through the inverse of our
    // tonemap so it comes out in that colour - our lighting boosted them to a warm white that
    // vanished into the bright view behind.
    if (hasBloom && !isWater(effectID))
        outColor0.rgb = AU_InverseTonemap(pow(clamp(auHytaleLit, 0.0, 0.88), vec3(2.2)));
#endif
#if ALPHA_BLEND && AU_GLASS
    // Glass and other see-through blocks composite themselves, like the water: the scene
    // behind (the refraction copy) dimmed and tinted by the glass, the lit glass texture over
    // it by its own alpha, and the sky it reflects, mostly at grazing angles. Blended by texture
    // alpha alone, a dim pane over our bright HDR outside vanished after the tonemap.
    if (!isWater(effectID) && !hasBloom && texColor.a < 0.99) {
        auGlass = true;
        vec3 auBehind = textureLod(uRefractionTexture, gl_FragCoord.xy * uCurrentInvViewportSize, 0.0).rgb;
        vec3 auTint = albedo / max(max(albedo.r, max(albedo.g, albedo.b)), 0.05);
        vec3 auThrough = auBehind * mix(vec3(1.0), auTint, AU_GLASS_TINT) * (1.0 - AU_GLASS_ABSORB);
        // The pane itself as Hytale lights it (open to the sky on one side, which our lighting
        // of a block in a wall misses), over the view through it.
        outColor0.rgb = mix(auThrough, auSubsurface == 3 ? outColor0.rgb : auHytaleLit, texColor.a);
        vec3 auV = fragPositionWS / max(length(fragPositionWS), 0.0001);
        vec3 auN = dot(normalWS, auV) > 0.0 ? -normalWS : normalWS;   // panes are seen from both sides
        float auF = pow(clamp(1.0 + dot(auN, auV), 0.0, 1.0), 5.0);
        vec3 auR = reflect(auV, auN);
        vec3 auSky = GetSky(auR.y, dot(auR, sunVec), auDither, true, true) * pow2(fragStaticLight.a);
        outColor0.rgb = mix(outColor0.rgb, auSky, (0.04 + 0.96 * auF) * AU_GLASS_REFLECT);
    }
#endif
#if AU_DEBUG_CLASS == 9 && ALPHA_BLEND
    // Debug: see-through blocks' texture alpha (red: emissive, blue: composited as glass).
    if (!isWater(effectID)) outColor0.rgb = vec3(hasBloom ? 1.0 : 0.0, texColor.a, auGlass ? 1.0 : 0.0);
#endif
#endif
