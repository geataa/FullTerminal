cmake_minimum_required(VERSION 3.20)
project(FullTerminal VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tek tasinabilir exe: statik CRT, harici DLL yok
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_definitions(
    -DUNICODE
    -D_UNICODE
    -DWIN32_LEAN_AND_MEAN
    -DNOMINMAX
    -DWINVER=0x0A00
    -D_WIN32_WINNT=0x0A00
    -DNTDDI_VERSION=0x0A000006   # NTDDI_WIN10_RS5: ConPTY API'si icin sart
)

include_directories(src resources)

set(SOURCES
    src/main.cpp

    src/core/Utf8.h
    src/core/Utf8.cpp
    src/core/ShellProfiles.h
    src/core/ShellProfiles.cpp
    src/core/Settings.h
    src/core/Settings.cpp

    src/model/Inventory.h
    src/model/Inventory.cpp
    src/model/K8sManager.h
    src/model/K8sManager.cpp
    src/model/SftpModel.h
    src/model/SftpModel.cpp
    src/model/SnippetModel.h
    src/model/SnippetModel.cpp
    src/model/TunnelModel.h
    src/model/TunnelModel.cpp

    src/services/KeyGenService.h
    src/services/KeyGenService.cpp
    src/services/KnownHostsService.h
    src/services/KnownHostsService.cpp
    src/services/AgentDetector.h
    src/services/AgentDetector.cpp
    src/services/SwarmWorkspace.h
    src/services/SwarmWorkspace.cpp
    src/services/SessionRecorder.h
    src/services/SessionRecorder.cpp
    src/services/SessionReplayer.h
    src/services/SessionReplayer.cpp

    src/transport/pty/ConPty.h
    src/transport/pty/ConPty.cpp
    src/transport/daemon/PtySession.h
    src/transport/daemon/PtySession.cpp
    src/transport/daemon/PtySessionClient.h
    src/transport/daemon/PtySessionClient.cpp
    src/transport/daemon/SessionDaemon.h
    src/transport/daemon/SessionDaemon.cpp
    src/transport/agentless/AgentlessExecutor.h
    src/transport/agentless/AgentlessExecutor.cpp

    src/mcp/Json.h
    src/mcp/McpBridge.h
    src/mcp/McpBridge.cpp
    src/mcp/McpServer.h
    src/mcp/McpServer.cpp

    src/vt/Cell.h
    src/vt/Screen.h
    src/vt/Screen.cpp
    src/vt/VtParser.h
    src/vt/VtParser.cpp

    src/render/Renderer.h
    src/render/Renderer.cpp

    src/ui/Theme.h
    src/ui/Ui.h
    src/ui/Ui.cpp
    src/ui/TerminalTab.h
    src/ui/TerminalTab.cpp
    src/ui/PaneLayout.h
    src/ui/PaneLayout.cpp
    src/ui/MainWindow.h
    src/ui/MainWindowInternal.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow_Screens.cpp
    src/ui/MainWindow_Sidebar.cpp
    src/ui/MainWindow_TabBar.cpp
    src/ui/MainWindow_Quake.cpp
    src/ui/MainWindow_Mcp.cpp
    src/ui/MainWindow_AgentRibbon.cpp
    src/ui/MainWindow_Swarm.cpp
    src/ui/MainWindow_Fleet.cpp

    resources/FullTerminal.rc
)

add_executable(FullTerminal WIN32 ${SOURCES})

target_compile_options(FullTerminal PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W3 /EHsc $<$<CONFIG:Release>:/O2 /Oi /Ot>>
)

target_link_libraries(FullTerminal PRIVATE
    d3d11
    dxgi
    d2d1
    dwrite
    dcomp
    windowscodecs
    shlwapi
    shell32
    ole32
    user32
    gdi32
    advapi32
    dwmapi
    winmm
    crypt32
)

add_custom_command(TARGET FullTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:FullTerminal>
        ${CMAKE_SOURCE_DIR}/FullTerminal.exe
    COMMENT "FullTerminal.exe kok dizine kopyalaniyor"
)
