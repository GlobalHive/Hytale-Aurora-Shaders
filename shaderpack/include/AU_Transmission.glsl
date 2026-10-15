#ifndef AU_TRANSMISSION_INC
#define AU_TRANSMISSION_INC

#if AU_SSS && AU_SSS_VOXEL
vec4 AU_TraceSubsurface(vec3 origin, vec3 direction, vec3 tint, int medium, out float thickness, out vec3 scatteringDepth)
{
    thickness = 0.0;
    scatteringDepth = vec3(0.0);
    ivec3 cameraBlock = ivec3(floor(CameraPosition));
    ivec3 block = ivec3(floor(origin));
    vec3 rayDirection = mix(direction, vec3(0.000001), lessThan(abs(direction), vec3(0.000001)));
    ivec3 stepDirection = ivec3(sign(rayDirection));
    vec3 stepDistance = abs(1.0 / rayDirection);
    vec3 boundary = (vec3(block) + max(vec3(stepDirection), 0.0) - origin) / rayDirection;
    vec3 transport = vec3(1.0);
    vec3 receiverTint = clamp(tint / max(max(tint.r, max(tint.g, tint.b)), 0.001), 0.03, 1.0);
    float travelled = 0.0;
    bool foundMedium = false;
    for (int stepIndex = 0; stepIndex < AU_SSS_TRACE_STEPS; stepIndex++) {
        if (!AU_VoxInWindow(block, cameraBlock)) return vec4(0.0);
        uint cell = AU_VoxGet(block);
        uint cellType = cell & 3u;
        bool foliage = cellType == AU_VOX_POROUS;
        bool ice = cellType == AU_VOX_SOLID && (cell & AU_VOX_ICE) != 0u;
        bool insideMedium = foliage || ice;
        if (cellType == AU_VOX_SOLID && !ice) return vec4(0.0, 0.0, 0.0, foundMedium ? 1.0 : 0.0);
        vec3 axis = boundary.x < boundary.y ? (boundary.x < boundary.z ? vec3(1, 0, 0) : vec3(0, 0, 1))
                                         : (boundary.y < boundary.z ? vec3(0, 1, 0) : vec3(0, 0, 1));
        float segmentEnd = min(dot(boundary, axis), AU_SSS_TRACE_DISTANCE);
        float segmentLength = max(segmentEnd - travelled, 0.0);
        if (insideMedium) {
            foundMedium = true;
            vec3 materialTint = (medium == 3) == ice ? receiverTint : ice ? vec3(0.55, 0.8, 1.0) : vec3(0.35, 1.0, 0.2);
            vec3 extinction = (vec3(0.2) - log(materialTint) * AU_SSS_ABSORPTION)
                * (AU_SSS_DENSITY * AU_SSS_THICKNESS * (ice ? 0.65 : 1.0));
            vec3 opticalDepth = extinction * segmentLength;
            transport *= exp(-opticalDepth);
            scatteringDepth += opticalDepth * materialTint * 0.7;
            thickness += segmentLength;
        }
        if (segmentEnd >= AU_SSS_TRACE_DISTANCE || max(transport.r, max(transport.g, transport.b)) < 0.001)
            return vec4(transport, foundMedium ? 1.0 : 0.0);
        travelled = segmentEnd;
        block += ivec3(axis) * stepDirection;
        boundary += axis * stepDistance;
    }
    return vec4(0.0);
}
#endif

#endif
