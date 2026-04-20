@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Debug"

set "RUN_DIR=%~dp0product\bin64\%CONFIG%-windows-x86_64"
set "EDITOR_EXE=%RUN_DIR%\Editor.exe"

if not exist "%EDITOR_EXE%" (
    echo [Error] "%EDITOR_EXE%" was not found.
    echo [Hint] Run build_editor.bat %CONFIG% x64 first.
    exit /b 1
)

echo [Run] Launching Editor from "%RUN_DIR%"...
pushd "%RUN_DIR%"
"%EDITOR_EXE%"
set "EXIT_CODE=%ERRORLEVEL%"
popd

exit /b %EXIT_CODE%

:help
echo Usage: run_editor.bat [Configuration]
echo.
echo Default:
echo   Configuration = Debug
echo.
echo Example:
echo   run_editor.bat Debug
exit /b 0
