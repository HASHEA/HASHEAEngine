@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="-h" goto :help
if /I "%~1"=="--help" goto :help

set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Debug"

call "%~dp0build_tests.bat" %CONFIG%
if errorlevel 1 exit /b 1

set "TESTS_EXE=%~dp0product\bin64\%CONFIG%-windows-x86_64\Tests.exe"
if not exist "%TESTS_EXE%" (
    echo [Tests] Tests.exe not found: %TESTS_EXE%
    exit /b 1
)

echo [Tests] Running %TESTS_EXE%
"%TESTS_EXE%" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%

:help
echo Usage: RunTests.bat [Configuration] [doctest args...]
echo.
echo Builds the Tests project and runs the doctest runner.
echo Exit code 0 means all tests passed.
echo.
echo Examples:
echo   RunTests.bat
echo   RunTests.bat Debug --test-case="*StringView*"
echo   RunTests.bat Debug --list-test-cases
exit /b 0
