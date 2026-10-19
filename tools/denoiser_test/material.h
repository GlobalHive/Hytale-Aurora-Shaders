static void surface_material_test(void)
{
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 8, 8, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    Str source = {0}; str_cstr(&source, "#version 410 core\n");
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    str_cstr(&source, "\n#undef AU_MATERIALS\n#define AU_MATERIALS 1\n#undef AU_RAIN_WETNESS\n#define AU_RAIN_WETNESS 1.0\n"
        "#undef AU_MATERIAL_GLOSS\n#define AU_MATERIAL_GLOSS 1.0\n");
    str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
        "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
        "uniform int testKind,testLevel,testFlags;uniform float testRain;uniform vec3 testNormal;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Common.glsl\"\n#include \"AU_Material.glsl\"\n", 0);
    str_cstr(&source, "void main(){SunPositionWS=vec3(.3,.9,0);CameraPosition=vec3(0,120,0);SunLightColor=vec4(1);SunColor=vec4(1);"
        "AmbientFrontColor=AmbientBackColor=vec3(.3);AmbientIntensity=.5;FogTopColor=FogFrontColor=FogBackColor=vec4(.3);"
        "FogParams=vec4(0,512,0,0);SceneBrightness=vec4(1);Time=0;IsCameraUnderwater=0;"
        "for(int matrix=0;matrix<4;matrix++)SunShadowMatrix[matrix]=mat4(0);AU_InitCommon();rainFactor=testRain;"
        "uint meta=uint(testKind<<5|testLevel<<2|((testFlags&1)!=0?2:0)|((testFlags&2)!=0?0:1));"
        "AU_MaterialData m=AU_GetMaterial(float(meta)/255.,vec3(.5),normalize(testNormal),1.,0.,(testFlags&1)!=0,(testFlags&2)!=0);"
        "vec3 faceN=AU_MaterialReflectionNormal(normalize(testNormal),m);"
        "outColor=vec4(m.smoothness,m.film,m.soak,m.metal+2.*faceN.y);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 8, 8);
    r_glUniform1f(r_glGetUniformLocation(program, "uHookWetness"), 0);
    #define MATERIAL(kind, level, flags, rain, nx, ny, nz, result) do { \
        r_glUniform1i(r_glGetUniformLocation(program, "testKind"), kind); \
        r_glUniform1i(r_glGetUniformLocation(program, "testLevel"), level); \
        r_glUniform1i(r_glGetUniformLocation(program, "testFlags"), flags); \
        r_glUniform1f(r_glGetUniformLocation(program, "testRain"), rain); \
        r_glUniform3f(r_glGetUniformLocation(program, "testNormal"), nx, ny, nz); \
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, result); \
        for (int channel = 0; channel < 4; channel++) assert(isfinite(result[channel])); } while (0)
    float plainDry[4], polishedDry[4], cutStone[4], metalDry[4], plainWetTop[4], plainWetWall[4], polishedWetTop[4];
    float grassWetTop[4], soilWetTop[4], emissiveWet[4], foliageWet[4], bumpedWet[4], bumpedDry[4], tiltedWet[4];
    MATERIAL(0, 0, 0, 0.f, 0, 1, 0, plainDry);
    MATERIAL(1, 4, 0, 0.f, 0, 1, 0, polishedDry);
    MATERIAL(1, 2, 0, 0.f, 0, 1, 0, cutStone);
    MATERIAL(2, 4, 0, 0.f, 0, 1, 0, metalDry);
    MATERIAL(0, 0, 0, 1.f, 0, 1, 0, plainWetTop);
    MATERIAL(0, 0, 0, 1.f, 1, 0, 0, plainWetWall);
    MATERIAL(1, 4, 0, 1.f, 0, 1, 0, polishedWetTop);
    MATERIAL(3, 7, 0, 1.f, 0, 1, 0, grassWetTop);
    MATERIAL(3, 6, 0, 1.f, 0, 1, 0, soilWetTop);
    MATERIAL(0, 0, 1, 1.f, 0, 1, 0, emissiveWet);
    MATERIAL(3, 7, 2, 1.f, 0, 1, 0, foliageWet);
    MATERIAL(0, 0, 0, 1.f, .3f, .95f, 0, bumpedWet);
    MATERIAL(0, 0, 0, 0.f, .3f, .95f, 0, bumpedDry);
    MATERIAL(0, 0, 0, 1.f, .7f, .7f, 0, tiltedWet);
    #undef MATERIAL
    // Dry wood, bricks and ordinary blocks are matte; polished stone is graded; metals are metal.
    assert(plainDry[0] == 0 && plainDry[1] == 0 && plainDry[2] == 0 && plainDry[3] < 2.0001f);
    assert(polishedDry[0] > .5f && polishedDry[0] < .65f && polishedDry[3] < 2.0001f);
    assert(cutStone[0] > .25f && cutStone[0] < polishedDry[0]);
    assert(metalDry[0] > .5f && metalDry[3] > 2.9f);
    // Rain: a film on tops, much less on walls; plain blocks soak a little, grass and soil a lot
    // and stay far from glossy; polished stone keeps the water on top.
    assert(plainWetTop[1] > .5f && plainWetWall[1] < plainWetTop[1] * .4f);
    assert(plainWetTop[2] > .3f && plainWetTop[2] < .5f && plainWetWall[2] < plainWetTop[2]);
    assert(polishedWetTop[1] > .9f && polishedWetTop[0] >= .72f && polishedWetTop[2] < .15f);
    assert(grassWetTop[0] < .25f && grassWetTop[2] > .95f);
    assert(soilWetTop[0] < .3f && soilWetTop[2] > .9f && soilWetTop[0] > grassWetTop[0]);
    // Emissive and foliage texels are never wet materials.
    for (int channel = 0; channel < 3; channel++) assert(emissiveWet[channel] == 0 && foliageWet[channel] == 0);
    // A rain film lies flat on the block face; dry relief, and clearly tilted surfaces, keep theirs.
    float bumpedY = .95f / sqrtf(.3f * .3f + .95f * .95f);
    assert(bumpedWet[3] / 2.f > bumpedY + .02f && fabsf(bumpedDry[3] / 2.f - bumpedY) < .001f);
    assert(fabsf(tiltedWet[3] / 2.f - sqrtf(.5f)) < .001f);
    r_glDeleteProgram(program);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); assert(!getError());
    puts("PASS: matte dry plain blocks, graded polish, metals, rain film on tops vs walls, porous soaking (grass/soil never glossy), emissive/foliage untouched and flat water-film reflection normals");
}
