cmake_minimum_required(VERSION 3.20)
project(FullTerminal VERSION 1.1.1 LANGUAGES C CXX RC)

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
add_library(ft_sqlite STATIC vendor/sqlite/sqlite3.c)
target_include_directories(ft_sqlite PUBLIC vendor/sqlite)
target_compile_definitions(ft_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_ENABLE_COLUMN_METADATA)
include(cmake/NativeClients.cmake)

set(SOURCES
    src/main.cpp
    src/database/Database.cpp
    src/database/Sql.cpp
    src/database/SqliteBackend.cpp
    src/database/OdbcBackend.cpp
    src/database/NativeBackend.cpp
    src/database/PostgresBackend.cpp
    src/database/MysqlBackend.cpp
    src/database/SqlServerBackend.cpp

    src/core/Utf8.h
    src/core/Utf8.cpp
    src/core/I18n.cpp
    src/core/ShellProfiles.h
    src/core/ShellProfiles.cpp
    src/core/Settings.h
    src/core/Settings.cpp

    src/model/Inventory.h
    src/model/Inventory.cpp
    src/model/K8sManager.h
    src/model/K8sManager.cpp
    src/model/SftpModel.h
    src/model/SftpController.cpp
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
    src/mcp/McpHttpServer.cpp

    src/vt/Cell.h
    src/vt/Screen.h
    src/vt/Screen.cpp
    src/vt/VtParser.h
    src/vt/VtParser.cpp

    src/render/Renderer.h
    src/render/PlainTextHighlights.h
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
    src/ui/MainWindow_Replay.cpp
    src/ui/MainWindow_Database.cpp

    resources/FullTerminal.rc
)

set(CORE_SOURCES ${SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main.cpp|src/ui/MainWindow|resources/FullTerminal.rc")
add_library(ft_core STATIC ${CORE_SOURCES})
set(GUI_SOURCES ${SOURCES})
list(REMOVE_ITEM GUI_SOURCES ${CORE_SOURCES})
list(REMOVE_ITEM GUI_SOURCES src/main.cpp resources/FullTerminal.rc)
add_library(ft_gui STATIC ${GUI_SOURCES})
target_link_libraries(ft_gui PUBLIC ft_core)
add_executable(FullTerminal WIN32 src/main.cpp resources/FullTerminal.rc)
target_link_options(FullTerminal PRIVATE /MANIFEST:NO) # resources/FullTerminal.rc embeds app.manifest

foreach(target FullTerminal ft_core ft_gui)
    target_compile_options(${target} PRIVATE /utf-8 /W3 /EHsc)
endforeach()

target_link_libraries(ft_core PUBLIC
    ft_sqlite ft_native_clients odbc32
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
    comdlg32
    ws2_32
)
target_link_libraries(FullTerminal PRIVATE ft_gui)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_database_odbc tests/test_database_odbc.cpp)
    target_link_libraries(test_database_odbc PRIVATE ft_core)
    target_compile_options(test_database_odbc PRIVATE /utf-8 /EHsc /UNDEBUG)
    foreach(name i18n theme_consistency opacity_accent osc_semantics session_recording screen_resize_snippet ssh_loading_fleet agent_detector agent_ribbon pane_layout swarm_workspace conpty_daemon mcp_panes window_presentation sftp_navigation database database_ui database_native)
        add_executable(test_${name} tests/test_${name}.cpp)
        target_link_libraries(test_${name} PRIVATE ft_core)
        if(name STREQUAL "window_presentation" OR name STREQUAL "database_ui")
            target_link_libraries(test_${name} PRIVATE ft_gui)
        endif()
        target_compile_options(test_${name} PRIVATE /utf-8 /EHsc /UNDEBUG)
        set(test_dir "${CMAKE_BINARY_DIR}/test-runs/${name}")
        file(MAKE_DIRECTORY "${test_dir}/bin")
        set_target_properties(test_${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${test_dir}")
        add_test(NAME ${name} COMMAND test_${name})
        set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${test_dir}" TIMEOUT 60)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ssh_loopback COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_ssh_loopback.py"
            $<TARGET_FILE:test_conpty_daemon>)
        set_tests_properties(ssh_loopback PROPERTIES TIMEOUT 50 SKIP_RETURN_CODE 77 RESOURCE_LOCK conpty_daemon_fixture)
        set_tests_properties(conpty_daemon PROPERTIES RESOURCE_LOCK conpty_daemon_fixture)
        add_test(NAME http_transport COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_http_transport.py"
            $<TARGET_FILE:FullTerminal> "${CMAKE_BINARY_DIR}/test-runs")
        set_tests_properties(http_transport PROPERTIES TIMEOUT 45)
    endif()
endif()

option(FT_COPY_TO_ROOT "Copy the built executable to the repository root" ON)
if(FT_COPY_TO_ROOT)
add_custom_command(TARGET FullTerminal POST_BUILD
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tools/Publish-Binary.ps1"
        -Source "$<TARGET_FILE:FullTerminal>"
        -Destination "${CMAKE_SOURCE_DIR}/FullTerminal.exe"
    VERBATIM
    COMMENT "FullTerminal.exe kok dizine kopyalaniyor"
)
endif()
