#define wmain denoiser_regression_main
#include "../denoiser_test/test.c"
#undef wmain

static GLuint cloud_benchmark_program(int layers, int lighting, int lightSamples)
{
    Str source = {0}; str_cstr(&source, "#version 410 core\n");
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    str_cstr(&source, "\n#undef AU_BIOME_ATMOSPHERE\n#define AU_BIOME_ATMOSPHERE 0\n"
        "#undef AU_ATMOSPHERE_MODEL\n#define AU_ATMOSPHERE_MODEL 0\n"
        "#undef AU_CLOUD_AMOUNT\n#define AU_CLOUD_AMOUNT 1.0\n#undef AU_CLOUD_ALT1\n#define AU_CLOUD_ALT1 300.0\n"
        "#undef AU_CLOUD_ALT2\n#define AU_CLOUD_ALT2 340.0\n#undef AU_CLOUD_STEP\n#define AU_CLOUD_STEP 8.0\n"
        "#undef AU_CLOUD_DISTANCE\n#define AU_CLOUD_DISTANCE 2400.0\n#undef AU_CLOUD_LAYERS\n"
        "#undef AU_CLOUD_LIGHTING\n#undef AU_CLOUD_LIGHT_SAMPLES\n");
    str_cstr(&source, layers == 1 ? "#define AU_CLOUD_LAYERS 1\n" : "#define AU_CLOUD_LAYERS 2\n");
    str_cstr(&source, lighting ? "#define AU_CLOUD_LIGHTING 1\n" : "#define AU_CLOUD_LIGHTING 0\n");
    str_cstr(&source, lightSamples == 1 ? "#define AU_CLOUD_LIGHT_SAMPLES 1\n" : "#define AU_CLOUD_LIGHT_SAMPLES 2\n");
    str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
        "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
        "uniform float testHeight;in vec2 fragTexCoords;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Clouds.glsl\"\n", 0);
    str_cstr(&source, "void main(){CameraPosition=vec3(700,testHeight,900);Time=50;IsCameraUnderwater=0;"
        "sunVec=normalize(vec3(1,1,0));lightVec=sunVec;sunVisibility=1;sunVisibility2=1;sunFactor=1;noonFactor=1;"
        "nightFactor=0;invNoonFactor=0;rainFactor=0;rainFactor2=0;invRainFactor=1;shadowTime=1;FogParams=vec4(0,768,0,0);"
        "ambientColor=vec3(.3,.4,.6);lightColor=vec3(1,.9,.8);dayUpSkyColor=vec3(.2,.4,.8);dayMiddleSkyColor=vec3(.5,.6,.8);dayDownSkyColor=vec3(.6);"
        "nightUpSkyColor=vec3(.02);nightMiddleSkyColor=vec3(.03);nightDownSkyColor=vec3(.01);sunsetDownSkyColorP=vec3(1,.3,.1);waterFogColor=vec3(0);"
        "vec3 direction=normalize(vec3((fragTexCoords.x-.5)*2,fragTexCoords.y-.1,1));"
        "outColor=GetClouds(direction,1e9,1,dot(direction,sunVec),direction.y,.5);}");
    GLuint program = test_program(source.p); free(source.p);
    return program;
}

static int compare_time(const void *left, const void *right)
{
    double first = *(const double *)left, second = *(const double *)right;
    return (first > second) - (first < second);
}

static GLuint atmosphere_benchmark_program(int model, int fog)
{
    Str source = {0}; str_cstr(&source, "#version 410 core\n");
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    str_cstr(&source, "\n#undef AU_BIOME_ATMOSPHERE\n#define AU_BIOME_ATMOSPHERE 0\n#undef AU_ATMOSPHERE_MODEL\n");
    str_cstr(&source, model ? "#define AU_ATMOSPHERE_MODEL 1\n" : "#define AU_ATMOSPHERE_MODEL 0\n");
    str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
        "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
        "uniform float testHeight;in vec2 fragTexCoords;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Fog.glsl\"\n", 0);
    str_cstr(&source, "void main(){CameraPosition=vec3(0,testHeight,0);SunPositionWS=normalize(vec3(1,1,0));"
        "IsCameraUnderwater=0;FogParams=vec4(0,768,0,0);SceneBrightness=vec4(1);AU_InitCommon();"
        "vec3 direction=normalize(vec3((fragTexCoords.x-.5)*2,fragTexCoords.y-.1,1));vec3 colour=vec3(.4);"
        "float alignment=dot(direction,sunVec);");
    str_cstr(&source, fog ? "DoAtmosphericFog(colour,direction*500,500,alignment,.5);" : "colour=GetSky(direction.y,alignment,.5,true,false);");
    str_cstr(&source, "outColor=vec4(colour,1);}");
    GLuint program = test_program(source.p); free(source.p);
    return program;
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 3);
    denoiser_regression_main(2, arguments);
    GLuint programs[9];
    wcscpy(g_packDir, arguments[2]); programs[0] = cloud_benchmark_program(2,0,1);
    wcscpy(g_packDir, arguments[1]);
    programs[1] = cloud_benchmark_program(1,0,1);
    programs[2] = cloud_benchmark_program(2,0,1);
    programs[3] = cloud_benchmark_program(2,1,1);
    programs[4] = cloud_benchmark_program(2,1,2);
    programs[5] = atmosphere_benchmark_program(0,0);
    programs[6] = atmosphere_benchmark_program(1,0);
    programs[7] = atmosphere_benchmark_program(0,1);
    programs[8] = atmosphere_benchmark_program(1,1);
    size_t textureLength = 0; char *textureData = read_file("textures/noise.raw", &textureLength);
    assert(textureData && textureLength >= 12 && !memcmp(textureData,"HYTX",4));
    unsigned dimensions[2]; memcpy(dimensions,textureData+4,sizeof dimensions);
    assert(dimensions[0] && dimensions[1] && textureLength == 12 + (size_t)dimensions[0] * dimensions[1] * 4);
    GLuint noiseTexture,framebuffer,output;
    ov.GenTextures(1,&noiseTexture); r_glActiveTexture(GL_TEXTURE0); r_glBindTexture(GL_TEXTURE_2D,noiseTexture);
    ov.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,dimensions[0],dimensions[1],0,GL_RGBA,GL_UNSIGNED_BYTE,textureData+12); free(textureData);
    void (APIENTRY *generateMipmaps)(GLenum) = (void *)real_wglGetProcAddress("glGenerateMipmap");
    generateMipmaps(GL_TEXTURE_2D);
    ov.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,0x2703); ov.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    ov.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT); ov.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    generateFramebuffer(1,&framebuffer); bindFramebuffer(0x8d40,framebuffer);
    ov.GenTextures(1,&output); r_glBindTexture(GL_TEXTURE_2D,output);
    ov.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,1920,1080,0,GL_RGBA,GL_FLOAT,NULL);
    attachTexture(0x8d40,0x8ce0,GL_TEXTURE_2D,output,0);
    ov.Viewport(0,0,1920,1080); ov.BindVertexArray(ov.vao);
    PFN_glGenQueries generateQueries = (void *)real_wglGetProcAddress("glGenQueries");
    PFN_glQueryCounter timestamp = (void *)real_wglGetProcAddress("glQueryCounter");
    PFN_glGetQueryObjectui64v queryResult = (void *)real_wglGetProcAddress("glGetQueryObjectui64v");
    GLuint queries[2]; generateQueries(2,queries);
    const char *names[] = {"baseline","Classic-1-layer","Classic-2-layers","Detailed-1-light-sample","Detailed-2-light-samples",
        "sky-Classic","sky-Enhanced","fog-Classic","fog-Enhanced"};
    for (int scene = 0; scene < 2; scene++) {
        double timings[9][21];
        for (int iteration = -3; iteration < 21; iteration++)
        for (int mode = 0; mode < 9; mode++) {
            r_glUseProgram(programs[mode]); set_texture(programs[mode],"noisetex",0,noiseTexture);
            r_glUniform1f(r_glGetUniformLocation(programs[mode],"testHeight"),scene ? 300 : 100);
            timestamp(queries[0],GL_TIMESTAMP); r_glDrawArrays(OV_GL_TRIANGLE_STRIP,0,4); timestamp(queries[1],GL_TIMESTAMP);
            uint64_t begin,end; queryResult(queries[0],GL_QUERY_RESULT,&begin); queryResult(queries[1],GL_QUERY_RESULT,&end);
            if (iteration >= 0) timings[mode][iteration] = (end-begin)/1e6;
        }
        for (int mode = 0; mode < 9; mode++) {
            qsort(timings[mode],21,sizeof(double),compare_time);
            printf("CLOUD BENCH 1920x1080 %s %s median=%.3fms p95=%.3fms\n",scene ? "inside" : "below",names[mode],timings[mode][10],timings[mode][19]);
        }
    }
    assert(!getError());
    return 0;
}
