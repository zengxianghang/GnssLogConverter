@echo off
setlocal

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake.exe was not found in PATH.
    echo Install CMake or use the Visual Studio Developer Command Prompt.
    exit /b 1
)

if exist build rmdir /s /q build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 exit /b 1

echo.
echo Build and tests completed successfully.
echo EXE: build\Release\GnssLogConverter.exe
endlocal
