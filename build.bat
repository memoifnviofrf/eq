@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo   Vocal EQ  one-click build
echo ==========================================
echo.

rem Add the default install folders, so tools installed a minute ago work without reopening this window
set "PATH=%PATH%;%ProgramFiles%\CMake\bin;%ProgramFiles%\Git\cmd"

:check_cmake
where cmake >nul 2>nul
if not errorlevel 1 goto check_git
echo [!] CMake was not found.
call :offer_install Kitware.CMake CMake
if errorlevel 1 goto fail
set "PATH=%PATH%;%ProgramFiles%\CMake\bin"
where cmake >nul 2>nul
if errorlevel 1 goto need_restart

:check_git
where git >nul 2>nul
if not errorlevel 1 goto check_vs
echo [!] Git was not found.
call :offer_install Git.Git Git
if errorlevel 1 goto fail
set "PATH=%PATH%;%ProgramFiles%\Git\cmd"
where git >nul 2>nul
if errorlevel 1 goto need_restart

:check_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH goto build
echo [!] Visual Studio with the C++ desktop tools was not found.
where winget >nul 2>nul
if errorlevel 1 goto need_vs_manual
choice /m "Install Visual Studio 2022 Community with C++ tools now (large download, takes a while)"
if errorlevel 2 goto fail
winget install --id Microsoft.VisualStudio.2022.Community -e --accept-package-agreements --accept-source-agreements --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --norestart"
echo.
echo Visual Studio setup finished. Please run build.bat again.
pause
exit /b 0

:build
echo [1/3] Configuring... the first run downloads JUCE and takes a few minutes.
cmake -B build -A x64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 goto fail_build

echo.
echo [2/3] Building Release...
cmake --build build --config Release --parallel
if errorlevel 1 goto fail_build

set "OUT=build\VocalEQ_artefacts\Release\VST3\Vocal EQ.vst3"
set "DEST=%CommonProgramFiles%\VST3\Vocal EQ.vst3"

echo.
echo [3/3] Installing the VST3 plugin...
xcopy "%OUT%" "%DEST%\" /E /I /Y /Q >nul 2>nul
if errorlevel 1 goto copy_failed

echo.
echo [OK] Installed to: %DEST%
echo      In REAPER / Cubase: rescan plugins, then load "Vocal EQ".
goto done

:copy_failed
echo.
echo [NOTE] Build succeeded, but copying needs Administrator rights.
echo   Right-click build.bat and choose "Run as administrator",
echo   or copy the "Vocal EQ.vst3" folder shown next to this folder:
echo   %CD%\%OUT%
start "" explorer "%CD%\build\VocalEQ_artefacts\Release\VST3"
echo   Destination: %CommonProgramFiles%\VST3
goto done

:done
echo.
echo Standalone test app: build\VocalEQ_artefacts\Release\Standalone\Vocal EQ.exe
echo.
pause
exit /b 0

:need_restart
echo.
echo The tool was installed, but this window cannot see it yet.
echo Close this window, open build.bat again, and it will continue.
pause
exit /b 1

:need_vs_manual
echo   Install Visual Studio 2022 (or newer) Community from https://visualstudio.microsoft.com/
echo   and tick "Desktop development with C++", then run build.bat again.
pause
exit /b 1

:fail_build
echo.
echo [ERROR] Build failed. Copy the error message above and send it to Claude.
pause
exit /b 1

:fail
echo.
echo Cancelled. Install the missing tool and run build.bat again.
pause
exit /b 1

:offer_install
where winget >nul 2>nul
if errorlevel 1 goto offer_manual
choice /m "Install %2 now with winget"
if errorlevel 2 exit /b 1
winget install --id %1 -e --accept-package-agreements --accept-source-agreements
exit /b 0

:offer_manual
echo   winget is not available. Please install %2 manually.
exit /b 1
