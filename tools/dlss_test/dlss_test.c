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
    const int rw = 960, rh = 540, ow = 1920, oh = 1080;

    PFN_glGenFramebuffers genFb = get_proc("glGenFramebuffers");
    GLuint fbo;
    genFb(1, &fbo);
    float identity[16] = {0}, projection[16] = {0};
    for (int axis = 0; axis < 4; axis++) identity[axis * 5] = 1.0f;
    projection[0] = 1.2f; projection[5] = 1.6f; projection[10] = -1.002f; projection[11] = -1.0f; projection[14] = -0.2f;
    aud_set_camera(identity, projection);
    for (int backend = 0; backend < (aud_rr_available() ? 2 : 1); backend++) {
    if (!aud_configure(rw, rh, ow, oh, 0, 1, backend, 0)) { logf_("FAIL: aud_configure backend %d", backend); return 1; }
    for (int frame = 0; frame < 16; frame++) {
        clear_tex(fbo, aud_texture(AUD_TEX_COLOR), 0, 0, rw / 3, rh, 2.0f, 0.1f, 0.1f, 1.0f);
        clear_tex(fbo, aud_texture(AUD_TEX_COLOR), rw / 3, 0, rw / 3, rh, 0.1f, 2.0f, 0.1f, 1.0f);
        clear_tex(fbo, aud_texture(AUD_TEX_COLOR), rw * 2 / 3, 0, rw / 3, rh, 0.1f, 0.1f, 2.0f, 1.0f);
        clear_tex(fbo, aud_texture(AUD_TEX_DEPTH), 0, 0, rw, rh, backend ? 5.0f : 0.5f, 0, 0, 0);
        clear_tex(fbo, aud_texture(AUD_TEX_MOTION), 0, 0, rw, rh, 0, 0, 0, 0);
        clear_tex(fbo, aud_texture(AUD_TEX_RESPONSIVE), 0, 0, rw, rh, 0, 0, 0, 0);
        if (backend) {
            clear_tex(fbo, aud_texture(AUD_TEX_ALBEDO), 0, 0, rw, rh, 0.5f, 0.5f, 0.5f, 1.0f);
            clear_tex(fbo, aud_texture(AUD_TEX_SPECULAR), 0, 0, rw, rh, 0.04f, 0.04f, 0.04f, 1.0f);
            clear_tex(fbo, aud_texture(AUD_TEX_NORMALS), 0, 0, rw, rh, 0, 0, 1.0f, 0.8f);
        }
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
    logf_("%s: %s through the HDR GL/Vulkan bridge", ok ? "PASS" : "FAIL", backend ? "Ray Reconstruction" : "Super Resolution");
    free(px);
    if (!ok) { aud_shutdown(); return 1; }
    }
    aud_shutdown();
    return 0;
}
