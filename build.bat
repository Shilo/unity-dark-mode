@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment
    exit /b 1
)
echo MSVC environment ready
where cl
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
echo BUILD SUCCESS
dir build\version.dll
