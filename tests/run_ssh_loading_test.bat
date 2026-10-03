@echo off
if not exist build\obj mkdir build\obj
if not exist bin mkdir bin
call "%~dp0..\tools\vcvars.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I src ^
   /Fo:build\obj\ ^
   tests\test_ssh_loading_fleet.cpp ^
   src\ui\TerminalTab.cpp ^
   src\vt\Screen.cpp ^
   src\vt\VtParser.cpp ^
   src\core\Utf8.cpp ^
   src\core\I18n.cpp ^
   src\core\Settings.cpp ^
   src\core\ShellProfiles.cpp ^
   src\model\Inventory.cpp ^
   src\model\SftpModel.cpp ^
   src\model\K8sManager.cpp ^
   src\transport\agentless\AgentlessExecutor.cpp ^
   src\transport\pty\ConPty.cpp ^
   src\services\AgentDetector.cpp ^
   src\services\SessionRecorder.cpp ^
   user32.lib shell32.lib shlwapi.lib advapi32.lib crypt32.lib ole32.lib ^
   /Fe:bin\test_ssh_loading_fleet.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
bin\test_ssh_loading_fleet.exe
