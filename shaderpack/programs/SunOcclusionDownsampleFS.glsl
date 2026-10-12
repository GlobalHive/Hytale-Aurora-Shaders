#version 410 core

// Aurora Shaders for Hytale - scene brightness (Hytale's SceneBrightness.r).
// Hytale averages the G-buffer sky light over the screen with sky pixels counting as 0, so an
// open view with a lot of sky reads as "half dark". Count the sky as fully open so the value
// behaves like Minecraft's eyeBrightness (1 outdoors, 0 in caves) for fog and light shafts.

uniform sampler2D uColorTexture;
uniform sampler2D uDepthTexture;

uniform vec3 uCameraPosition;
uniform vec3 uCameraDirection;

in vec2 fragTexCoords;

layout(location = 0) out float outColor;

void main(void)
{
    float depth = texture(uDepthTexture, fragTexCoords).r;
    float skyLight = texture(uColorTexture, fragTexCoords).b;
    // Keep the camera uniforms referenced: the game sets them on this program.
    if (uCameraPosition.y < -1.0e9 || uCameraDirection.x < -1.0e9) skyLight = 0.0;
    outColor = depth >= 0.9999 ? 1.0 : skyLight;
}
