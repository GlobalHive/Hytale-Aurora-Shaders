#define AU_USER_PROFILES 16
#define AU_BUILTIN_PROFILES 12
#define AU_DEFAULT_PROFILE (-2)

static MenuOpt *menu_profile_find(int selected)
{
    for (int index = 0; index < g_nMo; index++)
        if (g_mo[index].kind == MO_PROFILE && (int)g_mo[index].value == selected) return &g_mo[index];
    return NULL;
}

static void menu_profile_row(int tab, int kind, int selected, const wchar_t *label, const wchar_t *help)
{
    if (g_nMo >= MENU_MAX) return;
    MenuOpt *entry = &g_mo[g_nMo++];
    memset(entry, 0, sizeof(*entry));
    entry->tab = tab;
    entry->kind = kind;
    entry->value = selected;
    entry->def = -1;
    entry->present = 1;
    snprintf(entry->define, sizeof(entry->define), "!PROFILE_%s_%d", kind == MO_SECTION ? "GROUP" : "SELECT", selected);
    wcsncpy(entry->label, label, 63);
    wcsncpy(entry->help, help, 191);
    g_menuTabs[tab].count++;
}

static void menu_profiles_load(MenuOpt *option)
{
    static const wchar_t *names[] = {L"Lowest", L"Low", L"Medium", L"High", L"Highest", L"Ultra"};
    int tab = option->tab;
    g_menuTabs[tab].count--;
    menu_profile_row(tab, MO_PROFILE, AU_DEFAULT_PROFILE, L"Default",
        L"Restores the saved original defaults. This baseline is separate from the quality presets. Click or press Right to apply.");
    for (int family = 0; family < 2; family++) {
        menu_profile_row(tab, MO_SECTION, family, family == 0 ? L"RTX presets" : L"Non-RTX presets", L"");
        for (int tier = 0; tier < 6; tier++)
            menu_profile_row(tab, MO_PROFILE, family * 6 + tier, names[tier], family == 0
                ? L"GI and material reflections. DLSS Quality+ at 77% with NVIDIA RR when supported; Original fallback otherwise. Path tracing stays off. Click or press Right to apply."
                : L"Lighter lighting without bounce GI or material SSR. Original denoiser; DLSS off, native resolution. No path tracing. Click or press Right to apply.");
    }
    menu_profile_row(tab, MO_SECTION, 2, L"Saved profiles", L"");
    for (int slot = 0; slot < AU_USER_PROFILES; slot++) {
        char relative[64];
        snprintf(relative, sizeof relative, "presets/preset-%02d.glsl", slot);
        char *saved = read_file(relative, NULL);
        if (!saved) continue;
        if (!strncmp(saved, "// Aurora preset: ", 18)) {
            char *name = saved + 18;
            name[strcspn(name, "\r\n")] = 0;
            if (*name) {
                wchar_t label[64];
                to_wide(label, 64, name);
                menu_profile_row(tab, MO_PROFILE, slot + AU_BUILTIN_PROFILES, label,
                    L"Restores your saved shader settings, including any manually selected DLSS configuration. Click or press Right to apply.");
            }
        }
        free(saved);
    }
    if (!menu_profile_find(g_menuProfileChoice)) g_menuProfileChoice = -1;
}

static void menu_profile_set(const char *name, double value)
{
    for (int index = 0; index < g_nMo; index++)
        if (!strcmp(g_mo[index].define, name) && g_mo[index].present) g_mo[index].value = value;
}

static void menu_profile_builtin(int selected)
{
    static const struct {
        int shadowSamples, shaftSamples, giRays, reflectionSteps, waterSteps, contactSteps, farSteps, scatterSteps;
        double cloudStep, contactDistance;
    } quality[] = {
        {4, 4, 4, 8, 8, 6, 8, 16, 64.0, 48.0},
        {8, 8, 4, 12, 12, 8, 12, 16, 48.0, 72.0},
        {12, 12, 8, 16, 16, 12, 16, 16, 32.0, 128.0},
        {24, 20, 8, 32, 24, 18, 24, 24, 20.0, 256.0},
        {32, 28, 12, 48, 40, 24, 32, 32, 14.0, 384.0},
        {48, 40, 16, 64, 64, 32, 48, 48, 8.0, 480.0}
    };
    static const double reach[] = {4.0, 4.0, 6.0, 8.0, 12.0, 16.0};
    int tier = selected % 6;
    int rtx = selected < 6;
    for (int index = 0; index < g_nMo; index++) {
        MenuOpt *entry = &g_mo[index];
        if (entry->present && !strncmp(entry->define, "AU_", 3)) entry->value = entry->def;
    }
    menu_profile_set("AU_PRESET", rtx ? 2 : 1);
    menu_profile_set("AU_DLSS", rtx && AU_HAS_DLSS);
    menu_profile_set("AU_GI_DENOISER", rtx && AU_HAS_DLSS ? 2 : 1);
    menu_profile_set("AU_RENDER_SCALE", rtx && AU_HAS_DLSS ? 77 : 100);
    menu_profile_set("AU_DLSS_FRAMEGEN", 0);
    menu_profile_set("AU_PATH_TRACING", 0);
    menu_profile_set("AU_SHADOW_SAMPLES", quality[tier].shadowSamples);
    menu_profile_set("AU_PCSS", rtx && tier >= 2);
    menu_profile_set("AU_CONTACT_SHADOWS", tier >= 1);
    menu_profile_set("AU_CONTACT_SHADOW_STEPS", quality[tier].contactSteps);
    menu_profile_set("AU_CONTACT_SHADOW_DISTANCE", quality[tier].contactDistance);
    menu_profile_set("AU_FAR_SHADOWS", tier >= 2);
    menu_profile_set("AU_FAR_SHADOW_STEPS", quality[tier].farSteps);
    menu_profile_set("AU_CLOUDS", tier >= 1);
    menu_profile_set("AU_CLOUD_STEP", quality[tier].cloudStep);
    menu_profile_set("AU_CLOUD_LAYERS", tier >= 3 ? 2 : 1);
    menu_profile_set("AU_CLOUD_SHADOWS", tier >= 2);
    menu_profile_set("AU_LIGHTSHAFTS", tier >= 1);
    menu_profile_set("AU_LIGHTSHAFT_SAMPLES", quality[tier].shaftSamples);
    menu_profile_set("AU_LOCAL_LIGHTS", tier == 5);
    menu_profile_set("AU_SSS", tier >= 1);
    menu_profile_set("AU_SSS_VOXEL", tier >= 3 ? 1 : 0);
    menu_profile_set("AU_SSS_TRACE_STEPS", quality[tier].scatterSteps);
    menu_profile_set("AU_GENERATED_NORMALS", tier >= 2);
    menu_profile_set("AU_MATERIALS", tier >= 2);
    menu_profile_set("AU_LIGHT_HIGHLIGHT", tier >= 1);
    menu_profile_set("AU_RTX_GI_RAYS", quality[tier].giRays);
    menu_profile_set("AU_RTX_GI_RADIUS", reach[tier]);
    menu_profile_set("AU_RTX_SSR_STEPS", quality[tier].reflectionSteps);
    menu_profile_set("AU_WATER", tier >= 1);
    menu_profile_set("AU_WATER_SSR_STEPS", quality[tier].waterSteps);
    menu_profile_set("AU_WATER_CAUSTICS", tier >= 2);
    menu_profile_set("AU_GLASS", tier >= 1);
    menu_profile_set("AU_RAIN_PUDDLES", tier >= 2);
}

static void menu_profile_apply(void)
{
    int selected = g_menuProfileChoice;
    MenuOpt *profile = menu_profile_find(selected);
    if (!profile) return;
    char *saved = NULL;
    if (selected == AU_DEFAULT_PROFILE || selected >= AU_BUILTIN_PROFILES) {
        char relative[64];
        if (selected == AU_DEFAULT_PROFILE) snprintf(relative, sizeof relative, "presets/default.glsl");
        else snprintf(relative, sizeof relative, "presets/preset-%02d.glsl", selected - AU_BUILTIN_PROFILES);
        saved = read_file(relative, NULL);
        if (!saved) {
            _snwprintf(g_menuStatus, 96, L"The selected profile is missing");
            return;
        }
    }
    if (!saved) menu_profile_builtin(selected);
    for (int index = 0; saved && index < g_nMo; index++) {
        MenuOpt *option = &g_mo[index];
        if (!option->present || strncmp(option->define, "AU_", 3)) continue;
        char *end = NULL;
        char *token = find_define_value(saved, option->define, &end);
        if (!token) continue;
        char *parsed = NULL;
        double value = strtod(token, &parsed);
        if (parsed != end || !isfinite(value)) continue;
        if (option->kind == MO_BOOL) value = value >= 0.5;
        else if (option->kind == MO_INTERNAL && !strcmp(option->define, "AU_PRESET") && value != 1.0 && value != 2.0) continue;
        else if (option->kind == MO_RANGE) value = fmin(option->max, fmax(option->min, value));
        else if (option->kind == MO_ENUM) {
            int valid = 0;
            for (int entry = 0; entry < option->nEnum; entry++)
                if (fabs(option->enumVal[entry] - value) < 1e-9) valid = 1;
            if (!valid) continue;
        }
        option->value = value;
    }
    free(saved);
#if !AU_HAS_DLSS
    menu_profile_set("AU_DLSS", 0);
    menu_profile_set("AU_GI_DENOISER", 1);
    menu_profile_set("AU_RENDER_SCALE", 100);
#endif
    g_toggleOff = 0;
    g_menuWriteAt = 0;
    menu_save();
    InterlockedExchange(&g_reloadRequested, 1);
    _snwprintf(g_menuStatus, 96, L"%ls%ls applied", selected >= 0 && selected < 6 ? L"RTX " : selected >= 6 && selected < AU_BUILTIN_PROFILES ? L"Non-RTX " : L"", profile->label);
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
    for (int index = 0; index < g_nMo; index++)
        if (g_mo[index].kind == MO_PROFILE && !wcscmp(start, g_mo[index].label)) {
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
    g_menuProfileChoice = slot + AU_BUILTIN_PROFILES;
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
