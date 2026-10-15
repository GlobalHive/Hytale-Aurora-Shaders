#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <initializer_list>
#include "../../hook/aud_framegen.h"
#ifdef AU_FRAMEGEN_TEST_DLL
#define FG_DYNAMIC_FUNCTIONS(ENTRY) ENTRY(afg_init) ENTRY(afg_configure) ENTRY(afg_vulkan_context) ENTRY(afg_wait) ENTRY(afg_texture) ENTRY(afg_capture_inputs) ENTRY(afg_capture_hudless) ENTRY(afg_present) ENTRY(afg_stop) ENTRY(afg_shutdown) ENTRY(afg_presented_frames) ENTRY(afg_status)
#define FG_DYNAMIC_DECLARE(name) static decltype(&name) name##_dynamic;
FG_DYNAMIC_FUNCTIONS(FG_DYNAMIC_DECLARE)
#define afg_init afg_init_dynamic
#define afg_configure afg_configure_dynamic
#define afg_vulkan_context afg_vulkan_context_dynamic
#define afg_wait afg_wait_dynamic
#define afg_texture afg_texture_dynamic
#define afg_capture_inputs afg_capture_inputs_dynamic
#define afg_capture_hudless afg_capture_hudless_dynamic
#define afg_present afg_present_dynamic
#define afg_stop afg_stop_dynamic
#define afg_shutdown afg_shutdown_dynamic
#define afg_presented_frames afg_presented_frames_dynamic
#define afg_status afg_status_dynamic
#endif
#ifdef AU_FRAMEGEN_TEST_DLSS
extern "C" {
#include "../../hook/aud_dlss.h"
}
#endif

static HMODULE systemGL;
#ifndef AFG_TEST_WIDTH
#define AFG_TEST_WIDTH 1280
#define AFG_TEST_HEIGHT 720
#endif
static const int testWidth = AFG_TEST_WIDTH, testHeight = AFG_TEST_HEIGHT;
static void log_message(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format); vprintf(format, arguments); va_end(arguments);
    puts(""); fflush(stdout);
}

static void *get_proc(const char *name)
{
    void *address = (void *)wglGetProcAddress(name);
    if (!address || (uintptr_t)address <= 3 || address == (void *)-1) address = (void *)GetProcAddress(systemGL, name);
    return address;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM word, LPARAM parameter)
{
    if (message == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, word, parameter);
}

#ifdef AFG_TEST_WINDOW_THREAD
static HANDLE windowReady;
static HWND ownedWindow;
static DWORD WINAPI window_owner(void *)
{
    WNDCLASSW windowClass{};
    windowClass.style = CS_OWNDC; windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"AuroraFGValidation";
    RegisterClassW(&windowClass);
    ownedWindow = CreateWindowW(windowClass.lpszClassName, L"Aurora threaded DLSS-G validation", WS_POPUP, 0, 0, testWidth, testHeight, nullptr, nullptr, windowClass.hInstance, nullptr);
    SetEvent(windowReady);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (ownedWindow) DestroyWindow(ownedWindow);
    return 0;
}
#endif

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2 || argc > 4) { puts("Usage: framegen_test.exe <shaderpack\\streamline> [generated frames: 1..5] [DLSS runtime directory]"); return 2; }
    int generatedFrames = argc >= 3 ? _wtoi(argv[2]) : 1;
    if (generatedFrames < 1 || generatedFrames > 5) return 2;
#ifdef AFG_TEST_WINDOW_THREAD
    windowReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE windowThread = CreateThread(nullptr, 0, window_owner, nullptr, 0, nullptr);
    if (!windowThread || WaitForSingleObject(windowReady, 5000) != WAIT_OBJECT_0 || !ownedWindow) return 1;
    CloseHandle(windowReady);
    HWND window = ownedWindow;
#else
    WNDCLASSW windowClass{};
    windowClass.style = CS_OWNDC; windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"AuroraFGValidation";
    RegisterClassW(&windowClass);
    RECT rectangle{0, 0, testWidth, testHeight};
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Aurora DLSS-G validation", WS_POPUP, 0, 0, rectangle.right, rectangle.bottom, nullptr, nullptr, windowClass.hInstance, nullptr);
#endif
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR descriptor{sizeof(descriptor), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    SetPixelFormat(dc, ChoosePixelFormat(dc, &descriptor), &descriptor);
    HGLRC context = wglCreateContext(dc); wglMakeCurrent(dc, context);
    systemGL = GetModuleHandleW(L"opengl32.dll");
#ifdef AU_FRAMEGEN_TEST_DLL
    const wchar_t *proxyPath = _wgetenv(L"AFG_TEST_DLL");
    HMODULE proxy = proxyPath ? LoadLibraryW(proxyPath) : nullptr;
    if (!proxy) { log_message("Proxy load failed %lu", GetLastError()); return 1; }
#define FG_DYNAMIC_LOAD(name) name##_dynamic = reinterpret_cast<decltype(name##_dynamic)>(GetProcAddress(proxy, #name)); if (!name##_dynamic) { log_message("Missing %s", #name); return 1; }
    FG_DYNAMIC_FUNCTIONS(FG_DYNAMIC_LOAD)
#endif
    auto createCoreContext = (HGLRC (WINAPI *)(HDC, HGLRC, const int *))get_proc("wglCreateContextAttribsARB");
    int contextAttributes[] = {0x2091, 4, 0x2092, 1, 0x9126, 1, 0};
    HGLRC coreContext = createCoreContext ? createCoreContext(dc, nullptr, contextAttributes) : nullptr;
    if (coreContext) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context); context = coreContext; wglMakeCurrent(dc, context); }
    log_message("GL: %s", glGetString(GL_RENDERER));
#ifdef AU_FRAMEGEN_TEST_DLSS
    bool superResolution = argc == 4;
    if (superResolution && (!aud_init(log_message, get_proc, argv[3], argv[3]) || !aud_configure(1280, 720, 1280, 720, 5, 0, 0, 0))) return 1;
    if (superResolution) aud_shutdown();
#endif
    HWND previousForeground = GetForegroundWindow();
    ShowWindow(window, SW_SHOW);
    DWORD foregroundThread = GetWindowThreadProcessId(previousForeground, nullptr);
    DWORD currentThread = GetCurrentThreadId();
    if (foregroundThread != currentThread) AttachThreadInput(currentThread, foregroundThread, TRUE);
    SetForegroundWindow(window);
    SetFocus(window);
    if (foregroundThread != currentThread) AttachThreadInput(currentThread, foregroundThread, FALSE);
    log_message("Test window %p, foreground %p", window, GetForegroundWindow());
#ifdef AFG_TEST_CHILD_SURFACE
    HWND surfaceWindow = CreateWindowExW(WS_EX_NOACTIVATE, L"AuroraFGValidation", L"Aurora Vulkan surface", WS_CHILD | WS_VISIBLE, 0, 0, testWidth, testHeight, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    EnableWindow(surfaceWindow, FALSE);
#else
    HWND surfaceWindow = window;
#endif
    if (!afg_init(log_message, get_proc, argv[1], surfaceWindow) || !afg_configure(testWidth, testHeight, testWidth, testHeight)) {
        log_message("FAIL: %s", afg_status()); afg_shutdown(); return 1;
    }
#ifdef AU_FRAMEGEN_TEST_DLSS
    AudVulkanContext sharedContext;
    if (superResolution && (!afg_vulkan_context(&sharedContext) || !aud_init_shared(log_message, get_proc, argv[3], argv[3], &sharedContext) || !aud_configure(1280, 720, 1280, 720, 5, 0, 0, 0))) { puts("FAIL: shared SR/FG initialization"); return 1; }
#endif
    foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    if (foregroundThread != currentThread) AttachThreadInput(currentThread, foregroundThread, TRUE);
    SetForegroundWindow(window); SetFocus(window);
    if (foregroundThread != currentThread) AttachThreadInput(currentThread, foregroundThread, FALSE);
    auto genFbo = (void (APIENTRY *)(GLsizei, GLuint *))get_proc("glGenFramebuffers");
    auto bindFbo = (void (APIENTRY *)(GLenum, GLuint))get_proc("glBindFramebuffer");
    auto attach = (void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint))get_proc("glFramebufferTexture2D");
    auto clearBuffer = (void (APIENTRY *)(GLenum, GLint, const float *))get_proc("glClearBufferfv");
    GLuint fbo; genFbo(1, &fbo);
    float scene[152] = {}, previous[152] = {};
    for (int offset : {32, 48, 64, 80, 96}) scene[offset] = scene[offset + 5] = scene[offset + 10] = scene[offset + 15] = 1;
    scene[48] = 0.803333f; scene[53] = 1.428148f; scene[58] = -1.000020f;
    scene[59] = -1; scene[62] = -0.200002f; scene[63] = 0;
    scene[144] = 0.1f; scene[145] = 10000;
    memcpy(previous, scene, sizeof(scene));
    float jitter[2] = {};
    unsigned realFrames = 0, totalPresented = 0;
    for (unsigned frame = 0; frame < 64; frame++) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { afg_shutdown(); return 2; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (frame == 24) afg_stop();
        if (frame >= 24 && frame < 28) { glClearColor(0.2f, 0.3f, 0.4f, 1); glClear(GL_COLOR_BUFFER_BIT); SwapBuffers(dc); Sleep(20); continue; }
        if (frame == 28 && !afg_configure(testWidth, testHeight, testWidth, testHeight)) { puts("FAIL: Frame Gen re-enable"); return 1; }
        if (frame == 32) {
            afg_stop();
            if (!afg_configure(testWidth / 2, testHeight / 2, testWidth, testHeight)) { log_message("FAIL reconfigure: %s", afg_status()); afg_shutdown(); return 1; }
        }
        bindFbo(0x8d40, fbo);
        attach(0x8d40, 0x8ce0, GL_TEXTURE_2D, afg_texture(AFG_DEPTH), 0);
        float depth[4] = {0.95f, 0, 0, 0}; clearBuffer(0x1800, 0, depth);
        attach(0x8d40, 0x8ce0, GL_TEXTURE_2D, afg_texture(AFG_MOTION), 0);
        float motion[4] = {0, 0, 0, 0}; clearBuffer(0x1800, 0, motion);
        bindFbo(0x8d40, 0);
#ifdef AU_FRAMEGEN_TEST_DLSS
        if (superResolution) {
            bindFbo(0x8d40, fbo);
            const unsigned inputs[] = {aud_texture(AUD_TEX_COLOR), aud_texture(AUD_TEX_DEPTH), aud_texture(AUD_TEX_MOTION)};
            const float values[][4] = {{0.3f, 0.5f, 0.7f, 1}, {0.95f, 0, 0, 0}, {0, 0, 0, 0}};
            for (int input = 0; input < 3; input++) {
                attach(0x8d40, 0x8ce0, GL_TEXTURE_2D, inputs[input], 0);
                clearBuffer(0x1800, 0, values[input]);
            }
            bindFbo(0x8d40, 0);
            if (!aud_evaluate(0, 0, 1, 1, frame == 0)) { puts("FAIL: SR/FG coexistence"); afg_shutdown(); return 1; }
        }
#endif
        glViewport(0, 0, testWidth, testHeight); glClearColor(0.04f, 0.08f, 0.15f, 1); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST); glScissor(100 + frame * 7, 180, 200, 300);
        glClearColor(0.8f, 0.3f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        if (!afg_capture_inputs(afg_texture(AFG_DEPTH), afg_texture(AFG_MOTION), 0, scene + 48) || !afg_capture_hudless()) { puts("FAIL capture"); afg_shutdown(); return 1; }
        glEnable(GL_SCISSOR_TEST); glScissor(40, 40, 80, 20);
        glClearColor(1, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        if (!afg_present(frame, scene, previous, jitter, frame == 0 || frame == 28 || frame == 32, generatedFrames)) { log_message("FAIL present: %s", afg_status()); afg_shutdown(); return 1; }
        unsigned presented = afg_presented_frames();
        if (frame >= 8) { realFrames++; totalPresented += presented; }
        if (!(frame % 16)) log_message("frame %u: NVIDIA reports %u presented", frame, presented);
        Sleep(20);
    }
    bool generated = totalPresented >= realFrames * 3 / 2;
    log_message("%s: %u actual presents for %u rendered frames", generated ? "PASS" : "FAIL", totalPresented, realFrames);
    POINT sample{testWidth / 2, testHeight / 2};
    ClientToScreen(window, &sample);
    HDC screen = GetDC(nullptr);
    COLORREF displayed = GetPixel(screen, sample.x, sample.y);
    ReleaseDC(nullptr, screen);
    bool visible = displayed != CLR_INVALID && abs((int)GetRValue(displayed) - 10) < 16 && abs((int)GetGValue(displayed) - 20) < 16 && abs((int)GetBValue(displayed) - 38) < 16;
    log_message("%s: presented background RGB=%u,%u,%u", visible ? "PASS" : "FAIL", GetRValue(displayed), GetGValue(displayed), GetBValue(displayed));
    generated &= visible;
#ifdef AU_FRAMEGEN_TEST_DLSS
    if (superResolution) {
        glBindTexture(GL_TEXTURE_2D, aud_texture(AUD_TEX_OUTPUT));
        auto pixels = (float *)malloc(1280 * 720 * 4 * sizeof(float));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels);
        int center = (360 * 1280 + 640) * 4;
        bool valid = fabsf(pixels[center] - 0.3f) < 0.03f && fabsf(pixels[center + 1] - 0.5f) < 0.03f && fabsf(pixels[center + 2] - 0.7f) < 0.03f;
        free(pixels);
        if (!valid) { puts("FAIL: SR output changed while FG was active"); generated = false; }
        else log_message("PASS: SR pixels preserved while real FG presents on the shared Vulkan device");
        afg_stop();
        log_message("Shutting down SR after draining FG");
        aud_shutdown();
        log_message("SR shutdown complete");
    }
#endif
    log_message("Shutting down FG");
    afg_shutdown(); log_message("FG shutdown complete"); wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context); ReleaseDC(window, dc);
#ifdef AFG_TEST_WINDOW_THREAD
    PostMessageW(window, WM_CLOSE, 0, 0);
    WaitForSingleObject(windowThread, 5000); CloseHandle(windowThread);
#else
    DestroyWindow(window);
#endif
    if (IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
    return generated ? 0 : 1;
}
