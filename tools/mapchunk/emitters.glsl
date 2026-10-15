#if AU_VOXEL_WORLD && DEFERRED && !ALPHA_BLEND && !USE_SCENE_DATA_OVERRIDE
    vec3 auEmitterRadiance = auSelfEmission;
    float auEmitterPeak = max(albedo.r, max(albedo.g, albedo.b));
    vec3 auEmitterTint = albedo / max(auEmitterPeak, 0.001);
    if (hasBloom && max(auEmitterRadiance.r, max(auEmitterRadiance.g, auEmitterRadiance.b)) < 0.001
        && min(albedo.r, min(albedo.g, albedo.b)) < auEmitterPeak * 0.8)
        auEmitterRadiance = vec3(1.0);
    auEmitterRadiance *= auEmitterTint;
    if (auOreGlow > 0.0)
        auEmitterRadiance = max(auEmitterRadiance, auEmitterTint * AU_ORE_TORCH_RATIO * (AU_ORE_LIGHT / 8.0));
    bool auFineEmitter = false;
    if (hasBloom) {
        float auNativePeak = max(auSelfEmission.r, max(auSelfEmission.g, auSelfEmission.b));
        vec3 auVisibleEmission = albedo * (shadingMode == SHADING_FULLBRIGHT ? fragStaticLight.rgb : vec3(auNativePeak));
        float auVisiblePeak = max(auVisibleEmission.r, max(auVisibleEmission.g, auVisibleEmission.b));
        if (auVisiblePeak > 0.001) {
            auEmitterRadiance = auVisibleEmission;
            auFineEmitter = min(auVisibleEmission.r, min(auVisibleEmission.g, auVisibleEmission.b)) < auVisiblePeak * 0.8;
        }
    }
    AU_RecordEmitterAt(auEmitterPositionWS + CameraPosition - normalWS * 0.04, auEmitterRadiance, auFineEmitter);
#endif
