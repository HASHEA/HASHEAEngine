@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Debug"

set "PLATFORM=%~2"
if not defined PLATFORM set "PLATFORM=x64"

if not exist "%~dp0AshEngine.sln" (
    echo [Build] AshEngine.sln was not found. Generating it first...
    call "%~dp0generate_vs2022.bat"
    if errorlevel 1 exit /b 1
)

call :find_msbuild
if errorlevel 1 exit /b 1

echo [Build] Configuration: %CONFIG%
echo [Build] Platform: %PLATFORM%
echo [Build] Using MSBuild: %MSBUILD_EXE%
"%MSBUILD_EXE%" "%~dp0AshEngine.sln" /t:Editor /p:Configuration=%CONFIG%;Platform=%PLATFORM%
if errorlevel 1 (
    echo [Error] Build failed.
    exit /b 1
)

echo [Build] Editor build succeeded.
exit /b 0

:find_msbuild
set "MSBUILD_EXE="

for /f "delims=" %%I in ('where MSBuild.exe 2^>nul') do (
    set "MSBUILD_EXE=%%I"
    goto :msbuild_found
)

set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE_EXE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE_EXE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD_EXE=%%I"
        goto :msbuild_found
    )
)

echo [Error] MSBuild.exe was not found.
echo [Hint] Install Visual Studio 2022 or the Build Tools workload with MSBuild.
exit /b 1

:msbuild_found
exit /b 0

:help
echo Usage: build_editor.bat [Configuration] [Platform]
echo.
echo Defaults:
echo   Configuration = Debug
echo   Platform      = x64
echo.
echo Example:
echo   build_editor.bat Debug x64
exit /b 0
