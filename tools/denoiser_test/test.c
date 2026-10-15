#include "../../hook/hook.c"
#undef NDEBUG
#include <assert.h>

static void (APIENTRY *readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (APIENTRY *attachTexture)(GLenum, GLenum, GLenum, GLuint, GLint);
static void (APIENTRY *generateFramebuffer)(GLsizei, GLuint *);
static void (APIENTRY *bindFramebuffer)(GLenum, GLuint);
static unsigned (APIENTRY *getError)(void);

static GLuint test_program(const char *fragment)
{
    const char *vertex = "#version 410 core\nout vec2 fragTexCoords;void main(){vec2 corner=vec2(float(gl_VertexID&1),float(gl_VertexID>>1));fragTexCoords=corner;gl_Position=vec4(corner*2-1,0,1);}";
    GLuint program = ov.CreateProgram();
    const char *sources[] = {vertex, fragment};
    for (int index = 0; index < 2; index++) {
        GLuint shader = r_glCreateShader(index ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        r_glShaderSource(shader, 1, &sources[index], NULL); r_glCompileShader(shader);
        GLint valid; r_glGetShaderiv(shader, GL_COMPILE_STATUS, &valid);
        if (!valid) { char message[8192]; r_glGetShaderInfoLog(shader, sizeof(message), NULL, message); fprintf(stderr, "%s\n", message); }
        assert(valid); r_glAttachShader(program, shader); r_glDeleteShader(shader);
    }
    r_glLinkProgram(program);
    GLint linked; r_glGetProgramiv(program, GL_LINK_STATUS, &linked); assert(linked);
    return program;
}

static void set_texture(GLuint program, const char *name, GLuint unit, GLuint texture)
{
    r_glActiveTexture(GL_TEXTURE0 + unit); r_glBindTexture(GL_TEXTURE_2D, texture);
    r_glBindSampler(unit, 0); r_glUniform1i(r_glGetUniformLocation(program, name), unit);
}

static void original_noise_test(void)
{
    const int width = 128, height = 128;
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_VOXELS 1\n#define AU_PT_LIGHTS 0\n#define AU_GI_SPATIAL_FILTER 1\n#define AU_PT_HISTORY 24.0\n#define AU_GI_MAX_HISTORY .92\n"
        "#define AU_GI_HISTORY_TAPS 9\n#define AU_GI_SPATIAL_STRENGTH 1.0\n#define AU_GI_NORMAL_THRESHOLD .05\n"
        "#define AU_GI_PLANE_TOLERANCE .03\n#define AU_GI_HISTORY_REACH .3\n#define AU_GI_LIGHT_REJECTION .25\n#define AU_NEAR_IGNORE 1.2\n"
        "uniform mat4 ProjectionMatrix,ReprojectionMatrix,ViewMatrix;uniform vec3 CameraPosition;uniform vec2 ViewportSize;uniform int testFrame;"
        "in vec2 fragTexCoords;out vec4 outColor;float pow2(float value){return value*value;}\n");
    expand(&source, "#include \"AU_DenoiseLegacy.glsl\"\n", 0);
    str_cstr(&source, "void main(){float depth=testFrame>=68&&all(lessThan(abs(fragTexCoords-.5),vec2(.15)))?5.0:10.0;"
        "vec3 position=AU_ViewPosition(fragTexCoords,depth);uint seed=uint(gl_FragCoord.x)+uint(gl_FragCoord.y)*128u+uint(testFrame)*16384u;"
        "float irradiance=0.0;for(int ray=0;ray<3;ray++){seed=seed*1664525u+1013904223u;uint bits=seed;bits^=bits>>16;bits*=0x7feb352du;bits^=bits>>15;irradiance+=float((bits&255u)<32u)/3.0;}"
        "vec3 gi=testFrame>=64?vec3(.6):vec3(irradiance);if(depth==5.0)gi=vec3(.2);"
        "vec3 direct=vec3(0);float ao=1.0-irradiance,sky=irradiance;"
        "AU_GIQuadFilter(position,vec3(0,0,1),false,gi,ao,sky,direct);"
        "AU_GIAccumulate(fragTexCoords,position,vec3(0,0,1),gi,ao,sky,direct,testFrame>=64?2.0:1.0);outColor=vec4(gi,ao);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, width, height);
    float projection[16] = {0};
    projection[0] = 1.2f; projection[5] = 1.6f; projection[10] = -1.002f; projection[11] = -1; projection[14] = -.2f;
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ProjectionMatrix"), 1, 0, projection);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "uHookCurrentProjection"), 1, 0, projection);
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "uHookPreviousViewProjection"), 1, 0, projection);
    float identity[16] = {0};
    for (int axis = 0; axis < 4; axis++) identity[axis * 5] = 1;
    r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ViewMatrix"), 1, 0, identity);
    r_glUniform2f(r_glGetUniformLocation(program, "ViewportSize"), width, height);
    r_glUniform1i(r_glGetUniformLocation(program, "auGIOut"), GI_IMG_RAD);
    r_glUniform1i(r_glGetUniformLocation(program, "auGIOutPos"), GI_IMG_POS);
    r_glUniform1i(r_glGetUniformLocation(program, "auGIOutLight"), GI_IMG_LIGHT);
    float pixels[64 * 64 * 4];
    double stationaryVariance = 0;
    for (int frame = 0; frame < 69; frame++) {
        g_frame = frame; gi_bind(1, 0, 1, 0, 0);
        assert(!g_giTex[0][2] && !g_giTex[1][2] && !g_giRawTex[0]);
        r_glUniform1i(r_glGetUniformLocation(program, "testFrame"), frame);
        r_glUniform1i(r_glGetUniformLocation(program, "uHookHistoryValid"), frame != 0);
        float camera = frame >= 32 ? (frame - 31) * .15f : 0;
        float previous = frame > 32 ? (frame - 32) * .15f : 0;
        float reprojection[16]; memcpy(reprojection, projection, sizeof(reprojection));
        reprojection[12] = projection[0] * (camera - previous);
        r_glUniformMatrix4fv(r_glGetUniformLocation(program, "ReprojectionMatrix"), 1, 0, reprojection);
        r_glUniform3f(r_glGetUniformLocation(program, "CameraPosition"), camera, 0, 0);
        r_glUniform3f(r_glGetUniformLocation(program, "uHookPreviousCamera"), previous, 0, 0);
        set_texture(program, "auGIHist", GI_TEX_RAD, g_giTex[(frame & 1) ^ 1][0]);
        set_texture(program, "auGIHistPos", GI_TEX_POS, g_giTex[(frame & 1) ^ 1][1]);
        set_texture(program, "auGIHistLight", GI_TEX_LIGHT, g_giTex[(frame & 1) ^ 1][2]);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); r_glMemoryBarrier(0xffffffffu);
        if (frame == 31 || frame == 63 || frame == 64) {
            readPixels(32, 32, 64, 64, GL_RGBA, GL_FLOAT, pixels);
            double mean = 0, square = 0;
            for (int pixel = 0; pixel < 64 * 64; pixel++) {
                mean += pixels[pixel * 4]; square += pixels[pixel * 4] * pixels[pixel * 4];
            }
            mean /= 4096; square = square / 4096 - mean * mean;
            printf("Original frame %d sparse-light mean %.6f variance %.8f\n", frame, mean, square);
            assert(fabs(mean - (frame == 64 ? .6 : .125)) < .01);
            if (frame == 31) stationaryVariance = square;
            if (frame == 63) assert(square < stationaryVariance * 2.0 && square < .002);
        }
        if (frame == 68) {
            float center[4]; readPixels(64, 64, 1, 1, GL_RGBA, GL_FLOAT, center);
            assert(fabsf(center[0] - .2f) < .001f);
        }
    }
    gi_release(); r_glDeleteProgram(program);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output);
    assert(!getError());
    puts("PASS: Original preserves sparse Monte Carlo lighting during motion, rejects changed lighting and new foreground geometry");
}

static void local_light_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_GI_DENOISE 1\n#define AU_LOCAL_LIGHT_CANDIDATES 8\n#define AU_LOCAL_LIGHT_RANGE 8.0\n"
        "#define AU_LOCAL_LIGHT_STRENGTH 1.0\n#define AU_EMISSIVE_LIGHT_STRENGTH testEmitterStrength\n#define AU_LOCAL_LIGHT_SOFTNESS testSoftness\n"
        "uniform int uHookFrame,testCull,testOriginal;uniform vec3 CameraPosition,testPosition;uniform float testSoftness,testEmitterStrength;"
        "out vec4 outColor;float pow2(float value){return value*value;}"
        "float max0(float value){return max(value,0.0);}float GetLuminance(vec3 value){return dot(value,vec3(.299,.587,.114));}\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){if(testCull!=0){AU_BuildLocalLightTile();discard;}"
        "vec2 randomValue=fract(vec2(dot(gl_FragCoord.xy,vec2(.618034,.569840)),dot(gl_FragCoord.xy,vec2(.438579,.754878)))+vec2(.37,.19));"
        "vec3 lighting=testOriginal!=0?AU_LocalLightingOriginal(testPosition,vec3(0,0,1),false,randomValue)"
        ":AU_LocalLighting(testPosition,vec3(0,0,1),false,randomValue);outColor=vec4(lighting,1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 64, 64);
    r_glUniform1i(r_glGetUniformLocation(program, "auLights"), VOX_UNIT_LIGHTS);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    r_glUniform1f(r_glGetUniformLocation(program, "testEmitterStrength"), 1.0f);
    g_setVoxelColors = 0;
    vox_create(); assert(g_voxState == 1 && !g_voxTex[1]);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glBindImageTexture(VOX_UNIT_LIGHTS, g_voxLights, 0, 0, 0, GL_READ_WRITE, GL_R32UI);
    void (APIENTRY *update2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)GetProcAddress(g_real, "glTexSubImage2D");
    void (APIENTRY *update3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)real_wglGetProcAddress("glTexSubImage3D");
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    uint32_t light[3] = {1, 2u << 15, 255u};
    float pixels[64 * 64 * 4], torchValue = 0.0f;
    for (int step = 0; step < 4; step++) {
        light[2] = step == 1 ? 77u : 255u;
        r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
        update2D(GL_TEXTURE_2D, 0, 0, 0, 3, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, light);
        if (step == 2) {
            uint32_t blocker = 2u;
            r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
            update3D(GL_TEXTURE_3D, 0, 0, 0, 1, 1, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &blocker);
        }
        r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), step == 3 ? -1.2f : 0.0f, .5f, 0.0f);
        r_glUniform1f(r_glGetUniformLocation(program, "testSoftness"), step == 3 ? .35f : 0.0f);
        r_glMemoryBarrier(0xffffffffu);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
        double red = 0.0;
        for (int pixel = 0; pixel < 4096; pixel++) {
            red += pixels[pixel * 4];
            assert(fabsf(pixels[pixel * 4 + 1]) < .00001f && fabsf(pixels[pixel * 4 + 2]) < .00001f);
        }
        red /= 4096;
        printf("Local light step %d mean %.6f\n", step, red);
        if (step == 0) { torchValue = (float)red; assert(torchValue > .05f); }
        if (step == 1) assert(fabs(red / torchValue - 77.0 / 255.0) < .001);
        if (step == 2) assert(red < .00001);
        if (step == 3) assert(red > .01 && red < torchValue);
    }
    uint32_t emptyCell = 0;
    r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
    update3D(GL_TEXTURE_3D, 0, 0, 0, 1, 1, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &emptyCell);
    uint32_t crowdedLights[1025] = {512};
    for (int index = 0; index < 512; index++) {
        crowdedLights[1 + index * 2] = (uint32_t)(64 + index % 64) | (32u << 8) | ((uint32_t)(64 + index / 64) << 15);
        crowdedLights[2 + index * 2] = 255u;
    }
    crowdedLights[1] = 2u << 15;
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 1025, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, crowdedLights);
    r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), 0, .5f, 0);
    r_glUniform1f(r_glGetUniformLocation(program, "testSoftness"), 0);
    double sampledVariance = 0.0;
    for (int original = 0; original < 2; original++) {
        r_glUniform1i(r_glGetUniformLocation(program, "testOriginal"), original);
        if (original) {
            r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 1);
            ov.Viewport(0, 0, 32, 64);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 0);
        }
        ov.Viewport(0, 0, 64, 64);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
        double mean = 0, variance = 0;
        for (int pixel = 0; pixel < 4096; pixel++) {
            mean += pixels[pixel * 4]; variance += pixels[pixel * 4] * pixels[pixel * 4];
        }
        mean /= 4096; variance = variance / 4096 - mean * mean;
        printf("Crowded-scene %s mean %.6f variance %.8f\n", original ? "Original" : "NVIDIA input", mean, variance);
        if (!original) { sampledVariance = variance; assert(sampledVariance > .1); }
        else { assert(fabs(mean - torchValue) < .00001 && variance < .000001); }
    }
    r_glUniform1i(r_glGetUniformLocation(program, "testOriginal"), 1);
    crowdedLights[1] = (uint32_t)(-17 & 255) | ((uint32_t)(-1 & 127) << 8) | ((uint32_t)(-15 & 255) << 15);
    crowdedLights[2] = 77u;
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 1025, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, crowdedLights);
    r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), -17.0f, -.5f, -17.0f);
    r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 1);
    ov.Viewport(0, 0, 32, 64);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 0);
    ov.Viewport(0, 0, 64, 64);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    float negativeCell[4];
    readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, negativeCell);
    assert(fabsf(negativeCell[0] / torchValue - 77.0f / 255.0f) < .001f);
    r_glUniform1f(r_glGetUniformLocation(program, "testEmitterStrength"), 1.75f);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    float boostedOre[4]; readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, boostedOre);
    assert(fabsf(boostedOre[0] - negativeCell[0]) < .00001f);
    r_glUniform1f(r_glGetUniformLocation(program, "testEmitterStrength"), 1.0f);
    uint32_t bulbs[7] = {3};
    for (int bulb = 0; bulb < 3; bulb++) {
        bulbs[1 + bulb * 2] = (2u << 15) | ((uint32_t)(bulb + 1) << 23);
        bulbs[2 + bulb * 2] = 255u << (bulb * 8);
    }
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    update2D(GL_TEXTURE_2D, 0, 0, 0, 7, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, bulbs);
    r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), 1.0f / 6.0f, 1.0f / 6.0f, 1.95f);
    for (int original = 0; original < 2; original++) {
        r_glUniform1i(r_glGetUniformLocation(program, "testOriginal"), original);
        if (original) {
            r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 1);
            ov.Viewport(0, 0, 32, 64);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            r_glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            r_glUniform1i(r_glGetUniformLocation(program, "testCull"), 0);
        }
        ov.Viewport(0, 0, 64, 64);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
        double channels[3] = {0}, squares[3] = {0};
        for (int pixel = 0; pixel < 4096; pixel++)
            for (int component = 0; component < 3; component++) {
                double value = pixels[pixel * 4 + component];
                channels[component] += value / 4096.0;
                squares[component] += value * value / 4096.0;
            }
        printf("Adjacent bulbs %s RGB %.4f %.4f %.4f\n", original ? "Original" : "NVIDIA input", channels[0], channels[1], channels[2]);
        assert(channels[0] > channels[1] * 1.8 && channels[0] > channels[2] * 4.0 && channels[2] > 0);
        for (int component = 0; component < 3; component++)
            assert(fabs(squares[component] - channels[component] * channels[component]) < .00001);
        const float strengths[] = {0.0f, 1.75f};
        for (int setting = 0; setting < 2; setting++) {
            r_glUniform1f(r_glGetUniformLocation(program, "testEmitterStrength"), strengths[setting]);
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            float pixel[4]; readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, pixel);
            for (int component = 0; component < 3; component++)
                assert(fabs(pixel[component] - channels[component] * strengths[setting]) < .0001);
        }
        r_glUniform1f(r_glGetUniformLocation(program, "testEmitterStrength"), 1.0f);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(2, g_voxTex); deleteTextures(1, &g_voxLights); deleteTextures(1, &output);
    memset(g_voxTex, 0, sizeof(g_voxTex)); g_voxLights = 0; g_voxState = 0;
    assert(!getError()); r_glDeleteProgram(program);
    puts("PASS: source colors, ore/torch ratio, shadows, penumbra, crowded-scene Original noise rejection, negative cell boundaries, and no unused color grid");
}

static void emitter_capture_test(int nextFrame)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_PT_LIGHTS 0\n#define AU_VOX_WRITER_ONLY\n");
    if (nextFrame) str_cstr(&source, "#define AU_EMITTER_NEXT_FRAME\n");
    str_cstr(&source, "uniform vec3 CameraPosition,testColor,testPosition;uniform int testFine;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){if(testFine!=0)AU_RecordEmitterAt(testPosition,testColor,true);"
        "else AU_RecordEmitter(ivec3(-1,0,-2),vec3(.1,.8,.2));outColor=vec4(0,0,0,1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 64, 64);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    g_setVoxelColors = 0; vox_create();
    assert(g_voxState == 1 && !g_voxTex[1]);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glBindImageTexture(VOX_UNIT_LIGHTS, g_voxLights, 0, 0, 0, GL_READ_WRITE, GL_R32UI);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    r_glUniform1i(r_glGetUniformLocation(program, "auLights"), VOX_UNIT_LIGHTS);
    void (APIENTRY *download)(GLenum, GLint, GLenum, GLenum, void *) = (void *)GetProcAddress(g_real, "glGetTexImage");
    assert(download);
    for (int frame = 37; frame <= 38; frame++) {
        r_glUniform1i(r_glGetUniformLocation(program, "uHookFrame"), frame);
        r_glUniform1f(r_glGetUniformLocation(program, "uHookTime"), ((float)frame + .25f) / 1000.0f);
        for (int draw = 0; draw < 2; draw++) {
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            r_glMemoryBarrier(0xffffffffu);
        }
        uint32_t records[VOX_LIGHTS_W * VOX_LIGHTS_H];
        r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
        download(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, records);
        int active = 0;
        for (int slot = 0; slot < 1024; slot++) {
            if (!records[3 * VOX_LIGHTS_W + slot]) continue;
            active++;
            assert((records[1 + slot * 2] & 0x7fffffu) == (255u | (254u << 15)));
            uint32_t color = records[2 + slot * 2];
            assert((color & 255u) >= 25 && (color & 255u) <= 26);
            assert(((color >> 8) & 255u) == 204 && ((color >> 16) & 255u) == 51);
            assert(records[2 * VOX_LIGHTS_W + slot] == (uint32_t)(frame + 1));
        }
        assert(active == 1);
    }
    const int frames[] = {39, 63, 64, 71, 65535, 65536};
    const float positions[3][3] = {{-.83f, .17f, -1.83f}, {-.5f, .5f, -1.5f}, {-.17f, .83f, -1.17f}};
    int fineSlot = -1, genericSlot = -1;
    for (int step = 0; step < 6; step++) {
        r_glUniform1i(r_glGetUniformLocation(program, "uHookFrame"), frames[step]);
        r_glUniform1f(r_glGetUniformLocation(program, "uHookTime"), ((float)frames[step] + .25f) / 1000.0f);
        for (int draw = 0; draw < 6; draw++) {
            int component = draw == 4 ? 1 : draw - 1;
            r_glUniform1i(r_glGetUniformLocation(program, "testFine"), draw > 0 && draw < 5);
            if (component >= 0 && component < 3) {
                float brightness = draw == 4 ? .4f : (component == 0 ? .8f : 1.0f);
                r_glUniform3f(r_glGetUniformLocation(program, "testColor"), component == 0 ? brightness : 0,
                    component == 1 ? brightness : 0, component == 2 ? brightness : 0);
                r_glUniform3f(r_glGetUniformLocation(program, "testPosition"), positions[component][0], positions[component][1], positions[component][2]);
            }
            r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
            r_glMemoryBarrier(0xffffffffu);
        }
        uint32_t records[VOX_LIGHTS_W * VOX_LIGHTS_H];
        r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
        download(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, records);
        int active = 0, found = 0;
        for (int light = 0; light < 1024; light++) {
            if (!records[3 * VOX_LIGHTS_W + light]) continue;
            active++;
            uint32_t position = records[1 + light * 2];
            uint32_t color = records[2 + light * 2];
            assert((position & 0x7fffffu) == (255u | (254u << 15)));
            uint32_t offset = (position >> 23) & 31u;
            if (!offset) { genericSlot = light; continue; }
            int component = (int)(offset - 1) / 13;
            assert(component >= 0 && component < 3 && offset == (uint32_t)(1 + component * 13));
            assert((color & 0xffffffu) == (component == 0 ? 204u : 255u << (component * 8)));
            assert(records[2 * VOX_LIGHTS_W + light] == (uint32_t)(frames[step] + 1));
            found |= 1 << component;
            fineSlot = light;
        }
        assert(active == 4 && found == 7 && fineSlot >= 0 && genericSlot >= 0);
    }
    Str reader = {0};
    str_cstr(&reader, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_LOCAL_LIGHTS 1\n#define AU_LOCAL_LIGHT_RANGE 8.0\n#define AU_LOCAL_LIGHT_CANDIDATES 8\n"
        "#define AU_LOCAL_LIGHT_SOFTNESS 0.0\n#define AU_LOCAL_LIGHT_STRENGTH 1.0\n#define AU_EMISSIVE_LIGHT_STRENGTH 1.0\n"
        "uniform int uHookFrame,testIndex;uniform vec3 CameraPosition;out vec4 outColor;"
        "float pow2(float value){return value*value;}\n");
    expand(&reader, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&reader, "void main(){uint position=imageLoad(auLights,ivec2(1+2*testIndex,0)).r;"
        "outColor=vec4(vec3(AU_CachedLightWeight(uint(testIndex),position)),1);}");
    GLuint readerProgram = test_program(reader.p); free(reader.p);
    r_glUseProgram(readerProgram);
    r_glUniform1i(r_glGetUniformLocation(readerProgram, "auVoxGeo"), VOX_UNIT_GEO);
    r_glUniform1i(r_glGetUniformLocation(readerProgram, "auLights"), VOX_UNIT_LIGHTS);
    const int ages[] = {1, 750, 875, 1000, 1, 1};
    const float expected[] = {1, 1, .5f, 0, 0, 0};
    for (int step = 0; step < 6; step++) {
        r_glUniform1i(r_glGetUniformLocation(readerProgram, "uHookFrame"), 65536 + ages[step]);
        r_glUniform1f(r_glGetUniformLocation(readerProgram, "uHookTime"), ((float)(65536 + ages[step]) + .25f) / 1000.0f);
        r_glUniform1i(r_glGetUniformLocation(readerProgram, "testIndex"), step == 5 ? genericSlot : fineSlot);
        r_glUniform3f(r_glGetUniformLocation(readerProgram, "CameraPosition"), step == 4 ? 256.0f : 0.0f, 0, 0);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        float pixel[4]; readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        assert(fabsf(pixel[0] - expected[step]) < .001f);
    }
    vox_clear();
    uint32_t cleared[VOX_LIGHTS_W * VOX_LIGHTS_H];
    r_glBindTexture(GL_TEXTURE_2D, g_voxLights);
    download(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, cleared);
    for (int texel = 0; texel < VOX_LIGHTS_W * VOX_LIGHTS_H; texel++) assert(cleared[texel] == 0);
    r_glDeleteProgram(readerProgram);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(2, g_voxTex); deleteTextures(1, &g_voxLights); deleteTextures(1, &output);
    memset(g_voxTex, 0, sizeof(g_voxTex)); g_voxLights = 0; g_voxState = 0;
    r_glDeleteProgram(program); assert(!getError());
    printf("PASS: %s persistent RGB cache, concurrent deduplication, screen-edge retention, expiry, wrap rejection, native-fill suppression and reset\n", nextFrame ? "transparent" : "opaque");
}

static void native_light_colour_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#define AU_LOCAL_LIGHT_COLOR_BLEND 1.0\n"
        "uniform vec3 testNative,testIrradiance;uniform float testEnergy;out vec4 outColor;\n");
    expand(&source, "#include \"AU_LightColour.glsl\"\n", 0);
    str_cstr(&source, "void main(){if(testEnergy<0){vec3 signal=(int(gl_FragCoord.x)&7)==0?vec3(8,0,0):vec3(0);"
        "vec3 color=AU_AddIndirectLight(vec3(.4),vec3(.5),signal,true);outColor=vec4(pow(color,vec3(2.2)),1);}"
        "else outColor=vec4(AU_BlendLocalLightColour(testNative,testIrradiance,testEnergy),1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 64, 64);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    for (int step = 0; step < 5; step++) {
        r_glUniform3f(r_glGetUniformLocation(program, "testNative"), step == 2 ? 0 : .5f, step == 2 ? 0 : .4f, step == 2 ? 0 : .3f);
        r_glUniform3f(r_glGetUniformLocation(program, "testIrradiance"), step == 3 ? 0 : .01f,
            step == 3 ? 0 : step == 4 ? .01f : 1.0f, step == 3 ? 0 : step == 4 ? 2.0f : .01f);
        r_glUniform1f(r_glGetUniformLocation(program, "testEnergy"), step == 1 ? 0 : 1);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        float pixel[4]; readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        if (step == 0) assert(fabsf(pixel[1] - .5f) < .001f && pixel[0] < .06f && pixel[2] < .06f);
        if (step == 1 || step == 3) assert(fabsf(pixel[0] - .5f) < .001f && fabsf(pixel[1] - .4f) < .001f && fabsf(pixel[2] - .3f) < .001f);
        if (step == 2) assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);
        if (step == 4) assert(fabsf(pixel[2] - .5f) < .001f && pixel[0] < .04f && pixel[1] < .04f);
    }
    r_glUniform1f(r_glGetUniformLocation(program, "testEnergy"), -1.0f);
    r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
    float pixels[64 * 64 * 4]; readPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, pixels);
    double channels[3] = {0};
    for (int pixel = 0; pixel < 4096; pixel++)
        for (int component = 0; component < 3; component++) channels[component] += pixels[pixel * 4 + component] / 4096.0;
    double base = pow(.4, 2.2), reflected = pow(.5, 2.2);
    assert(fabs(channels[0] - base - reflected) < .0001 && fabs(channels[1] - base) < .0001 && fabs(channels[2] - base) < .0001);
    printf("RR sparse-light HDR energy %.6f %.6f %.6f (expected %.6f %.6f %.6f)\n", channels[0], channels[1], channels[2], base + reflected, base, base);
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); r_glDeleteProgram(program); assert(!getError());
    puts("PASS: native fill hue correction and unbiased linear-energy RR transport without gamma fireflies");
}

static void subsurface_depth_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
        "#define AU_VOX_WRITER_ONLY\n#define AU_LOCAL_LIGHTS 0\n#define AU_PT_LIGHTS 0\n#define AU_SSS 1\n#define AU_SSS_VOXEL 1\n"
        "#define AU_SSS_TRACE_DISTANCE 12.0\n#define AU_SSS_TRACE_STEPS 32\n#define AU_SSS_DENSITY .55\n#define AU_SSS_THICKNESS 1.0\n#define AU_SSS_ABSORPTION 1.0\n"
        "uniform vec3 CameraPosition,testOrigin,testTint;uniform int testMedium;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Voxel.glsl\"\n", 0);
    str_cstr(&source, "void main(){float depth;vec3 scattering;vec4 transport=AU_TraceSubsurface(testOrigin,vec3(1,0,0),testTint,testMedium,depth,scattering);"
        "outColor=vec4(transport.rgb*(vec3(1)+scattering*.5),transport.a>.5?depth:-1.0);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 64, 64);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 64, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    g_setVoxelColors = 0; vox_create(); assert(g_voxState == 1 && !g_voxTex[1]);
    r_glBindImageTexture(VOX_UNIT_GEO, g_voxTex[0], 0, 1, 0, GL_READ_WRITE, GL_R32UI);
    r_glUniform1i(r_glGetUniformLocation(program, "auVoxGeo"), VOX_UNIT_GEO);
    void (APIENTRY *update)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *) = (void *)real_wglGetProcAddress("glTexSubImage3D");
    float singleLayer = 0.0f;
    for (int step = 0; step < 8; step++) {
        uint32_t cells[16] = {0};
        if (step <= 4) cells[0] = 3;
        if (step == 1) cells[1] = cells[2] = cells[3] = 3;
        if (step == 2) cells[2] = 3;
        if (step == 3) cells[1] = 2;
        if (step == 4) cells[0] = 2 | 0x80;
        r_glBindTexture(GL_TEXTURE_3D, g_voxTex[0]);
        update(GL_TEXTURE_3D, 0, 0, 0, 0, 16, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, cells);
        r_glUniform3f(r_glGetUniformLocation(program, "testOrigin"), step == 5 ? -2.9f : step == 7 ? 127.1f : .1f, .5f, .5f);
        r_glUniform3f(r_glGetUniformLocation(program, "testTint"), .35f, step == 4 ? .7f : 1.0f, step == 4 ? 1.0f : .2f);
        r_glUniform1i(r_glGetUniformLocation(program, "testMedium"), step == 4 ? 3 : 2);
        if (step == 5) {
            uint32_t negative[2] = {3u | (3u << 8), 3u | (3u << 8)};
            update(GL_TEXTURE_3D, 0, 253, 0, 0, 2, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, negative);
        }
        if (step == 7) {
            uint32_t boundary = 3;
            update(GL_TEXTURE_3D, 0, 127, 0, 0, 1, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &boundary);
        }
        r_glMemoryBarrier(0xffffffffu);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        float pixel[4]; readPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, pixel);
        printf("SSS case %d RGB %.5f %.5f %.5f thickness %.5f\n", step, pixel[0], pixel[1], pixel[2], pixel[3]);
        for (int component = 0; component < 3; component++) assert(pixel[component] >= 0.0f && pixel[component] <= 1.0001f);
        if (step == 0) { assert(fabsf(pixel[3] - .9f) < .001f && pixel[1] > pixel[0] && pixel[1] > pixel[2]); singleLayer = pixel[1]; }
        if (step == 1) assert(fabsf(pixel[3] - 3.9f) < .001f && pixel[1] < singleLayer);
        if (step == 2 || step == 5) assert(fabsf(pixel[3] - 1.9f) < .001f);
        if (step == 3) assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] >= 0);
        if (step == 4) assert(fabsf(pixel[3] - .9f) < .001f && pixel[2] > pixel[1] && pixel[1] > pixel[0]);
        if (step == 6 || step == 7) assert(pixel[3] < 0);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(2, g_voxTex); deleteTextures(1, &g_voxLights); deleteTextures(1, &output);
    memset(g_voxTex, 0, sizeof(g_voxTex)); g_voxLights = 0; g_voxState = 0;
    r_glDeleteProgram(program); assert(!getError());
    puts("PASS: measured foliage/ice depth, gaps, RGB scattering accumulation, opaque blockers, negative coordinates and grid coverage");
}

static void tonemap_domain_test(void)
{
    Str source = {0};
    str_cstr(&source, "#version 410 core\nuniform vec3 testHDR;out vec4 outColor;\n");
    expand(&source, "#include \"AU_Tonemap.glsl\"\n", 0);
    str_cstr(&source, "void main(){vec3 color=testHDR;DoTonemap(color);DoBSLColorSaturation(color);outColor=vec4(clamp(color,0,1),1);}");
    GLuint program = test_program(source.p); free(source.p);
    r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, 16, 16);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 16, 16, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    const float colors[][3] = {{2, -.00001f, 0}, {-.003f, 1.25f, -.002f}, {.05f, -.0001f, 3},
        {0, 0, 0}, {2, .1f, .1f}, {65504, 0, 0}, {-1, -.1f, 0}};
    for (int sample = 0; sample < 7; sample++) {
        float actual[4], expected[4];
        r_glUniform3f(r_glGetUniformLocation(program, "testHDR"), colors[sample][0], colors[sample][1], colors[sample][2]);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(8, 8, 1, 1, GL_RGBA, GL_FLOAT, actual);
        r_glUniform3f(r_glGetUniformLocation(program, "testHDR"), fmaxf(colors[sample][0], 0),
            fmaxf(colors[sample][1], 0), fmaxf(colors[sample][2], 0));
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        readPixels(8, 8, 1, 1, GL_RGBA, GL_FLOAT, expected);
        printf("HDR tonemap case %d actual %.5f %.5f %.5f expected %.5f %.5f %.5f\n", sample,
            actual[0], actual[1], actual[2], expected[0], expected[1], expected[2]);
        for (int channel = 0; channel < 3; channel++) {
            assert(actual[channel] >= 0.0f && actual[channel] <= 1.0f);
            assert(fabsf(actual[channel] - expected[channel]) < .00001f);
        }
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); r_glDeleteProgram(program); assert(!getError());
    puts("PASS: negative reconstructed HDR channels cannot turn saturated emissives into black or nonfinite pixels");
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 2); setvbuf(stdout, NULL, _IONBF, 0); g_log = stdout; wcscpy(g_packDir, arguments[1]);
    WNDCLASSW windowClass = {0}; windowClass.style = CS_OWNDC; windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(NULL); windowClass.lpszClassName = L"AuroraDenoiserTest"; RegisterClassW(&windowClass);
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Aurora denoiser GPU test", WS_POPUP, 0, 0, 16, 16, NULL, NULL, windowClass.hInstance, NULL);
    HDC windowDC = GetDC(window);
    PIXELFORMATDESCRIPTOR descriptor = {sizeof(descriptor), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    assert(SetPixelFormat(windowDC, ChoosePixelFormat(windowDC, &descriptor), &descriptor));
    wchar_t systemPath[MAX_PATH]; GetSystemDirectoryW(systemPath, MAX_PATH); wcscat(systemPath, L"\\opengl32.dll");
    g_real = LoadLibraryW(systemPath); assert(g_real);
    real_wglGetProcAddress = (PFN_wglGetProcAddress)GetProcAddress(g_real, "wglGetProcAddress");
    HGLRC (WINAPI *createContext)(HDC) = (void *)GetProcAddress(g_real, "wglCreateContext");
    BOOL (WINAPI *makeCurrent)(HDC, HGLRC) = (void *)GetProcAddress(g_real, "wglMakeCurrent");
    HGLRC context = createContext(windowDC); assert(context && makeCurrent(windowDC, context));
    r_glGetIntegerv = (void *)GetProcAddress(g_real, "glGetIntegerv"); r_glBindTexture = (void *)GetProcAddress(g_real, "glBindTexture");
    r_glDrawArrays = (void *)GetProcAddress(g_real, "glDrawArrays");
#define TEST_GL(name) r_##name = (void *)real_wglGetProcAddress(#name); assert(r_##name)
    TEST_GL(glCreateShader); TEST_GL(glShaderSource); TEST_GL(glCompileShader); TEST_GL(glGetShaderiv); TEST_GL(glGetShaderInfoLog);
    TEST_GL(glDeleteShader); TEST_GL(glAttachShader); TEST_GL(glLinkProgram); TEST_GL(glGetProgramiv); TEST_GL(glDeleteProgram);
    TEST_GL(glGetUniformLocation); TEST_GL(glUseProgram); TEST_GL(glUniformMatrix4fv); TEST_GL(glUniform1i); TEST_GL(glUniform2f);
    TEST_GL(glUniform3f); TEST_GL(glBindSampler); TEST_GL(glActiveTexture);
    TEST_GL(glUniform1f);
#undef TEST_GL
    assert(ov_init()); readPixels = (void *)GetProcAddress(g_real, "glReadPixels"); getError = (void *)GetProcAddress(g_real, "glGetError");
    generateFramebuffer = (void *)real_wglGetProcAddress("glGenFramebuffers"); bindFramebuffer = (void *)real_wglGetProcAddress("glBindFramebuffer");
    attachTexture = (void *)real_wglGetProcAddress("glFramebufferTexture2D");
    original_noise_test();
    local_light_test();
    emitter_capture_test(0);
    emitter_capture_test(1);
    subsurface_depth_test();
    native_light_colour_test();
    tonemap_domain_test();
    puts("PASS: Original moving-camera denoising and geometry/light rejection");
    return 0;
}
