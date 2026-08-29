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
#include <powrprof.h>

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
constexpr wchar_t kRegistryKey[] = L"Software\\LibertyByBada";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowMenuMessage = WM_APP + 2;
constexpr UINT_PTR kSleepTimer = 11;
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
    ID_SETTINGS_CLOSE
};

struct MenuItem {
    UINT id;
    bool toggle;
    const wchar_t* english;
    const wchar_t* chinese;
};

constexpr MenuItem kMenuItems[] = {
    {ID_MAC_MAPPING, true,  L"Map shortcuts to macOS", L"将快捷键映射至 macOS"},
    {ID_SLEEP_WITH_DISPLAY, true, L"Sleep computer when display turns off", L"关闭显示器时让电脑休眠"},
    {ID_PREVENT_SLEEP, true, L"Prevent computer sleep", L"防止电脑休眠"},
    {ID_CLEAR_CACHE, false, L"Clear cache", L"清理缓存"},
    {ID_SAVE_SCREENSHOTS, true, L"Save screenshots to Desktop instead of clipboard", L"将截图保存至桌面而不是剪贴板"},
    {ID_SETTINGS, false, L"Settings", L"设置"}
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_menuWindow = nullptr;
HWND g_settingsWindow = nullptr;
HHOOK g_keyboardHook = nullptr;
HANDLE g_mutex = nullptr;
NOTIFYICONDATAW g_tray{};
ULONG_PTR g_gdiplusToken = 0;
UINT g_taskbarCreated = 0;
HFONT g_menuFont = nullptr;
HFONT g_settingsFont = nullptr;

bool g_chinese = true;
bool g_macMapping = false;
bool g_sleepWithDisplay = false;
bool g_preventSleep = false;
bool g_saveScreenshots = false;
bool g_commandDown = false;
int g_menuHover = -1;
DWORD g_lastClipboardSequence = 0;
#ifdef _DEBUG
bool g_previewMenu = false;
#endif

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
    SetThreadExecutionState(g_preventSleep ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
}

void TurnOffDisplay() {
    PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    if (g_sleepWithDisplay && g_window) SetTimer(g_window, kSleepTimer, 600, nullptr);
}

void SendKey(WORD virtualKey, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    input.ki.dwExtraInfo = kInjectedMarker;
    SendInput(1, &input, sizeof(input));
}

void SendControlCombo(WORD virtualKey, bool down) {
    SendKey(VK_CONTROL, true);
    SendKey(virtualKey, down);
    SendKey(VK_CONTROL, false);
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
    if (key == VK_LWIN) {
        g_commandDown = down;
        return 1;
    }
    if (!g_commandDown) return CallNextHookEx(g_keyboardHook, code, message, data);
    if (key == VK_TAB) {
        SendKey(VK_MENU, true); SendKey(VK_TAB, down); if (up) SendKey(VK_MENU, false);
        return 1;
    }
    if (key == 'Q') {
        SendKey(VK_MENU, true); SendKey(VK_F4, down); if (up) SendKey(VK_MENU, false);
        return 1;
    }
    if (key == 'H' || key == 'M') {
        if (down) MinimizeForeground();
        return 1;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
        SendKey(key == VK_LEFT ? VK_HOME : VK_END, down);
        return 1;
    }
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
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        g_commandDown = false;
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
    CloseClipboard();
    if (!copy) return;
    const bool saved = SaveBitmapPng(copy, DesktopPngPath());
    DeleteObject(copy);
    if (saved) {
        if (OpenClipboard(g_window)) {
            EmptyClipboard();
            CloseClipboard();
        }
        g_lastClipboardSequence = GetClipboardSequenceNumber();
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
                T(L"Liberty by Bada 0.1.1\n\nA small Windows control panel.", L"Liberty by Bada 0.1.1\n\n一个简约的 Windows 控制面板。"),
                kAppName, MB_OK | MB_ICONINFORMATION);
}

void UpdateMenuToggles() {
    if (!g_menuWindow) return;
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_MAC_MAPPING), g_macMapping ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_SLEEP_WITH_DISPLAY), g_sleepWithDisplay ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_PREVENT_SLEEP), g_preventSleep ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(g_menuWindow, ID_SAVE_SCREENSHOTS), g_saveScreenshots ? BST_CHECKED : BST_UNCHECKED);
}

bool IsToggle(UINT id) {
    return id == ID_MAC_MAPPING || id == ID_SLEEP_WITH_DISPLAY ||
           id == ID_PREVENT_SLEEP || id == ID_SAVE_SCREENSHOTS;
}

void ApplyToggle(UINT id) {
    switch (id) {
    case ID_MAC_MAPPING:
        SetMacMapping(!g_macMapping);
        break;
    case ID_SLEEP_WITH_DISPLAY:
        g_sleepWithDisplay = !g_sleepWithDisplay;
        SaveDword(L"SleepWithDisplay", g_sleepWithDisplay ? 1 : 0);
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
    }
    UpdateMenuToggles();
    UpdateTrayTip();
}

void ExecuteMenuAction(UINT id) {
    if (IsToggle(id)) {
        ApplyToggle(id);
        return;
    }
    if (id == ID_CLEAR_CACHE) {
        DestroyWindow(g_menuWindow);
        OpenCacheCleanup();
    } else if (id == ID_SETTINGS) {
        DestroyWindow(g_menuWindow);
        if (!g_settingsWindow) {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            g_settingsWindow = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, T(L"Settings", L"设置"),
                                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                                work.left + 80, work.top + 80, Scale(360), Scale(210),
                                                g_window, nullptr, g_instance, nullptr);
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
    for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
        const MenuItem& item = kMenuItems[index];
        if (!item.toggle) continue;
        RECT row = MenuRowRect(window, static_cast<int>(index));
        HWND checkbox = CreateWindowExW(0, L"BUTTON", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                        row.right - MulDiv(38, dpi, 96), row.top + MulDiv(10, dpi, 96),
                                        MulDiv(28, dpi, 96), MulDiv(30, dpi, 96),
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
    case WM_CREATE:
        SetRoundedWindow(window);
        BuildMenuControls(window);
        return 0;
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
            if (g_menuHover == static_cast<int>(index)) {
                HBRUSH hover = CreateSolidBrush(RGB(239, 244, 255));
                FillRect(buffer, &row, hover);
                DeleteObject(hover);
            }
            RECT text{row.left + Scale(14, window), row.top, row.right - Scale(item.toggle ? 54 : 42, window), row.bottom};
            DrawTextW(buffer, T(item.english, item.chinese), -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            if (!item.toggle) {
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
        if (HIWORD(wParam) == BN_CLICKED && IsToggle(LOWORD(wParam))) {
            ApplyToggle(LOWORD(wParam));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT footer = MenuFooterRect(window);
        if (PtInRect(&footer, point)) { ShowAbout(); return 0; }
        for (size_t index = 0; index < ARRAYSIZE(kMenuItems); ++index) {
            RECT row = MenuRowRect(window, static_cast<int>(index));
            if (!PtInRect(&row, point)) continue;
            const MenuItem& item = kMenuItems[index];
            if (item.toggle) ApplyToggle(item.id);
            else ExecuteMenuAction(item.id);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        return 0;
    case WM_ACTIVATE:
        return 0;
    case WM_CANCELMODE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        ClearMenuControls(window);
        if (GetCapture() == window) ReleaseCapture();
        if (g_menuWindow == window) g_menuWindow = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetRoundedWindow(window);
        g_settingsFont = CreateUiFont(window, 15);
        CreateWindowExW(0, L"STATIC", T(L"Interface language", L"界面语言"),
                        WS_CHILD | WS_VISIBLE, Scale(22, window), Scale(22, window), Scale(280, window), Scale(28, window),
                        window, nullptr, g_instance, nullptr);
        HWND chinese = CreateWindowExW(0, L"BUTTON", T(L"Use Simplified Chinese", L"使用简体中文"),
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       Scale(22, window), Scale(56, window), Scale(280, window), Scale(34, window),
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANGUAGE)), g_instance, nullptr);
        CreateWindowExW(0, L"BUTTON", T(L"Close", L"关闭"),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        Scale(210, window), Scale(120, window), Scale(100, window), Scale(34, window),
                        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SETTINGS_CLOSE)), g_instance, nullptr);
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SetWindowTheme(child, L"Explorer", nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_settingsFont), TRUE);
        }
        Button_SetCheck(chinese, g_chinese ? BST_CHECKED : BST_UNCHECKED);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LANGUAGE && HIWORD(wParam) == BN_CLICKED) {
            g_chinese = Button_GetCheck(GetDlgItem(window, ID_LANGUAGE)) == BST_CHECKED;
            SaveDword(L"Language", g_chinese ? 1 : 0);
            SetWindowTextW(window, T(L"Settings", L"设置"));
            return 0;
        }
        if (LOWORD(wParam) == ID_SETTINGS_CLOSE) { DestroyWindow(window); return 0; }
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
    const int height = Scale(408);
    int x = cursor.x - width + Scale(16);
    int y = cursor.y - height;
    if (x < info.rcWork.left + Scale(8)) x = info.rcWork.left + Scale(8);
    if (x + width > info.rcWork.right - Scale(8)) x = info.rcWork.right - width - Scale(8);
    if (y < info.rcWork.top + Scale(8)) y = info.rcWork.top + Scale(8);
    g_menuHover = -1;
#ifdef _DEBUG
    const DWORD extendedStyle = g_previewMenu ? WS_EX_APPWINDOW : WS_EX_TOPMOST;
    const DWORD windowStyle = WS_POPUP | WS_BORDER;
    HWND menuOwner = g_previewMenu ? nullptr : owner;
#else
    const DWORD extendedStyle = WS_EX_TOPMOST;
    const DWORD windowStyle = WS_POPUP | WS_BORDER;
    HWND menuOwner = owner;
#endif
    g_menuWindow = CreateWindowExW(extendedStyle, kMenuClass, kAppName,
                                   windowStyle, x, y, width, height, menuOwner, nullptr, g_instance, nullptr);
    if (!g_menuWindow) return;
    ShowWindow(g_menuWindow, SW_SHOWNORMAL);
    SetForegroundWindow(g_menuWindow);
    SetFocus(g_menuWindow);
    UpdateWindow(g_menuWindow);
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
        case WM_TIMER:
            if (wParam == kSleepTimer) {
                KillTimer(window, kSleepTimer);
                SetSuspendState(FALSE, FALSE, FALSE);
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == 1) TurnOffDisplay();
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
            RemoveClipboardFormatListener(window);
            ApplyPreventSleep();
            UnregisterHotKey(window, 1);
            if (g_keyboardHook) { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = nullptr; }
            if (g_menuWindow) DestroyWindow(g_menuWindow);
            if (g_settingsWindow) DestroyWindow(g_settingsWindow);
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
#ifdef _DEBUG
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        for (int index = 1; index < argumentCount; ++index)
            if (_wcsicmp(arguments[index], L"--preview-menu") == 0) g_previewMenu = true;
        LocalFree(arguments);
    }
#endif
    g_mutex = CreateMutexW(nullptr, FALSE, L"Local\\LibertyByBada.SingleInstance");
    if (!g_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) PostMessageW(existing, kShowMenuMessage, 0, 0);
        CloseHandle(g_mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    InitializeLanguage();
    g_macMapping = LoadDword(L"MacMapping", 0) != 0;
    g_sleepWithDisplay = LoadDword(L"SleepWithDisplay", 0) != 0;
    g_preventSleep = LoadDword(L"PreventSleep", 0) != 0;
    g_saveScreenshots = LoadDword(L"SaveScreenshots", 0) != 0;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterClasses()) return 1;

    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    AddClipboardFormatListener(g_window);
    g_lastClipboardSequence = GetClipboardSequenceNumber();
    ApplyPreventSleep();
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
#ifdef _DEBUG
    if (g_previewMenu) ShowMenu(g_window);
#endif

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    if (g_mutex) CloseHandle(g_mutex);
    return static_cast<int>(message.wParam);
}
