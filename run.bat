@echo off
chcp 65001 > nul
echo ========== Starting VideoRecorder.exe ==========

REM Set all DLL search paths
set PATH=D:\Qt\Qt5.12.11\5.12.11\mingw73_32\bin;D:\Qt\Qt5.12.11\Tools\mingw730_32\bin;D:\Qt\opencv-release\opencv-release\bin;D:\Users\anotherbuddy\VideoRecorder\ffmpeg-4.2.2\bin;D:\Users\anotherbuddy\VideoRecorder\SDL2-2.0.10\lib\x86;%PATH%

cd /d "D:\Users\anotherbuddy\VideoRecorder\build\release"

echo [DEBUG] Current directory: %CD%
echo [DEBUG] Checking VideoRecorder.exe existence...
if exist VideoRecorder.exe (
    echo [OK] VideoRecorder.exe found
) else (
    echo [ERROR] VideoRecorder.exe NOT found!
    pause
    exit /b 1
)

echo [DEBUG] Checking dependent DLLs...
where Qt5Core.dll 2>nul && echo [OK] Qt5Core.dll found in PATH || echo [WARNING] Qt5Core.dll NOT found in PATH
where Qt5Widgets.dll 2>nul && echo [OK] Qt5Widgets.dll found || echo [WARNING] Qt5Widgets.dll NOT found
where opencv_core420.dll 2>nul && echo [OK] opencv_core420.dll found || echo [WARNING] opencv_core420.dll NOT found
where avcodec-58.dll 2>nul && echo [OK] avcodec-58.dll found || echo [WARNING] avcodec-58.dll NOT found
where SDL2.dll 2>nul && echo [OK] SDL2.dll found || echo [WARNING] SDL2.dll NOT found
where qwindows.dll 2>nul && echo [OK] qwindows.dll found || echo [WARNING] qwindows.dll NOT found (need platforms\qwindows.dll)
echo.

echo [DEBUG] Starting VideoRecorder.exe...
VideoRecorder.exe

echo.
echo ========== Program exited with code: %ERRORLEVEL% ==========
pause
