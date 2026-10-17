#include "../../hook/hook.c"
#undef NDEBUG
#include <assert.h>

static GLenum (APIENTRY *testError)(void);
static void (APIENTRY *testRead)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (APIENTRY *testClear)(GLenum, GLint, const GLfloat *);
static void (APIENTRY *testScissor)(GLint, GLint, GLsizei, GLsizei);
static void (APIENTRY *testDeleteTextures)(GLsizei, const GLuint *);
static int testFailures;

static GLuint test_program(const char *fragment)
{
    const char *vertex = "#version 410 core\nuniform int testFlip;out vec2 fragTexCoords;void main(){"
        "vec2 corner=vec2(float(gl_VertexID&1),float(gl_VertexID>>1));fragTexCoords=corner;"
        "if(testFlip!=0)fragTexCoords.y=1-fragTexCoords.y;gl_Position=vec4(corner*2-1,0,1);}";
    const char *sources[] = {vertex, fragment};
    GLuint program = ov.CreateProgram();
    for (int stage = 0; stage < 2; stage++) {
        GLuint shader = r_glCreateShader(stage ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        r_glShaderSource(shader, 1, &sources[stage], NULL); r_glCompileShader(shader);
        GLint compiled = 0; r_glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) { char message[8192]; r_glGetShaderInfoLog(shader, sizeof message, NULL, message); puts(message); }
        assert(compiled); r_glAttachShader(program, shader); r_glDeleteShader(shader);
    }
    r_glLinkProgram(program);
    GLint linked = 0; r_glGetProgramiv(program, GL_LINK_STATUS, &linked); assert(linked);
    return program;
}

static GLuint test_texture(int width, int height)
{
    GLuint texture; ov.GenTextures(1, &texture); r_glBindTexture(GL_TEXTURE_2D, texture);
    ov.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ov.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ov.TexParameteri(GL_TEXTURE_2D, 0x2802, 0x812f);
    ov.TexParameteri(GL_TEXTURE_2D, 0x2803, 0x812f);
    return texture;
}

static void capture_test(const char *postEffect)
{
    const int width = 64, height = 48;
    const char *start = strstr(postEffect, "        ivec2 inputSize = imageSize(auDlssColor);");
    const char *end = start ? strstr(start, "        // The finished frame") : NULL;
    assert(start && end);
    Str source = {0};
    str_cstr(&source, "#version 410 core\n#extension GL_ARB_shader_image_load_store:enable\n"
        "#extension GL_ARB_shader_image_size:enable\nlayout(rgba16f)uniform writeonly image2D auDlssColor;"
        "uniform vec2 uPixelSize;uniform vec4 uHookDlssViewport;in vec2 fragTexCoords;out vec4 outColor;"
        "void main(){outColor=vec4(1);\n");
    str_add(&source, start, (size_t)(end - start));
    str_cstr(&source, "imageStore(auDlssColor,pix,vec4(.25+fragTexCoords*.5,.75,1));}");
    GLuint program = test_program(source.p); free(source.p); r_glUseProgram(program);
    GLuint framebuffer, textures[] = {test_texture(96, 80), test_texture(width, height)};
    dl.GenFb(1, &framebuffer); dl.BindFb(0x8d40, framebuffer);
    r_glUniform1i(r_glGetUniformLocation(program, "auDlssColor"), 2);
    r_glBindImageTexture(2, textures[1], 0, 0, 0, GL_WRITE_ONLY, GL_RGBA16F);
    for (int offset = 0; offset < 2; offset++)
    for (int flip = 0; flip < 2; flip++)
    for (int pixelSize = 0; pixelSize < 3; pixelSize++) {
        const GLfloat sentinel[] = {-1, -1, -1, -1};
        dl.FbTex(0x8d40, 0x8ce0, GL_TEXTURE_2D, textures[1], 0); testClear(0x1800, 0, sentinel);
        dl.FbTex(0x8d40, 0x8ce0, GL_TEXTURE_2D, textures[0], 0);
        GLint viewport[] = {offset ? 7 : 0, offset ? 11 : 0, width, height};
        GLfloat viewportUniform[] = {(float)viewport[0], (float)viewport[1], width, height};
        ov.Viewport(viewport[0], viewport[1], width, height);
        r_glUniform4fv(r_glGetUniformLocation(program, "uHookDlssViewport"), 1, viewportUniform);
        r_glUniform2f(r_glGetUniformLocation(program, "uPixelSize"), pixelSize == 2 ? 0 : 1.0f / (pixelSize ? 96 : width),
            pixelSize == 2 ? 0 : 1.0f / (pixelSize ? 80 : height));
        r_glUniform1i(r_glGetUniformLocation(program, "testFlip"), flip);
        r_glDrawArrays(OV_GL_TRIANGLE_STRIP, 0, 4); r_glMemoryBarrier(0xffffffffu);
        dl.FbTex(0x8d40, 0x8ce0, GL_TEXTURE_2D, textures[1], 0);
        float pixels[64 * 48 * 4]; testRead(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels);
        int written = 0;
        for (int pixel = 0; pixel < width * height; pixel++) written += pixels[pixel * 4 + 3] == 1;
        printf("capture offset=%d flip=%d pixelSize=%d: %d/%d written\n", offset, flip, pixelSize, written, width * height);
        testFailures += written != width * height;
    }
    testDeleteTextures(2, textures); r_glDeleteProgram(program); assert(!testError());
}

static void resolve_test(const char *postEffect)
{
    const char *start = strstr(postEffect, "    if (uHookDlssResolve == 1) {");
    const char *end = start ? strstr(start, "    vec4 color;") : NULL;
    assert(start && end);
    Str source = {0};
    str_cstr(&source, "#version 410 core\n");
    expand(&source, "#include \"AU_Tonemap.glsl\"\n", 0);
    str_cstr(&source, "\n#undef AU_LENSFLARE\n#define AU_LENSFLARE 0\n#undef AU_VIGNETTE\n#define AU_VIGNETTE 0\n"
        "uniform sampler2D uHookDlssOutput;uniform int uHookDlssResolve;uniform float uHookDlssSharpness;"
        "uniform vec4 uHookDlssViewport;in vec2 fragTexCoords;out vec4 outColor;void main(){\n#if 1\n");
    str_add(&source, start, (size_t)(end - start)); str_cstr(&source, "outColor=vec4(0);}");
    g_dlssProg = test_program(source.p); free(source.p);
    r_glUseProgram(g_dlssProg); g_dlssInputVAO = ov.vao; g_dlssInputMode = OV_GL_TRIANGLE_STRIP; g_dlssInputCount = 4;
    GLuint framebuffer, textures[] = {test_texture(96, 80), test_texture(8, 8)};
    dl.GenFb(1, &framebuffer); dl.BindFb(0x8d40, framebuffer);
    dl.FbTex(0x8d40, 0x8ce0, GL_TEXTURE_2D, textures[1], 0);
    for (int row = 0; row < 8; row++) {
        float colour[] = {.1f + row * .1f, .2f, .3f, 1};
        ov.Enable(GL_SCISSOR_TEST); testScissor(0, row, 8, 1); testClear(0x1800, 0, colour);
    }
    ov.Disable(GL_SCISSOR_TEST);
    dl.FbTex(0x8d40, 0x8ce0, GL_TEXTURE_2D, textures[0], 0);
    float reference[4] = {0};
    for (int flip = 0; flip < 2; flip++)
    for (int state = 0; state < 4; state++) {
        ov.ColorMask(1, 1, 1, 1); ov.Disable(0x8c89);
        const float black[] = {0, 0, 0, 0}; testClear(0x1800, 0, black);
        r_glUniform1i(r_glGetUniformLocation(g_dlssProg, "testFlip"), flip);
        if (state & 1) ov.ColorMask(0, 0, 0, 0);
        if (state & 2) ov.Enable(0x8c89);
        GLint viewport[] = {7, 11, 64, 48};
        int resolved = dlss_resolve(textures[1], viewport);
        float actual[4]; testRead(39, 17, 1, 1, GL_RGBA, GL_FLOAT, actual);
        if (!flip && !state) memcpy(reference, actual, sizeof reference);
        int valid = resolved && actual[0] > 0 && fabsf(actual[0] - reference[0]) < .001f;
        GLboolean mask[4]; ov.GetBooleanv(0x0c23, mask);
        for (int channel = 0; channel < 4; channel++) valid &= mask[channel] == !(state & 1);
        valid &= ov.IsEnabled(0x8c89) == !!(state & 2);
        printf("resolve flip=%d state=%d: RGB %.4f %.4f %.4f %s\n", flip, state, actual[0], actual[1], actual[2], valid ? "PASS" : "FAIL");
        testFailures += !valid;
    }
    ov.ColorMask(1, 1, 1, 1); ov.Disable(0x8c89);
    testDeleteTextures(2, textures); r_glDeleteProgram(g_dlssProg); g_dlssProg = 0; assert(!testError());
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 2); setvbuf(stdout, NULL, _IONBF, 0); g_log = stdout; wcscpy(g_packDir, arguments[1]);
    HWND window = CreateWindowW(L"STATIC", L"Aurora DLSS presentation test", WS_POPUP, 0, 0, 16, 16, NULL, NULL, NULL, NULL);
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR descriptor = {sizeof descriptor, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    assert(SetPixelFormat(dc, ChoosePixelFormat(dc, &descriptor), &descriptor));
    wchar_t path[MAX_PATH]; GetSystemDirectoryW(path, MAX_PATH); wcscat(path, L"\\opengl32.dll");
    g_real = LoadLibraryW(path); assert(g_real);
    real_wglGetProcAddress = (void *)GetProcAddress(g_real, "wglGetProcAddress");
    HGLRC (WINAPI *createContext)(HDC) = (void *)GetProcAddress(g_real, "wglCreateContext");
    BOOL (WINAPI *makeCurrent)(HDC, HGLRC) = (void *)GetProcAddress(g_real, "wglMakeCurrent");
    HGLRC context = createContext(dc); assert(context && makeCurrent(dc, context));
    r_glGetIntegerv = (void *)GetProcAddress(g_real, "glGetIntegerv"); r_glBindTexture = (void *)GetProcAddress(g_real, "glBindTexture");
    r_glDrawArrays = (void *)GetProcAddress(g_real, "glDrawArrays");
#define TEST_GL(name) r_##name = (void *)real_wglGetProcAddress(#name); assert(r_##name)
    TEST_GL(glCreateShader); TEST_GL(glShaderSource); TEST_GL(glCompileShader); TEST_GL(glGetShaderiv); TEST_GL(glGetShaderInfoLog);
    TEST_GL(glDeleteShader); TEST_GL(glAttachShader); TEST_GL(glLinkProgram); TEST_GL(glGetProgramiv); TEST_GL(glDeleteProgram);
    TEST_GL(glGetUniformLocation); TEST_GL(glUseProgram); TEST_GL(glUniform1i); TEST_GL(glUniform1f); TEST_GL(glUniform2f);
    TEST_GL(glUniform4fv); TEST_GL(glBindSampler); TEST_GL(glActiveTexture); TEST_GL(glBindImageTexture); TEST_GL(glMemoryBarrier);
#undef TEST_GL
    assert(ov_init()); ov.BindVertexArray(ov.vao);
    dl.GenFb = (void *)real_wglGetProcAddress("glGenFramebuffers"); dl.BindFb = (void *)real_wglGetProcAddress("glBindFramebuffer");
    dl.FbTex = (void *)real_wglGetProcAddress("glFramebufferTexture2D");
    dl.IsEnabled = (void *)GetProcAddress(g_real, "glIsEnabled"); dl.Enable = (void *)GetProcAddress(g_real, "glEnable"); dl.Disable = (void *)GetProcAddress(g_real, "glDisable");
    testError = (void *)GetProcAddress(g_real, "glGetError"); testRead = (void *)GetProcAddress(g_real, "glReadPixels");
    testDeleteTextures = (void *)GetProcAddress(g_real, "glDeleteTextures"); testClear = (void *)real_wglGetProcAddress("glClearBufferfv");
    testScissor = (void *)GetProcAddress(g_real, "glScissor");
    ov.Disable(0x0b71); ov.Disable(0x0be2); ov.Disable(0x0c11); ov.Disable(0x0b44); ov.ColorMask(1, 1, 1, 1);
    char *postEffect = read_file("programs/PostEffectFS.glsl", NULL); assert(postEffect);
    capture_test(postEffect); resolve_test(postEffect); free(postEffect);
    printf("DLSS presentation: %d failures\n", testFailures);
    return testFailures ? 1 : 0;
}
