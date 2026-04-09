@echo off
setlocal

:: Find vcvars32.bat using vswhere (shipped with VS 2017+)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    :: Fallback: try common install paths
    for %%P in (
        "C:\Program Files\Microsoft Visual Studio\18\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
    ) do (
        if exist "%%~P\VC\Auxiliary\Build\vcvars32.bat" (
            call "%%~P\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
            goto :found
        )
    )
    echo ERROR: Could not find Visual Studio. Install the C++ desktop workload.
    exit /b 1
)

:: Use vswhere to locate the newest install
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set VSDIR=%%i
)
if not defined VSDIR (
    echo ERROR: Could not find Visual Studio with C++ support.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1

:found
echo MSVC x86 environment ready

cd /d "%~dp0"
if exist build rmdir /s /q build

cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

cmake --build build
if errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)

echo.
echo BUILD SUCCESS
echo Output: %~dp0build\version.dll
dir /b build\version.dll
