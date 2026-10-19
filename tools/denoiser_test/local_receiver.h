// Grazing light on inset block faces must not shadow against the receiving floor.
static void local_receiver_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_LOCAL_LIGHT_CANDIDATES 8\n#define AU_LOCAL_LIGHT_RANGE 8.0\n"
        "#define AU_LOCAL_LIGHT_STRENGTH 1.0\n#define AU_EMISSIVE_LIGHT_STRENGTH 1.0\n#define AU_LOCAL_LIGHT_SOFTNESS 0.0\n"
        "uniform int uHookFrame;uniform vec3 CameraPosition;uniform float testSide,testInset,testNormalError;out vec4 outColor;"
        "float pow2(float x){return x*x;}float max0(float x){return max(x,0.0);}\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){vec3 normal=normalize(vec3(testNormalError*.3*sin(gl_FragCoord.y),testNormalError*.2*cos(gl_FragCoord.x),testSide));"
        "vec3 position=vec3(gl_FragCoord.xy/vec2(16,32),testSide>0?1-testInset:testInset);"
        "vec3 light=vec3(6.5,1.5,testSide>0?1.2:-.2);"
        "float old=AU_LocalLightVisibility(position+normal*.04,light,ivec3(floor(light)));"
        "float fixedValue=AU_LocalLightVisibility(AU_LocalLightOrigin(position-CameraPosition,normal),light,ivec3(floor(light)));"
        "vec3 noisyDepthNormal=normalize(vec3(2*sin(gl_FragCoord.y),2*cos(gl_FragCoord.x),testSide));float depthValue=AU_LocalLightVisibility(AU_LocalLightOrigin(position-CameraPosition,noisyDepthNormal),light,ivec3(floor(light)));outColor=vec4(fixedValue,old,depthValue,length(AU_LocalLightOrigin(position-CameraPosition,normal)-(position+normal*.04)));}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao);
    g_setVoxelColors = 0; vox_create(); assert(g_voxState == 1);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    void (APIENTRY *update3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *)
        = (void *)real_wglGetProcAddress("glTexSubImage3D");
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    ov.Viewport(0, 0, 64, 64);
    float pixels[64 * 64 * 4];
    for (int sparse = 0; sparse < 2; sparse++)
    for (int side = -1; side <= 1; side += 2) {
        vox_clear();
        r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
        uint32_t floor[8 * 4];
        for (int i = 0; i < 32; i++) floor[i] = sparse && ((i % 8 + i / 8) % 2 == 0) ? 0u : 2u;
        update3D(GL_TEXTURE_3D, 0, 0, 0, 0, 8, 4, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, floor);
        r_glUniform1f(r_glGetUniformLocation(program, "testSide"), (float)side);
        for (int wall = 0; wall < 2; wall++) {
            if (wall) {
                uint32_t cells[4];
                for (int z = side > 0 ? 1 : -2; z <= (side > 0 ? 2 : -1); z++) {
                    uint32_t tag = ((uint32_t)(z >> 8) & 3u) << 12;
                    for (int i = 0; i < 4; i++) cells[i] = 2u | tag;
                    update3D(GL_TEXTURE_3D, 0, 5, 0, z & 255, 1, 4, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, cells);
                }
            }
            for (int sample = 0; sample < 12; sample++) {
                int inset = sample % 3;
                r_glUniform3f(r_glGetUniformLocation(program, "CameraPosition"), sample >= 6 ? -64.0f : 0.0f, 0.0f, 0.0f);
                r_glUniform1f(r_glGetUniformLocation(program, "testNormalError"), (sample % 6) >= 3 ? 1.0f : 0.0f);
                r_glUniform1f(r_glGetUniformLocation(program, "testInset"), .075f + inset * .025f);
                r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
                readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
                double oldMean = 0.0, noisyDepthMean = 0.0;
                for (int pixel = 0; pixel < 4096; pixel++) {
                    assert(fabsf(pixels[pixel * 4] - (wall ? 0.0f : 1.0f)) < .00001f);
                    oldMean += pixels[pixel * 4 + 1] / 4096.0;
                    noisyDepthMean += pixels[pixel * 4 + 2] / 4096.0;
                }
                if (!wall) { assert(oldMean < .9); assert(noisyDepthMean < .9); }
            }
        }
    }
    // Neighbours must not push known air, small/porous models or deep unknown
    // receivers to a voxel boundary: doing so would leak through real geometry.
    r_glUniform3f(r_glGetUniformLocation(program, "CameraPosition"), 0, 0, 0);
    r_glUniform1f(r_glGetUniformLocation(program, "testSide"), 1);
    r_glUniform1f(r_glGetUniformLocation(program, "testNormalError"), 0);
    for (int kind = 0; kind < 4; kind++) {
        uint32_t types[] = {1u, 6u, 3u, 0u};
        uint32_t cells[32];
        for (int i = 0; i < 32; i++) cells[i] = (i % 8 + i / 8) % 2 == 0 ? types[kind] : 2u;
        r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
        update3D(GL_TEXTURE_3D, 0, 0, 0, 0, 8, 4, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, cells);
        r_glUniform1f(r_glGetUniformLocation(program, "testInset"), kind == 3 ? .4f : .1f);
        r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
            if ((x / 16 + y / 32) % 2 == 0)
                assert(pixels[(y * 64 + x) * 4 + 3] < .00001f);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(2, g_voxTex); deleteTextures(1, &g_voxLights); deleteTextures(1, &output);
    memset(g_voxTex, 0, sizeof(g_voxTex)); g_voxLights = 0; g_voxState = 0;
    assert(!getError()); r_glDeleteProgram(program);
    puts("PASS: inset floor/ceiling dense/sparse receivers have no grazing grid; walls still shadow and air/small/porous/deep unknown receivers stay unchanged");
}
