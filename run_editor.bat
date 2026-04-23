@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

call "%~dp0run.bat" editor %*
exit /b %ERRORLEVEL%

:help
echo Usage: run_editor.bat [Backend] [Configuration] [AppArgs...]
echo.
echo This is a compatibility wrapper around run.bat.
echo Examples:
echo   run_editor.bat
echo   run_editor.bat dx12 Debug --smoke-test-seconds=5
echo   run_editor.bat vulkan Debug --smoke-test=10
exit /b 0
