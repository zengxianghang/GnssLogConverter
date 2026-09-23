@echo off
setlocal EnableExtensions

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_PATH="
set "VSDEVCMD="
set "CMAKE_EXE="
set "CTEST_EXE="

rem -----------------------------------------------------------------------------
rem Locate Visual Studio 2022 with the C++ toolchain.
rem Prefer vswhere because it also works with non-default installation paths.
rem -----------------------------------------------------------------------------
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VS_PATH set "VS_PATH=%%I"
    )
)

rem Fallback for machines where vswhere is unavailable.
if not defined VS_PATH (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if not defined VS_PATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" (
            set "VS_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\%%E"
        )
    )
)

if not defined VS_PATH (
    echo [ERROR] Visual Studio 2022 with C++ build tools was not found.
    echo         Install the "Desktop development with C++" workload.
    exit /b 1
)

set "VSDEVCMD=%VS_PATH%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo [ERROR] VsDevCmd.bat was not found under:
    echo         %VS_PATH%
    exit /b 1
)

echo [INFO] Visual Studio found:
echo        %VS_PATH%

echo [INFO] Initializing x64 MSVC environment...
call "%VSDEVCMD%" -arch=x64 -host_arch=x64
if errorlevel 1 (
    echo [ERROR] Failed to initialize the Visual Studio build environment.
    exit /b 1
)

rem -----------------------------------------------------------------------------
rem Locate CMake. Prefer a PATH installation, otherwise use Visual Studio's
rem bundled CMake when available.
rem -----------------------------------------------------------------------------
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do (
    if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
)

if not defined CMAKE_EXE if exist "%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
    set "CMAKE_EXE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

if not defined CMAKE_EXE (
    echo [ERROR] CMake was not found in PATH or in the Visual Studio installation.
    echo         Add the Visual Studio CMake component or install CMake.
    exit /b 1
)

for %%I in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpIctest.exe"
if not exist "%CTEST_EXE%" (
    for /f "delims=" %%I in ('where ctest.exe 2^>nul') do (
        if not exist "%CTEST_EXE%" set "CTEST_EXE=%%I"
    )
)

if not exist "%CTEST_EXE%" (
    echo [ERROR] ctest.exe was not found next to CMake or in PATH.
    exit /b 1
)

echo [INFO] CMake:
echo        %CMAKE_EXE%

rem -----------------------------------------------------------------------------
rem Configure, build and test.
rem -----------------------------------------------------------------------------
if exist build rmdir /s /q build

"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

"%CMAKE_EXE%" --build build --config Release --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

"%CTEST_EXE%" --test-dir build -C Release --output-on-failure
if errorlevel 1 (
    echo [ERROR] Tests failed.
    exit /b 1
)

echo.
echo [SUCCESS] Build and tests completed successfully.
echo [SUCCESS] EXE: %CD%\build\Release\GnssLogConverter.exe

endlocal
exit /b 0
