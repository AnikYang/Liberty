# Liberty by Bada 0.1.2 — debugging record

Date: 2026-09-11. Host: Windows x64 10.0.26200, Visual Studio 2022 / MSBuild 17.14, Windows SDK 10.0.26100.

## Fixed defects

- First-open checkbox states: `WM_CREATE` previously called `UpdateMenuToggles` while the global menu HWND was still null. All boxes appeared unchecked until any click refreshed them. Establishing the HWND in `WM_NCCREATE` fixes that initialization order. Final regression coverage checks all 32 combinations of the five saved switches.
- Parent painting could overwrite native child controls. The popup now clips child windows and supplies a matching background to native checkbox controls.
- Translated key-up events could escape untranslated if Cmd was released before the other key. Key routes now survive until the matching key-up. Navigation injects extended key flags. Cmd+Tab holds Alt across repeated Tab presses and releases it with Cmd.
- Screenshot encoding could clear newer clipboard data copied while encoding. Clipboard clearing is conditional on the original sequence number still matching.
- Action rows were not reachable as buttons through keyboard navigation. They are now native button controls with accessible labels and keyboard activation; Escape closes the menu without quitting Liberty.
- Popup sizing now uses the hosting monitor's DPI. English toggle captions can wrap. Settings Save applies the language and returns to a refreshed menu.
- Interactive testing found that reopening a 120-minute timer reset the disabled duration field to 60. The final code also persists the selected duration and synchronizes the preset with custom edits.

## Scheduled shutdown

Added a native resource dialog with 15/30/60/120-minute choices and an always-editable custom duration. Accepts only whole numbers 1–10080, shows remaining time, and supports cancellation. The OS owns the timer; closing Liberty does not abandon it. A volatile current-session record allows re-opening Liberty to cancel it and is cleared by Windows at reboot. Scheduling does not force-close unsaved applications.

Implementation uses [InitiateSystemShutdownExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw) and AbortSystemShutdownW. Shutdown privilege is temporarily enabled and restored. Existing pending shutdowns are not automatically replaced. The UI describes the timer's lifetime and reports failure codes.

## Prevent automatic lock

Added an independent, default-off native checkbox. While enabled, the app keeps the system awake and submits zero-distance mouse activity only after 45 seconds idle, only in an unlocked WTS session on the Default input desktop. It does not change Windows policy or unlock a locked desktop. Manual lock and organization policies remain effective. Long-duration unattended lock prevention was not completed after the final Release was blocked.

## Completed verification

- Release x64 and Debug x64 compile without compiler warnings.
- Before the application-control block, CTest passed in both configurations. Final Debug CTest passes for all 32 switch combinations, invalid duration boundaries, independent native clicks, repeated menu lifetimes, GDI handle counts, isolated startup enable/disable, modifier persistence, duplicate rejection, key release order, Cmd+Tab lifetime, and PNG colors/opacity. Final local Release CTest could not start because application control rejected the unsigned executable; that is **not** recorded as a passing test.
- 100 extra create/destroy menu cycles do not grow GDI handles beyond the test tolerance.
- Native Windows debug-event observer ran a Debug application session for 275.5 seconds: no exceptions, no fatal exceptions. This session was deliberately stopped by the test operator so a newer build could be tested; the stop is not a crash.
- The same native debugger ran the regression executable to successful process exit, reporting no exceptions or fatal exceptions.
- A second launch exits while the first Liberty process remains alive and responsive.
- No new Liberty application-error or Defender malware-detection event was found in the inspected testing interval. Application-control/signing rejection is a separate failure described below.
- User preferences and the existing Release startup path were preserved. The integration test uses a separate temporary registry branch.

Real OS integration test (not a mock):

```text
REAL_WINDOWS_SHUTDOWN_SCHEDULED timeout=3600 forceAppsClosed=false
REAL_WINDOWS_SHUTDOWN_CANCELLED result=0 restored=1
OS_VERIFICATION error=1116 expected=1116
```

Error 1116 is `ERROR_NO_SHUTDOWN_IN_PROGRESS`. This was the expected result of checking after cancellation: no shutdown was left pending. The full power-off was deliberately not executed on the user's working computer.

## Completed desktop interaction

The desktop was initially locked. After the user unlocked it, the shipping Release executable was tested on the real desktop under the native debug-event observer:

- Main menu fits, labels do not overlap, and the originally enabled Prevent Sleep checkbox is correct on first open.
- Clicking Prevent Automatic Lock changes only its own saved value. All other existing switches retain their state; the new switch was restored to off after testing.
- Opened the native shutdown dialog; all controls and text fit.
- Typed 0 and pressed Enter: localized validation appears and no shutdown is scheduled.
- Typed 120 and clicked Start: actual Windows shutdown is scheduled, countdown shows 2:00:00, and scheduling controls disable.
- Clicked Back: the main menu returns and Liberty remains alive.
- Reopened shutdown: countdown continues (observed 1:59:24), and Cancel remains available.
- Clicked Cancel: schedule record is cleared and the Start button re-enables. No test shutdown was intentionally left pending.

That 287.9-second interactive session logged 150 first-chance C++ exceptions (`0xe06d7363`) handled by the process and **zero unhandled exceptions**. The application stayed alive and responsive. The available stack capture did not establish their origin; they must not be described as resolved or as proof of a crash. The session was stopped deliberately for the final rebuild.

## Final local execution blocker

The final Release rebuild (including WTS lock-state checking and restored duration display) was blocked by Windows Code Integrity with events 3033/3077: the executable did not meet the active signing requirements. The new Release regression executable and debugger helper were also blocked. Authenticode reports `NotSigned`; no applicable user code-signing certificate was found. No Windows security policy, exclusion, or trust setting was changed.

Final Debug builds and regression tests pass. The final Release compiles, but its last local runtime checks are **blocked**, not passed. Interactive testing of the final duration-restore fix, long idle lock prevention, shortcuts in another application, Windows screenshot UI, Windows 10, and other DPI configurations remains incomplete.

This build is published as a release candidate pending signing-policy resolution and final desktop acceptance.

## Reproduce

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
build\Debug\LibertyDebugSession.exe C:\path\to\Liberty.exe --window 180
```

`LibertyShutdownIntegration.exe` is an explicitly invoked local test that really schedules a 60-minute shutdown and immediately cancels it. It is excluded from automatic CTest and CI. `LibertyRegression` captures synthesized key events inside the test process and uses an isolated registry branch; it does not send keystrokes to other applications or schedule a shutdown.
