#if AU_SSS && AU_SSS_VOXEL == 2 && ALPHA_TEST
    if (auSSSShadowPass != 0 && AU_SSS_SHADOW_DITHER > 0.0) {
        vec2 auAtlasSize = vec2(textureSize(uTexture, 0).xy);
        vec2 auMask = AU_FoliageMask(fragTexCoords, fragAtlasPage, auAtlasSize);
        if (auMask.y > 0.5 && AU_DiscardFoliageShadow(fract(fragTexCoords * auAtlasSize / 32.0), auSSSShadowSeed, auMask.x)) discard;
    }
#endif
