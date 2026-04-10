#include "darkmode.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <commctrl.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// Undocumented UAH (User Action Handler) menu messages
// These are sent by Windows internally when it draws the menu bar.
// ---------------------------------------------------------------------------
enum
{
    WM_UAHDESTROYWINDOW   = 0x0090,
    WM_UAHDRAWMENU        = 0x0091,
    WM_UAHDRAWMENUITEM    = 0x0092,
    WM_UAHINITMENU        = 0x0093,
    WM_UAHMEASUREMENUITEM = 0x0094,
    WM_UAHNCPAINTMENUPOPUP= 0x0095
};

// ---------------------------------------------------------------------------
// Undocumented uxtheme.dll preferred-app-mode enum
// Available since Windows 10 1903 via ordinal #135.
// ---------------------------------------------------------------------------
enum class PreferredAppMode
{
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};

// ---------------------------------------------------------------------------
// Undocumented UAH menu structures
// ---------------------------------------------------------------------------
typedef union tagUAHMENUITEMMETRICS
{
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS
{
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU
{
    HMENU hmenu;
    HDC   hdc;
    DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM
{
    int                iPosition;
    UAHMENUITEMMETRICS umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct UAHDRAWMENUITEM
{
    DRAWITEMSTRUCT dis;
    UAHMENU        um;
    UAHMENUITEM    umi;
} UAHDRAWMENUITEM;

typedef struct tagUAHMEASUREMENUITEM
{
    MEASUREITEMSTRUCT mis;
    UAHMENU           um;
    UAHMENUITEM       umi;
} UAHMEASUREMENUITEM;

// ---------------------------------------------------------------------------
// Dark theme colours (matched to Unity 6 Editor dark theme)
//
// Source: Unity Editor Design System color palette
//   App Toolbar Background: #191919
//   App Toolbar Button Hover: #424242
//   App Toolbar Button Pressed: #6A6A6A
//   Default Text: #D2D2D2
//   Disabled/secondary Text: #BDBDBD
// ---------------------------------------------------------------------------
static const COLORREF CLR_TEXT          = RGB(210, 210, 210);  // #D2D2D2
static const COLORREF CLR_TEXT_DISABLED = RGB(189, 189, 189);  // #BDBDBD
static const COLORREF CLR_BG           = RGB(25, 25, 25);     // #191919
static const COLORREF CLR_BG_HOT       = RGB(66, 66, 66);     // #424242
static const COLORREF CLR_BG_SELECTED  = RGB(106, 106, 106);  // #6A6A6A
static const COLORREF CLR_BTN          = RGB(88, 88, 88);      // #585858
static const COLORREF CLR_BTN_BORDER   = RGB(48, 48, 48);      // #303030

static HBRUSH g_brBg          = nullptr;
static HBRUSH g_brBgHot       = nullptr;
static HBRUSH g_brBgSelected  = nullptr;

static HTHEME g_menuTheme = nullptr;
static HHOOK  g_cbtHook   = nullptr;

// Forward declarations
static LRESULT CALLBACK SubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK CBTProc(int, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void CreateBrushes()
{
    g_brBg         = CreateSolidBrush(CLR_BG);
    g_brBgHot      = CreateSolidBrush(CLR_BG_HOT);
    g_brBgSelected = CreateSolidBrush(CLR_BG_SELECTED);
}

static void DestroyBrushes()
{
    if (g_brBg)         { DeleteObject(g_brBg);         g_brBg = nullptr; }
    if (g_brBgHot)      { DeleteObject(g_brBgHot);      g_brBgHot = nullptr; }
    if (g_brBgSelected) { DeleteObject(g_brBgSelected);  g_brBgSelected = nullptr; }
}

static bool IsWndClass(HWND hWnd, const wchar_t* name)
{
    wchar_t buf[256];
    GetClassNameW(hWnd, buf, 256);
    return _wcsicmp(name, buf) == 0;
}

static bool IsUnityWindow(HWND hWnd)
{
    wchar_t buf[256];
    GetClassNameW(hWnd, buf, 256);
    // Match "UnityContainerWndClass" and related Unity container classes
    return wcsstr(buf, L"UnityContainer") != nullptr;
}

static bool ShouldSubclass(HWND hWnd)
{
    if (IsUnityWindow(hWnd))                        return true;
    if (IsWndClass(hWnd, L"#32770"))                return true;  // Dialog
    if (IsWndClass(hWnd, L"Button"))                return true;
    if (IsWndClass(hWnd, L"tooltips_class32"))       return true;
    if (IsWndClass(hWnd, L"ComboBox"))               return true;
    if (IsWndClass(hWnd, L"SysListView32"))          return true;
    if (IsWndClass(hWnd, L"SysTreeView32"))          return true;
    if (IsWndClass(hWnd, L"SysHeader32"))             return true;

    // Also subclass any window that owns a menu bar
    if (GetMenu(hWnd) != nullptr)                    return true;

    return false;
}

// ---------------------------------------------------------------------------
// EnableDarkMode -- per-window and per-process
//
// Uses four undocumented uxtheme.dll ordinals (stable since Win 10 1903):
//   #133  AllowDarkModeForWindow           -- per-window opt-in
//   #135  SetPreferredAppMode              -- process-level dark mode
//   #104  RefreshImmersiveColorPolicyState  -- commit colour policy change
//   #136  FlushMenuThemes                  -- refresh cached menu visuals
// ---------------------------------------------------------------------------
static void EnableDarkModeForWindow(HWND hWnd)
{
    using fnAllowDarkModeForWindow          = BOOL(WINAPI*)(HWND, BOOL);
    using fnSetPreferredAppMode             = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using fnFlushMenuThemes                 = void(WINAPI*)();
    using fnRefreshImmersiveColorPolicyState = void(WINAPI*)();

    static fnAllowDarkModeForWindow          pAllowDarkModeForWindow          = nullptr;
    static fnSetPreferredAppMode             pSetPreferredAppMode             = nullptr;
    static fnFlushMenuThemes                 pFlushMenuThemes                 = nullptr;
    static fnRefreshImmersiveColorPolicyState pRefreshImmersiveColorPolicyState = nullptr;
    static bool resolved = false;

    if (!resolved)
    {
        resolved = true;
        HMODULE hUx = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (hUx)
        {
            pAllowDarkModeForWindow          = reinterpret_cast<fnAllowDarkModeForWindow>(
                GetProcAddress(hUx, MAKEINTRESOURCEA(133)));
            pSetPreferredAppMode             = reinterpret_cast<fnSetPreferredAppMode>(
                GetProcAddress(hUx, MAKEINTRESOURCEA(135)));
            pFlushMenuThemes                 = reinterpret_cast<fnFlushMenuThemes>(
                GetProcAddress(hUx, MAKEINTRESOURCEA(136)));
            pRefreshImmersiveColorPolicyState = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(
                GetProcAddress(hUx, MAKEINTRESOURCEA(104)));
        }
    }

    if (hWnd)
    {
        // Dark title bar via documented DWM attribute (Windows 10 2004+ / Win 11)
        static constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR = 20;
        const BOOL useDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR, &useDark, sizeof(useDark));

        // Per-window dark mode via undocumented ordinal #133
        if (pAllowDarkModeForWindow)
            pAllowDarkModeForWindow(hWnd, TRUE);
    }
    else
    {
        // Process-level: force dark context menus via ordinal #135
        if (pSetPreferredAppMode)
            pSetPreferredAppMode(PreferredAppMode::ForceDark);

        // Commit the colour-policy change and flush cached menu themes
        // so dark mode takes effect immediately on existing menus.
        if (pRefreshImmersiveColorPolicyState)
            pRefreshImmersiveColorPolicyState();
        if (pFlushMenuThemes)
            pFlushMenuThemes();
    }
}

// ---------------------------------------------------------------------------
// Draw the 1 px line below the menu bar in dark colour so there is no
// bright seam between the menu bar and the client area.
// ---------------------------------------------------------------------------
static void DrawMenuBarBottomLine(HWND hWnd)
{
    MENUBARINFO mbi = { sizeof(mbi) };
    if (!GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
        return;

    RECT rcClient = {0};
    GetClientRect(hWnd, &rcClient);
    MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&rcClient), 2);

    RECT rcWindow = {0};
    GetWindowRect(hWnd, &rcWindow);
    OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);

    RECT rcLine = rcClient;
    rcLine.bottom = rcLine.top;
    rcLine.top--;

    HDC hdc = GetWindowDC(hWnd);
    if (!hdc)
        return;
    FillRect(hdc, &rcLine, g_brBg);
    ReleaseDC(hWnd, hdc);
}

// ---------------------------------------------------------------------------
// Owner-draw dark button  (Unity 6 button colours)
// ---------------------------------------------------------------------------
static void PaintDarkButton(const DRAWITEMSTRUCT& dis)
{
    HWND hwnd = dis.hwndItem;
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Background colour varies with button state
    COLORREF bgClr = CLR_BTN;                                    // #585858
    if (dis.itemState & ODS_SELECTED)
        bgClr = CLR_BG_SELECTED;                                // #6A6A6A pressed
    else if (dis.itemState & ODS_HOTLIGHT)
        bgClr = CLR_BG_HOT;                                     // #424242 hover

    COLORREF txtClr = (dis.itemState & (ODS_GRAYED | ODS_DISABLED))
                        ? CLR_TEXT_DISABLED : CLR_TEXT;

    HDC   hdc     = dis.hDC;
    HBRUSH brush  = CreateSolidBrush(bgClr);
    HPEN   pen    = CreatePen(PS_SOLID, 1, CLR_BTN_BORDER);     // #303030
    HGDIOBJ oldBr = SelectObject(hdc, brush);
    HGDIOBJ oldPn = SelectObject(hdc, pen);

    HFONT font = reinterpret_cast<HFONT>(SendMessage(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;

    SetBkColor(hdc, bgClr);
    SetTextColor(hdc, txtClr);

    Rectangle(hdc, 0, 0, rc.right, rc.bottom);

    if (GetFocus() == hwnd)
    {
        RECT temp = rc;
        InflateRect(&temp, -2, -2);
        DrawFocusRect(hdc, &temp);
    }

    wchar_t buf[128];
    GetWindowTextW(hwnd, buf, 128);
    DrawTextW(hdc, buf, -1, &rc, DT_EDITCONTROL | DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (oldFont)
        SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPn);
    SelectObject(hdc, oldBr);
    DeleteObject(brush);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// Window subclass procedure -- intercepts paint-related messages to draw
// the menu bar, controls, and dialog backgrounds in dark colours.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR /*uIdSubclass*/,
                                     DWORD_PTR /*dwRefData*/)
{
    switch (uMsg)
    {
    // ----- control background colours -----
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcCtl = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdcCtl, CLR_TEXT);
        SetBkColor(hdcCtl, CLR_BG);
        SetBkMode(hdcCtl, OPAQUE);
        return reinterpret_cast<LRESULT>(g_brBg);
    }

    // ----- owner-drawn buttons -----
    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_BUTTON)
        {
            PaintDarkButton(*dis);
            return TRUE;
        }
        break;
    }

    // ----- erase background -----
    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_brBg);
        return 1;
    }

    // ----- non-client paint: redraw the bright seam line -----
    case WM_NCACTIVATE:
    case WM_NCPAINT:
    {
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        DrawMenuBarBottomLine(hWnd);
        return result;
    }

    // ----- non-client create: apply dark themes to controls -----
    case WM_NCCREATE:
    {
        EnableDarkModeForWindow(hWnd);

        if (IsWndClass(hWnd, L"tooltips_class32"))
        {
            SetWindowTheme(hWnd, L"DarkMode_Explorer", nullptr);
        }
        else if (IsWndClass(hWnd, L"ComboBox"))
        {
            SetWindowTheme(hWnd, L"DarkMode_CFD", nullptr);
        }
        else if (IsWndClass(hWnd, L"SysHeader32"))
        {
            SetWindowTheme(hWnd, L"DarkMode_ItemsView", nullptr);
        }
        else if (IsWndClass(hWnd, L"Button"))
        {
            LONG_PTR style   = GetWindowLongPtr(hWnd, GWL_STYLE);
            LONG_PTR btnType = style & BS_TYPEMASK;

            if (btnType == BS_CHECKBOX      || btnType == BS_AUTOCHECKBOX  ||
                btnType == BS_RADIOBUTTON   || btnType == BS_AUTORADIOBUTTON ||
                btnType == BS_3STATE        || btnType == BS_AUTO3STATE)
            {
                SetWindowTheme(hWnd, L"DarkMode_Explorer", nullptr);
            }
            else if (btnType == BS_PUSHBUTTON || btnType == BS_DEFPUSHBUTTON)
            {
                // Convert push buttons to owner-draw so we can paint them dark
                style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
                SetWindowLongPtr(hWnd, GWL_STYLE, style);
            }
        }
        break;
    }

    // ----- create: theme combo-box internals after child windows exist -----
    case WM_CREATE:
    {
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        if (IsWndClass(hWnd, L"ComboBox"))
        {
            COMBOBOXINFO cbi = { sizeof(cbi) };
            if (SendMessage(hWnd, CB_GETCOMBOBOXINFO, 0, reinterpret_cast<LPARAM>(&cbi)))
            {
                if (cbi.hwndList)
                    SetWindowTheme(cbi.hwndList, L"DarkMode_Explorer", nullptr);
            }
        }
        return result;
    }

    // ----- paint: set colours on specific controls -----
    case WM_PAINT:
    {
        if (IsWndClass(hWnd, L"tooltips_class32"))
        {
            SendMessage(hWnd, TTM_SETTIPBKCOLOR,   static_cast<WPARAM>(CLR_BG),   0);
            SendMessage(hWnd, TTM_SETTIPTEXTCOLOR,  static_cast<WPARAM>(CLR_TEXT), 0);
        }
        else if (IsWndClass(hWnd, L"SysTreeView32"))
        {
            TreeView_SetBkColor(hWnd, CLR_BG);
            TreeView_SetTextColor(hWnd, CLR_TEXT);
        }
        else if (IsWndClass(hWnd, L"SysListView32"))
        {
            ListView_SetBkColor(hWnd, CLR_BG);
            ListView_SetTextBkColor(hWnd, CLR_BG);
            ListView_SetTextColor(hWnd, CLR_TEXT);
        }
        break;
    }

    // ----- block style changes that revert dark mode on Unity windows -----
    case WM_STYLECHANGING:
    case WM_STYLECHANGED:
    {
        if (IsUnityWindow(hWnd))
            return 0;
        break;
    }

    // ----- theme changed: reset cached theme handle -----
    case WM_THEMECHANGED:
    {
        if (g_menuTheme)
        {
            CloseThemeData(g_menuTheme);
            g_menuTheme = nullptr;
        }
        break;
    }

    // ----- undocumented: draw the entire menu bar background -----
    case WM_UAHDRAWMENU:
    {
        auto* pUDM = reinterpret_cast<UAHMENU*>(lParam);
        MENUBARINFO mbi = { sizeof(mbi) };
        if (GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
        {
            RECT rcWindow;
            GetWindowRect(hWnd, &rcWindow);

            RECT rcMenu = mbi.rcBar;
            OffsetRect(&rcMenu, -rcWindow.left, -rcWindow.top);

            FillRect(pUDM->hdc, &rcMenu, g_brBg);
        }
        return 0;
    }

    // ----- undocumented: draw a single menu bar item -----
    case WM_UAHDRAWMENUITEM:
    {
        auto* pUDMI = reinterpret_cast<UAHDRAWMENUITEM*>(lParam);

        // Pick background brush and text colour based on item state
        HBRUSH  bgBrush   = g_brBg;
        COLORREF textClr  = CLR_TEXT;

        if (pUDMI->dis.itemState & ODS_HOTLIGHT)
            bgBrush = g_brBgHot;
        if (pUDMI->dis.itemState & ODS_SELECTED)
            bgBrush = g_brBgSelected;
        if (pUDMI->dis.itemState & (ODS_GRAYED | ODS_DISABLED))
            textClr = CLR_TEXT_DISABLED;

        FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, bgBrush);

        // Retrieve menu item text
        wchar_t menuText[256] = {0};
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask    = MIIM_STRING;
        mii.dwTypeData = menuText;
        mii.cch      = (sizeof(menuText) / sizeof(wchar_t)) - 1;
        GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

        // Draw text using the theme engine for consistent font metrics
        if (!g_menuTheme)
            g_menuTheme = OpenThemeData(hWnd, L"Menu");

        if (g_menuTheme)
        {
            DTTOPTS dtOpts  = { sizeof(dtOpts) };
            dtOpts.dwFlags  = DTT_TEXTCOLOR;
            dtOpts.crText   = textClr;

            DWORD flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
            if (pUDMI->dis.itemState & ODS_NOACCEL)
                flags |= DT_HIDEPREFIX;

            DrawThemeTextEx(g_menuTheme, pUDMI->um.hdc,
                            MENU_BARITEM, MBI_NORMAL,
                            menuText, mii.cch,
                            flags, &pUDMI->dis.rcItem, &dtOpts);
        }
        return 0;
    }

    // ----- undocumented: measure menu item (pass through) -----
    case WM_UAHMEASUREMENUITEM:
        break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// CBT hook -- intercepts window creation / destruction so every new window
// automatically gets dark-mode treatment.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(g_cbtHook, nCode, wParam, lParam);

    __try
    {
        switch (nCode)
        {
        case HCBT_CREATEWND:
        {
            HWND hWnd = reinterpret_cast<HWND>(wParam);
            if (ShouldSubclass(hWnd))
            {
                EnableDarkModeForWindow(hWnd);
                SetWindowSubclass(hWnd, SubclassProc, 0, 0);
            }
            break;
        }
        case HCBT_DESTROYWND:
        {
            HWND hWnd = reinterpret_cast<HWND>(wParam);
            if (ShouldSubclass(hWnd))
                RemoveWindowSubclass(hWnd, SubclassProc, 0);
            break;
        }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // Silent no-op: skip dark mode for this window rather than crash Unity
    }

    return CallNextHookEx(g_cbtHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void InitializeDarkMode()
{
    __try
    {
        CreateBrushes();

        // Apply dark mode at the process level (forces dark context menus)
        EnableDarkModeForWindow(nullptr);

        // Dark-mode every existing window belonging to this process
        DWORD pid = GetCurrentProcessId();
        HWND  hWnd = nullptr;
        do
        {
            hWnd = FindWindowEx(nullptr, hWnd, nullptr, nullptr);
            if (hWnd)
            {
                DWORD wndPid = 0;
                GetWindowThreadProcessId(hWnd, &wndPid);
                if (wndPid == pid)
                {
                    EnableDarkModeForWindow(hWnd);
                    if (ShouldSubclass(hWnd))
                        SetWindowSubclass(hWnd, SubclassProc, 0, 0);
                }
            }
        } while (hWnd);

        // Hook future window creation on the current thread
        g_cbtHook = SetWindowsHookEx(WH_CBT, CBTProc, nullptr, GetCurrentThreadId());
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        // Silent no-op: dark mode init failed but Unity continues normally
    }
}

void ShutdownDarkMode()
{
    if (g_cbtHook)
    {
        UnhookWindowsHookEx(g_cbtHook);
        g_cbtHook = nullptr;
    }

    if (g_menuTheme)
    {
        CloseThemeData(g_menuTheme);
        g_menuTheme = nullptr;
    }

    DestroyBrushes();
}
