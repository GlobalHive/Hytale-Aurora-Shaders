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

static bool hotkeyKeyboard[512];

static const bool *hotkey_keyboard(int *count)
{
    *count = 512;
    return hotkeyKeyboard;
}

static void menu_hotkey_test(void)
{
    char *original = read_file(MENU_SETTINGS, NULL);
    assert(original);
    const double invalid[] = {-1, 0, 27, 33, 37, 112.5, 256, 1e20, NAN, INFINITY};
    for (int index = 0; index < (int)(sizeof invalid / sizeof invalid[0]); index++) {
        menu_hotkey_set(invalid[index]);
        assert(g_menuHotkey == 36);
    }
    char *start = strstr(original, "#define AU_MENU_KEY ");
    if (start) {
        char *end = strchr(start, '\n'); assert(end);
        Str legacy = {0}; str_add(&legacy, original, start - original); str_cstr(&legacy, end + 1);
        write_file(MENU_SETTINGS, legacy.p, legacy.n); free(legacy.p);
    }
    menu_load();
    int optionIndex = -1;
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "AU_MENU_KEY")) optionIndex = index;
    assert(optionIndex >= 0);
    MenuOpt *option = &g_mo[optionIndex];
    const int keyValues[] = {36, 45, 35, 46, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 192, 220};
    const int scans[] = {74, 73, 77, 76, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 53, 49};
    assert(option->present && option->kind == MO_KEY && option->def == 36 && option->value == 36 && option->nEnum == 0);
    for (int index = 0; index < 18; index++) assert(menu_hotkey_scan(keyValues[index]) == scans[index]);
    for (int key = 8; key < 256; key++) if (menu_hotkey_valid(key)) {
        wchar_t name[32]; menu_hotkey_name(key, name, 32);
        assert(*name && menu_hotkey_scan(key) > 0 && menu_hotkey_scan(key) < 224);
    }
    assert(menu_hotkey_scan('Q') == 20 && menu_hotkey_scan('5') == 34
        && menu_hotkey_scan(0x60) == 98 && menu_hotkey_scan(0x87) == 115);
    g_moSel = optionIndex; g_menuProfileChoice = 6;
    g_menuOpen = 1;
    menu_change(1, 0); assert(g_menuBinding);
    wchar_t value[64]; display_value(option, value, 64); assert(!wcscmp(value, L"Press a key..."));
    unsigned char pressed[256] = {0}; pressed[VK_RIGHT] = 1;
    assert(menu_bind_poll(pressed, 1) && !g_menuBindReady);
    pressed[VK_RIGHT] = 0; pressed[VK_MENU] = 1;
    assert(menu_bind_poll(pressed, 1) && !g_menuBindReady);
    memset(pressed, 0, sizeof pressed);
    assert(menu_bind_poll(pressed, 1) && g_menuBindReady);
    pressed[VK_UP] = 1;
    assert(menu_bind_poll(pressed, 1) && g_menuBinding && option->value == 36);
    pressed[VK_UP] = 0; menu_bind_poll(pressed, 1);
    pressed[119] = 1;
    assert(menu_bind_poll(pressed, 1) && !g_menuBinding && g_menuOpen);
    assert(option->value == 119 && g_menuProfileChoice == 6);
    menu_save(); expect_setting("AU_MENU_KEY", 119); assert(g_menuHotkey == 119);
    display_value(option, value, 64); assert(!wcscmp(value, L"F8"));
    const int captured[] = {'Q', '5', 0x60, 0x87, 0xdb, 0xe2, 119};
    for (int index = 0; index < 7; index++) {
        menu_change(1, 0); memset(pressed, 0, sizeof pressed); menu_bind_poll(pressed, 1);
        pressed[captured[index]] = 1; menu_bind_poll(pressed, 1);
        assert(!g_menuBinding && g_menuOpen && g_menuHotkey == captured[index]);
        expect_setting("AU_MENU_KEY", captured[index]);
    }
    char *beforeCancel = read_file(MENU_SETTINGS, NULL); assert(beforeCancel);
    menu_change(1, 0); memset(pressed, 0, sizeof pressed); menu_bind_poll(pressed, 1);
    pressed[VK_ESCAPE] = 1; menu_bind_poll(pressed, 1);
    assert(!g_menuBinding && g_menuOpen && g_menuHotkey == 119);
    char *afterCancel = read_file(MENU_SETTINGS, NULL); assert(afterCancel && !strcmp(beforeCancel, afterCancel));
    free(beforeCancel); free(afterCancel);
    menu_change(1, 0); memset(pressed, 0, sizeof pressed); menu_bind_poll(pressed, 1);
    menu_bind_key_event(VK_SHIFT); assert(g_menuBindEvent == 0);
    menu_bind_key_event('Q'); menu_bind_poll(pressed, 1);
    assert(!g_menuBinding && g_menuOpen && g_menuHotkey == 'Q');
    menu_change(1, 0); menu_bind_key_event(VK_ESCAPE); menu_bind_poll(pressed, 1);
    assert(!g_menuBinding && g_menuHotkey == 'Q');
    menu_change(1, 0); menu_bind_poll(pressed, 1); menu_bind_key_event(119); menu_bind_poll(pressed, 1);
    assert(!g_menuBinding && g_menuHotkey == 119);
    menu_change(1, 0); menu_bind_poll(pressed, 0); assert(!g_menuBinding);
    menu_change(1, 0); g_menuOpen = 0; menu_bind_poll(pressed, 1); assert(!g_menuBinding);
    g_menuOpen = 1;
    menu_load(); assert(g_menuHotkey == 119);
    select_profile(0); expect_setting("AU_MENU_KEY", 119);
    select_profile(AU_DEFAULT_PROFILE); expect_setting("AU_MENU_KEY", 119);
    const char *custom = "// Aurora preset: Hotkey isolation\n#define AU_MENU_KEY 36\n";
    write_file("presets/preset-00.glsl", custom, strlen(custom));
    menu_load(); select_profile(12); expect_setting("AU_MENU_KEY", 119);
    wchar_t path[MAX_PATH]; pack_path(path, "presets/preset-00.glsl"); assert(DeleteFileW(path));
    menu_load();
    g_menuOpen = 0; g_menuPresetNaming = g_menuSearchFocus = 0;
    assert(menu_shortcut_key(119) && !menu_shortcut_key(VK_HOME));
    MenuSDLEvent event = {0}; event.type = 0x300;
    uint32_t scan = 65; memcpy(event.bytes + 24, &scan, sizeof scan);
    assert(!menu_sdl_filter(&event, 0));
    scan = 74; memcpy(event.bytes + 24, &scan, sizeof scan); assert(menu_sdl_filter(&event, 0));
    g_menuOpen = 1; assert(menu_shortcut_key(VK_UP) && menu_shortcut_key(VK_ESCAPE));
    g_menuPresetNaming = 1; assert(!menu_shortcut_key(119));
    scan = 65; memcpy(event.bytes + 24, &scan, sizeof scan); assert(menu_sdl_filter(&event, 0));
    g_menuPresetNaming = g_menuOpen = 0;
    menu_sdl_keyboard_real = hotkey_keyboard;
    hotkeyKeyboard[65] = hotkeyKeyboard[74] = hotkeyKeyboard[26] = true;
    int count = 0; const bool *keys = menu_sdl_keyboard(&count);
    assert(count == 512 && !keys[65] && keys[74] && keys[26]);
    assert(hotkeyKeyboard[65]);
    hotkeyKeyboard[65] = false;
    assert(menu_sdl_keyboard(&count) == hotkeyKeyboard);
    hotkeyKeyboard[65] = true; g_menuPresetNaming = 1;
    assert(menu_sdl_keyboard(&count) == hotkeyKeyboard);
    g_menuPresetNaming = 0;
    g_menuOpen = 1; menu_load();
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "AU_MENU_KEY")) g_moSel = index;
    menu_activate_tab(g_mo[g_moSel].tab);
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "AU_MENU_KEY")) g_moSel = index;
    g_moTop = menu_visible_index(g_moSel);
    g_menuLayout = (MenuLayout){0, 0, 500, 700, 12, 140, 50, 100, 38, 10, 100, 152};
    g_menuPointerX = 220; g_menuPointerY = 126; g_menuLastClick = -1;
    menu_pointer(1); assert(g_menuBinding);
    assert(menu_edit_mode(1, 0) && !menu_edit_mode(0, 0));
    menu_render(1920, 1080); assert(g_ovBits);
    MenuOpt defaultKey = g_mo[g_moSel]; defaultKey.value = defaultKey.def;
    display_value(&defaultKey, value, 64); assert(!wcscmp(value, L"Home"));
    menu_bind_poll(pressed, 0); assert(!g_menuBinding);
    menu_change(0, 1); menu_save(); expect_setting("AU_MENU_KEY", 36); assert(g_menuHotkey == 36);
    assert(menu_shortcut_key(VK_HOME) && !menu_shortcut_key(119));
    write_file(MENU_SETTINGS, original, strlen(original)); free(original);
    g_menuOpen = 0; g_menuWriteAt = 0; menu_load();
    puts("PASS: direct hotkey capture, held-key/modifier guards, reserved keys, Escape/focus/close cancellation, persistence, profile isolation, SDL filtering and reset");
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
    menu_hotkey_test();
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
