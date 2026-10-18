// Installer capability probe: dynamically load EGL so development packages are unnecessary.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "../../hook/aud_dlss.h"
typedef void *Display;
static void *egl;
static void *(*get_proc)(const char *);
static void *gl_proc(const char *name) { return get_proc(name); }
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    wchar_t directory[4096];
    if (mbstowcs(directory, argv[1], 4096) == (size_t)-1) return 1;
    directory[4095] = 0;
    egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!egl) { puts("EGL runtime unavailable"); return 1; }
#define LOAD(type, name, args) type (*name) args = (void *)dlsym(egl, #name); if (!name) return 1
    LOAD(Display, eglGetDisplay, (void *));
    LOAD(unsigned, eglInitialize, (Display, int *, int *));
    LOAD(unsigned, eglBindAPI, (unsigned));
    LOAD(unsigned, eglChooseConfig, (Display, const int *, void **, int, int *));
    LOAD(void *, eglCreatePbufferSurface, (Display, void *, const int *));
    LOAD(void *, eglCreateContext, (Display, void *, void *, const int *));
    LOAD(unsigned, eglMakeCurrent, (Display, void *, void *, void *));
    get_proc = (void *)dlsym(egl, "eglGetProcAddress");
    if (!get_proc) return 1;
    Display display = eglGetDisplay(NULL);
    if (!eglInitialize(display, NULL, NULL)) {
        Display (*platform)(unsigned, void *, const int *) = get_proc("eglGetPlatformDisplayEXT");
        display = platform ? platform(0x31DD, NULL, NULL) : NULL; // surfaceless fallback
        if (!eglInitialize(display, NULL, NULL)) { puts("No usable EGL display"); return 1; }
    }
    int attrs[] = {0x3033, 1, 0x3040, 8, 0x3038}; // pbuffer / OpenGL
    void *config; int count;
    if (!eglBindAPI(0x30A2) || !eglChooseConfig(display, attrs, &config, 1, &count) || !count) return 1;
    int surface_attrs[] = {0x3057, 16, 0x3056, 16, 0x3038};
    void *surface = eglCreatePbufferSurface(display, config, surface_attrs);
    int context_attrs[] = {0x3098, 4, 0x30FB, 3, 0x30FD, 1, 0x3038};
    void *context = eglCreateContext(display, config, NULL, context_attrs);
    if (!eglMakeCurrent(display, surface, surface, context)) { puts("OpenGL 4.3 context unavailable"); return 1; }
    // Checks the actual GL/Vulkan UUID, fd sharing, NGX runtime and SR availability.
    if (!aud_init(NULL, gl_proc, directory, directory)) { puts("NGX/OpenGL/Vulkan DLSS capability check failed"); return 1; }
    if (!aud_configure(64, 64, 128, 128, 0, 1, 0, 0)) {
        puts("DLSS feature creation or shared-image import failed");
        aud_shutdown();
        return 1;
    }
    puts(aud_rr_available() ? "DLSS Super Resolution and Ray Reconstruction supported" : "DLSS Super Resolution supported");
    aud_shutdown();
    return 0;
}
