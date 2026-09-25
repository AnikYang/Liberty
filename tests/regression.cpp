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
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&controls);
    RegDeleteTreeW(HKEY_CURRENT_USER, kRegistryKey);
    int result = 0;
    try {
        DWORD minutes = 0;
        for (const wchar_t* value : {L"1", L"15", L"60", L"10080"})
            Check(liberty::ParseShutdownMinutes(value, minutes), "valid shutdown duration");
        for (const wchar_t* value : {L"", L"0", L"-1", L"1.5", L"10081", L"999999999999999", L" 60", L"60x", L"+2"})
            Check(!liberty::ParseShutdownMinutes(value, minutes), "invalid duration rejected without scheduling");

        std::vector<WORD> dailyTimes{23 * 60 + 30, 8 * 60 + 5, 23 * 60 + 30};
        Check(liberty::NormalizeDailyShutdownTimes(dailyTimes) &&
            dailyTimes == std::vector<WORD>({8 * 60 + 5, 23 * 60 + 30}),
            "daily times sort and deduplicate");
        Check(liberty::DailyShutdownTimeLabel(dailyTimes[0]) == L"08:05", "daily time uses 24-hour label");
        SYSTEMTIME boundaryDate{};
        boundaryDate.wYear = 2026; boundaryDate.wMonth = 9; boundaryDate.wDay = 16;
        Check(liberty::DailyShutdownBoundary(23 * 60 + 30, boundaryDate) == L"2026-09-16T23:30:00",
            "daily trigger starts at local calendar time");
        WORD parsedBoundary = 0;
        Check(liberty::ParseDailyShutdownBoundary(L"2026-09-16T01:15:00", parsedBoundary) && parsedBoundary == 75,
            "task trigger boundary imports existing plan time");
        Check(liberty::ParseDailyShutdownBoundary(L"2026-09-16T23:59:00+08:00", parsedBoundary) && parsedBoundary == 1439,
            "task trigger boundary accepts explicit time-zone suffix");
        Check(!liberty::ParseDailyShutdownBoundary(L"2026-09-16T24:00:00", parsedBoundary) &&
            !liberty::ParseDailyShutdownBoundary(L"bad", parsedBoundary), "invalid task trigger boundary rejected");
        dailyTimes.assign(25, 1);
        for (WORD index = 0; index < dailyTimes.size(); ++index) dailyTimes[index] = index;
        Check(!liberty::NormalizeDailyShutdownTimes(dailyTimes), "daily schedule rejects more than 24 distinct times");
        dailyTimes = {1440};
        Check(!liberty::NormalizeDailyShutdownTimes(dailyTimes), "daily schedule rejects invalid clock time");

        DYNAMIC_TIME_ZONE_INFORMATION utcZone{};
        wcscpy_s(utcZone.TimeZoneKeyName, L"UTC");
        auto date = [](WORD year, WORD month, WORD day, WORD hour, WORD minute, WORD second = 0) {
            SYSTEMTIME result{};
            result.wYear = year; result.wMonth = month; result.wDay = day;
            result.wHour = hour; result.wMinute = minute; result.wSecond = second;
            return result;
        };
        auto ticks = [](const SYSTEMTIME& time) {
            FILETIME file{};
            if (!SystemTimeToFileTime(&time, &file)) throw std::runtime_error("invalid test timestamp");
            return liberty::FileTimeTicks(file);
        };
        DWORD seconds = 0;
        ULONGLONG target = 0;
        using TimeError = liberty::ShutdownTimeError;
        Check(liberty::ResolveShutdownTime(date(2026, 9, 15, 21, 0), ticks(date(2026, 9, 15, 20, 59, 42)), seconds, target, &utcZone) == TimeError::None && seconds == 18,
            "21:00 uses second precision rather than rounding to minutes");
        Check(liberty::ResolveShutdownTime(date(2026, 9, 16, 8, 0), ticks(date(2026, 9, 15, 23, 30)), seconds, target, &utcZone) == TimeError::None && seconds == 30600,
            "tomorrow 08:00 crosses midnight correctly");
        Check(liberty::ResolveShutdownTime(date(2027, 1, 1, 0, 0), ticks(date(2026, 12, 31, 23, 59, 40)), seconds, target, &utcZone) == TimeError::None && seconds == 20,
            "midnight crosses year boundary");
        Check(liberty::ResolveShutdownTime(date(2028, 2, 29, 8, 0), ticks(date(2028, 2, 28, 8, 0)), seconds, target, &utcZone) == TimeError::None && seconds == 86400,
            "leap-day date supported");
        Check(liberty::ResolveShutdownTime(date(2026, 2, 29, 8, 0), ticks(date(2026, 2, 28, 8, 0)), seconds, target, &utcZone) == TimeError::Invalid,
            "invalid calendar date rejected");
        Check(liberty::ResolveShutdownTime(date(2026, 9, 15, 8, 0), ticks(date(2026, 9, 15, 21, 0)), seconds, target, &utcZone) == TimeError::Past,
            "past clock time rejected without immediate shutdown");
        const ULONGLONG fixedNow = ticks(date(2026, 9, 15, 12, 0));
        Check(liberty::SecondsUntil(fixedNow, fixedNow, seconds) == TimeError::Past, "equal current time rejected");
        Check(liberty::SecondsUntil(fixedNow + 1, fixedNow, seconds) == TimeError::None && seconds == 1, "fractional second rounds up, never timeout zero");
        Check(liberty::SecondsUntil(fixedNow + 604800 * liberty::kFileTimeSecond, fixedNow, seconds) == TimeError::None && seconds == 604800,
            "seven-day limit accepted");
        Check(liberty::SecondsUntil(fixedNow + 604801 * liberty::kFileTimeSecond, fixedNow, seconds) == TimeError::TooFar, "beyond seven days rejected");
        DYNAMIC_TIME_ZONE_INFORMATION china{}; china.Bias = -480;
        wcscpy_s(china.TimeZoneKeyName, L"China Standard Time");
        Check(liberty::ResolveShutdownTime(date(2026, 9, 15, 21, 0), ticks(date(2026, 9, 15, 12, 59, 42)), seconds, target, &china) == TimeError::None && seconds == 18,
            "local UTC+8 time resolves to correct UTC instant");
        SYSTEMTIME restoredTime{};
        Check(liberty::UtcTicksToLocal(target, restoredTime, &china) && restoredTime.wHour == 21 && restoredTime.wMinute == 0,
            "saved UTC target restores local date and time");

        HWND shutdownDialog = CreateDialogParamW(g_instance, MAKEINTRESOURCEW(IDD_SHUTDOWN), nullptr, ShutdownProc, 0);
        Check(shutdownDialog != nullptr, "native daily shutdown dialog resource loads");
        Check(ShutdownUsesDailySchedule(shutdownDialog) && GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DAILY_LIST) &&
            GetDlgItem(shutdownDialog, IDC_SHUTDOWN_TIME) && GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DAILY_EDIT) &&
            GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DAILY_REFRESH), "daily plan management controls present");
        WORD selectedMinute = 0;
        Check(ReadShutdownDailyTime(shutdownDialog, selectedMinute) && selectedMinute < 24 * 60,
            "native daily time resolves to a valid minute");
        SYSTEMTIME firstDaily{};
        firstDaily.wYear = 2026; firstDaily.wMonth = 9; firstDaily.wDay = 16; firstDaily.wHour = 23; firstDaily.wMinute = 30;
        SendDlgItemMessageW(shutdownDialog, IDC_SHUTDOWN_TIME, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&firstDaily));
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_ADD, BN_CLICKED), 0);
        firstDaily.wHour = 8; firstDaily.wMinute = 5;
        SendDlgItemMessageW(shutdownDialog, IDC_SHUTDOWN_TIME, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&firstDaily));
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_ADD, BN_CLICKED), 0);
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_ADD, BN_CLICKED), 0);
        Check(ReadDailyShutdownList(shutdownDialog) == std::vector<WORD>({8 * 60 + 5, 23 * 60 + 30}),
            "daily dialog keeps several sorted unique times");
        Check(g_dailyShutdownEnabled && g_dailyShutdownTimes == std::vector<WORD>({8 * 60 + 5, 23 * 60 + 30}),
            "adding a plan applies and persists immediately");
        SendDlgItemMessageW(shutdownDialog, IDC_SHUTDOWN_DAILY_LIST, LB_SETCURSEL, 0, 0);
        firstDaily.wHour = 9; firstDaily.wMinute = 10;
        SendDlgItemMessageW(shutdownDialog, IDC_SHUTDOWN_TIME, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&firstDaily));
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_EDIT, BN_CLICKED), 0);
        Check(ReadDailyShutdownList(shutdownDialog) == std::vector<WORD>({9 * 60 + 10, 23 * 60 + 30}),
            "selected daily plan can be edited");
        SendDlgItemMessageW(shutdownDialog, IDC_SHUTDOWN_DAILY_LIST, LB_SETCURSEL, 1, 0);
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_REMOVE, BN_CLICKED), 0);
        Check(ReadDailyShutdownList(shutdownDialog) == std::vector<WORD>({9 * 60 + 10}),
            "selected daily plan can be removed immediately");
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_REFRESH, BN_CLICKED), 0);
        Check(ReadDailyShutdownList(shutdownDialog) == std::vector<WORD>({9 * 60 + 10}),
            "refresh restores persisted plan list");
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_CANCEL, BN_CLICKED), 0);
        Check(!g_dailyShutdownEnabled && ReadDailyShutdownList(shutdownDialog) == std::vector<WORD>({9 * 60 + 10}),
            "disable all retains visible saved plans");
        SendMessageW(shutdownDialog, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_START, BN_CLICKED), 0);
        Check(g_dailyShutdownEnabled, "enable all restores saved plans");
        SendMessageW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DURATION_MODE), BM_CLICK, 0, 0);
        Check(!ShutdownUsesDailySchedule(shutdownDialog) && (GetWindowLongPtrW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_MINUTES), GWL_STYLE) & WS_VISIBLE) &&
            !(GetWindowLongPtrW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DAILY_LIST), GWL_STYLE) & WS_VISIBLE), "mode switch displays duration inputs only");
        SendMessageW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_TIME_MODE), BM_CLICK, 0, 0);
        Check(ShutdownUsesDailySchedule(shutdownDialog) && (GetWindowLongPtrW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_DAILY_LIST), GWL_STYLE) & WS_VISIBLE) &&
            !(GetWindowLongPtrW(GetDlgItem(shutdownDialog, IDC_SHUTDOWN_MINUTES), GWL_STYLE) & WS_VISIBLE), "mode switch displays clock inputs only");
        DestroyWindow(shutdownDialog);

        Check(RegisterClasses(), "window classes registered");
        HWND autoCloseMenu = CreateWindowExW(0, kMenuClass, kAppName, WS_POPUP | WS_CLIPCHILDREN,
            0, 0, 470, 462, nullptr, nullptr, g_instance, nullptr);
        Check(autoCloseMenu != nullptr && !g_menuAutoCloseArmed, "menu auto-close starts disarmed to prevent startup flash");
        MSG closeMessage{};
        SendMessageW(autoCloseMenu, WM_ACTIVATE, WA_INACTIVE, 0);
        Check(!PeekMessageW(&closeMessage, autoCloseMenu, kCloseMenuMessage, kCloseMenuMessage, PM_REMOVE),
            "initial inactive notification does not close menu");
        SendMessageW(autoCloseMenu, WM_ACTIVATE, WA_ACTIVE, 0);
        Check(g_menuAutoCloseArmed, "first activation arms menu auto-close");
        SendMessageW(autoCloseMenu, WM_ACTIVATE, WA_INACTIVE, 0);
        Check(PeekMessageW(&closeMessage, autoCloseMenu, kCloseMenuMessage, kCloseMenuMessage, PM_REMOVE),
            "losing activation schedules menu close");
        DestroyWindow(autoCloseMenu);
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
    if (g_shutdownWindow) DestroyWindow(g_shutdownWindow);
    SetThreadExecutionState(ES_CONTINUOUS);
    RegDeleteTreeW(HKEY_CURRENT_USER, kRegistryKey);
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return result;
}
