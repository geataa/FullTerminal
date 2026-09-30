@echo off
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /I src /Fo:build\obj\ tests\test_opacity_accent.cpp src\core\Settings.cpp src\core\Utf8.cpp src\core\ShellProfiles.cpp /Fe:bin\test_opacity_accent.exe shlwapi.lib advapi32.lib
if %ERRORLEVEL% EQU 0 (
    bin\test_opacity_accent.exe
)
