// A lava lake: every lava block is a light, and a spot beside it is reached by hundreds. Area
// lighting visits at most AU_LOCAL_LIGHT_BUDGET of the lights its cell lists, a random share each
// standing for the others: on average the same light as visiting every one (budget 1024), with
// per-pixel noise the temporal AA averages, at a fraction of the cost.
static GLuint lava_light_program(int budget)
{
    Str source = {0};
    char defines[96];
    snprintf(defines, sizeof defines, "#define AU_LOCAL_LIGHT_BUDGET %d\n", budget);
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_GI_DENOISE 1\n#define AU_LOCAL_LIGHT_CANDIDATES 8\n#define AU_LOCAL_LIGHT_RANGE 8.0\n"
        "#define AU_LOCAL_LIGHT_STRENGTH 1.0\n#define AU_EMISSIVE_LIGHT_STRENGTH 1.0\n#define AU_LOCAL_LIGHT_SOFTNESS 0.0\n"
        "#define AU_LIGHT_LIST_CACHED 1\n");
    str_cstr(&source, defines);
    str_cstr(&source, "uniform int uHookFrame,testCull,testTileRows;uniform vec3 CameraPosition,testPosition;"
        "out vec4 outColor;float pow2(float value){return value*value;}"
        "float max0(float value){return max(value,0.0);}float GetLuminance(vec3 value){return dot(value,vec3(.299,.587,.114));}\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){if(testCull!=0){AU_BuildLocalLightTile(testTileRows);discard;}"
        "vec2 randomValue=fract(vec2(dot(gl_FragCoord.xy,vec2(.618034,.569840)),dot(gl_FragCoord.xy,vec2(.438579,.754878)))+vec2(.37,.19));"
        "outColor=vec4(AU_LocalLightingOriginal(testPosition,vec3(0,0,1),false,randomValue),1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program);
    r_glUniform1i(r_glGetUniformLocation(program, "auLights"), VOX_UNIT_LIGHTS);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    r_glUniform1i(r_glGetUniformLocation(program, "uHookLocalLightTiles"), 2);
    r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), .5f, .5f, .5f);
    return program;
}

static void lava_light_budget_test(void)
{
    g_setVoxelColors = 0;
    vox_create(); assert(g_voxState == 1);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glBindImageTexture(VOX_UNIT_LIGHTS, g_voxLights, 0, 0, 0, GL_READ_WRITE, GL_R32UI);
    // 625 lava blocks under and around the camera, one light each (scattered over the light
    // slots the way hashing places them).
    enum { SIDE = 25, LIGHTS = SIDE * SIDE };
    static uint32_t row[1 + 2 * 1024];
    memset(row, 0, sizeof row);
    row[0] = 1024;
    for (int light = 0; light < LIGHTS; light++) {
        int x = light % SIDE - 12, z = light / SIDE - 12, slot = (light * 613) & 1023;
        row[1 + 2 * slot] = (uint32_t)(x & 255) | ((uint32_t)(-1 & 127) << 8) | ((uint32_t)(z & 255) << 15);
        row[2 + 2 * slot] = 255u | (110u << 8) | (20u << 16);
    }
    void (APIENTRY *update2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)GetProcAddress(g_real, "glTexSubImage2D");
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 1 + 2 * 1024, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, row);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    ov.BindVertexArray(ov.vao);
    PFN_glGenQueries generateQueries = (void *)real_wglGetProcAddress("glGenQueries");
    PFN_glQueryCounter timestamp = (void *)real_wglGetProcAddress("glQueryCounter");
    PFN_glGetQueryObjectui64v queryResult = (void *)real_wglGetProcAddress("glGetQueryObjectui64v");
    void (APIENTRY *deleteQueries)(GLsizei, const GLuint *) = (void *)real_wglGetProcAddress("glDeleteQueries");
    GLuint queries[2]; generateQueries(2, queries);
    static float pixels[64 * 64 * 4];
    double means[2], variances[2], milliseconds[2];
    const int budgets[2] = {1024, 32};
    for (int run = 0; run < 2; run++) {
        GLuint program = lava_light_program(budgets[run]);
        r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 1);
        r_glUniform1i(r_glGetUniformLocation(program, "testTileRows"), 2048);
        ov.Viewport(0, 0, 32, 2048);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 0);
        ov.Viewport(0, 0, 64, 64);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
        double sum = 0, squares = 0;
        for (int pixel = 0; pixel < 4096; pixel++) {
            for (int channel = 0; channel < 3; channel++) assert(isfinite(pixels[pixel * 4 + channel]));
            sum += pixels[pixel * 4]; squares += pixels[pixel * 4] * pixels[pixel * 4];
        }
        means[run] = sum / 4096; variances[run] = squares / 4096 - means[run] * means[run];
        // Cost: the same spot over a 1080p frame.
        r_glBindTexture(GL_TEXTURE_2D, output);
        ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1920, 1080, 0, GL_RGBA, GL_FLOAT, NULL);
        ov.Viewport(0, 0, 1920, 1080);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        timestamp(queries[0], GL_TIMESTAMP);
        for (int draw = 0; draw < 4; draw++) r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        timestamp(queries[1], GL_TIMESTAMP);
        uint64_t times[2];
        for (int query = 0; query < 2; query++) queryResult(queries[query], GL_QUERY_RESULT, &times[query]);
        milliseconds[run] = (times[1] - times[0]) / 4e6;
        ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
        r_glDeleteProgram(program);
        printf("Lava lights, budget %4d: mean %.5f variance %.8f, 1080p %.3f ms\n", budgets[run], means[run], variances[run], milliseconds[run]);
    }
    // Every light visited: the same value on every pixel. The budget: the same light on average,
    // noise per pixel, and much cheaper.
    assert(means[0] > .05 && variances[0] < 1e-8);
    assert(fabs(means[1] / means[0] - 1.0) < .02 && variances[1] > 1e-6);
    assert(milliseconds[1] < milliseconds[0] * .5);
    deleteQueries(2, queries);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); bindFramebuffer(0x8d40, 0);
    memset(row, 0, sizeof row);
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 1 + 2 * 1024, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, row);
    assert(!getError());
    puts("PASS: area lights by a lava lake: a budgeted random share of the cell's lights averages to visiting every one, at a fraction of the cost");
}
