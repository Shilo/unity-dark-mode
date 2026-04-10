#include "version_proxy.h"

// ---------------------------------------------------------------------------
// version.dll proxy
//
// This file loads the real system version.dll and fills the function pointer
// table. The actual trampolines are in version_proxy.asm (MASM64), where
// each export is a single JMP QWORD PTR through g_originalFuncs[i].
//
// The .def file maps the public export names (GetFileVersionInfoA, etc.)
// to the Proxy_* symbols defined in the .asm file.
// ---------------------------------------------------------------------------

// Function pointer table -- filled here, jumped through by the .asm trampolines.
extern "C" FARPROC g_originalFuncs[17] = {0};

static HMODULE g_realVersionDll = nullptr;

static const char* g_functionNames[17] = {
    "GetFileVersionInfoA",
    "GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA",
    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW",
    "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",
    "VerFindFileA",
    "VerFindFileW",
    "VerInstallFileA",
    "VerInstallFileW",
    "VerLanguageNameA",
    "VerLanguageNameW",
    "VerQueryValueA",
    "VerQueryValueW",
};

// Safe fallback for any export that could not be resolved from the real DLL.
// Returns 0 / FALSE which is the typical failure return for version.dll APIs.
static FARPROC WINAPI FallbackStub()
{
    return 0;
}

void InitializeProxy()
{
    // Pre-fill every slot with the safe fallback so the MASM trampolines
    // never jump through a null pointer, even if the real DLL fails to load.
    for (int i = 0; i < 17; ++i)
        g_originalFuncs[i] = reinterpret_cast<FARPROC>(FallbackStub);

    // Build the path to the real system version.dll.
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t dllPath[MAX_PATH];
    wsprintfW(dllPath, L"%s\\version.dll", systemDir);

    g_realVersionDll = LoadLibraryW(dllPath);
    if (!g_realVersionDll)
        return;

    for (int i = 0; i < 17; ++i)
    {
        FARPROC proc = GetProcAddress(g_realVersionDll, g_functionNames[i]);
        if (proc)
            g_originalFuncs[i] = proc;
    }
}

void ShutdownProxy()
{
    if (g_realVersionDll)
    {
        FreeLibrary(g_realVersionDll);
        g_realVersionDll = nullptr;
    }
}
