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
    MenuOpt *option = menu_profile_find(selected);
    assert(option);
    g_moSel = (int)(option - g_mo);
    menu_change(1, 0);
    assert(g_menuProfileChoice == selected);
}

static int profile_count(void)
{
    int count = 0;
    for (int index = 0; index < g_nMo; index++)
        if (g_mo[index].kind == MO_PROFILE) count++;
    return count;
}

static void profile_preview(const char *relative)
{
    menu_render(1920, 1080);
    GdiFlush();
    assert(g_ovBits && g_ovTexW > 0 && g_ovTexH > 0);
    size_t pixelBytes = (size_t)g_ovTexW * g_ovTexH * 4;
    BITMAPFILEHEADER fileHeader = {0};
    BITMAPINFOHEADER infoHeader = {0};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = (DWORD)(fileHeader.bfOffBits + pixelBytes);
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = g_ovTexW;
    infoHeader.biHeight = -g_ovTexH;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    unsigned char *bitmap = malloc(fileHeader.bfSize);
    assert(bitmap);
    memcpy(bitmap, &fileHeader, sizeof(fileHeader));
    memcpy(bitmap + sizeof(fileHeader), &infoHeader, sizeof(infoHeader));
    memcpy(bitmap + fileHeader.bfOffBits, g_ovBits, pixelBytes);
    write_file(relative, (const char *)bitmap, fileHeader.bfSize);
    free(bitmap);
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 2);
    wcscpy(g_packDir, arguments[1]);
    g_log = stdout;
    QueryPerformanceFrequency(&g_freq);
    menu_load();
    assert(profile_count() == 13 && g_nMo < MENU_MAX);
    char *defaultSnapshot = read_file("presets/default.glsl", NULL);
    assert(defaultSnapshot && !wcscmp(menu_profile_find(AU_DEFAULT_PROFILE)->label, L"Default"));
    select_profile(AU_DEFAULT_PROFILE);
    expect_setting("AU_PRESET", 2);
    expect_setting("AU_DLSS", 0);
    expect_setting("AU_GI_DENOISER", 1);
    expect_setting("AU_PATH_TRACING", 0);
    expect_setting("AU_SHADOW_SAMPLES", 24);
    assert(!wcscmp(g_menuStatus, L"Default applied"));
    assert(!strcmp(g_mo[g_nMo - 1].define, "!DLSS_FG_INFO"));
    static const wchar_t *names[] = {L"Lowest", L"Low", L"Medium", L"High", L"Highest", L"Ultra"};
    double previousShadow = 0.0, previousCloudStep = 100.0;
    for (int selected = 0; selected < 12; selected++) {
        assert(!wcscmp(menu_profile_find(selected)->label, names[selected % 6]));
        select_profile(selected);
        int nvidia = selected < 6 && AU_HAS_DLSS;
        expect_setting("AU_PRESET", selected < 6 ? 2 : 1);
        expect_setting("AU_DLSS", nvidia);
        expect_setting("AU_GI_DENOISER", nvidia ? 2 : 1);
        expect_setting("AU_RENDER_SCALE", nvidia ? 77 : 100);
        expect_setting("AU_PATH_TRACING", 0);
        expect_setting("AU_DLSS_FRAMEGEN", 0);
        expect_setting("AU_GI_HISTORY_TAPS", 9);
        expect_setting("AU_GI_TEMPORAL_FILTER", 1);
        expect_setting("AU_LOCAL_LIGHTS", selected % 6 == 5);
        char *settings = read_file(MENU_SETTINGS, NULL);
        double shadow = settings_num(settings, "AU_SHADOW_SAMPLES", 0);
        double cloudStep = settings_num(settings, "AU_CLOUD_STEP", 0);
        if (selected % 6) assert(shadow > previousShadow && cloudStep < previousCloudStep);
        previousShadow = shadow; previousCloudStep = cloudStep;
        free(settings);
    }
    for (int tab = 0; tab < g_menuTabCount; tab++) {
        menu_activate_tab(tab);
        for (int index = 0; index < g_menuVisibleCount; index++)
            assert(strcmp(g_mo[g_menuVisible[index]].define, "AU_PRESET"));
    }
    g_moSel = (int)(menu_profile_find(0) - g_mo);
    int profilesTab = g_mo[g_moSel].tab;
    menu_activate_tab(profilesTab);
    g_moSel = (int)(menu_profile_find(0) - g_mo);
    g_moTop = menu_visible_index(g_moSel);
    g_menuLayout = (MenuLayout){0, 0, 500, 700, 12, 140, 50, 100, 38, 10, 100, 152};
    g_menuPointerX = 200; g_menuPointerY = 119; g_menuLastClick = -1;
    menu_pointer(1);
    assert(g_menuProfileChoice == 0);
    g_moSel = (int)(menu_profile_find(11) - g_mo);
    menu_change(-1, 0);
    assert(g_menuProfileChoice == 0);
    g_moSel = (int)(menu_profile_find(0) - g_mo);
    g_moTop = 0;
    profile_preview("preview-rtx.bmp");
    select_profile(11);
    profile_preview("preview-non-rtx.bmp");
    menu_profile_set("AU_SCREEN_BRIGHTNESS", 1.35);
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "AU_PATH_TRACING")) g_moSel = index;
    menu_change(1, 0);
    expect_setting("AU_DLSS", 0);
    assert(g_menuProfileChoice == -1);
    menu_save();
    expect_setting("AU_PRESET", 2);
    wcscpy(g_menuSearch, L"Caf\x00e9 cave");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(!g_menuPresetNaming && profile_count() == 14);
    assert(!wcscmp(menu_profile_find(12)->label, L"Caf\x00e9 cave"));
    select_profile(0);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.0);
    select_profile(12);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.35);
    expect_setting("AU_PATH_TRACING", 1);
    wcscpy(g_menuSearch, L"Caf\x00e9 cave");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(g_menuPresetNaming && profile_count() == 14);
    menu_profile_cancel();
    assert(!g_menuPresetNaming && !*g_menuSearch);
    menu_load();
    assert(profile_count() == 14);
    for (int slot = 1; slot < AU_USER_PROFILES; slot++) {
        _snwprintf(g_menuSearch, 64, L"Saved %d", slot);
        g_menuPresetNaming = 1;
        menu_profile_finish();
        assert(!g_menuPresetNaming);
    }
    assert(profile_count() == 29 && g_nMo < MENU_MAX && menu_profile_find(27));
    assert(!strcmp(g_mo[g_nMo - 1].define, "!DLSS_FG_INFO"));
    select_profile(27);
    g_moSel = -1;
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "AU_SCREEN_BRIGHTNESS")) g_moSel = index;
    menu_change(1, 0);
    assert(g_menuProfileChoice == -1);
    wcscpy(g_menuSearch, L"No free slot");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(g_menuPresetNaming && profile_count() == 29);
    menu_profile_cancel();
    select_profile(AU_DEFAULT_PROFILE);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.0);
    expect_setting("AU_PATH_TRACING", 0);
    char *afterRestore = read_file("presets/default.glsl", NULL);
    assert(afterRestore && !strcmp(defaultSnapshot, afterRestore));
    free(afterRestore); free(defaultSnapshot);
    unsigned char maskFile[12 + 2 * sizeof(SSSMaskRecord)] = {0};
    memcpy(maskFile, "AUSSS001", 8);
    unsigned maskCount = 2;
    memcpy(maskFile + 8, &maskCount, 4);
    SSSMaskRecord records[2] = {{1, {0}}, {4000, {0}}};
    records[0].pixels[31] = 255;
    memcpy(maskFile + 12, records, sizeof records);
    write_file("sss-masks.bin", (const char *)maskFile, sizeof maskFile);
    sss_masks_load();
    assert(g_sssMaskCount == 2 && sss_mask_tile(1, 4000) == 1 && sss_mask_tile(77, 4000) == (2 | 32768u));
    assert(sss_mask_tile(2, 3) == 0 && g_sssMasks[0].pixels[31] == 255);
    write_file("sss-masks.bin", (const char *)maskFile, 11);
    sss_masks_load(); assert(g_sssMaskCount == 0);
    records[1].hash = 1;
    memcpy(maskFile + 12, records, sizeof records);
    write_file("sss-masks.bin", (const char *)maskFile, sizeof maskFile);
    sss_masks_load(); assert(g_sssMaskCount == 0);
    puts("PASS: SSS mask binary loading, lookup, row orientation and malformed-file rejection");
    puts("PASS: frozen Default restore, 12 quality profiles, Ultra-only area lights, 77 percent RR or native fallback, hidden legacy selector, click/arrow selection, 16 UTF-8 custom profiles, manual-edit tracking and slot limits");
    return 0;
}
