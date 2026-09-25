# 0.1.6 validation — 2026-09-25

Scope: tray-popup lifetime and focus behavior.

## Regression

- A newly created menu starts with auto-close disarmed.
- An initial `WA_INACTIVE` before activation does not queue a close, preventing the prior flash-away bug.
- The first active notification arms auto-close.
- A later inactive notification queues the menu-close message.
- Existing shutdown-plan, switch, shortcut, screenshot and GDI lifetime tests continue to run.

## Intended desktop behavior

- The tray popup closes when focus moves to another top-level application or the desktop.
- Clicking child controls does not count as top-level deactivation.
- Opening Settings or Scheduled Shutdown destroys the popup first and opens the selected work window.
- Settings and Scheduled Shutdown do not auto-close on focus loss, preserving in-progress edits.
- About closes the popup before showing its message window.

## Real desktop verification

- Started the final Release with the popup visible and confirmed it remained open long enough for interaction; no startup flash-close occurred.
- Activated an existing ordinary application window. The Liberty popup disappeared from the targetable-window list immediately.
- Reopened Liberty, opened the Scheduled Shutdown manager, then activated the other application. The manager remained present, preserving its controls and the current `22:30` plan.
- Activated the manager again and pressed Esc. It returned to the main popup. Activating the other application then closed that popup as expected.
- The existing `22:30` Windows task was only read during testing and was not edited, disabled, or deleted.
