#version 410 core

// Aurora Shaders for Hytale - bloom composite.
// Pass the encoded blur through untouched; PostEffectFS decodes and mixes it.

uniform sampler2D uBloomTexture;
uniform float uBloomIntensity;
#if USE_SUNSHAFT
uniform sampler2D uSunshaftTexture;
uniform float uSunshaftIntensity;
#endif

in vec2 fragTexCoords;

out vec3 outColor;

void main(void)
{
    outColor = texture(uBloomTexture, fragTexCoords.xy).rgb;
}
