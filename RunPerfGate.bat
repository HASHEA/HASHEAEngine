@echo off
setlocal

set "REPO_ROOT=%~dp0"
set "MENU_SCRIPT=%REPO_ROOT%scripts\RunPerfGateMenu.ps1"
set "RUNNER_SCRIPT=%REPO_ROOT%scripts\RunPerfGate.ps1"

if /I "%~1"=="/?" goto :help
if /I "%~1"=="-?" goto :help
if /I "%~1"=="/help" goto :help
if /I "%~1"=="--help" goto :help

if "%~1"=="" goto :menu
if /I "%~1"=="/menu" goto :menu
if /I "%~1"=="--menu" goto :menu

powershell -NoProfile -ExecutionPolicy Bypass -File "%RUNNER_SCRIPT%" %*
exit /b %ERRORLEVEL%

:menu
powershell -NoProfile -ExecutionPolicy Bypass -File "%MENU_SCRIPT%"
exit /b %ERRORLEVEL%

:help
echo AshEngine Perf Gate launcher
echo.
echo Usage:
echo   RunPerfGate.bat
echo       Open the interactive console menu.
echo.
echo   RunPerfGate.bat /menu
echo       Open the interactive console menu.
echo.
echo   RunPerfGate.bat -Profile Standard -SkipBuild
echo       Pass arguments directly to scripts\RunPerfGate.ps1.
echo.
echo Common arguments:
echo   -Profile Standard
echo   -Configuration Debug
echo   -SkipBuild
echo   -DryRun
exit /b 0
