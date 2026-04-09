#include <windows.h>
#include "version_proxy.h"
#include "darkmode.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InitializeProxy();
        InitializeDarkMode();
        break;

    case DLL_PROCESS_DETACH:
        ShutdownDarkMode();
        ShutdownProxy();
        break;
    }

    return TRUE;
}
