# 0.1.5 validation — 2026-09-25

Scope: make existing shutdown plans visible and manageable while keeping Windows Task Scheduler as the source of truth.

## Automated regression

- Parses daily trigger boundaries such as `2026-09-16T01:15:00` and boundaries carrying a time-zone suffix.
- Rejects malformed and out-of-range task boundaries.
- The native manager exposes Add, Edit, Remove and Refresh controls.
- Add immediately persists and enables a plan in the isolated test state.
- Edit replaces the selected time; Remove deletes only the selected time.
- Refresh restores the persisted list.
- Disable-all retains visible saved rows; Enable-all restores them.
- Existing switch independence, shortcut, screenshot, window-lifetime and time validation checks remain enabled.

Regression builds use `Software\LibertyByBadaRegression` and do not alter the user's Windows task.

## Real desktop, read-only plan verification

The host had an enabled `Liberty by Bada - Daily Shutdown` Windows task with triggers at `01:15` and `01:30`. Liberty 0.1.5 read this task directly and displayed:

- Main menu: `定时关机 · 已启用 2`.
- Manager header: task enabled, two plans, next execution `2026-09-26 01:15`.
- Rows: `01:15` and `01:30`, each marked enabled, daily, with its next occurrence.
- Selecting `01:15` copied it into the editor and enabled Edit and Remove.
- Refresh re-read the task and preserved both rows.

The Debug executable correctly warned that the task still pointed to the Release executable. The final Release uses that same path, so it does not require repair. Neither existing plan was edited, disabled or removed during this verification.

## Reconciliation behavior

On menu/manager open, Liberty queries the registered Windows task. Valid trigger times and enabled state replace stale app registry values and are written back to the app state. If the task is absent, saved rows remain visible as disabled. If Task Scheduler cannot be queried, the HRESULT is displayed rather than showing an empty-success state.
