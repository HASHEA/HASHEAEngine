@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

set "MODE=single"
set "TARGET=Editor"
set "BACKEND=CURRENT"
set "CONFIG=Debug"
set "APP_ARGS="
set "EXIT_CODE=0"
set "ENGINE_CONFIG=%~dp0product\config\Engine.ini"
set "ENGINE_CONFIG_BACKUP=%TEMP%\ash_engine_run_%RANDOM%_%RANDOM%.ini"

if not exist "%ENGINE_CONFIG%" (
    echo [Error] "%ENGINE_CONFIG%" was not found.
    exit /b 1
)

copy /y "%ENGINE_CONFIG%" "%ENGINE_CONFIG_BACKUP%" >nul
if errorlevel 1 (
    echo [Error] Failed to create a temporary backup for Engine.ini.
    exit /b 1
)

call :try_parse_target "%~1" PARSED_TARGET
if not errorlevel 1 (
    if /I "%PARSED_TARGET%"=="ALL" (
        set "MODE=matrix"
    ) else (
        set "TARGET=%PARSED_TARGET%"
    )
    shift
)

if /I "%MODE%"=="matrix" goto :parse_matrix

call :try_parse_backend "%~1" PARSED_BACKEND
if not errorlevel 1 (
    set "BACKEND=%PARSED_BACKEND%"
    shift
)

set "ARG1=%~1"
if defined ARG1 if not "%ARG1:~0,1%"=="-" (
    set "CONFIG=%~1"
    shift
)

:capture_single_remaining_args
if "%~1"=="" goto :single_args_captured
if defined APP_ARGS (
    set "APP_ARGS=!APP_ARGS! "%~1""
) else (
    set "APP_ARGS="%~1""
)
shift
goto :capture_single_remaining_args

:single_args_captured
call :run_single "%TARGET%" "%BACKEND%" "%CONFIG%"
set "EXIT_CODE=%ERRORLEVEL%"
goto :cleanup

:parse_matrix
set "ARG1=%~1"
if defined ARG1 if not "%ARG1:~0,1%"=="-" (
    set "CONFIG=%~1"
    shift
)
:mx_arg_loop
if "%~1"=="" goto :mx_args_done
if defined APP_ARGS (
    set "APP_ARGS=!APP_ARGS! "%~1""
) else (
    set "APP_ARGS="%~1""
)
shift
goto :mx_arg_loop

:mx_args_done
call :run_single "Editor" "DX12" "%CONFIG%"
set "RUN_RESULT=!ERRORLEVEL!"
if not "!RUN_RESULT!"=="0" set "EXIT_CODE=!RUN_RESULT!"

call :run_single "Editor" "Vulkan" "%CONFIG%"
set "RUN_RESULT=!ERRORLEVEL!"
if not "!RUN_RESULT!"=="0" set "EXIT_CODE=!RUN_RESULT!"

call :run_single "Sandbox" "DX12" "%CONFIG%"
set "RUN_RESULT=!ERRORLEVEL!"
if not "!RUN_RESULT!"=="0" set "EXIT_CODE=!RUN_RESULT!"

call :run_single "Sandbox" "Vulkan" "%CONFIG%"
set "RUN_RESULT=!ERRORLEVEL!"
if not "!RUN_RESULT!"=="0" set "EXIT_CODE=!RUN_RESULT!"

goto :cleanup

:run_single
set "RUN_TARGET=%~1"
set "RUN_BACKEND=%~2"
set "RUN_CONFIG=%~3"

call :resolve_executable_name "%RUN_TARGET%" RUN_EXE_NAME
if errorlevel 1 exit /b 1

set "RUN_DIR=%~dp0product\bin64\%RUN_CONFIG%-windows-x86_64"
set "RUN_EXE=%RUN_DIR%\%RUN_EXE_NAME%"

if not exist "%RUN_EXE%" (
    echo [Error] "%RUN_EXE%" was not found.
    if /I "%RUN_TARGET%"=="Editor" (
        echo [Hint] Run build_editor.bat %RUN_CONFIG% x64 first.
    ) else (
        echo [Hint] Build the Sandbox target for %RUN_CONFIG% x64 before running it.
    )
    exit /b 1
)

if /I not "%RUN_BACKEND%"=="CURRENT" (
    call :set_backend "%RUN_BACKEND%"
    if errorlevel 1 exit /b 1
    echo [Run] Backend: %RUN_BACKEND%
) else (
    echo [Run] Backend: current Engine.ini setting
)

echo [Run] Target: %RUN_TARGET%
echo [Run] Configuration: %RUN_CONFIG%
echo [Run] Executable: "%RUN_EXE%"
if defined APP_ARGS (
    echo [Run] Args: %APP_ARGS%
)

pushd "%RUN_DIR%"
"%RUN_EXE%" %APP_ARGS%
set "RUN_EXIT_CODE=%ERRORLEVEL%"
popd

if not "%RUN_EXIT_CODE%"=="0" (
    echo [Error] %RUN_TARGET% exited with code %RUN_EXIT_CODE%.
)
exit /b %RUN_EXIT_CODE%

:set_backend
set "ASH_ENGINE_TARGET_BACKEND=%~1"
set "ASH_ENGINE_CONFIG_PATH=%ENGINE_CONFIG%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$path = [System.IO.Path]::GetFullPath($env:ASH_ENGINE_CONFIG_PATH);" ^
    "$backend = $env:ASH_ENGINE_TARGET_BACKEND;" ^
    "$lines = Get-Content -LiteralPath $path;" ^
    "$output = New-Object System.Collections.Generic.List[string];" ^
    "$inRhi = $false;" ^
    "$hasRhi = $false;" ^
    "$updated = $false;" ^
    "foreach ($line in $lines) {" ^
    "    if ($line -match '^\s*\[(.+)\]\s*$') {" ^
    "        $section = $matches[1].Trim().ToLowerInvariant();" ^
    "        $inRhi = $section -eq 'rhi';" ^
    "        if ($inRhi) { $hasRhi = $true }" ^
    "        $output.Add($line);" ^
    "        continue;" ^
    "    }" ^
    "    if ($inRhi -and $line -match '^\s*Backend\s*=') {" ^
    "        $output.Add('Backend=' + $backend);" ^
    "        $updated = $true;" ^
    "        continue;" ^
    "    }" ^
    "    $output.Add($line);" ^
    "}" ^
    "if (-not $updated) {" ^
    "    if ($output.Count -gt 0 -and $output[$output.Count - 1] -ne '') { $output.Add('') }" ^
    "    if (-not $hasRhi) { $output.Add('[RHI]') }" ^
    "    $output.Add('Backend=' + $backend);" ^
    "}" ^
    "Set-Content -LiteralPath $path -Value $output -Encoding UTF8"

if errorlevel 1 (
    echo [Error] Failed to update Engine.ini backend to %ASH_ENGINE_TARGET_BACKEND%.
    exit /b 1
)
exit /b 0

:resolve_executable_name
set "%~2="
if /I "%~1"=="Editor" (
    set "%~2=Editor.exe"
    exit /b 0
)
if /I "%~1"=="Sandbox" (
    set "%~2=Sandbox.exe"
    exit /b 0
)
echo [Error] Unknown target "%~1".
exit /b 1

:try_parse_target
set "%~2="
if "%~1"=="" exit /b 1
if /I "%~1"=="editor" (
    set "%~2=Editor"
    exit /b 0
)
if /I "%~1"=="sandbox" (
    set "%~2=Sandbox"
    exit /b 0
)
if /I "%~1"=="all" (
    set "%~2=ALL"
    exit /b 0
)
if /I "%~1"=="matrix" (
    set "%~2=ALL"
    exit /b 0
)
exit /b 1

:try_parse_backend
set "%~2="
if "%~1"=="" exit /b 1
if /I "%~1"=="current" (
    set "%~2=CURRENT"
    exit /b 0
)
if /I "%~1"=="default" (
    set "%~2=CURRENT"
    exit /b 0
)
if /I "%~1"=="dx" (
    set "%~2=DX12"
    exit /b 0
)
if /I "%~1"=="dx12" (
    set "%~2=DX12"
    exit /b 0
)
if /I "%~1"=="d3d12" (
    set "%~2=DX12"
    exit /b 0
)
if /I "%~1"=="vk" (
    set "%~2=Vulkan"
    exit /b 0
)
if /I "%~1"=="vulkan" (
    set "%~2=Vulkan"
    exit /b 0
)
exit /b 1

:cleanup
if exist "%ENGINE_CONFIG_BACKUP%" (
    copy /y "%ENGINE_CONFIG_BACKUP%" "%ENGINE_CONFIG%" >nul
    del /f /q "%ENGINE_CONFIG_BACKUP%" >nul 2>nul
)
exit /b %EXIT_CODE%

:help
echo Usage:
echo   run.bat [Target] [Backend] [Configuration] [AppArgs...]
echo   run.bat all [Configuration] [AppArgs...]
echo.
echo Targets:
echo   editor    Launch Editor.exe
echo   sandbox   Launch Sandbox.exe
echo   all       Run Editor^+Sandbox on DX12 and Vulkan sequentially
echo.
echo Backends:
echo   current   Keep the current Engine.ini backend setting
echo   dx12      Write Backend=DX12 before launch
echo   vulkan    Write Backend=Vulkan before launch
echo.
echo Defaults:
echo   Target        = editor
echo   Backend       = current
echo   Configuration = Debug
echo.
echo Examples:
echo   run.bat
echo   run.bat editor dx12 Debug --smoke-test-seconds=120
echo   run.bat sandbox vulkan Debug --run-for-frames=10
echo   run.bat all Debug --smoke-test-seconds=120
exit /b 0
