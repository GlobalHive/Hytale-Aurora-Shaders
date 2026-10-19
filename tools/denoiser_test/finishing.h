// Highlight colour protection, white balance and adaptive exposure: brightness-neutral white
// balance, adaptation only where little sky is in view, glowing blocks keeping their exact
// displayed colour through both, and protected highlights keeping more colour.
static void finishing_test(void)
{
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    // 0: neutral; 1: warm white balance + green tint + adaptive exposure; 2: highlight protection
    // (a forward-only grade: it deliberately keeps bright colours more saturated, glowing blocks too).
    float results[3][8][4];
    for (int graded = 0; graded < 3; graded++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, graded == 2
            ? "\n#undef AU_WB_TEMPERATURE\n#define AU_WB_TEMPERATURE 0.0\n#undef AU_WB_TINT\n#define AU_WB_TINT 0.0\n"
              "#undef AU_ADAPTIVE_EXPOSURE\n#define AU_ADAPTIVE_EXPOSURE 0\n#undef AU_TM_HIGHLIGHT_PROTECT\n#define AU_TM_HIGHLIGHT_PROTECT 1.0\n"
            : graded
            ? "\n#undef AU_WB_TEMPERATURE\n#define AU_WB_TEMPERATURE 0.6\n#undef AU_WB_TINT\n#define AU_WB_TINT -0.4\n"
              "#undef AU_ADAPTIVE_EXPOSURE\n#define AU_ADAPTIVE_EXPOSURE 1\n#undef AU_ADAPT_STRENGTH\n#define AU_ADAPT_STRENGTH 0.8\n"
              "#undef AU_TM_HIGHLIGHT_PROTECT\n#define AU_TM_HIGHLIGHT_PROTECT 0.0\n"
            : "\n#undef AU_WB_TEMPERATURE\n#define AU_WB_TEMPERATURE 0.0\n#undef AU_WB_TINT\n#define AU_WB_TINT 0.0\n"
              "#undef AU_ADAPTIVE_EXPOSURE\n#define AU_ADAPTIVE_EXPOSURE 0\n#undef AU_TM_HIGHLIGHT_PROTECT\n#define AU_TM_HIGHLIGHT_PROTECT 0.0\n");
        str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
            "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
            "uniform int testCase;uniform float testEye;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Common.glsl\"\n#include \"AU_Tonemap.glsl\"\n", 0);
        str_cstr(&source, "void main(){SceneBrightness=vec4(testEye);vec3 colour;"
            // 0-3: an emissive texel's displayed colour after the inverse tonemap and the post chain.
            "if(testCase<4){vec3 shown=testCase==0?vec3(.9,.45,.1):testCase==1?vec3(.2,.6,.95):testCase==2?vec3(.85,.85,.8):vec3(.5,.05,.4);"
            "vec3 scene=AU_InverseTonemap(pow(shown,vec3(2.2)))*AU_AdaptiveExposure();DoTonemap(scene);colour=scene;}"
            "else if(testCase==4)colour=vec3(AU_Lum(AU_WhiteBalanceGain()));"
            "else if(testCase==5)colour=vec3(AU_AdaptiveExposure());"
            "else{vec3 hot=vec3(6.,.6,.15);DoTonemap(hot);colour=hot;}"
            "outColor=vec4(colour,1);}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
        for (int testCase = 0; testCase < 7; testCase++) {
            r_glUniform1i(r_glGetUniformLocation(program, "testCase"), testCase);
            r_glUniform1f(r_glGetUniformLocation(program, "testEye"), testCase == 5 ? 1.0f : 0.0f);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, results[graded][testCase]);
            for (int channel = 0; channel < 3; channel++) assert(isfinite(results[graded][testCase][channel]));
        }
        // In a dark cave the adaptation brightens; outdoors (case 5 uses full eye brightness) it doesn't.
        r_glUniform1i(r_glGetUniformLocation(program, "testCase"), 5);
        r_glUniform1f(r_glGetUniformLocation(program, "testEye"), 0.0f);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, results[graded][7]);
        r_glDeleteProgram(program);
    }
    // Glowing blocks show the same colour graded or not (white balance and adaptation undone).
    for (int testCase = 0; testCase < 4; testCase++)
        for (int channel = 0; channel < 3; channel++)
            assert(fabsf(results[0][testCase][channel] - results[1][testCase][channel]) < 0.004f);
    // White balance never changes brightness.
    assert(fabsf(results[0][4][0] - 1.0f) < 1e-4f && fabsf(results[1][4][0] - 1.0f) < 1e-4f);
    // Adaptation: none outdoors, up to 1 + strength in the dark; off means none anywhere.
    assert(fabsf(results[1][5][0] - 1.0f) < 1e-4f && fabsf(results[1][7][0] - 1.8f) < 1e-3f);
    assert(fabsf(results[0][7][0] - 1.0f) < 1e-4f);
    // A very bright red keeps more of its colour with highlight protection.
    #define SATURATION(c) ((fmaxf(c[0], fmaxf(c[1], c[2])) - fminf(c[0], fminf(c[1], c[2]))) / fmaxf(fmaxf(c[0], fmaxf(c[1], c[2])), 1e-5f))
    assert(SATURATION(results[2][6]) > SATURATION(results[0][6]) + 0.05f);
    #undef SATURATION
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); assert(!getError());
    puts("PASS: brightness-neutral white balance, dark-only adaptive exposure, unchanged glowing-block colours through both, and colour-keeping protected highlights");
}
