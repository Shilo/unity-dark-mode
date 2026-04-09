#pragma once

#include <windows.h>

// Initialize the version.dll proxy by loading the real system DLL
// and resolving all exported function addresses.
void InitializeProxy();

// Unload the real version.dll.
void ShutdownProxy();
