# Unity 4.6 Dark Mode

Zero-setup dark mode for the Unity 4.6 Editor on Windows 11. Drop a single DLL next to `Unity.exe` and the title bar, menu bar, context menus, and native dialogs all go dark.

Unity 4.6's Pro skin darkens the editor panels, but the OS-level window chrome stays light. This project fixes that gap.

## Quick Start

1. Build the project (see [Building](#building)) or download `version.dll` from a release.
2. Copy `version.dll` into the same folder as your `Unity.exe`.
3. Launch Unity normally. Everything is dark.

To remove, delete `version.dll` from the Unity folder.

## What Gets Patched

| Element | Before | After |
|---|---|---|
| Title bar | White / system default | Dark |
| Menu bar (File, Edit, ...) | White background, dark text | Dark background, light text |
| Menu bar hover / selection | System blue highlight | Subtle dark grey highlight |
| Context menus (right-click) | White | Dark |
| Native dialogs | White | Dark background, light text |
| Common controls (tooltips, combo boxes, tree/list views) | White | Dark themed |

## How It Works

The DLL uses the **version.dll proxy** technique. It masquerades as `version.dll`, a standard Windows system library that nearly every application loads. When Unity starts, Windows finds our copy first (same directory as the executable) and loads it. All 17 real `version.dll` functions are forwarded to the system copy via x86 assembly trampolines with zero overhead -- nothing breaks.

On load, the DLL applies four layers of dark mode patching:

1. **Dark title bar** -- `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE` tells Windows 11 to render the title bar in dark mode.
2. **Dark context menus** -- The undocumented `SetPreferredAppMode` API (uxtheme.dll ordinal #135, available since Windows 10 1903) forces all context menus and popup menus to use dark colours.
3. **Dark menu bar** -- A window subclass intercepts the undocumented UAH (User Action Handler) messages `WM_UAHDRAWMENU` (0x0091) and `WM_UAHDRAWMENUITEM` (0x0092) to custom-draw the menu bar background and text in dark colours.
4. **Dark controls and dialogs** -- The subclass handles `WM_CTLCOLOR*` messages to set dark backgrounds on edit boxes, list boxes, static text, scroll bars, and dialogs. Tooltips, tree views, list views, combo boxes, and buttons are individually themed.

A **CBT hook** (`WH_CBT`) installed on the current thread intercepts all future window creation, so windows created after startup are automatically patched too.

## Building

### Requirements

- **Windows 11** (or Windows 10 2004+)
- **Visual Studio** with the C++ desktop development workload (2019 or newer -- Community, Professional, or Build Tools editions all work)
- **CMake 3.20+**

### One-Step Build

```
build.bat
```

The script auto-detects your Visual Studio installation (via `vswhere` or common paths), sets up the x86 MSVC environment, and runs a CMake Release build. Output: `build\version.dll`.

### Manual Build

Open a **Visual Studio x86 Developer Command Prompt**, then:

```
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> **Important:** Unity 4.6 is a 32-bit application. The DLL **must** be compiled for x86. `build.bat` handles this automatically; for manual builds, make sure you use `vcvars32.bat` (not `vcvars64.bat`).

## Project Structure

```
Unity46-DarkMode/
  CMakeLists.txt          CMake build configuration
  build.bat               One-step build script (auto-detects VS)
  src/
    dllmain.cpp           DLL entry point -- wires proxy and dark mode init
    version_proxy.h       Proxy API declarations
    version_proxy.cpp     Loads real version.dll, 17 x86 asm trampolines
    version.def           Maps export names to proxy symbols
    darkmode.h            Dark mode API declarations
    darkmode.cpp          All dark mode logic (DWM, uxtheme, subclassing, hook)
```

## Requirements

- **OS:** Windows 11 (or Windows 10 version 2004+) -- the dark mode DWM and uxtheme APIs require it.
- **Unity:** 4.6 (32-bit Editor). May also work with nearby versions (4.x -- 5.x) that share the `UnityContainerWndClass` window class.
- **No runtime dependencies.** The DLL links only against standard Windows system libraries (`dwmapi`, `uxtheme`, `comctl32`).

## Safety

This DLL has been security-audited. It makes **no network calls**, writes **no files**, accesses **no private data**, and forwards all `version.dll` calls without tampering. The full audit is in [DOCUMENTATION.md](DOCUMENTATION.md).

## Troubleshooting

**Unity doesn't start or crashes immediately:**
Ensure you built the DLL for x86 (32-bit). A 64-bit DLL cannot be loaded by the 32-bit Unity 4.6 Editor.

**Dark mode doesn't apply:**
Make sure the DLL is named exactly `version.dll` and is in the same directory as `Unity.exe` (not a subfolder).

**Menu bar flickers or reverts to light:**
The DLL blocks `WM_STYLECHANGING`/`WM_STYLECHANGED` on Unity container windows to prevent this. If it still happens, check that no other plugin is also modifying window styles.

## Credits

Dark mode rendering approach adapted from [UnityEditor-DarkMode](https://github.com/Shilo/UnityEditor-DarkMode) by Jiaqi Liu, which was derived from [ReaperThemeHackDll](https://github.com/jjYBdx4IL/ReaperThemeHackDll).

## License

MIT
