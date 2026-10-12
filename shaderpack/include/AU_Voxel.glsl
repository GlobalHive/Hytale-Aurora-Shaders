// Aurora Shaders for Hytale - voxel world for the path tracer.
//
// A 256x128x256 block grid around the player, kept by the hook (opengl32.dll) in two 3D
// images that persist across frames: auVoxGeo (what is in each block) and auVoxCol (its
// colour). The grid wraps around (block b lives in cell b & mask), so walking never needs a
// copy; every cell carries a tag of which wrap it was written for, and a cell whose tag does
// not match the block asked for reads as unknown.
//
// Writers: MapChunkVS (every block the game draws: solid / porous / partial) and DeferredFS
// (colour of what is on screen, air in front of visible block faces - this is what removes
// broken blocks). The program file needs "#extension GL_ARB_shader_image_load_store : enable"
// before anything else.
#ifndef AU_VOXEL_INC
#define AU_VOXEL_INC

#define AU_VOX_SIZE  ivec3(256, 128, 256)
#define AU_VOX_MASK  ivec3(255, 127, 255)
#define AU_VOX_SHIFT ivec3(8, 7, 8)
#define AU_VOX_HALF  ivec3(128, 64, 128)

// auVoxGeo cell: bits 0-1 type, bit 2 small model (torch, rock: no vertex on a cell corner),
// bit 3 emissive, bits 8-13 wrap tag.
#define AU_VOX_UNKNOWN 0u
#define AU_VOX_AIR     1u
#define AU_VOX_SOLID   2u
#define AU_VOX_POROUS  3u   // leaves: light gets through the gaps
#define AU_VOX_SMALL   4u
#define AU_VOX_EMISSIVE 8u

layout(r32ui) uniform uimage3D auVoxGeo;
layout(rgba8) uniform image3D auVoxCol;

uint AU_VoxTag(ivec3 b)
{
    ivec3 k = (b >> AU_VOX_SHIFT) & 3;
    return uint(k.x | (k.y << 2) | (k.z << 4));
}

// Blocks the grid can hold right now: the window centred on the camera's block.
bool AU_VoxInWindow(ivec3 b, ivec3 camBlock)
{
    ivec3 d = b - camBlock + AU_VOX_HALF;
    return all(greaterThanEqual(d, ivec3(0))) && all(lessThan(d, AU_VOX_SIZE));
}

// Raw cell of block b, or 0 (unknown) when the cell belongs to another wrap.
uint AU_VoxGet(ivec3 b)
{
    uint v = imageLoad(auVoxGeo, b & AU_VOX_MASK).r;
    return ((v >> 8) & 63u) == AU_VoxTag(b) ? v : 0u;
}

// Albedo of block b; a = 0 when it was never seen on screen.
vec4 AU_VoxGetColor(ivec3 b)
{
    vec4 c = imageLoad(auVoxCol, b & AU_VOX_MASK);
    uint tag = uint(c.a * 255.0 + 0.5);
    return (tag & 64u) != 0u && (tag & 63u) == AU_VoxTag(b) ? vec4(c.rgb, 1.0) : vec4(0.0);
}

#ifndef AU_VOX_WRITER_ONLY

float AU_VoxHash(ivec3 b, float salt)
{
    return fract(sin(dot(vec3(b), vec3(12.9898, 78.233, 37.719)) + salt) * 43758.5453);
}

// Amanatides-Woo walk through the grid from `origin` (world space) along `dir`. Solid blocks
// stop the ray; leaves and partial blocks stop it only part of the time (`rnd` 0-1 decides,
// per block), which averages to light getting through. The start block is never tested.
// Unknown blocks count as air. Returns false when the ray leaves the grid or `maxDist`.
bool AU_VoxTrace(vec3 origin, vec3 dir, float maxDist, int maxSteps, float rnd,
                 out ivec3 hitBlock, out vec3 hitNormal, out float hitT, out uint hitCell)
{
    ivec3 camBlock = ivec3(floor(CameraPosition));
    ivec3 b = ivec3(floor(origin));
    dir = mix(dir, vec3(1e-6), lessThan(abs(dir), vec3(1e-6)));
    ivec3 stepDir = ivec3(sign(dir));
    vec3 tDelta = abs(1.0 / dir);
    vec3 tMax = (vec3(b) + max(vec3(stepDir), 0.0) - origin) / dir;
    hitBlock = b;
    hitNormal = vec3(0.0);
    hitT = 0.0;
    hitCell = 0u;
    for (int i = 0; i < maxSteps; i++) {
        // One axis per step, also on exact ties: stepping two at once slips diagonally
        // between blocks that only touch at an edge.
        vec3 mf = tMax.x < tMax.y ? (tMax.x < tMax.z ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0))
                                  : (tMax.y < tMax.z ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0));
        float t = dot(tMax, mf);
        if (t > maxDist) return false;
        b += ivec3(mf) * stepDir;
        tMax += mf * tDelta;
        if (!AU_VoxInWindow(b, camBlock)) return false;
        uint v = AU_VoxGet(b);
        uint type = v & 3u;
        if (type < AU_VOX_SOLID) continue;
        float pass = type == AU_VOX_POROUS ? 0.45 : ((v & AU_VOX_SMALL) != 0u ? 0.5 : 0.0);
        if (pass > 0.0 && fract(rnd + AU_VoxHash(b, 0.0)) < pass) continue;
        hitBlock = b;
        hitNormal = -mf * vec3(stepDir);
        hitT = t;
        hitCell = v;
        return true;
    }
    return false;
}

// DeferredFS: teaches the grid what is on screen. A tenth of the pixels per frame (`rnd`)
// blend their albedo slowly into the block behind them (a steady average of its texture), and clear a full block standing right in
// front of a visible block face - a face can only be seen when nothing solid is there, so
// that block was broken.
void AU_VoxFeed(vec3 playerPos, vec3 normal, vec3 albedo, bool foliage, bool emissive, float rnd)
{
    if (rnd > 0.1 || dot(playerPos, playerPos) > 128.0 * 128.0) return;
    ivec3 camBlock = ivec3(floor(CameraPosition));
    vec3 wp = playerPos + CameraPosition;
    // Only pixels well inside a face: at its edges the position from the depth buffer can
    // slip into the neighbouring block, which would then be coloured or cleared by mistake.
    vec3 an = abs(normal);
    vec3 axis = step(vec3(0.99), an);
    vec3 edge = abs(wp - round(wp));
    if (max(an.x, max(an.y, an.z)) > 0.99 && dot(step(edge, vec3(0.08)), 1.0 - axis) > 0.0) return;

    ivec3 inside = ivec3(floor(wp - normal * 0.08));
    uint insideCell = AU_VoxGet(inside);
    // Plants are not in the grid; their colour goes to the ground they grow on (grass
    // covers most of the ground's own pixels).
    bool plant = (insideCell & 3u) < AU_VOX_SOLID;
    if (plant) inside = ivec3(floor(wp)) - ivec3(0, 1, 0);
    // Flames and glowing pixels are not the block's colour (and animate).
    if (!emissive && AU_VoxInWindow(inside, camBlock) && (AU_VoxGet(inside) & 3u) >= AU_VOX_SOLID) {
        ivec3 c = inside & AU_VOX_MASK;
        uint tag = AU_VoxTag(inside);
        vec4 old = imageLoad(auVoxCol, c);
        uint oldTag = uint(old.a * 255.0 + 0.5);
        vec3 col = (oldTag & 64u) != 0u && (oldTag & 63u) == tag ? mix(old.rgb, albedo, 0.05) : albedo;
        imageStore(auVoxCol, c, vec4(col, float(64u | tag) / 255.0));
    }

    // Only from the face of a full block the grid knows: a grass blade standing against a
    // hill step would otherwise erase the step every frame (and the chunk pass re-add it).
    if (!plant && !foliage && (insideCell & 7u) == AU_VOX_SOLID && max(an.x, max(an.y, an.z)) > 0.99) {
        float offBoundary = dot(edge, axis);
        if (offBoundary < 0.04) {
            ivec3 front = ivec3(floor(wp + normal * 0.5));
            uint fv = AU_VoxGet(front);
            if (AU_VoxInWindow(front, camBlock) && (fv & 3u) == AU_VOX_SOLID && (fv & AU_VOX_SMALL) == 0u)
                imageStore(auVoxGeo, front & AU_VOX_MASK, uvec4(AU_VOX_AIR | (AU_VoxTag(front) << 8)));
        }
    }
}

#ifdef AU_RTX
// Sun light leaving a block face the camera can't see: the block's colour (grey until it
// has been on screen) lit by the sun when a shadow ray through the grid reaches the sky.
vec3 AU_VoxHitLight(ivec3 hb, vec3 hn, uint hv, vec3 hitWorld, float rnd)
{
    float NdotL = (hv & 3u) == AU_VOX_POROUS ? 0.6 : dot(hn, lightVec);
    if (NdotL <= 0.0) return vec3(0.0);
    vec4 c = AU_VoxGetColor(hb);
    vec3 alb = c.a > 0.5 ? c.rgb : vec3(0.3);
    ivec3 sb; vec3 sn; float st; uint sv;
    if (AU_VoxTrace(hitWorld + hn * 0.02, lightVec, 64.0, 64, rnd, sb, sn, st, sv)) return vec3(0.0);
    return alb * lightColor * NdotL;
}

// Path-traced diffuse bounce + ambient occlusion through the voxel grid; same outputs as
// AU_GetSSGI (AU_RTX.glsl), which it replaces. Rays are stratified over the cosine
// hemisphere with blue noise. A hit on a surface that is on screen takes that surface's
// light from the G-buffer (exact shadows, texture colour); hits behind objects or off screen
// are lit from the grid.
vec3 AU_GetVoxelGI(sampler2D depthTex, vec3 playerPos, vec3 normal, out float occlusion)
{
    const int rays = AU_PT_RAYS;
    const float reach = AU_PT_REACH;
    vec3 origin = playerPos + CameraPosition + normal * 0.02;
    vec2 pn = GetDither2(gl_FragCoord.xy);
    vec3 gi = vec3(0.0);
    float occluded = 0.0;
    for (int r = 0; r < rays; r++) {
        float u1 = (float(r) + pn.x) / float(rays);
        float u2 = fract(float(r) * 0.618034 + pn.y);
        vec3 dir = AU_HemisphereDir(normal, u1, u2);
        float rnd = fract(pn.x * 13.73 + pn.y * 7.31 + float(r) * 0.381966);
        ivec3 hb; vec3 hn; float ht; uint hv;
        if (!AU_VoxTrace(origin, dir, reach, 96, rnd, hb, hn, ht, hv)) continue;
        occluded += max(0.0, 1.0 - ht / AU_RTX_AO_RADIUS);
        vec3 hitRel = origin - CameraPosition + dir * ht;
        vec2 uv = AU_ProjectToScreen(hitRel);
        float slin = all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0))) ? AU_SceneLinAt(depthTex, uv) : -1.0;
        float lin = AU_LinOf(hitRel);
        vec3 light = slin > AU_NEAR_IGNORE && abs(slin - lin) < 0.35 + lin * 0.01
                   ? AU_GIHitLight(uv)
                   : AU_VoxHitLight(hb, hn, hv, hitRel + CameraPosition, rnd);
        gi += min(light, vec3(1.5)) * (0.5 + 0.5 * (1.0 - ht / reach));
    }
    occlusion = 1.0 - occluded / float(rays);
    return gi / float(rays);
}
#endif // AU_RTX

#endif // !AU_VOX_WRITER_ONLY

#endif // AU_VOXEL_INC
