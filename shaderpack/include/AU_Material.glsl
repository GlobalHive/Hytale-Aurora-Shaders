// Aurora Shaders for Hytale - surface materials.
// What a G-buffer texel is made of and how it answers light: dry gloss and metal from the block
// material map (tools/material_rules.txt, matched to the atlas by the hook), plus rain. Rain is
// two things: a water film that settles on top of a surface (much less on walls, and little on
// what drinks it in) and soaking, which darkens what absorbs it - wood a little, soil and grass a
// lot, polished stone and metal not at all. Shared by the deferred lighting and the DLSS Ray
// Reconstruction guides, so RR denoises the same surface DeferredFS lit.
// Requires AU_Common.glsl (rainFactor) and AU_InitCommon() to have run.

#ifndef AU_MATERIAL
#define AU_MATERIAL

#include "AU_Surface.glsl"

#ifndef AU_HOOK_WETNESS
#define AU_HOOK_WETNESS
uniform float uHookWetness;   // rises with rain and lingers after it (set by the hook)
#endif

struct AU_MaterialData
{
    uint kind;          // 0 plain, 1 polished, 2 metal, 3 porous (0 for emissive and foliage)
    float level;        // 0-1: gloss, polish or porosity within the class
    float smoothness;   // dry gloss or the rain film, whichever is glossier
    float metal;        // 1 for metals
    float crystal;      // glowing ore crystals: gem gloss
    float film;         // water film from rain, 0-1
    float soak;         // rain soaked in, 0-1 (darkens the albedo)
};

// How readily a material soaks water in instead of keeping it on top.
float AU_MaterialPorosity(uint kind, float level)
{
    return kind == 3u ? 0.5 + 0.5 * level : kind == 0u ? 0.45 : kind == 1u ? 0.1 : 0.0;
}

// Rain reaching a surface: falling now, or the wetness lingering after it, where the sky is open.
float AU_RainExposure(float skyLight)
{
    return max(rainFactor, uHookWetness) * AU_RAIN_WETNESS * smoothstep(0.6, 0.9, skyLight);
}

AU_MaterialData AU_GetMaterial(float meta, vec3 rawAlbedo, vec3 normal, float skyLight, float colourFootprint,
                               bool emissive, bool foliage)
{
    AU_MaterialData m;
    uint bits = uint(round(meta * 255.0));
    uint storedKind = (bits >> 5) & 3u;
    m.kind = emissive || foliage ? 0u : storedKind;
    m.level = float((bits >> 2) & 7u) / 7.0;
    m.metal = 0.0;
    m.smoothness = 0.0;
    m.film = 0.0;
    m.soak = 0.0;
    // Ore crystals (Hytale's REFLECTIVE blocks) keep their glow level in the same bits, class 0.
    m.crystal = emissive || foliage || storedKind != 0u ? 0.0 : smoothstep(0.0, 0.4, m.level) * AU_ORE_GLOSS;
#if AU_MATERIALS
    float gloss = m.kind == 1u ? (m.level > 0.0 ? mix(0.12, 0.92, m.level) : 0.0)
                : m.kind == 2u ? mix(0.35, 0.85, m.level) : 0.0;
    m.metal = m.kind == 2u ? 1.0 : 0.0;
    m.smoothness = AU_TextureSmoothness(min(gloss * AU_MATERIAL_GLOSS, 0.95), m.kind, rawAlbedo, colourFootprint);
    if (!emissive && !foliage) {
        float rain = AU_RainExposure(skyLight);
        float porosity = AU_MaterialPorosity(m.kind, m.level);
        float up = clamp(normal.y, 0.0, 1.0);
        m.soak = clamp(rain * mix(0.5, 1.0, up) * porosity * AU_RAIN_SOAKING, 0.0, 1.0);
        m.film = clamp(rain * mix(0.3, 1.0, up) * (1.0 - 0.75 * porosity), 0.0, 1.0);
        m.smoothness = max(m.smoothness, m.film * 0.8);
    }
#endif
    return m;
}

// Soaked surfaces are darker (applied to the albedo in the lighting's colour space).
vec3 AU_MaterialSoakedAlbedo(vec3 albedo, AU_MaterialData m)
{
    return albedo * (1.0 - 0.4 * m.soak);
}

// The normal a water film reflects with: the film fills the texture relief in, so it lies flat
// on the block face (blocks are axis-aligned; anything clearly tilted keeps its normal).
vec3 AU_MaterialReflectionNormal(vec3 normal, AU_MaterialData m)
{
    vec3 an = abs(normal);
    float major = max(an.x, max(an.y, an.z));
    if (m.film <= 0.0 || major < 0.85) return normal;
    vec3 face = an.y >= max(an.x, an.z) ? vec3(0.0, sign(normal.y), 0.0)
              : (an.z >= an.x ? vec3(0.0, 0.0, sign(normal.z)) : vec3(sign(normal.x), 0.0, 0.0));
    return normalize(mix(normal, face, 0.75 * m.film));
}

#endif // AU_MATERIAL
