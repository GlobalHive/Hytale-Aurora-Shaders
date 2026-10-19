static void water_absorption_test(void)
{
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    float results[2][3][8][4];
    for (int clarity = 0; clarity < 2; clarity++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, clarity ? "\n#undef AU_WATER_CLARITY\n#define AU_WATER_CLARITY 2.0\n"
                                  : "\n#undef AU_WATER_CLARITY\n#define AU_WATER_CLARITY 1.0\n");
        str_cstr(&source, "#undef AU_WATER_FOG_MULT\n#define AU_WATER_FOG_MULT 1.0\nuniform vec3 testTint;uniform float testDistance;uniform vec3 testLight;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Water.glsl\"\n", 0);
        str_cstr(&source, "void main(){vec3 a=AU_WaterAbsorption(testTint);"
            "outColor=vec4(AU_WaterTransmittance(a,testDistance),AU_WaterBedPath(testDistance,testLight));}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
        const float tints[3][3] = {{.2f, .45f, .9f}, {.25f, .6f, .35f}, {1, 1, 1}};   // blue lake, green swamp, neutral
        const float distances[8] = {0, .25f, 1, 2, 4, 8, 32, 600};
        for (int tint = 0; tint < 3; tint++)
        for (int sample = 0; sample < 8; sample++) {
            r_glUniform3f(r_glGetUniformLocation(program, "testTint"), tints[tint][0], tints[tint][1], tints[tint][2]);
            r_glUniform1f(r_glGetUniformLocation(program, "testDistance"), distances[sample]);
            r_glUniform3f(r_glGetUniformLocation(program, "testLight"), 0, sample == 3 ? .1f : 1, 0);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, results[clarity][tint][sample]);
            for (int channel = 0; channel < 4; channel++) assert(isfinite(results[clarity][tint][sample][channel]));
        }
        r_glDeleteProgram(program);
    }
    for (int clarity = 0; clarity < 2; clarity++)
    for (int tint = 0; tint < 3; tint++) {
        float (*t)[4] = results[clarity][tint];
        // Nothing is absorbed at the surface; transmittance falls steadily with distance, and
        // hundreds of blocks leave only the water's own scattered colour.
        for (int channel = 0; channel < 3; channel++) {
            assert(fabsf(t[0][channel] - 1) < 1e-5f && t[7][channel] < 1e-3f);
            for (int sample = 1; sample < 8; sample++) assert(t[sample][channel] <= t[sample - 1][channel] + 1e-6f);
        }
        // A quarter block of water stays clear; red is absorbed first.
        for (int channel = 0; channel < 3; channel++) assert(t[1][channel] > .85f);
        assert(t[4][0] < t[4][1] && t[4][0] < t[4][2]);
        // Light reaching the bed: steep sun = about the depth; low sun = a longer path.
        assert(t[2][3] >= 1.0f && t[2][3] < 1.3f && t[3][3] > 2.0f * 2.0f);
    }
    // The tint survives: the blue lake keeps blue longest, the swamp green; clearer water
    // carries light further.
    assert(results[0][0][5][2] > results[0][0][5][1] && results[0][1][5][1] > results[0][1][5][2]);
    for (int channel = 0; channel < 3; channel++) assert(results[1][0][4][channel] > results[0][0][4][channel]);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); assert(!getError());
    puts("PASS: enhanced water absorbs red first, keeps shallow water clear and the native tint longest, fades monotonically, longer low-sun bed paths and clarity control");
}
