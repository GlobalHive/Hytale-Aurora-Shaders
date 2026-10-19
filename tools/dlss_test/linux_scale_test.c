#define _GNU_SOURCE
// Exercises the actual SDL interposer scale/resize logic with a fake SDL window.
#include <assert.h>
#define AU_HAS_DLSS 1
#include "../../hook/hook.c"
static int physical_w = 2560, physical_h = 1440, event_count, fail_push;
static void *window = (void *)1;
static LinuxMenuEvent last_event;
static void no_init(void) {}
static bool size(void *w, int *x, int *y) { *x = physical_w; *y = physical_h; return true; }
static void *current(void) { return window; }
static uint32_t id(void *w) { return (uint32_t)(uintptr_t)w; }
static bool push(LinuxMenuEvent *e) { last_event = *e; event_count++; return !fail_push; }
int main(int argc, char **argv)
{
    assert(argc == 2);
    au_to_wide(g_packDir, MAX_PATH, argv[1]);
    pthread_once(&linux_init_once, no_init);
    linux_sdl_size = size; linux_current_window = current; lm_id = id; linux_push_event = push;
    g_setDlssEnabled = 1; g_setRenderScale = 77;
    linux_render_scale_sync(); assert(g_renderAppliedScale == 100); // capability unknown
    g_dlssState = 1; linux_render_scale_sync(); assert(g_renderAppliedScale == 77);
    int w, h; assert(SDL_GetWindowSizeInPixels(window, &w, &h) && w == 1971 && h == 1109);
    assert(SDL_GetWindowSizeInPixels((void *)2, &w, &h) && w == 2560 && h == 1440);
    assert(linux_physical_size(&w, &h) && w == 2560 && h == 1440);
    assert(linux_menu_u32(&last_event, 20) == 1971 && linux_menu_u32(&last_event, 24) == 1109);
    int count = event_count; linux_render_scale_sync(); assert(event_count == count);
    physical_w = 1920; physical_h = 1080; linux_render_scale_sync();
    assert(linux_menu_u32(&last_event, 20) == 1478 && linux_menu_u32(&last_event, 24) == 832);
    LinuxMenuEvent resize = {.type = 0x207}; uint32_t wid = 1;
    memcpy(resize.bytes + 16, &wid, 4); linux_render_scale_event(&resize);
    assert(linux_menu_u32(&resize, 20) == 1478);
    wid = 2; memcpy(resize.bytes + 16, &wid, 4); memset(resize.bytes + 20, 0, 8);
    linux_render_scale_event(&resize); assert(linux_menu_u32(&resize, 20) == 0);
    g_dlssState = -1; linux_render_scale_sync(); assert(g_renderAppliedScale == 100);
    g_dlssState = 1; g_setDlssEnabled = 0; linux_render_scale_sync(); assert(g_renderAppliedScale == 100);
    g_setDlssEnabled = 1; fail_push = 1; linux_render_scale_sync();
    assert(linux_render_width == 0); fail_push = 0; linux_render_scale_sync();
    assert(linux_render_width == physical_w);
    menu_load();
    int dlss_option = 0, scale_option = 0;
    for (int n = 0; n < g_nMo; n++) {
        if (!strcmp(g_mo[n].define, "AU_DLSS")) dlss_option++;
        if (!strcmp(g_mo[n].define, "AU_RENDER_SCALE")) scale_option++;
        assert(strcmp(g_mo[n].define, "AU_DLSS_FRAMEGEN"));
        assert(strcmp(g_mo[n].define, "!DLSS_FG_INFO"));
    }
    assert(dlss_option == 1 && scale_option == 1);
    Str source = {0}; g_dlssState = -1;
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    assert(strstr(source.p, "#undef AU_DLSS\n#define AU_DLSS 0")); free(source.p);
    source = (Str){0}; g_dlssState = 1;
    expand(&source, "#include \"AU_Settings.glsl\"\n", 0);
    assert(!strstr(source.p, "#undef AU_DLSS\n#define AU_DLSS 0"));
    assert(strstr(source.p, "#undef AU_DLSS_FRAMEGEN\n#define AU_DLSS_FRAMEGEN 0")); free(source.p);
    puts("PASS: Linux DLSS menu visibility and temporal-AA fallback shader override");
    window = NULL; count = event_count; linux_render_scale_sync(); assert(event_count == count);
    puts("PASS: Linux DLSS render scale, physical output size, resize, other windows and fallback");
}
