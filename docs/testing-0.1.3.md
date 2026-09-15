# 0.1.3 validation — 2026-09-15

Scope: scheduled shutdown at a specific local date/time. No changes to the other feature implementations.

## Automated checks

Debug x64 and Release x64 build without compiler warnings, and CTest passes in both configurations. Added regression coverage for:

- 20:59:42 → 21:00:00 = 18 seconds, not a whole-minute delay.
- Tomorrow 08:00 across midnight; year rollover; valid leap day; invalid calendar date.
- A past instant and an instant equal to now cannot produce an immediate shutdown.
- Fractional-second waits round upward, never to timeout zero.
- Exactly seven days accepted; beyond seven days rejected.
- UTC+8 local time conversion and restoration from stored UTC.
- The actual resource dialog loads its native date/time controls.
- Switching modes displays only the corresponding controls; reading HH:mm gives seconds/milliseconds zero.
- Existing switch, modifier, startup and screenshot regression coverage remains passing.

## Real Windows integration

Invoked `LibertyShutdownIntegration.exe --at-time`: created an actual one-hour-ahead local-time shutdown with forced application closure disabled, reopened the saved mode/UTC target, and immediately cancelled it. Result:

```text
REAL_WINDOWS_SHUTDOWN_SCHEDULED timeout=3600 forceAppsClosed=false
RESTORED_MODE atTime=1
REAL_WINDOWS_SHUTDOWN_CANCELLED result=0 restored=1
OS_VERIFICATION error=1116 expected=1116
```

1116 is `ERROR_NO_SHUTDOWN_IN_PROGRESS`, expected after cancellation. No full shutdown was performed.

## Real desktop interaction

The locally built Release started successfully. Inspected the bilingual-ready native layout in the Chinese interface; date, 24-hour time, both modes, explanatory text and bottom actions fit the window.

Used the native time field to set **2026-09-15 21:00**, scheduled it and observed the exact target with a live countdown. Returned to the menu, reopened the dialog and verified the same date, time and mode were restored. Cancelled the test schedule and confirmed the pending deadline was removed. An additional test activation was also cancelled. No pending test shutdown was left.

The final text clarifies that a pre-filled time is only a preview until Schedule is clicked. The final handler commits a partially typed time field before reading it and makes calendar Enter close the calendar without scheduling. Final Debug/Release regression tests were repeated after those changes.

## Practical limits

Windows owns a one-time countdown. The specified time is converted using the current local time-zone rules at submission; the delay rounds upward by less than one second. No automatic rescheduling after a system-clock change, no waking a sleeping/powered-off PC, and no forced closure of unsaved apps. DST gaps are rejected by round-trip validation; ambiguous repeated times follow Windows' conversion choice.

Native controls follow [Microsoft date/time picker documentation](https://learn.microsoft.com/en-us/windows/win32/controls/date-and-time-picker-controls). Local-to-UTC conversion follows [TzSpecificLocalTimeToSystemTimeEx](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-tzspecificlocaltimetosystemtimeex).
