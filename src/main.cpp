#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <uxtheme.h>
#include <wtsapi32.h>
#include "daily_shutdown.hpp"
#include "shutdown.hpp"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {

constexpr wchar_t kAppName[] = L"Liberty by Bada";
constexpr wchar_t kLegacyAppName[] = L"Liberty";
constexpr wchar_t kWindowClass[] = L"LibertyByBada.TrayWindow";
constexpr wchar_t kMenuClass[] = L"LibertyByBada.Menu";
constexpr wchar_t kSettingsClass[] = L"LibertyByBada.Settings";
#ifdef LIBERTY_TESTING
constexpr wchar_t kRegistryKey[] = L"Software\\LibertyByBadaRegression";
constexpr wchar_t kRunKey[] = L"Software\\LibertyByBadaRegression\\Run";
#else
constexpr wchar_t kRegistryKey[] = L"Software\\LibertyByBada";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowMenuMessage = WM_APP + 2;
constexpr UINT kDailyShutdownMessage = WM_APP + 3;
constexpr UINT kCloseMenuMessage = WM_APP + 4;
constexpr ULONG_PTR kInjectedMarker = 0x4C494245525459ULL;

enum Command : UINT {
    ID_MAC_MAPPING = 100,
    ID_SLEEP_WITH_DISPLAY,
    ID_PREVENT_SLEEP,
    ID_CLEAR_CACHE,
    ID_SAVE_SCREENSHOTS,
    ID_SETTINGS,
    ID_ABOUT,
    ID_LANGUAGE,
    ID_STARTUP,
    ID_COMMAND_COMBO,
    ID_ALT_COMBO,
    ID_CONTROL_COMBO,
    ID_SETTINGS_SAVE,
    ID_SETTINGS_CLOSE,
    ID_SHUTDOWN,
    ID_PREVENT_LOCK
};

struct MenuItem {
    UINT id;
    bool toggle;
    const wchar_t* english;
    const wchar_t* chinese;
};

struct ModifierChoice {
    WORD virtualKey;
    const wchar_t* english;
    const wchar_t* chinese;
};

constexpr MenuItem kMenuItems[] = {
    {ID_MAC_MAPPING, true,  L"Use macOS shortcuts", L"使用MacOS快捷键"},
    {ID_SLEEP_WITH_DISPLAY, true, L"Turn display off and prevent computer sleep", L"关闭显示器并防止电脑休眠"},
    {ID_PREVENT_SLEEP, true, L"Prevent computer sleep", L"防止电脑休眠"},
    {ID_PREVENT_LOCK, true, L"Prevent automatic lock", L"防止自动锁屏"},
    {ID_CLEAR_CACHE, false, L"Clear cache", L"清理缓存"},
    {ID_SAVE_SCREENSHOTS, true, L"Save screenshots to Desktop instead of clipboard", L"将截图保存至桌面而不是剪贴板"},
    {ID_SHUTDOWN, false, L"Scheduled shutdown", L"定时关机"},
    {ID_SETTINGS, false, L"Settings", L"设置"}
};

constexpr ModifierChoice kModifierChoices[] = {
    {VK_LWIN, L"Left Windows", L"左 Windows"},
    {VK_RWIN, L"Right Windows", L"右 Windows"},
    {VK_LMENU, L"Left Alt", L"左 Alt"},
    {VK_RMENU, L"Right Alt", L"右 Alt"},
    {VK_LCONTROL, L"Left Control", L"左 Control"},
    {VK_RCONTROL, L"Right Control", L"右 Control"},
    {VK_CAPITAL, L"Caps Lock", L"Caps Lock"}
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_menuWindow = nullptr;
HWND g_settingsWindow = nullptr;
HWND g_shutdownWindow = nullptr;
liberty::ShutdownSchedule g_shutdown;
HHOOK g_keyboardHook = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_tray{};
ULONG_PTR g_gdiplusToken = 0;
UINT g_taskbarCreated = 0;
HFONT g_menuFont = nullptr;
HFONT g_settingsFont = nullptr;

bool g_chinese = true;
bool g_macMapping = false;
bool g_displayOffAwake = false;
bool g_preventSleep = false;
bool g_preventLock = false;
bool g_saveScreenshots = false;
bool g_commandDown = false;
bool g_optionDown = false;
bool g_controlDown = false;
bool g_switchingApps = false;
enum class KeyRouteKind { None, Raw, Control, Alt, Swallow };
struct KeyRoute { KeyRouteKind kind = KeyRouteKind::None; WORD key = 0; };
KeyRoute g_keyRoutes[256]{};
#ifdef LIBERTY_TESTING
std::vector<INPUT> g_testKeyEvents;
#endif
bool g_startAtLogin = true;
WORD g_commandKey = VK_LWIN;
WORD g_altKey = VK_LMENU;
WORD g_controlKey = VK_LCONTROL;
int g_menuHover = -1;
bool g_menuAutoCloseArmed = false;
DWORD g_lastClipboardSequence = 0;
bool g_showWindow = false;
bool g_dailyShutdownTrigger = false;
bool g_dailyShutdownEnabled = false;
std::vector<WORD> g_dailyShutdownTimes;
liberty::DailyShutdownTaskStatus g_dailyTaskStatus;
HRESULT g_dailyTaskQueryResult = S_OK;

const wchar_t* T(const wchar_t* english, const wchar_t* chinese) {
    return g_chinese ? chinese : english;
}

int Scale(int value, HWND window = nullptr) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return MulDiv(value, dpi ? static_cast<int>(dpi) : 96, 96);
}

std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return text;
}

bool ReadDword(const wchar_t* name, DWORD& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value);
}

DWORD LoadDword(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    if (!ReadDword(name, value)) value = fallback;
    return value;
}

void SaveDword(const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

bool SaveDailyShutdownSettings(const std::vector<WORD>& times, bool enabled);

void LoadDailyShutdownSettings() {
    g_dailyShutdownEnabled = false;
    g_dailyShutdownTimes.clear();
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(key, L"DailyShutdownTimes", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
            type == REG_BINARY && bytes > 0 && bytes % sizeof(WORD) == 0 &&
            bytes <= liberty::kMaxDailyShutdownTimes * sizeof(WORD)) {
            g_dailyShutdownTimes.resize(bytes / sizeof(WORD));
            if (RegQueryValueExW(key, L"DailyShutdownTimes", nullptr, &type,
                reinterpret_cast<BYTE*>(g_dailyShutdownTimes.data()), &bytes) != ERROR_SUCCESS ||
                !liberty::NormalizeDailyShutdownTimes(g_dailyShutdownTimes)) g_dailyShutdownTimes.clear();
        }
        DWORD enabled = 0, enabledBytes = sizeof(enabled);
        if (RegQueryValueExW(key, L"DailyShutdownEnabled", nullptr, &type,
            reinterpret_cast<BYTE*>(&enabled), &enabledBytes) == ERROR_SUCCESS && type == REG_DWORD)
            g_dailyShutdownEnabled = enabled != 0 && !g_dailyShutdownTimes.empty();
        RegCloseKey(key);
    }
#ifdef LIBERTY_TESTING
    g_dailyTaskStatus = {};
    g_dailyTaskStatus.exists = g_dailyShutdownEnabled;
    g_dailyTaskStatus.enabled = g_dailyShutdownEnabled;
    g_dailyTaskStatus.times = g_dailyShutdownTimes;
    g_dailyTaskQueryResult = S_OK;
#else
    liberty::DailyShutdownTaskStatus actual;
    g_dailyTaskQueryResult = liberty::QueryDailyShutdownTask(actual);
    if (SUCCEEDED(g_dailyTaskQueryResult)) {
        g_dailyTaskStatus = actual;
        if (actual.exists && !actual.times.empty()) {
            // Windows Task Scheduler is the source of truth. Repair stale/missing app state.
            g_dailyShutdownTimes = actual.times;
            g_dailyShutdownEnabled = actual.enabled;
            SaveDailyShutdownSettings(g_dailyShutdownTimes, g_dailyShutdownEnabled);
        } else if (actual.exists) {
            g_dailyShutdownEnabled = false;
            SaveDailyShutdownSettings(g_dailyShutdownTimes, false);
        } else if (!actual.exists && g_dailyShutdownEnabled) {
            g_dailyShutdownEnabled = false;
            SaveDailyShutdownSettings(g_dailyShutdownTimes, false);
        }
    }
#endif
}

bool SaveDailyShutdownSettings(const std::vector<WORD>& times, bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>(times.size() * sizeof(WORD));
    const DWORD value = enabled ? 1 : 0;
    LONG result = RegSetValueExW(key, L"DailyShutdownTimes", 0, REG_BINARY,
        times.empty() ? nullptr : reinterpret_cast<const BYTE*>(times.data()), bytes);
    if (result == ERROR_SUCCESS)
        result = RegSetValueExW(key, L"DailyShutdownEnabled", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::wstring ModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

bool DeleteRunValue(const wchar_t* name) {
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (opened != ERROR_SUCCESS) return opened == ERROR_FILE_NOT_FOUND;
    const LONG result = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool SetStartAtLogin(bool enabled) {
    bool success = true;
    if (enabled) {
        const std::wstring path = ModulePath();
        if (path.empty()) return false;
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                            nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
        const std::wstring command = L"\"" + path + L"\"";
        success = RegSetValueExW(key, kAppName, 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(command.c_str()),
                                 static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        RegCloseKey(key);
        DeleteRunValue(kLegacyAppName);
    } else {
        success = DeleteRunValue(kAppName) && DeleteRunValue(kLegacyAppName);
    }
    if (success) {
        g_startAtLogin = enabled;
        SaveDword(L"StartAtLogin", enabled ? 1 : 0);
    }
    return success;
}

int ModifierChoiceIndex(WORD key) {
    for (size_t index = 0; index < ARRAYSIZE(kModifierChoices); ++index)
        if (kModifierChoices[index].virtualKey == key) return static_cast<int>(index);
    return 0;
}

bool IsModifierChoice(WORD key) {
    for (const ModifierChoice& choice : kModifierChoices)
        if (choice.virtualKey == key) return true;
    return false;
}

void LoadModifierMappings() {
    const WORD command = static_cast<WORD>(LoadDword(L"CommandKey", VK_LWIN));
    const WORD alt = static_cast<WORD>(LoadDword(L"AltKey", VK_LMENU));
    const WORD control = static_cast<WORD>(LoadDword(L"ControlKey", VK_LCONTROL));
    if (IsModifierChoice(command) && IsModifierChoice(alt) && IsModifierChoice(control) &&
        command != alt && command != control && alt != control) {
        g_commandKey = command;
        g_altKey = alt;
        g_controlKey = control;
    }
}

void InitializeLanguage() {
    const DWORD saved = LoadDword(L"Language", 2);
    if (saved == 0) g_chinese = false;
    else if (saved == 1) g_chinese = true;
    else g_chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
}

HFONT CreateUiFont(HWND window, int logicalHeight, LONG weight = FW_NORMAL) {
    return CreateFontW(-Scale(logicalHeight, window), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
}

void SetRoundedWindow(HWND window) {
    const DWORD corner = 2;
    DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
}

void DrawTrinity(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float size = (std::min)(bounds.Width, bounds.Height);
    const float x = bounds.X + (bounds.Width - size) / 2.0f;
    const float y = bounds.Y + (bounds.Height - size) / 2.0f;
    const Gdiplus::RectF arc(x + size * 0.16f, y + size * 0.16f, size * 0.68f, size * 0.68f);
    const Gdiplus::Color colors[] = {
        Gdiplus::Color(255, 27, 184, 224),
        Gdiplus::Color(255, 79, 140, 255),
        Gdiplus::Color(255, 111, 84, 255)
    };
    const float stroke = (std::max)(2.0f, size * 0.13f);
    for (int index = 0; index < 3; ++index) {
        Gdiplus::Pen pen(colors[index], stroke);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawArc(&pen, arc, -90.0f + index * 120.0f, 88.0f);
    }
}

void DrawTrinity(HDC hdc, const RECT& rect) {
    Gdiplus::Graphics graphics(hdc);
    DrawTrinity(graphics, Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                                         static_cast<float>(rect.right - rect.left),
                                         static_cast<float>(rect.bottom - rect.top)));
}

HICON CreateTrayIcon() {
    if (!g_gdiplusToken) return LoadIconW(nullptr, IDI_APPLICATION);
    Gdiplus::Bitmap bitmap(64, 64, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    DrawTrinity(graphics, Gdiplus::RectF(0, 0, 64, 64));
    HICON icon = nullptr;
    if (bitmap.GetHICON(&icon) != Gdiplus::Ok || !icon) return LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

void UpdateTrayTip() {
    std::wstring text = std::wstring(kAppName) + L" — ";
    text += g_macMapping ? T(L"macOS shortcut mapping on", L"macOS 快捷键映射已开启")
                         : T(L"ready", L"已就绪");
    lstrcpynW(g_tray.szTip, text.c_str(), ARRAYSIZE(g_tray.szTip));
    if (g_tray.hWnd) Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void Notify(const std::wstring& text) {
    if (!g_tray.hWnd) return;
    const UINT flags = g_tray.uFlags;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_INFO;
    lstrcpynW(g_tray.szInfoTitle, kAppName, ARRAYSIZE(g_tray.szInfoTitle));
    lstrcpynW(g_tray.szInfo, text.c_str(), ARRAYSIZE(g_tray.szInfo));
    g_tray.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = flags;
}

void ApplyPreventSleep() {
    EXECUTION_STATE state = ES_CONTINUOUS;
    if (g_preventSleep || g_displayOffAwake || g_preventLock) state |= ES_SYSTEM_REQUIRED;
    if (g_preventLock && !g_displayOffAwake) state |= ES_DISPLAY_REQUIRED;
    SetThreadExecutionState(state);
}

void MaintainUnlockedSession() {
    if (!g_preventLock) return;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return;
    LPWSTR sessionData = nullptr;
    DWORD sessionBytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session, WTSSessionInfoEx, &sessionData, &sessionBytes)) return;
    const auto* sessionInfo = reinterpret_cast<const WTSINFOEXW*>(sessionData);
    const bool unlocked = sessionBytes >= sizeof(WTSINFOEXW) && sessionInfo->Level == 1 &&
        sessionInfo->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
    WTSFreeMemory(sessionData);
    if (!unlocked) return;
    // Only reduce idle time on the already-unlocked interactive desktop.
    // Do not interact with Winlogon, the lock screen, or a UAC desktop.
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return;
    wchar_t name[64]{};
    DWORD bytes = 0;
    const BOOL named = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes);
    CloseDesktop(desktop);
    if (!named || _wcsicmp(name, L"Default") != 0) return;
    LASTINPUTINFO last{sizeof(last)};
    if (!GetLastInputInfo(&last) || GetTickCount() - last.dwTime < 45000) return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dwExtraInfo = kInjectedMarker;
    // A zero-distance mouse event resets idle time without moving the pointer.
    if (SendInput(1, &input, sizeof(input))) {
        OutputDebugStringW(L"Liberty: automatic-lock idle reset succeeded\n");
        if (g_displayOffAwake) PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    }
}

void TurnOffDisplay() {
    if (g_displayOffAwake) ApplyPreventSleep();
    PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
}

void SendKey(WORD virtualKey, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if ((virtualKey >= VK_PRIOR && virtualKey <= VK_DOWN) || virtualKey == VK_INSERT ||
        virtualKey == VK_DELETE || virtualKey == VK_RCONTROL || virtualKey == VK_RMENU ||
        virtualKey == VK_LWIN || virtualKey == VK_RWIN || virtualKey == VK_SNAPSHOT)
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    input.ki.dwExtraInfo = kInjectedMarker;
#ifdef LIBERTY_TESTING
    g_testKeyEvents.push_back(input);
#else
    SendInput(1, &input, sizeof(input));
#endif
}

void SendControlCombo(WORD virtualKey, bool down) {
    if (!g_controlDown) SendKey(VK_CONTROL, true);
    SendKey(virtualKey, down);
    if (!g_controlDown) SendKey(VK_CONTROL, false);
}

void SendAltCombo(WORD virtualKey, bool down) {
    SendKey(VK_MENU, true);
    SendKey(virtualKey, down);
    SendKey(VK_MENU, false);
}

bool IsPhysicalControl(WORD key) { return key == VK_LCONTROL || key == VK_RCONTROL; }
bool IsKeyDown(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

void ReleaseMappedState() {
    for (KeyRoute& route : g_keyRoutes) {
        if (route.kind != KeyRouteKind::None && route.kind != KeyRouteKind::Swallow)
            SendKey(route.key, false);
        route = {};
    }
    if (g_switchingApps) SendKey(VK_MENU, false);
    g_switchingApps = false;
    if (g_optionDown) SendKey(VK_MENU, false);
    if (g_controlDown && !IsPhysicalControl(g_controlKey)) SendKey(VK_CONTROL, false);
    g_commandDown = false;
    g_optionDown = false;
    g_controlDown = false;
}

void MinimizeForeground() {
    const HWND foreground = GetForegroundWindow();
    if (foreground) ShowWindow(foreground, SW_MINIMIZE);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data) {
    if (code < 0 || !g_macMapping) return CallNextHookEx(g_keyboardHook, code, message, data);
    const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (event->dwExtraInfo == kInjectedMarker) return CallNextHookEx(g_keyboardHook, code, message, data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(g_keyboardHook, code, message, data);
    const DWORD key = event->vkCode;
    if (key == g_commandKey) {
        g_commandDown = down;
        if (up && g_switchingApps) { SendKey(VK_MENU, false); g_switchingApps = false; }
        return 1;
    }
    if (key == g_altKey) {
        g_optionDown = down;
        SendKey(VK_MENU, down);
        return 1;
    }
    if (key == g_controlKey) {
        g_controlDown = down;
        if (IsPhysicalControl(g_controlKey)) return CallNextHookEx(g_keyboardHook, code, message, data);
        SendKey(VK_CONTROL, down);
        return 1;
    }
    if (key >= ARRAYSIZE(g_keyRoutes)) return CallNextHookEx(g_keyboardHook, code, message, data);
    KeyRoute& route = g_keyRoutes[key];
    // Key-up belongs to its original translation, even if Cmd was released first.
    if (up && route.kind != KeyRouteKind::None) {
        if (route.kind != KeyRouteKind::Swallow) SendKey(route.key, false);
        route = {};
        return 1;
    }
    if (down && route.kind != KeyRouteKind::None) {
        if (route.kind == KeyRouteKind::Control) SendControlCombo(route.key, true);
        else if (route.kind == KeyRouteKind::Alt) SendAltCombo(route.key, true);
        else if (route.kind == KeyRouteKind::Raw) SendKey(route.key, true);
        return 1;
    }
    if (!g_commandDown) {
        if (g_optionDown && (key == VK_LEFT || key == VK_RIGHT || key == VK_BACK)) {
            route = {KeyRouteKind::Control, static_cast<WORD>(key)};
            SendKey(VK_MENU, false);
            SendControlCombo(static_cast<WORD>(key), down);
            SendKey(VK_MENU, true);
            return 1;
        }
        return CallNextHookEx(g_keyboardHook, code, message, data);
    }
    if (key == VK_TAB) {
        route = {KeyRouteKind::Raw, VK_TAB};
        if (down && !g_switchingApps) { SendKey(VK_MENU, true); g_switchingApps = true; }
        SendKey(VK_TAB, down);
        return 1;
    }
    if (key == 'Q') {
        route = {KeyRouteKind::Alt, VK_F4};
        SendAltCombo(VK_F4, down);
        return 1;
    }
    if (key == 'H' || key == 'M') {
        route = {KeyRouteKind::Swallow, 0};
        if (down) MinimizeForeground();
        return 1;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
        route = {KeyRouteKind::Raw, static_cast<WORD>(key == VK_LEFT ? VK_HOME : VK_END)};
        SendKey(key == VK_LEFT ? VK_HOME : VK_END, down);
        return 1;
    }
    if (key == VK_UP || key == VK_DOWN) {
        route = {KeyRouteKind::Control, static_cast<WORD>(key == VK_UP ? VK_HOME : VK_END)};
        SendControlCombo(key == VK_UP ? VK_HOME : VK_END, down);
        return 1;
    }
    if (key == VK_SPACE) {
        route = {KeyRouteKind::Swallow, 0};
        if (down) {
            SendKey(VK_LWIN, true); SendKey('S', true); SendKey('S', false); SendKey(VK_LWIN, false);
        }
        return 1;
    }
    if ((key == '3' || key == '4') && IsKeyDown(VK_SHIFT)) {
        route = {KeyRouteKind::Swallow, 0};
        if (down && key == '3') { SendKey(VK_SNAPSHOT, true); SendKey(VK_SNAPSHOT, false); }
        if (down && key == '4') {
            SendKey(VK_LWIN, true); SendKey(VK_SHIFT, true); SendKey('S', true); SendKey('S', false);
            SendKey(VK_SHIFT, false); SendKey(VK_LWIN, false);
        }
        return 1;
    }
    route = {KeyRouteKind::Control, static_cast<WORD>(key)};
    SendControlCombo(static_cast<WORD>(key), down);
    return 1;
}

bool SetMacMapping(bool enabled) {
    if (enabled && !g_keyboardHook) {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, g_instance, 0);
        if (!g_keyboardHook) {
            MessageBoxW(g_window, T(L"Liberty by Bada could not enable shortcut mapping.", L"Liberty by Bada 无法开启快捷键映射。"),
                        kAppName, MB_OK | MB_ICONERROR);
            return false;
        }
    }
    if (!enabled && g_keyboardHook) {
        ReleaseMappedState();
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_macMapping = enabled;
    SaveDword(L"MacMapping", enabled ? 1 : 0);
    UpdateTrayTip();
    return true;
}

int FindImageEncoder(const wchar_t* mimeType, CLSID* clsid) {
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes) return -1;
    std::vector<BYTE> buffer(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return -1;
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(codecs[index].MimeType, mimeType) == 0) {
            *clsid = codecs[index].Clsid;
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::wstring DesktopPngPath() {
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop)) || !desktop) return {};
    std::wstring directory(desktop);
    CoTaskMemFree(desktop);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[128]{};
    swprintf_s(name, L"Liberty Screenshot %04u-%02u-%02u_%02u%02u%02u.png",
               now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::wstring path = directory + L"\\" + name;
    for (int suffix = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++suffix) {
        swprintf_s(name, L"Liberty Screenshot %04u-%02u-%02u_%02u%02u%02u (%d).png",
                   now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, suffix);
        path = directory + L"\\" + name;
    }
    return path;
}

bool SaveBitmapPng(HBITMAP source, const std::wstring& path) {
    if (!source || path.empty()) return false;
    BITMAP sourceInfo{};
    if (!GetObjectW(source, sizeof(sourceInfo), &sourceInfo) || sourceInfo.bmWidth <= 0 || sourceInfo.bmHeight <= 0) return false;
    HDC screen = GetDC(nullptr);
    HDC sourceDc = CreateCompatibleDC(screen);
    HDC targetDc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sourceInfo.bmWidth;
    info.bmiHeader.biHeight = -sourceInfo.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP target = screen ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool copied = false;
    if (screen && sourceDc && targetDc && target && pixels) {
        HBITMAP oldSource = static_cast<HBITMAP>(SelectObject(sourceDc, source));
        HBITMAP oldTarget = static_cast<HBITMAP>(SelectObject(targetDc, target));
        copied = BitBlt(targetDc, 0, 0, sourceInfo.bmWidth, sourceInfo.bmHeight, sourceDc, 0, 0, SRCCOPY) != FALSE;
        SelectObject(sourceDc, oldSource);
        SelectObject(targetDc, oldTarget);
    }
    bool saved = false;
    if (copied && pixels) {
        auto* bgra = reinterpret_cast<BYTE*>(pixels);
        for (size_t index = 0; index < static_cast<size_t>(sourceInfo.bmWidth) * sourceInfo.bmHeight; ++index) bgra[index * 4 + 3] = 255;
        CLSID encoder{};
        if (FindImageEncoder(L"image/png", &encoder) >= 0) {
            Gdiplus::Bitmap image(sourceInfo.bmWidth, sourceInfo.bmHeight, sourceInfo.bmWidth * 4,
                                  PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(pixels));
            saved = image.Save(path.c_str(), &encoder, nullptr) == Gdiplus::Ok;
        }
    }
    if (target) DeleteObject(target);
    if (sourceDc) DeleteDC(sourceDc);
    if (targetDc) DeleteDC(targetDc);
    if (screen) ReleaseDC(nullptr, screen);
    return saved;
}

void SaveClipboardScreenshot() {
    if (!g_saveScreenshots || !IsClipboardFormatAvailable(CF_BITMAP) || !OpenClipboard(g_window)) return;
    HBITMAP source = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    HBITMAP copy = source ? static_cast<HBITMAP>(CopyImage(source, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION)) : nullptr;
    const DWORD screenshotSequence = GetClipboardSequenceNumber();
    CloseClipboard();
    if (!copy) return;
    const bool saved = SaveBitmapPng(copy, DesktopPngPath());
    DeleteObject(copy);
    if (saved) {
        if (OpenClipboard(g_window)) {
            // Encoding may take time. Never erase something copied in the meantime.
            if (GetClipboardSequenceNumber() == screenshotSequence) EmptyClipboard();
            CloseClipboard();
        }
        Notify(T(L"Screenshot saved to Desktop.", L"截图已保存到桌面。"));
    }
}

void OpenCacheCleanup() {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"cleanmgr.exe", L"/d C", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        ShellExecuteW(nullptr, L"open", L"ms-settings:storagesense", nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowAbout() {
    MessageBoxW(g_menuWindow ? g_menuWindow : g_window,
                T(L"Liberty by Bada 0.1.6\n\nA small Windows control panel.", L"Liberty by Bada 0.1.6\n\n一个简约的 Windows 控制面板。"),
                kAppName, MB_OK | MB_ICONINFORMATION);
}

void UpdateMenuToggles() {
    if (!g_menuWindow) return;
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_MAC_MAPPING), g_macMapping ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_SLEEP_WITH_DISPLAY), g_displayOffAwake ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_PREVENT_SLEEP), g_preventSleep ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_PREVENT_LOCK), g_preventLock ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_SAVE_SCREENSHOTS), g_saveScreenshots ? BST_CHECKED : BST_UNCHECKED);
}

void ShowMenu(HWND owner);

bool ShutdownUsesDailySchedule(HWND window) {
    return Button_GetCheck(GetDlgItem(window, IDC_SHUTDOWN_TIME_MODE)) == BST_CHECKED;
}

bool ReadShutdownDailyTime(HWND window, WORD& minuteOfDay) {
    SYSTEMTIME time{};
    if (SendDlgItemMessageW(window, IDC_SHUTDOWN_TIME, DTM_GETSYSTEMTIME, 0,
        reinterpret_cast<LPARAM>(&time)) != GDT_VALID || time.wHour > 23 || time.wMinute > 59) return false;
    minuteOfDay = static_cast<WORD>(time.wHour * 60 + time.wMinute);
    return true;
}

void PopulateDailyShutdownList(HWND window, const std::vector<WORD>& values) {
    const HWND list = GetDlgItem(window, IDC_SHUTDOWN_DAILY_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (WORD value : values) {
        SYSTEMTIME now{}, next{};
        GetLocalTime(&now);
        next = now;
        next.wHour = value / 60;
        next.wMinute = value % 60;
        next.wSecond = 0;
        next.wMilliseconds = 0;
        FILETIME nowFile{}, nextFile{};
        SystemTimeToFileTime(&now, &nowFile);
        SystemTimeToFileTime(&next, &nextFile);
        const ULONGLONG nowTicks = (static_cast<ULONGLONG>(nowFile.dwHighDateTime) << 32) | nowFile.dwLowDateTime;
        ULONGLONG nextTicks = (static_cast<ULONGLONG>(nextFile.dwHighDateTime) << 32) | nextFile.dwLowDateTime;
        if (nextTicks <= nowTicks) nextTicks += 24ULL * 60 * 60 * liberty::kFileTimeSecond;
        nextFile.dwLowDateTime = static_cast<DWORD>(nextTicks);
        nextFile.dwHighDateTime = static_cast<DWORD>(nextTicks >> 32);
        FileTimeToSystemTime(&nextFile, &next);
        const bool inSystemTask = std::find(g_dailyTaskStatus.times.begin(), g_dailyTaskStatus.times.end(), value) !=
            g_dailyTaskStatus.times.end();
        const wchar_t* state = inSystemTask && g_dailyTaskStatus.enabled ? T(L"enabled", L"已启用") :
            inSystemTask ? T(L"disabled", L"已停用") : T(L"saved", L"已保存");
        wchar_t nextText[32]{};
        swprintf_s(nextText, L"%02u-%02u %02u:%02u", next.wMonth, next.wDay, next.wHour, next.wMinute);
        std::wstring label = liberty::DailyShutdownTimeLabel(value) + L"    " + state +
            T(L" · daily · next ", L" · 每天 · 下次 ") + nextText;
        const LRESULT index = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (index >= 0) SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(index), value);
    }
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_REMOVE), false);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_EDIT), false);
}

std::vector<WORD> ReadDailyShutdownList(HWND window) {
    std::vector<WORD> values;
    const HWND list = GetDlgItem(window, IDC_SHUTDOWN_DAILY_LIST);
    const LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        const LRESULT value = SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(index), 0);
        if (value >= 0 && value < 24 * 60) values.push_back(static_cast<WORD>(value));
    }
    liberty::NormalizeDailyShutdownTimes(values);
    return values;
}

std::wstring ScheduledTaskDate(DATE value) {
    SYSTEMTIME time{};
    if (value <= 0 || !VariantTimeToSystemTime(value, &time)) return T(L"unavailable", L"不可用");
    wchar_t buffer[40]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
    return buffer;
}

bool DailyTaskUsesCurrentExecutable() {
    if (!g_dailyTaskStatus.exists) return false;
    const std::wstring current = ModulePath();
    return !current.empty() && _wcsicmp(g_dailyTaskStatus.executable.c_str(), current.c_str()) == 0 &&
        Lower(g_dailyTaskStatus.arguments).find(L"--daily-shutdown") != std::wstring::npos;
}

void ShowShutdownMode(HWND window) {
    const bool daily = ShutdownUsesDailySchedule(window);
    for (int id : {IDC_SHUTDOWN_PRESET_LABEL, IDC_SHUTDOWN_PRESET, IDC_SHUTDOWN_LABEL, IDC_SHUTDOWN_MINUTES})
        ShowWindow(GetDlgItem(window, id), daily ? SW_HIDE : SW_SHOW);
    for (int id : {IDC_SHUTDOWN_TIME_LABEL, IDC_SHUTDOWN_TIME, IDC_SHUTDOWN_DAILY_LIST,
        IDC_SHUTDOWN_DAILY_ADD, IDC_SHUTDOWN_DAILY_EDIT, IDC_SHUTDOWN_DAILY_REMOVE,
        IDC_SHUTDOWN_DAILY_REFRESH})
        ShowWindow(GetDlgItem(window, id), daily ? SW_SHOW : SW_HIDE);
    SetDlgItemTextW(window, IDC_SHUTDOWN_START,
        daily ? T(L"Enable / repair", L"启用／修复") : T(L"Start timer", L"开始计时"));
    SetDlgItemTextW(window, IDC_SHUTDOWN_CANCEL,
        daily ? T(L"Disable all", L"停用全部") : T(L"Cancel timer", L"取消关机"));
    SetDlgItemTextW(window, IDC_SHUTDOWN_HINT, daily ? T(
        L"This list is read from Windows Task Scheduler. Add, edit and remove apply immediately. Refresh imports external changes. Each time repeats daily and starts a 60-second countdown.",
        L"列表直接读取 Windows 任务计划。添加、修改和删除会立即生效；“刷新”可导入外部更改。每个时间每天重复，到点启动 60 秒倒计时。") : T(
        L"Starts one Windows countdown. It remains active after closing Liberty. Unsaved applications may prevent shutdown.",
        L"启动一次 Windows 关机倒计时。关闭 Liberty 后倒计时仍有效，未保存的工作可能阻止关机。"));
}

void RefreshShutdownDialog(HWND window) {
    const bool active = g_shutdown.Active();
    const bool daily = ShutdownUsesDailySchedule(window);
    std::wstring status = T(L"No shutdown scheduled.", L"尚未设置定时关机。");
    if (daily) {
        if (FAILED(g_dailyTaskQueryResult)) {
            wchar_t code[24]{};
            swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(g_dailyTaskQueryResult));
            status = T(L"Could not read Windows Task Scheduler: ", L"无法读取 Windows 任务计划：") + std::wstring(code);
        } else if (g_dailyTaskStatus.exists && g_dailyTaskStatus.times.empty()) {
            status = T(L"The Windows task contains no readable daily plans.\nClick Enable / repair to rebuild it.",
                L"Windows 任务中没有可读取的每日计划。\n点击“启用／修复”重新创建。");
        } else if (g_dailyTaskStatus.exists) {
            status = g_dailyTaskStatus.enabled ? T(L"Windows task enabled", L"Windows 计划已启用") :
                T(L"Windows task disabled", L"Windows 计划已停用");
            status += T(L" · plans: ", L" · 计划数：") + std::to_wstring(g_dailyTaskStatus.times.size());
            status += T(L"\nNext run: ", L"\n下次执行：") + ScheduledTaskDate(g_dailyTaskStatus.nextRun);
            if (!DailyTaskUsesCurrentExecutable())
                status += T(L" · path needs repair", L" · 程序路径需修复");
        } else if (!g_dailyShutdownTimes.empty()) {
            status = T(L"Schedule disabled · saved plans: ", L"计划已停用 · 已保存：") +
                std::to_wstring(g_dailyShutdownTimes.size());
            status += T(L"\nClick Enable / repair to create the Windows task.", L" 条\n点击“启用／修复”创建 Windows 任务。");
        } else {
            status = T(L"No daily shutdown plans. Choose a time and click Add.",
                L"没有每日关机计划。选择时间后点击“添加”。");
        }
        if (active) {
            const ULONGLONG seconds = g_shutdown.SecondsRemaining();
            wchar_t countdown[80]{};
            swprintf_s(countdown, T(L" · one-time %llu:%02llu:%02llu", L" · 临时倒计时 %llu:%02llu:%02llu"),
                seconds / 3600, seconds / 60 % 60, seconds % 60);
            status += countdown;
        }
    } else if (active) {
        const ULONGLONG seconds = g_shutdown.SecondsRemaining();
        wchar_t buffer[160]{};
        if (seconds) swprintf_s(buffer, T(L"Shutdown in %llu:%02llu:%02llu", L"距离关机还有 %llu:%02llu:%02llu"),
            seconds / 3600, seconds / 60 % 60, seconds % 60);
        else swprintf_s(buffer, L"%s", T(L"Shutdown requested. Unsaved work may need attention.", L"已请求关机；未保存的工作可能需要处理。"));
        status = buffer;
    }
    SetDlgItemTextW(window, IDC_SHUTDOWN_STATUS, status.c_str());
    const bool hasDailyTimes = !ReadDailyShutdownList(window).empty();
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_START), daily ? hasDailyTimes : !active);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_CANCEL), daily ? g_dailyTaskStatus.exists : active);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DURATION_MODE), TRUE);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_TIME_MODE), TRUE);
    for (int id : {IDC_SHUTDOWN_PRESET, IDC_SHUTDOWN_MINUTES}) EnableWindow(GetDlgItem(window, id), !active);
    for (int id : {IDC_SHUTDOWN_TIME, IDC_SHUTDOWN_DAILY_LIST, IDC_SHUTDOWN_DAILY_ADD,
        IDC_SHUTDOWN_DAILY_REFRESH}) EnableWindow(GetDlgItem(window, id), daily);
    const LRESULT selection = SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_EDIT), daily && selection != LB_ERR);
    EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_REMOVE), daily && selection != LB_ERR);
}

void DailyShutdownTaskError(HWND window, HRESULT error) {
    std::wstring message;
    if (error == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
        message = T(L"This Windows account cannot update the daily shutdown task.", L"当前 Windows 账户没有修改每日关机任务的权限。");
    else
        message = T(L"Windows Task Scheduler could not update the daily shutdown plan.", L"Windows 任务计划程序无法更新每日关机计划。");
    wchar_t code[24]{};
    swprintf_s(code, L" (0x%08lX)", static_cast<unsigned long>(error));
    message += code;
    MessageBoxW(window, message.c_str(), kAppName, MB_OK | MB_ICONERROR);
}

void ReloadDailyShutdownPlans(HWND window, WORD preferred = 0xffff) {
    LoadDailyShutdownSettings();
    PopulateDailyShutdownList(window, g_dailyShutdownTimes);
    if (preferred < 24 * 60) {
        for (size_t index = 0; index < g_dailyShutdownTimes.size(); ++index) {
            if (g_dailyShutdownTimes[index] == preferred) {
                SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_SETCURSEL, index, 0);
                break;
            }
        }
    }
    RefreshShutdownDialog(window);
}

bool ApplyDailyShutdownPlans(HWND window, std::vector<WORD> values, bool enabled, WORD preferred = 0xffff) {
    if (!values.empty() && !liberty::NormalizeDailyShutdownTimes(values)) {
        MessageBoxW(window, T(L"The daily shutdown plan is invalid.", L"每日关机计划无效。"),
            kAppName, MB_OK | MB_ICONWARNING);
        return false;
    }
    if (values.empty()) enabled = false;
    const std::vector<WORD> previousTimes = g_dailyShutdownTimes;
    const bool previousEnabled = g_dailyShutdownEnabled;
    HRESULT result = S_OK;
#ifndef LIBERTY_TESTING
    result = enabled ? liberty::ApplyDailyShutdownTask(values, ModulePath()) : liberty::DeleteDailyShutdownTask();
#else
    (void)previousTimes;
    (void)previousEnabled;
#endif
    if (FAILED(result)) {
        DailyShutdownTaskError(window, result);
        return false;
    }
    if (!SaveDailyShutdownSettings(values, enabled)) {
#ifndef LIBERTY_TESTING
        if (previousEnabled && !previousTimes.empty()) liberty::ApplyDailyShutdownTask(previousTimes, ModulePath());
        else liberty::DeleteDailyShutdownTask();
#endif
        MessageBoxW(window, T(L"The plan could not be saved; the previous Windows task was restored.",
            L"未能保存计划，已恢复之前的 Windows 任务。"), kAppName, MB_OK | MB_ICONERROR);
        return false;
    }
    g_dailyShutdownTimes = std::move(values);
    g_dailyShutdownEnabled = enabled;
    SaveDword(L"ShutdownDailyMode", 1);
    ReloadDailyShutdownPlans(window, preferred);
    return true;
}

void ShutdownError(HWND window, DWORD error) {
    std::wstring message;
    if (error == ERROR_SHUTDOWN_IN_PROGRESS || error == ERROR_SHUTDOWN_IS_SCHEDULED)
        message = T(L"Windows already has a shutdown pending. Cancel it before scheduling another.", L"Windows 已有待执行的关机，请先取消后再设置。");
    else if (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_NOT_ALL_ASSIGNED || error == ERROR_ACCESS_DENIED)
        message = T(L"This Windows account is not allowed to schedule shutdown.", L"当前 Windows 账户没有设置关机的权限。");
    else if (error == ERROR_INVALID_TIME)
        message = T(L"Choose a valid future date and time within 7 days. If today's time has passed, select tomorrow.",
            L"请选择未来 7 天内的有效时间。如果今天的时刻已过，请把日期改为明天。");
    else {
        wchar_t detail[512]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, detail, ARRAYSIZE(detail), nullptr);
        message = T(L"Could not update the shutdown timer. ", L"无法更新定时关机。 ");
        message += detail;
    }
    message += L" (" + std::to_wstring(error) + L")";
    MessageBoxW(window, message.c_str(), kAppName, MB_OK | MB_ICONERROR);
}

INT_PTR CALLBACK ShutdownProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        g_shutdownWindow = window;
        SetRoundedWindow(window);
        SetWindowTextW(window, T(L"Scheduled shutdown - Liberty by Bada", L"定时关机 — Liberty by Bada"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_DURATION_MODE, T(L"After a duration", L"倒计时关机"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_TIME_MODE, T(L"Daily schedule", L"每日定时关机"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_TIME_LABEL, T(L"Time (24-hour)", L"时间（24 小时制）"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_DAILY_ADD, T(L"Add", L"添加"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_DAILY_EDIT, T(L"Edit", L"修改"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_DAILY_REMOVE, T(L"Remove", L"删除"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_DAILY_REFRESH, T(L"Refresh", L"刷新"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_PRESET_LABEL, T(L"Duration", L"常用时长"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_LABEL, T(L"Minutes (1-10080)", L"分钟（1–10080）"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_START, T(L"Start timer", L"开始计时"));
        SetDlgItemTextW(window, IDC_SHUTDOWN_CANCEL, T(L"Cancel timer", L"取消关机"));
        SetDlgItemTextW(window, IDCANCEL, T(L"Back", L"返回"));
        const wchar_t* english[] = {L"15 minutes", L"30 minutes", L"1 hour", L"2 hours", L"Custom"};
        const wchar_t* chinese[] = {L"15 分钟", L"30 分钟", L"1 小时", L"2 小时", L"自定义"};
        for (int i = 0; i < 5; ++i)
            SendDlgItemMessageW(window, IDC_SHUTDOWN_PRESET, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(english[i], chinese[i])));
        const DWORD initialMinutes = g_shutdown.Active() ? g_shutdown.Minutes() : 60;
        const int initialIndex = initialMinutes == 15 ? 0 : initialMinutes == 30 ? 1 : initialMinutes == 60 ? 2 : initialMinutes == 120 ? 3 : 4;
        SetDlgItemTextW(window, IDC_SHUTDOWN_MINUTES, std::to_wstring(initialMinutes).c_str());
        SendDlgItemMessageW(window, IDC_SHUTDOWN_PRESET, CB_SETCURSEL, initialIndex, 0);
        SendDlgItemMessageW(window, IDC_SHUTDOWN_MINUTES, EM_SETLIMITTEXT, 16, 0);
        LoadDailyShutdownSettings();
        PopulateDailyShutdownList(window, g_dailyShutdownTimes);
        const bool daily = g_shutdown.Active() ? false : LoadDword(L"ShutdownDailyMode", LoadDword(L"ShutdownClockMode", 1)) != 0;
        CheckRadioButton(window, IDC_SHUTDOWN_DURATION_MODE, IDC_SHUTDOWN_TIME_MODE,
            daily ? IDC_SHUTDOWN_TIME_MODE : IDC_SHUTDOWN_DURATION_MODE);
        SendDlgItemMessageW(window, IDC_SHUTDOWN_TIME, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH:mm"));
        SYSTEMTIME selected{};
        const ULONGLONG now = liberty::UtcNowTicks();
        const ULONGLONG minuteTicks = 60 * liberty::kFileTimeSecond;
        const ULONGLONG defaultTime = ((now + 3600 * liberty::kFileTimeSecond + minuteTicks - 1) / minuteTicks) * minuteTicks;
        liberty::UtcTicksToLocal(defaultTime, selected);
        SendDlgItemMessageW(window, IDC_SHUTDOWN_TIME, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&selected));
        ShowShutdownMode(window);
        RefreshShutdownDialog(window);
        SetTimer(window, 1, 1000, nullptr);
        SetFocus(GetDlgItem(window, g_shutdown.Active() ? IDC_SHUTDOWN_CANCEL : daily ? IDC_SHUTDOWN_TIME : IDC_SHUTDOWN_MINUTES));
        SendDlgItemMessageW(window, IDC_SHUTDOWN_MINUTES, EM_SETSEL, 0, -1);
        return FALSE;
    }
    case WM_TIMER:
        RefreshShutdownDialog(window);
        return TRUE;
    case WM_NOTIFY:
        if (reinterpret_cast<const NMHDR*>(lParam)->code == DTN_DATETIMECHANGE) {
            RefreshShutdownDialog(window);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if ((LOWORD(wParam) == IDC_SHUTDOWN_DURATION_MODE || LOWORD(wParam) == IDC_SHUTDOWN_TIME_MODE) && HIWORD(wParam) == BN_CLICKED) {
            ShowShutdownMode(window);
            RefreshShutdownDialog(window);
            SetFocus(GetDlgItem(window, ShutdownUsesDailySchedule(window) ? IDC_SHUTDOWN_TIME : IDC_SHUTDOWN_MINUTES));
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_DAILY_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            const LRESULT selected = SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR) {
                const LRESULT value = SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_GETITEMDATA,
                    static_cast<WPARAM>(selected), 0);
                if (value >= 0 && value < 24 * 60) {
                    SYSTEMTIME time{};
                    GetLocalTime(&time);
                    time.wHour = static_cast<WORD>(value / 60);
                    time.wMinute = static_cast<WORD>(value % 60);
                    time.wSecond = 0;
                    time.wMilliseconds = 0;
                    SendDlgItemMessageW(window, IDC_SHUTDOWN_TIME, DTM_SETSYSTEMTIME, GDT_VALID,
                        reinterpret_cast<LPARAM>(&time));
                }
            }
            RefreshShutdownDialog(window);
            EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_EDIT), selected != LB_ERR);
            EnableWindow(GetDlgItem(window, IDC_SHUTDOWN_DAILY_REMOVE), selected != LB_ERR);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_DAILY_ADD) {
            WORD value = 0;
            std::vector<WORD> values = ReadDailyShutdownList(window);
            if (!ReadShutdownDailyTime(window, value)) return TRUE;
            const auto existing = std::find(values.begin(), values.end(), value);
            if (existing != values.end()) {
                SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_SETCURSEL,
                    static_cast<WPARAM>(std::distance(values.begin(), existing)), 0);
                RefreshShutdownDialog(window);
                SetDlgItemTextW(window, IDC_SHUTDOWN_STATUS,
                    T(L"That daily plan already exists.", L"该每日关机计划已经存在。"));
                return TRUE;
            }
            values.push_back(value);
            if (!liberty::NormalizeDailyShutdownTimes(values) || values.size() > liberty::kMaxDailyShutdownTimes) {
                MessageBoxW(window, T(L"You can save up to 24 different daily times.", L"每天最多可以保存 24 个不同的关机时间。"),
                    kAppName, MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            ApplyDailyShutdownPlans(window, values, true, value);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_DAILY_EDIT) {
            const LRESULT selected = SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_GETCURSEL, 0, 0);
            WORD replacement = 0;
            std::vector<WORD> values = ReadDailyShutdownList(window);
            if (selected == LB_ERR || static_cast<size_t>(selected) >= values.size() ||
                !ReadShutdownDailyTime(window, replacement)) return TRUE;
            values[static_cast<size_t>(selected)] = replacement;
            liberty::NormalizeDailyShutdownTimes(values);
            ApplyDailyShutdownPlans(window, values, g_dailyShutdownEnabled, replacement);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_DAILY_REMOVE) {
            const LRESULT selected = SendDlgItemMessageW(window, IDC_SHUTDOWN_DAILY_LIST, LB_GETCURSEL, 0, 0);
            std::vector<WORD> values = ReadDailyShutdownList(window);
            if (selected != LB_ERR && static_cast<size_t>(selected) < values.size()) {
                values.erase(values.begin() + selected);
                ApplyDailyShutdownPlans(window, values, g_dailyShutdownEnabled && !values.empty());
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_DAILY_REFRESH) {
            ReloadDailyShutdownPlans(window);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_PRESET && HIWORD(wParam) == CBN_SELCHANGE) {
            const int index = static_cast<int>(SendDlgItemMessageW(window, IDC_SHUTDOWN_PRESET, CB_GETCURSEL, 0, 0));
            const wchar_t* values[] = {L"15", L"30", L"60", L"120"};
            if (index >= 0 && index < 4) SetDlgItemTextW(window, IDC_SHUTDOWN_MINUTES, values[index]);
            SetFocus(GetDlgItem(window, IDC_SHUTDOWN_MINUTES));
            SendDlgItemMessageW(window, IDC_SHUTDOWN_MINUTES, EM_SETSEL, 0, -1);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_MINUTES && HIWORD(wParam) == EN_CHANGE) {
            wchar_t input[32]{};
            GetDlgItemTextW(window, IDC_SHUTDOWN_MINUTES, input, ARRAYSIZE(input));
            DWORD minutes = 0;
            liberty::ParseShutdownMinutes(input, minutes);
            const int index = minutes == 15 ? 0 : minutes == 30 ? 1 : minutes == 60 ? 2 : minutes == 120 ? 3 : 4;
            SendDlgItemMessageW(window, IDC_SHUTDOWN_PRESET, CB_SETCURSEL, index, 0);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_START || LOWORD(wParam) == IDOK) {
            if (g_shutdown.Active()) return TRUE;
            const bool daily = ShutdownUsesDailySchedule(window);
            if (daily) {
                if (LOWORD(wParam) == IDOK && GetFocus() != GetDlgItem(window, IDC_SHUTDOWN_START)) {
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDC_SHUTDOWN_DAILY_ADD, BN_CLICKED), 0);
                    return TRUE;
                }
                std::vector<WORD> values = ReadDailyShutdownList(window);
                if (values.empty()) {
                    MessageBoxW(window, T(L"Add at least one daily time before enabling the plan.",
                        L"请先添加至少一个每日关机时间。"), kAppName, MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                if (ApplyDailyShutdownPlans(window, values, true)) SetFocus(GetDlgItem(window, IDC_SHUTDOWN_CANCEL));
                return TRUE;
            }
            wchar_t text[32]{};
            GetDlgItemTextW(window, IDC_SHUTDOWN_MINUTES, text, ARRAYSIZE(text));
            DWORD minutes = 0;
            if (!liberty::ParseShutdownMinutes(text, minutes)) {
                MessageBoxW(window, T(L"Enter a whole number from 1 to 10080 minutes.", L"请输入 1–10080 之间的整数分钟数。"), kAppName, MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(window, IDC_SHUTDOWN_MINUTES));
                SendDlgItemMessageW(window, IDC_SHUTDOWN_MINUTES, EM_SETSEL, 0, -1);
                return TRUE;
            }
            DWORD error = g_shutdown.Open(kRegistryKey);
            if (!error) error = g_shutdown.Start(minutes, T(L"Liberty by Bada: scheduled shutdown. Save your work.", L"Liberty by Bada：定时关机，请保存工作。"));
            if (error) ShutdownError(window, error);
            else SaveDword(L"ShutdownDailyMode", 0);
            RefreshShutdownDialog(window);
            if (!error) SetFocus(GetDlgItem(window, IDC_SHUTDOWN_CANCEL));
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_SHUTDOWN_CANCEL) {
            if (g_shutdown.Active()) {
                const DWORD error = g_shutdown.Cancel();
                if (error) ShutdownError(window, error);
                RefreshShutdownDialog(window);
                if (!error) SetDlgItemTextW(window, IDC_SHUTDOWN_STATUS, T(L"Shutdown cancelled.", L"已取消定时关机。"));
            } else if (ShutdownUsesDailySchedule(window) && g_dailyTaskStatus.exists) {
                std::vector<WORD> values = ReadDailyShutdownList(window);
                if (!values.empty()) liberty::NormalizeDailyShutdownTimes(values);
                if (ApplyDailyShutdownPlans(window, values, false)) {
                    SetDlgItemTextW(window, IDC_SHUTDOWN_STATUS, T(L"Daily shutdown schedule disabled. Saved times were kept.",
                        L"每日关机计划已停用；时间列表已保留。"));
                }
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            ShowMenu(g_window);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return TRUE;
    case WM_DESTROY:
        KillTimer(window, 1);
        g_shutdownWindow = nullptr;
        return TRUE;
    }
    return FALSE;
}

bool IsToggle(UINT id) {
    return id == ID_MAC_MAPPING || id == ID_SLEEP_WITH_DISPLAY ||
           id == ID_PREVENT_SLEEP || id == ID_SAVE_SCREENSHOTS || id == ID_PREVENT_LOCK;
}

void ApplyToggle(UINT id) {
    switch (id) {
    case ID_MAC_MAPPING:
        SetMacMapping(!g_macMapping);
        break;
    case ID_SLEEP_WITH_DISPLAY:
        g_displayOffAwake = !g_displayOffAwake;
        SaveDword(L"DisplayOffAwake", g_displayOffAwake ? 1 : 0);
        ApplyPreventSleep();
        if (g_displayOffAwake) TurnOffDisplay();
        break;
    case ID_PREVENT_SLEEP:
        g_preventSleep = !g_preventSleep;
        SaveDword(L"PreventSleep", g_preventSleep ? 1 : 0);
        ApplyPreventSleep();
        break;
    case ID_SAVE_SCREENSHOTS:
        g_saveScreenshots = !g_saveScreenshots;
        SaveDword(L"SaveScreenshots", g_saveScreenshots ? 1 : 0);
        break;
    case ID_PREVENT_LOCK:
        g_preventLock = !g_preventLock;
        SaveDword(L"PreventLock", g_preventLock ? 1 : 0);
        ApplyPreventSleep();
        break;
    }
    UpdateMenuToggles();
    UpdateTrayTip();
}

void ExecuteMenuAction(UINT id) {
    if (IsToggle(id)) {
        ApplyToggle(id);
        return;
    }
    if (id == ID_SHUTDOWN) {
        DestroyWindow(g_menuWindow);
        if (!g_shutdownWindow) {
            const DWORD error = g_shutdown.Open(kRegistryKey);
            if (error) { ShutdownError(g_window, error); return; }
            g_shutdownWindow = CreateDialogParamW(g_instance, MAKEINTRESOURCEW(IDD_SHUTDOWN), nullptr, ShutdownProc, 0);
        }
        if (g_shutdownWindow) { ShowWindow(g_shutdownWindow, SW_SHOW); SetForegroundWindow(g_shutdownWindow); }
    } else if (id == ID_CLEAR_CACHE) {
        DestroyWindow(g_menuWindow);
        OpenCacheCleanup();
    } else if (id == ID_SETTINGS) {
        DestroyWindow(g_menuWindow);
        if (!g_settingsWindow) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            g_settingsWindow = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kSettingsClass, T(L"Settings", L"设置"),
                                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                                work.left + 80, work.top + 80, Scale(460), Scale(390),
                                                nullptr, nullptr, g_instance, nullptr);
            if (g_settingsWindow) ShowWindow(g_settingsWindow, SW_SHOW);
        } else SetForegroundWindow(g_settingsWindow);
    } else if (id == ID_ABOUT) {
        ShowAbout();
    }
}

RECT MenuRowRect(HWND window, int index) {
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    const int margin = MulDiv(10, dpi, 96);
    const int top = MulDiv(12, dpi, 96);
    const int rowHeight = MulDiv(54, dpi, 96);
    RECT client{};
    GetClientRect(window, &client);
    return {margin, top + index * rowHeight, client.right - margin, top + (index + 1) * rowHeight};
}

RECT MenuFooterRect(HWND window) {
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    const int margin = MulDiv(10, dpi, 96);
    RECT client{};
    GetClientRect(window, &client);
    const int height = MulDiv(56, dpi, 96);
    return {margin, client.bottom - height - margin, client.right - margin, client.bottom - margin};
}

void ClearMenuControls(HWND window) {
    HWND child = GetWindow(window, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }
    if (g_menuFont) { DeleteObject(g_menuFont); g_menuFont = nullptr; }
}

void BuildMenuControls(HWND window) {
    ClearMenuControls(window);
    g_menuFont = CreateUiFont(window, 15, FW_NORMAL);
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    LoadDailyShutdownSettings();
    for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
        const MenuItem& item = kMenuItems[index];
        RECT row = MenuRowRect(window, static_cast<int>(index));
        const DWORD buttonStyle = item.toggle ? BS_AUTOCHECKBOX | BS_RIGHTBUTTON | BS_MULTILINE : BS_OWNERDRAW;
        std::wstring caption = T(item.english, item.chinese);
        if (item.id == ID_SHUTDOWN && !g_dailyShutdownTimes.empty()) {
            caption += g_dailyShutdownEnabled ? T(L" · active ", L" · 已启用 ") : T(L" · disabled ", L" · 已停用 ");
            caption += std::to_wstring(g_dailyShutdownTimes.size());
        }
        HWND checkbox = CreateWindowExW(0, L"BUTTON", caption.c_str(),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | buttonStyle,
                                        row.left + MulDiv(12, dpi, 96), row.top + MulDiv(3, dpi, 96),
                                        row.right - row.left - MulDiv(24, dpi, 96), row.bottom - row.top - MulDiv(6, dpi, 96),
                                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(item.id)),
                                        g_instance, nullptr);
        if (checkbox) {
            SetWindowTheme(checkbox, L"Explorer", nullptr);
            SendMessageW(checkbox, WM_SETFONT, reinterpret_cast<WPARAM>(g_menuFont), TRUE);
        }
    }
    UpdateMenuToggles();
}

void ShowMenu(HWND owner);

LRESULT CALLBACK MenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCREATE:
        // WM_CREATE runs inside CreateWindowEx, before its caller receives the HWND.
        g_menuWindow = window;
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_CREATE:
        g_menuAutoCloseArmed = false;
        SetRoundedWindow(window);
        BuildMenuControls(window);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(249, 250, 252));
        SetDCBrushColor(reinterpret_cast<HDC>(wParam), RGB(249, 250, 252));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!draw || draw->CtlType != ODT_BUTTON) break;
        RECT rect = draw->rcItem;
        SetDCBrushColor(draw->hDC, (draw->itemState & ODS_SELECTED) ? RGB(232, 239, 250) : RGB(249, 250, 252));
        FillRect(draw->hDC, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, RGB(32, 33, 38));
        HGDIOBJ previous = SelectObject(draw->hDC, g_menuFont);
        wchar_t label[160]{};
        GetWindowTextW(draw->hwndItem, label, ARRAYSIZE(label));
        RECT textRect = rect;
        textRect.right -= Scale(32, window);
        DrawTextW(draw->hDC, label, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT arrow = rect; arrow.left = arrow.right - Scale(20, window);
        DrawTextW(draw->hDC, L"›", -1, &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(draw->hDC, previous);
        if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(draw->hDC, &rect);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC buffer = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
        HBRUSH background = CreateSolidBrush(RGB(249, 250, 252));
        FillRect(buffer, &client, background);
        DeleteObject(background);
        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, RGB(32, 33, 38));

        HFONT font = CreateUiFont(window, 16, FW_NORMAL);
        HGDIOBJ oldFont = SelectObject(buffer, font);
        for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
            const MenuItem& item = kMenuItems[index];
            RECT row = MenuRowRect(window, static_cast<int>(index));
            if (!item.toggle && g_menuHover == static_cast<int>(index)) {
                HBRUSH hover = CreateSolidBrush(RGB(239, 244, 255));
                FillRect(buffer, &row, hover);
                DeleteObject(hover);
            }
            if (!item.toggle) {
                RECT text{row.left + Scale(14, window), row.top, row.right - Scale(42, window), row.bottom};
                DrawTextW(buffer, T(item.english, item.chinese), -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                RECT arrow{row.right - Scale(34, window), row.top, row.right - Scale(10, window), row.bottom};
                SetTextColor(buffer, RGB(96, 98, 106));
                DrawTextW(buffer, L"›", -1, &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                SetTextColor(buffer, RGB(32, 33, 38));
            }
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(225, 226, 230));
            HGDIOBJ oldPen = SelectObject(buffer, pen);
            MoveToEx(buffer, row.left, row.bottom - 1, nullptr);
            LineTo(buffer, row.right, row.bottom - 1);
            SelectObject(buffer, oldPen);
            DeleteObject(pen);
        }
        SelectObject(buffer, oldFont);
        DeleteObject(font);

        RECT footer = MenuFooterRect(window);
        HPEN footerPen = CreatePen(PS_SOLID, 1, RGB(216, 218, 224));
        HGDIOBJ oldPen = SelectObject(buffer, footerPen);
        MoveToEx(buffer, footer.left, footer.top, nullptr);
        LineTo(buffer, footer.right, footer.top);
        SelectObject(buffer, oldPen);
        DeleteObject(footerPen);
        SetTextColor(buffer, RGB(92, 94, 102));
        HFONT footerFont = CreateUiFont(window, 13, FW_SEMIBOLD);
        oldFont = SelectObject(buffer, footerFont);
        RECT info{footer.left + Scale(12, window), footer.top, footer.left + Scale(34, window), footer.bottom};
        HPEN infoPen = CreatePen(PS_SOLID, 1, RGB(92, 94, 102));
        HGDIOBJ oldInfoPen = SelectObject(buffer, infoPen);
        HGDIOBJ oldInfoBrush = SelectObject(buffer, GetStockObject(NULL_BRUSH));
        const int infoSize = Scale(18, window);
        const int infoX = (info.left + info.right - infoSize) / 2;
        const int infoY = (info.top + info.bottom - infoSize) / 2;
        Ellipse(buffer, infoX, infoY, infoX + infoSize, infoY + infoSize);
        SelectObject(buffer, oldInfoBrush);
        SelectObject(buffer, oldInfoPen);
        DeleteObject(infoPen);
        RECT infoLetter{infoX, infoY - Scale(1, window), infoX + infoSize, infoY + infoSize};
        DrawTextW(buffer, L"i", -1, &infoLetter, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT logo{footer.left + Scale(44, window), footer.top + Scale(11, window), footer.left + Scale(74, window), footer.top + Scale(41, window)};
        DrawTrinity(buffer, logo);
        RECT brand{footer.left + Scale(82, window), footer.top, footer.right - Scale(12, window), footer.bottom};
        DrawTextW(buffer, kAppName, -1, &brand, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(buffer, oldFont);
        DeleteObject(footerFont);

        BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        const int y = GET_Y_LPARAM(lParam);
        int hover = -1;
        for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
            RECT row = MenuRowRect(window, static_cast<int>(index));
            if (y >= row.top && y < row.bottom) { hover = static_cast<int>(index); break; }
        }
        if (hover != g_menuHover) { g_menuHover = hover; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_menuHover = -1;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(window); return 0; }
        if (HIWORD(wParam) == BN_CLICKED && IsToggle(LOWORD(wParam))) {
            if (reinterpret_cast<HWND>(lParam) != GetDlgItem(window, LOWORD(wParam))) return 0;
            ApplyToggle(LOWORD(wParam));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == ID_SHUTDOWN || LOWORD(wParam) == ID_CLEAR_CACHE || LOWORD(wParam) == ID_SETTINGS) &&
            reinterpret_cast<HWND>(lParam) == GetDlgItem(window, LOWORD(wParam))) {
            ExecuteMenuAction(LOWORD(wParam));
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT footer = MenuFooterRect(window);
        if (PtInRect(&footer, point)) { DestroyWindow(window); ShowAbout(); return 0; }
        for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
            RECT row = MenuRowRect(window, static_cast<int>(index));
            if (!PtInRect(&row, point)) continue;
            const MenuItem& item = kMenuItems[index];
            if (item.toggle) return 0;
            ExecuteMenuAction(item.id);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        BuildMenuControls(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) g_menuAutoCloseArmed = true;
        else if (g_menuAutoCloseArmed) PostMessageW(window, kCloseMenuMessage, 0, 0);
        return 0;
    case kCloseMenuMessage:
        if (g_menuAutoCloseArmed && GetForegroundWindow() != window) DestroyWindow(window);
        return 0;
    case WM_CANCELMODE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        ClearMenuControls(window);
        if (GetCapture() == window) ReleaseCapture();
        if (g_menuWindow == window) {
            g_menuWindow = nullptr;
            g_menuAutoCloseArmed = false;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void PopulateModifierCombo(HWND window, UINT controlId, WORD selectedKey) {
    HWND combo = GetDlgItem(window, controlId);
    for (const ModifierChoice& choice : kModifierChoices)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(choice.english, choice.chinese)));
    SendMessageW(combo, CB_SETCURSEL, ModifierChoiceIndex(selectedKey), 0);
}

bool ApplySettings(HWND window) {
    const int commandIndex = static_cast<int>(SendDlgItemMessageW(window, ID_COMMAND_COMBO, CB_GETCURSEL, 0, 0));
    const int altIndex = static_cast<int>(SendDlgItemMessageW(window, ID_ALT_COMBO, CB_GETCURSEL, 0, 0));
    const int controlIndex = static_cast<int>(SendDlgItemMessageW(window, ID_CONTROL_COMBO, CB_GETCURSEL, 0, 0));
    if (commandIndex < 0 || altIndex < 0 || controlIndex < 0) return false;
    const WORD commandKey = kModifierChoices[commandIndex].virtualKey;
    const WORD altKey = kModifierChoices[altIndex].virtualKey;
    const WORD controlKey = kModifierChoices[controlIndex].virtualKey;
    if (commandKey == altKey || commandKey == controlKey || altKey == controlKey) {
        MessageBoxW(window, T(L"Cmd, Alt, and Control must use different physical keys.",
                               L"Cmd、Alt 和 Control 必须使用不同的物理按键。"),
                    kAppName, MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool startAtLogin = Button_GetCheck(GetDlgItem(window, ID_STARTUP)) == BST_CHECKED;
    if (!SetStartAtLogin(startAtLogin)) {
        MessageBoxW(window, T(L"Could not update Windows startup.", L"无法更新开机启动设置。"),
                    kAppName, MB_OK | MB_ICONERROR);
        return false;
    }
    ReleaseMappedState();
    g_commandKey = commandKey;
    g_altKey = altKey;
    g_controlKey = controlKey;
    SaveDword(L"CommandKey", g_commandKey);
    SaveDword(L"AltKey", g_altKey);
    SaveDword(L"ControlKey", g_controlKey);
    g_chinese = Button_GetCheck(GetDlgItem(window, ID_LANGUAGE)) == BST_CHECKED;
    SaveDword(L"Language", g_chinese ? 1 : 0);
    Notify(T(L"Settings saved.", L"设置已保存。"));
    return true;
}

LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetRoundedWindow(window);
        g_settingsFont = CreateUiFont(window, 14);
        HWND startup = CreateWindowExW(0, L"BUTTON", T(L"Start with Windows", L"开机启动"),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_RIGHTBUTTON,
                                       Scale(22, window), Scale(18, window), Scale(386, window), Scale(34, window),
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STARTUP)), g_instance, nullptr);
        CreateWindowExW(0, L"STATIC", T(L"Shortcut mapping", L"快捷键映射"),
                        WS_CHILD | WS_VISIBLE, Scale(22, window), Scale(66, window), Scale(386, window), Scale(26, window),
                        window, nullptr, g_instance, nullptr);
        const wchar_t* labels[] = {L"Cmd", L"Alt", L"Control"};
        const UINT comboIds[] = {ID_COMMAND_COMBO, ID_ALT_COMBO, ID_CONTROL_COMBO};
        for (int index = 0; index < 3; ++index) {
            CreateWindowExW(0, L"STATIC", labels[index], WS_CHILD | WS_VISIBLE,
                            Scale(32, window), Scale(102 + index * 48, window), Scale(90, window), Scale(28, window),
                            window, nullptr, g_instance, nullptr);
            CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                            Scale(132, window), Scale(98 + index * 48, window), Scale(276, window), Scale(220, window),
                            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(comboIds[index])), g_instance, nullptr);
        }
        HWND chinese = CreateWindowExW(0, L"BUTTON", T(L"Use Simplified Chinese", L"使用简体中文"),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       Scale(22, window), Scale(252, window), Scale(386, window), Scale(32, window),
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANGUAGE)), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Save", L"保存"),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        Scale(222, window), Scale(300, window), Scale(92, window), Scale(34, window),
                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_SAVE)), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Close", L"关闭"),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        Scale(322, window), Scale(300, window), Scale(92, window), Scale(34, window),
                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_CLOSE)), g_instance, nullptr);
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SetWindowTheme(child, L"Explorer", nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_settingsFont), TRUE);
        }
        Button_SetCheck(startup, g_startAtLogin ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(chinese, g_chinese ? BST_CHECKED : BST_UNCHECKED);
        PopulateModifierCombo(window, ID_COMMAND_COMBO, g_commandKey);
        PopulateModifierCombo(window, ID_ALT_COMBO, g_altKey);
        PopulateModifierCombo(window, ID_CONTROL_COMBO, g_controlKey);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_SETTINGS_SAVE || LOWORD(wParam) == IDOK) {
            if (ApplySettings(window)) { DestroyWindow(window); ShowMenu(g_window); }
            return 0;
        }
        if (LOWORD(wParam) == ID_SETTINGS_CLOSE || LOWORD(wParam) == IDCANCEL) { DestroyWindow(window); ShowMenu(g_window); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        if (g_settingsFont) { DeleteObject(g_settingsFont); g_settingsFont = nullptr; }
        if (g_settingsWindow == window) g_settingsWindow = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowMenu(HWND owner) {
    if (g_menuWindow) { SetForegroundWindow(g_menuWindow); return; }
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const int width = Scale(470);
    const int height = Scale(12 + 54 * static_cast<int>(ARRAYSIZE(kMenuItems)) + 72);
    int x = cursor.x - width + Scale(16);
    int y = cursor.y - height;
    if (x < info.rcWork.left + Scale(8)) x = info.rcWork.left + Scale(8);
    if (x + width > info.rcWork.right - Scale(8)) x = info.rcWork.right - width - Scale(8);
    if (y < info.rcWork.top + Scale(8)) y = info.rcWork.top + Scale(8);
    g_menuHover = -1;
    const DWORD extendedStyle = g_showWindow ? WS_EX_APPWINDOW : WS_EX_TOPMOST;
    const DWORD windowStyle = WS_POPUP | WS_BORDER | WS_CLIPCHILDREN;
    HWND menuOwner = g_showWindow ? nullptr : owner;
    g_menuWindow = CreateWindowExW(extendedStyle, kMenuClass, kAppName,
                                   windowStyle, x, y, width, height, menuOwner, nullptr, g_instance, nullptr);
    if (!g_menuWindow) return;
    // Use the DPI of the monitor hosting the new popup, not the primary display.
    const int actualWidth = Scale(470, g_menuWindow);
    const int actualHeight = Scale(12 + 54 * static_cast<int>(ARRAYSIZE(kMenuItems)) + 72, g_menuWindow);
    x = (std::max)(info.rcWork.left, (std::min)(cursor.x - actualWidth + Scale(16, g_menuWindow), info.rcWork.right - actualWidth));
    y = (std::max)(info.rcWork.top, (std::min)(cursor.y - actualHeight, info.rcWork.bottom - actualHeight));
    SetWindowPos(g_menuWindow, nullptr, x, y, actualWidth, actualHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    BuildMenuControls(g_menuWindow);
    ShowWindow(g_menuWindow, SW_SHOWNORMAL);
    SetForegroundWindow(g_menuWindow);
    SetFocus(g_menuWindow);
    UpdateWindow(g_menuWindow);
    if (GetForegroundWindow() == g_menuWindow) g_menuAutoCloseArmed = true;
}

void StartDailyShutdownCountdown() {
    DWORD error = g_shutdown.Open(kRegistryKey);
    if (!error && !g_shutdown.Active())
        error = g_shutdown.Start(1, T(L"Liberty by Bada: daily shutdown. Save your work.",
            L"Liberty by Bada：每日定时关机，请保存工作。"));
    if (g_shutdownWindow) {
        RefreshShutdownDialog(g_shutdownWindow);
        if (error && error != ERROR_SHUTDOWN_IN_PROGRESS && error != ERROR_SHUTDOWN_IS_SCHEDULED)
            ShutdownError(g_shutdownWindow, error);
    }
}

bool RegisterClasses() {
    WNDCLASSEXW tray{sizeof(tray)};
    tray.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (g_taskbarCreated && message == g_taskbarCreated) {
            Shell_NotifyIconW(NIM_ADD, &g_tray);
            Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
            return 0;
        }
        switch (message) {
        case kShowMenuMessage:
            ShowMenu(window);
            return 0;
        case kDailyShutdownMessage:
            StartDailyShutdownCountdown();
            return 0;
        case WM_HOTKEY:
            if (wParam == 1) TurnOffDisplay();
            return 0;
        case WM_TIMER:
            if (wParam == 42) MaintainUnlockedSession();
            return 0;
        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONUP || event == WM_RBUTTONUP || event == WM_CONTEXTMENU) ShowMenu(window);
            return 0;
        }
        case WM_CLIPBOARDUPDATE: {
            const DWORD sequence = GetClipboardSequenceNumber();
            if (sequence != g_lastClipboardSequence) {
                g_lastClipboardSequence = sequence;
                SaveClipboardScreenshot();
            }
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 42);
            RemoveClipboardFormatListener(window);
            SetThreadExecutionState(ES_CONTINUOUS);
            UnregisterHotKey(window, 1);
            if (g_keyboardHook) { ReleaseMappedState(); UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = nullptr; }
            if (g_menuWindow) DestroyWindow(g_menuWindow);
            if (g_settingsWindow) DestroyWindow(g_settingsWindow);
            if (g_shutdownWindow) DestroyWindow(g_shutdownWindow);
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            if (g_tray.hIcon) DestroyIcon(g_tray.hIcon);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    };
    tray.hInstance = g_instance;
    tray.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    tray.hIcon = CreateTrayIcon();
    tray.hIconSm = tray.hIcon;
    tray.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&tray)) return false;

    WNDCLASSEXW menu{sizeof(menu)};
    menu.lpfnWndProc = MenuProc;
    menu.hInstance = g_instance;
    menu.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    menu.lpszClassName = kMenuClass;
    if (!RegisterClassExW(&menu)) return false;

    WNDCLASSEXW settings{sizeof(settings)};
    settings.lpfnWndProc = SettingsProc;
    settings.hInstance = g_instance;
    settings.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settings.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    settings.lpszClassName = kSettingsClass;
    return RegisterClassExW(&settings) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int index = 1; index < argumentCount; ++index) {
            if (_wcsicmp(arguments[index], L"--window") == 0 || _wcsicmp(arguments[index], L"--preview-menu") == 0) g_showWindow = true;
            if (_wcsicmp(arguments[index], liberty::kDailyShutdownArgument) == 0) g_dailyShutdownTrigger = true;
        }
        LocalFree(arguments);
    }
    g_mutex = CreateMutexW(nullptr, FALSE, L"Local\\LibertyByBada.SingleInstance");
    if (!g_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr))
            PostMessageW(existing, g_dailyShutdownTrigger ? kDailyShutdownMessage : kShowMenuMessage, 0, 0);
        CloseHandle(g_mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_DATE_CLASSES};
    InitCommonControlsEx(&controls);
    InitializeLanguage();
    g_macMapping = LoadDword(L"MacMapping", 0) != 0;
    g_displayOffAwake = LoadDword(L"DisplayOffAwake", 0) != 0;
    g_preventSleep = LoadDword(L"PreventSleep", 0) != 0;
    g_preventLock = LoadDword(L"PreventLock", 0) != 0;
    g_saveScreenshots = LoadDword(L"SaveScreenshots", 0) != 0;
    g_startAtLogin = LoadDword(L"StartAtLogin", 1) != 0;
    LoadModifierMappings();
#ifndef _DEBUG
    SetStartAtLogin(g_startAtLogin);
#endif
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterClasses()) return 1;

    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    if (g_dailyShutdownTrigger) PostMessageW(g_window, kDailyShutdownMessage, 0, 0);
    AddClipboardFormatListener(g_window);
    g_lastClipboardSequence = GetClipboardSequenceNumber();
    ApplyPreventSleep();
    SetTimer(g_window, 42, 15000, nullptr);
    if (g_macMapping && !SetMacMapping(true)) g_macMapping = false;
    RegisterHotKey(g_window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10);

    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = CreateTrayIcon();
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    UpdateTrayTip();
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    if (g_showWindow) ShowMenu(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (g_shutdownWindow && IsDialogMessageW(g_shutdownWindow, &message)) continue;
        if (g_settingsWindow && IsDialogMessageW(g_settingsWindow, &message)) continue;
        if (g_menuWindow && IsDialogMessageW(g_menuWindow, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    if (g_mutex) CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
