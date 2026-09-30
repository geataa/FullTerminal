@echo off
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /EHsc /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I src /Fo:build\obj\ tests\test_session_recording.cpp src\services\SessionRecorder.cpp src\services\SessionReplayer.cpp src\vt\Screen.cpp src\vt\VtParser.cpp src\core\Utf8.cpp user32.lib /Fe:bin\test_session_recording.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
bin\test_session_recording.exe
