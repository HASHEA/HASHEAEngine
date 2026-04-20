@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

if not exist "%~dp0premake5.exe" (
    echo [Error] premake5.exe was not found in "%~dp0".
    exit /b 1
)

echo [Premake] Generating Visual Studio 2022 solution...
"%~dp0premake5.exe" vs2022
if errorlevel 1 (
    echo [Error] premake generation failed.
    exit /b 1
)

echo [Premake] Done.
exit /b 0

:help
echo Usage: generate_vs2022.bat
echo.
echo Generates the Visual Studio 2022 solution from premake5.lua.
exit /b 0
