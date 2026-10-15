#include "../../hook/hook.c"
#include <assert.h>

static void expect_setting(const char *name, double expected)
{
    char *settings = read_file(MENU_SETTINGS, NULL);
    assert(settings);
    double actual = settings_num(settings, name, -999);
    if (fabs(actual - expected) >= .00001) fprintf(stderr, "%s: expected %.6f, actual %.6f\n", name, expected, actual);
    assert(fabs(actual - expected) < 0.00001);
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

static void profile_management_test(void)
{
    char *defaultSnapshot = read_file("presets/default.glsl", NULL);
    assert(defaultSnapshot);
    int originalCount = profile_count();
    for (int selected = AU_DEFAULT_PROFILE; selected < AU_BUILTIN_PROFILES; selected++) {
        g_menuProfileChoice = selected;
        menu_profile_action("!PROFILE_DELETE");
        menu_profile_action("!PROFILE_EXPORT");
        assert(!menu_profile_delete_armed() && profile_count() == originalCount);
    }
    select_profile(12);
    size_t savedLength = 0;
    char *saved = read_file("presets/preset-00.glsl", &savedLength);
    assert(saved && strstr(saved, "// Aurora preset: Caf\xc3\xa9 cave\n"));
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "!PROFILE_EXPORT")) g_moSel = index;
    wchar_t action[64];
    display_value(&g_mo[g_moSel], action, 64);
    assert(!wcscmp(action, L"Export: Caf\x00e9 cave"));
    g_menuLayout = (MenuLayout){0, 0, 500, 700, 12, 140, 50, 100, 38, 10, 100, 152};
    g_moTop = menu_visible_index(g_moSel);
    g_menuPointerX = 200; g_menuPointerY = 106; g_menuLastClick = -1;
    menu_pointer(1);
    char *first = read_file("presets/exports/Aurora-Caf-cave-00-001.glsl", NULL);
    assert(first && !strcmp(first, saved));
    menu_profile_set("AU_SCREEN_BRIGHTNESS", 1.6);
    menu_save();
    char *settings = read_file(MENU_SETTINGS, NULL);
    menu_change(1, 0);
    char *second = read_file("presets/exports/Aurora-Caf-cave-00-002.glsl", NULL);
    char *sourceAfter = read_file("presets/preset-00.glsl", NULL);
    assert(second && sourceAfter && !strcmp(first, second) && !strcmp(saved, sourceAfter));
    free(sourceAfter);
    MenuOpt unsafeName = {0};
    unsafeName.value = 14;
    wcscpy(unsafeName.label, L"../../CON:bad /?Name");
    char relative[128];
    assert(menu_profile_export_file(&unsafeName, saved, savedLength, relative, sizeof relative));
    assert(!strcmp(relative, "presets/exports/Aurora-CON-bad-Name-02-001.glsl"));
    char *portable = read_file(relative, NULL);
    assert(portable && !strcmp(portable, saved));
    free(portable);
    assert(!menu_profile_export_file(&unsafeName, saved, savedLength, relative, 16));
    select_profile(13);
    menu_profile_action("!PROFILE_DELETE");
    assert(menu_profile_delete_armed());
    uint64_t previousHash = g_menuProfileDeleteHash;
    size_t changedLength = 0;
    char *changed = read_file("presets/preset-01.glsl", &changedLength);
    assert(changed);
    Str update = {0}; str_add(&update, changed, changedLength); str_cstr(&update, "\n");
    write_file("presets/preset-01.glsl", update.p, update.n);
    free(changed); free(update.p);
    g_menuProfileDeleteArmedAt -= 1;
    menu_profile_action("!PROFILE_DELETE");
    assert(menu_profile_delete_armed() && g_menuProfileDeleteHash != previousHash);
    assert(profile_count() == originalCount);
    menu_load();
    assert(!menu_profile_delete_armed());
    select_profile(12);
    menu_profile_set("AU_SCREEN_BRIGHTNESS", 1.6);
    menu_save();
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "!PROFILE_DELETE")) g_moSel = index;
    assert(g_mo[g_moSel].help[0]);
    g_moTop = menu_visible_index(g_moSel);
    g_menuPointerX = 200; g_menuPointerY = 106; g_menuLastClick = -1;
    menu_pointer(1);
    assert(menu_profile_delete_armed());
    display_value(&g_mo[g_moSel], action, 64);
    assert(!wcscmp(action, L"Confirm delete: Caf\x00e9 cave"));
    menu_change(1, 0);
    assert(menu_profile_find(12) && profile_count() == originalCount);
    g_menuProfileDeleteUntil = 0;
    g_menuProfileDeleteArmedAt -= 1;
    menu_change(1, 0);
    assert(menu_profile_delete_armed() && profile_count() == originalCount);
    g_menuProfileDeleteArmedAt -= 1;
    menu_change(1, 0);
    assert(!menu_profile_find(12) && profile_count() == originalCount - 1 && g_menuProfileChoice == -1);
    assert(!read_file("presets/preset-00.glsl", NULL));
    char *afterDelete = read_file(MENU_SETTINGS, NULL);
    char *afterDefault = read_file("presets/default.glsl", NULL);
    char *afterExport = read_file("presets/exports/Aurora-Caf-cave-00-001.glsl", NULL);
    assert(afterDelete && !strcmp(afterDelete, settings));
    assert(afterDefault && !strcmp(afterDefault, defaultSnapshot));
    assert(afterExport && !strcmp(afterExport, first));
    free(afterDelete); free(afterDefault); free(afterExport);
    free(first); free(second); free(saved); free(settings); free(defaultSnapshot);
    wcscpy(g_menuSearch, L"Reused slot");
    g_menuPresetNaming = 1;
    menu_profile_finish();
    assert(profile_count() == originalCount && !wcscmp(menu_profile_find(12)->label, L"Reused slot"));
    profile_preview("preview-profile-actions.bmp");
    puts("PASS: portable UTF-8 named profile export, collision-safe copies, protected built-ins, confirmed deletion, double-input/expiry/change rejection, preserved settings/exports and slot reuse");
}

int wmain(int count, wchar_t **arguments)
{
    assert(count == 2);
    wcscpy(g_packDir, arguments[1]);
    g_log = stdout;
    QueryPerformanceFrequency(&g_freq);
    menu_load();
    assert(profile_count() == 13 && g_nMo < MENU_MAX);
    int firstGeneral = -1;
    for (int index = 0; index < g_nMo; index++) {
        MenuOpt *option = &g_mo[index];
        if (option->tab == 0 && option->kind != MO_HEADER && firstGeneral < 0) firstGeneral = index;
        if (!strcmp(option->define, "AU_POLISH_ENABLE")) {
            assert(!wcscmp(option->label, L"Surface detail & shading"));
            assert(!wcscmp(g_menuTabs[option->tab].label, L"Ores & materials"));
            assert(option->def == 1);
        }
        if (!strcmp(option->define, "AU_BIOME_ATMOSPHERE")) {
            assert(!wcscmp(g_menuTabs[option->tab].label, L"Sky & atmosphere"));
            assert(option->def == 0);
        }
        if (!strcmp(option->define, "AU_POLISH_SUNLIGHT") || !strcmp(option->define, "AU_POLISH_SUN_BOUNCE"))
            assert(!wcscmp(g_menuTabs[option->tab].label, L"Lighting"));
        if (!strcmp(option->define, "AU_POLISH_SUN_SHADOWS") || !strcmp(option->define, "AU_POLISH_SUN_DIAMETER") ||
            !strcmp(option->define, "AU_POLISH_CONTACT_WEIGHT"))
            assert(!wcscmp(g_menuTabs[option->tab].label, L"Shadows"));
    }
    assert(firstGeneral >= 0 && !strcmp(g_mo[firstGeneral].define, "!TAA_INFO"));
    char *defaultSnapshot = read_file("presets/default.glsl", NULL);
    assert(defaultSnapshot && !wcscmp(menu_profile_find(AU_DEFAULT_PROFILE)->label, L"Default"));
    menu_profile_set("AU_BIOME_ATMOSPHERE", 1);
    menu_profile_set("AU_POLISH_ENABLE", 0);
    select_profile(AU_DEFAULT_PROFILE);
    expect_setting("AU_POLISH_ENABLE", 1);
    expect_setting("AU_PRESET", 2);
    expect_setting("AU_DLSS", 0);
    expect_setting("AU_GI_DENOISER", 1);
    expect_setting("AU_PATH_TRACING", 0);
    expect_setting("AU_SHADOW_SAMPLES", 24);
    expect_setting("AU_BIOME_ATMOSPHERE", 0);
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
        expect_setting("AU_BIOME_ATMOSPHERE", 0);
        expect_setting("AU_POLISH_ENABLE", 1);
        expect_setting("AU_POLISH_SUNLIGHT", selected % 6 >= 2);
        expect_setting("AU_POLISH_SKY_FILL", .65);
        expect_setting("AU_POLISH_SUN_COLOUR", .5);
        expect_setting("AU_POLISH_SUN_HIGHLIGHTS", .6);
        expect_setting("AU_POLISH_FOLIAGE_SUN", .65);
        expect_setting("AU_POLISH_SUN_BOUNCE", 1.0);
        expect_setting("AU_POLISH_SUN_SHADOWS", 1);
        expect_setting("AU_POLISH_SUN_DIAMETER", .75);
        expect_setting("AU_POLISH_CONTACT_WEIGHT", .85);
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
    menu_profile_set("AU_BIOME_ATMOSPHERE", 1);
    menu_profile_set("AU_POLISH_ENABLE", 0);
    menu_profile_set("AU_POLISH_ROUGHNESS", .75);
    menu_profile_set("AU_POLISH_SUNLIGHT", 0);
    menu_profile_set("AU_POLISH_SUN_DIAMETER", 1.25);
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
    expect_setting("AU_POLISH_ENABLE", 1);
    select_profile(12);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.35);
    expect_setting("AU_BIOME_ATMOSPHERE", 1);
    expect_setting("AU_POLISH_ENABLE", 0);
    expect_setting("AU_POLISH_ROUGHNESS", .75);
    expect_setting("AU_POLISH_SUNLIGHT", 0);
    expect_setting("AU_POLISH_SUN_DIAMETER", 1.25);
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
    expect_setting("AU_POLISH_SUNLIGHT", 1);
    expect_setting("AU_POLISH_SUN_DIAMETER", .75);
    expect_setting("AU_PATH_TRACING", 0);
    char *afterRestore = read_file("presets/default.glsl", NULL);
    assert(afterRestore && !strcmp(defaultSnapshot, afterRestore));
    free(afterRestore); free(defaultSnapshot);
    const char *lookProfile = "// Aurora preset: Surface Detail & Shading\n#define AU_POLISH_ENABLE 1\n#define AU_POLISH_ROUGHNESS .35\n"
        "#define AU_POLISH_SPECULAR_AA 1\n#define AU_POLISH_AMBIENT .4\n#define AU_POLISH_FOLIAGE_MATTE .65\n";
    write_file("presets/preset-15.glsl", lookProfile, strlen(lookProfile));
    menu_load();
    menu_profile_set("AU_SCREEN_BRIGHTNESS", 1.2);
    menu_save();
    select_profile(27);
    expect_setting("AU_POLISH_ENABLE", 1);
    expect_setting("AU_SCREEN_BRIGHTNESS", 1.2);
    expect_setting("AU_DLSS", 0);
    expect_setting("AU_PATH_TRACING", 0);
    select_profile(AU_DEFAULT_PROFILE);
    expect_setting("AU_POLISH_ENABLE", 1);
    expect_setting("AU_BIOME_ATMOSPHERE", 0);
    profile_management_test();
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
