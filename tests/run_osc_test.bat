@echo off
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I src /Fo:build\obj\ tests\test_osc_semantics.cpp src\vt\Screen.cpp src\vt\VtParser.cpp src\core\Utf8.cpp user32.lib /Fe:bin\test_osc_semantics.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
bin\test_osc_semantics.exe
