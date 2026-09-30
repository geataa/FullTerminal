@echo off
setlocal
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /I src ^
    /Fo:build\obj\ ^
    tests\test_i18n.cpp ^
    src\core\I18n.cpp ^
    /Fe:bin\test_i18n.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
bin\test_i18n.exe
