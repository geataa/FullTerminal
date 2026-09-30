@echo off
setlocal
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1

cl /nologo /std:c++20 /EHsc /utf-8 /I src /Fo:build\obj\ tests\test_theme_consistency.cpp src\vt\Screen.cpp src\vt\VtParser.cpp src\core\Utf8.cpp /Fe:bin\test_theme_consistency.exe
if errorlevel 1 (
    echo [HATA] Derleme basarisiz!
    exit /b 1
)

bin\test_theme_consistency.exe
