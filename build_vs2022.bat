@echo off
setlocal EnableExtensions EnableDelayedExpansion

pushd "%~dp0"

set "VSWHERE="
set "VS_PATH="
set "VSDEVCMD="
set "VCVARS64="
set "CL_PATH="

rem -----------------------------------------------------------------------------
rem First follow the same idea as LogMerger/scripts/build_msvc.bat:
rem if cl.exe is already available, use it directly.
rem -----------------------------------------------------------------------------
for /f "delims=" %%I in ('where cl.exe 2^>nul') do (
    if not defined CL_PATH set "CL_PATH=%%I"
)
if defined CL_PATH goto :compiler_ready

rem -----------------------------------------------------------------------------
rem Try an already-defined Visual Studio installation first.
rem -----------------------------------------------------------------------------
if defined VSINSTALLDIR (
    if exist "%VSINSTALLDIR%\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VSINSTALLDIR%\Common7\Tools\VsDevCmd.bat"
    if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
)

rem -----------------------------------------------------------------------------
rem Locate vswhere.exe from PATH or the standard Visual Studio Installer path.
rem Do not restrict the query to a fixed VS install directory.
rem -----------------------------------------------------------------------------
if not defined VSDEVCMD if not defined VCVARS64 (
    for /f "delims=" %%I in ('where vswhere.exe 2^>nul') do (
        if not defined VSWHERE set "VSWHERE=%%I"
    )

    if not defined VSWHERE if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" (
        set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    if defined VSWHERE (
        for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if not defined VS_PATH set "VS_PATH=%%I"
        )
        if defined VS_PATH (
            if exist "!VS_PATH!\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=!VS_PATH!\Common7\Tools\VsDevCmd.bat"
            if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

rem -----------------------------------------------------------------------------
rem Explicit fallback locations. This covers standard installs and machines where
rem Visual Studio is installed under a custom system_app directory, e.g.
rem E:\system_app\visual_studio_2022.
rem -----------------------------------------------------------------------------
if not defined VSDEVCMD if not defined VCVARS64 (
    for %%D in (C D E F G H) do (
        if not defined VSDEVCMD if exist "%%D:\system_app\visual_studio_2022\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%D:\system_app\visual_studio_2022\Common7\Tools\VsDevCmd.bat"
        if not defined VCVARS64 if exist "%%D:\system_app\visual_studio_2022\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=%%D:\system_app\visual_studio_2022\VC\Auxiliary\Build\vcvars64.bat"

        if not defined VSDEVCMD if exist "%%D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
        if not defined VSDEVCMD if exist "%%D:\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%D:\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
        if not defined VSDEVCMD if exist "%%D:\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%D:\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
        if not defined VSDEVCMD if exist "%%D:\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%%D:\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )
)

rem Standard Program Files fallbacks, including VS 2019 in case the installed
rem MSVC toolchain is older but still supports this C++11 project.
if not defined VSDEVCMD if not defined VCVARS64 (
    for %%V in (2022 2019) do (
        for %%E in (Community Professional Enterprise BuildTools) do (
            if not defined VSDEVCMD if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat"
            if not defined VCVARS64 if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            if not defined VSDEVCMD if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\Common7\Tools\VsDevCmd.bat"
            if not defined VCVARS64 if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if defined VSDEVCMD (
    echo [INFO] Initializing MSVC with:
    echo        !VSDEVCMD!
    call "!VSDEVCMD!" -arch=x64 -host_arch=x64
    if errorlevel 1 goto :vs_init_failed
) else if defined VCVARS64 (
    echo [INFO] Initializing MSVC with:
    echo        !VCVARS64!
    call "!VCVARS64!"
    if errorlevel 1 goto :vs_init_failed
) else (
    echo [ERROR] Visual Studio C++ build environment was not found.
    echo.
    echo Checked:
    echo   - cl.exe already in PATH
    echo   - VSINSTALLDIR
    echo   - vswhere.exe
    echo   - standard Visual Studio 2022/2019 locations
    echo   - C: through H:\system_app\visual_studio_2022
    echo.
    echo If Visual Studio is installed elsewhere, run this once from an
    echo "x64 Native Tools Command Prompt for VS" or edit the custom path list.
    popd
    exit /b 1
)

for /f "delims=" %%I in ('where cl.exe 2^>nul') do (
    if not defined CL_PATH set "CL_PATH=%%I"
)
if not defined CL_PATH (
    echo [ERROR] Visual Studio environment loaded, but cl.exe is still unavailable.
    popd
    exit /b 1
)

:compiler_ready
echo [INFO] MSVC compiler:
echo        %CL_PATH%

rem -----------------------------------------------------------------------------
rem Compile directly with cl.exe, matching the proven LogMerger build approach.
rem This intentionally does not depend on CMake.
rem -----------------------------------------------------------------------------
if exist build rmdir /s /q build
mkdir build\Release >nul 2>nul
mkdir build\obj_app >nul 2>nul
mkdir build\obj_test >nul 2>nul
mkdir build\obj_com1 >nul 2>nul

set "CORE_SOURCES=src\byte_io.cpp src\crc32.cpp src\range_converter.cpp src\novatel\novatel_protocol.cpp src\novatel\novatel_range.cpp src\unicore\unicore_protocol.cpp src\unicore\unicore_obsvm.cpp"
set "COMMON_FLAGS=/nologo /EHsc /O2 /std:c++14 /W4 /permissive- /I src"

echo [INFO] Building GnssLogConverter.exe ...
cl %COMMON_FLAGS% /Fo"build\obj_app\\" /Fe"build\Release\GnssLogConverter.exe" src\main.cpp %CORE_SOURCES%
if errorlevel 1 goto :build_failed

echo [INFO] Building gnsslog_tests.exe ...
cl %COMMON_FLAGS% /Fo"build\obj_test\\" /Fe"build\Release\gnsslog_tests.exe" tests\test_core.cpp %CORE_SOURCES%
if errorlevel 1 goto :build_failed

echo [INFO] Building gnsslog_com1_roundtrip_test.exe ...
cl %COMMON_FLAGS% /Fo"build\obj_com1\\" /Fe"build\Release\gnsslog_com1_roundtrip_test.exe" tests\test_com1_roundtrip.cpp %CORE_SOURCES%
if errorlevel 1 goto :build_failed

echo [INFO] Running core tests ...
"build\Release\gnsslog_tests.exe"
if errorlevel 1 goto :test_failed

echo [INFO] Running COM1 -^> 0x20 -^> COM1 round-trip test ...
"build\Release\gnsslog_com1_roundtrip_test.exe"
if errorlevel 1 goto :test_failed

echo [PASS] COM1 round-trip preserved: COM1 -^> 0x20 -^> COM1

echo.
echo [SUCCESS] Build and tests completed successfully.
echo [SUCCESS] EXE: %CD%\build\Release\GnssLogConverter.exe
popd
endlocal
exit /b 0

:vs_init_failed
echo [ERROR] Failed to initialize the Visual Studio x64 build environment.
popd
endlocal
exit /b 1

:build_failed
echo [ERROR] MSVC compilation failed.
popd
endlocal
exit /b 1

:test_failed
echo [ERROR] Tests failed.
popd
endlocal
exit /b 1
