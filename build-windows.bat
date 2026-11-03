@echo off
setlocal EnableExtensions
rem ===========================================================================
rem  FretForge Studio - one-click Windows build
rem
rem  Double-click this file, or run from a command prompt:
rem     build-windows.bat                 Release build + tests (+ installer if NSIS is installed)
rem     build-windows.bat clean           delete the previous build first
rem     build-windows.bat debug           Debug build
rem     build-windows.bat asio C:\asiosdk enable ASIO using the Steinberg ASIO SDK at that path
rem  Options can be combined, e.g.:  build-windows.bat clean asio C:\asiosdk
rem
rem  Needs: Visual Studio 2022 or newer (Community is fine) with the
rem         "Desktop development with C++" workload, and Git for Windows.
rem ===========================================================================

title FretForge Studio - Windows build
cd /d "%~dp0"

set "CONFIG=Release"
set "DO_CLEAN=0"
set "ASIO_DIR="
set "BUILD_DIR=%~dp0build"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="clean" set "DO_CLEAN=1"
if /i "%~1"=="debug" set "CONFIG=Debug"
if /i "%~1"=="asio" (
    set "ASIO_DIR=%~2"
    shift
)
shift
goto parse_args
:args_done

echo.
echo  ============================================
echo   FretForge Studio  -  Windows build (%CONFIG%)
echo  ============================================
echo.

rem ---------------------------------------------------------------- Visual Studio
echo [1/6] Looking for Visual Studio C++ tools...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vs

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto no_vs
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto no_vs
echo       Found: %VSPATH%

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto no_vs

rem ---------------------------------------------------------------- tools
echo [2/6] Checking for CMake, Ninja and Git...
where cmake >nul 2>nul
if errorlevel 1 goto no_cmake
where git >nul 2>nul
if errorlevel 1 goto no_git

set "GENERATOR=Ninja"
where ninja >nul 2>nul
if errorlevel 1 set "GENERATOR=NMake Makefiles"
echo       CMake:  OK
echo       Git:    OK
echo       Build system: %GENERATOR%

rem ---------------------------------------------------------------- ASIO
set "ASIO_FLAGS="
if not defined ASIO_DIR goto asio_done
if not exist "%ASIO_DIR%\common\iasiodrv.h" goto bad_asio
set "ASIO_FLAGS=-DFF_ENABLE_ASIO=ON -DFF_ASIO_SDK_DIR=%ASIO_DIR:\=/%"
echo       ASIO:   enabled (%ASIO_DIR%)
:asio_done

rem ---------------------------------------------------------------- configure
if "%DO_CLEAN%"=="1" (
    echo       Removing previous build folder...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

rem A build folder made with a different generator can't be reused.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /c:"CMAKE_GENERATOR:INTERNAL=%GENERATOR%" "%BUILD_DIR%\CMakeCache.txt" >nul || rmdir /s /q "%BUILD_DIR%"
)

echo [3/6] Configuring (the first run downloads JUCE - this can take a few minutes)...
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%CONFIG% %ASIO_FLAGS%
if errorlevel 1 goto failed

rem ---------------------------------------------------------------- build
echo [4/6] Compiling (5-15 minutes the first time)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 goto failed

set "APP_DIR=%BUILD_DIR%\FretForgeStudio_artefacts\%CONFIG%"
set "APP_EXE=%APP_DIR%\FretForge Studio.exe"
if not exist "%APP_EXE%" goto failed

rem ---------------------------------------------------------------- tests
echo [5/6] Running unit tests...
set "TEST_EXE=%BUILD_DIR%\ff_tests_artefacts\%CONFIG%\ff_tests.exe"
if exist "%TEST_EXE%" (
    "%TEST_EXE%" > "%BUILD_DIR%\test-results.txt" 2>&1
    if errorlevel 1 (
        echo       WARNING: some tests failed - see build\test-results.txt
    ) else (
        echo       All tests passed.
    )
) else (
    echo       Test runner not found, skipping.
)

rem ---------------------------------------------------------------- installer
echo [6/6] Packaging...
set "MAKENSIS="
where makensis >nul 2>nul && set "MAKENSIS=1"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "PATH=%ProgramFiles(x86)%\NSIS;%PATH%" & set "MAKENSIS=1"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "PATH=%ProgramFiles%\NSIS;%PATH%" & set "MAKENSIS=1"

pushd "%BUILD_DIR%"
if defined MAKENSIS (
    cpack -C %CONFIG% -G "NSIS;ZIP" >nul
) else (
    cpack -C %CONFIG% -G ZIP >nul
)
set "CPACK_RESULT=%errorlevel%"
popd
if not "%CPACK_RESULT%"=="0" echo       Packaging failed (the exe itself is fine).
if not defined MAKENSIS echo       Tip: install NSIS from https://nsis.sourceforge.io to also get a setup .exe

rem ---------------------------------------------------------------- done
echo.
echo  ============================================
echo   BUILD SUCCEEDED
echo  ============================================
echo   App:        %APP_EXE%
for %%f in ("%BUILD_DIR%\FretForgeStudio-*.exe") do echo   Installer:  %%~ff
for %%f in ("%BUILD_DIR%\FretForgeStudio-*.zip") do echo   Zip:        %%~ff
echo.
echo   Windows may show a SmartScreen warning because the app is not
echo   code-signed yet: choose "More info" then "Run anyway".
echo.
choice /c YN /n /m "Launch FretForge Studio now? [Y/N] "
if errorlevel 2 goto open_folder
start "" "%APP_EXE%"
:open_folder
start "" explorer "%APP_DIR%"
goto end

rem ---------------------------------------------------------------- errors
:no_vs
echo.
echo  ERROR: Visual Studio with C++ tools was not found.
echo.
echo   1. Download Visual Studio Community (free):
echo        https://visualstudio.microsoft.com/downloads/
echo   2. In the installer tick "Desktop development with C++".
echo   3. Run this script again.
echo.
echo   Opening the download page...
start "" https://visualstudio.microsoft.com/downloads/
goto fail_end

:no_cmake
echo.
echo  ERROR: CMake was not found. Open the Visual Studio Installer, click
echo  "Modify", and make sure "C++ CMake tools for Windows" is ticked
echo  (it is part of "Desktop development with C++").
goto fail_end

:no_git
echo.
echo  ERROR: Git was not found. It is needed to download the JUCE framework.
echo.
echo   Install it from https://git-scm.com/download/win
echo   (or run:  winget install --id Git.Git -e)
echo   then CLOSE this window and run the script again.
start "" https://git-scm.com/download/win
goto fail_end

:bad_asio
echo.
echo  ERROR: "%ASIO_DIR%" does not look like the ASIO SDK
echo  (expected to find common\iasiodrv.h inside it).
goto fail_end

:failed
echo.
echo  ============================================
echo   BUILD FAILED
echo  ============================================
echo   Scroll up to the first line containing "error" and send it
echo   (or the whole window text) to get it fixed.
echo   To retry from scratch:  build-windows.bat clean
goto fail_end

:fail_end
echo.
pause
endlocal
exit /b 1

:end
endlocal
exit /b 0
