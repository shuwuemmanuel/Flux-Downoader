@echo off
echo ========================================
echo  FLUX DOWNLOADER EXTENSION DIAGNOSTIC
echo ========================================
echo.

:: Check if extension folder exists
if not exist "chrome_extension" (
    echo [ERROR] chrome_extension folder not found!
    echo Current directory: %CD%
    echo.
    pause
    exit /b 1
)

echo [1/5] Checking extension files...
cd chrome_extension

set ERROR=0

:: Check each required file
if exist "manifest.json" (echo   [OK] manifest.json) else (echo   [FAIL] manifest.json MISSING & set ERROR=1)
if exist "background.js" (echo   [OK] background.js) else (echo   [FAIL] background.js MISSING & set ERROR=1)
if exist "content_script.js" (echo   [OK] content_script.js) else (echo   [FAIL] content_script.js MISSING & set ERROR=1)
if exist "popup.html" (echo   [OK] popup.html) else (echo   [FAIL] popup.html MISSING & set ERROR=1)
if exist "popup.js" (echo   [OK] popup.js) else (echo   [FAIL] popup.js MISSING & set ERROR=1)
if exist "icons\icon16.png" (echo   [OK] icons\icon16.png) else (echo   [FAIL] icons\icon16.png MISSING & set ERROR=1)
if exist "icons\icon48.png" (echo   [OK] icons\icon48.png) else (echo   [FAIL] icons\icon48.png MISSING & set ERROR=1)
if exist "icons\icon128.png" (echo   [OK] icons\icon128.png) else (echo   [FAIL] icons\icon128.png MISSING & set ERROR=1)

echo.

if %ERROR%==1 (
    echo [RESULT] Some files are MISSING - extension cannot be installed
    echo.
    cd ..
    pause
    exit /b 1
) else (
    echo [RESULT] All files present!
)

echo.
echo [2/5] Checking if Flux Downloader app is running...

:: Try to ping the local server
curl -s http://127.0.0.1:38019/ping >nul 2>&1
if %ERRORLEVEL%==0 (
    echo   [OK] Flux Downloader is running on port 38019
    set APP_RUNNING=1
) else (
    echo   [WARN] Flux Downloader app is NOT running
    echo   The extension needs the app to be running to work!
    set APP_RUNNING=0
)

echo.
echo [3/5] Testing server connection...

if %APP_RUNNING%==1 (
    curl -s http://127.0.0.1:38019/ping
    echo.
    echo   [OK] Server responded successfully
) else (
    echo   [SKIP] Cannot test - app not running
)

echo.
echo [4/5] Checking Chrome installation...

where chrome.exe >nul 2>&1
if %ERRORLEVEL%==0 (
    echo   [OK] Chrome.exe found in PATH
) else (
    echo   [INFO] Chrome not in PATH (this is normal)
    if exist "C:\Program Files\Google\Chrome\Application\chrome.exe" (
        echo   [OK] Chrome found: C:\Program Files\Google\Chrome\Application\chrome.exe
    ) else if exist "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" (
        echo   [OK] Chrome found: C:\Program Files (x86)\Google\Chrome\Application\chrome.exe
    ) else (
        echo   [WARN] Chrome installation not detected
    )
)

echo.
echo [5/5] Extension Installation Status
echo.
echo   To check if extension is installed:
echo   1. Open Chrome
echo   2. Type: chrome://extensions
echo   3. Look for "Flux Downloader Integration"
echo.

cd ..

echo ========================================
echo  DIAGNOSTIC SUMMARY
echo ========================================
echo.

if %ERROR%==1 (
    echo Status: FAILED - Missing files
    echo Action: Contact developer
) else if %APP_RUNNING%==0 (
    echo Status: READY but app not running
    echo Action: Start Flux Downloader app first!
    echo Command: FluxDownloader.exe
) else (
    echo Status: READY TO USE
    echo Action: Install extension in Chrome if not done
)

echo.
echo ========================================
echo  QUICK FIX STEPS
echo ========================================
echo.

if %APP_RUNNING%==0 (
    echo 1. Start the app: FluxDownloader.exe
    echo 2. Keep the app running in background
    echo 3. Then install the extension
    echo.
)

echo To install extension:
echo 1. Open Chrome and go to: chrome://extensions
echo 2. Enable "Developer mode" (top-right toggle)
echo 3. Click "Load unpacked"
echo 4. Select this folder:
echo    %~dp0chrome_extension
echo 5. Extension should appear in the list
echo 6. Make sure extension toggle is ON
echo.

echo To test if it's working:
echo 1. Click extension icon in Chrome toolbar
echo 2. Should show: "Connected to Flux Downloader" with green dot
echo 3. Try downloading any file
echo 4. It should appear in Flux Downloader app!
echo.

pause
