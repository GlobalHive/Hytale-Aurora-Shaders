// Coloured light fog (AU_Voxel.glsl AU_LightFog): a light colours the air around it in its own
// colour, nothing glows without a light, a wall between the camera and the light blocks it, and
// the culled light-cell path agrees with the candidate path.
static void light_fog_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_GI_DENOISE 1\n#define AU_LOCAL_LIGHT_CANDIDATES 8\n#define AU_LOCAL_LIGHT_RANGE 8.0\n"
        "#define AU_LOCAL_LIGHT_STRENGTH 1.0\n#define AU_EMISSIVE_LIGHT_STRENGTH 1.0\n#define AU_LOCAL_LIGHT_SOFTNESS 0.0\n"
        "#define AU_LIGHT_FOG 1\n#define AU_LIGHT_FOG_DENSITY 1.0\n#define AU_LIGHT_FOG_DISTANCE 24.0\n#define AU_LIGHT_FOG_SAMPLES 8\n"
        "uniform int uHookFrame,testCull,testTileRows;uniform vec3 CameraPosition,testView;"
        "out vec4 outColor;float pow2(float value){return value*value;}"
        "float max0(float value){return max(value,0.0);}float GetLuminance(vec3 value){return dot(value,vec3(.299,.587,.114));}\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){if(testCull!=0){AU_BuildLocalLightTile(testTileRows);discard;}"
        "vec2 noise=fract(vec2(dot(gl_FragCoord.xy,vec2(.618034,.569840)),dot(gl_FragCoord.xy,vec2(.438579,.754878)))+vec2(.37,.19));"
        "float dither=fract(dot(gl_FragCoord.xy,vec2(.7548776,.5698403)));"
        "outColor=vec4(AU_LightFog(testView,dither,noise),1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 64, 64);
    r_glUniform1i(r_glGetUniformLocation(program, "auLights"), VOX_UNIT_LIGHTS);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    r_glUniform1i(r_glGetUniformLocation(program, "testTileRows"), 64);
    g_setVoxelColors = 0;
    vox_create(); assert(g_voxState == 1);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glBindImageTexture(VOX_UNIT_LIGHTS, g_voxLights, 0, 0, 0, GL_READ_WRITE, GL_R32UI);
    void (APIENTRY *update2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)GetProcAddress(g_real, "glTexSubImage2D");
    void (APIENTRY *update3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)real_wglGetProcAddress("glTexSubImage3D");
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    float pixels[64 * 64 * 4];
    // Mean fog colour over the 64x64 noise patterns for a view ray.
    #define FOG(camera, view, tiles, result) do { \
        r_glUniform3f(r_glGetUniformLocation(program, "CameraPosition"), camera[0], camera[1], camera[2]); \
        r_glUniform3f(r_glGetUniformLocation(program, "testView"), view[0], view[1], view[2]); \
        r_glUniform1i(r_glGetUniformLocation(program, "uHookLocalLightTiles"), tiles); \
        if (tiles) { r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 1); ov.Viewport(0, 0, 32, 64); \
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT); \
            r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 0); ov.Viewport(0, 0, 64, 64); } \
        r_glMemoryBarrier(0xffffffffu); r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); \
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels); \
        for (int c = 0; c < 3; c++) result[c] = 0; \
        for (int p = 0; p < 4096; p++) for (int c = 0; c < 3; c++) { assert(isfinite(pixels[p * 4 + c]) && pixels[p * 4 + c] >= 0); result[c] += pixels[p * 4 + c] / 4096.0f; } \
    } while (0)
    // A red light in block (0, 0, 2); the camera 4 blocks in front of it looking past it.
    uint32_t light[3] = {1, 2u << 15, 255u};
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 3, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, light);
    const float camera[3] = {0.5f, 0.5f, -3.5f}, farCamera[3] = {0.5f, 0.5f, -7.0f}, toward[3] = {0.0f, 0.0f, 10.0f}, away[3] = {0.0f, 0.0f, -24.0f}, shortRay[3] = {0.0f, 0.0f, 3.2f};
    float lit[3], litTiled[3], awayFog[3], beforeWall[3], blocked[3], none[3];
    FOG(camera, toward, 0, lit);
    FOG(camera, toward, 2, litTiled);
    FOG(farCamera, away, 0, awayFog);
    FOG(camera, shortRay, 0, beforeWall);
    // A wall of solid blocks at z = 1 (x, y from -3 to 3) between the camera and the light.
    uint32_t solid = 2u;
    r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
    for (int x = -3; x <= 3; x++)
        for (int y = -3; y <= 3; y++)
            update3D(GL_TEXTURE_3D, 0, x & 255, y & 127, 1, 1, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &solid);
    FOG(camera, shortRay, 0, blocked);
    // No lights at all.
    uint32_t empty = 0;
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &empty);
    FOG(camera, toward, 0, none);
    #undef FOG
    printf("Light fog RGB toward %.5f %.5f %.5f, culled %.5f, away %.5f, before wall %.5f -> walled %.5f, no light %.5f\n",
        lit[0], lit[1], lit[2], litTiled[0], awayFog[0], beforeWall[0], blocked[0], none[0]);
    // The air glows in the light's own colour (red only) where the ray passes the light.
    assert(lit[0] > 0.001f && lit[1] < 1e-6f && lit[2] < 1e-6f);
    // The culled light cells give the same glow as the candidate list.
    assert(fabsf(litTiled[0] - lit[0]) < 0.02f * lit[0]);
    // Looking away along a ray that stays out of the light's reach: no glow.
    assert(awayFog[0] == 0.0f);
    // The wall hides the light from the air on the camera's side of it.
    assert(beforeWall[0] > 0.0005f && blocked[0] < beforeWall[0] * 0.05f);
    // No light, no coloured air.
    assert(none[0] == 0.0f && none[1] == 0.0f && none[2] == 0.0f);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(2, g_voxTex); deleteTextures(1, &g_voxLights); deleteTextures(1, &output);
    memset(g_voxTex, 0, sizeof(g_voxTex)); g_voxLights = 0; g_voxState = 0;
    assert(!getError()); r_glDeleteProgram(program);
    puts("PASS: coloured light fog in the light's own colour, culled cells match candidates, nothing away from lights or without them, walls block it");
}
