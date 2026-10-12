#if AU_GENERATED_NORMALS
    // Generated normals: compare the texel's brightness
    // with neighbours a fraction of a texel away, so only texel edges bevel, with a dead zone
    // and a clamp. Tangent frame rebuilt from screen derivatives (Hytale has no tangents).
    if (hasSSAO && !hasBloom)
    {
        const float normalThreshold = 0.05;
        const float normalClamp = 0.2;
        vec2 atlasSize = vec2(textureSize(uTexture, 0).xy);
        vec2 tileSize = 32.0 / atlasSize;                       // Hytale block textures are 32 px
        vec2 originalOffsetR = tileSize / AU_GENERATED_NORMAL_RES;
        vec2 offsetR = max(originalOffsetR, fwidth(fragTexCoords));
        vec2 minCoord = floor(fragTexCoords / tileSize) * tileSize;
        vec2 maxCoord = minCoord + tileSize;
        float lOriginal = length(texColor.rgb);

        vec3 nts = vec3(0.0, 0.0, 1.0);
        vec2 oc; float d;
#define AU_GN_DIF(coord) (d = lOriginal - length(texture(uTexture, vec3(coord, fragAtlasPages.x)).rgb), d = d > 0.0 ? max(d - normalThreshold, 0.0) : min(d + normalThreshold, 0.0), clamp(d, -normalClamp, normalClamp))
        oc = fragTexCoords + vec2(0.0, offsetR.y); if (oc.y < maxCoord.y) nts.y += AU_GN_DIF(oc);
        oc = fragTexCoords + vec2(offsetR.x, 0.0); if (oc.x < maxCoord.x) nts.x += AU_GN_DIF(oc);
        oc = fragTexCoords - vec2(0.0, offsetR.y); if (oc.y > minCoord.y) nts.y -= AU_GN_DIF(oc);
        oc = fragTexCoords - vec2(offsetR.x, 0.0); if (oc.x > minCoord.x) nts.x -= AU_GN_DIF(oc);
#undef AU_GN_DIF
        nts.xy *= AU_GENERATED_NORMAL_MULT * 0.025 * sqrt(AU_GENERATED_NORMAL_RES / 128.0) * (originalOffsetR / offsetR);
        nts.xy = clamp(nts.xy, vec2(-1.0), vec2(1.0));

        if (nts.xy != vec2(0.0)) {
            vec4 ndcPos = vec4(gl_FragCoord.xy * InvViewportSize * 2.0 - 1.0, gl_FragCoord.z * 2.0 - 1.0, 1.0);
            vec4 wp = InvViewProjectionMatrix * ndcPos;
            vec3 pos = wp.xyz / wp.w;
            vec3 dp1 = dFdx(pos), dp2 = dFdy(pos);
            vec2 duv1 = dFdx(fragTexCoords), duv2 = dFdy(fragTexCoords);
            vec3 dp2perp = cross(dp2, normalWS);
            vec3 dp1perp = cross(normalWS, dp1);
            vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
            vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
            float tbLen = max(dot(T, T), dot(B, B));
            if (tbLen > 1e-20) {
                float invmax = inversesqrt(tbLen);
                vec3 bumped = normalize(T * invmax * nts.x + B * invmax * nts.y + normalWS * nts.z);
                if (dot(bumped, normalWS) > 0.2) normalWS = bumped;
            }
        }
    }
#endif
