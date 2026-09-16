# 0.1.4 validation — 2026-09-16

Scope: replace the one-time date/time mode with a persistent multi-time daily shutdown schedule. The existing duration countdown remains available.

## Automated regression

- Daily times validate the 00:00–23:59 range, sort, deduplicate and reject more than 24 distinct values.
- Task Scheduler boundaries use the computer's local calendar date and exact 24-hour minute.
- The native dialog exposes the time picker, add/remove controls and a multi-time list.
- Adding `23:30`, `08:05`, and duplicate `08:05` yields exactly `08:05`, `23:30`.
- Switching between duration and daily modes shows only the controls belonging to that mode.
- Existing keyboard, clipboard, power, lock-prevention, settings and GDI-lifetime regressions continue to run unchanged.

## Real Windows validation

Using computer-use on the Debug application:

1. Opened 定时关机 and confirmed the new 每日定时关机 mode.
2. Added `23:58` and `23:59`; both appeared as separate sorted rows.
3. Saved the schedule. The UI reported `每日计划已启用：23:58、23:59`.
4. Queried the actual Windows task. It contained two `CalendarTrigger` entries with `DaysInterval=1`, boundaries `2026-09-16T23:58:00` and `2026-09-16T23:59:00`, and action `Liberty.exe --daily-shutdown`.
5. Disabled the schedule in the UI. The Windows task no longer existed while the two times remained available after restarting Liberty.
6. Deleted both times and saved the empty list. Registry state became disabled with an empty time payload.
7. Windows returned error 1116 when asked to abort shutdown, confirming no shutdown countdown remained.

The final Release integration executable also created a real one-hour-ahead Windows shutdown request, reopened its persisted mode and target, cancelled it immediately, and received the expected `ERROR_NO_SHUTDOWN_IN_PROGRESS (1116)` on the verification attempt.

No test task, test time, or pending shutdown was left on the machine.

## Operating boundary

The daily task uses the current user's interactive token and does not request wake-from-sleep. The PC must be on, signed in and awake. At a trigger, Liberty requests a 60-second non-forced shutdown; unsaved applications may block it. Moving the portable executable requires saving the schedule again to update the task action path.
