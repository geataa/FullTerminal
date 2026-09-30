@echo off
setlocal
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /EHsc /I src ^
    /Fo:build\obj\ ^
    tests\test_i18n.cpp ^
    src\core\I18n.cpp ^
    /Fe:bin\test_i18n.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
bin\test_i18n.exe
