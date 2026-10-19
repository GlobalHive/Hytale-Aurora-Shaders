static void ssr_test(void)
{
    enum { SIZE = 256 };
    const float tanHalf = 0.7f, nearClip = 0.1f, farClip = 512.0f;
    float projection[16] = {0}, identity[16] = {0};
    projection[0] = 1.0f / tanHalf; projection[5] = 1.0f / tanHalf;
    projection[10] = -(farClip + nearClip) / (farClip - nearClip); projection[11] = -1.0f;
    projection[14] = -2.0f * farClip * nearClip / (farClip - nearClip);
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    GLuint framebuffer, depth, colour, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    GLuint *targets[3] = {&depth, &colour, &output};
    for (int i = 0; i < 3; i++) {
        ov.GenTextures(1, targets[i]); r_glBindTexture(GL_TEXTURE_2D, *targets[i]);
        ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT, NULL);
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, i == 1 ? 0x2703 : GL_NEAREST);   // GL_LINEAR_MIPMAP_LINEAR
        ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, i == 1 ? GL_LINEAR : GL_NEAREST);
    }
    // The scene (camera at the origin looking down -z, view = world): a floor at y = -2 (blue)
    // and a wall at z = -12 (red); green sky. Linear depth / far plane, and the colour image the
    // previous frame would hold (the camera did not move).
    const char *sceneCode = "#version 410 core\nin vec2 fragTexCoords;uniform mat4 testProjection;uniform int testColour;out vec4 outColor;"
        "void main(){vec2 ndc=fragTexCoords*2-1;vec3 dir=vec3(ndc/vec2(testProjection[0][0],testProjection[1][1]),-1);"
        "float tFloor=dir.y<0?-2./dir.y:1e9;float tWall=12.;float t=min(tFloor,tWall);bool wall=tWall<tFloor;"
        "outColor=testColour==1?(wall?vec4(1,0,0,1):vec4(0,0,1,1)):vec4(t/512.);}";
    GLuint scene = test_program(sceneCode);
    r_glUseProgram(scene); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, SIZE, SIZE);
    r_glUniformMatrix4fv(r_glGetUniformLocation(scene, "testProjection"), 1, 0, projection);
    for (int pass = 0; pass < 2; pass++) {
        attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, pass ? colour : depth, 0);
        r_glUniform1i(r_glGetUniformLocation(scene, "testColour"), pass);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    }
    r_glDeleteProgram(scene);
    r_glBindTexture(GL_TEXTURE_2D, colour);
    void (APIENTRY *generateMipmaps)(GLenum) = (void *)real_wglGetProcAddress("glGenerateMipmap");
    generateMipmaps(GL_TEXTURE_2D);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    Str source = {0}; str_cstr(&source, "#version 410 core\n#define USE_LINEAR_Z 1\n");
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    str_cstr(&source, "\n#undef AU_RTX_SSR_STEPS\n#define AU_RTX_SSR_STEPS 64\n"
        "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater,Time;"
        "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
        "uniform mat4 ProjectionMatrix,ViewMatrix,ViewProjectionMatrix,ReprojectionMatrix;uniform float FarClip;"
        "uniform sampler2D testDepth;uniform vec3 testPoint,testNormal;uniform float testRoughness;out vec4 outColor;\n");
    expand(&source, "#include \"AU_RTX.glsl\"\n", 0);
    str_cstr(&source, "vec3 AU_GIHitLight(vec2 uv){return vec3(0);}"
        "vec3 AU_SSRHitNormal(vec2 uv){vec2 ndc=uv*2-1;vec3 dir=vec3(ndc/vec2(ProjectionMatrix[0][0],ProjectionMatrix[1][1]),-1);"
        "return dir.y<0&&-2./dir.y<12.?vec3(0,1,0):vec3(0,0,1);}"
        "void main(){outColor=AU_GetSSR(testDepth,testPoint,testNormal,testRoughness,.5);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ProjectionMatrix"), 1, 0, projection);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ViewProjectionMatrix"), 1, 0, projection);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ReprojectionMatrix"), 1, 0, projection);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ViewMatrix"), 1, 0, identity);
    r_glUniform1f(r_glGetUniformLocation(program, "FarClip"), farClip);
    set_texture(program, "testDepth", 0, depth);
    set_texture(program, "uHookPrevScene", 1, colour);
    float sharp[4], rough[4], facing[4];
    #define SSR(px, py, pz, nx, ny, nz, roughness, result) do { \
        r_glUniform3f(r_glGetUniformLocation(program, "testPoint"), px, py, pz); \
        r_glUniform3f(r_glGetUniformLocation(program, "testNormal"), nx, ny, nz); \
        r_glUniform1f(r_glGetUniformLocation(program, "testRoughness"), roughness); \
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); readPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, result); \
        for (int channel = 0; channel < 4; channel++) assert(isfinite(result[channel])); } while (0)
    // A floor point 6 blocks out reflects the wall (red), with good confidence.
    SSR(0, -2, -6, 0, 1, 0, 0.05f, sharp);
    assert(sharp[0] > .9f && sharp[2] < .1f && sharp[3] > .6f && sharp[3] <= 1);
    // Rough: the same wall, through a blurrier mip, still found.
    SSR(0, -2, -6, 0, 1, 0, 0.6f, rough);
    assert(rough[0] > .5f && rough[3] > .6f);
    // The wall facing the camera reflects back toward it: nothing on screen, no confidence.
    SSR(0, 0, -12, 0, 0, 1, 0.05f, facing);
    assert(facing[3] == 0);
    #undef SSR
    r_glDeleteProgram(program);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &depth); deleteTextures(1, &colour); deleteTextures(1, &output); assert(!getError());
    puts("PASS: refined screen-space reflections find the reflected wall sharp and rough, with confidence, and give no confidence to rays turning back to the camera");
}
