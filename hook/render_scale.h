static bool (*render_pixels_real)(void *, int *, int *);
static bool (*render_push_event)(MenuSDLEvent *);
static uint32_t (*render_window_id)(void *);
static void *g_renderWindow;
static int g_renderAppliedScale = 100, g_renderPhysicalWidth, g_renderPhysicalHeight;

static int render_requested_scale(void)
{
#if AU_HAS_DLSS
    if (g_setDlssEnabled && g_dlssState >= 0 && g_setRenderScale > 0) return g_setRenderScale;
#endif
    return 100;
}

static bool render_pixels(void *window, int *width, int *height)
{
    int physicalWidth = 0, physicalHeight = 0;
    if (!render_pixels_real(window, &physicalWidth, &physicalHeight)) return false;
    int scale = window == g_renderWindow ? g_renderAppliedScale : 100;
    if (width) *width = (physicalWidth * scale + 50) / 100;
    if (height) *height = (physicalHeight * scale + 50) / 100;
    return true;
}

static void render_scale_sync(void)
{
    if (!render_pixels_real || !g_gameHwnd || GetCurrentThreadId() != GetWindowThreadProcessId(g_gameHwnd, NULL)) return;
    void *window = menu_sdl_focus();
    if (window) g_renderWindow = window;
    if (!g_renderWindow) return;
    int width = 0, height = 0;
    if (!render_pixels_real(g_renderWindow, &width, &height) || width <= 0 || height <= 0) return;
    int scale = render_requested_scale();
    if (scale == g_renderAppliedScale && width == g_renderPhysicalWidth && height == g_renderPhysicalHeight) return;
    g_renderAppliedScale = scale;
    g_renderPhysicalWidth = width;
    g_renderPhysicalHeight = height;
    MenuSDLEvent event = {0};
    uint32_t windowID = render_window_id(g_renderWindow);
    memcpy(event.bytes + 16, &windowID, sizeof(windowID));
    int logicalWidth = 0, logicalHeight = 0;
    if (menu_sdl_window_size(g_renderWindow, &logicalWidth, &logicalHeight)) {
        event.type = 0x206;
        memcpy(event.bytes + 20, &logicalWidth, sizeof(logicalWidth));
        memcpy(event.bytes + 24, &logicalHeight, sizeof(logicalHeight));
        render_push_event(&event);
    }
    event.type = 0x207;
    int scaledWidth = (width * scale + 50) / 100, scaledHeight = (height * scale + 50) / 100;
    memcpy(event.bytes + 20, &scaledWidth, sizeof(scaledWidth));
    memcpy(event.bytes + 24, &scaledHeight, sizeof(scaledHeight));
    if (!render_push_event(&event)) {
        g_renderPhysicalWidth = g_renderPhysicalHeight = 0;
        logf_("render scale: resize event could not be queued");
        return;
    }
    logf_("render scale: native renderer resize %dx%d (%d%%), physical window %dx%d", scaledWidth, scaledHeight, scale, width, height);
}

static void render_scale_event(MenuSDLEvent *event)
{
    if (event->type != 0x207 || !render_pixels_real || !g_renderWindow || !g_gameHwnd
        || GetCurrentThreadId() != GetWindowThreadProcessId(g_gameHwnd, NULL)) return;
    uint32_t windowID;
    memcpy(&windowID, event->bytes + 16, sizeof(windowID));
    if (windowID != render_window_id(g_renderWindow)) return;
    int width = 0, height = 0;
    if (!render_pixels(g_renderWindow, &width, &height)) return;
    memcpy(event->bytes + 20, &width, sizeof(width));
    memcpy(event->bytes + 24, &height, sizeof(height));
}

static void render_scale_install(HMODULE module)
{
    render_push_event = (bool (*)(MenuSDLEvent *))GetProcAddress(module, "SDL_PushEvent");
    render_window_id = (uint32_t (*)(void *))GetProcAddress(module, "SDL_GetWindowID");
    void **slot = menu_sdl_slot(module, "SDL_GetWindowSizeInPixels");
    if (!slot || !render_push_event || !render_window_id) { logf_("render scale: native SDL resizing unavailable"); return; }
    render_pixels_real = (bool (*)(void *, int *, int *))*slot;
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) { render_pixels_real = NULL; return; }
    InterlockedExchangePointer(slot, render_pixels);
    VirtualProtect(slot, sizeof(*slot), protection, &protection);
    logf_("render scale: native SDL pixel-size hook ready");
}
