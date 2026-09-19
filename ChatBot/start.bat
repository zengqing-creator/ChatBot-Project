@echo off
setlocal

set "PROJECT_DIR=D:\ChatBot Project\ChatBot"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "EXE_DIR=%BUILD_DIR%\bin\Release"
set "EXE_NAME=ChatBot.exe"
set "NGROK=C:\Users\z'q\AppData\Local\Microsoft\WindowsApps\ngrok.exe"

cd /d "%PROJECT_DIR%"

if errorlevel 1 (
    echo "Failed to change directory to %PROJECT_DIR%"
    pause
    exit /b 1
)
if not exist "%BUILD_DIR%" (
    echo "Build directory %BUILD_DIR% not found. Creating it..."
    mkdir "%BUILD_DIR%"
    if errorlevel 1 (
        echo "Failed to create build directory %BUILD_DIR%"
        pause
        exit /b 1
    )
)
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo "CmakeCache.txt not found in %BUILD_DIR%. Running CMake to generate build files..."
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo "CMake configuration failed."
        pause
        exit /b 1
    )
)

echo "1. Building %EXE_NAME%..."
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo "Build failed."
    pause
    exit /b 1
)

echo "2. Starting ChatBot..."
cd /d "%EXE_DIR%"
if not exist "%EXE_NAME%" (
    echo "%EXE_NAME% not found in %EXE_DIR%"
    pause
    exit /b 1
)
start "" "%EXE_NAME%"

echo "3. Starting ngrok..."
timeout /t 2 >nul
start "" "%NGROK%" http 8080
echo "ngrok tunnel started."