@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "POWERSHELL_EXE=C:\Program Files\PowerShell\7\pwsh.exe"
if not exist "%POWERSHELL_EXE%" set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\CheckArchBoundary.ps1" %*
exit /b %ERRORLEVEL%
