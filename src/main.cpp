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
#include <knownfolders.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <objbase.h>
#include <shellscalingapi.h>
#include <taskschd.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The Windows headers define min/max as macros. Keep the standard-library
// algorithms readable and avoid macro expansion in the native UI code.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {

constexpr wchar_t kAppName[] = L"Liberty";
constexpr wchar_t kWindowClass[] = L"Liberty.TrayWindow";
constexpr wchar_t kMenuClass[] = L"Liberty.ModernMenu";
constexpr wchar_t kOverlayClass[] = L"Liberty.ImageOverlay";
constexpr wchar_t kCleanupClass[] = L"Liberty.CleanupWindow";
constexpr wchar_t kStartupClass[] = L"Liberty.StartupManager";
constexpr wchar_t kAboutClass[] = L"Liberty.AboutWindow";
constexpr wchar_t kUiFontProperty[] = L"Liberty.UiFont";
constexpr wchar_t kRegistryKey[] = L"Software\\Liberty";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupApprovedRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kSecuritySystrayPolicyKey[] =
    L"SOFTWARE\\Policies\\Microsoft\\Windows Defender Security Center\\Systray";
constexpr wchar_t kSecuritySystrayPolicyValue[] = L"HideSystray";
constexpr wchar_t kSecuritySystrayBackupValue[] = L"SecuritySystrayBackupValue";
constexpr wchar_t kSecuritySystrayBackupValid[] = L"SecuritySystrayBackupValid";
constexpr wchar_t kVolumeCachesKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VolumeCaches";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kCleanupScanComplete = WM_APP + 3;
constexpr UINT kCleanupRunComplete = WM_APP + 4;
constexpr UINT kStartupScanComplete = WM_APP + 6;
constexpr UINT kStartupApplyComplete = WM_APP + 7;
constexpr UINT kMenuRebuild = WM_APP + 8;
constexpr UINT_PTR kOneDriveRefreshTimer = 5;
constexpr ULONG_PTR kInjectedMarker = 0x4C494245525459ULL;

enum Command : UINT {
    ID_TOGGLE = 100,
    ID_SCREEN_OFF,
    ID_SCREENSHOT_DESKTOP,
    ID_SHUTDOWN_15,
    ID_SHUTDOWN_30,
    ID_SHUTDOWN_60,
    ID_SHUTDOWN_120,
    ID_SHUTDOWN_CUSTOM,
    ID_SHUTDOWN_CANCEL,
    ID_STARTUP,
    ID_MAPPINGS,
    ID_STARTUP_MANAGER,
    ID_MENU_LANGUAGE,
    ID_MENU_SHORTCUTS,
    ID_MENU_MAINTENANCE,
    ID_MENU_STATUS,
    ID_MENU_OTHER,
    ID_MENU_BACK,
    ID_BLOCK_ONEDRIVE,
    ID_HIDE_NVIDIA,
    ID_HIDE_AMD,
    ID_HIDE_SECURITY,
    ID_OVERLAY_OPEN,
    ID_OVERLAY_RESTORE,
    ID_OVERLAY_LOCK,
    ID_OVERLAY_CLICKTHROUGH,
    ID_OVERLAY_OPACITY_25,
    ID_OVERLAY_OPACITY_50,
    ID_OVERLAY_OPACITY_75,
    ID_OVERLAY_OPACITY_100,
    ID_OVERLAY_CLOSE,
    ID_CLEANUP,
    ID_ABOUT,
    ID_EXIT
};

enum MappingControl : WORD {
    ID_MAPPING_HINT = 1100,
    ID_MAPPING_COMMAND = 1101,
    ID_MAPPING_OPTION = 1102,
    ID_MAPPING_CONTROL = 1103
};

enum CleanupControl : int {
    ID_CLEANUP_LIST = 2100,
    ID_CLEANUP_STATUS,
    ID_CLEANUP_SCAN,
    ID_CLEANUP_RUN,
    ID_CLEANUP_CANCEL
};

enum StartupControl : int {
    ID_STARTUP_LIST = 2300,
    ID_STARTUP_STATUS,
    ID_STARTUP_SCAN,
    ID_STARTUP_SELECT_RISK,
    ID_STARTUP_BLOCK,
    ID_STARTUP_RESTORE,
    ID_STARTUP_CANCEL
};

enum class AppLanguage : DWORD { English = 0, Chinese = 1, Auto = 2 };

enum class ManagedStartupKind { Registry, StartupFolder, ScheduledTask, Service };
enum class MenuPage { Root, Shortcuts, Maintenance, Status, Other };

struct ModifierChoice {
    WORD virtualKey;
    const wchar_t* english;
    const wchar_t* chinese;
};

struct StartupEntry {
    const wchar_t* runValue;
    const wchar_t* backupValue;
    const wchar_t* backupType;
    const wchar_t* backupValid;
};

struct TaskSnapshot {
    std::wstring path;
    bool enabled = true;
};

struct MenuRow {
    enum Kind { Header, Action, Separator, Status, Category } kind = Action;
    UINT id = 0;
    std::wstring label;
    std::wstring detail;
    bool checked = false;
    bool enabled = true;
};

struct OverlayState {
    std::wstring path;
    std::vector<BYTE> pixels;
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    float scale = 1.0f;
    BYTE opacity = 235;
    bool locked = false;
    bool clickThrough = false;
    bool dragging = false;
    POINT dragStart{};
    POINT windowStart{};
};

struct CleanupItem {
    std::wstring keyName;
    std::wstring displayName;
    std::wstring description;
    DWORDLONG space = 0;
    bool safeDefault = false;
    bool highRisk = false;
    bool handlerReady = false;
    bool needsElevation = true;
};

struct CleanupScanResult { std::vector<CleanupItem> items; };

struct CleanupRunResult {
    bool success = false;
    std::wstring message;
};

struct StartupItem {
    ManagedStartupKind kind = ManagedStartupKind::Registry;
    std::wstring id;
    std::wstring name;
    std::wstring source;
    std::wstring command;
    std::wstring location;
    std::wstring registrySubKey;
    std::wstring registryValueName;
    std::wstring filePath;
    std::wstring taskPath;
    std::wstring serviceName;
    HKEY registryRoot = nullptr;
    REGSAM registryView = 0;
    DWORD serviceStartType = SERVICE_NO_CHANGE;
    bool enabled = true;
    bool requiresElevation = false;
    bool chainRisk = false;
    bool highRisk = false;
    bool thirdParty = false;
    bool protectedItem = false;
};

struct StartupScanResult { std::vector<StartupItem> items; };

struct StartupApplyResult {
    bool success = false;
    size_t changed = 0;
    std::wstring message;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_menuWindow = nullptr;
HWND g_overlayWindow = nullptr;
HWND g_cleanupWindow = nullptr;
HWND g_startupWindow = nullptr;
HWND g_aboutWindow = nullptr;
HHOOK g_hook = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_tray{};
ULONG_PTR g_gdiplusToken = 0;
UINT g_taskbarCreated = 0;

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
UINT g_pendingShutdownSeconds = 0;
AppLanguage g_language = AppLanguage::English;
AppLanguage g_languageSetting = AppLanguage::Auto;
OverlayState g_overlay{};
std::wstring g_initialOverlayPath;
std::vector<CleanupItem> g_cleanupItems;
std::vector<StartupItem> g_startupItems;
int g_menuHover = -1;
int g_menuSelected = -1;
int g_menuScroll = 0;
DWORD g_lastTrayMenuEventTick = 0;
MenuPage g_menuPage = MenuPage::Root;

constexpr ModifierChoice kModifierChoices[] = {
    {VK_LWIN, L"Left Windows", L"左 Windows"},
    {VK_RWIN, L"Right Windows", L"右 Windows"},
    {VK_LMENU, L"Left Alt", L"左 Alt"},
    {VK_RMENU, L"Right Alt", L"右 Alt"},
    {VK_LCONTROL, L"Left Control", L"左 Control"},
    {VK_RCONTROL, L"Right Control", L"右 Control"},
    {VK_CAPITAL, L"Caps Lock", L"Caps Lock"}
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

bool IsDarkTheme();
void SetDarkMode(HWND window, bool dark);
void UpdateTrayTip();
void CloseOverlay();
void ShowStartupWindow(HWND owner);
void RefreshModernMenu();

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool ContainsInsensitive(const std::wstring& value, const wchar_t* needle) {
    return Lower(value).find(Lower(needle)) != std::wstring::npos;
}

const wchar_t* T(const wchar_t* english, const wchar_t* chinese) {
    return g_language == AppLanguage::Chinese ? chinese : english;
}

std::wstring FormatBytes(DWORDLONG bytes) {
    wchar_t buffer[64]{};
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    }
    return buffer;
}

bool ReadRegistryDword(HKEY root, const wchar_t* subKey, const wchar_t* name, DWORD& value) {
    HKEY key = nullptr;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    const LONG result = RegQueryValueExW(key, name, nullptr, &type,
                                         reinterpret_cast<BYTE*>(&value), &size);
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
    result = RegQueryValueExW(key, name, nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()), &size);
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

bool ReadRegistryBinary(HKEY root, const wchar_t* subKey, const wchar_t* name,
                        std::vector<BYTE>& data, DWORD* typeOut = nullptr) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_BINARY) {
        RegCloseKey(key);
        return false;
    }
    data.resize(size);
    result = RegQueryValueExW(key, name, nullptr, &type, data.data(), &size);
    RegCloseKey(key);
    if (typeOut) *typeOut = type;
    return result == ERROR_SUCCESS;
}

bool WriteRegistryBinary(HKEY root, const wchar_t* subKey, const wchar_t* name,
                         const std::vector<BYTE>& data, DWORD type = REG_BINARY) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG result = RegSetValueExW(key, name, 0, type, data.data(),
                                       static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool ReadRegistryMultiSz(HKEY root, const wchar_t* subKey, const wchar_t* name,
                         std::vector<std::wstring>& values) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS || type != REG_MULTI_SZ) {
        RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 2, L'\0');
    result = RegQueryValueExW(key, name, nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return false;
    for (const wchar_t* current = buffer.data(); *current; current += wcslen(current) + 1) {
        values.emplace_back(current);
    }
    return true;
}

bool WriteRegistryMultiSz(HKEY root, const wchar_t* subKey, const wchar_t* name,
                          const std::vector<std::wstring>& values) {
    std::vector<wchar_t> buffer;
    for (const std::wstring& value : values) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG result = RegSetValueExW(key, name, 0, REG_MULTI_SZ,
                                       reinterpret_cast<const BYTE*>(buffer.data()),
                                       static_cast<DWORD>(buffer.size() * sizeof(wchar_t)));
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

constexpr wchar_t kStartupBackupKey[] = L"Software\\Liberty\\StartupManager\\Backups";

std::wstring StartupRegistryViewLabel(REGSAM view);
std::wstring StartupRegistryRootLabel(HKEY root);
std::wstring StartupBackupName(const StartupItem& item, const wchar_t* suffix);
bool HasStartupBackup(const StartupItem& item);
void ClassifyStartupItem(StartupItem& item);

bool SaveStartupDword(const wchar_t* name, DWORD value) {
    return WriteRegistryDword(HKEY_CURRENT_USER, kStartupBackupKey, name, value);
}

DWORD LoadStartupDword(const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    if (!ReadRegistryDword(HKEY_CURRENT_USER, kStartupBackupKey, name, value)) value = fallback;
    return value;
}

bool SaveStartupString(const wchar_t* name, const std::wstring& value) {
    return WriteRegistryString(HKEY_CURRENT_USER, kStartupBackupKey, name, value);
}

bool LoadStartupString(const wchar_t* name, std::wstring& value) {
    return ReadRegistryString(HKEY_CURRENT_USER, kStartupBackupKey, name, value);
}

bool SaveStartupMetadata(const StartupItem& item) {
    const std::vector<std::wstring> values = {
        std::to_wstring(static_cast<int>(item.kind)), item.id, item.name, item.source,
        item.command, item.location, item.registrySubKey, item.registryValueName,
        item.filePath, item.taskPath, item.serviceName, StartupRegistryRootLabel(item.registryRoot),
        StartupRegistryViewLabel(item.registryView), std::to_wstring(item.serviceStartType)
    };
    return WriteRegistryMultiSz(HKEY_CURRENT_USER, kStartupBackupKey,
                                StartupBackupName(item, L"_Meta").c_str(), values);
}

bool LoadStartupMetadata(const wchar_t* valueName, StartupItem& item) {
    std::vector<std::wstring> values;
    if (!ReadRegistryMultiSz(HKEY_CURRENT_USER, kStartupBackupKey, valueName, values) || values.size() < 14) return false;
    item.kind = static_cast<ManagedStartupKind>(_wtoi(values[0].c_str()));
    item.id = values[1]; item.name = values[2]; item.source = values[3]; item.command = values[4];
    item.location = values[5]; item.registrySubKey = values[6]; item.registryValueName = values[7];
    item.filePath = values[8]; item.taskPath = values[9]; item.serviceName = values[10];
    item.registryRoot = values[11] == L"HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    item.registryView = values[12] == L"32-bit" ? KEY_WOW64_32KEY : values[12] == L"64-bit" ? KEY_WOW64_64KEY : 0;
    item.serviceStartType = wcstoul(values[13].c_str(), nullptr, 10);
    item.requiresElevation = item.registryRoot == HKEY_LOCAL_MACHINE ||
                             item.kind == ManagedStartupKind::ScheduledTask || item.kind == ManagedStartupKind::Service;
    item.enabled = false;
    ClassifyStartupItem(item);
    return HasStartupBackup(item);
}

void MergeStartupBackupItems(std::vector<StartupItem>& result) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupBackupKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD maxName = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &maxName, nullptr, nullptr, nullptr);
    std::vector<wchar_t> valueName(maxName + 2, L'\0');
    DWORD index = 0;
    while (true) {
        DWORD nameLength = static_cast<DWORD>(valueName.size() - 1);
        DWORD type = 0, dataSize = 0;
        LONG status = RegEnumValueW(key, index++, valueName.data(), &nameLength, nullptr, &type, nullptr, &dataSize);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if ((status != ERROR_SUCCESS && status != ERROR_MORE_DATA) || type != REG_MULTI_SZ) continue;
        const std::wstring name(valueName.data(), nameLength);
        if (name.size() < 5 || name.substr(name.size() - 5) != L"_Meta") continue;
        StartupItem item;
        if (!LoadStartupMetadata(name.c_str(), item)) continue;
        const bool exists = std::any_of(result.begin(), result.end(), [&item](const StartupItem& current) { return current.id == item.id; });
        if (!exists) result.push_back(std::move(item));
    }
    RegCloseKey(key);
}

bool ReadRawRegistryValue(HKEY root, const std::wstring& subKey, const std::wstring& name,
                          REGSAM view, std::vector<BYTE>& data, DWORD& type) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) return false;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size);
    if (result != ERROR_SUCCESS) { RegCloseKey(key); return false; }
    data.resize(size);
    result = RegQueryValueExW(key, name.c_str(), nullptr, &type, data.data(), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool WriteRawRegistryValue(HKEY root, const std::wstring& subKey, const std::wstring& name,
                           REGSAM view, DWORD type, const std::vector<BYTE>& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE | view,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const LONG result = RegSetValueExW(key, name.c_str(), 0, type, data.data(),
                                       static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

std::wstring StartupHash(const std::wstring& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t character : value) {
        hash ^= static_cast<uint16_t>(character);
        hash *= 1099511628211ULL;
    }
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%016llX", static_cast<unsigned long long>(hash));
    return buffer;
}

std::wstring StartupBackupName(const StartupItem& item, const wchar_t* suffix) {
    return StartupHash(item.id) + suffix;
}

bool HasStartupBackup(const StartupItem& item) {
    return LoadStartupDword(StartupBackupName(item, L"_Valid").c_str(), 0) != 0;
}

bool SaveStartupRawBackup(const StartupItem& item, DWORD type, const std::vector<BYTE>& data) {
    std::vector<BYTE> blob(sizeof(DWORD) * 2 + data.size());
    const DWORD size = static_cast<DWORD>(data.size());
    memcpy(blob.data(), &type, sizeof(type));
    memcpy(blob.data() + sizeof(type), &size, sizeof(size));
    if (!data.empty()) memcpy(blob.data() + sizeof(type) * 2, data.data(), data.size());
    if (!WriteRegistryBinary(HKEY_CURRENT_USER, kStartupBackupKey,
                             StartupBackupName(item, L"_Raw").c_str(), blob)) return false;
    SaveStartupDword(StartupBackupName(item, L"_Valid").c_str(), 1);
    return true;
}

bool LoadStartupRawBackup(const StartupItem& item, DWORD& type, std::vector<BYTE>& data) {
    std::vector<BYTE> blob;
    if (!ReadRegistryBinary(HKEY_CURRENT_USER, kStartupBackupKey,
                            StartupBackupName(item, L"_Raw").c_str(), blob) || blob.size() < sizeof(DWORD) * 2) return false;
    DWORD size = 0;
    memcpy(&type, blob.data(), sizeof(type));
    memcpy(&size, blob.data() + sizeof(type), sizeof(size));
    if (blob.size() != sizeof(DWORD) * 2 + size) return false;
    data.assign(blob.begin() + sizeof(DWORD) * 2, blob.end());
    return true;
}

void ClearStartupBackup(const StartupItem& item) {
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_Raw").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_Folder").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_FolderOriginal").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_FolderBackup").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_ServiceType").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_TaskEnabled").c_str());
    DeleteRegistryValue(HKEY_CURRENT_USER, kStartupBackupKey, StartupBackupName(item, L"_Meta").c_str());
    SaveStartupDword(StartupBackupName(item, L"_Valid").c_str(), 0);
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

bool SaveBinarySetting(const wchar_t* name, const std::vector<BYTE>& value) {
    return WriteRegistryBinary(HKEY_CURRENT_USER, kRegistryKey, name, value);
}

bool LoadBinarySetting(const wchar_t* name, std::vector<BYTE>& value) {
    return ReadRegistryBinary(HKEY_CURRENT_USER, kRegistryKey, name, value);
}

std::wstring GetKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path)) || !path) return {};
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

std::wstring GetModulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

UINT GetWindowDpiSafe(HWND window) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

UINT GetMonitorDpiSafe(HMONITOR monitor, HWND fallbackWindow = nullptr) {
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX) return dpiX;
    return GetWindowDpiSafe(fallbackWindow);
}

int ScaleUi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

HFONT CreateUiFont(HWND referenceWindow, int pixelHeight, LONG weight = FW_NORMAL,
                   bool displayFace = false) {
    const UINT dpi = GetWindowDpiSafe(referenceWindow);
    const int height = -ScaleUi(pixelHeight, dpi);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, displayFace ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text");
}

void ApplyUiFont(HWND window, int pixelHeight = 16) {
    if (!window) return;
    if (HANDLE previous = RemovePropW(window, kUiFontProperty)) DeleteObject(previous);
    HFONT font = CreateUiFont(window, pixelHeight);
    if (!font) return;
    SetPropW(window, kUiFontProperty, font);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void ReleaseUiFont(HWND window) {
    if (window) {
        if (HANDLE font = RemovePropW(window, kUiFontProperty)) DeleteObject(font);
    }
}

void InitializeLanguage() {
    g_languageSetting = static_cast<AppLanguage>(LoadDword(L"Language", static_cast<DWORD>(AppLanguage::Auto)));
    if (g_languageSetting != AppLanguage::English && g_languageSetting != AppLanguage::Chinese &&
        g_languageSetting != AppLanguage::Auto) g_languageSetting = AppLanguage::Auto;
    if (g_languageSetting == AppLanguage::Auto) {
        g_language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE
                         ? AppLanguage::Chinese : AppLanguage::English;
    } else {
        g_language = g_languageSetting;
    }
}

void ToggleLanguage() {
    g_language = g_language == AppLanguage::Chinese ? AppLanguage::English : AppLanguage::Chinese;
    g_languageSetting = g_language;
    SaveDword(L"Language", static_cast<DWORD>(g_languageSetting));
    if (g_menuWindow) InvalidateRect(g_menuWindow, nullptr, TRUE);
    UpdateTrayTip();
}

bool IsKeyDown(int virtualKey) { return (GetAsyncKeyState(virtualKey) & 0x8000) != 0; }

void SendKey(WORD virtualKey, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = kInjectedMarker;
    SendInput(1, &input, sizeof(input));
}

void TapKey(WORD virtualKey) {
    SendKey(virtualKey, true);
    SendKey(virtualKey, false);
}

void SendModifiedKey(WORD modifier, WORD virtualKey, bool down) {
    SendKey(modifier, true);
    SendKey(virtualKey, down);
    SendKey(modifier, false);
}

void ReleaseSwitcher() {
    if (g_switchAltDown) {
        SendKey(VK_MENU, false);
        g_switchAltDown = false;
    }
}

void MinimizeForeground() {
    const HWND active = GetForegroundWindow();
    if (active) ShowWindow(active, SW_MINIMIZE);
}

bool IsModifierChoice(WORD virtualKey) {
    for (const ModifierChoice& choice : kModifierChoices) if (choice.virtualKey == virtualKey) return true;
    return false;
}

WORD FirstAvailableModifier(WORD firstUsed, WORD secondUsed) {
    for (const ModifierChoice& choice : kModifierChoices) {
        if (choice.virtualKey != firstUsed && choice.virtualKey != secondUsed) return choice.virtualKey;
    }
    return VK_LCONTROL;
}

const wchar_t* ModifierLabel(WORD virtualKey) {
    for (const ModifierChoice& choice : kModifierChoices) {
        if (choice.virtualKey == virtualKey) return T(choice.english, choice.chinese);
    }
    return T(L"Unknown", L"未知");
}

int ModifierChoiceIndex(WORD virtualKey) {
    for (size_t index = 0; index < ARRAYSIZE(kModifierChoices); ++index) {
        if (kModifierChoices[index].virtualKey == virtualKey) return static_cast<int>(index);
    }
    return 0;
}

void LoadModifierMappings() {
    const WORD savedCommand = static_cast<WORD>(LoadDword(L"CommandKey", VK_LWIN));
    const WORD savedOption = static_cast<WORD>(LoadDword(L"OptionKey", VK_LMENU));
    const WORD savedControl = static_cast<WORD>(LoadDword(L"ControlKey", VK_LCONTROL));
    g_commandKey = IsModifierChoice(savedCommand) ? savedCommand : VK_LWIN;
    g_optionKey = IsModifierChoice(savedOption) && savedOption != g_commandKey
                      ? savedOption : FirstAvailableModifier(g_commandKey, 0);
    g_controlKey = IsModifierChoice(savedControl) && savedControl != g_commandKey && savedControl != g_optionKey
                       ? savedControl : FirstAvailableModifier(g_commandKey, g_optionKey);
}

void ResetMappedModifierState() {
    if (g_optionDown && g_optionKey != VK_LMENU && g_optionKey != VK_RMENU) SendKey(VK_MENU, false);
    if (g_controlDown && g_controlKey != VK_LCONTROL && g_controlKey != VK_RCONTROL) SendKey(VK_CONTROL, false);
    g_commandDown = false;
    g_optionDown = false;
    g_controlDown = false;
    ReleaseSwitcher();
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data) {
    if (code < 0 || !g_enabled) return CallNextHookEx(g_hook, code, message, data);
    const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (event->dwExtraInfo == kInjectedMarker) return CallNextHookEx(g_hook, code, message, data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(g_hook, code, message, data);
    const DWORD virtualKey = event->vkCode;
    if (virtualKey == g_commandKey) {
        if (down) g_commandDown = true;
        if (up) { g_commandDown = false; ReleaseSwitcher(); }
        return 1;
    }
    if (virtualKey == g_optionKey) {
        g_optionDown = down;
        if (g_optionKey == VK_LMENU || g_optionKey == VK_RMENU) return CallNextHookEx(g_hook, code, message, data);
        SendKey(VK_MENU, down);
        return 1;
    }
    if (virtualKey == g_controlKey) {
        g_controlDown = down;
        if (g_controlKey == VK_LCONTROL || g_controlKey == VK_RCONTROL) return CallNextHookEx(g_hook, code, message, data);
        SendKey(VK_CONTROL, down);
        return 1;
    }
    if (!g_commandDown && g_optionDown) {
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT) {
            SendKey(VK_MENU, false);
            SendModifiedKey(VK_CONTROL, static_cast<WORD>(virtualKey), down);
            SendKey(VK_MENU, true);
            return 1;
        }
        if (virtualKey == VK_BACK) {
            SendKey(VK_MENU, false);
            SendModifiedKey(VK_CONTROL, VK_BACK, down);
            SendKey(VK_MENU, true);
            return 1;
        }
    }
    if (!g_commandDown) return CallNextHookEx(g_hook, code, message, data);
    if (virtualKey == VK_TAB || virtualKey == VK_OEM_3) {
        if (!g_switchAltDown) { SendKey(VK_MENU, true); g_switchAltDown = true; }
        if (virtualKey == VK_OEM_3) SendKey(VK_SHIFT, down);
        return 1;
    }
    if (virtualKey == 'Q') { SendModifiedKey(VK_MENU, VK_F4, down); return 1; }
    if (virtualKey == 'H' || virtualKey == 'M') { if (down) MinimizeForeground(); return 1; }
    if (virtualKey == VK_SPACE) {
        if (down) { SendKey(VK_LWIN, true); TapKey('S'); SendKey(VK_LWIN, false); }
        return 1;
    }
    if ((virtualKey == '3' || virtualKey == '4') && IsKeyDown(VK_SHIFT)) {
        if (down && virtualKey == '3') TapKey(VK_SNAPSHOT);
        if (down && virtualKey == '4') {
            SendKey(VK_LWIN, true); SendKey(VK_SHIFT, true); TapKey('S');
            SendKey(VK_SHIFT, false); SendKey(VK_LWIN, false);
        }
        return 1;
    }
    if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT) {
        SendKey(virtualKey == VK_LEFT ? VK_HOME : VK_END, down);
        return 1;
    }
    if (virtualKey == VK_UP || virtualKey == VK_DOWN) {
        SendModifiedKey(VK_CONTROL, virtualKey == VK_UP ? VK_HOME : VK_END, down);
        return 1;
    }
    SendModifiedKey(VK_CONTROL, static_cast<WORD>(virtualKey), down);
    return 1;
}

void DrawTrinityLogoGraphics(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, bool active) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float width = bounds.Width;
    const float height = bounds.Height;
    const float size = std::min(width, height);
    const float x = bounds.X + (width - size) / 2.0f;
    const float y = bounds.Y + (height - size) / 2.0f;
    const Gdiplus::RectF circle(x + size * 0.12f, y + size * 0.12f, size * 0.76f, size * 0.76f);
    const Gdiplus::Color colors[] = {
        Gdiplus::Color(255, 79, 140, 255), Gdiplus::Color(255, 99, 91, 255), Gdiplus::Color(255, 32, 199, 217)
    };
    const float penWidth = std::max(2.0f, size * 0.105f);
    for (int index = 0; index < 3; ++index) {
        Gdiplus::Pen pen(active ? colors[index] : Gdiplus::Color(255, 130, 135, 145), penWidth);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawArc(&pen, circle, -90.0f + index * 120.0f, 96.0f);
    }
    Gdiplus::PointF triangle[] = {
        {x + size * 0.50f, y + size * 0.30f}, {x + size * 0.68f, y + size * 0.61f}, {x + size * 0.32f, y + size * 0.61f}
    };
    Gdiplus::SolidBrush center(Gdiplus::Color(40, 11, 16, 32));
    graphics.FillPolygon(&center, triangle, ARRAYSIZE(triangle));
}

void DrawTrinityLogo(HDC hdc, const RECT& bounds, bool active) {
    Gdiplus::Graphics graphics(hdc);
    DrawTrinityLogoGraphics(graphics, Gdiplus::RectF(
        static_cast<float>(bounds.left), static_cast<float>(bounds.top),
        static_cast<float>(bounds.right - bounds.left), static_cast<float>(bounds.bottom - bounds.top)), active);
}

HICON CreateFallbackIcon(bool active) {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memory, color));
    RECT rect{0, 0, size, size};
    HBRUSH background = CreateSolidBrush(active ? RGB(54, 91, 220) : RGB(110, 110, 110));
    FillRect(memory, &rect, background);
    DeleteObject(background);
    SelectObject(memory, old);
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

HICON CreateTrinityIcon(bool active) {
    if (!g_gdiplusToken) return CreateFallbackIcon(active);
    Gdiplus::Bitmap bitmap(64, 64, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    DrawTrinityLogoGraphics(graphics, Gdiplus::RectF(0.0f, 0.0f, 64.0f, 64.0f), active);
    HICON icon = nullptr;
    if (bitmap.GetHICON(&icon) != Gdiplus::Ok || !icon) return CreateFallbackIcon(active);
    return icon;
}

void UpdateTrayTip() {
    const wchar_t* state = g_enabled ? T(L"shortcuts enabled", L"快捷键已启用") : T(L"shortcuts paused", L"快捷键已暂停");
    std::wstring tip = L"Liberty — ";
    tip += state;
    lstrcpynW(g_tray.szTip, tip.c_str(), ARRAYSIZE(g_tray.szTip));
    if (g_tray.hWnd) Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void RefreshTrayIcon() {
    HICON next = CreateTrinityIcon(g_enabled);
    HICON previous = g_tray.hIcon;
    g_tray.hIcon = next;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    if (previous) DestroyIcon(previous);
    UpdateTrayTip();
}

void ScreenOff() { PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2); }

int FindImageEncoder(const WCHAR* mimeType, CLSID* clsid) {
    UINT number = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&number, &bytes) != Gdiplus::Ok || !bytes) return -1;
    std::vector<BYTE> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(number, bytes, encoders) != Gdiplus::Ok) return -1;
    for (UINT index = 0; index < number; ++index) {
        if (wcscmp(encoders[index].MimeType, mimeType) == 0) {
            *clsid = encoders[index].Clsid;
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ShowTrayNotification(const std::wstring& text) {
    if (!g_tray.hWnd) return;
    const UINT previousFlags = g_tray.uFlags;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_INFO;
    lstrcpynW(g_tray.szInfoTitle, kAppName, ARRAYSIZE(g_tray.szInfoTitle));
    lstrcpynW(g_tray.szInfo, text.c_str(), ARRAYSIZE(g_tray.szInfo));
    g_tray.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = previousFlags;
}

bool CaptureDesktopScreenshot() {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) return false;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    if (!screen || !memory || !bitmap || !BitBlt(memory, 0, 0, width, height, screen, left, top,
                                                  SRCCOPY | CAPTUREBLT)) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(nullptr, screen);
        return false;
    }

    const std::wstring desktop = GetKnownFolder(FOLDERID_Desktop);
    if (desktop.empty()) {
        DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen); return false;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring stem = L"Liberty Screenshot " + std::to_wstring(now.wYear) +
        (now.wMonth < 10 ? L"-0" : L"-") + std::to_wstring(now.wMonth) +
        (now.wDay < 10 ? L"-0" : L"-") + std::to_wstring(now.wDay) + L"_" +
        (now.wHour < 10 ? L"0" : L"") + std::to_wstring(now.wHour) +
        (now.wMinute < 10 ? L"0" : L"") + std::to_wstring(now.wMinute) +
        (now.wSecond < 10 ? L"0" : L"") + std::to_wstring(now.wSecond);
    std::wstring path = desktop + L"\\" + stem + L".png";
    for (int suffix = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; ++suffix)
        path = desktop + L"\\" + stem + L" (" + std::to_wstring(suffix) + L").png";

    CLSID pngClsid{};
    bool saved = false;
    if (FindImageEncoder(L"image/png", &pngClsid) >= 0) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        saved = image.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
    }
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (saved) ShowTrayNotification(T(L"Screenshot saved to Desktop.", L"截图已保存到桌面。"));
    return saved;
}

bool RunShutdownCommand(const wchar_t* arguments) {
    wchar_t systemDirectory[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory))) return false;
    const std::wstring executable = std::wstring(systemDirectory) + L"\\shutdown.exe";
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
    RunShutdownCommand(L"/a");
    if (!RunShutdownCommand((L"/s /t " + std::to_wstring(seconds)).c_str())) {
        MessageBoxW(g_window, T(L"Windows did not accept the shutdown schedule.", L"Windows 未接受关机计划。"),
                    kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    g_pendingShutdownSeconds = seconds;
    SaveDword(L"PendingShutdown", seconds);
    std::wstring message = T(L"Windows will shut down in ", L"Windows 将在 ");
    message += std::to_wstring(seconds / 60);
    message += T(L" minutes.", L" 分钟后关机。");
    g_tray.uFlags = NIF_INFO;
    lstrcpynW(g_tray.szInfoTitle, kAppName, ARRAYSIZE(g_tray.szInfoTitle));
    lstrcpynW(g_tray.szInfo, message.c_str(), ARRAYSIZE(g_tray.szInfo));
    g_tray.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void CancelShutdown() {
    if (!RunShutdownCommand(L"/a")) {
        MessageBoxW(g_window, T(L"There is no Windows shutdown countdown to cancel.", L"当前没有可取消的 Windows 关机倒计时。"),
                    kAppName, MB_OK | MB_ICONINFORMATION);
    }
    g_pendingShutdownSeconds = 0;
    SaveDword(L"PendingShutdown", 0);
}

bool IsStartupEnabled() {
    std::wstring current;
    if (!ReadRegistryString(HKEY_CURRENT_USER, kRunKey, kAppName, current)) return false;
    return ContainsInsensitive(current, GetModulePath().c_str());
}

bool SetStartup(bool enabled) {
    const std::wstring module = GetModulePath();
    if (module.empty()) return false;
    if (enabled) {
        if (!WriteRegistryString(HKEY_CURRENT_USER, kRunKey, kAppName, L"\"" + module + L"\"")) return false;
    } else if (!DeleteRegistryValue(HKEY_CURRENT_USER, kRunKey, kAppName)) return false;
    g_startAtLogin = enabled;
    return true;
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
            } else SaveDword(entry.backupValid, 0);
        }
        return DeleteRegistryValue(HKEY_CURRENT_USER, kRunKey, entry.runValue);
    }
    if (!hasBackup) return true;
    std::wstring current;
    if (ReadRegistryString(HKEY_CURRENT_USER, kRunKey, entry.runValue, current)) return ClearRunEntryBackup(entry);
    std::wstring backup;
    if (!LoadStringSetting(entry.backupValue, backup)) return false;
    if (!WriteRegistryString(HKEY_CURRENT_USER, kRunKey, entry.runValue, backup, LoadDword(entry.backupType, REG_SZ))) return false;
    return ClearRunEntryBackup(entry);
}

bool ApplyRunEntryBlocks(const StartupEntry* entries, size_t count, bool blocked) {
    bool success = true;
    for (size_t index = 0; index < count; ++index) if (!ApplyRunEntryBlock(entries[index], blocked)) success = false;
    return success;
}

bool ClearBinaryBackup(const wchar_t* dataName, const wchar_t* validName) {
    const bool removed = DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, dataName);
    SaveDword(validName, 0);
    return removed;
}

bool ApplyBinaryEntryBlock(const wchar_t* sourceKey, const wchar_t* sourceValue,
                           const wchar_t* backupValue, const wchar_t* backupValid, bool blocked) {
    const bool hasBackup = LoadDword(backupValid, 0) != 0;
    if (blocked) {
        if (!hasBackup) {
            std::vector<BYTE> current;
            if (ReadRegistryBinary(HKEY_CURRENT_USER, sourceKey, sourceValue, current)) {
                if (!SaveBinarySetting(backupValue, current)) return false;
                SaveDword(backupValid, 1);
            } else SaveDword(backupValid, 0);
        }
        return DeleteRegistryValue(HKEY_CURRENT_USER, sourceKey, sourceValue);
    }
    if (!hasBackup) return true;
    std::vector<BYTE> current;
    if (ReadRegistryBinary(HKEY_CURRENT_USER, sourceKey, sourceValue, current)) return ClearBinaryBackup(backupValue, backupValid);
    std::vector<BYTE> backup;
    if (!LoadBinarySetting(backupValue, backup)) return false;
    if (!WriteRegistryBinary(HKEY_CURRENT_USER, sourceKey, sourceValue, backup)) return false;
    return ClearBinaryBackup(backupValue, backupValid);
}

std::wstring GetStartupFolder() { return GetKnownFolder(FOLDERID_Startup); }

bool ApplyOneDriveStartupLink(bool blocked) {
    constexpr wchar_t kValid[] = L"OneDriveStartupLinkBackupValid";
    constexpr wchar_t kOriginal[] = L"OneDriveStartupLinkOriginal";
    constexpr wchar_t kBackup[] = L"OneDriveStartupLinkBackup";
    const bool hasBackup = LoadDword(kValid, 0) != 0;
    if (blocked) {
        if (hasBackup) return true;
        const std::wstring startup = GetStartupFolder();
        if (startup.empty()) return true;
        WIN32_FIND_DATAW data{};
        const HANDLE find = FindFirstFileW((startup + L"\\*.lnk").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) { SaveDword(kValid, 0); return true; }
        std::wstring original;
        do {
            if (ContainsInsensitive(data.cFileName, L"onedrive")) { original = startup + L"\\" + data.cFileName; break; }
        } while (FindNextFileW(find, &data));
        FindClose(find);
        if (original.empty()) { SaveDword(kValid, 0); return true; }
        const std::wstring local = GetKnownFolder(FOLDERID_LocalAppData);
        const std::wstring directory = local + L"\\Liberty\\StartupBackup";
        CreateDirectoryW((local + L"\\Liberty").c_str(), nullptr);
        CreateDirectoryW(directory.c_str(), nullptr);
        const std::wstring backup = directory + L"\\OneDrive.lnk";
        if (!MoveFileExW(original.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
        SaveStringSetting(kOriginal, original); SaveStringSetting(kBackup, backup); SaveDword(kValid, 1);
        return true;
    }
    if (!hasBackup) return true;
    std::wstring original, backup;
    if (!LoadStringSetting(kOriginal, original) || !LoadStringSetting(kBackup, backup)) return false;
    bool success = true;
    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(original.c_str()) == INVALID_FILE_ATTRIBUTES)
        success = MoveFileExW(backup.c_str(), original.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    if (success) {
        DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, kOriginal);
        DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, kBackup);
        SaveDword(kValid, 0);
    }
    return success;
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* operator->() const { return value_; }
    T* get() const { return value_; }
    T** put() { reset(); return &value_; }
    void reset(T* value = nullptr) { if (value_) value_->Release(); value_ = value; }
    explicit operator bool() const { return value_ != nullptr; }
private:
    T* value_ = nullptr;
};

std::wstring BstrText(BSTR value) { return value ? std::wstring(value, SysStringLen(value)) : std::wstring(); }

bool IsOneDriveTask(IRegisteredTask* task) {
    BSTR name = nullptr;
    task->get_Name(&name);
    const std::wstring taskName = BstrText(name);
    if (name) SysFreeString(name);
    if (ContainsInsensitive(taskName, L"OneDrive Startup Task")) return true;
    ComPtr<ITaskDefinition> definition;
    if (FAILED(task->get_Definition(definition.put()))) return false;
    ComPtr<IActionCollection> actions;
    if (FAILED(definition->get_Actions(actions.put()))) return false;
    LONG count = 0;
    actions->get_Count(&count);
    for (LONG index = 1; index <= count; ++index) {
        ComPtr<IAction> action;
        if (FAILED(actions->get_Item(index, action.put()))) continue;
        TASK_ACTION_TYPE actionType{};
        if (FAILED(action->get_Type(&actionType)) || actionType != TASK_ACTION_EXEC) continue;
        ComPtr<IExecAction> exec;
        if (FAILED(action->QueryInterface(__uuidof(IExecAction), reinterpret_cast<void**>(exec.put())))) continue;
        BSTR path = nullptr; BSTR arguments = nullptr;
        exec->get_Path(&path); exec->get_Arguments(&arguments);
        const std::wstring pathText = BstrText(path);
        const std::wstring argumentText = BstrText(arguments);
        if (path) SysFreeString(path); if (arguments) SysFreeString(arguments);
        if (ContainsInsensitive(pathText, L"OneDriveLauncher.exe") && ContainsInsensitive(argumentText, L"startInstances")) return true;
    }
    return false;
}

bool ConnectTaskScheduler(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& root) {
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(ITaskService), reinterpret_cast<void**>(service.put())))) return false;
    VARIANT empty{}; VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty))) return false;
    BSTR rootName = SysAllocString(L"\\");
    const HRESULT result = service->GetFolder(rootName, root.put());
    SysFreeString(rootName);
    return SUCCEEDED(result);
}

std::vector<TaskSnapshot> FindOneDriveTasks(bool* querySucceeded = nullptr) {
    if (querySucceeded) *querySucceeded = false;
    std::vector<TaskSnapshot> result;
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> root;
    if (!ConnectTaskScheduler(service, root)) return result;
    ComPtr<IRegisteredTaskCollection> tasks;
    if (FAILED(root->GetTasks(TASK_ENUM_HIDDEN, tasks.put()))) return result;
    LONG count = 0; tasks->get_Count(&count);
    for (LONG index = 1; index <= count; ++index) {
        VARIANT itemIndex{}; itemIndex.vt = VT_I4; itemIndex.lVal = index;
        ComPtr<IRegisteredTask> task;
        if (FAILED(tasks->get_Item(itemIndex, task.put())) || !IsOneDriveTask(task.get())) continue;
        BSTR path = nullptr; VARIANT_BOOL enabled = VARIANT_TRUE;
        task->get_Path(&path); task->get_Enabled(&enabled);
        result.push_back({BstrText(path), enabled != VARIANT_FALSE});
        if (path) SysFreeString(path);
    }
    if (querySucceeded) *querySucceeded = true;
    return result;
}

bool SetScheduledTaskEnabled(const std::wstring& path, bool enabled) {
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> root;
    if (!ConnectTaskScheduler(service, root)) return false;
    BSTR taskPath = SysAllocString(path.c_str());
    ComPtr<IRegisteredTask> task;
    const HRESULT result = root->GetTask(taskPath, task.put());
    SysFreeString(taskPath);
    return SUCCEEDED(result) && SUCCEEDED(task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE));
}

bool SetOneDriveTaskEnabled(const std::wstring& path, bool enabled) {
    return SetScheduledTaskEnabled(path, enabled);
}

bool SaveTaskBackups(const std::vector<TaskSnapshot>& tasks) {
    std::vector<std::wstring> values;
    for (const TaskSnapshot& task : tasks) values.push_back(std::wstring(task.enabled ? L"1|" : L"0|") + task.path);
    if (!WriteRegistryMultiSz(HKEY_CURRENT_USER, kRegistryKey, L"OneDriveTaskBackups", values)) return false;
    SaveDword(L"OneDriveTaskBackupValid", 1);
    return true;
}

std::vector<TaskSnapshot> LoadTaskBackups() {
    std::vector<TaskSnapshot> result; std::vector<std::wstring> values;
    if (!ReadRegistryMultiSz(HKEY_CURRENT_USER, kRegistryKey, L"OneDriveTaskBackups", values)) return result;
    for (const std::wstring& value : values) if (value.size() > 2 && value[1] == L'|') result.push_back({value.substr(2), value[0] == L'1'});
    return result;
}

bool ApplyOneDriveTasks(bool blocked) {
    const bool hasBackup = LoadDword(L"OneDriveTaskBackupValid", 0) != 0;
    if (blocked) {
        bool querySucceeded = false;
        const std::vector<TaskSnapshot> currentTasks = FindOneDriveTasks(&querySucceeded);
        if (!querySucceeded) return false;
        if (!hasBackup && !SaveTaskBackups(currentTasks)) return false;
        bool success = true;
        for (const TaskSnapshot& task : currentTasks) if (!SetOneDriveTaskEnabled(task.path, false)) success = false;
        return success;
    }
    if (!hasBackup) return true;
    bool success = true;
    for (const TaskSnapshot& task : LoadTaskBackups()) if (!SetOneDriveTaskEnabled(task.path, task.enabled)) success = false;
    if (success) { DeleteRegistryValue(HKEY_CURRENT_USER, kRegistryKey, L"OneDriveTaskBackups"); SaveDword(L"OneDriveTaskBackupValid", 0); }
    return success;
}

std::wstring StartupExecutableToken(const std::wstring& command);
bool StartupCommandContains(const std::wstring& command, std::initializer_list<const wchar_t*> needles);
bool IsProtectedStartupCommand(const std::wstring& command, const std::wstring& name = {});
void ClassifyStartupItem(StartupItem& item);
void ScanStartupRegistry(std::vector<StartupItem>& result);
void ScanStartupFolder(const std::wstring& folder, const wchar_t* source, bool requiresElevation,
                       std::vector<StartupItem>& result);

bool GetTaskStartupCommand(IRegisteredTask* task, std::wstring& command) {
    ComPtr<ITaskDefinition> definition;
    if (FAILED(task->get_Definition(definition.put()))) return false;
    ComPtr<ITriggerCollection> triggers;
    if (FAILED(definition->get_Triggers(triggers.put()))) return false;
    LONG triggerCount = 0;
    triggers->get_Count(&triggerCount);
    bool startupTrigger = false;
    for (LONG index = 1; index <= triggerCount; ++index) {
        ComPtr<ITrigger> trigger;
        if (FAILED(triggers->get_Item(index, trigger.put()))) continue;
        TASK_TRIGGER_TYPE2 type{};
        if (SUCCEEDED(trigger->get_Type(&type)) && (type == TASK_TRIGGER_LOGON || type == TASK_TRIGGER_BOOT)) {
            startupTrigger = true;
            break;
        }
    }
    if (!startupTrigger) return false;
    ComPtr<IActionCollection> actions;
    if (FAILED(definition->get_Actions(actions.put()))) return false;
    LONG actionCount = 0;
    actions->get_Count(&actionCount);
    for (LONG index = 1; index <= actionCount; ++index) {
        ComPtr<IAction> action;
        if (FAILED(actions->get_Item(index, action.put()))) continue;
        TASK_ACTION_TYPE actionType{};
        if (FAILED(action->get_Type(&actionType)) || actionType != TASK_ACTION_EXEC) continue;
        ComPtr<IExecAction> exec;
        if (FAILED(action->QueryInterface(__uuidof(IExecAction), reinterpret_cast<void**>(exec.put())))) continue;
        BSTR path = nullptr; BSTR arguments = nullptr;
        exec->get_Path(&path); exec->get_Arguments(&arguments);
        command = BstrText(path);
        if (arguments && *arguments) command += L" " + BstrText(arguments);
        if (path) SysFreeString(path);
        if (arguments) SysFreeString(arguments);
        return !command.empty();
    }
    return false;
}

void ScanStartupTaskFolder(ITaskFolder* folder, std::vector<StartupItem>& result, int depth = 0) {
    if (!folder || depth > 8) return;
    ComPtr<IRegisteredTaskCollection> tasks;
    if (FAILED(folder->GetTasks(TASK_ENUM_HIDDEN, tasks.put()))) return;
    LONG count = 0; tasks->get_Count(&count);
    for (LONG index = 1; index <= count; ++index) {
        VARIANT itemIndex{}; itemIndex.vt = VT_I4; itemIndex.lVal = index;
        ComPtr<IRegisteredTask> task;
        if (FAILED(tasks->get_Item(itemIndex, task.put()))) continue;
        std::wstring command;
        if (!GetTaskStartupCommand(task.get(), command)) continue;
        BSTR path = nullptr; BSTR name = nullptr; VARIANT_BOOL enabled = VARIANT_TRUE;
        task->get_Path(&path); task->get_Name(&name); task->get_Enabled(&enabled);
        StartupItem item;
        item.kind = ManagedStartupKind::ScheduledTask;
        item.taskPath = BstrText(path);
        item.id = L"TASK|" + item.taskPath;
        item.name = BstrText(name);
        item.source = T(L"Logon task", L"登录任务");
        item.location = item.taskPath;
        item.command = command;
        item.requiresElevation = true;
        item.enabled = enabled != VARIANT_FALSE && !HasStartupBackup(item);
        item.protectedItem = ContainsInsensitive(item.taskPath, L"\\Microsoft\\Windows\\") ||
                             ContainsInsensitive(item.name, L"Windows");
        ClassifyStartupItem(item);
        result.push_back(std::move(item));
        if (path) SysFreeString(path);
        if (name) SysFreeString(name);
    }
    ComPtr<ITaskFolderCollection> folders;
    if (FAILED(folder->GetFolders(TASK_ENUM_HIDDEN, folders.put()))) return;
    LONG folderCount = 0; folders->get_Count(&folderCount);
    for (LONG index = 1; index <= folderCount; ++index) {
        VARIANT itemIndex{}; itemIndex.vt = VT_I4; itemIndex.lVal = index;
        ComPtr<ITaskFolder> child;
        if (SUCCEEDED(folders->get_Item(itemIndex, child.put()))) ScanStartupTaskFolder(child.get(), result, depth + 1);
    }
}

void ScanStartupTasks(std::vector<StartupItem>& result) {
    ComPtr<ITaskService> service; ComPtr<ITaskFolder> root;
    if (ConnectTaskScheduler(service, root)) ScanStartupTaskFolder(root.get(), result);
}

bool IsProtectedService(const std::wstring& name, const std::wstring& command) {
    return IsProtectedStartupCommand(command, name) ||
           StartupCommandContains(name, {L"wuauserv", L"bits", L"WinDefend", L"WdNisSvc", L"SecurityHealth", L"RpcSs", L"DcomLaunch", L"PlugPlay", L"EventLog"});
}

void ScanStartupServices(std::vector<StartupItem>& result) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!manager) return;
    DWORD bytesNeeded = 0, serviceCount = 0, resume = 0;
    EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNeeded, &serviceCount, &resume, nullptr);
    if (!bytesNeeded) { CloseServiceHandle(manager); return; }
    std::vector<BYTE> buffer(bytesNeeded + 4096);
    if (!EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                               buffer.data(), static_cast<DWORD>(buffer.size()), &bytesNeeded,
                               &serviceCount, &resume, nullptr)) { CloseServiceHandle(manager); return; }
    auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    for (DWORD index = 0; index < serviceCount; ++index) {
        SC_HANDLE service = OpenServiceW(manager, services[index].lpServiceName, SERVICE_QUERY_CONFIG);
        if (!service) continue;
        DWORD needed = 0;
        QueryServiceConfigW(service, nullptr, 0, &needed);
        if (!needed) { CloseServiceHandle(service); continue; }
        std::vector<BYTE> configBuffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data());
        if (!QueryServiceConfigW(service, config, needed, &needed)) { CloseServiceHandle(service); continue; }
        StartupItem item;
        item.kind = ManagedStartupKind::Service;
        item.serviceName = services[index].lpServiceName;
        item.id = L"SERVICE|" + item.serviceName;
        item.name = config->lpDisplayName ? config->lpDisplayName : item.serviceName;
        item.source = T(L"Service", L"服务");
        item.location = item.serviceName;
        item.command = config->lpBinaryPathName ? config->lpBinaryPathName : L"";
        item.serviceStartType = config->dwStartType;
        item.requiresElevation = true;
        item.enabled = config->dwStartType == SERVICE_AUTO_START && !HasStartupBackup(item);
        item.protectedItem = IsProtectedService(item.serviceName, item.command);
        ClassifyStartupItem(item);
        if (config->dwStartType == SERVICE_AUTO_START || HasStartupBackup(item)) result.push_back(std::move(item));
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
}

std::vector<StartupItem> ScanStartupItems() {
    std::vector<StartupItem> result;
    ScanStartupRegistry(result);
    ScanStartupFolder(GetKnownFolder(FOLDERID_Startup), T(L"User Startup folder", L"用户启动文件夹"), false, result);
    ScanStartupFolder(GetKnownFolder(FOLDERID_CommonStartup), T(L"Common Startup folder", L"公共启动文件夹"), true, result);
    ScanStartupTasks(result);
    ScanStartupServices(result);
    MergeStartupBackupItems(result);
    for (size_t left = 0; left < result.size(); ++left) {
        const std::wstring leftExecutable = StartupExecutableToken(result[left].command);
        if (leftExecutable.empty()) continue;
        for (size_t right = 0; right < result.size(); ++right) {
            if (left == right) continue;
            const std::wstring rightExecutable = StartupExecutableToken(result[right].command);
            if (!rightExecutable.empty() && (leftExecutable == rightExecutable ||
                ContainsInsensitive(result[left].command, rightExecutable.c_str()))) {
                result[left].chainRisk = true;
                break;
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const StartupItem& left, const StartupItem& right) {
        if (left.protectedItem != right.protectedItem) return !left.protectedItem;
        if (left.highRisk != right.highRisk) return left.highRisk > right.highRisk;
        return Lower(left.name) < Lower(right.name);
    });
    return result;
}

bool ApplyStartupRegistryItem(const StartupItem& item, bool block) {
    const bool hasBackup = HasStartupBackup(item);
    if (block) {
        if (!hasBackup) {
            std::vector<BYTE> data; DWORD type = 0;
            if (!ReadRawRegistryValue(item.registryRoot, item.registrySubKey, item.registryValueName,
                                      item.registryView, data, type)) return true;
            if (!SaveStartupRawBackup(item, type, data) || !SaveStartupMetadata(item)) return false;
        }
        return DeleteRegistryValue(item.registryRoot, item.registrySubKey.c_str(), item.registryValueName.c_str());
    }
    if (!hasBackup) return true;
    std::vector<BYTE> current; DWORD currentType = 0;
    if (ReadRawRegistryValue(item.registryRoot, item.registrySubKey, item.registryValueName,
                             item.registryView, current, currentType)) {
        ClearStartupBackup(item);
        return true;
    }
    DWORD type = 0; std::vector<BYTE> data;
    if (!LoadStartupRawBackup(item, type, data)) return false;
    if (!WriteRawRegistryValue(item.registryRoot, item.registrySubKey, item.registryValueName,
                               item.registryView, type, data)) return false;
    ClearStartupBackup(item);
    return true;
}

bool ApplyStartupFolderItem(const StartupItem& item, bool block) {
    const bool hasBackup = HasStartupBackup(item);
    if (block) {
        if (hasBackup) return true;
        if (GetFileAttributesW(item.filePath.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
        const std::wstring local = GetKnownFolder(FOLDERID_LocalAppData);
        const std::wstring directory = local + L"\\Liberty\\StartupManager\\FolderBackup";
        CreateDirectoryW((local + L"\\Liberty").c_str(), nullptr);
        CreateDirectoryW((local + L"\\Liberty\\StartupManager").c_str(), nullptr);
        CreateDirectoryW(directory.c_str(), nullptr);
        const size_t dot = item.filePath.find_last_of(L'.');
        const std::wstring extension = dot == std::wstring::npos ? L".item" : item.filePath.substr(dot);
        const std::wstring backup = directory + L"\\" + StartupHash(item.id) + extension;
        if (!MoveFileExW(item.filePath.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
        if (!SaveStartupString(StartupBackupName(item, L"_FolderOriginal").c_str(), item.filePath) ||
            !SaveStartupString(StartupBackupName(item, L"_FolderBackup").c_str(), backup) ||
            !SaveStartupMetadata(item)) {
            MoveFileExW(backup.c_str(), item.filePath.c_str(), MOVEFILE_REPLACE_EXISTING);
            ClearStartupBackup(item);
            return false;
        }
        SaveStartupDword(StartupBackupName(item, L"_Valid").c_str(), 1);
        return true;
    }
    if (!hasBackup) return true;
    std::wstring original, backup;
    if (!LoadStartupString(StartupBackupName(item, L"_FolderOriginal").c_str(), original) ||
        !LoadStartupString(StartupBackupName(item, L"_FolderBackup").c_str(), backup)) return false;
    if (GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    if (GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (!MoveFileExW(backup.c_str(), original.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    ClearStartupBackup(item);
    return true;
}

bool ApplyStartupTaskItem(const StartupItem& item, bool block) {
    const bool hasBackup = HasStartupBackup(item);
    if (block) {
        if (!hasBackup) {
            if (!SaveStartupDword(StartupBackupName(item, L"_TaskEnabled").c_str(), item.enabled ? 1 : 0) || !SaveStartupMetadata(item)) return false;
            SaveStartupDword(StartupBackupName(item, L"_Valid").c_str(), 1);
        }
        return SetScheduledTaskEnabled(item.taskPath, false);
    }
    if (!hasBackup) return true;
    const bool enabled = LoadStartupDword(StartupBackupName(item, L"_TaskEnabled").c_str(), 1) != 0;
    if (!SetScheduledTaskEnabled(item.taskPath, enabled)) return false;
    ClearStartupBackup(item);
    return true;
}

bool ApplyStartupServiceItem(const StartupItem& item, bool block) {
    const bool hasBackup = HasStartupBackup(item);
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, item.serviceName.c_str(), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);
    if (!service) { CloseServiceHandle(manager); return false; }
    bool success = false;
    if (block) {
        if (!hasBackup) {
            if (!SaveStartupDword(StartupBackupName(item, L"_ServiceType").c_str(), item.serviceStartType) || !SaveStartupMetadata(item)) goto done;
            SaveStartupDword(StartupBackupName(item, L"_Valid").c_str(), 1);
        }
        success = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
    } else if (hasBackup) {
        const DWORD startType = LoadStartupDword(StartupBackupName(item, L"_ServiceType").c_str(), SERVICE_DEMAND_START);
        success = ChangeServiceConfigW(service, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
        if (success) ClearStartupBackup(item);
    } else success = true;
done:
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return success;
}

bool ApplyStartupItem(const StartupItem& item, bool block) {
    if (item.protectedItem) return false;
    switch (item.kind) {
    case ManagedStartupKind::Registry: return ApplyStartupRegistryItem(item, block);
    case ManagedStartupKind::StartupFolder: return ApplyStartupFolderItem(item, block);
    case ManagedStartupKind::ScheduledTask: return ApplyStartupTaskItem(item, block);
    case ManagedStartupKind::Service: return ApplyStartupServiceItem(item, block);
    }
    return false;
}

bool ApplyStartupItems(const std::vector<StartupItem>& items, bool block, size_t& changed) {
    changed = 0;
    bool success = true;
    for (const StartupItem& item : items) {
        if (item.protectedItem) { success = false; continue; }
        if (ApplyStartupItem(item, block)) ++changed;
        else success = false;
    }
    return success;
}

struct StartupPlanEntry {
    bool block = true;
    std::wstring id;
};

bool WriteStartupPlan(const std::wstring& path, const std::vector<StartupPlanEntry>& entries) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::wstring content;
    for (const StartupPlanEntry& entry : entries) content += (entry.block ? L"B\t" : L"R\t") + entry.id + L"\r\n";
    DWORD written = 0;
    const bool success = WriteFile(file, content.data(), static_cast<DWORD>(content.size() * sizeof(wchar_t)), &written, nullptr) != FALSE;
    CloseHandle(file);
    return success;
}

std::vector<StartupPlanEntry> ReadStartupPlan(const std::wstring& path) {
    std::vector<StartupPlanEntry> result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return result;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 2 * 1024 * 1024) {
        CloseHandle(file); return result;
    }
    std::vector<wchar_t> buffer(static_cast<size_t>(size.QuadPart / sizeof(wchar_t)) + 2, L'\0');
    DWORD read = 0;
    ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(file);
    std::wstring content(buffer.data(), read / sizeof(wchar_t));
    size_t start = 0;
    while (start < content.size()) {
        const size_t end = content.find_first_of(L"\r\n", start);
        const std::wstring line = content.substr(start, end == std::wstring::npos ? end : end - start);
        if (line.size() > 2 && line[1] == L'\t' && (line[0] == L'B' || line[0] == L'R'))
            result.push_back({line[0] == L'B', line.substr(2)});
        if (end == std::wstring::npos) break;
        start = end + 1;
        while (start < content.size() && (content[start] == L'\r' || content[start] == L'\n')) ++start;
    }
    return result;
}

bool RunStartupPlan(const std::wstring& path, std::wstring& message) {
    const std::vector<StartupPlanEntry> plan = ReadStartupPlan(path);
    if (plan.empty()) { message = T(L"The startup plan was empty.", L"启动项计划为空。"); return false; }
    const std::vector<StartupItem> items = ScanStartupItems();
    size_t changed = 0;
    bool success = true;
    for (const StartupPlanEntry& entry : plan) {
        auto match = std::find_if(items.begin(), items.end(), [&entry](const StartupItem& item) { return item.id == entry.id; });
        if (match == items.end() || !ApplyStartupItem(*match, entry.block)) { success = false; continue; }
        ++changed;
    }
    message = std::to_wstring(changed) + T(L" startup item(s) updated.", L" 个启动项已更新。");
    return success;
}

bool RunElevatedStartupPlan(const std::vector<StartupPlanEntry>& entries) {
    const std::wstring local = GetKnownFolder(FOLDERID_LocalAppData);
    const std::wstring path = local + L"\\LibertyStartup-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    if (!WriteStartupPlan(path, entries)) return false;
    const std::wstring executable = GetModulePath();
    const std::wstring parameters = L"--startup-apply \"" + path + L"\"";
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = executable.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;
    const bool launched = ShellExecuteExW(&execute) != FALSE;
    if (!launched) { DeleteFileW(path.c_str()); return false; }
    WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(execute.hProcess, &exitCode);
    CloseHandle(execute.hProcess); DeleteFileW(path.c_str());
    return exitCode == 0;
}

bool IsCurrentUserProcess(HANDLE process) {
    HANDLE currentToken = nullptr, processToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken) || !OpenProcessToken(process, TOKEN_QUERY, &processToken)) {
        if (currentToken) CloseHandle(currentToken); if (processToken) CloseHandle(processToken); return false;
    }
    DWORD currentSize = 0, processSize = 0;
    GetTokenInformation(currentToken, TokenUser, nullptr, 0, &currentSize);
    GetTokenInformation(processToken, TokenUser, nullptr, 0, &processSize);
    std::vector<BYTE> currentBuffer(currentSize), processBuffer(processSize);
    const bool readCurrent = GetTokenInformation(currentToken, TokenUser, currentBuffer.data(), currentSize, &currentSize) != FALSE;
    const bool readProcess = GetTokenInformation(processToken, TokenUser, processBuffer.data(), processSize, &processSize) != FALSE;
    bool equal = false;
    if (readCurrent && readProcess) equal = EqualSid(reinterpret_cast<TOKEN_USER*>(currentBuffer.data())->User.Sid,
                                                     reinterpret_cast<TOKEN_USER*>(processBuffer.data())->User.Sid) != FALSE;
    CloseHandle(currentToken); CloseHandle(processToken); return equal;
}

std::wstring ProcessPath(HANDLE process) {
    std::vector<wchar_t> buffer(MAX_PATH); DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) return {};
    return std::wstring(buffer.data(), size);
}

struct ProcessWindowSearch { DWORD processId = 0; bool found = false; };

BOOL CALLBACK CloseProcessWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<ProcessWindowSearch*>(parameter);
    DWORD processId = 0; GetWindowThreadProcessId(window, &processId);
    if (processId == search->processId && IsWindowVisible(window)) { PostMessageW(window, WM_CLOSE, 0, 0); search->found = true; }
    return TRUE;
}

void StopRunningOneDrive() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); return; }
    do {
        if (_wcsicmp(entry.szExeFile, L"OneDrive.exe") != 0) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!process) continue;
        const std::wstring path = ProcessPath(process);
        if (!IsCurrentUserProcess(process) || !ContainsInsensitive(path, L"onedrive")) { CloseHandle(process); continue; }
        ProcessWindowSearch search{entry.th32ProcessID, false}; EnumWindows(CloseProcessWindow, reinterpret_cast<LPARAM>(&search));
        if (search.found) WaitForSingleObject(process, 2000);
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) TerminateProcess(process, 0);
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
}

bool OneDrivePolicyForcesAutoStart() {
    DWORD value = 0;
    return (ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Policies\\Microsoft\\OneDrive", L"EnableAutoStart", value) && value != 0) ||
           (ReadRegistryDword(HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\OneDrive", L"EnableAutoStart", value) && value != 0);
}

bool ApplyOneDriveBlock(bool blocked, bool stopRunning) {
    if (blocked && OneDrivePolicyForcesAutoStart()) return false;
    bool success = ApplyRunEntryBlock(kOneDriveEntry, blocked);
    success = ApplyBinaryEntryBlock(kStartupApprovedRunKey, L"OneDrive", L"OneDriveStartupApprovedBackup", L"OneDriveStartupApprovedValid", blocked) && success;
    success = ApplyOneDriveStartupLink(blocked) && success;
    success = ApplyOneDriveTasks(blocked) && success;
    // Do not leave a half-disabled startup chain behind when one component
    // rejects the change (for example, Task Scheduler access is denied).
    if (!success && blocked) {
        ApplyOneDriveBlock(false, false);
        return false;
    }
    if (blocked && stopRunning) StopRunningOneDrive();
    return success;
}

std::wstring StartupRegistryViewLabel(REGSAM view) {
    if (view == KEY_WOW64_32KEY) return L"32-bit";
    if (view == KEY_WOW64_64KEY) return L"64-bit";
    return L"native";
}

std::wstring StartupRegistryRootLabel(HKEY root) {
    return root == HKEY_LOCAL_MACHINE ? L"HKLM" : L"HKCU";
}

std::wstring RegistryDataText(DWORD type, const std::vector<BYTE>& data) {
    if (data.empty()) return {};
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ) return L"(non-text registry value)";
    const size_t count = data.size() / sizeof(wchar_t);
    const wchar_t* text = reinterpret_cast<const wchar_t*>(data.data());
    if (type == REG_MULTI_SZ) {
        std::wstring result;
        for (size_t offset = 0; offset < count && text[offset]; ) {
            if (!result.empty()) result += L" | ";
            result += text + offset;
            offset += wcslen(text + offset) + 1;
        }
        return result;
    }
    return std::wstring(text, wcsnlen_s(text, count));
}

std::wstring ExpandStartupCommand(const std::wstring& command) {
    if (command.empty()) return {};
    DWORD needed = ExpandEnvironmentStringsW(command.c_str(), nullptr, 0);
    if (!needed) return command;
    std::vector<wchar_t> buffer(needed + 1, L'\0');
    ExpandEnvironmentStringsW(command.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    return buffer.data();
}

std::wstring StartupExecutableToken(const std::wstring& command) {
    std::wstring text = command;
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    if (text.empty()) return {};
    std::wstring token;
    if (text.front() == L'"') {
        const size_t end = text.find(L'"', 1);
        token = text.substr(1, end == std::wstring::npos ? end : end - 1);
    } else {
        const size_t end = text.find_first_of(L" \t");
        token = text.substr(0, end);
    }
    const size_t slash = token.find_last_of(L"\\/");
    return Lower(slash == std::wstring::npos ? token : token.substr(slash + 1));
}

bool StartupCommandContains(const std::wstring& command, std::initializer_list<const wchar_t*> needles) {
    for (const wchar_t* needle : needles) if (ContainsInsensitive(command, needle)) return true;
    return false;
}

bool IsProtectedStartupCommand(const std::wstring& command, const std::wstring& name) {
    const std::wstring expanded = Lower(ExpandStartupCommand(command));
    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory));
    const std::wstring windowsPath = Lower(windowsDirectory);
    if (!windowsPath.empty() && (expanded.rfind(windowsPath + L"\\", 0) == 0 || expanded.rfind(L"\"" + windowsPath + L"\\", 0) == 0)) return true;
    return StartupCommandContains(expanded, {L"securityhealthsystray", L"msmpeng.exe", L"windefend", L"lsass.exe", L"services.exe", L"svchost.exe", L"explorer.exe", L"winlogon.exe"}) ||
           StartupCommandContains(name, {L"Windows Defender", L"Windows Security", L"Microsoft Defender"});
}

void ClassifyStartupItem(StartupItem& item) {
    const std::wstring combined = Lower(item.name + L" " + item.command + L" " + item.location);
    item.thirdParty = StartupCommandContains(combined, {
        L"腾讯", L"qq", L"wechat", L"微信", L"钉钉", L"企业微信", L"百度", L"爱奇艺", L"迅雷", L"360", L"搜狗", L"wps", L"金山", L"鲁大师", L"驱动精灵", L"字节", L"抖音", L"快手", L"网易", L"阿里", L"小米", L"华为", L"联想", L"oppo", L"vivo"
    });
    item.chainRisk = StartupCommandContains(combined, {
        L"launcher", L"updater", L"update.exe", L"bootstrap", L"helper", L"startinstances", L"--background", L"/background", L"--silent", L"/silent", L"schtasks", L"rundll32", L"regsvr32", L"mshta", L"powershell", L"pwsh", L"wscript", L"cscript", L"cmd.exe /c", L".bat", L".cmd", L".vbs", L".js"
    });
    item.highRisk = StartupCommandContains(combined, {
        L"\\appdata\\local\\temp\\", L"\\downloads\\", L"javascript:", L"-enc ", L"-encodedcommand", L"base64", L"rundll32", L"regsvr32", L"mshta"
    });
    item.protectedItem = item.protectedItem || IsProtectedStartupCommand(item.command, item.name);
}

std::wstring MakeRegistryStartupId(HKEY root, REGSAM view, const std::wstring& subKey, const std::wstring& valueName) {
    return L"REG|" + StartupRegistryRootLabel(root) + L"|" + StartupRegistryViewLabel(view) + L"|" + subKey + L"|" + valueName;
}

void ScanStartupRegistryTree(HKEY root, const std::wstring& subKey, const wchar_t* source,
                             REGSAM view, std::vector<StartupItem>& result, int depth = 0) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) return;
    DWORD maxValueName = 0, maxValueData = 0, subKeyCount = 0, maxSubKey = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeyCount, &maxSubKey, nullptr,
                     nullptr, &maxValueName, &maxValueData, nullptr, nullptr);
    std::vector<wchar_t> valueName(maxValueName + 2, L'\0');
    std::vector<BYTE> valueData(maxValueData + 2, 0);
    for (DWORD index = 0; index < maxValueName + subKeyCount + 128; ++index) {
        DWORD nameLength = static_cast<DWORD>(valueName.size() - 1);
        DWORD dataSize = static_cast<DWORD>(valueData.size());
        DWORD type = 0;
        LONG status = RegEnumValueW(key, index, valueName.data(), &nameLength, nullptr, &type,
                                    valueData.data(), &dataSize);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_MORE_DATA) {
            valueName.resize(valueName.size() * 2 + 2, L'\0');
            valueData.resize(valueData.size() * 2 + 2, 0);
            --index;
            continue;
        }
        if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)) continue;
        StartupItem item;
        item.kind = ManagedStartupKind::Registry;
        item.id = MakeRegistryStartupId(root, view, subKey, std::wstring(valueName.data(), nameLength));
        item.name = std::wstring(valueName.data(), nameLength);
        item.source = std::wstring(StartupRegistryRootLabel(root) + L" " + source + L" (" + StartupRegistryViewLabel(view) + L")");
        item.registryRoot = root;
        item.registryView = view;
        item.registrySubKey = subKey;
        item.registryValueName = item.name;
        item.location = subKey + L"\\" + item.name;
        item.command = RegistryDataText(type, valueData);
        item.requiresElevation = root == HKEY_LOCAL_MACHINE;
        item.enabled = !HasStartupBackup(item);
        ClassifyStartupItem(item);
        result.push_back(std::move(item));
    }
    if (depth < 3) {
        std::vector<wchar_t> childName(maxSubKey + 2, L'\0');
        for (DWORD index = 0; index < subKeyCount; ++index) {
            DWORD childLength = static_cast<DWORD>(childName.size() - 1);
            if (RegEnumKeyExW(key, index, childName.data(), &childLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
                ScanStartupRegistryTree(root, subKey + L"\\" + std::wstring(childName.data(), childLength), source, view, result, depth + 1);
        }
    }
    RegCloseKey(key);
}

void ScanStartupRegistry(std::vector<StartupItem>& result) {
    struct RegistrySource { HKEY root; const wchar_t* key; const wchar_t* label; };
    const RegistrySource sources[] = {
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Run"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"RunOnce"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx", L"RunOnceEx"},
        {HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"Windows run/load"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Run"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"RunOnce"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx", L"RunOnceEx"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"Windows run/load"}
    };
    const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (const RegistrySource& source : sources)
        for (REGSAM view : views) ScanStartupRegistryTree(source.root, source.key, source.label, view, result);
}

void ScanStartupFolder(const std::wstring& folder, const wchar_t* source, bool requiresElevation,
                       std::vector<StartupItem>& result) {
    if (folder.empty()) return;
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = data.cFileName;
        if (!ContainsInsensitive(name, L".lnk") && !ContainsInsensitive(name, L".url") &&
            !ContainsInsensitive(name, L".exe") && !ContainsInsensitive(name, L".bat") &&
            !ContainsInsensitive(name, L".cmd")) continue;
        StartupItem item;
        item.kind = ManagedStartupKind::StartupFolder;
        item.filePath = folder + L"\\" + name;
        item.id = L"FOLDER|" + item.filePath;
        item.name = name;
        item.source = T(source, source);
        item.location = item.filePath;
        item.command = item.filePath;
        item.requiresElevation = requiresElevation;
        item.enabled = !HasStartupBackup(item);
        ClassifyStartupItem(item);
        result.push_back(std::move(item));
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

bool IsSecurityCenterHidden() {
    DWORD value = 0;
    return ReadRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey, kSecuritySystrayPolicyValue, value) && value != 0;
}

bool SetSecurityCenterHiddenDirect(bool hidden) {
    const bool hasBackup = LoadDword(kSecuritySystrayBackupValid, 0) != 0;
    if (hidden) {
        if (!hasBackup) {
            DWORD current = 0;
            if (ReadRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey, kSecuritySystrayPolicyValue, current)) {
                SaveDword(kSecuritySystrayBackupValue, current); SaveDword(kSecuritySystrayBackupValid, 1);
            } else SaveDword(kSecuritySystrayBackupValid, 0);
        }
        return WriteRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey, kSecuritySystrayPolicyValue, 1);
    }
    bool success = false;
    if (hasBackup) success = WriteRegistryDword(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey, kSecuritySystrayPolicyValue, LoadDword(kSecuritySystrayBackupValue, 0));
    else success = DeleteRegistryValue(HKEY_LOCAL_MACHINE, kSecuritySystrayPolicyKey, kSecuritySystrayPolicyValue);
    if (success) SaveDword(kSecuritySystrayBackupValid, 0);
    return success;
}

bool RunElevatedSecurityPolicy(bool hidden) {
    const std::wstring executable = GetModulePath();
    if (executable.empty()) return false;
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS; execute.lpVerb = L"runas"; execute.lpFile = executable.c_str();
    execute.lpParameters = hidden ? L"--apply-security-systray 1" : L"--apply-security-systray 0"; execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) return false;
    WaitForSingleObject(execute.hProcess, INFINITE); DWORD exitCode = 1; GetExitCodeProcess(execute.hProcess, &exitCode); CloseHandle(execute.hProcess);
    return exitCode == 0;
}

bool SetSecurityCenterHidden(bool hidden) {
    if (!SetSecurityCenterHiddenDirect(hidden) && !RunElevatedSecurityPolicy(hidden)) return false;
    const wchar_t settingName[] = L"Windows Defender Security Center";
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(settingName), SMTO_ABORTIFHUNG, 1000, nullptr);
    return IsSecurityCenterHidden() == hidden;
}

class DialogTemplateBuilder {
public:
    void Begin(DWORD style, DWORD extendedStyle, WORD controlCount, short x, short y, short width, short height, const wchar_t* title) {
        DLGTEMPLATE dialog{}; dialog.style = style; dialog.dwExtendedStyle = extendedStyle; dialog.cdit = controlCount;
        dialog.x = x; dialog.y = y; dialog.cx = width; dialog.cy = height; Append(dialog);
        AppendWord(0); AppendWord(0); AppendString(title); AppendWord(10); AppendString(L"Segoe UI Variable Text");
    }
    void AddItem(DWORD style, DWORD extendedStyle, short x, short y, short width, short height, WORD id, WORD classAtom, const wchar_t* title) {
        AlignDword(); DLGITEMTEMPLATE item{}; item.style = style; item.dwExtendedStyle = extendedStyle; item.x = x; item.y = y; item.cx = width; item.cy = height; item.id = id; Append(item);
        AppendWord(0xFFFF); AppendWord(classAtom); AppendString(title); AppendWord(0);
    }
    const DLGTEMPLATE* Data() const { return reinterpret_cast<const DLGTEMPLATE*>(bytes.data()); }
private:
    template <typename T> void Append(const T& value) { const BYTE* begin = reinterpret_cast<const BYTE*>(&value); bytes.insert(bytes.end(), begin, begin + sizeof(T)); }
    void AppendWord(WORD value) { Append(value); }
    void AppendString(const wchar_t* value) { if (!value) { AppendWord(0); return; } while (*value) AppendWord(static_cast<WORD>(*value++)); AppendWord(0); }
    void AlignDword() { bytes.resize((bytes.size() + 3u) & ~3u, 0); }
    std::vector<BYTE> bytes;
};

INT_PTR CALLBACK MinutesDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetDarkMode(dialog, IsDarkTheme()); SetDlgItemTextW(dialog, 1001, L"45"); SetFocus(GetDlgItem(dialog, 1001)); SendDlgItemMessageW(dialog, 1001, EM_SETSEL, 0, -1); return FALSE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wParam) == IDOK) {
            BOOL valid = FALSE; const UINT minutes = GetDlgItemInt(dialog, 1001, &valid, FALSE);
            if (!valid || minutes < 1 || minutes > 10080) { MessageBoxW(dialog, T(L"Enter 1–10080 minutes.", L"请输入 1–10080 分钟。"), kAppName, MB_OK | MB_ICONWARNING); SetFocus(GetDlgItem(dialog, 1001)); return TRUE; }
            EndDialog(dialog, static_cast<INT_PTR>(minutes)); return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, 0); return TRUE; }
    }
    return FALSE;
}

INT_PTR ShowMinutesDialog(HWND parent) {
    DialogTemplateBuilder builder;
    builder.Begin(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT, 0, 4, 10, 10, 230, 86, T(L"Timed shutdown", L"定时关机"));
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 11, 104, 12, 1000, 0x0082, T(L"Minutes (1–10080):", L"分钟（1–10080）："));
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 0, 116, 8, 94, 16, 1001, 0x0081, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 82, 50, 58, 17, IDOK, 0x0080, T(L"OK", L"确定"));
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 148, 50, 58, 17, IDCANCEL, 0x0080, T(L"Cancel", L"取消"));
    return DialogBoxIndirectParamW(g_instance, builder.Data(), parent, MinutesDialogProc, 0);
}

INT_PTR CALLBACK MappingDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetDarkMode(dialog, IsDarkTheme());
        const WORD controls[] = {ID_MAPPING_COMMAND, ID_MAPPING_OPTION, ID_MAPPING_CONTROL};
        for (WORD control : controls) for (const ModifierChoice& choice : kModifierChoices)
            SendMessageW(GetDlgItem(dialog, control), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(T(choice.english, choice.chinese)));
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_COMMAND), CB_SETCURSEL, ModifierChoiceIndex(g_commandKey), 0);
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_OPTION), CB_SETCURSEL, ModifierChoiceIndex(g_optionKey), 0);
        SendMessageW(GetDlgItem(dialog, ID_MAPPING_CONTROL), CB_SETCURSEL, ModifierChoiceIndex(g_controlKey), 0);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        const int commandIndex = static_cast<int>(SendMessageW(GetDlgItem(dialog, ID_MAPPING_COMMAND), CB_GETCURSEL, 0, 0));
        const int optionIndex = static_cast<int>(SendMessageW(GetDlgItem(dialog, ID_MAPPING_OPTION), CB_GETCURSEL, 0, 0));
        const int controlIndex = static_cast<int>(SendMessageW(GetDlgItem(dialog, ID_MAPPING_CONTROL), CB_GETCURSEL, 0, 0));
        if (commandIndex < 0 || optionIndex < 0 || controlIndex < 0) return TRUE;
        const WORD commandKey = kModifierChoices[commandIndex].virtualKey, optionKey = kModifierChoices[optionIndex].virtualKey, controlKey = kModifierChoices[controlIndex].virtualKey;
        if (commandKey == optionKey || commandKey == controlKey || optionKey == controlKey) { MessageBoxW(dialog, T(L"Cmd, Option, and Control must use different physical keys.", L"Cmd、Option 和 Control 必须使用不同的物理按键。"), kAppName, MB_OK | MB_ICONWARNING); return TRUE; }
        ResetMappedModifierState(); g_commandKey = commandKey; g_optionKey = optionKey; g_controlKey = controlKey;
        SaveDword(L"CommandKey", g_commandKey); SaveDword(L"OptionKey", g_optionKey); SaveDword(L"ControlKey", g_controlKey); EndDialog(dialog, IDOK); return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}

INT_PTR ShowMappingDialog(HWND parent) {
    DialogTemplateBuilder builder;
    builder.Begin(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT, 0, 8, 10, 10, 280, 124, T(L"Keyboard mappings", L"快捷键映射"));
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 8, 264, 12, ID_MAPPING_HINT, 0x0082, T(L"Choose the physical key for each macOS modifier:", L"选择每个 macOS 修饰键对应的物理按键："));
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 29, 80, 12, 1201, 0x0082, L"Cmd:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 96, 26, 170, 80, ID_MAPPING_COMMAND, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 52, 80, 12, 1202, 0x0082, L"Option:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 96, 49, 170, 80, ID_MAPPING_OPTION, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE, 0, 8, 75, 80, 12, 1203, 0x0082, L"Control:");
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 96, 72, 170, 80, ID_MAPPING_CONTROL, 0x0085, nullptr);
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 124, 99, 62, 17, IDOK, 0x0080, T(L"OK", L"确定"));
    builder.AddItem(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 196, 99, 62, 17, IDCANCEL, 0x0080, T(L"Cancel", L"取消"));
    return DialogBoxIndirectParamW(g_instance, builder.Data(), parent, MappingDialogProc, 0);
}

void SaveOverlaySettings() {
    if (g_overlay.path.empty()) return;
    SaveStringSetting(L"OverlayPath", g_overlay.path); SaveDword(L"OverlayOpacity", g_overlay.opacity); SaveDword(L"OverlayLocked", g_overlay.locked ? 1 : 0); SaveDword(L"OverlayClickThrough", g_overlay.clickThrough ? 1 : 0); SaveDword(L"OverlayScalePercent", static_cast<DWORD>(std::round(g_overlay.scale * 100.0f)));
    if (g_overlayWindow) { RECT rect{}; GetWindowRect(g_overlayWindow, &rect); SaveDword(L"OverlayX", static_cast<DWORD>(rect.left)); SaveDword(L"OverlayY", static_cast<DWORD>(rect.top)); SaveDword(L"OverlayWidth", static_cast<DWORD>(rect.right - rect.left)); SaveDword(L"OverlayHeight", static_cast<DWORD>(rect.bottom - rect.top)); }
}

bool DecodeImage(const std::wstring& path, std::vector<BYTE>& pixels, UINT& width, UINT& height) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())))) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()))) return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) return false;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 || width > 12000 || height > 12000) return false;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put()))) return false;
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
}

bool RenderOverlay() {
    if (!g_overlayWindow || g_overlay.pixels.empty() || !g_overlay.sourceWidth || !g_overlay.sourceHeight) return false;
    const UINT width = std::max<UINT>(80, static_cast<UINT>(std::round(g_overlay.sourceWidth * g_overlay.scale)));
    const UINT height = std::max<UINT>(80, static_cast<UINT>(std::round(g_overlay.sourceHeight * g_overlay.scale)));
    BITMAPINFO bitmapInfo{}; bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width); bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height); bitmapInfo.bmiHeader.biPlanes = 1; bitmapInfo.bmiHeader.biBitCount = 32; bitmapInfo.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr); HDC memory = CreateCompatibleDC(screen); void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!screen || !memory || !bitmap || !bits) { if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); if (screen) ReleaseDC(nullptr, screen); return false; }
    auto* destination = reinterpret_cast<BYTE*>(bits);
    for (UINT y = 0; y < height; ++y) {
        const UINT sourceY = std::min(g_overlay.sourceHeight - 1, static_cast<UINT>(static_cast<double>(y) * g_overlay.sourceHeight / height));
        for (UINT x = 0; x < width; ++x) {
            const UINT sourceX = std::min(g_overlay.sourceWidth - 1, static_cast<UINT>(static_cast<double>(x) * g_overlay.sourceWidth / width));
            const BYTE* source = &g_overlay.pixels[(static_cast<size_t>(sourceY) * g_overlay.sourceWidth + sourceX) * 4];
            BYTE* pixel = &destination[(static_cast<size_t>(y) * width + x) * 4];
            pixel[0] = static_cast<BYTE>(source[0] * g_overlay.opacity / 255); pixel[1] = static_cast<BYTE>(source[1] * g_overlay.opacity / 255); pixel[2] = static_cast<BYTE>(source[2] * g_overlay.opacity / 255); pixel[3] = static_cast<BYTE>(source[3] * g_overlay.opacity / 255);
        }
    }
    HBITMAP previous = static_cast<HBITMAP>(SelectObject(memory, bitmap)); RECT rect{}; GetWindowRect(g_overlayWindow, &rect); SIZE size{static_cast<LONG>(width), static_cast<LONG>(height)}; POINT position{rect.left, rect.top}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL updated = UpdateLayeredWindow(g_overlayWindow, screen, &position, &size, memory, nullptr, 0, &blend, ULW_ALPHA);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    return updated != FALSE;
}

void SetOverlayClickThrough(bool enabled) {
    g_overlay.clickThrough = enabled;
    if (g_overlayWindow) { LONG_PTR style = GetWindowLongPtrW(g_overlayWindow, GWL_EXSTYLE); if (enabled) style |= WS_EX_TRANSPARENT; else style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT); SetWindowLongPtrW(g_overlayWindow, GWL_EXSTYLE, style); SetWindowPos(g_overlayWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED); }
    SaveOverlaySettings();
}

void SetOverlayOpacity(BYTE opacity) {
    g_overlay.opacity = opacity;
    RenderOverlay();
    SaveOverlaySettings();
}

void CloseOverlay() {
    if (g_overlayWindow) DestroyWindow(g_overlayWindow);
    g_overlayWindow = nullptr;
}

void ShowOverlayContextMenu(HWND owner, POINT point) {
    HMENU menu = CreatePopupMenu(); if (!menu) return;
    AppendMenuW(menu, MF_STRING | (g_overlay.locked ? MF_CHECKED : 0), ID_OVERLAY_LOCK, T(L"Lock position", L"锁定位置"));
    AppendMenuW(menu, MF_STRING | (g_overlay.clickThrough ? MF_CHECKED : 0), ID_OVERLAY_CLICKTHROUGH, T(L"Mouse passthrough", L"鼠标穿透"));
    HMENU opacityMenu = CreatePopupMenu();
    if (opacityMenu) {
        AppendMenuW(opacityMenu, MF_STRING | (g_overlay.opacity == 64 ? MF_CHECKED : 0), ID_OVERLAY_OPACITY_25, T(L"25%", L"25%"));
        AppendMenuW(opacityMenu, MF_STRING | (g_overlay.opacity == 128 ? MF_CHECKED : 0), ID_OVERLAY_OPACITY_50, T(L"50%", L"50%"));
        AppendMenuW(opacityMenu, MF_STRING | (g_overlay.opacity == 192 ? MF_CHECKED : 0), ID_OVERLAY_OPACITY_75, T(L"75%", L"75%"));
        AppendMenuW(opacityMenu, MF_STRING | (g_overlay.opacity == 255 ? MF_CHECKED : 0), ID_OVERLAY_OPACITY_100, T(L"100%", L"100%"));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opacityMenu), T(L"Opacity", L"透明度"));
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_OVERLAY_CLOSE, T(L"Close image", L"关闭图片"));
    SetForegroundWindow(owner); const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr); DestroyMenu(menu);
    if (command == ID_OVERLAY_LOCK) { g_overlay.locked = !g_overlay.locked; SaveOverlaySettings(); }
    else if (command == ID_OVERLAY_CLICKTHROUGH) SetOverlayClickThrough(!g_overlay.clickThrough);
    else if (command == ID_OVERLAY_OPACITY_25) SetOverlayOpacity(64);
    else if (command == ID_OVERLAY_OPACITY_50) SetOverlayOpacity(128);
    else if (command == ID_OVERLAY_OPACITY_75) SetOverlayOpacity(192);
    else if (command == ID_OVERLAY_OPACITY_100) SetOverlayOpacity(255);
    else if (command == ID_OVERLAY_CLOSE) CloseOverlay();
}

LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: return g_overlay.clickThrough ? HTTRANSPARENT : HTCLIENT;
    case WM_LBUTTONDOWN:
        if (!g_overlay.locked && !g_overlay.clickThrough) { SetCapture(window); g_overlay.dragging = true; g_overlay.dragStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; RECT rect{}; GetWindowRect(window, &rect); g_overlay.windowStart = {rect.left, rect.top}; }
        return 0;
    case WM_MOUSEMOVE:
        if (g_overlay.dragging && GetCapture() == window) { POINT cursor{}; GetCursorPos(&cursor); SetWindowPos(window, HWND_TOPMOST, g_overlay.windowStart.x + cursor.x - g_overlay.dragStart.x, g_overlay.windowStart.y + cursor.y - g_overlay.dragStart.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE); }
        return 0;
    case WM_LBUTTONUP:
        if (g_overlay.dragging) { g_overlay.dragging = false; ReleaseCapture(); SaveOverlaySettings(); }
        return 0;
    case WM_MOUSEWHEEL: {
        const short delta = GET_WHEEL_DELTA_WPARAM(wParam); g_overlay.scale = std::clamp(g_overlay.scale * (delta > 0 ? 1.1f : 0.9f), 0.1f, 5.0f); RenderOverlay(); SaveOverlaySettings(); return 0;
    }
    case WM_DISPLAYCHANGE: RenderOverlay(); return 0;
    case WM_RBUTTONUP: { POINT point{}; GetCursorPos(&point); ShowOverlayContextMenu(g_window, point); return 0; }
    case WM_KEYDOWN: if (wParam == VK_ESCAPE) CloseOverlay(); return 0;
    case WM_DESTROY: SaveOverlaySettings(); if (g_overlayWindow == window) g_overlayWindow = nullptr; return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ShowOverlayFromPath(const std::wstring& path, bool restore) {
    std::vector<BYTE> pixels; UINT width = 0, height = 0;
    if (!DecodeImage(path, pixels, width, height)) { MessageBoxW(g_window, T(L"Liberty could not decode this image.", L"Liberty 无法读取这张图片。"), kAppName, MB_OK | MB_ICONERROR); return false; }
    g_overlay.path = path; g_overlay.pixels = std::move(pixels); g_overlay.sourceWidth = width; g_overlay.sourceHeight = height; const float fitScale = (std::min)(1.0f, (std::min)(720.0f / static_cast<float>(width), 520.0f / static_cast<float>(height))); g_overlay.scale = restore ? LoadDword(L"OverlayScalePercent", static_cast<DWORD>(std::round(fitScale * 100.0f))) / 100.0f : fitScale; g_overlay.scale = std::clamp(g_overlay.scale, 0.1f, 5.0f); g_overlay.opacity = static_cast<BYTE>(std::clamp<DWORD>(LoadDword(L"OverlayOpacity", 235), 20, 255)); g_overlay.locked = restore && LoadDword(L"OverlayLocked", 0) != 0; g_overlay.clickThrough = restore && LoadDword(L"OverlayClickThrough", 0) != 0;
    RECT workArea{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0); DWORD savedWidth = restore ? LoadDword(L"OverlayWidth", 0) : 0, savedHeight = restore ? LoadDword(L"OverlayHeight", 0) : 0; const int targetWidth = savedWidth ? static_cast<int>(savedWidth) : (std::max)(80, static_cast<int>(std::round(width * g_overlay.scale))); const int targetHeight = savedHeight ? static_cast<int>(savedHeight) : (std::max)(80, static_cast<int>(std::round(height * g_overlay.scale))); const int defaultX = workArea.left + (workArea.right - workArea.left - targetWidth) / 2, defaultY = workArea.top + (workArea.bottom - workArea.top - targetHeight) / 2; const int x = restore ? static_cast<int>(LoadDword(L"OverlayX", defaultX)) : defaultX, y = restore ? static_cast<int>(LoadDword(L"OverlayY", defaultY)) : defaultY;
    if (!g_overlayWindow) g_overlayWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | (g_overlay.clickThrough ? WS_EX_TRANSPARENT : 0), kOverlayClass, kAppName, WS_POPUP, x, y, targetWidth, targetHeight, nullptr, nullptr, g_instance, nullptr);
    else SetWindowPos(g_overlayWindow, HWND_TOPMOST, x, y, targetWidth, targetHeight, SWP_NOACTIVATE);
    if (!g_overlayWindow) return false;
    SetOverlayClickThrough(g_overlay.clickThrough);
    SetWindowPos(g_overlayWindow, HWND_TOPMOST, x, y, targetWidth, targetHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(g_overlayWindow, SW_SHOWNOACTIVATE);
    if (!RenderOverlay()) {
        DestroyWindow(g_overlayWindow);
        g_overlayWindow = nullptr;
        MessageBoxW(g_window, T(L"Liberty could not create the layered image window.", L"Liberty 无法创建图片悬浮窗口。"), kAppName, MB_OK | MB_ICONERROR);
        return false;
    }
    SaveOverlaySettings();
    return true;
}

void OpenOverlayFile(HWND owner) {
    wchar_t fileName[32768]{}; OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner = owner; dialog.lpstrFile = fileName; dialog.nMaxFile = ARRAYSIZE(fileName); dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico\0All files\0*.*\0"; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY; dialog.lpstrTitle = T(L"Choose an image to pin", L"选择要悬浮的图片"); if (GetOpenFileNameW(&dialog)) ShowOverlayFromPath(fileName, false);
}

struct __declspec(uuid("6E793361-73C6-11D0-8469-00AA00442901")) LibertyEmptyVolumeCacheCallback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ScanProgress(DWORDLONG, DWORD, LPCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE PurgeProgress(DWORDLONG, DWORDLONG, DWORD, LPCWSTR) = 0;
};

struct __declspec(uuid("8FCE5227-04DA-11D1-A004-00805F8ABE06")) LibertyEmptyVolumeCache : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Initialize(HKEY, LPCWSTR, LPWSTR*, LPWSTR*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSpaceUsed(DWORDLONG*, LibertyEmptyVolumeCacheCallback*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Purge(DWORDLONG, LibertyEmptyVolumeCacheCallback*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowProperties(HWND) = 0;
    virtual HRESULT STDMETHODCALLTYPE Deactivate(DWORD*) = 0;
};

struct __declspec(uuid("02B7E3BA-4DB3-11D2-B2D9-00C04F8EEC8C")) LibertyEmptyVolumeCache2 : public LibertyEmptyVolumeCache {
    virtual HRESULT STDMETHODCALLTYPE InitializeEx(HKEY, LPCWSTR, LPCWSTR, LPWSTR*, LPWSTR*, LPWSTR*, DWORD*) = 0;
};

class CleanupCallback final : public LibertyEmptyVolumeCacheCallback {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER; *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(LibertyEmptyVolumeCacheCallback)) { *object = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE ScanProgress(DWORDLONG, DWORD, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PurgeProgress(DWORDLONG, DWORDLONG, DWORD, LPCWSTR) override { return S_OK; }
};

struct CleanupHandler {
    ComPtr<LibertyEmptyVolumeCache2> version2;
    ComPtr<LibertyEmptyVolumeCache> version1;
    HKEY registryKey = nullptr;
    ~CleanupHandler() { if (registryKey) RegCloseKey(registryKey); }
    LibertyEmptyVolumeCache* base() const { return version2 ? static_cast<LibertyEmptyVolumeCache*>(version2.get()) : version1.get(); }
};

bool IsTrustedCleanupClass(const std::wstring& clsid) {
    std::wstring inproc; const std::wstring key = L"CLSID\\" + clsid + L"\\InprocServer32";
    if (!ReadRegistryString(HKEY_CLASSES_ROOT, key.c_str(), L"", inproc)) return false;
    wchar_t windowsDirectory[MAX_PATH]{}; GetWindowsDirectoryW(windowsDirectory, ARRAYSIZE(windowsDirectory));
    const std::wstring lowerPath = Lower(inproc), lowerWindows = Lower(windowsDirectory);
    return lowerPath.rfind(lowerWindows + L"\\", 0) == 0 || lowerPath.find(L"\\windows defender\\") != std::wstring::npos;
}

bool OpenCleanupHandler(const std::wstring& keyName, CleanupHandler& handler, std::wstring& display, std::wstring& description) {
    const std::wstring registryPath = std::wstring(kVolumeCachesKey) + L"\\" + keyName; std::wstring clsidText;
    if (!ReadRegistryString(HKEY_LOCAL_MACHINE, registryPath.c_str(), L"", clsidText) || !IsTrustedCleanupClass(clsidText)) return false;
    CLSID clsid{}; if (FAILED(CLSIDFromString(clsidText.c_str(), &clsid))) return false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, registryPath.c_str(), 0, KEY_READ, &handler.registryKey) != ERROR_SUCCESS) return false;
    HRESULT result = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(LibertyEmptyVolumeCache2), reinterpret_cast<void**>(handler.version2.put()));
    LPWSTR displayName = nullptr, descriptionText = nullptr, buttonText = nullptr; DWORD flags = 0;
    if (SUCCEEDED(result)) result = handler.version2->InitializeEx(handler.registryKey, L"C:\\", keyName.c_str(), &displayName, &descriptionText, &buttonText, &flags);
    else {
        result = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(LibertyEmptyVolumeCache), reinterpret_cast<void**>(handler.version1.put()));
        if (SUCCEEDED(result)) result = handler.version1->Initialize(handler.registryKey, L"C:\\", &displayName, &descriptionText, &flags);
    }
    if (displayName) { display = displayName; CoTaskMemFree(displayName); }
    if (descriptionText) { description = descriptionText; CoTaskMemFree(descriptionText); }
    if (display.empty()) display = keyName;
    return SUCCEEDED(result);
}

bool CleanupNameIsRisky(const std::wstring& name) {
    const std::wstring lower = Lower(name);
    return lower.find(L"download") != std::wstring::npos || lower.find(L"previous") != std::wstring::npos || lower.find(L"installation") != std::wstring::npos || lower.find(L"driver") != std::wstring::npos || lower.find(L"esd") != std::wstring::npos || lower.find(L"language") != std::wstring::npos || lower.find(L"reset") != std::wstring::npos || lower.find(L"upgrade") != std::wstring::npos;
}

bool CleanupNameIsSafe(const std::wstring& name) {
    const std::wstring lower = Lower(name);
    return lower.find(L"temporary") != std::wstring::npos || lower.find(L"thumbnail") != std::wstring::npos || lower.find(L"shader") != std::wstring::npos || lower.find(L"delivery optimization") != std::wstring::npos || lower.find(L"error reporting") != std::wstring::npos || lower.find(L"recycle bin") != std::wstring::npos || lower.find(L"internet cache") != std::wstring::npos || lower.find(L"setup log") != std::wstring::npos;
}

std::vector<CleanupItem> ScanCleanupHandlers() {
    std::vector<CleanupItem> result; HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kVolumeCachesKey, 0, KEY_READ, &root) != ERROR_SUCCESS) return result;
    DWORD index = 0; wchar_t name[256]{}; DWORD nameLength = ARRAYSIZE(name);
    while (RegEnumKeyExW(root, index++, name, &nameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        const std::wstring keyName(name, nameLength); nameLength = ARRAYSIZE(name); CleanupItem item; item.keyName = keyName; item.highRisk = CleanupNameIsRisky(keyName); item.safeDefault = !item.highRisk && CleanupNameIsSafe(keyName); item.needsElevation = keyName != L"Internet Cache Files" && keyName != L"Thumbnail Cache";
        CleanupHandler handler; item.handlerReady = OpenCleanupHandler(keyName, handler, item.displayName, item.description);
        if (item.handlerReady) { CleanupCallback callback; DWORDLONG space = 0; if (SUCCEEDED(handler.base()->GetSpaceUsed(&space, &callback))) item.space = space; DWORD flags = 0; handler.base()->Deactivate(&flags); }
        else { item.displayName = keyName; item.description = T(L"The Windows cleanup handler is unavailable or requires elevation.", L"Windows 清理处理器不可用或需要管理员权限。"); }
        result.push_back(std::move(item));
    }
    RegCloseKey(root);
    std::sort(result.begin(), result.end(), [](const CleanupItem& left, const CleanupItem& right) { return left.displayName < right.displayName; });
    return result;
}

bool PurgeCleanupHandler(const std::wstring& keyName) {
    CleanupHandler handler; std::wstring display, description;
    if (!OpenCleanupHandler(keyName, handler, display, description)) return false;
    CleanupCallback callback; const HRESULT result = handler.base()->Purge(static_cast<DWORDLONG>(-1), &callback); DWORD flags = 0; handler.base()->Deactivate(&flags); return SUCCEEDED(result);
}

bool IsProcessElevated() {
    HANDLE token = nullptr; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{}; DWORD size = sizeof(elevation); const bool result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated != 0; CloseHandle(token); return result;
}

bool WriteCleanupPlan(const std::wstring& path, const std::vector<std::wstring>& keys) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr); if (file == INVALID_HANDLE_VALUE) return false;
    std::wstring content; for (const std::wstring& key : keys) content += key + L"\r\n"; DWORD written = 0; const bool success = WriteFile(file, content.data(), static_cast<DWORD>(content.size() * sizeof(wchar_t)), &written, nullptr) != FALSE; CloseHandle(file); return success;
}

std::vector<std::wstring> ReadCleanupPlan(const std::wstring& path) {
    std::vector<std::wstring> result; HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr); if (file == INVALID_HANDLE_VALUE) return result;
    LARGE_INTEGER size{}; if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) { CloseHandle(file); return result; }
    std::vector<wchar_t> buffer(static_cast<size_t>(size.QuadPart / sizeof(wchar_t)) + 2, L'\0'); DWORD read = 0; ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr); CloseHandle(file);
    std::wstring all(buffer.data(), read / sizeof(wchar_t)); size_t start = 0;
    while (start < all.size()) { const size_t end = all.find_first_of(L"\r\n", start); const std::wstring line = all.substr(start, end == std::wstring::npos ? end : end - start); if (!line.empty()) result.push_back(line); if (end == std::wstring::npos) break; start = end + 1; while (start < all.size() && (all[start] == L'\r' || all[start] == L'\n')) ++start; }
    return result;
}

bool RunCleanupKeys(const std::vector<std::wstring>& keys, size_t& successCount) {
    successCount = 0; for (const std::wstring& key : keys) if (PurgeCleanupHandler(key)) ++successCount; return successCount == keys.size();
}

bool RunElevatedCleanup(const std::vector<std::wstring>& keys) {
    const std::wstring local = GetKnownFolder(FOLDERID_LocalAppData); const std::wstring path = local + L"\\LibertyCleanup-" + std::to_wstring(GetCurrentProcessId()) + L".txt"; if (!WriteCleanupPlan(path, keys)) return false;
    const std::wstring executable = GetModulePath(), parameters = L"--cleanup \"" + path + L"\""; SHELLEXECUTEINFOW execute{sizeof(execute)}; execute.fMask = SEE_MASK_NOCLOSEPROCESS; execute.lpVerb = L"runas"; execute.lpFile = executable.c_str(); execute.lpParameters = parameters.c_str(); execute.nShow = SW_HIDE;
    const bool launched = ShellExecuteExW(&execute) != FALSE; if (!launched) { DeleteFileW(path.c_str()); return false; } WaitForSingleObject(execute.hProcess, INFINITE); DWORD exitCode = 1; GetExitCodeProcess(execute.hProcess, &exitCode); CloseHandle(execute.hProcess); DeleteFileW(path.c_str()); return exitCode == 0;
}

void PopulateCleanupList(HWND list) {
    ListView_DeleteAllItems(list);
    for (size_t index = 0; index < g_cleanupItems.size(); ++index) {
        const CleanupItem& item = g_cleanupItems[index]; LVITEMW row{LVIF_TEXT, static_cast<int>(index), 0, 0, 0, const_cast<LPWSTR>(item.displayName.c_str()), 0, 0, 0}; ListView_InsertItem(list, &row); std::wstring size = FormatBytes(item.space); ListView_SetItemText(list, static_cast<int>(index), 1, size.data()); const wchar_t* risk = item.highRisk ? T(L"Review", L"需确认") : (item.handlerReady ? T(L"Safe", L"低风险") : T(L"Unavailable", L"不可用")); ListView_SetItemText(list, static_cast<int>(index), 2, const_cast<LPWSTR>(risk)); ListView_SetCheckState(list, static_cast<int>(index), item.safeDefault ? TRUE : FALSE);
    }
}

void LayoutCleanupWindow(HWND window) {
    if (!window) return;
    const UINT dpi = GetWindowDpiSafe(window);
    RECT client{};
    GetClientRect(window, &client);
    const int margin = ScaleUi(24, dpi);
    const int statusTop = ScaleUi(14, dpi);
    const int statusHeight = ScaleUi(30, dpi);
    const int listTop = ScaleUi(58, dpi);
    const int buttonHeight = ScaleUi(38, dpi);
    const int bottomGap = ScaleUi(16, dpi);
    const int listBottom = (std::max)(listTop + ScaleUi(120, dpi), static_cast<int>(client.bottom) - buttonHeight - bottomGap);
    const int contentWidth = (std::max)(0, static_cast<int>(client.right) - margin * 2);

    MoveWindow(GetDlgItem(window, ID_CLEANUP_STATUS), margin, statusTop,
               contentWidth, statusHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_CLEANUP_LIST), margin, listTop,
               contentWidth, (std::max)(0, listBottom - listTop), TRUE);

    const int scanWidth = ScaleUi(148, dpi);
    const int closeWidth = ScaleUi(116, dpi);
    const int runWidth = ScaleUi(190, dpi);
    const int buttonGap = ScaleUi(12, dpi);
    const int buttonY = (std::max)(listTop, static_cast<int>(client.bottom) - bottomGap - buttonHeight);
    const int runX = client.right - margin - runWidth;
    const int closeX = runX - buttonGap - closeWidth;
    MoveWindow(GetDlgItem(window, ID_CLEANUP_SCAN), margin, buttonY, scanWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_CLEANUP_CANCEL), closeX, buttonY, closeWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_CLEANUP_RUN), runX, buttonY, runWidth, buttonHeight, TRUE);

    HWND list = GetDlgItem(window, ID_CLEANUP_LIST);
    if (list) {
        const int riskWidth = ScaleUi(156, dpi);
        const int spaceWidth = ScaleUi(136, dpi);
        const int nameWidth = (std::max)(ScaleUi(260, dpi), contentWidth - spaceWidth - riskWidth);
        ListView_SetColumnWidth(list, 0, nameWidth);
        ListView_SetColumnWidth(list, 1, spaceWidth);
        ListView_SetColumnWidth(list, 2, riskWidth);
    }
}

void StartCleanupScan(HWND window) {
    SetDlgItemTextW(window, ID_CLEANUP_STATUS, T(L"Scanning Windows cleanup handlers…", L"正在扫描 Windows 清理项目…")); EnableWindow(GetDlgItem(window, ID_CLEANUP_SCAN), FALSE); EnableWindow(GetDlgItem(window, ID_CLEANUP_RUN), FALSE);
    std::thread([window]() { auto* result = new CleanupScanResult; CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); result->items = ScanCleanupHandlers(); CoUninitialize(); if (IsWindow(window)) PostMessageW(window, kCleanupScanComplete, 0, reinterpret_cast<LPARAM>(result)); else delete result; }).detach();
}

LRESULT CALLBACK CleanupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetDarkMode(window, IsDarkTheme());
        CreateWindowExW(0, L"STATIC", T(L"Preparing…", L"正在准备…"), WS_CHILD | WS_VISIBLE,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CLEANUP_STATUS), g_instance, nullptr);
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_VSCROLL,
                                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CLEANUP_LIST), g_instance, nullptr);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 260, const_cast<LPWSTR>(T(L"Cleanup item", L"清理项目"))};
        ListView_InsertColumn(list, 0, &column);
        column.cx = 136; column.pszText = const_cast<LPWSTR>(T(L"Space", L"空间"));
        ListView_InsertColumn(list, 1, &column);
        column.cx = 156; column.pszText = const_cast<LPWSTR>(T(L"Risk", L"风险"));
        ListView_InsertColumn(list, 2, &column);
        CreateWindowExW(0, L"BUTTON", T(L"Scan again", L"重新扫描"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CLEANUP_SCAN), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Close", L"关闭"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CLEANUP_CANCEL), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Clean selected", L"清理所选"),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_CLEANUP_RUN), g_instance, nullptr);
        ApplyUiFont(window, 16);
        LayoutCleanupWindow(window);
        StartCleanupScan(window);
        return 0;
    }
    case WM_SIZE:
        LayoutCleanupWindow(window);
        return 0;
    case WM_DPICHANGED:
        if (const RECT* suggested = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyUiFont(window, 16);
        LayoutCleanupWindow(window);
        return 0;
    case kCleanupScanComplete: {
        auto* result = reinterpret_cast<CleanupScanResult*>(lParam); g_cleanupItems = std::move(result->items); delete result; PopulateCleanupList(GetDlgItem(window, ID_CLEANUP_LIST)); std::wstring statusMessage = T(L"Found ", L"发现 ") + std::to_wstring(g_cleanupItems.size()) + T(L" cleanup categories.", L" 个清理项目。"); SetDlgItemTextW(window, ID_CLEANUP_STATUS, statusMessage.c_str()); EnableWindow(GetDlgItem(window, ID_CLEANUP_SCAN), TRUE); EnableWindow(GetDlgItem(window, ID_CLEANUP_RUN), !g_cleanupItems.empty()); return 0;
    }
    case kCleanupRunComplete: {
        auto* result = reinterpret_cast<CleanupRunResult*>(lParam); EnableWindow(GetDlgItem(window, ID_CLEANUP_SCAN), TRUE); EnableWindow(GetDlgItem(window, ID_CLEANUP_RUN), TRUE); MessageBoxW(window, result->message.c_str(), kAppName, result->success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING); delete result; StartCleanupScan(window); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_CLEANUP_CANCEL) { DestroyWindow(window); return 0; }
        if (LOWORD(wParam) == ID_CLEANUP_SCAN) { StartCleanupScan(window); return 0; }
        if (LOWORD(wParam) == ID_CLEANUP_RUN) {
            HWND list = GetDlgItem(window, ID_CLEANUP_LIST); std::vector<std::wstring> selected; bool risky = false, needsElevation = false;
            for (int index = 0; index < ListView_GetItemCount(list) && index < static_cast<int>(g_cleanupItems.size()); ++index) if (ListView_GetCheckState(list, index)) { const CleanupItem& item = g_cleanupItems[static_cast<size_t>(index)]; if (!item.handlerReady) continue; selected.push_back(item.keyName); risky = risky || item.highRisk; needsElevation = needsElevation || item.needsElevation; }
            if (selected.empty()) { MessageBoxW(window, T(L"Select at least one available cleanup item.", L"请至少选择一个可用的清理项目。"), kAppName, MB_OK | MB_ICONINFORMATION); return 0; }
            if (risky && MessageBoxW(window, T(L"Some selected items can remove downloads, update rollback data, or driver packages. Continue?", L"所选项目可能删除下载内容、更新回滚数据或驱动包。是否继续？"), kAppName, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
            EnableWindow(GetDlgItem(window, ID_CLEANUP_SCAN), FALSE); EnableWindow(GetDlgItem(window, ID_CLEANUP_RUN), FALSE); SetDlgItemTextW(window, ID_CLEANUP_STATUS, T(L"Cleaning…", L"正在清理…"));
            std::thread([window, selected, needsElevation]() { auto* result = new CleanupRunResult; size_t successCount = 0; const bool success = needsElevation && !IsProcessElevated() ? RunElevatedCleanup(selected) : RunCleanupKeys(selected, successCount); result->success = success; result->message = success ? T(L"Cleanup completed.", L"清理完成。") : T(L"Some cleanup handlers could not complete. Locked files were left untouched.", L"部分清理项目未完成，正在使用的文件已保留。"); if (IsWindow(window)) PostMessageW(window, kCleanupRunComplete, 0, reinterpret_cast<LPARAM>(result)); else delete result; }).detach();
            return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: if (g_cleanupWindow == window) g_cleanupWindow = nullptr; return 0;
    case WM_NCDESTROY: ReleaseUiFont(window); return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowCleanupWindow(HWND owner) {
    if (g_cleanupWindow) { SetForegroundWindow(g_cleanupWindow); return; }
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const UINT dpi = GetMonitorDpiSafe(monitor, owner);
    const int edge = ScaleUi(24, dpi);
    const int width = (std::min)(ScaleUi(1080, dpi), static_cast<int>(info.rcWork.right - info.rcWork.left - edge * 2));
    const int height = (std::min)(ScaleUi(760, dpi), static_cast<int>(info.rcWork.bottom - info.rcWork.top - edge * 2));
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    g_cleanupWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kCleanupClass,
                                      T(L"Liberty cleanup", L"Liberty 清理"),
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                      x, y, width, height, owner, nullptr, g_instance, nullptr);
    if (g_cleanupWindow) { ShowWindow(g_cleanupWindow, SW_SHOW); UpdateWindow(g_cleanupWindow); }
}

const wchar_t* StartupRiskLabel(const StartupItem& item) {
    if (item.protectedItem) return T(L"Protected", L"系统保护");
    if (item.highRisk && item.chainRisk) return T(L"High / chain", L"高风险/链式");
    if (item.highRisk) return T(L"High risk", L"高风险");
    if (item.chainRisk) return T(L"Chain wake", L"链式唤醒");
    if (item.thirdParty) return T(L"Third-party", L"第三方软件");
    return T(L"Review", L"可管理");
}

void PopulateStartupList(HWND list) {
    ListView_DeleteAllItems(list);
    for (size_t index = 0; index < g_startupItems.size(); ++index) {
        const StartupItem& item = g_startupItems[index];
        std::wstring name = item.name.empty() ? item.location : item.name;
        if (!item.enabled && HasStartupBackup(item)) name += T(L"  [blocked by Liberty]", L"  [Liberty 已阻止]");
        LVITEMW row{LVIF_TEXT, static_cast<int>(index), 0, 0, 0, const_cast<LPWSTR>(name.c_str()), 0, 0, 0};
        ListView_InsertItem(list, &row);
        ListView_SetItemText(list, static_cast<int>(index), 1, const_cast<LPWSTR>(item.source.c_str()));
        ListView_SetItemText(list, static_cast<int>(index), 2, const_cast<LPWSTR>(item.command.c_str()));
        ListView_SetItemText(list, static_cast<int>(index), 3, const_cast<LPWSTR>(StartupRiskLabel(item)));
        ListView_SetCheckState(list, static_cast<int>(index), FALSE);
    }
}

void LayoutStartupWindow(HWND window) {
    if (!window) return;
    const UINT dpi = GetWindowDpiSafe(window);
    RECT client{}; GetClientRect(window, &client);
    const int margin = ScaleUi(24, dpi);
    const int statusTop = ScaleUi(14, dpi);
    const int statusHeight = ScaleUi(32, dpi);
    const int listTop = ScaleUi(62, dpi);
    const int buttonHeight = ScaleUi(38, dpi);
    const int bottomGap = ScaleUi(16, dpi);
    const int listBottom = (std::max)(listTop + ScaleUi(140, dpi), static_cast<int>(client.bottom) - buttonHeight - bottomGap);
    const int contentWidth = (std::max)(0, static_cast<int>(client.right) - margin * 2);
    MoveWindow(GetDlgItem(window, ID_STARTUP_STATUS), margin, statusTop, contentWidth, statusHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_STARTUP_LIST), margin, listTop, contentWidth,
               (std::max)(0, listBottom - listTop), TRUE);

    const int scanWidth = ScaleUi(148, dpi);
    const int selectWidth = ScaleUi(154, dpi);
    const int blockWidth = ScaleUi(160, dpi);
    const int restoreWidth = ScaleUi(148, dpi);
    const int closeWidth = ScaleUi(116, dpi);
    const int gap = ScaleUi(12, dpi);
    const int buttonY = (std::max)(listTop, static_cast<int>(client.bottom) - bottomGap - buttonHeight);
    const int closeX = client.right - margin - closeWidth;
    const int restoreX = closeX - gap - restoreWidth;
    const int blockX = restoreX - gap - blockWidth;
    const int selectX = blockX - gap - selectWidth;
    MoveWindow(GetDlgItem(window, ID_STARTUP_SCAN), margin, buttonY, scanWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_STARTUP_SELECT_RISK), selectX, buttonY, selectWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_STARTUP_BLOCK), blockX, buttonY, blockWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_STARTUP_RESTORE), restoreX, buttonY, restoreWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, ID_STARTUP_CANCEL), closeX, buttonY, closeWidth, buttonHeight, TRUE);

    HWND list = GetDlgItem(window, ID_STARTUP_LIST);
    if (list) {
        const int sourceWidth = ScaleUi(164, dpi);
        const int riskWidth = ScaleUi(144, dpi);
        const int nameWidth = ScaleUi(250, dpi);
        ListView_SetColumnWidth(list, 0, nameWidth);
        ListView_SetColumnWidth(list, 1, sourceWidth);
        ListView_SetColumnWidth(list, 2, (std::max)(ScaleUi(300, dpi), contentWidth - nameWidth - sourceWidth - riskWidth));
        ListView_SetColumnWidth(list, 3, riskWidth);
    }
}

void StartStartupScan(HWND window) {
    SetDlgItemTextW(window, ID_STARTUP_STATUS,
                    T(L"Scanning startup locations, tasks, and services…", L"正在扫描启动位置、登录任务和服务…"));
    EnableWindow(GetDlgItem(window, ID_STARTUP_SCAN), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_SELECT_RISK), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_BLOCK), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_RESTORE), FALSE);
    std::thread([window]() {
        auto* result = new StartupScanResult;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        result->items = ScanStartupItems();
        CoUninitialize();
        if (IsWindow(window)) PostMessageW(window, kStartupScanComplete, 0, reinterpret_cast<LPARAM>(result));
        else delete result;
    }).detach();
}

void ApplySelectedStartupItems(HWND window, bool block) {
    HWND list = GetDlgItem(window, ID_STARTUP_LIST);
    std::vector<StartupItem> selected;
    std::vector<StartupPlanEntry> plan;
    bool hasWarning = false;
    bool needsElevation = false;
    for (int index = 0; index < ListView_GetItemCount(list) && index < static_cast<int>(g_startupItems.size()); ++index) {
        if (!ListView_GetCheckState(list, index)) continue;
        const StartupItem& item = g_startupItems[static_cast<size_t>(index)];
        if (item.protectedItem || (block ? !item.enabled : item.enabled)) continue;
        selected.push_back(item);
        plan.push_back({block, item.id});
        needsElevation = needsElevation || item.requiresElevation;
        hasWarning = hasWarning || item.chainRisk || item.highRisk || item.thirdParty;
    }
    if (selected.empty()) {
        MessageBoxW(window, T(L"Select eligible startup items first.", L"请先选择可管理的启动项。"), kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (block && hasWarning && MessageBoxW(window,
        T(L"Some selected entries can relaunch through launchers, updaters, scripts, or third-party background helpers. Disable only entries you recognize. Continue?",
          L"所选项目包含启动器、更新器、脚本、重复唤醒链或第三方后台助手。请确认你认识这些项目后再阻止。是否继续？"),
        kAppName, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    EnableWindow(GetDlgItem(window, ID_STARTUP_SCAN), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_SELECT_RISK), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_BLOCK), FALSE);
    EnableWindow(GetDlgItem(window, ID_STARTUP_RESTORE), FALSE);
    SetDlgItemTextW(window, ID_STARTUP_STATUS, block ? T(L"Blocking selected startup items…", L"正在阻止选中的启动项…") : T(L"Restoring selected startup items…", L"正在恢复选中的启动项…"));
    std::thread([window, selected, plan, needsElevation]() {
        auto* result = new StartupApplyResult;
        if (needsElevation && !IsProcessElevated()) {
            result->success = RunElevatedStartupPlan(plan);
            result->changed = result->success ? plan.size() : 0;
        } else {
            result->success = ApplyStartupItems(selected, plan.front().block, result->changed);
        }
        result->message = result->success
            ? std::to_wstring(result->changed) + T(L" startup item(s) updated.", L" 个启动项已更新。")
            : T(L"Some entries could not be changed. Protected or locked items were left untouched.", L"部分项目无法修改；系统保护项或正在使用的项目已保留。");
        if (IsWindow(window)) PostMessageW(window, kStartupApplyComplete, 0, reinterpret_cast<LPARAM>(result));
        else delete result;
    }).detach();
}

LRESULT CALLBACK StartupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetDarkMode(window, IsDarkTheme());
        CreateWindowExW(0, L"STATIC", T(L"Preparing…", L"正在准备…"), WS_CHILD | WS_VISIBLE,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_STATUS), g_instance, nullptr);
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_VSCROLL,
                                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_LIST), g_instance, nullptr);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH, 0, 250, const_cast<LPWSTR>(T(L"Startup item", L"启动项"))};
        ListView_InsertColumn(list, 0, &column);
        column.cx = 164; column.pszText = const_cast<LPWSTR>(T(L"Source", L"来源")); ListView_InsertColumn(list, 1, &column);
        column.cx = 360; column.pszText = const_cast<LPWSTR>(T(L"Command / target", L"命令/目标")); ListView_InsertColumn(list, 2, &column);
        column.cx = 144; column.pszText = const_cast<LPWSTR>(T(L"Risk", L"风险")); ListView_InsertColumn(list, 3, &column);
        CreateWindowExW(0, L"BUTTON", T(L"Scan again", L"重新扫描"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_SCAN), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Select risks", L"选择风险项"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_SELECT_RISK), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Block selected", L"阻止选中项"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_BLOCK), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Restore selected", L"恢复选中项"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_RESTORE), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Close", L"关闭"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_STARTUP_CANCEL), g_instance, nullptr);
        ApplyUiFont(window, 16);
        LayoutStartupWindow(window);
        StartStartupScan(window);
        return 0;
    }
    case WM_SIZE: LayoutStartupWindow(window); return 0;
    case WM_DPICHANGED:
        if (const RECT* suggested = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyUiFont(window, 16); LayoutStartupWindow(window); return 0;
    case kStartupScanComplete: {
        auto* result = reinterpret_cast<StartupScanResult*>(lParam);
        g_startupItems = std::move(result->items); delete result;
        PopulateStartupList(GetDlgItem(window, ID_STARTUP_LIST));
        size_t chain = 0, high = 0, protectedCount = 0;
        for (const StartupItem& item : g_startupItems) { chain += item.chainRisk; high += item.highRisk; protectedCount += item.protectedItem; }
        std::wstring status = T(L"Found ", L"发现 ") + std::to_wstring(g_startupItems.size()) +
            T(L" startup items; ", L" 个启动项；") + std::to_wstring(chain) +
            T(L" chain-risk, ", L" 个链式风险，") + std::to_wstring(high) +
            T(L" high-risk. Protected: ", L" 个高风险。系统保护：") + std::to_wstring(protectedCount);
        SetDlgItemTextW(window, ID_STARTUP_STATUS, status.c_str());
        EnableWindow(GetDlgItem(window, ID_STARTUP_SCAN), TRUE);
        EnableWindow(GetDlgItem(window, ID_STARTUP_SELECT_RISK), TRUE);
        EnableWindow(GetDlgItem(window, ID_STARTUP_BLOCK), TRUE);
        EnableWindow(GetDlgItem(window, ID_STARTUP_RESTORE), TRUE);
        return 0;
    }
    case kStartupApplyComplete: {
        auto* result = reinterpret_cast<StartupApplyResult*>(lParam);
        MessageBoxW(window, result->message.c_str(), kAppName,
                    result->success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);
        delete result; StartStartupScan(window); return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_STARTUP_SCAN: StartStartupScan(window); return 0;
        case ID_STARTUP_SELECT_RISK: {
            HWND list = GetDlgItem(window, ID_STARTUP_LIST);
            for (int index = 0; index < ListView_GetItemCount(list) && index < static_cast<int>(g_startupItems.size()); ++index) {
                const StartupItem& item = g_startupItems[static_cast<size_t>(index)];
                ListView_SetCheckState(list, index, (!item.protectedItem && item.enabled && (item.chainRisk || item.highRisk || item.thirdParty)) ? TRUE : FALSE);
            }
            return 0;
        }
        case ID_STARTUP_BLOCK: ApplySelectedStartupItems(window, true); return 0;
        case ID_STARTUP_RESTORE: ApplySelectedStartupItems(window, false); return 0;
        case ID_STARTUP_CANCEL: DestroyWindow(window); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: if (g_startupWindow == window) g_startupWindow = nullptr; return 0;
    case WM_NCDESTROY: ReleaseUiFont(window); return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowStartupWindow(HWND owner) {
    if (g_startupWindow) { SetForegroundWindow(g_startupWindow); return; }
    POINT cursor{}; GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    const UINT dpi = GetMonitorDpiSafe(monitor, owner);
    const int edge = ScaleUi(24, dpi);
    const int width = (std::min)(ScaleUi(1180, dpi), static_cast<int>(info.rcWork.right - info.rcWork.left - edge * 2));
    const int height = (std::min)(ScaleUi(780, dpi), static_cast<int>(info.rcWork.bottom - info.rcWork.top - edge * 2));
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    g_startupWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kStartupClass,
                                      T(L"Liberty startup manager", L"Liberty 启动项管理"),
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                      x, y, width, height, owner, nullptr, g_instance, nullptr);
    if (g_startupWindow) { ShowWindow(g_startupWindow, SW_SHOW); UpdateWindow(g_startupWindow); }
}

void ShowAboutWindow(HWND owner) {
    if (g_aboutWindow) { SetForegroundWindow(g_aboutWindow); return; }
    POINT cursor{}; GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor, &info);
    const UINT dpi = GetMonitorDpiSafe(monitor, owner);
    const int width = ScaleUi(520, dpi);
    const int height = ScaleUi(360, dpi);
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    g_aboutWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kAboutClass,
                                    T(L"About Liberty", L"关于 Liberty"),
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                    x, y, width, height, owner, nullptr, g_instance, nullptr);
    if (g_aboutWindow) ShowWindow(g_aboutWindow, SW_SHOW);
}

void LayoutAboutWindow(HWND window) {
    const UINT dpi = GetWindowDpiSafe(window);
    RECT client{}; GetClientRect(window, &client);
    const int buttonWidth = ScaleUi(104, dpi);
    const int buttonHeight = ScaleUi(36, dpi);
    const int margin = ScaleUi(24, dpi);
    MoveWindow(GetDlgItem(window, IDOK), client.right - margin - buttonWidth,
               client.bottom - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
}

LRESULT CALLBACK AboutProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        SetDarkMode(window, IsDarkTheme());
        CreateWindowExW(0, L"BUTTON", T(L"Close", L"关闭"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(IDOK), g_instance, nullptr);
        ApplyUiFont(window, 16);
        LayoutAboutWindow(window);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) { DestroyWindow(window); return 0; }
    if (message == WM_SIZE) { LayoutAboutWindow(window); return 0; }
    if (message == WM_DPICHANGED) {
        if (const RECT* suggested = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyUiFont(window, 16); LayoutAboutWindow(window); return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC hdc = BeginPaint(window, &paint);
        const UINT dpi = GetWindowDpiSafe(window);
        RECT logo{ScaleUi(28, dpi), ScaleUi(28, dpi), ScaleUi(140, dpi), ScaleUi(140, dpi)};
        DrawTrinityLogo(hdc, logo, true);
        RECT title{ScaleUi(166, dpi), ScaleUi(34, dpi), ScaleUi(480, dpi), ScaleUi(76, dpi)};
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, IsDarkTheme() ? RGB(245, 245, 250) : RGB(25, 25, 35));
        HFONT font = CreateUiFont(window, 27, FW_SEMIBOLD, true);
        HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
        DrawTextW(hdc, L"Liberty", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, old); DeleteObject(font);
        RECT text{ScaleUi(28, dpi), ScaleUi(156, dpi), ScaleUi(480, dpi), ScaleUi(278, dpi)};
        std::wstring about = L"Liberty 1.3.0\n\n";
        about += T(L"Trinity mark: freedom, control, and focus.", L"Trinity 标志：自由、控制与专注。\n");
        about += L"\nCmd = "; about += ModifierLabel(g_commandKey);
        about += L"\nOption = "; about += ModifierLabel(g_optionKey);
        about += L"\nControl = "; about += ModifierLabel(g_controlKey);
        SetTextColor(hdc, IsDarkTheme() ? RGB(205, 207, 220) : RGB(70, 70, 85));
        DrawTextW(hdc, about.c_str(), -1, &text, DT_LEFT | DT_WORDBREAK);
        EndPaint(window, &paint); return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY && g_aboutWindow == window) g_aboutWindow = nullptr;
    if (message == WM_NCDESTROY) { ReleaseUiFont(window); return DefWindowProcW(window, message, wParam, 0); }
    return DefWindowProcW(window, message, wParam, 0);
}

bool IsDarkTheme() {
    DWORD value = 1; if (!ReadRegistryDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", value)) return false; return value == 0;
}

void SetDarkMode(HWND window, bool dark) {
    if (!window) return; BOOL value = dark ? TRUE : FALSE; DwmSetWindowAttribute(window, 20, &value, sizeof(value)); DWORD corner = 2; DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
}

void AddMenuHeader(std::vector<MenuRow>& rows, const wchar_t* english, const wchar_t* chinese) { rows.push_back({MenuRow::Header, 0, T(english, chinese), {}, false, true}); }

void AddMenuAction(std::vector<MenuRow>& rows, UINT id, const wchar_t* english, const wchar_t* chinese, bool checked = false, bool enabled = true, const std::wstring& detail = {}) { rows.push_back({MenuRow::Action, id, T(english, chinese), detail, checked, enabled}); }

void AddMenuCategory(std::vector<MenuRow>& rows, UINT id, const wchar_t* english, const wchar_t* chinese,
                     const wchar_t* englishDetail, const wchar_t* chineseDetail) {
    rows.push_back({MenuRow::Category, id, T(english, chinese), T(englishDetail, chineseDetail), false, true});
}

void AddMenuBack(std::vector<MenuRow>& rows) {
    AddMenuAction(rows, ID_MENU_BACK, L"Back", L"返回");
}

std::vector<MenuRow> BuildMenuRows(MenuPage page = MenuPage::Root) {
    std::vector<MenuRow> rows;
    if (page == MenuPage::Root) {
        rows.push_back({MenuRow::Status, 0, T(L"Current status", L"当前状态"),
                        g_enabled ? T(L"Shortcuts enabled", L"快捷键已启用") : T(L"Shortcuts paused", L"快捷键已暂停"), false, true});
        AddMenuCategory(rows, ID_MENU_SHORTCUTS, L"Shortcuts", L"快捷键", L"Remapping and keyboard settings", L"快捷键转换与键盘设置");
        AddMenuCategory(rows, ID_MENU_MAINTENANCE, L"System maintenance", L"系统维护", L"Power, startup, cleanup and protection", L"电源、启动、清理与保护");
        AddMenuCategory(rows, ID_MENU_STATUS, L"Status bar", L"状态栏", L"Current Liberty and overlay state", L"查看 Liberty 与悬浮图片状态");
        AddMenuCategory(rows, ID_MENU_OTHER, L"Other", L"其他", L"Image overlay, language and about", L"图片悬浮、语言与关于");
        return rows;
    }

    AddMenuBack(rows);
    if (page == MenuPage::Shortcuts) {
        AddMenuAction(rows, ID_TOGGLE, g_enabled ? L"Pause shortcuts" : L"Resume shortcuts", g_enabled ? L"暂停快捷键" : L"恢复快捷键", g_enabled);
        AddMenuAction(rows, ID_MAPPINGS, L"Keyboard mappings…", L"快捷键映射…");
        AddMenuAction(rows, ID_STARTUP, L"Start Liberty with Windows", L"随 Windows 启动", g_startAtLogin);
    } else if (page == MenuPage::Maintenance) {
        AddMenuAction(rows, ID_SCREEN_OFF, L"Turn display off", L"关闭显示器");
        AddMenuAction(rows, ID_SCREENSHOT_DESKTOP, L"Save screenshot to Desktop", L"截图并保存到桌面");
        AddMenuAction(rows, ID_SHUTDOWN_15, L"Shut down in 15 minutes", L"15 分钟后关机");
        AddMenuAction(rows, ID_SHUTDOWN_30, L"Shut down in 30 minutes", L"30 分钟后关机");
        AddMenuAction(rows, ID_SHUTDOWN_60, L"Shut down in 1 hour", L"1 小时后关机");
        AddMenuAction(rows, ID_SHUTDOWN_120, L"Shut down in 2 hours", L"2 小时后关机");
        AddMenuAction(rows, ID_SHUTDOWN_CUSTOM, L"Custom shutdown time…", L"自定义关机时间…");
        AddMenuAction(rows, ID_SHUTDOWN_CANCEL, L"Cancel scheduled shutdown", L"取消定时关机");
        AddMenuAction(rows, ID_STARTUP_MANAGER, L"Manage startup items…", L"管理开机启动项…");
        AddMenuAction(rows, ID_CLEANUP, L"Scan and clean caches…", L"扫描并清理缓存…");
        AddMenuAction(rows, ID_BLOCK_ONEDRIVE, L"Block OneDrive auto-start", L"阻止 OneDrive 自动启动", g_blockOneDrive);
        AddMenuAction(rows, ID_HIDE_NVIDIA, L"Hide NVIDIA panel startup", L"隐藏 NVIDIA 面板启动", g_hideNvidiaPanel);
        AddMenuAction(rows, ID_HIDE_AMD, L"Hide AMD panel startup", L"隐藏 AMD 面板启动", g_hideAmdPanel);
        AddMenuAction(rows, ID_HIDE_SECURITY, L"Hide Windows Security tray entry", L"隐藏 Windows Security 托盘入口", g_hideSecurityCenter);
    } else if (page == MenuPage::Status) {
        rows.push_back({MenuRow::Status, 0, T(L"Shortcuts", L"快捷键"), g_enabled ? T(L"Enabled", L"已启用") : T(L"Paused", L"已暂停"), false, true});
        rows.push_back({MenuRow::Status, 0, T(L"Windows startup", L"Windows 启动"), g_startAtLogin ? T(L"Enabled", L"已启用") : T(L"Disabled", L"未启用"), false, true});
        rows.push_back({MenuRow::Status, 0, T(L"OneDrive", L"OneDrive"), g_blockOneDrive ? T(L"Auto-start blocked", L"已阻止自动启动") : T(L"Not blocked", L"未阻止"), false, true});
        rows.push_back({MenuRow::Status, 0, T(L"Floating image", L"悬浮图片"), g_overlayWindow ? T(L"Visible", L"显示中") : T(L"Not open", L"未打开"), false, true});
    } else if (page == MenuPage::Other) {
        AddMenuAction(rows, ID_OVERLAY_OPEN, L"Pin an image on desktop", L"将图片悬浮在桌面");
        AddMenuAction(rows, ID_OVERLAY_RESTORE, L"Restore last image", L"恢复上次图片", false, !g_overlay.path.empty());
        AddMenuAction(rows, ID_OVERLAY_LOCK, g_overlay.locked ? L"Unlock image position" : L"Lock image position", g_overlay.locked ? L"解锁图片位置" : L"锁定图片位置", g_overlay.locked, g_overlayWindow != nullptr);
        AddMenuAction(rows, ID_OVERLAY_CLICKTHROUGH, g_overlay.clickThrough ? L"Disable mouse passthrough" : L"Enable mouse passthrough", g_overlay.clickThrough ? L"关闭鼠标穿透" : L"开启鼠标穿透", g_overlay.clickThrough, g_overlayWindow != nullptr);
        AddMenuAction(rows, ID_OVERLAY_CLOSE, L"Close floating image", L"关闭悬浮图片", false, g_overlayWindow != nullptr);
        AddMenuAction(rows, ID_MENU_LANGUAGE, g_language == AppLanguage::Chinese ? L"Switch to English" : L"切换到简体中文", g_language == AppLanguage::Chinese ? L"切换到 English" : L"切换到简体中文");
        AddMenuAction(rows, ID_ABOUT, L"About Liberty", L"关于 Liberty");
        AddMenuAction(rows, ID_EXIT, L"Exit Liberty", L"退出 Liberty");
    }
    return rows;
}

int MenuRowHeight(const MenuRow& row, UINT dpi) {
    if (row.kind == MenuRow::Header) return ScaleUi(34, dpi);
    if (row.kind == MenuRow::Separator) return ScaleUi(10, dpi);
    if (row.kind == MenuRow::Status) return ScaleUi(60, dpi);
    return ScaleUi(58, dpi);
}

int MenuTopHeight(UINT dpi) { return ScaleUi(92, dpi); }

int MenuTotalHeight(const std::vector<MenuRow>& rows, UINT dpi) {
    int height = MenuTopHeight(dpi) + ScaleUi(14, dpi);
    for (const MenuRow& row : rows) height += MenuRowHeight(row, dpi);
    return height;
}

int MenuHitTest(int y, const std::vector<MenuRow>& rows, int scroll, UINT dpi) {
    int current = MenuTopHeight(dpi) - scroll;
    for (size_t index = 0; index < rows.size(); ++index) {
        const int height = MenuRowHeight(rows[index], dpi);
        if (y >= current && y < current + height) return static_cast<int>(index);
        current += height;
    }
    return -1;
}

void ExecuteCommand(UINT command) {
    switch (command) {
    case ID_MENU_SHORTCUTS: g_menuPage = MenuPage::Shortcuts; RefreshModernMenu(); break;
    case ID_MENU_MAINTENANCE: g_menuPage = MenuPage::Maintenance; RefreshModernMenu(); break;
    case ID_MENU_STATUS: g_menuPage = MenuPage::Status; RefreshModernMenu(); break;
    case ID_MENU_OTHER: g_menuPage = MenuPage::Other; RefreshModernMenu(); break;
    case ID_MENU_BACK: g_menuPage = MenuPage::Root; RefreshModernMenu(); break;
    case ID_TOGGLE: g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled ? 1 : 0); ResetMappedModifierState(); RefreshTrayIcon(); break;
    case ID_SCREEN_OFF: ScreenOff(); break;
    case ID_SCREENSHOT_DESKTOP: if (!CaptureDesktopScreenshot()) MessageBoxW(g_window, T(L"Liberty could not save the desktop screenshot.", L"Liberty 无法保存桌面截图。"), kAppName, MB_OK | MB_ICONERROR); break;
    case ID_SHUTDOWN_15: ScheduleShutdown(15 * 60); break;
    case ID_SHUTDOWN_30: ScheduleShutdown(30 * 60); break;
    case ID_SHUTDOWN_60: ScheduleShutdown(60 * 60); break;
    case ID_SHUTDOWN_120: ScheduleShutdown(120 * 60); break;
    case ID_SHUTDOWN_CUSTOM: { const INT_PTR minutes = ShowMinutesDialog(g_window); if (minutes > 0) ScheduleShutdown(static_cast<UINT>(minutes) * 60); break; }
    case ID_SHUTDOWN_CANCEL: CancelShutdown(); break;
    case ID_STARTUP: if (!SetStartup(!g_startAtLogin)) MessageBoxW(g_window, T(L"Could not update startup.", L"无法更新启动设置。"), kAppName, MB_OK | MB_ICONERROR); break;
    case ID_MAPPINGS: ShowMappingDialog(g_window); break;
    case ID_STARTUP_MANAGER: ShowStartupWindow(g_window); break;
    case ID_MENU_LANGUAGE: ToggleLanguage(); break;
    case ID_BLOCK_ONEDRIVE: { const bool next = !g_blockOneDrive; if (next && OneDrivePolicyForcesAutoStart()) { MessageBoxW(g_window, T(L"OneDrive auto-start is controlled by a Windows policy.", L"OneDrive 自动启动由 Windows 策略控制。"), kAppName, MB_OK | MB_ICONWARNING); break; } if (ApplyOneDriveBlock(next, next)) { g_blockOneDrive = next; SaveDword(L"BlockOneDrive", next ? 1 : 0); } else MessageBoxW(g_window, T(L"Liberty could not update all OneDrive startup entries.", L"Liberty 无法更新全部 OneDrive 启动入口。"), kAppName, MB_OK | MB_ICONERROR); break; }
    case ID_HIDE_NVIDIA: if (ApplyRunEntryBlocks(kNvidiaEntries, ARRAYSIZE(kNvidiaEntries), !g_hideNvidiaPanel)) { g_hideNvidiaPanel = !g_hideNvidiaPanel; SaveDword(L"HideNvidiaPanel", g_hideNvidiaPanel ? 1 : 0); } else MessageBoxW(g_window, T(L"Could not update NVIDIA startup entries.", L"无法更新 NVIDIA 启动入口。"), kAppName, MB_OK | MB_ICONERROR); break;
    case ID_HIDE_AMD: if (ApplyRunEntryBlocks(kAmdEntries, ARRAYSIZE(kAmdEntries), !g_hideAmdPanel)) { g_hideAmdPanel = !g_hideAmdPanel; SaveDword(L"HideAmdPanel", g_hideAmdPanel ? 1 : 0); } else MessageBoxW(g_window, T(L"Could not update AMD startup entries.", L"无法更新 AMD 启动入口。"), kAppName, MB_OK | MB_ICONERROR); break;
    case ID_HIDE_SECURITY: { const bool next = !g_hideSecurityCenter; if (SetSecurityCenterHidden(next)) g_hideSecurityCenter = next; else MessageBoxW(g_window, T(L"Could not change the Windows Security tray policy. Try again and allow UAC.", L"无法修改 Windows Security 托盘策略。请重试并允许 UAC。"), kAppName, MB_OK | MB_ICONERROR); break; }
    case ID_OVERLAY_OPEN: OpenOverlayFile(g_window); break;
    case ID_OVERLAY_RESTORE: { std::wstring path; if (!LoadStringSetting(L"OverlayPath", path) || path.empty() || !ShowOverlayFromPath(path, true)) MessageBoxW(g_window, T(L"There is no restorable image.", L"没有可恢复的图片。"), kAppName, MB_OK | MB_ICONINFORMATION); break; }
    case ID_OVERLAY_LOCK: g_overlay.locked = !g_overlay.locked; SaveOverlaySettings(); break;
    case ID_OVERLAY_CLICKTHROUGH: SetOverlayClickThrough(!g_overlay.clickThrough); break;
    case ID_OVERLAY_CLOSE: CloseOverlay(); break;
    case ID_CLEANUP: ShowCleanupWindow(g_window); break;
    case ID_ABOUT: ShowAboutWindow(g_window); break;
    case ID_EXIT: DestroyWindow(g_window); break;
    }
}

void ShowModernMenu(HWND owner) {
    if (g_menuWindow) { DestroyWindow(g_menuWindow); return; }
    g_menuPage = MenuPage::Root;
    const std::vector<MenuRow> rows = BuildMenuRows(g_menuPage);
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const UINT dpi = GetMonitorDpiSafe(monitor, owner);
    const int width = ScaleUi(480, dpi);
    const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
    const int maxHeight = (std::max)(ScaleUi(300, dpi), workHeight / 3);
    const int height = (std::min)(MenuTotalHeight(rows, dpi), maxHeight);
    int x = cursor.x;
    int y = cursor.y - height;
    if (x + width > info.rcWork.right) x = info.rcWork.right - width - ScaleUi(6, dpi);
    if (x < info.rcWork.left) x = info.rcWork.left + ScaleUi(6, dpi);
    if (y < info.rcWork.top) y = info.rcWork.top + ScaleUi(6, dpi);
    g_menuScroll = 0;
    g_menuHover = -1;
    g_menuSelected = -1;
    g_menuWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuClass, kAppName,
                                   WS_POPUP | WS_BORDER, x, y, width, height, owner, nullptr,
                                   g_instance, nullptr);
    if (!g_menuWindow) return;
    ShowWindow(g_menuWindow, SW_SHOWNORMAL);
    SetForegroundWindow(g_menuWindow);
    SetFocus(g_menuWindow);
    SetCapture(g_menuWindow);
    UpdateWindow(g_menuWindow);
}

void RefreshModernMenu() {
    if (g_menuWindow) PostMessageW(g_menuWindow, kMenuRebuild, 0, 0);
}

void HandleTrayMenuEvent(HWND window, UINT event) {
    const DWORD now = GetTickCount();
    // Explorer can report one physical right-click as both WM_RBUTTONUP and
    // WM_CONTEXTMENU. Treat that pair as one invocation; otherwise the
    // second notification immediately toggles the popup back off.
    if (g_lastTrayMenuEventTick != 0 && now - g_lastTrayMenuEventTick < 300) return;
    g_lastTrayMenuEventTick = now;
    if (event == WM_CONTEXTMENU && g_menuWindow) return;
    ShowModernMenu(window);
}

#if 0
LRESULT CALLBACK MenuProcLegacy(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::vector<MenuRow> rows;
    switch (message) {
    case WM_CREATE: SetDarkMode(window, IsDarkTheme()); rows = BuildMenuRows(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC hdc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client); HDC buffer = CreateCompatibleDC(hdc); HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom); HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap)); const bool dark = IsDarkTheme(); HBRUSH background = CreateSolidBrush(dark ? RGB(28, 29, 36) : RGB(249, 249, 252)); FillRect(buffer, &client, background); DeleteObject(background); RECT logo{18, 16, 66, 64}; DrawTrinityLogo(buffer, logo, true); SetBkMode(buffer, TRANSPARENT); SetTextColor(buffer, dark ? RGB(245, 245, 250) : RGB(28, 29, 38)); HFONT titleFont = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); HFONT oldFont = static_cast<HFONT>(SelectObject(buffer, titleFont)); RECT title{78, 18, client.right - 18, 42}; DrawTextW(buffer, L"Liberty", -1, &title, DT_LEFT | DT_SINGLELINE); SelectObject(buffer, oldFont); DeleteObject(titleFont);
        int current = 78 - g_menuScroll; for (size_t index = 0; index < rows.size(); ++index) { const MenuRow& row = rows[index]; const int rowHeight = MenuRowHeight(row); RECT rowRect{8, current, client.right - 8, current + rowHeight}; if (rowRect.bottom >= 0 && rowRect.top <= client.bottom) { if (row.kind == MenuRow::Header) { SetTextColor(buffer, dark ? RGB(143, 151, 177) : RGB(91, 98, 120)); HFONT headerFont = CreateFontW(11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); HFONT old = static_cast<HFONT>(SelectObject(buffer, headerFont)); RECT text{18, rowRect.top + 8, rowRect.right, rowRect.bottom}; DrawTextW(buffer, row.label.c_str(), -1, &text, DT_LEFT | DT_SINGLELINE); SelectObject(buffer, old); DeleteObject(headerFont); } else if (row.kind == MenuRow::Status) { SetTextColor(buffer, dark ? RGB(245, 245, 250) : RGB(28, 29, 38)); RECT detail{78, rowRect.top + 22, rowRect.right - 18, rowRect.bottom}; SetTextColor(buffer, dark ? RGB(155, 160, 178) : RGB(97, 100, 118)); DrawTextW(buffer, row.detail.c_str(), -1, &detail, DT_LEFT | DT_SINGLELINE); } else if (row.kind == MenuRow::Action) { if (static_cast<int>(index) == g_menuHover || static_cast<int>(index) == g_menuSelected) { HBRUSH hover = CreateSolidBrush(dark ? RGB(55, 58, 72) : RGB(231, 236, 248)); FillRect(buffer, &rowRect, hover); DeleteObject(hover); } if (row.checked) { HBRUSH check = CreateSolidBrush(RGB(79, 140, 255)); RECT checkRect{20, rowRect.top + 11, 30, rowRect.top + 21}; FillRect(buffer, &checkRect, check); DeleteObject(check); } SetTextColor(buffer, row.enabled ? (dark ? RGB(245, 245, 250) : RGB(32, 33, 42)) : (dark ? RGB(100, 103, 115) : RGB(170, 171, 180))); RECT text{42, rowRect.top + 8, rowRect.right - 18, rowRect.bottom}; DrawTextW(buffer, row.label.c_str(), -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER); } } current += rowHeight; }
        BitBlt(hdc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY); SelectObject(buffer, oldBitmap); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(window, &paint); return 0;
    }
    case WM_MOUSEMOVE: { TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking); const int hit = MenuHitTest(GET_Y_LPARAM(lParam), rows, g_menuScroll); if (hit != g_menuHover) { g_menuHover = hit; InvalidateRect(window, nullptr, FALSE); } return 0; }
    case WM_MOUSELEAVE: g_menuHover = -1; InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: { RECT client{}; GetClientRect(window, &client); const int maxScroll = (std::max)(0, MenuTotalHeight(rows) - static_cast<int>(client.bottom) + 78); g_menuScroll = std::clamp(g_menuScroll - GET_WHEEL_DELTA_WPARAM(wParam) / 2, 0, maxScroll); InvalidateRect(window, nullptr, FALSE); return 0; }
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_LBUTTONDOWN: {
        RECT client{}; GetClientRect(window, &client); POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!PtInRect(&client, point)) { DestroyWindow(window); return 0; }
        return 0;
    }
    case WM_LBUTTONUP: { const int hit = MenuHitTest(GET_Y_LPARAM(lParam), rows, g_menuScroll); if (hit >= 0 && rows[static_cast<size_t>(hit)].kind == MenuRow::Action && rows[static_cast<size_t>(hit)].enabled) { const UINT command = rows[static_cast<size_t>(hit)].id; DestroyWindow(window); ExecuteCommand(command); } else { RECT client{}; GetClientRect(window, &client); POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; if (!PtInRect(&client, point)) DestroyWindow(window); } return 0; }
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP: DestroyWindow(window); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        if (wParam == VK_DOWN || wParam == VK_UP) { const int direction = wParam == VK_DOWN ? 1 : -1; int index = g_menuSelected < 0 ? (direction > 0 ? 0 : static_cast<int>(rows.size()) - 1) : g_menuSelected + direction; while (index >= 0 && index < static_cast<int>(rows.size()) && rows[static_cast<size_t>(index)].kind != MenuRow::Action) index += direction; if (index >= 0 && index < static_cast<int>(rows.size())) { g_menuSelected = index; InvalidateRect(window, nullptr, FALSE); } return 0; }
        if (wParam == VK_RETURN && g_menuSelected >= 0 && g_menuSelected < static_cast<int>(rows.size())) { const MenuRow& row = rows[static_cast<size_t>(g_menuSelected)]; if (row.kind == MenuRow::Action && row.enabled) { DestroyWindow(window); ExecuteCommand(row.id); } return 0; }
        return 0;
    case WM_ACTIVATE: return 0;
    case WM_CANCELMODE: DestroyWindow(window); return 0;
    case WM_SETTINGCHANGE: SetDarkMode(window, IsDarkTheme()); InvalidateRect(window, nullptr, TRUE); return 0;
    case WM_NCDESTROY: if (GetCapture() == window) ReleaseCapture(); if (g_menuWindow == window) g_menuWindow = nullptr; return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

#endif

LRESULT CALLBACK MenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    static std::vector<MenuRow> rows;
    const UINT dpi = GetWindowDpiSafe(window);
    switch (message) {
    case WM_CREATE:
        SetDarkMode(window, IsDarkTheme());
        rows = BuildMenuRows(g_menuPage);
        return 0;
    case kMenuRebuild:
        rows = BuildMenuRows(g_menuPage);
        g_menuScroll = 0;
        g_menuHover = -1;
        g_menuSelected = -1;
        SetCapture(window);
        SetFocus(window);
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_DPICHANGED:
        SetDarkMode(window, IsDarkTheme());
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC buffer = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(buffer, bitmap));
        const bool dark = IsDarkTheme();
        HBRUSH background = CreateSolidBrush(dark ? RGB(28, 29, 36) : RGB(249, 249, 252));
        FillRect(buffer, &client, background);
        DeleteObject(background);

        RECT logo{ScaleUi(18, dpi), ScaleUi(14, dpi), ScaleUi(78, dpi), ScaleUi(74, dpi)};
        DrawTrinityLogo(buffer, logo, true);
        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, dark ? RGB(245, 245, 250) : RGB(28, 29, 38));
        HFONT titleFont = CreateUiFont(window, 20, FW_SEMIBOLD, true);
        HFONT oldFont = static_cast<HFONT>(SelectObject(buffer, titleFont));
        RECT title{ScaleUi(94, dpi), ScaleUi(18, dpi), client.right - ScaleUi(18, dpi), ScaleUi(48, dpi)};
        DrawTextW(buffer, L"Liberty", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(buffer, oldFont);
        DeleteObject(titleFont);

        const int contentTop = MenuTopHeight(dpi);
        const int contentClip = SaveDC(buffer);
        IntersectClipRect(buffer, 0, contentTop, client.right, client.bottom);
        int current = contentTop - g_menuScroll;
        for (size_t index = 0; index < rows.size(); ++index) {
            const MenuRow& row = rows[index];
            const int rowHeight = MenuRowHeight(row, dpi);
            RECT rowRect{ScaleUi(10, dpi), current, client.right - ScaleUi(10, dpi), current + rowHeight};
            if (rowRect.bottom >= 0 && rowRect.top <= client.bottom) {
                if (row.kind == MenuRow::Header) {
                    SetTextColor(buffer, dark ? RGB(143, 151, 177) : RGB(91, 98, 120));
                    HFONT headerFont = CreateUiFont(window, 12, FW_SEMIBOLD, true);
                    HFONT old = static_cast<HFONT>(SelectObject(buffer, headerFont));
                    RECT text{ScaleUi(20, dpi), rowRect.top + ScaleUi(9, dpi), rowRect.right, rowRect.bottom};
                    DrawTextW(buffer, row.label.c_str(), -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                    SelectObject(buffer, old);
                    DeleteObject(headerFont);
                } else if (row.kind == MenuRow::Status) {
                    SetTextColor(buffer, dark ? RGB(245, 245, 250) : RGB(28, 29, 38));
                    HFONT statusFont = CreateUiFont(window, 15, FW_SEMIBOLD);
                    HFONT old = static_cast<HFONT>(SelectObject(buffer, statusFont));
                    RECT label{ScaleUi(20, dpi), rowRect.top + ScaleUi(8, dpi), rowRect.right - ScaleUi(18, dpi), rowRect.top + ScaleUi(30, dpi)};
                    DrawTextW(buffer, row.label.c_str(), -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                    SelectObject(buffer, old); DeleteObject(statusFont);
                    SetTextColor(buffer, dark ? RGB(170, 175, 190) : RGB(97, 100, 118));
                    HFONT detailFont = CreateUiFont(window, 14, FW_NORMAL);
                    old = static_cast<HFONT>(SelectObject(buffer, detailFont));
                    RECT detail{ScaleUi(20, dpi), rowRect.top + ScaleUi(30, dpi), rowRect.right - ScaleUi(18, dpi), rowRect.bottom};
                    DrawTextW(buffer, row.detail.c_str(), -1, &detail, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                    SelectObject(buffer, old); DeleteObject(detailFont);
                } else if (row.kind == MenuRow::Action || row.kind == MenuRow::Category) {
                    if (static_cast<int>(index) == g_menuHover || static_cast<int>(index) == g_menuSelected) {
                        HBRUSH hover = CreateSolidBrush(dark ? RGB(55, 58, 72) : RGB(231, 236, 248));
                        HPEN pen = CreatePen(PS_NULL, 0, 0);
                        HGDIOBJ oldPen = SelectObject(buffer, pen);
                        HGDIOBJ oldBrush = SelectObject(buffer, hover);
                        RoundRect(buffer, rowRect.left, rowRect.top, rowRect.right, rowRect.bottom,
                                  ScaleUi(8, dpi), ScaleUi(8, dpi));
                        SelectObject(buffer, oldBrush);
                        SelectObject(buffer, oldPen);
                        DeleteObject(hover);
                        DeleteObject(pen);
                    }
                    if (row.checked) {
                        HBRUSH check = CreateSolidBrush(RGB(79, 140, 255));
                        RECT checkRect{ScaleUi(22, dpi), rowRect.top + ScaleUi(16, dpi),
                                       ScaleUi(36, dpi), rowRect.top + ScaleUi(30, dpi)};
                        FillRect(buffer, &checkRect, check);
                        DeleteObject(check);
                    }
                    SetTextColor(buffer, row.enabled ? (dark ? RGB(245, 245, 250) : RGB(32, 33, 42))
                                                       : (dark ? RGB(100, 103, 115) : RGB(170, 171, 180)));
                    HFONT actionFont = CreateUiFont(window, 16, FW_MEDIUM);
                    HFONT old = static_cast<HFONT>(SelectObject(buffer, actionFont));
                    RECT text{ScaleUi(52, dpi), rowRect.top + ScaleUi(8, dpi),
                                rowRect.right - ScaleUi(18, dpi), rowRect.bottom - ScaleUi(4, dpi)};
                    DrawTextW(buffer, row.label.c_str(), -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                    if (row.kind == MenuRow::Category) {
                        SetTextColor(buffer, dark ? RGB(170, 175, 190) : RGB(97, 100, 118));
                        HFONT detailFont = CreateUiFont(window, 13, FW_NORMAL);
                        SelectObject(buffer, detailFont);
                        RECT detail{ScaleUi(52, dpi), rowRect.top + ScaleUi(31, dpi), rowRect.right - ScaleUi(42, dpi), rowRect.bottom};
                        DrawTextW(buffer, row.detail.c_str(), -1, &detail, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                        RECT arrow{rowRect.right - ScaleUi(30, dpi), rowRect.top, rowRect.right - ScaleUi(12, dpi), rowRect.bottom};
                        DrawTextW(buffer, L"›", -1, &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                        DeleteObject(SelectObject(buffer, old));
                    } else {
                        SelectObject(buffer, old);
                    }
                    DeleteObject(actionFont);
                }
            }
            current += rowHeight;
        }
        RestoreDC(buffer, contentClip);
        HBRUSH divider = CreateSolidBrush(dark ? RGB(42, 44, 54) : RGB(225, 228, 236));
        RECT dividerRect{0, contentTop - ScaleUi(4, dpi), client.right, contentTop};
        FillRect(buffer, &dividerRect, divider);
        DeleteObject(divider);
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
        const int hit = MenuHitTest(GET_Y_LPARAM(lParam), rows, g_menuScroll, dpi);
        if (hit != g_menuHover) { g_menuHover = hit; InvalidateRect(window, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_menuHover = -1;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        RECT client{};
        GetClientRect(window, &client);
        const int maxScroll = (std::max)(0, MenuTotalHeight(rows, dpi) - static_cast<int>(client.bottom));
        g_menuScroll = std::clamp(g_menuScroll - GET_WHEEL_DELTA_WPARAM(wParam) / 2, 0, maxScroll);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_LBUTTONDOWN: {
        RECT client{}; GetClientRect(window, &client);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!PtInRect(&client, point)) { DestroyWindow(window); return 0; }
        return 0;
    }
    case WM_LBUTTONUP: {
        const int hit = MenuHitTest(GET_Y_LPARAM(lParam), rows, g_menuScroll, dpi);
        if (hit >= 0 && (rows[static_cast<size_t>(hit)].kind == MenuRow::Action || rows[static_cast<size_t>(hit)].kind == MenuRow::Category) && rows[static_cast<size_t>(hit)].enabled) {
            const UINT command = rows[static_cast<size_t>(hit)].id;
            if (rows[static_cast<size_t>(hit)].kind != MenuRow::Category) DestroyWindow(window);
            ExecuteCommand(command);
        } else {
            RECT client{}; GetClientRect(window, &client);
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!PtInRect(&client, point)) DestroyWindow(window);
        }
        return 0;
    }
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        DestroyWindow(window); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        if (wParam == VK_DOWN || wParam == VK_UP) {
            const int direction = wParam == VK_DOWN ? 1 : -1;
            int index = g_menuSelected < 0 ? (direction > 0 ? 0 : static_cast<int>(rows.size()) - 1) : g_menuSelected + direction;
            while (index >= 0 && index < static_cast<int>(rows.size()) && rows[static_cast<size_t>(index)].kind != MenuRow::Action && rows[static_cast<size_t>(index)].kind != MenuRow::Category) index += direction;
            if (index >= 0 && index < static_cast<int>(rows.size())) { g_menuSelected = index; InvalidateRect(window, nullptr, FALSE); }
            return 0;
        }
        if (wParam == VK_RETURN && g_menuSelected >= 0 && g_menuSelected < static_cast<int>(rows.size())) {
            const MenuRow& row = rows[static_cast<size_t>(g_menuSelected)];
            if ((row.kind == MenuRow::Action || row.kind == MenuRow::Category) && row.enabled) { if (row.kind != MenuRow::Category) DestroyWindow(window); ExecuteCommand(row.id); }
            return 0;
        }
        return 0;
    case WM_ACTIVATE: return 0;
    case WM_CANCELMODE: DestroyWindow(window); return 0;
    case WM_SETTINGCHANGE: SetDarkMode(window, IsDarkTheme()); InvalidateRect(window, nullptr, TRUE); return 0;
    case WM_NCDESTROY:
        if (GetCapture() == window) ReleaseCapture();
        if (g_menuWindow == window) g_menuWindow = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterClasses() {
    WNDCLASSEXW tray{sizeof(tray)};
    tray.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (g_taskbarCreated && message == g_taskbarCreated) { Shell_NotifyIconW(NIM_ADD, &g_tray); g_tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_tray); return 0; }
        switch (message) {
        case WM_TIMER: if (wParam == kOneDriveRefreshTimer && g_blockOneDrive) ApplyOneDriveBlock(true, false); return 0;
        case WM_HOTKEY: if (wParam == 1) { g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled ? 1 : 0); ResetMappedModifierState(); RefreshTrayIcon(); } if (wParam == 2) ScreenOff(); if (wParam == 3) ScheduleShutdown(60 * 60); if (wParam == 4) CancelShutdown(); return 0;
        case kTrayMessage: { const UINT trayEvent = LOWORD(lParam); if (trayEvent == WM_LBUTTONUP) { g_enabled = !g_enabled; SaveDword(L"Enabled", g_enabled ? 1 : 0); ResetMappedModifierState(); RefreshTrayIcon(); } else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) HandleTrayMenuEvent(window, trayEvent); return 0; }
        case WM_SETTINGCHANGE: if (g_menuWindow) InvalidateRect(g_menuWindow, nullptr, TRUE); return 0;
        case WM_DESTROY: KillTimer(window, kOneDriveRefreshTimer); ResetMappedModifierState(); UnregisterHotKey(window, 1); UnregisterHotKey(window, 2); UnregisterHotKey(window, 3); UnregisterHotKey(window, 4); if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; } CloseOverlay(); if (g_cleanupWindow) DestroyWindow(g_cleanupWindow); if (g_startupWindow) DestroyWindow(g_startupWindow); if (g_aboutWindow) DestroyWindow(g_aboutWindow); Shell_NotifyIconW(NIM_DELETE, &g_tray); if (g_tray.hIcon) { DestroyIcon(g_tray.hIcon); g_tray.hIcon = nullptr; } PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    };
    tray.hInstance = g_instance; tray.hCursor = LoadCursorW(nullptr, IDC_ARROW); tray.hIcon = CreateTrinityIcon(true); tray.hIconSm = tray.hIcon; tray.lpszClassName = kWindowClass; if (!RegisterClassExW(&tray)) return false;
    auto registerSimple = [](const wchar_t* name, WNDPROC proc) { WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = proc; cls.hInstance = g_instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); cls.lpszClassName = name; return RegisterClassExW(&cls) != 0; };
    return registerSimple(kMenuClass, MenuProc) && registerSimple(kOverlayClass, OverlayProc) && registerSimple(kCleanupClass, CleanupProc) && registerSimple(kStartupClass, StartupProc) && registerSimple(kAboutClass, AboutProc);
}

void RunCleanupHelper(const std::wstring& path) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); const std::vector<std::wstring> keys = ReadCleanupPlan(path); size_t successCount = 0; const bool success = !keys.empty() && RunCleanupKeys(keys, successCount); CoUninitialize(); ExitProcess(success ? 0 : 1);
}

void RunStartupHelper(const std::wstring& path) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring message;
    const bool success = RunStartupPlan(path, message);
    CoUninitialize();
    ExitProcess(success ? 0 : 1);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argumentCount = 0; LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int index = 1; index < argumentCount; ++index) {
            if (_wcsicmp(arguments[index], L"--apply-security-systray") == 0 && index + 1 < argumentCount) { const bool hidden = _wcsicmp(arguments[index + 1], L"1") == 0; LocalFree(arguments); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); const bool result = SetSecurityCenterHiddenDirect(hidden); CoUninitialize(); return result ? 0 : 1; }
            if (_wcsicmp(arguments[index], L"--cleanup") == 0 && index + 1 < argumentCount) { const std::wstring path = arguments[index + 1]; LocalFree(arguments); RunCleanupHelper(path); return 1; }
            if (_wcsicmp(arguments[index], L"--startup-apply") == 0 && index + 1 < argumentCount) { const std::wstring path = arguments[index + 1]; LocalFree(arguments); RunStartupHelper(path); return 1; }
            if (_wcsicmp(arguments[index], L"--overlay") == 0 && index + 1 < argumentCount) g_initialOverlayPath = arguments[++index];
        }
        LocalFree(arguments);
    }
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\Liberty.SingleInstance"); if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); Gdiplus::GdiplusStartupInput gdiplusInput; Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr); INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls); InitializeLanguage();
    g_enabled = LoadDword(L"Enabled", 1) != 0; g_startAtLogin = IsStartupEnabled(); g_blockOneDrive = LoadDword(L"BlockOneDrive", 0) != 0; g_hideNvidiaPanel = LoadDword(L"HideNvidiaPanel", 0) != 0; g_hideAmdPanel = LoadDword(L"HideAmdPanel", 0) != 0; g_hideSecurityCenter = IsSecurityCenterHidden(); LoadModifierMappings(); g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated"); if (!RegisterClasses()) return 1;
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr); if (!g_window) return 1;
    if (g_blockOneDrive) ApplyOneDriveBlock(true, true); if (g_hideNvidiaPanel) ApplyRunEntryBlocks(kNvidiaEntries, ARRAYSIZE(kNvidiaEntries), true); if (g_hideAmdPanel) ApplyRunEntryBlocks(kAmdEntries, ARRAYSIZE(kAmdEntries), true); SetTimer(g_window, kOneDriveRefreshTimer, 30000, nullptr);
    g_tray.cbSize = sizeof(g_tray); g_tray.hWnd = g_window; g_tray.uID = 1; g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; g_tray.uCallbackMessage = kTrayMessage; g_tray.hIcon = CreateTrinityIcon(g_enabled); UpdateTrayTip(); Shell_NotifyIconW(NIM_ADD, &g_tray); g_tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    RegisterHotKey(g_window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F9); RegisterHotKey(g_window, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10); RegisterHotKey(g_window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F11); RegisterHotKey(g_window, 4, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12); g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    if (!g_hook) { MessageBoxW(nullptr, T(L"Liberty could not install its keyboard hook.", L"Liberty 无法安装键盘钩子。"), kAppName, MB_OK | MB_ICONERROR); DestroyWindow(g_window); return 1; }
    if (!g_initialOverlayPath.empty()) ShowOverlayFromPath(g_initialOverlayPath, false);
    else { std::wstring restoredPath; if (LoadStringSetting(L"OverlayPath", restoredPath) && !restoredPath.empty() && GetFileAttributesW(restoredPath.c_str()) != INVALID_FILE_ATTRIBUTES) ShowOverlayFromPath(restoredPath, true); }
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (g_mutex) { ReleaseMutex(g_mutex); CloseHandle(g_mutex); } if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken); CoUninitialize(); return static_cast<int>(message.wParam);
}
