#include "version_proxy.h"

// ---------------------------------------------------------------------------
// version.dll proxy
//
// This file loads the real system version.dll and forwards all 17 exported
// functions via naked x86 trampolines.  Each trampoline is a single JMP
// through a function-pointer table, so there is zero overhead and no
// register clobbering.
//
// The .def file maps the public export names (GetFileVersionInfoA, etc.)
// to the Proxy_* symbols defined here.
// ---------------------------------------------------------------------------

// Function pointer table -- filled by InitializeProxy().
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

void InitializeProxy()
{
    // Build the path to the real system version.dll.
    // GetSystemDirectoryW returns the correct directory for the
    // current process architecture (SysWOW64 for 32-bit on 64-bit OS).
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t dllPath[MAX_PATH];
    swprintf_s(dllPath, L"%s\\version.dll", systemDir);

    g_realVersionDll = LoadLibraryW(dllPath);
    if (!g_realVersionDll)
        return;

    for (int i = 0; i < 17; ++i)
    {
        g_originalFuncs[i] = GetProcAddress(g_realVersionDll, g_functionNames[i]);
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

// ---------------------------------------------------------------------------
// Naked trampolines (x86 only -- MSVC inline asm)
//
// Each stub jumps directly to the corresponding slot in g_originalFuncs[].
// Because the jump is indirect through a memory operand, the calling
// convention and stack layout are preserved exactly.
// ---------------------------------------------------------------------------

extern "C" __declspec(naked) void Proxy_GetFileVersionInfoA()      { __asm { jmp dword ptr [g_originalFuncs +  0] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoByHandle(){ __asm { jmp dword ptr [g_originalFuncs +  4] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoExA()     { __asm { jmp dword ptr [g_originalFuncs +  8] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoExW()     { __asm { jmp dword ptr [g_originalFuncs + 12] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoSizeA()   { __asm { jmp dword ptr [g_originalFuncs + 16] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoSizeExA() { __asm { jmp dword ptr [g_originalFuncs + 20] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoSizeExW() { __asm { jmp dword ptr [g_originalFuncs + 24] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoSizeW()   { __asm { jmp dword ptr [g_originalFuncs + 28] } }
extern "C" __declspec(naked) void Proxy_GetFileVersionInfoW()       { __asm { jmp dword ptr [g_originalFuncs + 32] } }
extern "C" __declspec(naked) void Proxy_VerFindFileA()              { __asm { jmp dword ptr [g_originalFuncs + 36] } }
extern "C" __declspec(naked) void Proxy_VerFindFileW()              { __asm { jmp dword ptr [g_originalFuncs + 40] } }
extern "C" __declspec(naked) void Proxy_VerInstallFileA()           { __asm { jmp dword ptr [g_originalFuncs + 44] } }
extern "C" __declspec(naked) void Proxy_VerInstallFileW()           { __asm { jmp dword ptr [g_originalFuncs + 48] } }
extern "C" __declspec(naked) void Proxy_VerLanguageNameA()          { __asm { jmp dword ptr [g_originalFuncs + 52] } }
extern "C" __declspec(naked) void Proxy_VerLanguageNameW()          { __asm { jmp dword ptr [g_originalFuncs + 56] } }
extern "C" __declspec(naked) void Proxy_VerQueryValueA()            { __asm { jmp dword ptr [g_originalFuncs + 60] } }
extern "C" __declspec(naked) void Proxy_VerQueryValueW()            { __asm { jmp dword ptr [g_originalFuncs + 64] } }
