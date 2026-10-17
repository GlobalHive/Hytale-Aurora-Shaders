// Standalone check of the DLSS bridge (hook/aud_dlss.c) outside the game: a hidden OpenGL
// window, DLSS upscaling a half red / half blue picture 2x, the result read back through GL.
//   tools\dlss_test\build.ps1, then run dlss_test.exe (needs nvngx_dlss.dll next to it)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <GL/gl.h>
#include "../../hook/aud_dlss.h"

#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GLAPI __stdcall
typedef void (GLAPI *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (GLAPI *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (GLAPI *PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (GLAPI *PFN_glClearBufferfv)(GLenum, GLint, const float *);

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static HMODULE g_gl32;
static void *get_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (!p || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) p = (void *)GetProcAddress(g_gl32, name);
    return p;
}

static void clear_tex(GLuint fbo, GLuint tex, int x, int y, int w, int h, float r, float g, float b, float a)
{
    static PFN_glBindFramebuffer bind;
    static PFN_glFramebufferTexture2D attach;
    static PFN_glClearBufferfv clearBuf;
    if (!bind) {
        bind = get_proc("glBindFramebuffer");
        attach = get_proc("glFramebufferTexture2D");
        clearBuf = get_proc("glClearBufferfv");
    }
    bind(GL_FRAMEBUFFER, fbo);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    float c[4] = {r, g, b, a};
    clearBuf(GL_COLOR, 0, c);
    glDisable(GL_SCISSOR_TEST);
    bind(GL_FRAMEBUFFER, 0);
}

static GLuint capture_program(void)
{
    GLuint (GLAPI *createProgram)(void) = get_proc("glCreateProgram");
    GLuint (GLAPI *createShader)(GLenum) = get_proc("glCreateShader");
    void (GLAPI *shaderSource)(GLuint, GLsizei, const char *const *, const GLint *) = get_proc("glShaderSource");
    void (GLAPI *compileShader)(GLuint) = get_proc("glCompileShader");
    void (GLAPI *getShader)(GLuint, GLenum, GLint *) = get_proc("glGetShaderiv");
    void (GLAPI *attachShader)(GLuint, GLuint) = get_proc("glAttachShader");
    void (GLAPI *deleteShader)(GLuint) = get_proc("glDeleteShader");
    void (GLAPI *linkProgram)(GLuint) = get_proc("glLinkProgram");
    void (GLAPI *getProgram)(GLuint, GLenum, GLint *) = get_proc("glGetProgramiv");
    const char *sources[] = {
        "#version 410 core\nvoid main(){vec2 corner=vec2(float((gl_VertexID&1)<<1),float(gl_VertexID&2));gl_Position=vec4(corner*2-1,0,1);}",
        "#version 410 core\n#extension GL_ARB_shader_image_load_store:enable\n#extension GL_ARB_shader_image_size:enable\n"
        "layout(rgba16f)uniform writeonly image2D inputColour;out vec4 outColor;void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);"
        "int light=pixel.x*3/imageSize(inputColour).x;vec3 colour=vec3(.1);colour[light]=2;imageStore(inputColour,pixel,vec4(colour,1));outColor=vec4(0);}"
    };
    GLuint program = createProgram();
    for (int stage = 0; stage < 2; stage++) {
        GLuint shader = createShader(stage ? 0x8b30 : 0x8b31);
        shaderSource(shader, 1, &sources[stage], NULL); compileShader(shader);
        GLint compiled = 0; getShader(shader, 0x8b81, &compiled);
        if (!compiled) { logf_("FAIL: shared image capture shader compilation"); exit(1); }
        attachShader(program, shader); deleteShader(shader);
    }
    linkProgram(program);
    GLint linked = 0; getProgram(program, 0x8b82, &linked);
    if (!linked) { logf_("FAIL: shared image capture program link"); exit(1); }
    return program;
}

int main(void)
{
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    *wcsrchr(dir, L'\\') = 0;

    HWND wnd = CreateWindowW(L"STATIC", L"dlss_test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
    HDC dc = GetDC(wnd);
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    g_gl32 = GetModuleHandleW(L"opengl32.dll");
    logf_("GL: %s / %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER));

    if (!aud_init(logf_, get_proc, dir, dir)) { logf_("FAIL: aud_init"); return 1; }
    const int ow = 1920, oh = 1080;
    const int widths[] = {960, 1478, 1920}, heights[] = {540, 831, 1080}, qualities[] = {0, 2, 5};
    GLuint program = capture_program();
    void (GLAPI *useProgram)(GLuint) = get_proc("glUseProgram");
    GLint (GLAPI *uniformLocation)(GLuint, const char *) = get_proc("glGetUniformLocation");
    void (GLAPI *uniformInt)(GLint, GLint) = get_proc("glUniform1i");
    void (GLAPI *bindImage)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) = get_proc("glBindImageTexture");
    void (GLAPI *memoryBarrier)(GLenum) = get_proc("glMemoryBarrier");
    void (GLAPI *generateVAO)(GLsizei, GLuint *) = get_proc("glGenVertexArrays");
    void (GLAPI *bindVAO)(GLuint) = get_proc("glBindVertexArray");
    GLuint vertexArray; generateVAO(1, &vertexArray); bindVAO(vertexArray);

    PFN_glGenFramebuffers genFb = get_proc("glGenFramebuffers");
    GLuint fbo;
    genFb(1, &fbo);
    float identity[16] = {0}, projection[16] = {0};
    for (int axis = 0; axis < 4; axis++) identity[axis * 5] = 1.0f;
    projection[0] = 1.2f; projection[5] = 1.6f; projection[10] = -1.002f; projection[11] = -1.0f; projection[14] = -0.2f;
    aud_set_camera(identity, projection);
    for (int backend = 0; backend < (aud_rr_available() ? 2 : 1); backend++)
    for (int resolution = 0; resolution < 3; resolution++) {
    int rw = widths[resolution], rh = heights[resolution];
    if (!aud_configure(rw, rh, ow, oh, qualities[resolution], 1, backend, 0)) { logf_("FAIL: aud_configure backend %d", backend); return 1; }
    for (int frame = 0; frame < 16; frame++) {
        clear_tex(fbo, aud_texture(AUD_TEX_DEPTH), 0, 0, rw, rh, backend ? 5.0f : 0.5f, 0, 0, 0);
        clear_tex(fbo, aud_texture(AUD_TEX_MOTION), 0, 0, rw, rh, 0, 0, 0, 0);
        clear_tex(fbo, aud_texture(AUD_TEX_RESPONSIVE), 0, 0, rw, rh, 0, 0, 0, 0);
        if (backend) {
            clear_tex(fbo, aud_texture(AUD_TEX_ALBEDO), 0, 0, rw, rh, 0.5f, 0.5f, 0.5f, 1.0f);
            clear_tex(fbo, aud_texture(AUD_TEX_SPECULAR), 0, 0, rw, rh, 0.04f, 0.04f, 0.04f, 1.0f);
            clear_tex(fbo, aud_texture(AUD_TEX_NORMALS), 0, 0, rw, rh, 0, 0, 1.0f, 0.8f);
        }
        PFN_glBindFramebuffer bind = get_proc("glBindFramebuffer");
        PFN_glFramebufferTexture2D attach = get_proc("glFramebufferTexture2D");
        bind(GL_FRAMEBUFFER, fbo); attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aud_texture(AUD_TEX_OUTPUT), 0);
        glViewport(0, 0, rw, rh); useProgram(program); uniformInt(uniformLocation(program, "inputColour"), 2);
        bindImage(2, aud_texture(AUD_TEX_COLOR), 0, 0, 0, 0x88b9, 0x881a);
        glDrawArrays(GL_TRIANGLES, 0, 3); memoryBarrier(0xffffffffu); bind(GL_FRAMEBUFFER, 0);
        if (glGetError()) { logf_("FAIL: shared image capture draw"); return 1; }
        if (!aud_evaluate(0.0f, 0.0f, 1.0f, 1.0f, frame == 0)) { logf_("FAIL: aud_evaluate frame %d", frame); return 1; }
    }
    glFinish();

    float *px = (float *)malloc((size_t)ow * oh * 4 * sizeof(float));
    glBindTexture(GL_TEXTURE_2D, aud_texture(AUD_TEX_OUTPUT));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px);
    int ok = 1;
    for (int light = 0; light < 3; light++) {
        const float *sample = px + ((size_t)(oh / 2) * ow + ow * (1 + 2 * light) / 6) * 4;
        logf_("HDR light %d RGB (%.3f %.3f %.3f)", light, sample[0], sample[1], sample[2]);
        for (int component = 0; component < 3; component++)
            if (!(component == light ? sample[component] > 1.4f && sample[component] < 2.6f
                                    : sample[component] >= 0.0f && sample[component] < 0.3f)) ok = 0;
    }
    logf_("%s: %s %dx%d -> %dx%d through shader image capture and the HDR GL/Vulkan bridge", ok ? "PASS" : "FAIL",
        backend ? "Ray Reconstruction" : "Super Resolution", rw, rh, ow, oh);
    free(px);
    if (!ok) { aud_shutdown(); return 1; }
    }
    aud_shutdown();
    return 0;
}
