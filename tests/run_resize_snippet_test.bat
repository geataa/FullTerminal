@echo off
setlocal
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1
if not exist "build\obj" mkdir "build\obj"
if not exist "bin" mkdir "bin"

cl /nologo /std:c++20 /O2 /EHsc /utf-8 /I src ^
   /Fo"build\obj\\" /Fe"bin\test_resize_snippet.exe" ^
   tests\test_screen_resize_snippet.cpp ^
   src\vt\Screen.cpp src\core\Utf8.cpp src\model\SnippetModel.cpp ^
   user32.lib gdi32.lib shlwapi.lib

if %errorlevel% neq 0 (
    echo Derleme hatasi!
    exit /b 1
)

bin\test_resize_snippet.exe
