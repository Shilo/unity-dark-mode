# Unity Dark Mode -- Technical Documentation

This document covers the internal architecture, Win32 API usage, security audit, and differences from the upstream project.

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [DLL Proxy Layer](#dll-proxy-layer)
- [Dark Mode Engine](#dark-mode-engine)
  - [Initialization Sequence](#initialization-sequence)
  - [Title Bar Darkening](#title-bar-darkening)
  - [Context Menu Darkening](#context-menu-darkening)
  - [Menu Bar Rendering](#menu-bar-rendering)
  - [Control and Dialog Theming](#control-and-dialog-theming)
  - [CBT Hook](#cbt-hook)
  - [Window Subclass Procedure](#window-subclass-procedure)
- [Win32 API Reference](#win32-api-reference)
- [Undocumented APIs and Structures](#undocumented-apis-and-structures)
- [Colour Palette](#colour-palette)
- [Security Audit](#security-audit)
- [Differences from 0x7c13/UnityEditor-DarkMode](#differences-from-0x7c13unityeditor-darkmode)
- [Known Limitations](#known-limitations)
- [Build Details](#build-details)

---

## Architecture Overview

The project is a native Win32 C++ DLL that runs inside the Unity Editor process. It has two layers:

```
+------------------------------------------------------+
|  Unity 6 Editor process (64-bit)                     |
|                                                      |
|  +-----------------+    +-------------------------+  |
|  | version.dll     |    | Dark Mode Engine        |  |
|  | proxy layer     |--->| (darkmode.cpp)          |  |
|  | (version_proxy) |    |                         |  |
|  |                 |    | - DWM title bar          |  |
|  | Forwards all 17 |    | - uxtheme context menus  |  |
|  | version.dll     |    | - UAH menu bar drawing   |  |
|  | exports to the  |    | - WM_CTLCOLOR* controls  |  |
|  | real system DLL |    | - CBT hook for new wins  |  |
|  +-----------------+    +-------------------------+  |
|          |                         |                  |
|          v                         v                  |
|  +----------------+    +------------------------+    |
|  | C:\Windows\    |    | DWM / uxtheme / comctl |    |
|  | System32\      |    | system services        |    |
|  | version.dll    |    +------------------------+    |
|  +----------------+                                  |
+------------------------------------------------------+
```

**Entry point:** `DllMain` in `dllmain.cpp`. Called by Windows when the DLL is loaded.

**Execution order on `DLL_PROCESS_ATTACH`:**
1. `DisableThreadLibraryCalls` -- optimization, prevents per-thread attach/detach notifications.
2. `InitializeProxy()` -- loads the real `version.dll` and fills the function pointer table.
3. `InitializeDarkMode()` -- applies dark mode to existing windows and installs the CBT hook.

**Execution order on `DLL_PROCESS_DETACH`:**
1. `ShutdownDarkMode()` -- unhooks CBT, closes theme data, destroys brushes.
2. `ShutdownProxy()` -- frees the real `version.dll`.

---

## DLL Proxy Layer

**Files:** `src/version_proxy.cpp`, `src/version_proxy.h`, `src/version_proxy.asm`, `src/version.def`

### Why version.dll?

The DLL proxy technique exploits Windows' DLL search order: when any module in the Unity process requests `version.dll`, Windows searches the application directory first, finds our copy, and loads it. This requires zero setup — just place the DLL next to `Unity.exe`.

`version.dll` is ideal because:
- It has only 17 exports (easy to proxy completely).
- It is not in Windows' Known DLLs list (so it is not forced to load from System32).
- Nearly all Windows applications depend on it directly or transitively.

### How Forwarding Works

On initialization, `InitializeProxy()` resolves the real system DLL:

```
GetSystemDirectoryW -> "C:\Windows\System32"
LoadLibraryW("C:\Windows\System32\version.dll")
GetProcAddress x 17 -> fills g_originalFuncs[]
```

Each of the 17 exports is implemented as an **x64 MASM assembly trampoline** in `version_proxy.asm`:

```asm
; Example: Proxy_GetFileVersionInfoA
jmp QWORD PTR [g_originalFuncs + 0*8]
```

This is a single indirect `JMP` instruction. It:
- Preserves the x64 calling convention (rcx, rdx, r8, r9 + stack shadow space) because it never touches any registers or the stack.
- Has zero overhead beyond the single instruction.
- Causes the real function to return directly to the original caller (not back to the trampoline).

The `version.def` module definition file maps the public export names (`GetFileVersionInfoA`, etc.) to the internal `Proxy_*` symbols defined in the `.asm` file.

### Exported Functions

| Index | Export Name |
|-------|-------------|
| 0 | `GetFileVersionInfoA` |
| 1 | `GetFileVersionInfoByHandle` |
| 2 | `GetFileVersionInfoExA` |
| 3 | `GetFileVersionInfoExW` |
| 4 | `GetFileVersionInfoSizeA` |
| 5 | `GetFileVersionInfoSizeExA` |
| 6 | `GetFileVersionInfoSizeExW` |
| 7 | `GetFileVersionInfoSizeW` |
| 8 | `GetFileVersionInfoW` |
| 9 | `VerFindFileA` |
| 10 | `VerFindFileW` |
| 11 | `VerInstallFileA` |
| 12 | `VerInstallFileW` |
| 13 | `VerLanguageNameA` |
| 14 | `VerLanguageNameW` |
| 15 | `VerQueryValueA` |
| 16 | `VerQueryValueW` |

Each `FARPROC` is 8 bytes on x64. The `.asm` trampolines index into `g_originalFuncs[]` at `index * 8`.

---

## Dark Mode Engine

**File:** `src/darkmode.cpp`, `src/darkmode.h`

### Initialization Sequence

`InitializeDarkMode()` runs these steps:

1. **Create GDI brushes** for the three background colours (normal, hover, selected).
2. **Call `EnableDarkModeForWindow(nullptr)`** to set the process-level preferred app mode to `ForceDark`. This affects all future context menus and some system-drawn UI.
3. **Enumerate existing windows** using `FindWindowEx` in a loop, filtered by `GetCurrentProcessId()`. For each window that passes `ShouldSubclass()`, apply `EnableDarkModeForWindow()` and install the subclass procedure.
4. **Install a CBT hook** via `SetWindowsHookEx(WH_CBT, CBTProc, nullptr, GetCurrentThreadId())`. This hooks the current thread only (not system-wide).

### Title Bar Darkening

```cpp
static constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR = 20;
DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR, &useDark, sizeof(useDark));
```

This is the documented DWM (Desktop Window Manager) attribute available since Windows 10 version 2004. It tells the compositor to render the window's title bar, caption buttons (minimize / maximize / close), and window border in dark colours.

Applied to every window that passes `ShouldSubclass()`.

### Context Menu Darkening

```cpp
auto SetPreferredAppMode = GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
SetPreferredAppMode(PreferredAppMode::ForceDark);
```

This is the **undocumented** `SetPreferredAppMode` function exported by ordinal #135 from `uxtheme.dll`. It was introduced in Windows 10 version 1903 and controls whether the system draws popup menus, context menus, and some common controls using the dark palette.

The enum values:
- `Default` (0) -- follow system setting
- `AllowDark` (1) -- allow dark if system is dark
- `ForceDark` (2) -- always dark regardless of system setting
- `ForceLight` (3) -- always light

We use `ForceDark` to ensure dark menus even if the user's system theme is light.

The `uxtheme.dll` module is loaded with `LOAD_LIBRARY_SEARCH_SYSTEM32` to ensure we get the real system copy and not a proxy.

### Menu Bar Rendering

The Windows menu bar is drawn by an internal system component that sends **undocumented UAH messages** to the window procedure. By intercepting these messages in our subclass, we can custom-draw the menu bar:

#### `WM_UAHDRAWMENU` (0x0091)

Sent to draw the entire menu bar background. `lParam` is a pointer to a `UAHMENU` structure containing the menu handle and HDC.

Our handler:
1. Gets the menu bar rectangle via `GetMenuBarInfo(OBJID_MENU)`.
2. Converts from screen coordinates to window-relative coordinates.
3. Fills the rectangle with the dark background brush.

#### `WM_UAHDRAWMENUITEM` (0x0092)

Sent to draw a single menu bar item. `lParam` is a pointer to a `UAHDRAWMENUITEM` structure containing a `DRAWITEMSTRUCT`, the menu handle/HDC, and the item position.

Our handler:
1. Checks `itemState` flags (`ODS_HOTLIGHT`, `ODS_SELECTED`, `ODS_GRAYED`, `ODS_DISABLED`) to pick the background brush and text colour.
2. Fills the item rectangle with the appropriate brush.
3. Retrieves the item text via `GetMenuItemInfoW`.
4. Draws the text using `DrawThemeTextEx` with a custom `DTTOPTS` that overrides the text colour. This preserves the system font metrics and handles accelerator underlines (`DT_HIDEPREFIX` when `ODS_NOACCEL` is set).

#### `WM_UAHMEASUREMENUITEM` (0x0094)

Sent to measure a menu item's size. We pass this through to `DefSubclassProc` to let the system calculate the default size.

#### Menu Bar Bottom Line

After `WM_NCPAINT` and `WM_NCACTIVATE`, we call `DrawMenuBarBottomLine()` to paint over the 1-pixel bright line between the menu bar and the client area. This line is drawn by the system's non-client painting and would otherwise remain white.

### Control and Dialog Theming

The subclass procedure intercepts these standard Windows messages:

| Message | Purpose |
|---------|---------|
| `WM_CTLCOLORDLG` | Set dark background on dialog windows |
| `WM_CTLCOLOREDIT` | Set dark background/text on edit controls |
| `WM_CTLCOLORLISTBOX` | Set dark background/text on listboxes |
| `WM_CTLCOLORSCROLLBAR` | Set dark background on scrollbar tracks |
| `WM_CTLCOLORSTATIC` | Set dark background/text on static labels |
| `WM_ERASEBKGND` | Fill client area with dark brush |
| `WM_DRAWITEM` | Owner-draw dark push buttons |
| `WM_PAINT` | Set dark colours on tooltips, tree views, list views |
| `WM_NCCREATE` | Apply dark themes and convert button styles |
| `WM_STYLECHANGING` / `WM_STYLECHANGED` | Block style reversion on Unity windows |
| `WM_THEMECHANGED` | Reset cached menu theme handle |

**Button handling in `WM_NCCREATE`:**
- Checkboxes, radio buttons, and 3-state buttons get `SetWindowTheme(L"DarkMode_Explorer")`.
- Push buttons and default push buttons are converted to `BS_OWNERDRAW` so the `WM_DRAWITEM` handler can paint them dark.

**Combo box handling in `WM_NCCREATE`:**
- The combo box itself gets `SetWindowTheme(L"DarkMode_CFD")`.
- The dropdown list window gets `SetWindowTheme(L"DarkMode_Explorer")`.

### CBT Hook

```cpp
g_cbtHook = SetWindowsHookEx(WH_CBT, CBTProc, nullptr, GetCurrentThreadId());
```

The Computer-Based Training hook intercepts window lifecycle events on the current thread:

- **`HCBT_CREATEWND`**: When a new window is created, check `ShouldSubclass()`. If true, call `EnableDarkModeForWindow()` and install the subclass procedure.
- **`HCBT_DESTROYWND`**: When a window is destroyed, remove the subclass procedure to prevent dangling pointers.

Per Win32 documentation, `CBTProc` passes through immediately when `nCode < 0` by calling `CallNextHookEx`.

### Window Subclass Procedure

`ShouldSubclass()` returns true for:
- Windows whose class name contains `"UnityContainer"` (the Unity Editor's main window class).
- `#32770` (standard Windows dialog boxes).
- `Button`, `tooltips_class32`, `ComboBox`, `SysListView32`, `SysTreeView32`.
- Any window that has a menu bar (`GetMenu(hWnd) != nullptr`).

---

## Win32 API Reference

### DWM (Desktop Window Manager)
- `DwmSetWindowAttribute` -- sets the immersive dark mode attribute on a window's title bar.

### UxTheme (Visual Styles)
- `OpenThemeData` / `CloseThemeData` -- manages theme handles for menu bar text rendering.
- `DrawThemeTextEx` -- draws text with custom colour override via `DTTOPTS`.
- `SetWindowTheme` -- applies `DarkMode_Explorer` or `DarkMode_CFD` theme to controls.

### Common Controls (comctl32)
- `SetWindowSubclass` / `RemoveWindowSubclass` / `DefSubclassProc` -- window procedure subclassing.
- `TreeView_SetBkColor`, `TreeView_SetTextColor` -- tree view dark colours.
- `ListView_SetBkColor`, `ListView_SetTextBkColor`, `ListView_SetTextColor` -- list view dark colours.

### GDI
- `CreateSolidBrush`, `CreatePen`, `DeleteObject`, `SelectObject` -- brush and pen management.
- `FillRect`, `Rectangle`, `DrawTextW`, `DrawFocusRect` -- painting.
- `SetTextColor`, `SetBkColor`, `SetBkMode` -- text rendering colours.
- `GetWindowDC`, `ReleaseDC` -- non-client area device context.

### Window Management
- `SetWindowsHookEx` / `UnhookWindowsHookEx` / `CallNextHookEx` -- CBT hook.
- `FindWindowEx` / `GetWindowThreadProcessId` / `GetCurrentProcessId` -- window enumeration.
- `GetClassNameW` -- window class identification.
- `GetWindowLongPtr` / `SetWindowLongPtr` -- window style manipulation.
- `GetMenuBarInfo` / `GetMenuItemInfoW` -- menu bar geometry and text.
- `GetClientRect`, `GetWindowRect`, `MapWindowPoints`, `OffsetRect` -- coordinate conversion.
- `SendMessage` -- control configuration (`CB_GETCOMBOBOXINFO`, `TTM_SETTIPBKCOLOR`, etc.).

### Module Loading
- `GetSystemDirectoryW` -- locates the System32 directory.
- `LoadLibraryW` -- loads the real `version.dll`.
- `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_SYSTEM32` -- loads `uxtheme.dll` securely.
- `GetProcAddress` -- resolves function pointers (version.dll exports, uxtheme ordinal #135).
- `FreeLibrary` -- releases loaded DLLs.

---

## Undocumented APIs and Structures

### SetPreferredAppMode (uxtheme ordinal #135)

```cpp
enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
PreferredAppMode WINAPI SetPreferredAppMode(PreferredAppMode appMode);
```

Available since Windows 10 version 1903. Controls whether the process uses dark or light visual styles for system-drawn UI (context menus, scrollbars, etc.). Not officially documented by Microsoft.

### UAH Menu Messages

These messages are sent by Windows' internal menu bar rendering system:

| Message | Value | Purpose |
|---------|-------|---------|
| `WM_UAHDESTROYWINDOW` | 0x0090 | Window being destroyed (unused) |
| `WM_UAHDRAWMENU` | 0x0091 | Draw the menu bar background |
| `WM_UAHDRAWMENUITEM` | 0x0092 | Draw a single menu bar item |
| `WM_UAHINITMENU` | 0x0093 | Menu initialization (unused) |
| `WM_UAHMEASUREMENUITEM` | 0x0094 | Measure a menu bar item |
| `WM_UAHNCPAINTMENUPOPUP` | 0x0095 | Paint popup menu non-client area (unused) |

### UAH Structures

```cpp
typedef struct tagUAHMENU {
    HMENU hmenu;
    HDC   hdc;
    DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM {
    int                iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct UAHDRAWMENUITEM {
    DRAWITEMSTRUCT dis;    // Standard draw-item info (itemState, rcItem, etc.)
    UAHMENU        um;     // Menu handle and HDC
    UAHMENUITEM    umi;    // Item position and metrics
} UAHDRAWMENUITEM;
```

These structures are reverse-engineered from Windows internals and have been stable across Windows 10 and 11.

---

## Colour Palette

All colours are matched to the Unity 6 Editor dark theme (source: Unity Editor Design System color palette):

| Constant | RGB | Hex | Usage |
|----------|-----|-----|-------|
| `CLR_TEXT` | 210, 210, 210 | `#D2D2D2` | Menu bar text, control text |
| `CLR_TEXT_DISABLED` | 189, 189, 189 | `#BDBDBD` | Disabled / greyed menu items |
| `CLR_BG` | 25, 25, 25 | `#191919` | Menu bar background, control backgrounds, dialog backgrounds |
| `CLR_BG_HOT` | 66, 66, 66 | `#424242` | Menu item hover highlight |
| `CLR_BG_SELECTED` | 106, 106, 106 | `#6A6A6A` | Menu item selected / pressed state |

---

## Security Audit

### Summary

**Verdict: SAFE.** The DLL performs only cosmetic modifications to window rendering. It makes no network calls, writes no files, accesses no private data, and faithfully forwards all version.dll calls without tampering.

### Detailed Findings

**Network access:** None. No `WinInet`, `WinHTTP`, `socket`, `WSA*`, or any networking API is used anywhere.

**File system writes:** None. No `CreateFile`, `WriteFile`, `DeleteFile`, or any file I/O. The only file-related operations are `LoadLibraryW` / `LoadLibraryExW` to load system DLLs, and `GetSystemDirectoryW` to read the system directory path.

**Registry access:** None. No `RegOpenKey`, `RegSetValue`, or any registry API.

**Process manipulation:** None. No `CreateProcess`, `CreateRemoteThread`, `ReadProcessMemory`, `WriteProcessMemory`, or any cross-process API.

**Input capture:** None. No keyboard hooks (`WH_KEYBOARD`), no mouse hooks (`WH_MOUSE`), no clipboard access. The only hook is `WH_CBT` on the current thread for window lifecycle events.

**Proxy integrity:** All 17 version.dll exports are forwarded via MASM64 JMP QWORD PTR trampolines. No arguments are read, modified, or logged. No return values are intercepted.

**DLL loading:** Only two DLLs are loaded at runtime:
- The real `version.dll` from the system directory (via `GetSystemDirectoryW` + `LoadLibraryW`).
- `uxtheme.dll` from the system directory (via `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_SYSTEM32`).

Both are standard Windows system libraries loaded securely.

**No obfuscation:** All strings are readable C literals. No encoded data, no dynamic code generation, no `VirtualAlloc` + execute patterns.

**No persistence:** The DLL does not modify the registry, create services, install scheduled tasks, or copy itself anywhere. Removing the file undoes everything.

---

## Differences from 0x7c13/UnityEditor-DarkMode

This project is inspired by [0x7c13/UnityEditor-DarkMode](https://github.com/0x7c13/UnityEditor-DarkMode). Key differences:

### Architecture

| Aspect | 0x7c13/UnityEditor-DarkMode | This Project |
|--------|----------|-------------|
| Injection method | Unity native plugin / Detours `withdll.exe` | version.dll proxy (DLL search order) |
| Target Unity versions | 2019 -- Unity 6 | 6.4+ |
| Target architecture | x64 (inline asm) | x64 (MASM64) |
| Source layout | Single `UnityEditorDarkMode.cpp` | Split into `dllmain.cpp`, `darkmode.cpp`, `version_proxy.cpp`, `version_proxy.asm` |
| Configuration | INI file with customizable colours | Hardcoded colours matched to Unity 6 dark theme (zero-config) |
| Dependencies | inipp (header-only INI parser), ATL (`atlstr.h`), C++20 `<filesystem>` | None -- only Windows system libs, C++17 |

### Bug Fixes

1. **`WM_CTLCOLORDLG` return value:** 0x7c13 returns a `COLORREF` value cast to `INT_PTR`, which is incorrect -- the handler must return a brush handle. Fixed.
2. **`PaintDarkButton` debug artifact:** 0x7c13 has a stray `FillRect` with `RGB(127, 192, 127)` (green). Removed.
3. **`CBTProc` hook chain:** 0x7c13 returns 0 without calling `CallNextHookEx`. Fixed to properly propagate to the next hook.
4. **`CBTProc` negative nCode:** 0x7c13 does not check for `nCode < 0`. Fixed to pass through immediately per Win32 documentation.
5. **Button type extraction:** 0x7c13 uses `LOWORD(style)` instead of the correct `style & BS_TYPEMASK`. Fixed with additional coverage for radio buttons, 3-state buttons, and default push buttons.

### Improvements

- **Broader window matching:** `IsUnityWindow()` matches `"UnityContainer"` prefix (not just the exact `"UnityContainerWndClass"`), plus any window with a menu bar.
- **Canonical dark theme names:** Uses `DarkMode_Explorer` and `DarkMode_CFD` (the community-documented identifiers) instead of `"wstr"`.
- **No external downloads:** The build has zero `FetchContent` or network dependencies.

### Features Not Included

- **INI configuration:** Colour customization via `.dll.ini` file. Removed for simplicity (zero-config goal). To change colours, edit the `CLR_*` constants in `darkmode.cpp` and rebuild.

---

## Known Limitations

1. **Windows 10 2004+ required.** The `DWMWA_USE_IMMERSIVE_DARK_MODE` attribute (value 20) and `SetPreferredAppMode` ordinal #135 are not available on older Windows versions.

2. **No colour customization.** Colours are hardcoded to match Unity 6's dark theme. To change them, edit the `CLR_*` constants in `darkmode.cpp` and rebuild.

3. **Current thread only.** The CBT hook is installed on the main thread via `GetCurrentThreadId()`. Windows created on background threads will not be automatically patched (this is uncommon in Unity's architecture).

4. **Subclass cleanup on detach.** `ShutdownDarkMode()` removes the CBT hook and destroys GDI resources but does not enumerate and remove subclass procedures from individual windows. This is acceptable because `DLL_PROCESS_DETACH` typically runs during process exit.

---

## Build Details

### Compiler Requirements

- MSVC (Visual Studio 2019 or newer) with the **C++ desktop development** workload.
- MASM64 assembler (`ml64.exe`, included with MSVC).
- C++17 standard (`/std:c++17`).

### CMake Configuration

- **Languages:** `CXX` + `ASM_MASM`.
- **Generator:** NMake Makefiles (via `build.bat`) or Visual Studio.
- **Definitions:** `NOMINMAX`, `UNICODE`, `_UNICODE`.
- **Linked libraries:** `dwmapi.lib`, `uxtheme.lib`, `comctl32.lib` (via `#pragma comment(lib, ...)`).
- **Output:** `version.dll` (set via `OUTPUT_NAME` property on the `darkmode_proxy` target).
