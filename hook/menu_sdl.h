#include <stdbool.h>

typedef union {
    uint32_t type;
    uint64_t alignment;
    unsigned char bytes[128];
} MenuSDLEvent;

static bool (*menu_sdl_poll_real)(MenuSDLEvent *);
static int (*menu_sdl_peep_real)(MenuSDLEvent *, int, int, uint32_t, uint32_t);
static const bool *(*menu_sdl_keyboard_real)(int *);
static uint32_t (*menu_sdl_mouse_real)(float *, float *);
static uint32_t (*menu_sdl_global_mouse_real)(float *, float *);
static uint32_t (*menu_sdl_relative_mouse_real)(float *, float *);
static bool (*menu_sdl_relative_real)(void *, bool);
static bool (*menu_sdl_grab_real)(void *, bool);
static void *(*menu_sdl_focus)(void);
static bool (*menu_sdl_relative_get)(void *);
static bool (*menu_sdl_grab_get)(void *);
static bool (*menu_sdl_cursor_get)(void);
static bool (*menu_sdl_cursor_show)(void);
static bool (*menu_sdl_cursor_hide)(void);
static void (*menu_sdl_reset_keyboard)(void);
static void (*menu_sdl_warp)(void *, float, float);
static bool (*menu_sdl_window_size)(void *, int *, int *);
static void *g_menuSDLWindow;
static int g_menuSDLCaptured, g_menuSDLRelative, g_menuSDLGrab, g_menuSDLCursor;
static float g_menuSDLScaleX = 1.0f, g_menuSDLScaleY = 1.0f;
static float g_menuSDLMouseX, g_menuSDLMouseY, g_menuSDLGlobalX, g_menuSDLGlobalY;
static __thread bool g_menuSDLKeys[512];
static __thread int g_menuSDLPolling;
static void render_scale_sync(void);
static void render_scale_event(MenuSDLEvent *event);

static int menu_sdl_capture(void)
{
    return menu_edit_mode(is_game_foreground(), GetAsyncKeyState(VK_MENU));
}

static int menu_shortcut_key(int key)
{
    if (!g_menuOpen) return key == VK_HOME;
    return key == VK_HOME || key == VK_ESCAPE || key == VK_UP || key == VK_DOWN
        || key == VK_LEFT || key == VK_RIGHT || key == VK_PRIOR || key == VK_NEXT;
}

static void menu_sdl_sync(void)
{
    if (g_gameHwnd && GetCurrentThreadId() != GetWindowThreadProcessId(g_gameHwnd, NULL)) return;
    int capture = menu_sdl_capture();
    if (capture == g_menuSDLCaptured) return;
    if (capture) {
        g_menuSDLWindow = menu_sdl_focus();
        if (!g_menuSDLWindow) return;
        g_menuSDLRelative = menu_sdl_relative_get(g_menuSDLWindow);
        g_menuSDLGrab = menu_sdl_grab_get(g_menuSDLWindow);
        g_menuSDLCursor = menu_sdl_cursor_get();
        int logicalWidth = 0, logicalHeight = 0;
        RECT client;
        if (menu_sdl_window_size(g_menuSDLWindow, &logicalWidth, &logicalHeight)
            && logicalWidth > 0 && logicalHeight > 0 && GetClientRect(g_gameHwnd, &client)) {
            g_menuSDLScaleX = (float)client.right / logicalWidth;
            g_menuSDLScaleY = (float)client.bottom / logicalHeight;
        }
        menu_sdl_mouse_real(&g_menuSDLMouseX, &g_menuSDLMouseY);
        menu_sdl_global_mouse_real(&g_menuSDLGlobalX, &g_menuSDLGlobalY);
        g_menuSDLCaptured = 1;
        menu_sdl_reset_keyboard();
        menu_sdl_relative_real(g_menuSDLWindow, false);
        menu_sdl_grab_real(g_menuSDLWindow, false);
        menu_sdl_warp(g_menuSDLWindow, g_menuPointerX / g_menuSDLScaleX, g_menuPointerY / g_menuSDLScaleY);
        menu_sdl_cursor_show();
    } else {
        g_menuSDLCaptured = 0;
        menu_sdl_reset_keyboard();
        menu_sdl_relative_real(g_menuSDLWindow, g_menuSDLRelative != 0);
        menu_sdl_grab_real(g_menuSDLWindow, g_menuSDLGrab != 0);
        if (!g_menuSDLCursor) menu_sdl_cursor_hide();
        g_menuSDLWindow = NULL;
    }
    logf_("menu: SDL mouse capture %s", capture ? "ON" : "OFF");
}

static uint32_t menu_sdl_u32(const MenuSDLEvent *event, int offset)
{
    uint32_t value;
    memcpy(&value, event->bytes + offset, sizeof(value));
    return value;
}

static float menu_sdl_float(const MenuSDLEvent *event, int offset)
{
    float value;
    memcpy(&value, event->bytes + offset, sizeof(value));
    return value;
}

static int menu_sdl_filter(MenuSDLEvent *event, int capture)
{
    render_scale_event(event);
    if (event->type == 0x300) {
        uint32_t scan = menu_sdl_u32(event, 24);
        if (scan == 74 || (g_menuOpen && ((scan >= 79 && scan <= 82) || scan == 75 || scan == 78
            || scan == 41))) return 0;
        if (capture) return 0;
    }
    if (!capture) return 1;
    if (event->type >= 0x400 && event->type <= 0x403) {
        EnterCriticalSection(&g_menuLock);
        if (event->type == 0x400 || event->type == 0x401 || event->type == 0x402) {
            g_menuPointerX = menu_sdl_float(event, 28) * g_menuSDLScaleX;
            g_menuPointerY = menu_sdl_float(event, 32) * g_menuSDLScaleY;
            if (event->type == 0x402 && event->bytes[24] == 1) g_menuDragging = -1;
            menu_pointer(event->type == 0x401 && event->bytes[24] == 1);
        } else {
            float wheel = menu_sdl_float(event, 28);
            if (menu_sdl_u32(event, 32) == 1) wheel = -wheel;
            menu_scroll(wheel > 0.0f ? -3 : (wheel < 0.0f ? 3 : 0));
        }
        LeaveCriticalSection(&g_menuLock);
        return 0;
    }
    return event->type != 0x302 && event->type != 0x303;
}

static bool menu_sdl_poll(MenuSDLEvent *event)
{
    menu_sdl_sync();
    render_scale_sync();
    if (!event) return menu_sdl_poll_real(event);
    for (;;) {
        g_menuSDLPolling++;
        bool available = menu_sdl_poll_real(event);
        g_menuSDLPolling--;
        if (!available) return false;
        int capture = menu_sdl_capture();
        if (capture) menu_sdl_reset_keyboard();
        if (menu_sdl_filter(event, capture)) return true;
    }
}

static int menu_sdl_peep(MenuSDLEvent *events, int capacity, int action, uint32_t minimum, uint32_t maximum)
{
    menu_sdl_sync();
    if (!g_menuSDLPolling) render_scale_sync();
    int count = menu_sdl_peep_real(events, capacity, action, minimum, maximum);
    if (g_menuSDLPolling) return count;
    if (count <= 0 || action != 2 || !events) return count;
    int kept = 0, capture = menu_sdl_capture();
    for (int index = 0; index < count; index++)
        if (menu_sdl_filter(&events[index], capture)) events[kept++] = events[index];
    return kept;
}

static const bool *menu_sdl_keyboard(int *count)
{
    int size = 0;
    const bool *state = menu_sdl_keyboard_real(&size);
    if (count) *count = size;
    if (!menu_sdl_capture()) return state;
    if (size > (int)sizeof(g_menuSDLKeys)) return state;
    memset(g_menuSDLKeys, 0, sizeof(g_menuSDLKeys));
    return g_menuSDLKeys;
}

static uint32_t menu_sdl_mouse(float *horizontal, float *vertical)
{
    uint32_t buttons = menu_sdl_mouse_real(horizontal, vertical);
    if (!menu_sdl_capture()) return buttons;
    if (horizontal) *horizontal = g_menuSDLMouseX;
    if (vertical) *vertical = g_menuSDLMouseY;
    return 0;
}

static uint32_t menu_sdl_global_mouse(float *horizontal, float *vertical)
{
    uint32_t buttons = menu_sdl_global_mouse_real(horizontal, vertical);
    if (!menu_sdl_capture()) return buttons;
    if (horizontal) *horizontal = g_menuSDLGlobalX;
    if (vertical) *vertical = g_menuSDLGlobalY;
    return 0;
}

static uint32_t menu_sdl_relative_mouse(float *horizontal, float *vertical)
{
    uint32_t buttons = menu_sdl_relative_mouse_real(horizontal, vertical);
    if (!menu_sdl_capture()) return buttons;
    if (horizontal) *horizontal = 0.0f;
    if (vertical) *vertical = 0.0f;
    return 0;
}

static bool menu_sdl_relative(void *window, bool enabled)
{
    if (g_menuSDLCaptured && window == g_menuSDLWindow) {
        g_menuSDLRelative = enabled;
        return menu_sdl_relative_real(window, false);
    }
    return menu_sdl_relative_real(window, enabled);
}

static bool menu_sdl_grab(void *window, bool enabled)
{
    if (g_menuSDLCaptured && window == g_menuSDLWindow) {
        g_menuSDLGrab = enabled;
        return menu_sdl_grab_real(window, false);
    }
    return menu_sdl_grab_real(window, enabled);
}

static void **menu_sdl_slot(HMODULE module, const char *name)
{
    unsigned char *entry = (unsigned char *)GetProcAddress(module, name);
    if (!entry) return NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS *headers = (IMAGE_NT_HEADERS *)((unsigned char *)module + dos->e_lfanew);
    uintptr_t base = (uintptr_t)module, limit = base + headers->OptionalHeader.SizeOfImage;
    for (int offset = 0; offset < 26; offset++) {
        if (entry[offset] == 0xcc || entry[offset] == 0xc3) break;
        int guarded = entry[offset] == 0x48 && entry[offset + 1] == 0x8b && entry[offset + 2] == 0x05;
        int direct = entry[offset] == 0xff && (entry[offset + 1] == 0x25 || entry[offset + 1] == 0x15);
        if (guarded || direct) {
            int32_t displacement;
            int length = guarded ? 7 : 6;
            memcpy(&displacement, entry + offset + length - 4, sizeof(displacement));
            void **slot = (void **)(entry + offset + length + displacement);
            uintptr_t address = (uintptr_t)slot;
            if (address >= base && address + sizeof(*slot) <= limit && *slot) return slot;
            return NULL;
        }
    }
    return NULL;
}

#include "render_scale.h"

static void menu_sdl_install(void)
{
    static int attempted;
    if (attempted) return;
    HMODULE module = GetModuleHandleW(L"SDL3.dll");
    if (!module) return;
    attempted = 1;
    menu_sdl_focus = (void *(*)(void))GetProcAddress(module, "SDL_GetKeyboardFocus");
    menu_sdl_relative_get = (bool (*)(void *))GetProcAddress(module, "SDL_GetWindowRelativeMouseMode");
    menu_sdl_grab_get = (bool (*)(void *))GetProcAddress(module, "SDL_GetWindowMouseGrab");
    menu_sdl_cursor_get = (bool (*)(void))GetProcAddress(module, "SDL_CursorVisible");
    menu_sdl_cursor_show = (bool (*)(void))GetProcAddress(module, "SDL_ShowCursor");
    menu_sdl_cursor_hide = (bool (*)(void))GetProcAddress(module, "SDL_HideCursor");
    menu_sdl_reset_keyboard = (void (*)(void))GetProcAddress(module, "SDL_ResetKeyboard");
    menu_sdl_warp = (void (*)(void *, float, float))GetProcAddress(module, "SDL_WarpMouseInWindow");
    menu_sdl_window_size = (bool (*)(void *, int *, int *))GetProcAddress(module, "SDL_GetWindowSize");
    if (!menu_sdl_focus || !menu_sdl_relative_get || !menu_sdl_grab_get || !menu_sdl_cursor_get
        || !menu_sdl_cursor_show || !menu_sdl_cursor_hide || !menu_sdl_reset_keyboard || !menu_sdl_warp || !menu_sdl_window_size) return;
    menu_sdl_keyboard_real = (const bool *(*)(int *))GetProcAddress(module, "SDL_GetKeyboardState");
    if (!menu_sdl_keyboard_real) return;
    menu_sdl_keyboard_real(NULL);
    struct { const char *name; void *hook; void **real; void **slot; } hooks[] = {
        {"SDL_PollEvent", menu_sdl_poll, (void **)&menu_sdl_poll_real, NULL},
        {"SDL_PeepEvents", menu_sdl_peep, (void **)&menu_sdl_peep_real, NULL},
        {"SDL_GetKeyboardState", menu_sdl_keyboard, (void **)&menu_sdl_keyboard_real, NULL},
        {"SDL_GetMouseState", menu_sdl_mouse, (void **)&menu_sdl_mouse_real, NULL},
        {"SDL_GetGlobalMouseState", menu_sdl_global_mouse, (void **)&menu_sdl_global_mouse_real, NULL},
        {"SDL_GetRelativeMouseState", menu_sdl_relative_mouse, (void **)&menu_sdl_relative_mouse_real, NULL},
        {"SDL_SetWindowRelativeMouseMode", menu_sdl_relative, (void **)&menu_sdl_relative_real, NULL},
        {"SDL_SetWindowMouseGrab", menu_sdl_grab, (void **)&menu_sdl_grab_real, NULL}
    };
    int count = (int)(sizeof(hooks) / sizeof(hooks[0]));
    for (int index = 0; index < count; index++) {
        hooks[index].slot = menu_sdl_slot(module, hooks[index].name);
        if (!hooks[index].slot) { logf_("menu: unsupported SDL entry %s", hooks[index].name); return; }
        for (int previous = 0; previous < index; previous++)
            if (hooks[index].slot == hooks[previous].slot) { logf_("menu: shared SDL dispatcher rejected"); return; }
        *hooks[index].real = *hooks[index].slot;
    }
    int installed = 0;
    for (; installed < count; installed++) {
        DWORD previous;
        if (!VirtualProtect(hooks[installed].slot, sizeof(void *), PAGE_READWRITE, &previous)) break;
        InterlockedExchangePointer(hooks[installed].slot, hooks[installed].hook);
        VirtualProtect(hooks[installed].slot, sizeof(void *), previous, &previous);
    }
    if (installed != count) {
        while (installed-- > 0) {
            DWORD previous;
            if (VirtualProtect(hooks[installed].slot, sizeof(void *), PAGE_READWRITE, &previous)) {
                InterlockedExchangePointer(hooks[installed].slot, *hooks[installed].real);
                VirtualProtect(hooks[installed].slot, sizeof(void *), previous, &previous);
            }
        }
        return;
    }
    g_menuSDLReady = 1;
    render_scale_install(module);
    logf_("menu: SDL input hooks ready (%d)", count);
}
