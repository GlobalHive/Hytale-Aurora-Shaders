static void gtao_test(void)
{
    enum { SIZE = 128 };
    const float aspect = 1.0f, tanHalf = 0.7f, nearClip = 0.1f, farClip = 512.0f;
    // Column-major perspective projection (GL convention), as the game's ProjectionMatrix.
    float projection[16] = {0};
    projection[0] = 1.0f / (aspect * tanHalf); projection[5] = 1.0f / tanHalf;
    projection[10] = -(farClip + nearClip) / (farClip - nearClip); projection[11] = -1.0f;
    projection[14] = -2.0f * farClip * nearClip / (farClip - nearClip);
    GLuint framebuffer, depth, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &depth); r_glBindTexture(GL_TEXTURE_2D, depth);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT, NULL);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT, NULL);
    // The scene: a wall 10 blocks in front of the camera meeting a side wall at x = 3 that runs
    // toward the camera - an inside room corner. Linear depth / far plane.
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, depth, 0);
    GLuint scene = test_program("#version 410 core\nin vec2 fragTexCoords;uniform mat4 testProjection;out vec4 outColor;"
        "void main(){vec2 ndc=fragTexCoords*2-1;vec3 dir=vec3(ndc/vec2(testProjection[0][0],testProjection[1][1]),-1);"
        "float t=dir.x>.3?3./dir.x:10.;outColor=vec4(t/512.);}");
    r_glUseProgram(scene); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, SIZE, SIZE);
    r_glUniformMatrix4fv(r_glGetUniformLocation(scene, "testProjection"), 1, 0, projection);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    r_glDeleteProgram(scene);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    for (int quality = 1; quality <= 3; quality++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n#define USE_LINEAR_Z 1\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        char defines[160];
        snprintf(defines, sizeof defines, "\n#undef AU_GTAO_QUALITY\n#define AU_GTAO_QUALITY %d\n#undef AU_GTAO_RADIUS\n#define AU_GTAO_RADIUS 1.5\n", quality);
        str_cstr(&source, defines);
        str_cstr(&source, "uniform mat4 ProjectionMatrix;uniform float FarClip;vec3 CameraPosition=vec3(0);"
            "in vec2 fragTexCoords;uniform sampler2D testDepth;uniform vec3 testColour;uniform int testBent;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Temporal.glsl\"\n#include \"AU_GTAO.glsl\"\n", 0);
        str_cstr(&source, "void main(){float lin=AU_GTAOLinearDepth(testDepth,fragTexCoords);vec3 pos=AU_ViewPosition(fragTexCoords,lin);"
            "vec3 n=pos.z<-9.999?vec3(0,0,1):vec3(-1,0,0);vec3 bent;float v=AU_GTAOVisibilityBent(testDepth,fragTexCoords,pos,n,vec2(.5),bent);"
            "outColor=testBent!=0?vec4(bent.x,bent.z,pos.x,lin):vec4(v,AU_GTAOMultiBounce(v,testColour),pos.x,lin);}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program);
        r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ProjectionMatrix"), 1, 0, projection);
        r_glUniform1f(r_glGetUniformLocation(program, "FarClip"), farClip);
        set_texture(program, "testDepth", 0, depth);
        float pixels[SIZE][4];
        for (int albedo = 0; albedo < 2; albedo++) {
            const float colour[3] = {albedo ? .9f : .1f, albedo ? .9f : .1f, albedo ? .9f : .1f};
            r_glUniform3f(r_glGetUniformLocation(program, "testColour"), colour[0], colour[1], colour[2]);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            readPixels(0, SIZE / 2, SIZE, 1, GL_RGBA, GL_FLOAT, pixels);
            float open = 0, crease = 2, nearCrease = 0, darkBounce = 2, brightBounce = 0;
            for (int x = 0; x < SIZE; x++) {
                for (int channel = 0; channel < 4; channel++) assert(isfinite(pixels[x][channel]));
                float v = pixels[x][0], worldX = pixels[x][2];
                assert(v >= 0 && v <= 1 && pixels[x][1] >= v - 1e-5f && pixels[x][1] <= 1);
                if (pixels[x][3] < 9.99f) continue;   // only the back wall
                if (worldX < 0.0f && worldX > -2.0f) open = fmaxf(open, 1.0f - v);
                if (worldX < 3.0f && worldX > 2.75f) { crease = fminf(crease, v); if (albedo) brightBounce = fmaxf(brightBounce, pixels[x][1] - v); else darkBounce = fminf(darkBounce, pixels[x][1] - v); }
                if (worldX < 2.5f && worldX > 2.2f) nearCrease = fmaxf(nearCrease, v);
            }
            // A plain wall far from anything is open; next to the crease the other wall hides part
            // of the hemisphere, and less so a little further out.
            assert(open < .02f);
            assert(crease < .85f && crease > .3f);
            assert(nearCrease > crease);
            if (albedo) assert(brightBounce > .02f); else assert(darkBounce < .02f);
        }
        // Bent normal: on the open wall it is the wall's normal; at the crease it leans away from
        // the side wall (toward -x), the way the open sky lies.
        r_glUniform1i(r_glGetUniformLocation(program, "testBent"), 1);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, SIZE / 2, SIZE, 1, GL_RGBA, GL_FLOAT, pixels);
        r_glUniform1i(r_glGetUniformLocation(program, "testBent"), 0);
        float openLean = 0, openFacing = 1, creaseLean = 1;
        for (int x = 0; x < SIZE; x++) {
            if (pixels[x][3] < 9.99f) continue;
            float worldX = pixels[x][2];
            if (worldX < 0.0f && worldX > -2.0f) { openLean = fmaxf(openLean, fabsf(pixels[x][0])); openFacing = fminf(openFacing, pixels[x][1]); }
            if (worldX < 3.0f && worldX > 2.75f) creaseLean = fminf(creaseLean, -pixels[x][0]);
        }
        printf("GTAO quality %d bent normal: open lean %.3f facing %.3f, crease lean %.3f\n", quality, openLean, openFacing, creaseLean);
        assert(openLean < .05f && openFacing > .98f && creaseLean > .1f);
        r_glDeleteProgram(program);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &depth); deleteTextures(1, &output); assert(!getError());
    puts("PASS: GTAO open wall unoccluded, inside corner occluded and fading with distance, bounded multi-bounce brighter for white materials, finite at low/medium/high quality");
}
