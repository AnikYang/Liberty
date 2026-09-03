# Liberty by Bada

Liberty by Bada is a compact native C++/Win32 tray panel for Windows 10/11 x64.
Version: **0.1.1**.

The whole product surface is the single control panel below. Features outside this panel have intentionally been removed.

![Liberty Trinity logo](assets/liberty-trinity.svg)

## Control panel

- **使用MacOS快捷键** — enables macOS-style shortcuts on Windows. Cmd/Alt/Control can each be assigned to a physical key in Settings.
- **关闭显示器并防止电脑休眠** — turns the display off immediately while keeping the computer awake until the switch is turned off.
- **防止电脑休眠** — keeps Windows awake until the switch is turned off.
- **清理缓存 →** — opens the built-in Windows Disk Cleanup experience.
- **将截图保存至桌面而不是剪贴板** — saves incoming clipboard screenshot bitmaps as PNG files on the Desktop, then clears that screenshot from the clipboard.
- **设置 →** — controls startup, shortcut mapping, and the interface language.
- The footer opens **About Liberty by Bada**.

The display-off hotkey is Ctrl+Alt+F10. Start with Windows is enabled by default and can be changed in Settings.

The macOS shortcut layer supports common Cmd shortcuts through their Windows equivalents, including copy/paste/select-all, Cmd+Tab, Cmd+Q, Cmd+H/M, Cmd+arrow navigation, Cmd+Space, and Cmd+Shift+3/4. Option+Left/Right/Backspace uses word-wise navigation or deletion. The default physical mapping is left Windows = Cmd, left Alt = Alt/Option, and left Control = Control. Duplicate physical assignments are rejected.

## Design and safety

The tray panel is intentionally small: six left-aligned rows, native Windows checkbox controls on the right, two disclosure arrows, restrained dividers, and a fixed Liberty by Bada footer. It uses a native Win32 popup with the Windows system font and controls rather than a custom drawn fake switch.

The following earlier experimental features are no longer included in the shipped binary: startup-item management, OneDrive/NVIDIA/AMD/Windows Security changes, desktop image overlay, timed shutdown, direct cache purging, and persistent system policy changes. Liberty only manages its own Windows startup entry. This keeps the app focused and substantially reduces its security and antivirus heuristic surface.

## Build

Install Visual Studio Build Tools 2022 with **Desktop development with C++** and **CMake tools for Windows**, then run:

    powershell -ExecutionPolicy Bypass -File .\build.ps1

The portable output is Liberty.exe. Both Release x64 and Debug x64 build without warnings.

## Privacy and license

Liberty by Bada has no telemetry, updater, network client, installer, service, or scheduled task. Settings are stored under HKCU\Software\LibertyByBada. When startup is enabled, Liberty writes only its own entry under the current user's Windows Run key.

Released under the [MIT License](LICENSE).

## 中文说明

Liberty by Bada 是一个单页 Windows 托盘控制面板。菜单只保留六项草图功能：使用 MacOS 快捷键、关闭显示器并防止电脑休眠、防止休眠、打开 Windows 缓存清理、将截图保存到桌面、设置。菜单以 Windows 原生复选组件呈现开关，右侧箭头只用于“清理缓存”和“设置”。

设置页提供“开机启动”（默认开启）、界面语言以及 Cmd/Alt/Control 三个物理按键映射。默认映射为左 Windows = Cmd、左 Alt = Alt/Option、左 Control = Control；三个角色不能选择同一个物理按键。开关使用各自独立的原生控件和独立状态，不会再出现开启一项同时触发其他项的问题。
