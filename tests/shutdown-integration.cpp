// Opt-in test: creates a REAL 60-minute Windows shutdown and cancels it immediately.
// Not registered in CTest/CI. Never leaves a deliberate pending test shutdown.
#include "../src/shutdown.hpp"
#include <cstdio>

int main() {
    liberty::ShutdownSchedule schedule;
    DWORD error = schedule.Open(L"Software\\LibertyByBadaShutdownIntegration");
    if (error) { std::printf("OPEN_FAILED %lu\n", error); return 1; }
    if (schedule.Active()) { std::puts("Existing test schedule; cancel it before retrying."); return 1; }
    error = schedule.Start(60, L"Liberty integration test: 60-minute timer, cancelled immediately.");
    if (error) { std::printf("SCHEDULE_FAILED %lu (no schedule created)\n", error); return 1; }
    struct CancelGuard {
        liberty::ShutdownSchedule& schedule;
        ~CancelGuard() { if (schedule.Active()) schedule.Cancel(); }
    } guard{schedule};
    std::puts("REAL_WINDOWS_SHUTDOWN_SCHEDULED timeout=3600 forceAppsClosed=false");
    std::fflush(stdout);
    liberty::ShutdownSchedule reopened;
    const DWORD reopenedError = reopened.Open(L"Software\\LibertyByBadaShutdownIntegration");
    const bool restored = !reopenedError && reopened.Active() && reopened.SecondsRemaining() > 3500;
    error = schedule.Cancel();
    std::printf("REAL_WINDOWS_SHUTDOWN_CANCELLED result=%lu restored=%d\n", error, restored);
    if (error) return 1;
    liberty::ShutdownPrivilege privilege;
    // Verify the OS itself no longer has a pending shutdown.
    const BOOL abortedAgain = AbortSystemShutdownW(nullptr);
    const DWORD verification = abortedAgain ? ERROR_SUCCESS : GetLastError();
    std::printf("OS_VERIFICATION error=%lu expected=%lu\n", verification, static_cast<DWORD>(ERROR_NO_SHUTDOWN_IN_PROGRESS));
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\LibertyByBadaShutdownIntegration");
    return restored && !abortedAgain && verification == ERROR_NO_SHUTDOWN_IN_PROGRESS ? 0 : 1;
}
