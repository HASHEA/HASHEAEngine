@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Debug"
set "PLATFORM=%~2"
if not defined PLATFORM set "PLATFORM=x64"
set "MSBUILD_EXE="
for /f "delims=" %%I in ('where MSBuild.exe 2^>nul') do (
    set "MSBUILD_EXE=%%I"
    goto :found
)
set "VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE_EXE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE_EXE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD_EXE=%%I"
        goto :found
    )
)
echo [Error] MSBuild not found.
exit /b 1
:found
echo [Build] %MSBUILD_EXE%
"%MSBUILD_EXE%" "%~dp0AshEngine.sln" /t:Sandbox /p:Configuration=%CONFIG%;Platform=%PLATFORM% /m /v:minimal
exit /b %errorlevel%
