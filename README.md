# Liberty by Bada

Liberty by Bada is a compact native C++/Win32 tray panel for Windows 10/11 x64.
Version: **0.1.2 RC1** (application file version 0.1.2).

The whole product surface is the single control panel below. Features outside this panel have intentionally been removed.

![Liberty Trinity logo](assets/liberty-trinity.svg)

## Control panel

- **使用MacOS快捷键** — enables macOS-style shortcuts on Windows. Cmd/Alt/Control can each be assigned to a physical key in Settings.
- **关闭显示器并防止电脑休眠** — turns the display off immediately while keeping the computer awake until the switch is turned off.
- **防止电脑休眠** — keeps Windows awake until the switch is turned off.
- **防止自动锁屏** — keeps an already-unlocked session active while enabled; manual locking remains available.
- **清理缓存 →** — opens the built-in Windows Disk Cleanup experience.
- **将截图保存至桌面而不是剪贴板** — saves incoming clipboard screenshot bitmaps as PNG files on the Desktop, then clears that screenshot from the clipboard.
- **定时关机 →** — 15/30/60/120-minute presets, editable 1–10080-minute duration, countdown, and cancellation.
- **设置 →** — controls startup, shortcut mapping, and the interface language.
- The footer opens **About Liberty by Bada**.

The display-off hotkey is Ctrl+Alt+F10. Start with Windows is enabled by default and can be changed in Settings.

The macOS shortcut layer supports common Cmd shortcuts through their Windows equivalents, including copy/paste/select-all, Cmd+Tab, Cmd+Q, Cmd+H/M, Cmd+arrow navigation, Cmd+Space, and Cmd+Shift+3/4. Option+Left/Right/Backspace uses word-wise navigation or deletion. The default physical mapping is left Windows = Cmd, left Alt = Alt/Option, and left Control = Control. Duplicate physical assignments are rejected.

## Design and safety

The tray panel has eight left-aligned rows, native Windows checkbox controls, restrained dividers, and a fixed Liberty by Bada footer. Saved checkbox states are applied during window creation. The parent paint does not overwrite child controls, and the popup uses its own monitor's DPI.

Prevent automatic lock is off by default. While enabled, Liberty requests an awake system and, after 45 seconds of inactivity, submits a zero-distance mouse activity event. It checks the Windows session and input desktop first, never unlocks an already-locked session, and does not change lock, screen-saver, or organization policies. Manual locking, Dynamic Lock, and enforced organization restrictions are not overridden. The display can briefly wake when activity is submitted; if display-off mode is enabled Liberty requests display-off again.

The following earlier experimental features are no longer included in the shipped binary: startup-item management, OneDrive/NVIDIA/AMD/Windows Security changes, desktop image overlay, direct cache purging, and persistent system policy changes. Liberty only manages its own Windows startup entry.

## Scheduled shutdown

Open **定时关机 / Scheduled shutdown**, choose a duration or type a whole number of minutes, then select **开始计时 / Start timer** (Enter also works). The window shows the remaining time and offers **取消关机 / Cancel timer**. **返回 / Back** returns to the menu; closing the window does not cancel the schedule.

Liberty schedules shutdown through [Windows InitiateSystemShutdownExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-initiatesystemshutdownexw), with forced application closure disabled. Unsaved work may block shutdown. Windows owns the countdown, so it survives Liberty exiting. Reopening Liberty during the same Windows session restores the cancellation control. A volatile registry record is discarded when Windows reboots; old timers are never automatically re-created. If shutdown is cancelled by another tool, cancel it once in Liberty to clear its saved display state.

Existing pending Windows shutdowns are not silently replaced. Invalid inputs, missing privilege, and OS errors are reported. The feature does not create scheduled tasks or change power policies.

## Build

Install Visual Studio Build Tools 2022 with **Desktop development with C++** and **CMake tools for Windows**, then run:

    powershell -ExecutionPolicy Bypass -File .\build.ps1

The portable output is Liberty.exe. Both Release x64 and Debug x64 build without warnings.

Run the automated regression suite with `ctest --test-dir build -C Release --output-on-failure`. Tests use an isolated registry branch and never send keyboard input or schedule a real shutdown. CI runs them before publishing.

For an interactive debugging session, use `build\Debug\LibertyDebugSession.exe build\Debug\Liberty.exe --window 180`. The last argument is the debugger duration in seconds; it detaches and leaves Liberty running. `--window` opens the same application menu as a visible window for desktop inspection. The separately invoked `LibertyShutdownIntegration.exe` creates a real 60-minute Windows shutdown, verifies persisted state, and cancels it immediately; it is deliberately excluded from automatic CI tests.

## Privacy and license

Liberty by Bada has no telemetry, updater, network client, installer, service, or scheduled task. Settings are stored under HKCU\Software\LibertyByBada. When startup is enabled, Liberty writes only its own entry under the current user's Windows Run key.

Released under the [MIT License](LICENSE).

## 中文说明

Liberty by Bada 是一个单页 Windows 托盘控制面板。本版在草图的六项功能上增加“定时关机”和“防止自动锁屏”，共八项。菜单使用 Windows 原生复选组件。

设置页提供“开机启动”（默认开启）、界面语言以及 Cmd/Alt/Control 三个物理按键映射。默认映射为左 Windows = Cmd、左 Alt = Alt/Option、左 Control = Control；三个角色不能选择同一个物理按键。

定时关机支持 15/30 分钟、1/2 小时和自定义 1–10080 分钟，开始后显示倒计时并允许取消。按 Enter 开始，按 Esc 或“返回”回到主菜单。关闭窗口或退出 Liberty 后 Windows 仍会执行计划，不会强制关闭有未保存内容的应用。请通过“取消关机”结束倒计时。重新打开 Liberty 可查看本次 Windows 会话中已保存的计划；重启电脑后旧计划不会恢复。

“防止自动锁屏”默认关闭。开启后，在已解锁的会话中定期维持活动，不会移动指针。关闭该开关即停止维持。手动锁定、动态锁和组织强制策略仍然有效；不会修改 Windows 锁屏策略，也不会自动解锁。

本候选版的主菜单、时间输入、开始/取消和返回流程已上机测试。最后一轮 Release 构建被本机 Windows 应用程序控制签名策略拦截，尚未完成最终 Release 的本机再验收；Debug 回归测试通过。详情见 [调试记录](docs/debug-0.1.2.md)。
