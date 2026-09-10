#define LIBERTY_TESTING
#include "../src/main.cpp"
#include <cstdio>
#include <stdexcept>

void Check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    std::printf("PASS %s\n", name);
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_instance = GetModuleHandleW(nullptr);
    Gdiplus::GdiplusStartupInput gdip;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdip, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 0;
    try {
        DWORD minutes = 0;
        for (const wchar_t* value : {L"1", L"15", L"60", L"10080"})
            Check(liberty::ParseShutdownMinutes(value, minutes), "valid shutdown duration");
        for (const wchar_t* value : {L"", L"0", L"-1", L"1.5", L"10081", L"999999999999999", L" 60", L"60x", L"+2"})
            Check(!liberty::ParseShutdownMinutes(value, minutes), "invalid duration rejected without scheduling");

        Check(RegisterClasses(), "window classes registered");
        const UINT ids[] = {ID_MAC_MAPPING, ID_SLEEP_WITH_DISPLAY, ID_PREVENT_SLEEP, ID_SAVE_SCREENSHOTS, ID_PREVENT_LOCK};
        for (int mask = 0; mask < 32; ++mask) {
            g_macMapping = (mask & 1) != 0;
            g_displayOffAwake = (mask & 2) != 0;
            g_preventSleep = (mask & 4) != 0;
            g_saveScreenshots = (mask & 8) != 0;
            g_preventLock = (mask & 16) != 0;
            HWND menu = CreateWindowExW(0, kMenuClass, kAppName, WS_POPUP | WS_CLIPCHILDREN,
                0, 0, 470, 462, nullptr, nullptr, g_instance, nullptr);
            Check(menu != nullptr, "menu created");
            for (int i = 0; i < 5; ++i)
                Check(Button_GetCheck(GetDlgItem(menu, ids[i])) == ((mask & (1 << i)) ? BST_CHECKED : BST_UNCHECKED),
                    "persisted checkbox correct on first paint");
            // Exercise native button notifications; this must never affect another option.
            SendMessageW(GetDlgItem(menu, ID_SAVE_SCREENSHOTS), BM_CLICK, 0, 0);
            for (int i = 0; i < 5; ++i)
                Check(Button_GetCheck(GetDlgItem(menu, ids[i])) == (((mask ^ 8) & (1 << i)) ? BST_CHECKED : BST_UNCHECKED),
                    "native click changes only selected option");
            DestroyWindow(menu);
        }
        Check(!g_menuWindow, "closing menu clears only menu lifetime");
        // Exercise the actual hook translator while capturing injected events locally.
        // Tests never send keystrokes to applications on the user's desktop.
        g_macMapping = true; g_commandKey = VK_CAPITAL;
        auto hook = [](WORD key, bool down) {
            KBDLLHOOKSTRUCT event{}; event.vkCode = key;
            return KeyboardProc(HC_ACTION, down ? WM_KEYDOWN : WM_KEYUP, reinterpret_cast<LPARAM>(&event));
        };
        Check(hook(VK_CAPITAL, true) == 1 && hook(VK_LEFT, true) == 1, "Cmd+Left handled");
        Check(g_testKeyEvents.back().ki.wVk == VK_HOME && (g_testKeyEvents.back().ki.dwFlags & KEYEVENTF_EXTENDEDKEY),
            "navigation mapping uses extended Home key");
        hook(VK_CAPITAL, false);
        Check(hook(VK_LEFT, false) == 1 && g_testKeyEvents.back().ki.wVk == VK_HOME &&
            (g_testKeyEvents.back().ki.dwFlags & KEYEVENTF_KEYUP), "Cmd released first still releases translated key");
        hook(VK_CAPITAL, true); hook(VK_TAB, true); hook(VK_TAB, false);
        Check(g_switchingApps, "Cmd+Tab holds switcher across repeated Tab presses");
        hook(VK_CAPITAL, false);
        Check(!g_switchingApps && g_testKeyEvents.back().ki.wVk == VK_MENU &&
            (g_testKeyEvents.back().ki.dwFlags & KEYEVENTF_KEYUP), "Cmd release completes switcher without stuck Alt");
        hook(VK_CAPITAL, true); hook('A', true); hook(VK_CAPITAL, false); hook('A', false);
        Check(g_keyRoutes['A'].kind == KeyRouteKind::None && g_testKeyEvents.back().ki.wVk == 'A' &&
            (g_testKeyEvents.back().ki.dwFlags & KEYEVENTF_KEYUP), "Cmd+A has matching translated key-up");
        ReleaseMappedState();
        g_macMapping = false;

        const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 100; ++i) {
            HWND menu = CreateWindowExW(0, kMenuClass, kAppName, WS_POPUP | WS_CLIPCHILDREN,
                0, 0, 470, 462, nullptr, nullptr, g_instance, nullptr);
            Check(menu != nullptr, "repeat menu lifecycle");
            RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            DestroyWindow(menu);
        }
        Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore + 2, "repeated menus do not leak GDI handles");
        Check(SetStartAtLogin(true), "startup can be enabled in isolated test registry");
        Check(SetStartAtLogin(false), "startup can be disabled in isolated test registry");
        SaveDword(L"CommandKey", VK_CAPITAL); SaveDword(L"AltKey", VK_LMENU); SaveDword(L"ControlKey", VK_LCONTROL);
        LoadModifierMappings();
        Check(g_commandKey == VK_CAPITAL && g_altKey == VK_LMENU && g_controlKey == VK_LCONTROL, "custom modifier persistence");
        SaveDword(L"CommandKey", VK_LMENU);
        LoadModifierMappings();
        Check(g_commandKey == VK_CAPITAL, "duplicate physical mapping not applied");

        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), 2, -2, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(bitmap && pixels, "screenshot fixture created");
        auto* colors = static_cast<DWORD*>(pixels);
        colors[0] = 0x00ff0000; colors[1] = 0x0000ff00; colors[2] = 0x000000ff; colors[3] = 0x00ffffff;
        const std::wstring path = L"screenshot-regression.png";
        Check(SaveBitmapPng(bitmap, path), "screenshot PNG encoded");
        DeleteObject(bitmap);
        {
            Gdiplus::Bitmap decoded(path.c_str());
            Gdiplus::Color red, blue;
            decoded.GetPixel(0, 0, &red); decoded.GetPixel(0, 1, &blue);
            Check(red.GetR() == 255 && red.GetA() == 255 && blue.GetB() == 255, "screenshot colors and opacity preserved, not black");
        }
        DeleteFileW(path.c_str());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        result = 1;
    }
    if (g_menuWindow) DestroyWindow(g_menuWindow);
    SetThreadExecutionState(ES_CONTINUOUS);
    RegDeleteTreeW(HKEY_CURRENT_USER, kRegistryKey);
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return result;
}
