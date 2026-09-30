@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
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
