# Unity 4.6 Dark Mode

Forces full dark mode on the Unity 4.6 Editor title bar and menu bar on Windows 11. No setup required -- just drop a single DLL next to `Unity.exe`.

Unity 4.6's Pro skin makes the editor panels dark, but the Windows-level chrome (title bar, menu bar, context menus, dialogs) stays light. This DLL fixes that by patching the Win32 window rendering at load time.

## Usage

1. Build the project (see below) or grab `version.dll` from a release.
2. Copy `version.dll` into the same directory as `Unity.exe`.
3. Launch Unity. The title bar, menu bar, and context menus are now dark.

To uninstall, delete `version.dll` from the Unity directory.

## How It Works

The DLL masquerades as `version.dll` (a standard Windows system library). When Unity starts, Windows loads our DLL first due to DLL search order. All real `version.dll` calls are forwarded transparently to the system copy via x86 assembly trampolines, so nothing breaks.

On load, the DLL:

- Enables the immersive dark title bar via `DwmSetWindowAttribute`
- Forces dark context menus via the undocumented `SetPreferredAppMode` API (uxtheme ordinal #135)
- Installs a CBT hook to catch every new window Unity creates
- Subclasses windows to custom-draw the menu bar, dialogs, and controls in dark colours using the undocumented UAH menu messages

## Building

**Requirements:** Visual Studio with the C++ desktop workload (VS 2019+ Build Tools or Community), CMake 3.20+.

**Quick build (x86):**

```
build.bat
```

This invokes `vcvars32.bat`, configures CMake for NMake with x86, and builds a Release DLL.

**Manual build:**

```
# Open a Visual Studio x86 Developer Command Prompt, then:
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is `build/version.dll`.

> **Note:** Unity 4.6 is a 32-bit application, so the DLL must be built for x86. The `build.bat` script handles this automatically by calling `vcvars32.bat`.

## Requirements

- **Windows 11** (or Windows 10 2004+) -- required for the dark mode DWM/uxtheme APIs
- **Unity 4.6** (32-bit Editor)

## Credits

Dark mode rendering approach adapted from [UnityEditor-DarkMode](https://github.com/Shilo/UnityEditor-DarkMode), which was itself derived from [ReaperThemeHackDll](https://github.com/jjYBdx4IL/ReaperThemeHackDll).

## License

MIT
