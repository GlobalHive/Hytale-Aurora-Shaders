// Renders the aurora over the whole night sky dome (fisheye, zenith in the middle, horizon at
// the rim) with the pack's real noise texture, checks the output is finite and bounded, and
// saves a preview to build/validation/sky-aurora.bmp.
static void sky_preview_save(const char *path, const float *pixels, int size, float exposure)
{
    FILE *file = fopen(path, "wb");
    if (!file) return;
    int rowBytes = size * 3, padded = (rowBytes + 3) & ~3, imageBytes = padded * size;
    unsigned char header[54] = {'B', 'M'};
    unsigned int fileSize = 54 + imageBytes;
    memcpy(header + 2, &fileSize, 4); header[10] = 54; header[14] = 40;
    memcpy(header + 18, &size, 4); memcpy(header + 22, &size, 4); header[26] = 1; header[28] = 24;
    memcpy(header + 34, &imageBytes, 4);
    fwrite(header, 1, 54, file);
    unsigned char *row = calloc(1, padded);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++)
            for (int channel = 0; channel < 3; channel++) {
                float value = pixels[(y * size + x) * 4 + (2 - channel)] * exposure;   // gamma space, like the sky pass
                value = fminf(fmaxf(value, 0.0f), 1.0f);
                row[x * 3 + channel] = (unsigned char)(value * 255.0f + 0.5f);
            }
        fwrite(row, 1, padded, file);
    }
    free(row); fclose(file);
}

static void sky_preview_test(void)
{
    enum { SIZE = 512 };
    size_t length = 0;
    unsigned char *raw = (unsigned char *)read_file("textures\\noise.raw", &length);
    assert(raw && length >= 12 && !memcmp(raw, "HYTX", 4));
    unsigned int width, height; memcpy(&width, raw + 4, 4); memcpy(&height, raw + 8, 4);
    GLuint noise; ov.GenTextures(1, &noise); r_glBindTexture(GL_TEXTURE_2D, noise);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, raw + 12);
    free(raw);
    void (APIENTRY *generateMipmaps)(GLenum) = (void *)real_wglGetProcAddress("glGenerateMipmap");
    generateMipmaps(GL_TEXTURE_2D);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x2901); ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x2901);
    ov.TexParameteri(GL_TEXTURE_2D, 0x84FE, 16);
    GLuint framebuffer, output;
    generateFramebuffer(1, &framebuffer); bindFramebuffer(0x8d40, framebuffer);
    ov.GenTextures(1, &output); r_glBindTexture(GL_TEXTURE_2D, output);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT, NULL);
    attachTexture(0x8d40, 0x8ce0, GL_TEXTURE_2D, output, 0);
    for (int effect = 0; effect < 1; effect++) {
        Str source = {0}; str_cstr(&source, "#version 410 core\n");
        expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
        str_cstr(&source, "\n#undef AU_AURORA\n#define AU_AURORA 1\n#undef AU_AURORA_STRENGTH\n#define AU_AURORA_STRENGTH 1.0\n"
            "#undef AU_AURORA_SPEED\n#define AU_AURORA_SPEED 1.0\n"
            "#undef AU_BIOME_ATMOSPHERE\n#define AU_BIOME_ATMOSPHERE 0\n");
        str_cstr(&source, "vec3 SunPositionWS,CameraPosition,AmbientFrontColor,AmbientBackColor;float AmbientIntensity,IsCameraUnderwater;"
            "vec4 SunLightColor,SunColor,FogTopColor,FogFrontColor,FogBackColor,FogParams,SceneBrightness;mat4 SunShadowMatrix[4];"
            "uniform float Time;in vec2 fragTexCoords;out vec4 outColor;\n");
        expand(&source, "#include \"AU_Sky.glsl\"\n#include \"AU_Aurora.glsl\"\n", 0);
        str_cstr(&source,
              "void main(){vec2 d=fragTexCoords*2-1;float r=length(d);if(r>1.){outColor=vec4(0);return;}"
              "float el=(1.-r)*1.5707963;vec3 dir=vec3(cos(el)*d.x/max(r,1e-5),sin(el),cos(el)*d.y/max(r,1e-5));"
              "SunPositionWS=normalize(vec3(.3,-.7,.2));CameraPosition=vec3(0,110,0);SunLightColor=vec4(.2,.2,.3,1);SunColor=SunLightColor;"
              "AmbientFrontColor=AmbientBackColor=vec3(.05);AmbientIntensity=.2;FogTopColor=FogFrontColor=FogBackColor=vec4(.02,.03,.08,1);"
              "FogParams=vec4(0,512,0,0);SceneBrightness=vec4(1);IsCameraUnderwater=0;for(int m=0;m<4;m++)SunShadowMatrix[m]=mat4(0);"
              "AU_InitCommon();vec3 sky=GetSky(dir.y,dot(dir,sunVec),.5,false,false);vec3 a=AU_GetAurora(dir,.5);"
              "outColor=vec4(sky+a,max(a.r,max(a.g,a.b)));}");
        GLuint program = test_program(source.p); free(source.p);
        r_glUseProgram(program); ov.BindVertexArray(ov.vao); ov.Viewport(0, 0, SIZE, SIZE);
        r_glUniform1f(r_glGetUniformLocation(program, "Time"), 300.0f);
        set_texture(program, "noisetex", 0, noise);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4);
        float *pixels = malloc(sizeof(float) * 4 * SIZE * SIZE);
        readPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_FLOAT, pixels);
        float peak = 0, coverage = 0;
        for (int i = 0; i < SIZE * SIZE; i++) {
            for (int channel = 0; channel < 4; channel++) assert(isfinite(pixels[i * 4 + channel]) && pixels[i * 4 + channel] >= 0);
            peak = fmaxf(peak, pixels[i * 4 + 3]);
            coverage += pixels[i * 4 + 3] > (effect ? 0.04f : 0.02f);
        }
        coverage /= (float)(SIZE * SIZE) * 0.785f;
        printf("Sky preview %s: peak %.3f, coverage %.1f%% of the dome\n", effect ? "veil" : "aurora", peak, coverage * 100.0f);
        sky_preview_save(effect ? "build\\validation\\sky-veil.bmp" : "build\\validation\\sky-aurora.bmp", pixels, SIZE, 1.0f);
        assert(peak > (effect ? 0.05f : 0.1f) && peak < (effect ? 0.23f : 2.5f));
        assert(coverage > (effect ? 0.01f : 0.02f) && coverage < (effect ? 0.4f : 0.65f));
        free(pixels);
        r_glDeleteProgram(program);
    }
    PFN_glDeleteTextures deleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures");
    deleteTextures(1, &output); deleteTextures(1, &noise); assert(!getError());
    puts("PASS: aurora finite, bounded and partial over the night sky dome (preview in build/validation)");
}
