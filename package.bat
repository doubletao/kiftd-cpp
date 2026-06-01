@echo off
setlocal

echo ==============================
echo  kiftd-cpp build ^& package
echo ==============================

:: Check Node.js
where node >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Node.js not found, please install it first.
    exit /b 1
)

:: Check CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] CMake not found, please install it first.
    exit /b 1
)

:: Step 1: Build frontend
echo.
echo [1/3] Building frontend...
cd /d "%~dp0web"
if exist node_modules\ (
    echo   Dependencies already installed.
) else (
    echo   Installing dependencies...
    call npm install
    if %errorlevel% neq 0 (
        echo [ERROR] npm install failed.
        exit /b 1
    )
)
call npm run build
if %errorlevel% neq 0 (
    echo [ERROR] Frontend build failed.
    exit /b 1
)
echo   Frontend build OK.
cd /d "%~dp0"

:: Step 2: Build backend
echo.
echo [2/3] Building backend...
cmake -B build -S . >nul 2>&1
cmake --build build --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Backend build failed.
    exit /b 1
)
echo   Backend build OK.

:: Step 3: Package
echo.
echo [3/3] Packaging...

:: Create release directory
set RELEASE_DIR=kiftd-cpp-release
if exist "%RELEASE_DIR%\." (
    rmdir /s /q "%RELEASE_DIR%"
)
mkdir "%RELEASE_DIR%"

:: Copy files
copy build\Release\kiftd.exe "%RELEASE_DIR%\" >nul
copy data\config.json "%RELEASE_DIR%\" >nul
xcopy /e /i /q web\dist "%RELEASE_DIR%\web\dist" >nul

:: Create empty dirs
mkdir "%RELEASE_DIR%\data\files" 2>nul

:: Create README for the release
(
echo kiftd-cpp - lightweight file server
echo.
echo Start:  kiftd.exe
echo Config: config.json
echo Data:   data\kiftd.db (auto created)
echo Files:  data\files\  (auto created)
echo Web:    http://localhost:8081
echo.
echo Default login: admin / admin
echo.
echo To change port, edit config.json and restart.
echo To migrate: copy the entire data\ folder to the new location.
) > "%RELEASE_DIR%\README.txt"

:: Create zip
set ZIP_NAME=kiftd-cpp-v1.0.0-win64.zip
if exist "%ZIP_NAME%" del "%ZIP_NAME%"

:: Use PowerShell to create zip
powershell -Command "Compress-Archive -Path '%RELEASE_DIR%\*' -DestinationPath '%ZIP_NAME%' -Force"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to create zip.
    exit /b 1
)

:: Cleanup release dir
rmdir /s /q "%RELEASE_DIR%"

echo.
echo ==============================
echo  Done!
echo  Output: %ZIP_NAME%
echo ==============================

endlocal
