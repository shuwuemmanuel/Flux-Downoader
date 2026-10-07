@echo off
title Flux Downloader - Extension Setup
color 0B

echo.
echo  ========================================
echo   FLUX DOWNLOADER - CHROME EXTENSION
echo          Quick Setup Wizard
echo  ========================================
echo.
echo  This will help you install the extension
echo  in just a few simple steps!
echo.
pause

cls
echo.
echo  ========================================
echo   STEP 1: Verify Files
echo  ========================================
echo.

cd /d "%~dp0"

if not exist "chrome_extension\manifest.json" (
    echo  [X] ERROR: Extension files not found!
    echo.
    echo  Make sure you're running this from:
    echo  %~dp0
    echo.
    pause
    exit /b 1
)

echo  [OK] Extension files found!
echo.
echo  Location: %~dp0chrome_extension
echo.
pause

cls
echo.
echo  ========================================
echo   STEP 2: Start Flux Downloader App
echo  ========================================
echo.

curl -s http://127.0.0.1:38019/ping >nul 2>&1
if %ERRORLEVEL%==0 (
    echo  [OK] Flux Downloader is already running!
    echo.
) else (
    echo  [!] Flux Downloader app is NOT running
    echo.
    echo  IMPORTANT: The app MUST be running for the
    echo  extension to work properly!
    echo.
    echo  Options:
    echo  1. Open a new command window
    echo  2. Navigate to: %~dp0
    echo  3. Run: FluxDownloader.exe
    echo  4. Leave it running in background
    echo.
    echo  Press any key once the app is running...
    pause >nul
    
    :: Check again
    curl -s http://127.0.0.1:38019/ping >nul 2>&1
    if %ERRORLEVEL%==0 (
        echo  [OK] App detected!
    ) else (
        echo  [!] Still not detected. Continue anyway? (y/n)
        set /p choice=
        if /i not "%choice%"=="y" exit /b
    )
)

echo.
pause

cls
echo.
echo  ========================================
echo   STEP 3: Open Chrome Extensions Page
echo  ========================================
echo.
echo  I will now open Chrome's extensions page
echo  for you automatically!
echo.
echo  When it opens:
echo  1. Look for the toggle "Developer mode"
echo     in the TOP-RIGHT corner
echo  2. Turn it ON (should turn blue)
echo.
echo  Press any key to open chrome://extensions
pause >nul

:: Try to open Chrome extensions page
start chrome://extensions/

echo.
echo  Did the page open? (y/n)
set /p opened=

if /i not "%opened%"=="y" (
    echo.
    echo  Please open Chrome manually and type:
    echo  chrome://extensions
    echo.
    pause
)

cls
echo.
echo  ========================================
echo   STEP 4: Enable Developer Mode
echo  ========================================
echo.
echo  In the Chrome extensions page:
echo.
echo  1. Look at the TOP-RIGHT corner
echo  2. Find the toggle labeled "Developer mode"
echo  3. Click it to turn it ON
echo  4. It should turn BLUE when enabled
echo.
echo  You should now see 3 new buttons:
echo  - Load unpacked
echo  - Pack extension  
echo  - Update
echo.
echo  Have you enabled Developer mode? (y/n)
set /p devmode=

if /i not "%devmode%"=="y" (
    echo.
    echo  Please enable it before continuing!
    echo.
    pause
    cls
    goto :step4
)

cls
echo.
echo  ========================================
echo   STEP 5: Load Extension
echo  ========================================
echo.
echo  Now:
echo  1. Click the "Load unpacked" button
echo  2. A file browser will open
echo  3. Navigate to and SELECT this folder:
echo.
echo     %~dp0chrome_extension
echo.
echo  4. Click "Select Folder"
echo.
echo  IMPORTANT: Select the "chrome_extension"
echo  folder, NOT the main flux_downloader folder!
echo.
echo.
echo  I'll open the folder for you now...
echo.
pause

:: Open the extension folder in Explorer
explorer "%~dp0chrome_extension"

echo.
echo  Folder opened!
echo.
echo  Now in Chrome:
echo  1. Click "Load unpacked"
echo  2. Select the folder that just opened
echo  3. Click "Select Folder"
echo.
echo  Did the extension load successfully? (y/n)
set /p loaded=

if /i not "%loaded%"=="y" (
    echo.
    echo  Troubleshooting:
    echo  - Make sure you selected chrome_extension folder
    echo  - Check for any error messages in Chrome
    echo  - Try again from "Load unpacked" button
    echo.
    pause
    exit /b
)

cls
echo.
echo  ========================================
echo   STEP 6: Verify Installation
echo  ========================================
echo.
echo  You should now see:
echo.
echo  [OK] "Flux Downloader Integration" in the list
echo  [OK] Version 1.0.0
echo  [OK] A blue toggle switch (should be ON)
echo.
echo  If you see any errors:
echo  - Click "Details" to see what's wrong
echo  - Common issues: missing files, wrong folder
echo.
echo  Is the extension showing in the list? (y/n)
set /p showing=

if /i not "%showing%"=="y" (
    echo.
    echo  Something went wrong. Try:
    echo  1. Go back to Step 5
    echo  2. Click "Load unpacked" again
    echo  3. Make sure to select chrome_extension folder
    echo.
    pause
    exit /b
)

cls
echo.
echo  ========================================
echo   STEP 7: Test Connection
echo  ========================================
echo.
echo  Final test:
echo.
echo  1. Look for the Flux Downloader icon in
echo     Chrome's toolbar (top-right)
echo.
echo  2. Click on it (small popup will open)
echo.
echo  3. Check the status:
echo     - Green dot = Connected! SUCCESS!
echo     - Red dot = App not running
echo.
echo  Do you see a GREEN dot? (y/n)
set /p greentest=

if /i "%greentest%"=="y" (
    cls
    echo.
    echo  ========================================
    echo   SUCCESS! Extension Installed!
    echo  ========================================
    echo.
    echo  The Chrome extension is now working!
    echo.
    echo  What you can do:
    echo.
    echo  1. DOWNLOAD INTERCEPTION:
    echo     - Click any download link in Chrome
    echo     - It will appear in Flux Downloader app
    echo     - Chrome's default download is canceled
    echo.
    echo  2. VIDEO DOWNLOAD ICONS:
    echo     - Hover over any video on any website
    echo     - Blue download icon appears
    echo     - Click to download with Flux
    echo.
    echo  3. RIGHT-CLICK MENU:
    echo     - Right-click any link
    echo     - "Download with Flux Downloader"
    echo     - Instantly sent to app
    echo.
    echo  4. YOUTUBE BUTTON:
    echo     - On YouTube videos
    echo     - "Send to Flux Downloader" button
    echo     - Downloads videos/playlists
    echo.
    echo  TIP: Keep Flux Downloader app running
    echo  in the background for the extension to work!
    echo.
    echo  Enjoy your new download manager!
    echo.
) else (
    echo.
    echo  Red dot means the app isn't running.
    echo.
    echo  Fix:
    echo  1. Open command prompt
    echo  2. Navigate to: %~dp0
    echo  3. Run: FluxDownloader.exe
    echo  4. Leave it running
    echo  5. Click extension icon again
    echo  6. Should show green dot now!
    echo.
)

echo.
echo  ========================================
echo.
pause
