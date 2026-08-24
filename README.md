# Liberty

Liberty is a tiny native Windows tray utility that brings common macOS keyboard muscle memory to Windows, turns the display off instantly, and schedules shutdowns. It is deliberately built with the Win32 API and has no runtime, installer, service, or background updater.

## Design goals

- One portable `Liberty.exe`; double-click to run.
- Native C++/Win32, event-driven message loop, no polling.
- Per-user settings by default; hiding the Windows Security tray entry may ask for administrator permission because Windows scopes that policy to the computer.
- Left Windows key acts as macOS Command. Right Windows remains unchanged as a safety escape hatch.
- `Ctrl+Alt+F9` always pauses/resumes remapping.

## Shortcut behavior

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

“All shortcuts” cannot be translated literally because macOS and Windows applications do not share one universal command vocabulary. Liberty applies the broad Command→Control rule, then handles the important operating-system differences explicitly. App-reserved, hardware, accessibility, gaming, remote-desktop, and elevated-window shortcuts may behave differently.

## Custom modifier mappings

Open **Keyboard mappings…** from the tray menu to choose the physical key used as macOS Cmd, Option, and Control. The defaults are Left Windows, Left Alt, and Left Control. Mappings are stored under `HKCU\Software\Liberty` and the three keys must be distinct. The dialog supports Left/Right Windows, Left/Right Alt, Left/Right Control, and Caps Lock.

## Tray actions

- Left-click the tray icon: pause/resume mappings.
- Right-click: screen off, timed shutdown (15/30/60/120/custom minutes), cancel shutdown, start with Windows, help, exit.
- **Windows UI & startup**: block OneDrive auto-start, hide known NVIDIA/AMD panel startup entries, and hide the Windows Security tray entry.
- `Ctrl+Alt+F10`: turn display off.
- `Ctrl+Alt+F11`: shut down in 60 minutes.
- `Ctrl+Alt+F12`: cancel scheduled shutdown.

Windows itself displays its standard shutdown warning and owns the countdown, so the schedule survives if Liberty exits. Cancel it from the tray or with `Ctrl+Alt+F12`.

The startup cleanup options are reversible and keep a backup of user-level `Run` values under Liberty's settings. They do not uninstall drivers or stop driver services. The NVIDIA/AMD options cover common user-level panel entries; vendor updates may add different entries. The Windows Security option uses the documented `HideSystray` policy to hide the notification-area entry only. It does not disable Microsoft Defender, Windows Security services, or real-time protection.

## Build a single EXE

On Windows 10/11, install **Visual Studio Build Tools 2022**, selecting **Desktop development with C++** and **CMake tools for Windows**. Then right-click `build.ps1`, choose **Run with PowerShell**, or run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The output is `Liberty.exe`. No DLLs or other files are required at runtime.

## Security and limitations

- Liberty does not collect data or use the network.
- Settings are stored under `HKCU\Software\Liberty`; optional startup and reversible startup-entry backups are stored under the current user's `Run` key and Liberty settings.
- Windows blocks a normal process from remapping keystrokes inside administrator-elevated apps. Run Liberty elevated only if that behavior is explicitly needed.
- Some games and security software intentionally block low-level keyboard hooks.
- Turning off the display asks Windows/monitor firmware to enter power-save mode; mouse or keyboard activity wakes it.

## Third-party code

No third-party source is embedded in this release. Liberty uses only documented Microsoft Win32 APIs, keeping the binary and attack surface smaller than integrating a general-purpose automation runtime. The project can be distributed under the MIT License.

