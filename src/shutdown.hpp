#pragma once
#include <windows.h>
#include <reason.h>
#include <string>
#include <string_view>

namespace liberty {

constexpr ULONGLONG kFileTimeSecond = 10000000ULL;
constexpr DWORD kMaxShutdownSeconds = 7 * 24 * 60 * 60;

inline ULONGLONG FileTimeTicks(const FILETIME& time) {
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

inline ULONGLONG UtcNowTicks() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return FileTimeTicks(now);
}

inline bool UtcTicksToLocal(ULONGLONG ticks, SYSTEMTIME& local,
                            const DYNAMIC_TIME_ZONE_INFORMATION* zone = nullptr) {
    const FILETIME file{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utc{};
    return FileTimeToSystemTime(&file, &utc) && SystemTimeToTzSpecificLocalTimeEx(zone, &utc, &local);
}

enum class ShutdownTimeError { None, Invalid, Past, TooFar };

inline ShutdownTimeError SecondsUntil(ULONGLONG target, ULONGLONG now, DWORD& seconds) {
    if (target <= now) return ShutdownTimeError::Past;
    const ULONGLONG delta = target - now;
    if (delta > static_cast<ULONGLONG>(kMaxShutdownSeconds) * kFileTimeSecond) return ShutdownTimeError::TooFar;
    // Never round down to an early shutdown, or accidentally request timeout=0.
    seconds = static_cast<DWORD>((delta + kFileTimeSecond - 1) / kFileTimeSecond);
    return ShutdownTimeError::None;
}

inline ShutdownTimeError ResolveShutdownTime(const SYSTEMTIME& local, ULONGLONG now,
    DWORD& seconds, ULONGLONG& target, const DYNAMIC_TIME_ZONE_INFORMATION* zone = nullptr) {
    SYSTEMTIME utc{}, roundTrip{};
    FILETIME file{};
    if (!SystemTimeToFileTime(&local, &file) ||
        !TzSpecificLocalTimeToSystemTimeEx(zone, &local, &utc) || !SystemTimeToFileTime(&utc, &file) ||
        !SystemTimeToTzSpecificLocalTimeEx(zone, &utc, &roundTrip)) return ShutdownTimeError::Invalid;
    // A missing local hour during a DST transition must not silently change the time.
    if (local.wYear != roundTrip.wYear || local.wMonth != roundTrip.wMonth || local.wDay != roundTrip.wDay ||
        local.wHour != roundTrip.wHour || local.wMinute != roundTrip.wMinute ||
        local.wSecond != roundTrip.wSecond || local.wMilliseconds != roundTrip.wMilliseconds)
        return ShutdownTimeError::Invalid;
    target = FileTimeTicks(file);
    return SecondsUntil(target, now, seconds);
}

inline bool ParseShutdownMinutes(std::wstring_view text, DWORD& minutes) {
    if (text.empty() || text.size() > 5) return false;
    DWORD value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        value = value * 10 + static_cast<DWORD>(c - L'0');
    }
    if (value < 1 || value > 10080) return false;
    minutes = value;
    return true;
}

// Enable only the local shutdown privilege and restore its previous state.
class ShutdownPrivilege {
    HANDLE token_ = nullptr;
    TOKEN_PRIVILEGES previous_{};
    bool changed_ = false;
public:
    DWORD error = ERROR_SUCCESS;
    ShutdownPrivilege() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_)) {
            error = GetLastError(); return;
        }
        TOKEN_PRIVILEGES requested{};
        requested.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &requested.Privileges[0].Luid)) {
            error = GetLastError(); return;
        }
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        DWORD bytes = sizeof(previous_);
        SetLastError(ERROR_SUCCESS);
        const BOOL result = AdjustTokenPrivileges(token_, FALSE, &requested, sizeof(previous_), &previous_, &bytes);
        error = GetLastError();
        changed_ = result && error == ERROR_SUCCESS;
        if (!result && error == ERROR_SUCCESS) error = ERROR_PRIVILEGE_NOT_HELD;
    }
    ~ShutdownPrivilege() {
        if (changed_) AdjustTokenPrivileges(token_, FALSE, &previous_, 0, nullptr, nullptr);
        if (token_) CloseHandle(token_);
    }
    ShutdownPrivilege(const ShutdownPrivilege&) = delete;
    ShutdownPrivilege& operator=(const ShutdownPrivilege&) = delete;
};

class ShutdownSchedule {
    HKEY state_ = nullptr;
    ULONGLONG deadline_ = 0;
    DWORD minutes_ = 60;
    ULONGLONG targetUtc_ = 0;
    DWORD atTime_ = 0;

    DWORD StartTarget(ULONGLONG targetUtc, DWORD minutes, bool atTime, const wchar_t* message) {
        if (!state_) return ERROR_INVALID_HANDLE;
        if (Active()) return ERROR_SHUTDOWN_IS_SCHEDULED;
        DWORD seconds = 0;
        if (SecondsUntil(targetUtc, UtcNowTicks(), seconds) != ShutdownTimeError::None) return ERROR_INVALID_TIME;
        ShutdownPrivilege privilege;
        if (privilege.error) return privilege.error;
        const DWORD mode = atTime ? 1 : 0;
        LONG saved = RegSetValueExW(state_, L"Minutes", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&minutes), sizeof(minutes));
        if (!saved) saved = RegSetValueExW(state_, L"AtTime", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&mode), sizeof(mode));
        if (!saved) saved = RegSetValueExW(state_, L"TargetUtc", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&targetUtc), sizeof(targetUtc));
        if (saved) return static_cast<DWORD>(saved);
        // Re-evaluate at submission time; don't round a chosen 21:00 down to whole minutes.
        if (SecondsUntil(targetUtc, UtcNowTicks(), seconds) != ShutdownTimeError::None) return ERROR_INVALID_TIME;
        const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
        saved = RegSetValueExW(state_, L"DeadlineTick", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&deadline), sizeof(deadline));
        if (saved) return static_cast<DWORD>(saved);
        std::wstring mutableMessage(message);
        if (!InitiateSystemShutdownExW(nullptr, mutableMessage.data(), seconds, FALSE, FALSE,
            SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED)) {
            const DWORD error = GetLastError();
            RegDeleteValueW(state_, L"DeadlineTick");
            return error;
        }
        deadline_ = deadline;
        minutes_ = minutes;
        targetUtc_ = targetUtc;
        atTime_ = mode;
        return ERROR_SUCCESS;
    }
public:
    ~ShutdownSchedule() { if (state_) RegCloseKey(state_); }
    DWORD Open(const std::wstring& settingsKey) {
        if (state_) return ERROR_SUCCESS;
        const std::wstring path = settingsKey + L"\\ShutdownSession";
        // Survives Liberty restarts, but never restores a schedule after Windows reboots.
        const LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
            REG_OPTION_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &state_, nullptr);
        if (result != ERROR_SUCCESS) return static_cast<DWORD>(result);
        DWORD type = 0, size = sizeof(deadline_);
        if (RegQueryValueExW(state_, L"DeadlineTick", nullptr, &type,
            reinterpret_cast<BYTE*>(&deadline_), &size) != ERROR_SUCCESS ||
            type != REG_QWORD || size != sizeof(deadline_)) deadline_ = 0;
        DWORD minutesSize = sizeof(minutes_);
        RegGetValueW(state_, nullptr, L"Minutes", RRF_RT_REG_DWORD, nullptr, &minutes_, &minutesSize);
        if (minutes_ < 1 || minutes_ > 10080) minutes_ = 60;
        DWORD targetSize = sizeof(targetUtc_), modeSize = sizeof(atTime_);
        RegGetValueW(state_, nullptr, L"TargetUtc", RRF_RT_REG_QWORD, nullptr, &targetUtc_, &targetSize);
        RegGetValueW(state_, nullptr, L"AtTime", RRF_RT_REG_DWORD, nullptr, &atTime_, &modeSize);
        return ERROR_SUCCESS;
    }
    bool Active() const { return deadline_ != 0; }
    DWORD Minutes() const { return minutes_; }
    bool AtTime() const { return atTime_ == 1; }
    ULONGLONG TargetUtc() const { return targetUtc_; }
    ULONGLONG SecondsRemaining() const {
        const ULONGLONG now = GetTickCount64();
        return deadline_ > now ? (deadline_ - now + 999) / 1000 : 0;
    }
    DWORD Start(DWORD minutes, const wchar_t* message) {
        if (minutes < 1 || minutes > 10080) return ERROR_INVALID_PARAMETER;
        return StartTarget(UtcNowTicks() + static_cast<ULONGLONG>(minutes) * 60 * kFileTimeSecond, minutes, false, message);
    }
    DWORD StartAt(const SYSTEMTIME& local, const wchar_t* message) {
        DWORD seconds = 0;
        ULONGLONG target = 0;
        if (ResolveShutdownTime(local, UtcNowTicks(), seconds, target) != ShutdownTimeError::None) return ERROR_INVALID_TIME;
        return StartTarget(target, (seconds + 59) / 60, true, message);
    }
    DWORD Cancel() {
        if (!Active()) return ERROR_NO_SHUTDOWN_IN_PROGRESS;
        ShutdownPrivilege privilege;
        if (privilege.error) return privilege.error;
        const BOOL result = AbortSystemShutdownW(nullptr);
        const DWORD error = result ? ERROR_SUCCESS : GetLastError();
        if (result || error == ERROR_NO_SHUTDOWN_IN_PROGRESS) {
            deadline_ = 0;
            if (state_) RegDeleteValueW(state_, L"DeadlineTick");
            return ERROR_SUCCESS;
        }
        return error;
    }
};
} // namespace liberty
