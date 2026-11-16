@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\Build-NativeClients.ps1
if errorlevel 1 exit /b 1
call tools\vcvars.bat
if errorlevel 1 exit /b 1
set "FT_NINJA=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
cmake -S . -B build\release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DFT_COPY_TO_ROOT=OFF "-DCMAKE_MAKE_PROGRAM=%FT_NINJA%"
if errorlevel 1 exit /b 1
cmake --build build\release --parallel 4
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\Publish-Binary.ps1 -Source "%~dp0build\release\FullTerminal.exe" -Destination "%~dp0bin\FullTerminal.exe"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\Publish-Binary.ps1 -Source "%~dp0build\release\FullTerminal.exe" -Destination "%~dp0FullTerminal.exe"
if errorlevel 1 exit /b 1
echo Build ready: FullTerminal.exe
