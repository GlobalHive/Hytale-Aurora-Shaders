# Surface detail & shading

**Ores & materials → Surface detail & shading** is the master switch. It is On by default.

| Category | Control | Effect |
|---|---|---|
| Ores & materials | Texture definition | Adds local texture contrast using Block relief's existing samples. Requires Block relief. |
| Ores & materials | Texture roughness detail | Varies roughness on polished and metal materials. |
| Ores & materials | Specular antialiasing | Softens unresolved highlights to reduce shimmer. |
| Lighting | Directional ambient depth | Separates upward faces, walls and undersides. |
| Lighting | Matte foliage highlights | Reduces plastic-looking highlights on grass and leaves. |
| Lighting | Outdoor sunlight | Controls the [sun and sky lighting](outdoor-sunlight.md). |

Dry wood, ordinary stone and soil do not gain reflections from these controls. Texture detail fades with distance. The effects reuse existing samples and do not add a denoising pass.

[Biome & weather atmosphere](biome-atmosphere.md) is a separate setting and defaults to Off.
