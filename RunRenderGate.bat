@echo off
setlocal

if /I "%~1"=="/?" goto :help
if /I "%~1"=="-?" goto :help
if /I "%~1"=="/help" goto :help
if /I "%~1"=="--help" goto :help

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\RunRenderGate.ps1" %*
exit /b %ERRORLEVEL%

:help
echo AshEngine Render Gate launcher
echo.
echo Usage:
echo   RunRenderGate.bat
echo       Run golden regression for vulkan + dx12 and the cross-backend diff.
echo.
echo   RunRenderGate.bat -Scenes particles -BlessGolden
echo       After visual approval, refresh only the selected scene's golden baselines.
echo.
echo Common arguments:
echo   -Configuration Debug
echo   -Backends vulkan,dx12
echo   -Scenes sandbox,particles
echo   -TimeoutSeconds 120
echo   -ProcessTimeoutGraceSeconds 15
echo   -GoldenSsimThreshold 0.995
echo   -CrossSsimThreshold 0.99
echo   -SkipCrossBackend
exit /b 0
