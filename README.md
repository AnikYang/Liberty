# Liberty by Bada

Liberty by Bada is a compact native C++/Win32 tray panel for Windows 10/11 x64.
Version: **0.1.6**.

The whole product surface is the single control panel below. Features outside this panel have intentionally been removed.

![Liberty Trinity logo](assets/liberty-trinity.svg)

## Control panel

- **使用MacOS快捷键** — enables macOS-style shortcuts on Windows. Cmd/Alt/Control can each be assigned to a physical key in Settings.
- **关闭显示器并防止电脑休眠** — turns the display off immediately while keeping the computer awake until the switch is turned off.
- **防止电脑休眠** — keeps Windows awake until the switch is turned off.
- **防止自动锁屏** — keeps an already-unlocked session active while enabled; manual locking remains available.
- **清理缓存 →** — opens the built-in Windows Disk Cleanup experience.
- **将截图保存至桌面而不是剪贴板** — saves incoming clipboard screenshot bitmaps as PNG files on the Desktop, then clears that screenshot from the clipboard.
- **定时关机 →** — view and manage the plans that are actually registered in Windows, or use a one-time 1–10080-minute countdown.
- **设置 →** — controls startup, shortcut mapping, and the interface language.
- The footer opens **About Liberty by Bada**.

The display-off hotkey is Ctrl+Alt+F10. Start with Windows is enabled by default and can be changed in Settings.

The macOS shortcut layer supports common Cmd shortcuts through their Windows equivalents, including copy/paste/select-all, Cmd+Tab, Cmd+Q, Cmd+H/M, Cmd+arrow navigation, Cmd+Space, and Cmd+Shift+3/4. Option+Left/Right/Backspace uses word-wise navigation or deletion. The default physical mapping is left Windows = Cmd, left Alt = Alt/Option, and left Control = Control. Duplicate physical assignments are rejected.

## Design and safety

The tray panel has eight left-aligned rows, native Windows checkbox controls, restrained dividers, and a fixed Liberty by Bada footer. Saved checkbox states are applied during window creation. The parent paint does not overwrite child controls, and the popup uses its own monitor's DPI.

The tray popup closes automatically when another application or the desktop receives focus. Auto-close is armed only after the popup has successfully activated, preventing the earlier open-then-flash-away race. Settings and shutdown-plan windows are normal work windows and remain open while editing; Back, Close, or Esc dismisses them.

Prevent automatic lock is off by default. While enabled, Liberty requests an awake system and, after 45 seconds of inactivity, submits a zero-distance mouse activity event. It checks the Windows session and input desktop first, never unlocks an already-locked session, and does not change lock, screen-saver, or organization policies. Manual locking, Dynamic Lock, and enforced organization restrictions are not overridden. The display can briefly wake when activity is submitted; if display-off mode is enabled Liberty requests display-off again.

The following earlier experimental features are no longer included in the shipped binary: startup-item management, OneDrive/NVIDIA/AMD/Windows Security changes, desktop image overlay, direct cache purging, and persistent system policy changes. Liberty only manages its own Windows startup entry.

## Scheduled shutdown

Open **定时关机 / Scheduled shutdown** and choose a mode:

- **每日定时关机 / Daily schedule**: the main menu shows whether the Windows plan is enabled and how many entries exist. The manager reads the real Windows Task Scheduler task and shows each time, enabled/disabled state and next occurrence. It also shows the task's next run and warns when the executable path needs repair.
- **Manage plans**: **添加 / Add** applies and enables a new time immediately. Select an item to load it into the time field, then use **修改 / Edit** or **删除 / Remove**; changes take effect immediately. **刷新 / Refresh** re-reads Windows and imports changes made outside Liberty. **停用全部 / Disable all** removes the Windows task but keeps the saved rows; **启用／修复 / Enable / repair** recreates the task and updates its executable path.
- **倒计时关机 / After a duration**: choose 15/30/60/120 minutes or enter 1–10080 whole minutes, then click **开始计时 / Start timer**.

The daily schedule follows the computer's local time zone. At each selected time, Task Scheduler starts Liberty with a private `--daily-shutdown` action and Liberty begins a 60-second Windows shutdown countdown. The computer must be on, signed in and awake at the trigger time; Liberty does not wake a sleeping or powered-off computer. Moving the portable EXE causes the manager to show a path warning; click **Enable / repair** to update it. Unsaved applications are not forced closed and may block shutdown.

Windows Task Scheduler is the source of truth for enabled plans. When Liberty opens the menu or manager it reconciles its saved list with the actual task, so a valid task remains visible even if the app's registry state is missing or stale. Disabled rows are retained locally for re-enabling. If the task cannot be read, the manager displays the Windows error instead of claiming that no plans exist.

The duration mode remains a one-time Windows countdown. **取消关机 / Cancel timer** cancels that countdown; **返回 / Back** closes the panel without cancelling it. Existing pending Windows shutdowns are never silently replaced.

Liberty submits shutdown through [Windows InitiateSystemShutdownExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw), with forced application closure disabled. The daily schedule is one current-user Task Scheduler task with one daily trigger per listed time; it runs only while that user is logged on and does not request wake-from-sleep. The one-time countdown uses volatile session state and is discarded at reboot.

Invalid inputs, missing privilege and Task Scheduler errors are shown in the interface. Liberty changes only its own scheduled task and shutdown countdown; it does not change Windows power or lock policies.

## Build

Install Visual Studio Build Tools 2022 with **Desktop development with C++** and **CMake tools for Windows**, then run:

    powershell -ExecutionPolicy Bypass -File .\build.ps1

The portable output is Liberty.exe. Both Release x64 and Debug x64 build without warnings.

Run the automated regression suite with `ctest --test-dir build -C Release --output-on-failure`. Tests use an isolated registry branch and never send keyboard input or schedule a real shutdown. CI runs them before publishing.

For an interactive debugging session, use `build\Debug\LibertyDebugSession.exe build\Debug\Liberty.exe --window 180`. The last argument is the debugger duration in seconds; it detaches and leaves Liberty running. `--window` opens the same application menu as a visible window for desktop inspection. The separately invoked `LibertyShutdownIntegration.exe` creates a real 60-minute Windows shutdown, verifies persisted state, and cancels it immediately. Add `--at-time` to test a specific local time one hour ahead. Both real-shutdown tests are deliberately excluded from automatic CI tests.

## Privacy and license

Liberty by Bada has no telemetry, updater, network client, installer, or service. Settings are stored under HKCU\Software\LibertyByBada. When startup is enabled, Liberty writes its own entry under the current user's Windows Run key. Enabling daily shutdown creates only `Liberty by Bada - Daily Shutdown` in the current user's Windows Task Scheduler; disabling or clearing the plan removes it.

Released under the [MIT License](LICENSE).

## 中文说明

Liberty by Bada 是一个单页 Windows 托盘控制面板。本版在草图的六项功能上增加“定时关机”和“防止自动锁屏”，共八项。菜单使用 Windows 原生复选组件。

设置页提供“开机启动”（默认开启）、界面语言以及 Cmd/Alt/Control 三个物理按键映射。默认映射为左 Windows = Cmd、左 Alt = Alt/Option、左 Control = Control；三个角色不能选择同一个物理按键。

定时关机支持 15/30 分钟、1/2 小时和自定义 1–10080 分钟，开始后显示倒计时并允许取消。按 Enter 开始，按 Esc 或“返回”回到主菜单。关闭窗口或退出 Liberty 后 Windows 仍会执行计划，不会强制关闭有未保存内容的应用。请通过“取消关机”结束倒计时。重新打开 Liberty 可查看本次 Windows 会话中已保存的计划；重启电脑后旧计划不会恢复。

“每日定时关机”使用电脑本地时区和 24 小时制。主菜单直接显示计划是否启用及数量；管理窗口从 Windows 任务计划读取实际生效内容，逐条显示时间、启用状态和下一次执行时间，顶部显示系统任务状态及最近的下一次执行。

选择时间后点“添加”会立即启用；选中已有计划后可“修改”或“删除”，操作立即写入 Windows。“刷新”会重新读取系统任务并导入外部修改。“停用全部”删除系统任务但保留时间列表；“启用／修复”会重新创建任务，并在移动 Liberty.exe 后修复程序路径。列表自动排序、去重，最多 24 条。

“倒计时关机”仍是一轮有效的临时计划，并可随时取消。Windows 任务是启用状态和实际时间的真实来源；Liberty 会在打开菜单或管理窗口时自动同步，避免出现系统已有计划但软件内看不到的情况。

“防止自动锁屏”默认关闭。开启后，在已解锁的会话中定期维持活动，不会移动指针。关闭该开关即停止维持。手动锁定、动态锁和组织强制策略仍然有效；不会修改 Windows 锁屏策略，也不会自动解锁。

0.1.6 修复托盘主菜单失去焦点后仍停留的问题。主菜单成功激活后，点击其他应用或桌面会自动收起；首次显示期间的无效失焦消息不会触发关闭，避免菜单一闪而过。设置和关机计划管理窗口不会因失焦丢失编辑内容。详情见 [本版测试记录](docs/testing-0.1.6.md)。
