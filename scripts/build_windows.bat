@echo off
setlocal enabledelayedexpansion
echo ========================================
echo  Flux Downloader (C++) - Build for Windows
echo ========================================
echo.
REM Requirements:
REM   - Visual Studio 2022 (Desktop development with C++) or Build Tools
REM   - CMake 3.19+          (bundled with Visual Studio)
REM   - Qt 6 for MSVC 64-bit (https://www.qt.io/download-qt-installer)
REM   - vcpkg                (git clone https://github.com/microsoft/vcpkg ^&^& vcpkg\bootstrap-vcpkg.bat)
REM
REM Set these if they are not found automatically:
REM   set QT_DIR=C:\Qt\6.7.3\msvc2019_64
REM   set VCPKG_ROOT=C:\vcpkg

cd /d "%~dp0.."

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found. Run this from a "x64 Native Tools Command Prompt for VS 2022".
    pause
    exit /b 1
)

if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" set VCPKG_ROOT=C:\vcpkg
)
if "%VCPKG_ROOT%"=="" (
    echo [ERROR] VCPKG_ROOT is not set. Install vcpkg and run: set VCPKG_ROOT=C:\path\to\vcpkg
    pause
    exit /b 1
)

if "%QT_DIR%"=="" (
    for /d %%Q in (C:\Qt\6.*) do (
        for /d %%M in ("%%Q\msvc*_64") do set QT_DIR=%%M
    )
)
if "%QT_DIR%"=="" (
    echo [ERROR] Qt 6 for MSVC not found. Run: set QT_DIR=C:\Qt\6.x.x\msvc2019_64
    pause
    exit /b 1
)
echo Using Qt:    %QT_DIR%
echo Using vcpkg: %VCPKG_ROOT%
echo.

echo Step 1: Configuring (vcpkg builds libcurl + libtorrent the first time - this can take a while)...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 goto :fail

echo.
echo Step 2: Building...
cmake --build build --config Release --parallel
if errorlevel 1 goto :fail

echo.
echo Step 3: Creating distribution folder...
if exist dist\FluxDownloader rmdir /s /q dist\FluxDownloader
mkdir dist\FluxDownloader
copy /y build\Release\*.exe dist\FluxDownloader\ >nul
copy /y build\Release\*.dll dist\FluxDownloader\ >nul 2>&1
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw dist\FluxDownloader\FluxDownloader.exe
if errorlevel 1 goto :fail
xcopy /E /I /Y /Q chrome_extension dist\FluxDownloader\chrome_extension >nul
copy /y scripts\setup_extension.bat dist\FluxDownloader\ >nul
copy /y scripts\diagnose_extension.bat dist\FluxDownloader\ >nul

echo.
echo Step 4: Downloading yt-dlp and ffmpeg (for YouTube downloads)...
if not exist dist\FluxDownloader\yt-dlp.exe (
    powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' -OutFile 'dist\FluxDownloader\yt-dlp.exe'"
)
if not exist ffmpeg_download\ffmpeg.exe (
    if not exist ffmpeg_download mkdir ffmpeg_download
    powershell -NoProfile -Command "Invoke-WebRequest -Uri 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip' -OutFile 'ffmpeg_download\ffmpeg.zip'"
    powershell -NoProfile -Command "Expand-Archive -Path 'ffmpeg_download\ffmpeg.zip' -DestinationPath 'ffmpeg_download' -Force"
    for /r ffmpeg_download %%i in (ffmpeg.exe ffprobe.exe) do copy /y "%%i" ffmpeg_download\ >nul
)
copy /y ffmpeg_download\ffmpeg.exe dist\FluxDownloader\ >nul 2>&1
copy /y ffmpeg_download\ffprobe.exe dist\FluxDownloader\ >nul 2>&1

echo.
echo ========================================
echo  BUILD COMPLETE!
echo ========================================
echo.
echo Output folder: dist\FluxDownloader
echo Run:           dist\FluxDownloader\FluxDownloader.exe
echo.
pause
exit /b 0

:fail
echo.
echo [ERROR] Build failed - see the messages above.
pause
exit /b 1
