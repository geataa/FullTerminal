@echo off
setlocal
cd /d "%~dp0.."
call tools\vcvars.bat
if errorlevel 1 exit /b 1
set "FT_NINJA=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
cmake -S . -B build\tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DFT_COPY_TO_ROOT=OFF "-DCMAKE_MAKE_PROGRAM=%FT_NINJA%"
if errorlevel 1 exit /b 1
cmake --build build\tests --parallel 4
if errorlevel 1 exit /b 1
if "%~1"=="" (
    ctest --test-dir build\tests --output-on-failure
) else (
    ctest --test-dir build\tests --output-on-failure -R "^%~1$"
)
exit /b %errorlevel%
