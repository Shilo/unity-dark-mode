#pragma once

#include <windows.h>

// Apply dark mode to all existing windows and install hooks
// for future window creation.
void InitializeDarkMode();

// Remove hooks and clean up resources.
void ShutdownDarkMode();
