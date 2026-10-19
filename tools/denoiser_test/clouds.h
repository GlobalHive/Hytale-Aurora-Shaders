static void cloud_volume_test(void)
{
    GLuint framebuffer, output, noiseTexture;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    unsigned char noise[128 * 128 * 4]; memset(noise, 217, sizeof noise);
    ov.GenTextures(1, &noiseTexture); r_glActiveTexture(GL_TEXTURE0); r_glBindTexture(GL_TEXTURE_2D, noiseTexture);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, noise);
    void (APIENTRY *generateMipmaps)(GLenum) = (void *)real_wglGetProcAddress("glGenerateMipmap");
    generateMipmaps(GL_TEXTURE_2D);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const float cameras[][3] = {{0,100,0},{0,400,0},{0,300,0},{0,100,0},{0,100,0},{0,100,0},{0,300,0},{0,290,0}};
    const float directions[][3] = {{0,1,0},{0,-1,0},{1,0,0},{1,0,0},{0,1,0},{0,1,0},{1,0.0000001f,0},{0,1,0}};
    const float limits[] = {2400,2400,2400,2400,100,200,2400,2400};
    const float expected[][4] = {{184,216,224,256},{44,76,84,116},{0,2400,0,0},{0,0,0,0},{0,0,0,0},{184,200,0,0},{0,2400,0,0},{0,26,34,66}};
    for (int layers = 1; layers <= 2; layers++)
    for (int lighting = 0; lighting < 2; lighting++)
    for (int lightSamples = 1; lightSamples <= 2; lightSamples++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, "\n#undef AU_BIOME_ATMOSPHERE\n#define AU_BIOME_ATMOSPHERE 0\n"
            "#undef AU_CLOUD_AMOUNT\n#define AU_CLOUD_AMOUNT 1.0\n#undef AU_CLOUD_ALT1\n#define AU_CLOUD_ALT1 300.0\n"
            "#undef AU_CLOUD_ALT2\n#define AU_CLOUD_ALT2 340.0\n#undef AU_CLOUD_STEP\n#define AU_CLOUD_STEP 8.0\n"
            "#undef AU_CLOUD_DISTANCE\n#define AU_CLOUD_DISTANCE 2400.0\n#undef AU_CLOUD_LAYERS\n"
            "#undef AU_CLOUD_LIGHTING\n#undef AU_CLOUD_LIGHT_SAMPLES\n");
        str_cstr(&source, layers == 1 ? "#define AU_CLOUD_LAYERS 1\n" : "#define AU_CLOUD_LAYERS 2\n");
        str_cstr(&source, lighting ? "#define AU_CLOUD_LIGHTING 1\n" : "#define AU_CLOUD_LIGHTING 0\n");
        str_cstr(&source, lightSamples == 1 ? "#define AU_CLOUD_LIGHT_SAMPLES 1\n" : "#define AU_CLOUD_LIGHT_SAMPLES 2\n");
        str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
            "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
            "uniform vec3 testCamera,testDirection;uniform float testLimit,testDither;uniform int testCase;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Clouds.glsl\"\n", 0);
        str_cstr(&source, "void main(){CameraPosition=testCamera;Time=0;IsCameraUnderwater=0;"
            "vec2 nearRange,farRange;AU_CloudIntervals(testCamera,testDirection,testLimit,nearRange,farRange);"
            "if(testCase==0){outColor=vec4(nearRange,farRange);return;}"
            "if(testCase==1){outColor=vec4(vec2(AU_CloudSampleCounts(nearRange,farRange)),0,1);return;}"
            "sunVec=normalize(vec3(1,1,0));lightVec=sunVec;sunVisibility=1;sunVisibility2=1;sunFactor=1;noonFactor=1;"
            "nightFactor=0;invNoonFactor=0;rainFactor=0;rainFactor2=0;invRainFactor=1;shadowTime=1;"
            "ambientColor=vec3(.3,.4,.6);lightColor=vec3(1,.9,.8);dayUpSkyColor=vec3(.2,.4,.8);dayMiddleSkyColor=vec3(.5,.6,.8);dayDownSkyColor=vec3(.6);"
            "nightUpSkyColor=vec3(.02);nightMiddleSkyColor=vec3(.03);nightDownSkyColor=vec3(.01);sunsetDownSkyColorP=vec3(1,.3,.1);waterFogColor=vec3(0);"
            "outColor=GetClouds(testDirection,testLimit,1,dot(testDirection,sunVec),testDirection.y,testDither);}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
        set_texture(program, "noisetex", 0, noiseTexture);
        for (int scene = 0; scene < 8; scene++) {
            r_glUniform3f(r_glGetUniformLocation(program,"testCamera"),cameras[scene][0],cameras[scene][1],cameras[scene][2]);
            r_glUniform3f(r_glGetUniformLocation(program,"testDirection"),directions[scene][0],directions[scene][1],directions[scene][2]);
            r_glUniform1f(r_glGetUniformLocation(program,"testLimit"),limits[scene]);
            for (int testCase = 0; testCase < 3; testCase++) {
                r_glUniform1i(r_glGetUniformLocation(program,"testCase"),testCase);
                r_glDrawArrays(OV_GL_TRIANGLE_STRIP,0,4);
                float actual[4]; readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,actual);
                for (int channel = 0; channel < 4; channel++) assert(isfinite(actual[channel]));
                if (testCase == 0) {
                    for (int channel = 0; channel < 4; channel++) {
                        float wanted = expected[scene][channel];
                        if (layers == 1) wanted = channel >= 2 ? 0 : scene == 1 ? expected[scene][channel + 2] : wanted;
                        assert(fabsf(actual[channel] - wanted) < .001f);
                    }
                } else if (testCase == 1) {
                    int budget = lighting ? (int)(96.0 / (1 + .5 * lightSamples)) : 96;
                    assert(actual[0] >= 0 && actual[1] >= 0 && actual[0] + actual[1] <= budget);
                    if (scene == 3 || scene == 4) assert(actual[0] == 0 && actual[1] == 0);
                    else assert(actual[0] > 0);
                } else {
                    assert(actual[3] >= 0 && actual[3] <= 1);
                    if (scene == 3 || scene == 4) assert(actual[3] == 0);
                    else assert(actual[3] > 0);
                    for (int frame = 0; frame < 3; frame++) {
                        r_glUniform1f(r_glGetUniformLocation(program,"testDither"),frame * .499f);
                        r_glUniform1i(r_glGetUniformLocation(program,"uHookFrame"),frame * 17);
                        r_glDrawArrays(OV_GL_TRIANGLE_STRIP,0,4);
                        float repeated[4]; readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,repeated);
                        for (int channel = 0; channel < 4; channel++) assert(fabsf(actual[channel] - repeated[channel]) < .00001f);
                    }
                }
            }
        }
        r_glDeleteProgram(program);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real,"glDeleteTextures");
    deleteTextures(1,&noiseTexture); deleteTextures(1,&output); assert(!getError());
    puts("PASS: cloud ray intervals below/inside/above layers, horizon, depth clipping, shared budgets and frame-stable Classic/Detailed lighting");
}
