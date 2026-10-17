static volatile LONG g_menuHotkey = 0x24;
static volatile LONG g_menuBinding;

static int menu_hotkey_scan(int key)
{
    if (key >= 'A' && key <= 'Z') return 4 + key - 'A';
    if (key >= '1' && key <= '9') return 30 + key - '1';
    if (key >= 0x70 && key <= 0x87) return key <= 0x7b ? 58 + key - 0x70 : 104 + key - 0x7c;
    if (key >= 0x61 && key <= 0x69) return 89 + key - 0x61;
    switch (key) {
    case '0': return 39;
    case 0x08: return 42;
    case 0x09: return 43;
    case 0x0d: return 40;
    case 0x10: case 0xa0: return 225;
    case 0x11: case 0xa2: return 224;
    case 0x12: case 0xa4: return 226;
    case 0xa1: return 229;
    case 0xa3: return 228;
    case 0xa5: return 230;
    case 0x5b: return 227;
    case 0x5c: return 231;
    case 0x13: return 72;
    case 0x14: return 57;
    case 0x1b: return 41;
    case 0x20: return 44;
    case 0x21: return 75;
    case 0x22: return 78;
    case 0x25: return 80;
    case 0x26: return 82;
    case 0x27: return 79;
    case 0x28: return 81;
    case 0x2c: return 70;
    case 0x24: return 74;
    case 0x2d: return 73;
    case 0x23: return 77;
    case 0x2e: return 76;
    case 0x60: return 98;
    case 0x6a: return 85;
    case 0x6b: return 87;
    case 0x6c: return 88;
    case 0x6d: return 86;
    case 0x6e: return 99;
    case 0x6f: return 84;
    case 0x90: return 83;
    case 0x91: return 71;
    case 0xba: return 51;
    case 0xbb: return 46;
    case 0xbc: return 54;
    case 0xbd: return 45;
    case 0xbe: return 55;
    case 0xbf: return 56;
    case 0xc0: return 53;
    case 0xdb: return 47;
    case 0xdc: return 49;
    case 0xdd: return 48;
    case 0xde: return 52;
    case 0xe2: return 100;
    default: return 0;
    }
}

static int menu_hotkey_valid(int key)
{
    int scan = menu_hotkey_scan(key);
    return scan > 0 && scan < 224 && scan != 41 && scan != 72
        && scan != 75 && scan != 78 && (scan < 79 || scan > 82);
}

static void menu_hotkey_set(double value)
{
    int key = value >= 0 && value <= 255 ? (int)value : 0;
    g_menuHotkey = value == key && menu_hotkey_valid(key) ? key : 0x24;
}

static void menu_hotkey_name(int key, wchar_t *output, int capacity)
{
    const wchar_t *name = NULL;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) _snwprintf(output, capacity, L"%lc", (wchar_t)key);
    else if (key >= 0x70 && key <= 0x87) _snwprintf(output, capacity, L"F%d", key - 0x70 + 1);
    else if (key >= 0x60 && key <= 0x69) _snwprintf(output, capacity, L"Num %d", key - 0x60);
    else {
        switch (key) {
        case 0x08: name = L"Backspace"; break;
        case 0x09: name = L"Tab"; break;
        case 0x0d: name = L"Enter"; break;
        case 0x14: name = L"Caps Lock"; break;
        case 0x20: name = L"Space"; break;
        case 0x23: name = L"End"; break;
        case 0x24: name = L"Home"; break;
        case 0x2c: name = L"Print Screen"; break;
        case 0x2d: name = L"Insert"; break;
        case 0x2e: name = L"Delete"; break;
        case 0x6a: name = L"Num *"; break;
        case 0x6b: name = L"Num +"; break;
        case 0x6c: name = L"Num Enter"; break;
        case 0x6d: name = L"Num -"; break;
        case 0x6e: name = L"Num ."; break;
        case 0x6f: name = L"Num /"; break;
        case 0x90: name = L"Num Lock"; break;
        case 0x91: name = L"Scroll Lock"; break;
        case 0xba: name = L";"; break;
        case 0xbb: name = L"="; break;
        case 0xbc: name = L","; break;
        case 0xbd: name = L"-"; break;
        case 0xbe: name = L"."; break;
        case 0xbf: name = L"/"; break;
        case 0xc0: name = L"Backtick"; break;
        case 0xdb: name = L"["; break;
        case 0xdc: name = L"Backslash"; break;
        case 0xdd: name = L"]"; break;
        case 0xde: name = L"'"; break;
        case 0xe2: name = L"Non-US backslash"; break;
        }
        if (name) _snwprintf(output, capacity, L"%ls", name);
        else _snwprintf(output, capacity, L"Key 0x%X", key);
    }
    output[capacity - 1] = 0;
}
