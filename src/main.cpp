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
#include <string>
#include <vector>

namespace {

constexpr wchar_t kAppName[] = L"Liberty";
constexpr wchar_t kWindowClass[] = L"Liberty.TrayWindow";
constexpr wchar_t kRegistryKey[] = L"Software\\Liberty";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kSecuritySystrayPolicyKey[] =
    L"SOFTWARE\\Policies\\Microsoft\\Windows Defender Security Center\\Systray";
constexpr wchar_t kSecuritySystrayPolicyValue[] = L"HideSystray";
constexpr wchar_t kSecuritySystrayBackupValue[] = L"SecuritySystrayBackupValue";
constexpr wchar_t kSecuritySystrayBackupValid[] = L"SecuritySystrayBackupValid";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kShutdownTimer = 1;
constexpr ULONG_PTR kInjectedMarker = 0x4C494245525459ULL; // "LIBERTY"

enum Command : UINT {
    ID_TOGGLE = 100,
    ID_SCREEN_OFF,
    ID_SHUTDOWN_15,
    ID_SHUTDOWN_30,
    ID_SHUTDOWN_60,
    ID_SHUTDOWN_120,
    ID_SHUTDOWN_CUSTOM,
    ID_SHUTDOWN_CANCEL,
    ID_STARTUP,
    ID_MAPPINGS,
    ID_BLOCK_ONEDRIVE,
    ID_HIDE_NVIDIA,
    ID_HIDE_AMD,
    ID_HIDE_SECURITY,
    ID_ABOUT,
    ID_EXIT
};

enum MappingControl : WORD {
    ID_MAPPING_HINT = 1100,
    ID_MAPPING_COMMAND = 1101,
    ID_MAPPING_OPTION = 1102,
    ID_MAPPING_CONTROL = 1103
};

struct ModifierChoice {
    WORD virtualKey;
    const wchar_t* label;
};

struct StartupEntry {
    const wchar_t* runValue;
    const wchar_t* backupValue;
    const wchar_t* backupType;
    const wchar_t* backupValid;
};

constexpr ModifierChoice kModifierChoices[] = {
    {VK_LWIN, L"Left Windows"},
    {VK_RWIN, L"Right Windows"},
    {VK_LMENU, L"Left Alt"},
    {VK_RMENU, L"Right Alt"},
    {VK_LCONTROL, L"Left Control"},
    {VK_RCONTROL, L"Right Control"},
    {VK_CAPITAL, L"Caps Lock"}
};

const StartupEntry kOneDriveEntry{
    L"OneDrive", L"OneDriveRunBackup", L"OneDriveRunBackupType", L"OneDriveRunBackupValid"};

const StartupEntry kNvidiaEntries[] = {
    {L"NvBackend", L"NvBackendRunBackup", L"NvBackendRunBackupType", L"NvBackendRunBackupValid"},
    {L"NvCplDaemon", L"NvCplDaemonRunBackup", L"NvCplDaemonRunBackupType", L"NvCplDaemonRunBackupValid"},
    {L"NVIDIA App", L"NvidiaAppRunBackup", L"NvidiaAppRunBackupType", L"NvidiaAppRunBackupValid"},
    {L"NVIDIA GeForce Experience", L"NvidiaGfeRunBackup", L"NvidiaGfeRunBackupType", L"NvidiaGfeRunBackupValid"},
    {L"NVIDIA Share", L"NvidiaShareRunBackup", L"NvidiaShareRunBackupType", L"NvidiaShareRunBackupValid"},
    {L"NvNodeLauncher", L"NvNodeRunBackup", L"NvNodeRunBackupType", L"NvNodeRunBackupValid"}
};

const StartupEntry kAmdEntries[] = {
    {L"StartCN", L"AmdStartCNRunBackup", L"AmdStartCNRunBackupType", L"AmdStartCNRunBackupValid"},
    {L"RadeonSoftware", L"AmdRadeonRunBackup", L"AmdRadeonRunBackupType", L"AmdRadeonRunBackupValid"},
    {L"AMD Radeon Software", L"AmdSoftwareRunBackup", L"AmdSoftwareRunBackupType", L"AmdSoftwareRunBackupValid"},
    {L"AMDRSServ", L"AmdRssRunBackup", L"AmdRssRunBackupType", L"AmdRssRunBackupValid"},
    {L"AMDNoiseSuppression", L"AmdNoiseRunBackup", L"AmdNoiseRunBackupType", L"AmdNoiseRunBackupValid"},
    {L"AMD External Events Utility", L"AmdEventsRunBackup", L"AmdEventsRunBackupType", L"AmdEventsRunBackupValid"}
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HHOOK g_hook = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_tray{};
bool g_enabled = true;
bool g_switchAltDown = false;
bool g_startAtLogin = false;
bool g_blockOneDrive = false;
bool g_hideNvidiaPanel = false;
bool g_hideAmdPanel = false;
bool g_hideSecurityCenter = false;
WORD g_commandKey = VK_LWIN;
WORD g_optionKey = VK_LMENU;
WORD g_controlKey = VK_LCONTROL;
bool g_commandDown = false;
bool g_optionDown = false;
bool g_controlDown = false;
bool g_qDown = false;
bool g_minimizeDown = false;
UINT g_pendingShutdownSeconds = 0;
UINT g_taskbarCreated = 0;

bool IsKeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool ReadRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* name, DWORD& value) {
    HKEY key = nullptr;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    const LONG result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value);
}

bool WriteRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG result = RegSetValueExW(key, name, 0, REG_DWORD,
                                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool ReadRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* name,
                        std::wstring& value, DWORD* typeOut = nullptr) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 2, L'\0');
    result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return false;

    value.assign(buffer.data());
    if (typeOut) *typeOut = type;
    return true;
}

bool WriteRegistryString(HKEY root, const wchar_t* subKey, const wchar_t* name,
                         const std::wstring& value, DWORD type = REG_SZ) {
    if (type != REG_SZ && type != REG_EXPAND_SZ) type = REG_SZ;
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG result = RegSetValueExW(key, name, 0, type,
                                       reinterpret_cast<const BYTE*>(value.c_str()), size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool DeleteRegistryValue(HKEY root, const wchar_t* subKey, const wchar_t* name) {
    HKEY key = nullptr;
    const LONG openResult = RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND) return true;
    if (openResult != ERROR_SUCCESS) return false;
    const LONG result = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

void SaveDword(const wchar_t* name, DWORD value) {
    WriteRegistryDword(HKEY_CURRENT_USER, kRegistryKey, name, value);
}

DWORD LoadDword(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, kRegistryKey, name, value)) value = fallback;
    return value;
}

bool SaveStringSetting(const wchar_t* name, const std::wstring& value) {
    return WriteRegistryString(HKEY_CURRENT_USER, kRegistryKey, name, value);
}

bool LoadStringSetting(const wchar_t* name, std::wstring& value) {
    return ReadRegistryString(HKEY_CURRENT_USER, kRegistryKey, name, value);
}

void SendKey(WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = kInjectedMarker;
    SendInput(1, &input, sizeof(input));
}

void TapKey(WORD vk) {
    SendKey(vk, true);
    SendKey(vk, false);
}

void SendModifiedKey(WORD modifier, WORD vk, bool down) {
    SendKey(modifier, true);
    SendKey(vk, down);
    SendKey(modifier, false);
}

void ReleaseSwitcher() {
    if (g_switchAltDown) {
        SendKey(VK_MENU, false);
        g_switchAltDown = false;
    }
}

void MinimizeForeground() {
    HWND active = GetForegroundWindow();
    if (active) ShowWindow(active, SW_MINIMIZE);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data) {
    if (code < 0 || !g_enabled) return CallNextHookEx(g_hook, code, message, data);
    const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (event->dwExtraInfo == kInjectedMarker) return CallNextHookEx(g_hook, code, message, data);

    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(g_hook, code, message, data);
    const DWORD vk = event->vkCode;

    // The selected physical key acts as macOS Command and is kept out of Windows.
    if (vk == g_commandKey) {
        if (down) g_commandDown = true;
        if (up) {
            g_commandDown = false;
            ReleaseSwitcher();
        }
        return 1;
    }

    // Option and Control can be moved to another physical key. Native Alt/Ctrl keys
    // are allowed through unchanged so Windows handles their normal system behavior.
    if (vk == g_optionKey) {
        g_optionDown = down ? true : false;
        if (g_optionKey == VK_LMENU || g_optionKey == VK_RMENU)
            return CallNextHookEx(g_hook, code, message, data);
        SendKey(VK_MENU, down);
        return 1;
    }
    if (vk == g_controlKey) {
        g_controlDown = down ? true : false;
        if (g_controlKey == VK_LCONTROL || g_controlKey == VK_RCONTROL)
            return CallNextHookEx(g_hook, code, message, data);
        SendKey(VK_CONTROL, down);
        return 1;
    }

    // macOS Option navigation: word-by-word movement/deletion.
    if (!g_commandDown && g_optionDown) {
        if (vk == VK_LEFT || vk == VK_RIGHT) {
            SendKey(VK_LMENU, false);
            SendModifiedKey(VK_CONTROL, static_cast<WORD>(vk), down);
            SendKey(VK_LMENU, true);
            return 1;
        }
        if (vk == VK_BACK) {
            SendKey(VK_LMENU, false);
            SendModifiedKey(VK_CONTROL, VK_BACK, down);
            SendKey(VK_LMENU, true);
            return 1;
        }
    }

    if (!g_commandDown) return CallNextHookEx(g_hook, code, message, data);

    // Command+Tab / Command+` application switching.
    if (vk == VK_TAB || vk == VK_OEM_3) {
        if (!g_switchAltDown) {
            SendKey(VK_MENU, true);
            g_switchAltDown = true;
        }
        if (vk == VK_OEM_3) SendKey(VK_SHIFT, true);
        SendKey(VK_TAB, down);
        if (vk == VK_OEM_3) SendKey(VK_SHIFT, false);
        return 1;
    }

    // Command+Option+Escape -> Task Manager (closest Windows equivalent to Force Quit).
    if (vk == VK_ESCAPE && g_optionDown) {
        if (down) {
            SendKey(VK_LMENU, false);
            SendKey(VK_CONTROL, true); SendKey(VK_SHIFT, true); TapKey(VK_ESCAPE);
            SendKey(VK_SHIFT, false); SendKey(VK_CONTROL, false);
            SendKey(VK_LMENU, true);
        }
        return 1;
    }

    // Command+Q quits the active app; Command+H/M minimizes it.
    if (vk == 'Q') {
        if (down && !g_qDown) {
            g_qDown = true;
            SendKey(VK_MENU, true); TapKey(VK_F4); SendKey(VK_MENU, false);
        }
        if (up) g_qDown = false;
        return 1;
    }
    if (vk == 'H' || vk == 'M') {
        if (down && !g_minimizeDown) { g_minimizeDown = true; MinimizeForeground(); }
        if (up) g_minimizeDown = false;
        return 1;
    }

    // Familiar macOS system shortcuts: Spotlight-equivalent search and screenshots.
    if (vk == VK_SPACE) {
        if (down) { SendKey(VK_LWIN, true); TapKey('S'); SendKey(VK_LWIN, false); }
        return 1;
    }
    if ((vk == '3' || vk == '4') && IsKeyDown(VK_SHIFT)) {
        if (down && vk == '3') TapKey(VK_SNAPSHOT);
        if (down && vk == '4') {
            SendKey(VK_LWIN, true); SendKey(VK_SHIFT, true); TapKey('S');
            SendKey(VK_SHIFT, false); SendKey(VK_LWIN, false);
        }
        return 1;
    }

    // Command+Left/Right = line boundary; Command+Up/Down = document boundary.
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        SendKey(vk == VK_LEFT ? VK_HOME : VK_END, down);
        return 1;
    }
    if (vk == VK_UP || vk == VK_DOWN) {
        SendModifiedKey(VK_CONTROL, vk == VK_UP ? VK_HOME : VK_END, down);
        return 1;
    }

    // Everything else follows the central macOS rule: Command becomes Control.
    SendModifiedKey(VK_CONTROL, static_cast<WORD>(vk), down);
    return 1;
}

void UpdateTrayTip() {
    const wchar_t* state = g_enabled ? L"Liberty — macOS shortcuts enabled" : L"Liberty — shortcuts paused";
    lstrcpynW(g_tray.szTip, state, ARRAYSIZE(g_tray.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

HICON CreateTrayIcon(bool enabled) {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, color));
    RECT rect{0, 0, size, size};
    HBRUSH background = CreateSolidBrush(enabled ? RGB(42, 93, 246) : RGB(110, 110, 110));
    FillRect(memory, &rect, background);
    DeleteObject(background);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));
    HFONT font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memory, font));
    DrawTextW(memory, L"L", 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memory, oldFont);
    SelectObject(memory, old);
    DeleteObject(font);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    BYTE maskBits[(size * size) / 8]{};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits);
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

void RefreshTrayIcon() {
    HICON next = CreateTrayIcon(g_enabled);
    HICON previous = g_tray.hIcon;
    g_tray.hIcon = next;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    if (previous) DestroyIcon(previous);
    UpdateTrayTip();
}

void ScreenOff() {
    PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
}

bool RunShutdownCommand(const wchar_t* arguments) {
    wchar_t systemDirectory[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory))) return false;
    std::wstring executable = std::wstring(systemDirectory) + L"\\shutdown.exe";
    std::wstring commandLine = L"\"" + executable + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    WaitForSingleObject(process.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

void ScheduleShutdown(UINT seconds) {
    // Replace an existing countdown instead of silently failing.
    RunShutdownCommand(L"/a");
    if (!RunShutdownCommand((L"/s /t " + std::to_wstring(seconds)).c_str())) {
        MessageBoxW(g_window, L"Windows did not accept the shutdown schedule.", L"Liberty", MB_OK | MB_ICONERROR);
        return;
    }
    g_pendingShutdownSeconds = seconds;
    SaveDword(L"PendingShutdown", seconds);
    std::wstring message = L"Windows will shut down in " + std::to_wstring(seconds / 60) + L" minutes.";
    g_tray.uFlags = NIF_INFO;
    lstrcpynW(g_tray.szInfoTitle, L"Liberty", ARRAYSIZE(g_tray.szInfoTitle));
    lstrcpynW(g_tray.szInfo, message.c_str(), ARRAYSIZE(g_tray.szInfo));
    g_tray.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void CancelShutdown() {
    if (!RunShutdownCommand(L"/a")) {
        MessageBoxW(g_window, L"There is no Windows shutdown countdown to cancel.", L"Liberty", MB_OK | MB_ICONINFORMATION);
    }
    g_pendingShutdownSeconds = 0;
    SaveDword(L"PendingShutdown", 0);
}

bool IsStartupEnabled() {
    HKEY key = nullptr;
    wchar_t value[MAX_PATH * 2]{};
    DWORD size = sizeof(value), type = REG_SZ;
    bool enabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        enabled = RegQueryValueExW(key, kAppName, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS &&
                  (type == REG_SZ || type == REG_EXPAND_SZ);
        RegCloseKey(key);
    }
    return enabled;
}

bool SetStartup(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        MessageBoxW(g_window, L"Liberty could not update Windows startup settings.", L"Liberty",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (length == 0 || length >= ARRAYSIZE(path)) {
            RegCloseKey(key);
            MessageBoxW(g_window, L"Liberty could not determine its executable path.", L"Liberty",
                        MB_OK | MB_ICONERROR);
            return false;
        }
        std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
        result = RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted.c_str()),
                                static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kAppName);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        MessageBoxW(g_window, L"Liberty could not update Windows startup settings.", L"Liberty",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    g_startAtLogin = enabled;
    return true;
}

bool IsModifierChoice(WORD virtualKey) {
    for (const ModifierChoice& choice : kModifierChoices) {
        if (choice.virtualKey == virtualKey) return true;
    }
    return false;
}

WORD FirstAvailableModifier(WORD firstUsed, WORD secondUsed) {
    for (const ModifierChoice& choice : kModifierChoices) {
        if (choice.virtualKey != firstUsed && choice.virtualKey != secondUsed) return choice.virtualKey;
    }
    return VK_LCONTROL;
}

void LoadModifierMappings() {
    const WORD savedCommand = static_cast<WORD>(LoadDword(L"CommandKey", VK_LWIN));
    const WORD savedOption = static_cast<WORD>(LoadDword(L"OptionKey", VK_LMENU));
    const WORD savedControl = static_cast<WORD>(LoadDword(L"ControlKey", VK_LCONTROL));

    g_commandKey = IsModifierChoice(savedCommand) ? savedCommand : VK_LWIN;
    g_optionKey = IsModifierChoice(savedOption) && savedOption != g_commandKey
                      ? savedOption
                      : FirstAvailableModifier(g_commandKey, 0);
    g_controlKey = IsModifierChoice(savedControl) && savedControl != g_commandKey && savedControl != g_optionKey
                       ? savedControl
                       : FirstAvailableModifier(g_commandKey, g_optionKey);
}

void ResetMappedModifierState() {
    if (g_optionDown && g_optionKey != VK_LMENU && g_optionKey != VK_RMENU) SendKey(VK_MENU, false);
    if (g_controlDown && g_controlKey != VK_LCONTROL && g_controlKey != VK_RCONTROL) SendKey(VK_CONTROL, false);
    g_commandDown = false;
    g_optionDown = false;
    g_controlDown = false;
    ReleaseSwitcher();
}

bool ClearRunEntryBackup(const StartupEntry& entry) {
    const bool backupRemoved = DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, entry.backupValue);
    const bool typeRemoved = DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, entry.backupType);
    SaveDword(entry.backupValid, 0);
    return backupRemoved && typeRemoved;
}

bool ApplyRunEntryBlock(const StartupEntry& entry, bool blocked) {
    const bool hasBackup = LoadDword(entry.backupValid, 0) != 0;
    if (blocked) {
        if (!hasBackup) {
            std::wstring current;
            DWORD type = REG_SZ;
            if (ReadRegistryString(HKEY_CURRENT_USER, kRunKey, entry.runValue, current, &type)) {
                if (!SaveStringSetting(entry.backupValue, current)) return false;
                SaveDword(entry.backupType, type);
                SaveDword(entry.backupValid, 1);
            } else {
                SaveDword(entry.backupValid, 0);
            }
        }
        return DeleteRegistryValue(HKEY_CURRENT_USER, kRunKey, entry.runValue);
    }

    if (!hasBackup) return true;

    // Do not overwrite a value the user recreated while the block was active.
    std::wstring current;
    if (ReadRegistryString(HKEY_CURRENT_USER, kRunKey, entry.runValue, current)) {
        return ClearRunEntryBackup(entry);
    }

    std::wstring backup;
    if (!LoadStringSetting(entry.backupValue, backup)) return false;
    const DWORD type = LoadDword(entry.backupType, REG_SZ);
    if (!WriteRegistryString(HKEY_CURRENT_USER, kRunKey, entry.runValue, backup, type)) return false;
    return ClearRunEntryBackup(entry);
}

bool ApplyRunEntryBlocks(const StartupEntry* entries, size_t count, bool blocked) {
    bool success = true;
    for (size_t index = 0; index < count; ++index) {
        if (!ApplyRunEntryBlock(entries[index], blocked)) success = false;
    }
    return success;
}

bool IsSecurityCenterHidden() {
    DWORD value = 0;
    return ReadRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey,
                             kSecuritySystrayPolicyValue, value) && value != 0;
}

bool SetSecurityCenterHiddenDirect(bool hidden) {
    const bool hasBackup = LoadDword(kSecuritySystrayBackupValid, 0) != 0;
    if (hidden) {
        if (!hasBackup) {
            DWORD current = 0;
            if (ReadRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey,
                                  kSecuritySystrayPolicyValue, current)) {
                SaveDword(kSecuritySystrayBackupValue, current);
                SaveDword(kSecuritySystrayBackupValid, 1);
            } else {
                SaveDword(kSecuritySystrayBackupValid, 0);
            }
        }
        return WriteRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey,
                                  kSecuritySystrayPolicyValue, 1);
    }

    bool success = false;
    if (hasBackup) {
        const DWORD previous = LoadDword(kSecuritySystrayBackupValue, 0);
        success = WriteRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey,
                                     kSecuritySystrayPolicyValue, previous);
    } else {
        success = DeleteRegistryValue(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey,
                                      kSecuritySystrayPolicyValue);
    }
    if (success) SaveDword(kSecuritySystrayBackupValid, 0);
    return success;
}

bool RunElevatedSecurityPolicy(bool hidden) {
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, ARRAYSIZE(executablePath));
    if (length == 0 || length >= ARRAYSIZE(executablePath)) return false;

    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = executablePath;
    execute.lpParameters = hidden ? L"--apply-security-systray 1" : L"--apply-security-systray 0";
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) return false;

    WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(execute.hProcess, &exitCode);
    CloseHandle(execute.hProcess);
    return exitCode == 0;
}

bool SetSecurityCenterHidden(bool hidden) {
    if (!SetSecurityCenterHiddenDirect(hidden)) {
        if (!RunElevatedSecurityPolicy(hidden)) return false;
    }

    const wchar_t settingName[] = L"Windows Defender Security Center";
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(settingName), SMTO_ABORTIFHUNG, 1000, nullptr);
    return IsSecurityCenterHidden() == hidden;
}

INT_PTR CALLBACK MinutesDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, 1001, L"45");
        SendDlgItemMessageW(dialog, 1001, EM_SETSEL, 0, -1);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == IDOK) {
            BOOL valid = FALSE;
            UINT minutes = GetDlgItemInt(dialog, 1001, &valid, FALSE);
            if (!valid || minutes < 1 || minutes > 10080) {
                MessageBoxW(dialog, L"Enter 1–10080 minutes.", L"Liberty", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(dialog, static_cast<INT_PTR>(minutes));
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, 0); return TRUE; }
    }
    return FALSE;
}

class DialogTemplateBuilder {
public:
    void Begin(DWORD style, DWORD extendedStyle, WORD controlCount,
               short x, short y, short width, short height,
               const wchar_t* title) {
        DLGTEMPLATE dialog{};
        dialog.style = style;
        dialog.dwExtendedStyle = extendedStyle;
        dialog.cdit = controlCount;
        dialog.x = x;
        dialog.y = y;
        dialog.cx = width;
        dialog.cy = height;
        Append(dialog);
        AppendWord(0); // no menu
        AppendWord(0); // default dialog class
        AppendString(title);
        AppendWord(9);
        AppendString(L"Segoe UI");
    }

    void AddItem(DWORD style, DWORD extendedStyle, short x, short y,
                 short width, short height, WORD id, WORD classAtom,
                 const wchar_t* title) {
        AlignDword();
        DLGITEMTEMPLATE item{};
        item.style = style;
        item.dwExtendedStyle = extendedStyle;
        item.x = x;
        item.y = y;
        item.cx = width;
        item.cy = height;
        item.id = id;
        Append(item);
        AppendWord(0xFFFF);
        AppendWord(classAtom);
        AppendString(title);
        AppendWord(0); // no creation data
    }

    const DLGTEMPLATE* Data() const {
        return reinterpret_cast<const DLGTEMPLATE*>(bytes.data());
    }

private:
    template <typename T>
    void Append(const T& value) {
        const BYTE* begin = reinterpret_cast<const BYTE*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(T));
    }

    void AppendWord(WORD value) { Append(value); }

    void AppendString(const wchar_t* value) {
        if (!value) {
            AppendWord(0);
            return;
        }
        while (*value) AppendWord(static_cast<WORD>(*value++));
        AppendWord(0);
    }

    void AlignDword() { bytes.resize((bytes.size() + 3u) & ~3u, 0); }

    std::vector<BYTE> bytes;
};

int ModifierChoiceIndex(WORD virtualKey) {
    for (size_t index = 0; index < ARRAYSIZE(kModifierChoices); ++index) {
        if (kModifierChoices[index].virtualKey == virtualKey) return static_cast<int>(index);
    }
    return 0;
}

const wchar_t* ModifierLabel(WORD virtualKey) {
    for (const ModifierChoice& choice : kModifierChoices) {
        if (choice.virtualKey == virtualKey) return choice.label;
    }
    return L"Unknown";
}

INT_PTR CALLBACK MappingDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        const WORD controls[] = {ID_MAPPING_COMMAND, ID_MAPPING_OPTION, ID_MAPPING_CONTROL};
        for (WORD control : controls) {
            HWND combo = GetDlgItem(dialog, control);
            for (const ModifierChoice& choice : kModifierChoices) {
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
            }
        }
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_COMMAND), CB_SETCURSEL,
                     ModifierChoiceIndex(g_commandKey), 0);
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_OPTION), CB_SETCURSEL,
                     ModifierChoiceIndex(g_optionKey), 0);
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_CONTROL), CB_SETCURSEL,
                     ModifierChoiceIndex(g_controlKey), 0);
        return TRUE;
    }

    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        const HWND commandCombo = GetDlgItem(dialog, ID_MAPPING_COMMAND);
        const HWND optionCombo = GetDlgItem(dialog, ID_MAPPING_OPTION);
        const HWND controlCombo = GetDlgItem(dialog, ID_MAPPING_CONTROL);
        const int commandIndex = static_cast<int>(SendMessageW(commandCombo, CB_GETCURSEL, 0, 0));
        const int optionIndex = static_cast<int>(SendMessageW(optionCombo, CB_GETCURSEL, 0, 0));
        const int controlIndex = static_cast<int>(SendMessageW(controlCombo, CB_GETCURSEL, 0, 0));
        if (commandIndex < 0 || optionIndex < 0 || controlIndex < 0) return TRUE;

        const WORD commandKey = kModifierChoices[commandIndex].virtualKey;
        const WORD optionKey = kModifierChoices[optionIndex].virtualKey;
        const WORD controlKey = kModifierChoices[controlIndex].virtualKey;
        if (commandKey == optionKey || commandKey == controlKey || optionKey == controlKey) {
            MessageBoxW(dialog, L"Cmd, Option, and Control must use different physical keys.",
                        L"Liberty", MB_OK | MB_ICONWARNING);
            return TRUE;
        }

        ResetMappedModifierState();
        g_commandKey = commandKey;
        g_optionKey = optionKey;
        g_controlKey = controlKey;
        SaveDword(L"CommandKey", g_commandKey);
        SaveDword(L"OptionKey", g_optionKey);
        SaveDword(L"ControlKey", g_controlKey);
        EndDialog(dialog, IDOK);
        return TRUE;
    }

    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

INT_PTR ShowMappingDialog(HWND parent) {
    DialogTemplateBuilder builder;
    builder.Begin(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT,
                 0, 8, 10, 10, 260, 116, L"Keyboard mappings");
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 8, 244, 12, ID_MAPPING_HINT, 0x0082,
                    L"Choose the physical key used for each macOS modifier:");
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 27, 80, 12, 1201, 0x0082, L"Cmd:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    0, 96, 24, 150, 80, ID_MAPPING_COMMAND, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 49, 80, 12, 1202, 0x0082, L"Option:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    0, 96, 46, 150, 80, ID_MAPPING_OPTION, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 71, 80, 12, 1203, 0x0082, L"Control:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    0, 96, 68, 150, 80, ID_MAPPING_CONTROL, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 104, 94, 58, 16, IDOK, 0x0080, L"OK");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 174, 94, 58, 16, IDCANCEL, 0x0080, L"Cancel");
    return DialogBoxIndirectParamW(g_instance, builder.Data(), parent, MappingDialogProc, 0);
}

INT_PTR ShowMinutesDialog(HWND parent) {
    // In-memory dialog template keeps Liberty as one EXE with no resource file dependency.
    struct Template {
        DLGTEMPLATE dlg;
        WORD menu, windowClass;
        wchar_t title[17];
        WORD pointSize;
        wchar_t font[9];
        DLGITEMTEMPLATE label;
        WORD labelClass[2];
        wchar_t labelText[20];
        WORD labelData;
        DLGITEMTEMPLATE edit;
        WORD editClass[2];
        WORD editTitle;
        WORD editData;
        DLGITEMTEMPLATE ok;
        WORD buttonClass1[2];
        wchar_t okText[3];
        WORD okData;
        DLGITEMTEMPLATE cancel;
        WORD buttonClass2[2];
        wchar_t cancelText[7];
        WORD cancelData;
    } t{};
    t.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
    t.dlg.dwExtendedStyle = 0; t.dlg.cdit = 4; t.dlg.x = 10; t.dlg.y = 10; t.dlg.cx = 190; t.dlg.cy = 72;
    t.menu = 0; t.windowClass = 0; lstrcpyW(t.title, L"Timed shutdown"); t.pointSize = 9; lstrcpyW(t.font, L"Segoe UI");
    t.label.style = WS_CHILD | WS_VISIBLE; t.label.x = 8; t.label.y = 10; t.label.cx = 85; t.label.cy = 12; t.label.id = 1000;
    t.labelClass[0] = 0xFFFF; t.labelClass[1] = 0x0082; lstrcpyW(t.labelText, L"Minutes (1–10080):");
    t.edit.style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL; t.edit.x = 100; t.edit.y = 8; t.edit.cx = 78; t.edit.cy = 14; t.edit.id = 1001;
    t.editClass[0] = 0xFFFF; t.editClass[1] = 0x0081;
    t.ok.style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON; t.ok.x = 42; t.ok.y = 42; t.ok.cx = 48; t.ok.cy = 15; t.ok.id = IDOK;
    t.buttonClass1[0] = 0xFFFF; t.buttonClass1[1] = 0x0080; lstrcpyW(t.okText, L"OK");
    t.cancel.style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON; t.cancel.x = 100; t.cancel.y = 42; t.cancel.cx = 48; t.cancel.cy = 15; t.cancel.id = IDCANCEL;
    t.buttonClass2[0] = 0xFFFF; t.buttonClass2[1] = 0x0080; lstrcpyW(t.cancelText, L"Cancel");
    return DialogBoxIndirectParamW(g_instance, &t.dlg, parent, MinutesDialogProc, 0);
}

void ShowMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    HMENU shutdown = CreatePopupMenu();
    HMENU system = CreatePopupMenu();
    if (!menu || !shutdown || !system) {
        if (menu) DestroyMenu(menu);
        if (shutdown) DestroyMenu(shutdown);
        if (system) DestroyMenu(system);
        return;
    }

    const wchar_t* status = g_enabled ? L"Liberty — shortcuts enabled" : L"Liberty — shortcuts paused";
    const wchar_t* toggleLabel = g_enabled ? L"Pause macOS shortcuts" : L"Resume macOS shortcuts";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_TOGGLE, toggleLabel);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_SCREEN_OFF, L"Turn display off\tCtrl+Alt+F10");
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_15, L"15 minutes");
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_30, L"30 minutes");
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_60, L"1 hour");
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_120, L"2 hours");
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_CUSTOM, L"Custom…");
    AppendMenuW(shutdown, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(shutdown, MF_STRING, ID_SHUTDOWN_CANCEL, L"Cancel scheduled shutdown");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(shutdown), L"Timed shutdown");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_startAtLogin ? MF_CHECKED : 0), ID_STARTUP, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, ID_MAPPINGS, L"Keyboard mappings…");
    AppendMenuW(system, MF_STRING | (g_blockOneDrive ? MF_CHECKED : 0),
                ID_BLOCK_ONEDRIVE, L"Block OneDrive auto-start");
    AppendMenuW(system, MF_STRING | (g_hideNvidiaPanel ? MF_CHECKED : 0),
                ID_HIDE_NVIDIA, L"Hide NVIDIA panel startup/tray entry");
    AppendMenuW(system, MF_STRING | (g_hideAmdPanel ? MF_CHECKED : 0),
                ID_HIDE_AMD, L"Hide AMD panel startup/tray entry");
    AppendMenuW(system, MF_STRING | (g_hideSecurityCenter ? MF_CHECKED : 0),
                ID_HIDE_SECURITY, L"Hide Windows Security tray entry");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(system), L"Windows UI & startup");
    AppendMenuW(menu, MF_STRING, ID_ABOUT, L"About & shortcuts");
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit Liberty");

    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu); // destroys attached submenu too
}

void ShowAbout(HWND window) {
    std::wstring message = L"Liberty 1.0\n\n";
    message += L"Cmd = ";
    message += ModifierLabel(g_commandKey);
    message += L"\nOption = ";
    message += ModifierLabel(g_optionKey);
    message += L"\nControl = ";
    message += ModifierLabel(g_controlKey);
    message += L"\n\nCommand+C/V/X/Z/A/S/F/W and other app shortcuts map to Control.\n";
    message += L"Command+Tab / ` switches apps. Command+Q quits. Command+H/M minimizes.\n";
    message += L"Command+arrows navigate lines/documents. Command+Option+Esc opens Task Manager.\n\n";
    message += L"Ctrl+Alt+F9 pauses/resumes mapping\n";
    message += L"Ctrl+Alt+F10 turns the display off\n";
    message += L"Ctrl+Alt+F11 schedules shutdown in 60 minutes\n";
    message += L"Ctrl+Alt+F12 cancels shutdown\n\n";
    message += L"Windows UI options only hide startup/tray entries; they do not uninstall drivers or disable protection.";
    MessageBoxW(window, message.c_str(), L"About Liberty", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_taskbarCreated && g_taskbarCreated != 0) {
        Shell_NotifyIconW(NIM_ADD, &g_tray);
        g_tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
        return 0;
    }
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TOGGLE: g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled); ReleaseSwitcher(); RefreshTrayIcon(); break;
        case ID_SCREEN_OFF: ScreenOff(); break;
        case ID_SHUTDOWN_15: ScheduleShutdown(15 * 60); break;
        case ID_SHUTDOWN_30: ScheduleShutdown(30 * 60); break;
        case ID_SHUTDOWN_60: ScheduleShutdown(60 * 60); break;
        case ID_SHUTDOWN_120: ScheduleShutdown(120 * 60); break;
        case ID_SHUTDOWN_CUSTOM: { INT_PTR minutes = ShowMinutesDialog(window); if (minutes > 0) ScheduleShutdown(static_cast<UINT>(minutes) * 60); break; }
        case ID_SHUTDOWN_CANCEL: CancelShutdown(); break;
        case ID_STARTUP: SetStartup(!g_startAtLogin); break;
        case ID_MAPPINGS: ShowMappingDialog(window); break;
        case ID_BLOCK_ONEDRIVE: {
            const bool next = !g_blockOneDrive;
            if (ApplyRunEntryBlock(kOneDriveEntry, next)) {
                g_blockOneDrive = next;
                SaveDword(L"BlockOneDrive", g_blockOneDrive);
            } else {
                MessageBoxW(window, L"Liberty could not update the OneDrive startup entry.", L"Liberty",
                            MB_OK | MB_ICONERROR);
            }
            break;
        }
        case ID_HIDE_NVIDIA: {
            const bool next = !g_hideNvidiaPanel;
            if (ApplyRunEntryBlocks(kNvidiaEntries, ARRAYSIZE(kNvidiaEntries), next)) {
                g_hideNvidiaPanel = next;
                SaveDword(L"HideNvidiaPanel", g_hideNvidiaPanel);
            } else {
                MessageBoxW(window, L"Liberty could not update the NVIDIA startup entries.", L"Liberty",
                            MB_OK | MB_ICONERROR);
            }
            break;
        }
        case ID_HIDE_AMD: {
            const bool next = !g_hideAmdPanel;
            if (ApplyRunEntryBlocks(kAmdEntries, ARRAYSIZE(kAmdEntries), next)) {
                g_hideAmdPanel = next;
                SaveDword(L"HideAmdPanel", g_hideAmdPanel);
            } else {
                MessageBoxW(window, L"Liberty could not update the AMD startup entries.", L"Liberty",
                            MB_OK | MB_ICONERROR);
            }
            break;
        }
        case ID_HIDE_SECURITY: {
            const bool next = !g_hideSecurityCenter;
            if (next) {
                MessageBoxW(window,
                            L"This hides only the Windows Security tray entry and notifications.\n\n"
                            L"Defender, Security Center services, and real-time protection remain enabled.\n"
                            L"Windows may show a UAC prompt and require sign-out to refresh the icon.",
                            L"Liberty", MB_OK | MB_ICONWARNING);
            }
            if (SetSecurityCenterHidden(next)) {
                g_hideSecurityCenter = next;
            } else {
                MessageBoxW(window,
                            L"Liberty could not change the Windows Security tray policy.\n"
                            L"Try again and allow the administrator prompt if shown.",
                            L"Liberty", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case ID_ABOUT: ShowAbout(window); break;
        case ID_EXIT: DestroyWindow(window); break;
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == 1) { g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled); ReleaseSwitcher(); RefreshTrayIcon(); }
        if (wParam == 2) ScreenOff();
        if (wParam == 3) ScheduleShutdown(60 * 60);
        if (wParam == 4) CancelShutdown();
        return 0;
    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP) { g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled); ReleaseSwitcher(); RefreshTrayIcon(); }
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) ShowMenu(window);
        return 0;
    case WM_DESTROY:
        ReleaseSwitcher();
        UnregisterHotKey(window, 1); UnregisterHotKey(window, 2); UnregisterHotKey(window, 3); UnregisterHotKey(window, 4);
        if (g_hook) UnhookWindowsHookEx(g_hook);
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
        if (g_tray.hIcon) DestroyIcon(g_tray.hIcon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    g_instance = instance;

    const std::wstring arguments = commandLine ? commandLine : L"";
    if (arguments.rfind(L"--apply-security-systray", 0) == 0) {
        const bool hidden = arguments.find(L" 1") != std::wstring::npos;
        return SetSecurityCenterHiddenDirect(hidden) ? 0 : 1;
    }

    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\Liberty.SingleInstance");
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_enabled = LoadDword(L"Enabled", 1) != 0;
    g_startAtLogin = IsStartupEnabled();
    g_blockOneDrive = LoadDword(L"BlockOneDrive", 0) != 0;
    g_hideNvidiaPanel = LoadDword(L"HideNvidiaPanel", 0) != 0;
    g_hideAmdPanel = LoadDword(L"HideAmdPanel", 0) != 0;
    g_hideSecurityCenter = IsSecurityCenterHidden();
    LoadModifierMappings();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return 1;
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;

    if (g_blockOneDrive) ApplyRunEntryBlock(kOneDriveEntry, true);
    if (g_hideNvidiaPanel) ApplyRunEntryBlocks(kNvidiaEntries, ARRAYSIZE(kNvidiaEntries), true);
    if (g_hideAmdPanel) ApplyRunEntryBlocks(kAmdEntries, ARRAYSIZE(kAmdEntries), true);

    g_tray.cbSize = sizeof(g_tray); g_tray.hWnd = g_window; g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; g_tray.uCallbackMessage = kTrayMessage;
    g_tray.hIcon = CreateTrayIcon(g_enabled); UpdateTrayTip();
    Shell_NotifyIconW(NIM_ADD, &g_tray); g_tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_tray);

    RegisterHotKey(g_window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F9);
    RegisterHotKey(g_window, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10);
    RegisterHotKey(g_window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F11);
    RegisterHotKey(g_window, 4, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    if (!g_hook) {
        MessageBoxW(nullptr, L"Liberty could not install its keyboard hook.", L"Liberty", MB_OK | MB_ICONERROR);
        DestroyWindow(g_window); return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); }
    return static_cast<int>(message.wParam);
}

