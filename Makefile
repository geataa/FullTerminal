CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result
INCLUDES := -Isrc

ifneq ($(wildcard deps/extracted/usr/include/.*),)
    INCLUDES += -Ideps/extracted/usr/include -Ideps/extracted/usr/include/freetype2
endif

LDFLAGS :=
ifneq ($(wildcard deps/lib/.*),)
    LDFLAGS += -Ldeps/lib
endif
LIBS := -lX11 -lGL -lfreetype -lfontconfig -lpthread -lutil

SRCS := \
    src/main.cpp \
    src/core/Utf8.cpp \
    src/core/ShellProfiles.cpp \
    src/core/Settings.cpp \
    src/core/I18n.cpp \
    src/model/Inventory.cpp \
    src/model/K8sManager.cpp \
    src/model/SftpModel.cpp \
    src/model/SnippetModel.cpp \
    src/model/TunnelModel.cpp \
    src/services/KeyGenService.cpp \
    src/services/KnownHostsService.cpp \
    src/services/AgentDetector.cpp \
    src/services/SwarmWorkspace.cpp \
    src/services/SessionRecorder.cpp \
    src/services/SessionReplayer.cpp \
    src/transport/pty/PosixPty.cpp \
    src/transport/daemon/PtySession.cpp \
    src/transport/daemon/PtySessionClient.cpp \
    src/transport/daemon/SessionDaemon.cpp \
    src/transport/agentless/AgentlessExecutor.cpp \
    src/mcp/McpBridge.cpp \
    src/mcp/McpServer.cpp \
    src/vt/Screen.cpp \
    src/vt/VtParser.cpp \
    src/render/Renderer.cpp \
    src/render/Renderer_Linux.cpp \
    src/ui/Ui.cpp \
    src/ui/TerminalTab.cpp \
    src/ui/PaneLayout.cpp \
    src/ui/MainWindow.cpp \
    src/ui/MainWindow_Linux.cpp \
    src/ui/MainWindow_Quake.cpp \
    src/ui/MainWindow_Screens.cpp \
    src/ui/MainWindow_Sidebar.cpp \
    src/ui/MainWindow_TabBar.cpp \
    src/ui/MainWindow_AgentRibbon.cpp \
    src/ui/MainWindow_Fleet.cpp \
    src/ui/MainWindow_Mcp.cpp \
    src/ui/MainWindow_Swarm.cpp

OBJS := $(patsubst %.cpp,build/obj/%.o,$(SRCS))
TARGET := build/fullterminal

.PHONY: all build clean test deb portable appimage install

all build: $(TARGET) portable

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
	@echo "Build complete: $(TARGET)"

build/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

test: $(TARGET)
	@echo "Running test suite..."
	@mkdir -p build/tests
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_pane_layout.cpp src/ui/PaneLayout.cpp src/ui/TerminalTab.cpp src/vt/Screen.cpp src/vt/VtParser.cpp src/transport/pty/PosixPty.cpp src/core/Utf8.cpp src/core/Settings.cpp src/core/ShellProfiles.cpp src/services/SessionRecorder.cpp src/model/SftpModel.cpp src/model/Inventory.cpp src/model/K8sManager.cpp src/transport/agentless/AgentlessExecutor.cpp src/services/AgentDetector.cpp -o build/tests/test_pane_layout $(LDFLAGS) -lutil -lpthread && ./build/tests/test_pane_layout
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_i18n.cpp src/core/I18n.cpp -o build/tests/test_i18n && ./build/tests/test_i18n
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_osc_semantics.cpp src/vt/Screen.cpp src/vt/VtParser.cpp src/core/Utf8.cpp -o build/tests/test_osc_semantics && ./build/tests/test_osc_semantics
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_session_recording.cpp src/services/SessionRecorder.cpp src/services/SessionReplayer.cpp src/vt/Screen.cpp src/vt/VtParser.cpp src/core/Utf8.cpp -o build/tests/test_session_recording && ./build/tests/test_session_recording
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_screen_resize_snippet.cpp src/vt/Screen.cpp src/core/Utf8.cpp src/model/SnippetModel.cpp -o build/tests/test_screen_resize_snippet && ./build/tests/test_screen_resize_snippet
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_theme_consistency.cpp src/vt/Screen.cpp src/vt/VtParser.cpp src/core/Utf8.cpp -o build/tests/test_theme_consistency && ./build/tests/test_theme_consistency
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_opacity_accent.cpp src/core/Settings.cpp src/core/Utf8.cpp src/core/ShellProfiles.cpp -o build/tests/test_opacity_accent && ./build/tests/test_opacity_accent
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_agent_detector.cpp src/services/AgentDetector.cpp src/core/Utf8.cpp -o build/tests/test_agent_detector && ./build/tests/test_agent_detector
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_conpty_daemon.cpp src/transport/daemon/SessionDaemon.cpp src/transport/daemon/PtySession.cpp src/transport/daemon/PtySessionClient.cpp src/transport/pty/PosixPty.cpp src/core/ShellProfiles.cpp src/core/Utf8.cpp src/core/Settings.cpp -o build/tests/test_conpty_daemon $(LDFLAGS) -lutil -lpthread && ./build/tests/test_conpty_daemon
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_mcp_panes.cpp src/mcp/McpServer.cpp src/core/Settings.cpp src/core/Utf8.cpp src/core/ShellProfiles.cpp src/transport/daemon/SessionDaemon.cpp src/transport/daemon/PtySession.cpp src/transport/pty/PosixPty.cpp src/model/Inventory.cpp src/model/K8sManager.cpp src/transport/agentless/AgentlessExecutor.cpp src/mcp/McpBridge.cpp -o build/tests/test_mcp_panes $(LDFLAGS) $(LIBS) && ./build/tests/test_mcp_panes
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_ssh_loading_fleet.cpp src/ui/TerminalTab.cpp src/vt/Screen.cpp src/vt/VtParser.cpp src/core/Utf8.cpp src/core/Settings.cpp src/core/ShellProfiles.cpp src/model/Inventory.cpp src/model/SftpModel.cpp src/model/K8sManager.cpp src/transport/agentless/AgentlessExecutor.cpp src/transport/pty/PosixPty.cpp src/services/AgentDetector.cpp src/services/SessionRecorder.cpp -o build/tests/test_ssh_loading_fleet $(LDFLAGS) -lutil -lpthread && ./build/tests/test_ssh_loading_fleet
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_agent_ribbon.cpp src/services/AgentDetector.cpp src/core/Utf8.cpp -o build/tests/test_agent_ribbon && ./build/tests/test_agent_ribbon
	@echo "All tests passed successfully!"

portable appimage: $(TARGET)
	@chmod +x packaging/build_portable.sh
	@./packaging/build_portable.sh

deb: $(TARGET)
	@echo "Building Debian package (.deb)..."
	@chmod +x packaging/build_deb.sh
	@./packaging/build_deb.sh

clean:
	rm -rf build/obj build/tests $(TARGET) packaging/deb/ build/*.deb dist/ /tmp/FullTerminal.AppDir /tmp/FullTerminal-portable
