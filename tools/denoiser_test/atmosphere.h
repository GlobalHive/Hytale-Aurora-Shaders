static void enhanced_atmosphere_test(void)
{
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    float results[2][2][5][8][4];
    for (int model = 0; model < 2; model++)
    for (int biome = 0; biome < 2; biome++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, "\n#undef AU_ATMOSPHERE_MODEL\n#undef AU_BIOME_ATMOSPHERE\n"
            "#undef AU_ATM_FOG_MULT\n#define AU_ATM_FOG_MULT .95\n#undef AU_ATM_FOG_DISTANCE\n#define AU_ATM_FOG_DISTANCE 1.0\n");
        str_cstr(&source, model ? "#define AU_ATMOSPHERE_MODEL 1\n" : "#define AU_ATMOSPHERE_MODEL 0\n");
        str_cstr(&source, biome ? "#define AU_BIOME_ATMOSPHERE 1\n" : "#define AU_BIOME_ATMOSPHERE 0\n");
        str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
            "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
            "uniform int testCase,testScene;uniform float testView,testDistance,testBrightness,testHeight;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Fog.glsl\"\n#include \"AU_Clouds.glsl\"\n", 0);
        str_cstr(&source, "void main(){float elevation=testScene==0?.9:testScene==1?.01:testScene==2?-.09:testScene==3?-.8:.6;"
            "SunPositionWS=vec3(sqrt(1-elevation*elevation),elevation,0);CameraPosition=vec3(0,testHeight,0);"
            "SunLightColor=vec4(.8,.6,.4,1);SunColor=SunLightColor;AmbientFrontColor=AmbientBackColor=vec3(.2,.3,.4);AmbientIntensity=.5;"
            "FogTopColor=vec4(.15,.3,.6,1);FogFrontColor=vec4(.6,.3,.1,1);FogBackColor=vec4(.1,.2,.3,1);"
            "FogParams=vec4(0,512,0,0);SceneBrightness=vec4(testBrightness);Time=0;IsCameraUnderwater=0;"
            "for(int matrix=0;matrix<4;matrix++)SunShadowMatrix[matrix]=mat4(0);AU_InitCommon();"
            "if(testScene==4){rainFactor=rainFactor2=1;invRainFactor=0;}"
            "vec3 direction=vec3(sqrt(max(1-testView*testView,0)),testView,0);float alignment=dot(direction,sunVec);vec3 colour;"
            "if(testCase==0)colour=GetSky(testView,alignment,.5,false,false);"
            "else if(testCase==1)colour=lightColor;else if(testCase==2)colour=ambientColor;else if(testCase==3)colour=shaftColor;"
            "else if(testCase==4){colour=vec3(.2);DoAtmosphericFog(colour,direction*testDistance,testDistance,alignment,.5);}"
            "else if(testCase==5){InitCloudColors();colour=cloudAmbientColor;}"
            "else if(testCase==6)colour=vec3(AU_AtmosphereFogAmount(direction*testDistance,testDistance,testBrightness));"
            "else {IsCameraUnderwater=1;colour=GetSky(testView,alignment,.5,true,false);}outColor=vec4(colour,1);}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
        r_glUniform1f(r_glGetUniformLocation(program, "testHeight"), 120);
        r_glUniform1f(r_glGetUniformLocation(program, "testView"), .15f);
        r_glUniform1f(r_glGetUniformLocation(program, "testDistance"), 300);
        r_glUniform1f(r_glGetUniformLocation(program, "testBrightness"), 1);
        const float topColour[] = {.2f, .4f, .8f, 1};
        r_glUniform4fv(r_glGetUniformLocation(program, "uHookTopGradientColor"), 1, topColour);
        for (int scene = 0; scene < 5; scene++)
        for (int testCase = 0; testCase < 8; testCase++) {
            r_glUniform1i(r_glGetUniformLocation(program, "testScene"), scene);
            r_glUniform1i(r_glGetUniformLocation(program, "testCase"), testCase);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,results[model][biome][scene][testCase]);
            for (int channel = 0; channel < 4; channel++) {
                float value = results[model][biome][scene][testCase][channel];
                assert(isfinite(value) && value >= 0 && value < 16);
            }
        }
        r_glUniform1i(r_glGetUniformLocation(program, "testScene"), 0);
        r_glUniform1i(r_glGetUniformLocation(program, "testCase"), 6);
        r_glUniform1f(r_glGetUniformLocation(program, "testView"), 0);
        float previous = -1;
        for (int sample = 0; sample <= 16; sample++) {
            r_glUniform1f(r_glGetUniformLocation(program, "testDistance"), sample * 64.0f);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            float actual[4]; readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,actual);
            assert(actual[0] >= previous && actual[0] >= 0 && actual[0] <= 1);
            if (sample == 0) assert(actual[0] == 0);
            previous = actual[0];
        }
        r_glUniform1f(r_glGetUniformLocation(program, "testBrightness"), 0);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        float closedCave[4]; readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,closedCave);
        assert(closedCave[0] == 0);
        r_glUniform1i(r_glGetUniformLocation(program, "testCase"), 0);
        for (int height = -128; height <= 1024; height += 384) {
            r_glUniform1f(r_glGetUniformLocation(program, "testHeight"), (float)height);
            float below[4], above[4];
            r_glUniform1f(r_glGetUniformLocation(program, "testView"), -.00001f);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,below);
            r_glUniform1f(r_glGetUniformLocation(program, "testView"), .00001f);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); readPixels(4,4,1,1,GL_RGBA,GL_FLOAT,above);
            for (int channel = 0; channel < 3; channel++) {
                assert(isfinite(below[channel]) && isfinite(above[channel]));
                assert(fabsf(below[channel] - above[channel]) < .001f);
            }
        }
        r_glDeleteProgram(program);
    }
    for (int biome = 0; biome < 2; biome++)
    for (int scene = 0; scene < 5; scene++)
    for (int testCase = 0; testCase < 8; testCase++)
    for (int channel = 0; channel < 3; channel++) {
        if ((testCase >= 1 && testCase <= 3) || (biome && testCase != 6))
            assert(fabsf(results[0][biome][scene][testCase][channel] - results[1][biome][scene][testCase][channel]) < .00001f);
    }
    assert(fabsf(results[0][0][0][0][2] - results[1][0][0][0][2]) > .01f);
    for (int scene = 0; scene < 4; scene++) {
        const float *skyColour = results[1][0][scene][0];
        assert(skyColour[1] <= fmaxf(skyColour[0], skyColour[2]));
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real,"glDeleteTextures");
    deleteTextures(1,&output); assert(!getError());
    puts("PASS: enhanced sky/fog finite at noon/twilight/night/rain/underwater, continuous horizon, bounded monotonic haze, dark-cave exclusion, unchanged surface lighting and biome override");
}
