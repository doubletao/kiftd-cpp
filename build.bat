@echo off
setlocal

echo ==============================
echo  kiftd-cpp build ^& deploy
echo ==============================

:: Step 1: Build frontend
echo.
echo [1/3] Building frontend...
cd /d "%~dp0web"
if not exist node_modules\ (
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

:: Step 3: Deploy to bin
echo.
echo [3/3] Deploying to bin...
set BIN_DIR=bin
if not exist "%BIN_DIR%\data\files" mkdir "%BIN_DIR%\data\files"
if not exist "%BIN_DIR%\data\transcode" mkdir "%BIN_DIR%\data\transcode"

copy /y build\Release\kiftd.exe "%BIN_DIR%\" >nul
xcopy /e /i /q /y web\dist "%BIN_DIR%\web\dist" >nul

echo   Deploy OK.

echo.
echo ==============================
echo  Done! Output: bin\
echo ==============================

endlocal
