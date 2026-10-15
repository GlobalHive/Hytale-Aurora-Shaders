#include "../../hook/hook.c"
#include <assert.h>

static void expect_setting(const char *name, double expected)
{
    char *settings = read_file(MENU_SETTINGS, NULL);
    assert(settings);
    assert(fabs(settings_num(settings, name, -999) - expected) < 0.00001);
    free(settings);
}

static void select_profile(int selected)
{
    MenuOpt *option = menu_profile_option();
    assert(option);
    option->value = g_menuProfileChoice = selected;
    menu_profile_apply();
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 2);
    wcscpy(g_packDir, arguments[1]);
    g_log = stdout;
    QueryPerformanceFrequency(&g_freq);
    menu_load();
    assert(menu_profile_option()->nEnum == 4);
    select_profile(0);
    expect_setting("AU_PRESET", 2);
    expect_setting("AU_DLSS", 0);
    expect_setting("AU_GI_DENOISER", 1);
    expect_setting("AU_PATH_TRACING", 0);
    select_profile(1);
    expect_setting("AU_DLSS", 1);
    expect_setting("AU_GI_DENOISER", 2);
    expect_setting("AU_RENDER_SCALE", 77);
    expect_setting("AU_PATH_TRACING", 0);
    select_profile(2);
    expect_setting("AU_DLSS", 1);
    expect_setting("AU_GI_DENOISER", 2);
    expect_setting("AU_RENDER_SCALE", 77);
    expect_setting("AU_PATH_TRACING", 1);
    menu_profile_set("AU_SCREEN_BRIGHTNESS", 1.35);
    wcscpy(g_menuSearch, L"Caf\x00e9 cave");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(!g_menuPresetNaming && menu_profile_option()->nEnum == 5);
    assert(!wcscmp(menu_profile_option()->enumLabel[4], L"Caf\x00e9 cave"));
    select_profile(0);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.0);
    select_profile(3);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.35);
    expect_setting("AU_PATH_TRACING", 1);
    wcscpy(g_menuSearch, L"Caf\x00e9 cave");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(g_menuPresetNaming && menu_profile_option()->nEnum == 5);
    menu_profile_cancel();
    assert(!g_menuPresetNaming && !*g_menuSearch);
    menu_load();
    assert(menu_profile_option()->nEnum == 5);
    puts("PASS: built-in profile settings, 77 percent RR, path tracing, named UTF-8 save/load, duplicate-name protection, cancellation and persistence");
    return 0;
}
