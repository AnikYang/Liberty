# Liberty

![Liberty Trinity logo](assets/liberty-trinity.svg)

Liberty is a portable native C++/Win32 utility for Windows 10/11 x64. It combines macOS-style keyboard muscle memory, a modern tray menu, reversible startup controls, a desktop image pin, and a conservative Windows cleanup scanner in one `Liberty.exe`.

Liberty does not ship a .NET, Python, Node, Qt, or other runtime. The release executable is built with the static MSVC runtime and uses documented Windows APIs.

## Features

- Windows 11-inspired custom tray popup: rounded/DWM-aware presentation, larger readable spacing, hover states, keyboard navigation, per-monitor DPI scaling, light/dark theme, and Windows 10 fallback.
- Uses `Segoe UI Variable Text` / `Segoe UI Variable Display` with a Windows 10 fallback so Chinese and English controls remain readable on high-resolution displays.
- 简体中文 / English switching. The default follows the Windows UI language and the selected language is saved per user.
- Cmd / Option / Control physical-key mapping. Choose left/right Windows, Alt, Control, or Caps Lock; mappings must remain distinct.
- Custom shutdown from 1–10080 minutes with input focus, validation, Enter confirmation, and Esc cancellation.
- Full virtual-desktop screenshot capture saved as a timestamped PNG on the Desktop, including multi-monitor layouts.
- OneDrive auto-start blocking that backs up and restores the current-user Run value, StartupApproved bytes, matching Startup-folder shortcuts, and the `OneDrive Startup Task-*` login tasks. It does not disable OneDrive update/reporting tasks or delete synced data.
- Reversible NVIDIA and AMD panel startup-entry hiding. Driver services are never stopped.
- Windows Security tray-entry hiding via the documented `HideSystray` policy. Defender, real-time protection, and security services remain enabled.
- Desktop image overlay using Windows Imaging Component: PNG, JPEG, BMP, GIF, TIFF, and ICO; topmost display, drag, wheel zoom, opacity, lock, mouse passthrough, and saved restoration.
- Windows Empty Volume Cache scan in a resizable, DPI-aware window. Low-risk cache categories are selected by default; Downloads, Windows.old, driver packages, ESD, rollback, and other advanced categories require explicit selection and a second confirmation. No registry cleaning and no Documents/OneDrive deletion.

## Trinity brand

The Trinity mark is an original geometric symbol made from three overlapping rounded ribbons. The central aperture represents a balance between freedom, control, and focus. The SVG is the vector master; the generated multi-size ICO is embedded into the executable and reused by the tray icon and Windows shell metadata.

Files:

- `assets/liberty-trinity.svg` — blue/indigo/cyan gradient master.
- `assets/liberty-trinity-mono.svg` — monochrome/high-contrast variant.
- `assets/liberty-trinity.ico` — 16/20/32/48/64/128/256px shell icon.
- `tools/generate-icon.ps1` — deterministic ICO generator used before packaging.

## Keyboard behavior

| macOS muscle memory | Liberty on Windows |
|---|---|
| Command + common application key | Control + that key |
| Command + Tab | Alt + Tab |
| Command + ` | Alt + Shift + Tab |
| Command + Q | Alt + F4 |
| Command + H / M | Minimize active window |
| Command + Left / Right | Home / End |
| Command + Up / Down | Control + Home / End |
| Option + Left / Right | Control + Left / Right |
| Option + Delete | Control + Backspace |
| Command + Option + Escape | Task Manager |
| Command + Space | Windows Search |
| Command + Shift + 3 | Full-screen screenshot to clipboard |
| Command + Shift + 4 | Windows region capture |

`Ctrl+Alt+F9` pauses/resumes remapping. `Ctrl+Alt+F10` turns the display off. `Ctrl+Alt+F11` schedules shutdown in one hour. `Ctrl+Alt+F12` cancels the Windows countdown.

## OneDrive safety model

When blocking OneDrive, Liberty first checks the current-user and local-machine `EnableAutoStart` policy. If a policy forces startup, Liberty refuses to override it. Otherwise it saves original registry bytes/types and task enabled states before disabling only startup paths matching OneDrive. A timer re-checks the startup task so a recreated `OneDrive Startup Task-*` is disabled while the block remains enabled. Unblocking restores the saved state. A normal close is attempted for the current user's `OneDrive.exe`; only a matching current-user process is terminated after the timeout.

The feature does not touch OneDrive sync folders, update/reporting tasks, or enterprise policy. If a component cannot be changed, Liberty rolls back the partial block and reports the failure.

## Cleanup safety model

The scanner asks Windows' built-in `IEmptyVolumeCache` / `IEmptyVolumeCache2` handlers for space estimates and purge operations. It validates handler DLLs against Windows system locations, re-scans after cleaning, leaves locked files to Windows, and requests UAC only for categories that need it. It intentionally does not implement registry cleaning, Storage Sense policy changes, or broad user-file deletion.

## Build

Install Visual Studio Build Tools 2022 with **Desktop development with C++** and **CMake tools for Windows**. Then run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The result is `Liberty.exe`. The build targets Release x64, embeds the Trinity ICO, and copies the portable executable to the repository root. GitHub Actions builds the same target on every `main` push; pushing a `v*` tag creates a Release with `Liberty.exe` and `SHA256SUMS.txt`.

## Privacy and license

Liberty has no network code, telemetry, updater, service, or installer. Settings and reversible backups are stored under `HKCU\Software\Liberty`. The cleanup tool only invokes Windows' registered cleanup handlers after the user selects categories.

This project is released under the [MIT License](LICENSE). The overlay interaction model was reviewed against the MIT-licensed [PinView](https://github.com/Pragyanand/PinView) and [TraceIt](https://github.com/SigNeedsGit/TraceIt); no third-party source or runtime is embedded in Liberty.

## Liberty（中文说明）

Liberty 是面向 Windows 10/11 x64 的原生 C++/Win32 便携工具。右键托盘图标可以打开更大、更易读的 Windows 11 风格菜单，并在中文和 English 之间切换。工具包含快捷键映射、定时关机、桌面截图保存、OneDrive 自动启动阻止、NVIDIA/AMD 面板启动隐藏、Windows Security 托盘入口隐藏、桌面图片悬浮，以及基于 Windows 内置清理处理器的 DPI 自适应缓存清理窗口。

OneDrive 功能只处理当前用户的启动入口和登录任务，并保存原始状态以便恢复；不会删除同步文件，不会关闭更新任务，也不会覆盖企业策略。清理功能默认只选择低风险缓存，高风险项目必须手动勾选并二次确认，不清理注册表、不删除 Documents 或 OneDrive 文件。

每次发布迭代都会先完成 Release x64 编译和冒烟测试，再提交 Git commit、推送公开仓库，并在版本标签下提供 `Liberty.exe` 与 SHA-256 校验值。
