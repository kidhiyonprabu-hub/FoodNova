@echo off
setlocal
cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"

echo ===================================================================
echo   FoodRush - Autonomous High-Performance Food Delivery Platform
echo   Ultra-Low-Latency C Concurrency Engine (POSIX Thread Pool)
echo ===================================================================
echo.

if not exist foodrush.exe (
    echo [INFO] foodrush.exe not found. Building project...
    call build.bat
    if errorlevel 1 (
        echo [ERROR] Build failed! Check errors above.
        pause
        exit /b 1
    )
)

echo [INFO] FoodRush server starting on http://localhost:8080
echo [INFO] Default accounts:
echo        - Admin:    username=admin  password=admin123
echo        - Customer: username=john   password=password123
echo.
echo [INFO] Opening default browser at http://localhost:8080 ...
start http://localhost:8080

echo [INFO] Server running. Press Ctrl+C to stop.
echo -------------------------------------------------------------------
foodrush.exe
