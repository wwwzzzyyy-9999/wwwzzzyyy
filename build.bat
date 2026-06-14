@echo off
set QTDIR=D:\Qt\Qt5.12.11\5.12.11\mingw73_32
set PATH=%QTDIR%\bin;D:\Qt\Qt5.12.11\Tools\mingw730_32\bin;%PATH%

echo === Cleaning build directory ===
cd /d D:\Users\anotherbuddy\VideoRecorder
if exist build rmdir /s /q build
mkdir build
cd build

echo === Running qmake ===
"%QTDIR%\bin\qmake.exe" "D:\Users\anotherbuddy\VideoRecorder\VideoRecorder.pro"

if errorlevel 1 (
    echo.
    echo *** QMAKE FAILED ***
    pause
    exit /b 1
)

echo.
echo === Running mingw32-make ===
mingw32-make.exe -j4 2>&1

if errorlevel 1 (
    echo.
    echo *** BUILD FAILED ***
    pause
    exit /b 1
)

echo.
echo === BUILD SUCCESS ===
if exist debug\VideoRecorder.exe (
    echo Debug build: debug\VideoRecorder.exe
) else if exist release\VideoRecorder.exe (
    echo Release build: release\VideoRecorder.exe
)
pause
