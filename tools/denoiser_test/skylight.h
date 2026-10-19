static void sky_light_test(void)
{
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    enum { CASES = 11, SCENES = 5 };
    for (int model = 0; model < 2; model++)
    for (int biome = 0; biome < 2; biome++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, "\n#undef AU_ATMOSPHERE_MODEL\n#undef AU_BIOME_ATMOSPHERE\n#undef AU_SKY_LIGHTING\n#define AU_SKY_LIGHTING 1\n");
        str_cstr(&source, model ? "#define AU_ATMOSPHERE_MODEL 1\n" : "#define AU_ATMOSPHERE_MODEL 0\n");
        str_cstr(&source, biome ? "#define AU_BIOME_ATMOSPHERE 1\n" : "#define AU_BIOME_ATMOSPHERE 0\n");
        str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
            "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
            "uniform int testCase,testScene;out vec4 outColor;\n");
        expand(&source, "#include \"AU_SkyLight.glsl\"\n", 0);
        str_cstr(&source, "void main(){float elevation=testScene==0?.9:testScene==1?.01:testScene==2?-.09:testScene==3?-.8:.6;"
            "SunPositionWS=vec3(sqrt(1-elevation*elevation),elevation,0);CameraPosition=vec3(0,120,0);"
            "SunLightColor=vec4(.8,.6,.4,1);SunColor=SunLightColor;AmbientFrontColor=AmbientBackColor=vec3(.2,.3,.4);AmbientIntensity=.5;"
            "FogTopColor=vec4(.15,.3,.6,1);FogFrontColor=vec4(.6,.3,.1,1);FogBackColor=vec4(.1,.2,.3,1);"
            "FogParams=vec4(0,512,0,0);SceneBrightness=vec4(1);Time=0;IsCameraUnderwater=0;"
            "for(int matrix=0;matrix<4;matrix++)SunShadowMatrix[matrix]=mat4(0);AU_InitCommon();"
            "if(testScene==4){rainFactor=rainFactor2=1;invRainFactor=0;}"
            "AU_SkyLightCube(auSkyCube);auSkyCubeValid=true;vec3 colour;"
            "if(testCase<6)colour=auSkyCube[testCase];"
            "else if(testCase==6)colour=AU_SkyAmbient(ambientColor,vec3(0,1,0),0);"
            "else if(testCase==7)colour=AU_SkyAmbient(ambientColor,vec3(0,-1,0),0);"
            "else if(testCase==8)colour=AU_SkyAmbient(ambientColor,vec3(1,0,0),0);"
            "else if(testCase==9)colour=AU_SkyAmbient(ambientColor,vec3(-1,0,0),0);"
            "else colour=ambientColor;outColor=vec4(colour,1);}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
        const float topColour[] = {.2f, .4f, .8f, 1};
        r_glUniform4fv(r_glGetUniformLocation(program, "uHookTopGradientColor"), 1, topColour);
        for (int scene = 0; scene < SCENES; scene++) {
            float results[CASES][4];
            r_glUniform1i(r_glGetUniformLocation(program, "testScene"), scene);
            for (int testCase = 0; testCase < CASES; testCase++) {
                r_glUniform1i(r_glGetUniformLocation(program, "testCase"), testCase);
                r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
                readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, results[testCase]);
                for (int channel = 0; channel < 3; channel++) {
                    float value = results[testCase][channel];
                    assert(isfinite(value) && value >= 0 && value < 64);
                }
            }
            #define LUM(c) (.299f * (c)[0] + .587f * (c)[1] + .114f * (c)[2])
            // Upward faces keep the existing ambient brightness exactly; nothing gets more sky
            // light than them from below; the ground bounce is not zero.
            assert(fabsf(LUM(results[6]) - LUM(results[10])) <= .002f * LUM(results[10]) + 1e-6f);
            assert(LUM(results[7]) <= LUM(results[6]) + 1e-6f);
            assert(LUM(results[3]) > 0 && LUM(results[3]) < LUM(results[2]));
            for (int face = 0; face < 6; face++) if (face != 2) assert(LUM(results[face]) <= LUM(results[2]) * 1.5f);
            // The daylit atmosphere is brighter toward the sun (+x here).
            if (model && !biome && scene <= 1) assert(LUM(results[8]) > LUM(results[9]));
            // The sky's hue stays bounded.
            float lum = LUM(results[6]);
            if (lum > 1e-5f) for (int channel = 0; channel < 3; channel++)
                assert(results[6][channel] / lum > .4f && results[6][channel] / lum < 2.5f);
            #undef LUM
        }
        r_glDeleteProgram(program);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); assert(!getError());
    puts("PASS: sky light cube finite for both atmosphere models and biome atmosphere at noon/twilight/dusk/night/rain; upward faces keep the ambient brightness, undersides get less, the sun side more, hue bounded");
}
