@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   FullTerminal - MSVC Derleyici Scripti
echo ========================================================

if not exist bin mkdir bin
if not exist build\obj mkdir build\obj

call "%~dp0tools\vcvars.bat"
if errorlevel 1 exit /b 1

echo.
echo Kaynaklar derleniyor...
rc /nologo /i resources /fo build\obj\FullTerminal.res resources\FullTerminal.rc
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Derleme basliyor...
cl /nologo /W3 /O2 /Oi /Ot /MT /EHsc /std:c++20 /utf-8 ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000006 ^
    /I src /I resources ^
    /Fo:build\obj\ ^
    src\main.cpp ^
    src\core\Utf8.cpp ^
    src\core\I18n.cpp ^
    src\core\ShellProfiles.cpp ^
    src\core\Settings.cpp ^
    src\model\Inventory.cpp ^
    src\model\K8sManager.cpp ^
    src\model\SftpModel.cpp ^
    src\model\SnippetModel.cpp ^
    src\model\TunnelModel.cpp ^
    src\services\KeyGenService.cpp ^
    src\services\KnownHostsService.cpp ^
    src\services\AgentDetector.cpp ^
    src\services\SwarmWorkspace.cpp ^
    src\services\SessionRecorder.cpp ^
    src\services\SessionReplayer.cpp ^
    src\transport\pty\ConPty.cpp ^
    src\transport\daemon\PtySession.cpp ^
    src\transport\daemon\PtySessionClient.cpp ^
    src\transport\daemon\SessionDaemon.cpp ^
    src\transport\agentless\AgentlessExecutor.cpp ^
    src\mcp\McpBridge.cpp ^
    src\mcp\McpServer.cpp ^
    src\vt\Screen.cpp ^
    src\vt\VtParser.cpp ^
    src\render\Renderer.cpp ^
    src\ui\Ui.cpp ^
    src\ui\TerminalTab.cpp ^
    src\ui\PaneLayout.cpp ^
    src\ui\MainWindow.cpp ^
    src\ui\MainWindow_Screens.cpp ^
    src\ui\MainWindow_Sidebar.cpp ^
    src\ui\MainWindow_TabBar.cpp ^
    src\ui\MainWindow_Quake.cpp ^
    src\ui\MainWindow_Mcp.cpp ^
    src\ui\MainWindow_AgentRibbon.cpp ^
    src\ui\MainWindow_Swarm.cpp ^
    src\ui\MainWindow_Fleet.cpp ^
    build\obj\FullTerminal.res ^
    /Fe:bin\FullTerminal.exe ^
    /link /SUBSYSTEM:WINDOWS ^
    d3d11.lib dxgi.lib d2d1.lib dwrite.lib dcomp.lib windowscodecs.lib ^
    shlwapi.lib shell32.lib user32.lib gdi32.lib ole32.lib advapi32.lib dwmapi.lib winmm.lib crypt32.lib comdlg32.lib

if %ERRORLEVEL% EQU 0 (
    copy /Y bin\FullTerminal.exe FullTerminal.exe >nul 2>nul
    if errorlevel 1 (
        echo.
        echo UYARI: FullTerminal.exe calisiyor ^(tepside veya Guake'de gizli olabilir^), uzerine yazilamadi.
        echo        Yeni surum: bin\FullTerminal.exe  -  uygulamayi kapatip build.bat'i yeniden calistir.
        exit /b 2
    )
    echo.
    echo ========================================================
    echo   DERLEME BASARILI  -^>  FullTerminal.exe
    echo ========================================================
) else (
    echo.
    echo DERLEME HATASI! Kod: %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
