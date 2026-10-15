#define AU_USER_PROFILES 16

static MenuOpt *menu_profile_option(void)
{
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, "!PROFILE")) return &g_mo[index];
    return NULL;
}

static void menu_profiles_load(MenuOpt *option)
{
    static const wchar_t *names[] = {L"Current settings", L"Default", L"DLSS Preset", L"Path Tracing"};
    option->kind = MO_ENUM;
    option->present = 1;
    option->def = -1;
    option->value = g_menuProfileChoice;
    option->nEnum = 4;
    for (int index = 0; index < 4; index++) {
        option->enumVal[index] = index - 1;
        wcsncpy(option->enumLabel[index], names[index], 63);
    }
    for (int slot = 0; slot < AU_USER_PROFILES; slot++) {
        char relative[64];
        snprintf(relative, sizeof relative, "presets/preset-%02d.glsl", slot);
        char *saved = read_file(relative, NULL);
        if (!saved) continue;
        if (!strncmp(saved, "// Aurora preset: ", 18)) {
            char *name = saved + 18;
            name[strcspn(name, "\r\n")] = 0;
            if (*name) {
                int index = option->nEnum++;
                option->enumVal[index] = slot + 3;
                to_wide(option->enumLabel[index], 64, name);
            }
        }
        free(saved);
    }
    int present = 0;
    for (int index = 0; index < option->nEnum; index++)
        if ((int)option->enumVal[index] == g_menuProfileChoice) present = 1;
    if (!present) option->value = g_menuProfileChoice = -1;
}

static void menu_profile_set(const char *name, double value)
{
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, name) && g_mo[index].present) g_mo[index].value = value;
}

static void menu_profile_apply(void)
{
    MenuOpt *selector = menu_profile_option();
    int selected = selector ? (int)selector->value : -1;
    if (selected < 0) return;
#if !AU_HAS_DLSS
    if (selected == 1 || selected == 2) {
        _snwprintf(g_menuStatus, 96, L"This build does not support NVIDIA DLSS profiles");
        return;
    }
#endif
    char *saved = NULL;
    if (selected >= 3) {
        char relative[64];
        snprintf(relative, sizeof relative, "presets/preset-%02d.glsl", selected - 3);
        saved = read_file(relative, NULL);
        if (!saved) {
            _snwprintf(g_menuStatus, 96, L"The selected profile is missing");
            return;
        }
    }
    for (int index = 0; index < g_nMo; index++) {
        MenuOpt *option = &g_mo[index];
        if (!option->present || strncmp(option->define, "AU_", 3)) continue;
        if (!saved) option->value = option->def;
        else {
            char *end = NULL;
            char *token = find_define_value(saved, option->define, &end);
            if (!token) continue;
            char *parsed = NULL;
            double value = strtod(token, &parsed);
            if (parsed != end || !isfinite(value)) continue;
            if (option->kind == MO_BOOL) value = value >= 0.5;
            else if (option->kind == MO_RANGE) value = fmin(option->max, fmax(option->min, value));
            else if (option->kind == MO_ENUM) {
                int valid = 0;
                for (int entry = 0; entry < option->nEnum; entry++)
                    if (fabs(option->enumVal[entry] - value) < 1e-9) valid = 1;
                if (!valid) continue;
            }
            option->value = value;
        }
    }
    free(saved);
    if (selected < 3) {
        menu_profile_set("AU_PRESET", 2);
        menu_profile_set("AU_DLSS", selected != 0);
        menu_profile_set("AU_GI_DENOISER", selected == 0 ? 1 : 2);
        menu_profile_set("AU_RENDER_SCALE", selected == 0 ? 0 : 77);
        menu_profile_set("AU_PATH_TRACING", selected == 2);
    }
#if !AU_HAS_DLSS
    menu_profile_set("AU_DLSS", 0);
    menu_profile_set("AU_GI_DENOISER", 1);
    menu_profile_set("AU_RENDER_SCALE", 0);
#endif
    g_toggleOff = 0;
    g_menuWriteAt = 0;
    menu_save();
    InterlockedExchange(&g_reloadRequested, 1);
    _snwprintf(g_menuStatus, 96, L"Profile applied");
    g_menuDirty = 1;
}

static void menu_profile_finish(void)
{
    wchar_t name[64];
    wcsncpy(name, g_menuSearch, 63);
    name[63] = 0;
    size_t length = wcslen(name);
    while (length && iswspace(name[length - 1])) name[--length] = 0;
    wchar_t *start = name;
    while (iswspace(*start)) start++;
    if (!*start) {
        _snwprintf(g_menuStatus, 96, L"Enter a preset name first");
        return;
    }
    MenuOpt *selector = menu_profile_option();
    if (selector)
        for (int index = 1; index < selector->nEnum; index++)
            if (!wcscmp(start, selector->enumLabel[index])) {
                _snwprintf(g_menuStatus, 96, L"Name already exists; choose another name");
                return;
            }
    int slot = -1;
    char relative[64];
    for (int index = 0; index < AU_USER_PROFILES; index++) {
        snprintf(relative, sizeof relative, "presets/preset-%02d.glsl", index);
        char *existing = read_file(relative, NULL);
        if (!existing) { slot = index; break; }
        free(existing);
    }
    if (slot < 0) {
        _snwprintf(g_menuStatus, 96, L"All 16 custom preset slots are used");
        return;
    }
    menu_save();
    char *settings = read_file(MENU_SETTINGS, NULL);
    if (!settings) return;
    char encoded[256];
#ifdef _WIN32
    if (!WideCharToMultiByte(CP_UTF8, 0, start, -1, encoded, sizeof encoded, NULL, NULL)) { free(settings); return; }
#else
    au_to_utf8(encoded, sizeof encoded, start);
#endif
    wchar_t directory[MAX_PATH], pending[MAX_PATH], destination[MAX_PATH];
    pack_path(directory, "presets");
    CreateDirectoryW(directory, NULL);
    pack_path(pending, "presets/pending.tmp");
    pack_path(destination, relative);
    FILE *output = _wfopen(pending, L"wb");
    int complete = 0;
    if (output) {
        int wrote = fprintf(output, "// Aurora preset: %s\n%s", encoded, settings);
        int closed = fclose(output);
        if (wrote > 0 && closed == 0) {
#ifdef _WIN32
            complete = MoveFileW(pending, destination) != 0;
#else
            char sourcePath[MAX_PATH], destinationPath[MAX_PATH];
            au_to_utf8(sourcePath, sizeof sourcePath, pending);
            au_to_utf8(destinationPath, sizeof destinationPath, destination);
            complete = rename(sourcePath, destinationPath) == 0;
#endif
        }
    }
    free(settings);
    if (!complete) {
        DeleteFileW(pending);
        _snwprintf(g_menuStatus, 96, L"Could not save preset; check folder permissions");
        return;
    }
    g_menuProfileChoice = slot + 3;
    g_menuPresetNaming = 0;
    g_menuSearchFocus = 0;
    g_menuSearch[0] = 0;
    menu_load();
    _snwprintf(g_menuStatus, 96, L"Preset saved");
    g_menuDirty = 1;
}

static void menu_profile_action(const char *name)
{
    if (!strcmp(name, "!PROFILE_APPLY")) menu_profile_apply();
    else if (!strcmp(name, "!PROFILE_SAVE")) {
        if (g_menuPresetNaming) menu_profile_finish();
        else {
            g_menuPresetNaming = 1;
            g_menuSearchFocus = 1;
            g_menuSearch[0] = 0;
            _snwprintf(g_menuStatus, 96, L"Type a name; Enter or Save name confirms");
            g_menuDirty = 1;
        }
    }
}

static void menu_profile_cancel(void)
{
    if (!g_menuPresetNaming) return;
    g_menuPresetNaming = 0;
    g_menuSearchFocus = 0;
    g_menuSearch[0] = 0;
    menu_rebuild_rows();
    g_menuDirty = 1;
}
