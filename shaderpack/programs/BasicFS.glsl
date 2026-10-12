#version 410 core
// Aurora Shaders for Hytale - generic textured quads (also the sun and moon).

//-------------------------------------------------------------------------------------------------------------------------
//                                  Shader Params
//-------------------------------------------------------------------------------------------------------------------------
#if USE_TEXTURE_ARRAY
uniform sampler2DArray uTexture;
uniform int uPage = 0;
#else
uniform sampler2D uTexture;
#endif
uniform int uMipLevel = 0;

#if USE_COLOR_AND_OPACITY
uniform vec3 uColor;
uniform float uOpacity = 1.0;
#endif

//-------------------------------------------------------------------------------------------------------------------------
//                                  In
//-------------------------------------------------------------------------------------------------------------------------
in vec2 fragTexCoords;

//-------------------------------------------------------------------------------------------------------------------------
//                                  Out
//-------------------------------------------------------------------------------------------------------------------------
#if WRITE_ALPHA
layout (location = 0) out vec4 outColor;
#else
layout(location = 0) out vec3 outColor;
#endif

//-------------------------------------------------------------------------------------------------------------------------

void main()
{
#if USE_TEXTURE_ARRAY
    vec4 texel = textureLod(uTexture, vec3(fragTexCoords, float(uPage)), uMipLevel);
#else
    vec4 texel = textureLod(uTexture, fragTexCoords, uMipLevel);
#endif

#if USE_DISCARD
    if (texel.a == 0.0) discard;
#endif

#if USE_COLOR_AND_OPACITY
    vec4 color = vec4(texel.rgb * uColor.rgb, texel.a * uOpacity);
#else
    vec4 color = texel.rgba;
#endif

    outColor.rgb = color.rgb;
    // The sun and moon sprites sit at sky depth; make them HDR bright
    // so they stay brighter than the tonemapped sky.
    if (gl_FragCoord.z > 0.9995) outColor.rgb *= 4.0;

#if WRITE_ALPHA
    outColor.a = color.a;
#endif
}
