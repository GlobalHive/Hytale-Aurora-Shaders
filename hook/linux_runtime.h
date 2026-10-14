// Native SDL3 interposition. Hytale resolves SDL exports from a dlopen handle, so
// LD_PRELOAD alone is insufficient: intercept those two explicit dlsym lookups too.
// SDL handles GLX/EGL and X11/Wayland; no display-server-specific input polling.
typedef PROC (*SDL_GetProc)(const char *);
static SDL_GetProc linux_sdl_getproc;
static bool (*linux_sdl_swap)(void *);
static const bool *(*linux_sdl_keys)(int *);
static void *(*linux_sdl_focus)(void);
static bool (*linux_sdl_size)(void *, int *, int *);
static void *linux_sdl_handle;
static pthread_once_t linux_init_once = PTHREAD_ONCE_INIT;
static int linux_notify = -1;
static double linux_reload_at;

__declspec(dllexport) PROC SDL_GL_GetProcAddress(const char *name);
__declspec(dllexport) bool SDL_GL_SwapWindow(void *window);

static void *linux_real_dlsym(void *handle, const char *name)
{
    // x86_64 glibc's original symbol version predates Ubuntu's supported releases.
    typedef void *(*Lookup)(void *, const char *);
    Lookup lookup = (Lookup)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    return lookup ? lookup(handle, name) : NULL;
}

__declspec(dllexport) void *dlsym(void *handle, const char *name)
{
    void *p = linux_real_dlsym(handle, name);
    if (!p) return NULL;
    if (!strcmp(name, "SDL_GL_GetProcAddress") && p != (void *)SDL_GL_GetProcAddress) {
        linux_sdl_getproc = (SDL_GetProc)p;
        linux_sdl_handle = handle;
        return (void *)SDL_GL_GetProcAddress;
    }
    if (!strcmp(name, "SDL_GL_SwapWindow") && p != (void *)SDL_GL_SwapWindow) {
        linux_sdl_swap = (bool (*)(void *))p;
        linux_sdl_handle = handle;
        return (void *)SDL_GL_SwapWindow;
    }
    return p;
}

static PROC linux_gl_proc(const char *name)
{
    return linux_sdl_getproc && name ? linux_sdl_getproc(name) : NULL;
}

static void linux_init(void)
{
    // Also supports programs linked directly against SDL3.
    if (!linux_sdl_handle) linux_sdl_handle = RTLD_NEXT;
    if (!linux_sdl_getproc) linux_sdl_getproc = (SDL_GetProc)linux_real_dlsym(linux_sdl_handle, "SDL_GL_GetProcAddress");
    if (!linux_sdl_swap) linux_sdl_swap = (bool (*)(void *))linux_real_dlsym(linux_sdl_handle, "SDL_GL_SwapWindow");
    linux_sdl_keys = (const bool *(*)(int *))linux_real_dlsym(linux_sdl_handle, "SDL_GetKeyboardState");
    linux_sdl_focus = (void *(*)(void))linux_real_dlsym(linux_sdl_handle, "SDL_GetKeyboardFocus");
    linux_sdl_size = (bool (*)(void *, int *, int *))linux_real_dlsym(linux_sdl_handle, "SDL_GetWindowSizeInPixels");
    real_wglGetProcAddress = linux_gl_proc;
    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_menuLock);
    const char *pack = getenv("AURORA_SHADERPACK");
    char path[MAX_PATH];
    if (!pack || !*pack) {
        Dl_info info;
        if (dladdr((void *)linux_init, &info) && realpath(info.dli_fname, path)) {
            char *slash = strrchr(path, '/');
            if (slash) *slash = 0;
            if (strlen(path) + 12 < sizeof path) strcat(path, "/shaderpack");
            pack = path;
        } else pack = "shaderpack";
    }
    au_to_wide(g_packDir, MAX_PATH, pack);
    wchar_t wide[MAX_PATH];
    pack_path(wide, "hook.log");
    g_log = _wfopen(wide, L"w");
    g_dump = pack_flag("dump");
    pack_path(wide, "errors");
    CreateDirectoryW(wide, NULL);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    g_enabled = !pack_flag("programs/_disabled");
    settings_load();
    load_index(); load_shares(); load_textures_cfg(); load_forward_cfg();
    r_glBindTexture = (PFN_glBindTexture)linux_gl_proc("glBindTexture");
    r_glDrawArrays = (PFN_glDrawArrays)linux_gl_proc("glDrawArrays");
    r_glDrawElements = (PFN_glDrawElements)linux_gl_proc("glDrawElements");
    linux_notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (linux_notify >= 0) {
        const char *dirs[] = {"programs", "include"};
        for (int i = 0; i < 2; i++) {
            pack_path(wide, dirs[i]);
            au_to_utf8(path, sizeof path, wide);
            if (inotify_add_watch(linux_notify, path, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) < 0)
                logf_("cannot watch %s for shader edits", path);
        }
    }
    logf_("Aurora native Linux SDL3 hook loaded; DLSS unavailable; dump=%d", g_dump);
}

static void linux_input(void *window)
{
    // SDL3 scancodes: Home, Up, Down, Left, Right, Backspace, PageUp, PageDown, Pause.
    static const int keys[] = {74, 82, 81, 80, 79, 42, 75, 78, 72};
    static bool down[9];
    static double next[9];
    double t = now_sec();
    if (linux_notify >= 0) {
        char events[4096];
        while (read(linux_notify, events, sizeof events) > 0) linux_reload_at = t + 0.4;
        if (linux_reload_at && t >= linux_reload_at) {
            linux_reload_at = 0;
            InterlockedExchange(&g_reloadRequested, 1);
        }
    }
    if (g_menuWriteAt && t >= g_menuWriteAt) {
        g_menuWriteAt = 0;
        menu_save();
        InterlockedExchange(&g_reloadRequested, 1);
        swprintf(g_menuStatus, 96, L"Saved - shaders reloading");
        g_menuDirty = 1;
    }
    if (!linux_sdl_keys || !linux_sdl_focus) return;
    int count = 0;
    const bool *state = linux_sdl_keys(&count);
    bool focus = linux_sdl_focus() == window;
    for (int k = 0; k < 9; k++) {
        bool d = focus && state && keys[k] < count && state[keys[k]];
        bool repeat = d && down[k] && k >= 1 && k <= 4 && t >= next[k];
        bool fire = (d && !down[k]) || repeat;
        if (fire) next[k] = t + (repeat ? 0.06 : 0.35);
        down[k] = d;
        if (!fire) continue;
        if (k == 8) {
            g_toggleOff = !g_toggleOff;
            InterlockedExchange(&g_reloadRequested, 1);
            if (g_menuOpen) menu_load();
            g_menuDirty = 1;
        } else if (k == 0) {
            g_menuOpen = !g_menuOpen;
            if (g_menuOpen) { menu_load(); g_menuStatus[0] = 0; }
            g_menuDirty = 1;
        } else if (g_menuOpen) {
            bool sliding = g_moSel >= 0 && g_mo[g_moSel].kind == MO_RANGE;
            switch (k) {
            case 1: menu_move(-1); break;
            case 2: menu_move(1); break;
            case 3: if (!repeat || sliding) menu_change(-1, 0); break;
            case 4: if (!repeat || sliding) menu_change(1, 0); break;
            case 5: menu_change(0, 1); break;
            case 6: menu_section(-1); break;
            case 7: menu_section(1); break;
            }
        }
    }
}

__declspec(dllexport) PROC SDL_GL_GetProcAddress(const char *name)
{
    pthread_once(&linux_init_once, linux_init);
    return name ? hook_get_proc(name) : NULL;
}

__declspec(dllexport) bool SDL_GL_SwapWindow(void *window)
{
    pthread_once(&linux_init_once, linux_init);
    g_postPresent = 0;
    linux_input(window);
    int sw = 0, sh = 0;
    if (g_menuOpen && linux_sdl_size && linux_sdl_size(window, &sw, &sh) && sw > 0 && sh > 0 && ov_init()) {
        if (g_menuDirty || sh != g_ovScreenH) {
            menu_render(sh);
            g_ovScreenH = sh; g_ovUpload = 1; g_menuDirty = 0;
        }
        if (g_ovBits) menu_draw(sw, sh);
    }
    return linux_sdl_swap ? linux_sdl_swap(window) : false;
}
