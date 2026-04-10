# Unity Dark Mode Mod

Zero-setup dark mode for the Unity 6 Editor on Windows 11. Drop a single DLL next to `Unity.exe` and the title bar, menu bar, context menus, and native dialogs all go dark.

Unity 6's dark theme covers the editor panels, but the OS-level window chrome stays light. This project fixes that gap by patching the Win32 window rendering at load time.

## Quick Start

1. Download `version.dll.zip` from the [latest release](../../releases/latest) and extract `version.dll`.
   - Or build it yourself (see [Building](#building)).
2. Open the folder that contains `Unity.exe`.
   - Easy way: **Unity Hub > Installs > Manage > Show in Explorer**
   - Direct path: `%ProgramFiles%\Unity\Hub\Editor\<version>\Editor` (example: `C:\Program Files\Unity\Hub\Editor\6000.4.0f1\Editor`)
3. Move `version.dll` into that folder next to `Unity.exe`.
4. Launch Unity normally. Everything is dark.

To remove, delete `version.dll` from the same folder.

> **Important:** Put the DLL next to `Unity.exe`, not in your Unity project folder and not next to the Unity Hub app itself.

## What Gets Patched

| Element                                                  | Before                      | After                       |
| -------------------------------------------------------- | --------------------------- | --------------------------- |
| Title bar                                                | White / system default      | Dark                        |
| Menu bar (File, Edit, ...)                               | White background, dark text | Dark background, light text |
| Menu bar hover / selection                               | System blue highlight       | Subtle dark grey highlight  |
| Context menus (right-click)                              | White                       | Dark                        |
| Native dialogs                                           | White                       | Dark background, light text |
| Common controls (tooltips, combo boxes, tree/list views) | White                       | Dark themed                 |

## How It Works

The DLL uses the **version.dll proxy** technique. It masquerades as `version.dll`, a standard Windows system library that nearly every application loads. When Unity starts, Windows finds our copy first (same directory as the executable) and loads it. All 17 real `version.dll` functions are forwarded to the system copy via x64 MASM assembly trampolines with zero overhead -- nothing breaks.

On load, the DLL applies four layers of dark mode patching:

1. **Dark title bar** -- `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE` tells Windows 11 to render the title bar in dark mode.
2. **Dark context menus** -- Four undocumented uxtheme.dll ordinals (stable since Windows 10 1903) work together: `SetPreferredAppMode` (#135) forces dark context menus process-wide, `AllowDarkModeForWindow` (#133) opts each window into dark theme class resolution, `RefreshImmersiveColorPolicyState` (#104) commits the colour policy, and `FlushMenuThemes` (#136) refreshes cached menu visuals immediately.
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

The script auto-detects your Visual Studio installation (via `vswhere` or common paths), sets up the x64 MSVC environment, and runs a CMake Release build. Output: `build\version.dll`.

### Manual Build

Open a **Visual Studio x64 Developer Command Prompt**, then:

```
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> **Important:** Unity 6 is a 64-bit application. The DLL **must** be compiled for x64. `build.bat` handles this automatically by calling `vcvars64.bat`.

## Differences from [0x7c13/UnityEditor-DarkMode](https://github.com/0x7c13/UnityEditor-DarkMode)

This project is inspired by 0x7c13/UnityEditor-DarkMode and uses similar Win32 dark mode techniques, but is a separate implementation built specifically for Unity 6.

|                           | 0x7c13/UnityEditor-DarkMode                                        | unity-dark-mode                                                        |
| ------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------------------------- |
| **Unity versions**        | 2019, 2020, 2021, 2022, 2023, Unity 6                              | 6.4+                                                                   |
| **How the DLL loads**     | Unity native plugin ("Load on startup") or Detours `withdll.exe`   | version.dll proxy — auto-loads via DLL search order                    |
| **Setup required**        | Enable "Load on startup" in plugin settings, or launch via Detours | None — drop DLL next to Unity.exe                                      |
| **Configuration**         | INI file for custom colours                                        | Zero-config (colours matched to Unity 6 dark theme)                    |
| **External dependencies** | inipp (INI parser), ATL, C++20 `<filesystem>`                      | None                                                                   |
| **C++ standard**          | C++20                                                              | C++17                                                                  |
| **DLL injection**         | Native plugin loaded by Unity's plugin system                      | version.dll proxy with x64 MASM trampolines                           |
| **Source layout**         | Single `.cpp` file                                                 | Split across `dllmain.cpp`, `darkmode.cpp`, `version_proxy.cpp/.asm`   |
| **Crash protection**      | None — undocumented API failure crashes the host app               | SEH wraps all init and hooks — silently disables on failure            |
| **Dark mode APIs**        | uxtheme ordinal #135 only                                          | Ordinals #133, #135, #104, #136 for fuller coverage                    |
| **Build**                 | CMake                                                              | CMake + one-step `build.bat` (auto-detects Visual Studio)              |

For older Unity versions that require specific setup methods, use the original project.

## Project Structure

```
unity-dark-mode/
  CMakeLists.txt           CMake build configuration (CXX + ASM_MASM)
  build.bat                One-step build script (auto-detects VS)
  src/
    dllmain.cpp            DLL entry point -- wires proxy and dark mode init
    version_proxy.h        Proxy API declarations
    version_proxy.cpp      Loads real version.dll, fills function pointer table
    version_proxy.asm      x64 MASM trampolines (17 JMP QWORD PTR stubs)
    version.def            Maps export names to proxy symbols
    darkmode.h             Dark mode API declarations
    darkmode.cpp           All dark mode logic (DWM, uxtheme, subclassing, hook)
```

## Requirements

- **OS:** Windows 11 (or Windows 10 version 2004+) -- the dark mode DWM and uxtheme APIs require it.
- **Unity:** 6.4+ (64-bit Editor).
- **No runtime dependencies.** The DLL links only against standard Windows system libraries (`dwmapi`, `uxtheme`, `comctl32`).

## Safety

This DLL has been security-audited. It makes **no network calls**, writes **no files**, accesses **no private data**, and forwards all `version.dll` calls without tampering. Structured exception handling (SEH) wraps all initialization and hook callbacks so that if any undocumented API misbehaves on a future Windows build, dark mode silently disables itself instead of crashing Unity. The full audit is in [DOCUMENTATION.md](DOCUMENTATION.md).

## Troubleshooting

**Unity doesn't start or crashes immediately:**
Ensure you built the DLL for x64 (64-bit). A 32-bit DLL cannot be loaded by the 64-bit Unity Editor.

**Dark mode doesn't apply:**
Make sure the DLL is named exactly `version.dll` and is in the same directory as `Unity.exe` (not a subfolder). On a standard Unity Hub install, that folder is `%ProgramFiles%\Unity\Hub\Editor\<version>\Editor`.

**Menu bar flickers or reverts to light:**
The DLL blocks `WM_STYLECHANGING`/`WM_STYLECHANGED` on Unity container windows to prevent this. If it still happens, check that no other plugin is also modifying window styles.

## Credits

Dark mode rendering approach inspired by [0x7c13/UnityEditor-DarkMode](https://github.com/0x7c13/UnityEditor-DarkMode), which was derived from [ReaperThemeHackDll](https://github.com/jjYBdx4IL/ReaperThemeHackDll).

## License

MIT
