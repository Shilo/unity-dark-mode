@echo off
setlocal
set "ROOT_DIR=%~dp0"
set "BUILD_DIR=%ROOT_DIR%build"
set "OUTPUT_FILE=%BUILD_DIR%\version.dll"

:: Find vcvars64.bat using vswhere (shipped with VS 2017+)
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
        if exist "%%~P\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%~P\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
            goto :found
        )
    )
    echo ERROR: Could not find Visual Studio. Install the C++ desktop workload.
    goto :fail
)

:: Use vswhere to locate the newest install
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set VSDIR=%%i
)
if not defined VSDIR (
    echo ERROR: Could not find Visual Studio with C++ support.
    goto :fail
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:found
echo MSVC x64 environment ready

cd /d "%ROOT_DIR%"
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

cmake -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo ERROR: CMake configure failed
    goto :fail
)

cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Build failed
    goto :fail
)

echo.
echo BUILD SUCCESS
echo Output: %OUTPUT_FILE%
echo Folder: %BUILD_DIR%
if exist "%OUTPUT_FILE%" dir /b "%OUTPUT_FILE%"

if defined BUILD_OPENER (
    call "%BUILD_OPENER%" "%BUILD_DIR%"
) else (
    start "" explorer "%BUILD_DIR%" >nul 2>&1
)

exit /b 0

:fail
if not defined BUILD_NO_PAUSE pause
exit /b 1
