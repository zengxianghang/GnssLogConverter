@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem build_pos_convert.bat
rem Automatically locate Visual Studio / MSVC x64 and compile pos_convert.cpp
rem
rem Usage:
rem   Double-click: build_pos_convert.bat
rem   Command line: build_pos_convert.bat [source.cpp] [output.exe] [nopause]
rem
rem Defaults:
rem   source = pos_convert.cpp
rem   output = pos_convert.exe
rem ============================================================

pushd "%~dp0" >nul

set "SRC=pos_convert.cpp"
set "OUT=pos_convert.exe"
set "NO_PAUSE=0"

if not "%~1"=="" set "SRC=%~1"
if not "%~2"=="" set "OUT=%~2"
if /I "%~1"=="nopause" (
    set "SRC=pos_convert.cpp"
    set "NO_PAUSE=1"
)
if /I "%~2"=="nopause" (
    set "OUT=pos_convert.exe"
    set "NO_PAUSE=1"
)
if /I "%~3"=="nopause" set "NO_PAUSE=1"

if not exist "%SRC%" (
    echo.
    echo [ERROR] Source file not found:
    echo         "%CD%\%SRC%"
    goto :FAILED
)

rem If cl.exe is already available, use the current environment.
where cl.exe >nul 2>nul
if not errorlevel 1 goto :COMPILE

echo [INFO] Searching for Visual Studio...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VSINSTALL set "VSINSTALL=%%I"
    )
)

if defined VSINSTALL (
    set "VCVARS=!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
    if exist "!VCVARS!" (
        echo [INFO] Visual Studio found:
        echo        "!VSINSTALL!"
        echo [INFO] Initializing x64 compiler environment...
        call "!VCVARS!"
        if errorlevel 1 goto :INIT_FAILED
        goto :CHECK_CL
    )
)

rem Fallback for common Visual Studio locations.
for %%Y in (2022 2019 2017) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :FALLBACK_FOUND
        )
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :FALLBACK_FOUND
        )
    )
)

echo.
echo [ERROR] Visual Studio C++ x64 build tools were not found.
echo         Open Visual Studio Installer and install:
echo         "Desktop development with C++"
goto :FAILED

:FALLBACK_FOUND
echo [INFO] Using Visual Studio build environment:
echo        "%VCVARS%"
call "%VCVARS%"
if errorlevel 1 goto :INIT_FAILED

goto :CHECK_CL

:INIT_FAILED
echo.
echo [ERROR] Failed to initialize the Visual Studio compiler environment.
goto :FAILED

:CHECK_CL
where cl.exe >nul 2>nul
if errorlevel 1 (
    echo.
    echo [ERROR] Visual Studio was found, but cl.exe is unavailable.
    echo         Install "Desktop development with C++".
    goto :FAILED
)

:COMPILE
echo.
echo ============================================================
echo Compiling
 echo Source : "%SRC%"
echo Output : "%OUT%"
echo ============================================================
echo.

cl.exe /nologo /O2 /EHsc /W4 /std:c++14 /utf-8 /D_CRT_SECURE_NO_WARNINGS "%SRC%" /Fe:"%OUT%"
set "BUILD_RC=%ERRORLEVEL%"

if not "%BUILD_RC%"=="0" (
    echo.
    echo [ERROR] BUILD FAILED. Error code: %BUILD_RC%
    goto :FAILED
)

if not exist "%OUT%" (
    echo.
    echo [ERROR] Compiler reported success, but output EXE was not found.
    goto :FAILED
)

echo.
echo ============================================================
echo BUILD SUCCESS
 echo EXE: "%CD%\%OUT%"
echo ============================================================
echo.

popd >nul
if "%NO_PAUSE%"=="0" pause
exit /b 0

:FAILED
echo.
echo [FAILED] Build was not completed.
echo.
popd >nul
if "%NO_PAUSE%"=="0" pause
exit /b 1
