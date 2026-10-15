# Outdoor sunlight

Enable **Lighting → Outdoor sunlight** with **Ores & materials → Surface detail & shading** On.

Outdoor sunlight is enabled in Medium, High, Highest and Ultra profiles for both RTX and Non-RTX. Lowest and Low leave it Off.

| Category | Control | Effect |
|---|---|---|
| Lighting | Directional sky fill | More sky light on exposed faces, with softer fill on walls and undersides. |
| Lighting | Sun / sky colour balance | Balances warmer low sunlight and more neutral midday light against sky fill. |
| Lighting | Sunlit highlight protection | Keeps bright diffuse textures from washing out. |
| Lighting | Sunlit foliage detail | Refines wrapped lighting and backlit transmission on leaves and grass. |
| Lighting | Sunlight bounce intensity | Adjusts texture-coloured sunlight gathered by the existing RTX or path-traced GI rays. |
| Shadows | Sun-sized soft shadows | Sets penumbra width from blocker distance and shadow scale. Requires Contact-hardening shadows. |
| Shadows | Sun angular diameter (degrees) | Larger values give broader penumbrae. Shadow softness still applies. |
| Shadows | Contact shadow strength | Adjusts contact shadows without disabling terrain or cloud shadows. |

The controls reuse existing ray and shadow-sample budgets. They add no fullscreen passes or history buffers. Sunlight bounce does not enable GI in Non-RTX profiles.

Torch and ore emission, biome atmosphere and denoising remain separate controls.
