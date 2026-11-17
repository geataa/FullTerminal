CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-misleading-indentation
NATIVE_PREFIX := build/native-installed/x64-linux-static
NATIVE_STAMP := $(NATIVE_PREFIX)/lib/libmariadbclient.a
INCLUDES := -Isrc -Ivendor/sqlite -I$(NATIVE_PREFIX)/include -I$(NATIVE_PREFIX)/include/mysql
CC ?= gcc
CFLAGS ?= -O2
SQLITE_DEFS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_ENABLE_COLUMN_METADATA

ifneq ($(wildcard deps/extracted/usr/include/.*),)
    INCLUDES += -Ideps/extracted/usr/include -Ideps/extracted/usr/include/freetype2
endif

LDFLAGS :=
ifneq ($(wildcard deps/lib/.*),)
    LDFLAGS += -Ldeps/lib
endif
NATIVE_LIBS = $(wildcard $(NATIVE_PREFIX)/lib/lib*.a)
LIBS := -lX11 -lGL -lfreetype -lfontconfig -lpthread -lutil -ldl -lm

SRCS := \
    src/main.cpp \
    src/database/Database.cpp \
    src/database/Sql.cpp \
    src/database/SqliteBackend.cpp \
    src/database/OdbcBackend.cpp \
    src/database/NativeBackend.cpp \
    src/database/PostgresBackend.cpp \
    src/database/MysqlBackend.cpp \
    src/database/SqlServerBackend.cpp \
    src/core/Utf8.cpp \
    src/core/ShellProfiles.cpp \
    src/core/Settings.cpp \
    src/core/I18n.cpp \
    src/model/Inventory.cpp \
    src/model/K8sManager.cpp \
    src/model/SftpController.cpp \
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
    src/mcp/McpHttpServer.cpp \
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
    src/ui/MainWindow_Swarm.cpp \
    src/ui/MainWindow_Replay.cpp \
    src/ui/MainWindow_Database.cpp \
    src/platform/linux/LinuxNative.cpp

OBJS := $(patsubst %.cpp,build/obj/%.o,$(SRCS)) build/obj/vendor/sqlite/sqlite3.o
TARGET := build/fullterminal

.PHONY: all build clean test deb portable appimage release install

all build: $(TARGET) portable

$(NATIVE_STAMP):
	@chmod +x tools/build_native_clients_linux.sh
	@./tools/build_native_clients_linux.sh

$(TARGET): $(OBJS) $(NATIVE_STAMP)
	@mkdir -p $(dir $@)
	@rm -f $@
	$(CXX) $(OBJS) -Wl,--start-group $(NATIVE_LIBS) -Wl,--end-group -o $@ $(LDFLAGS) $(LIBS)
	@echo "Build complete: $(TARGET)"

build/obj/%.o: %.cpp $(NATIVE_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP $(INCLUDES) -c $< -o $@

-include $(OBJS:.o=.d)

# Gomulu SQLite (Windows surumuyle ayni amalgamation)
build/obj/vendor/sqlite/sqlite3.o: vendor/sqlite/sqlite3.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SQLITE_DEFS) -c $< -o $@

# ---------------------------------------------------------------- testler --
# CMake (Windows) duzeniyle ayni: cekirdek kitaplik + GUI kitapligi, her test
# kendi calisma klasorunde (build/test-runs/<ad>) 60 sn zaman asimiyla kosar.
CORE_OBJS := $(filter-out build/obj/src/main.o build/obj/src/ui/MainWindow%,$(OBJS))
GUI_OBJS := $(filter build/obj/src/ui/MainWindow%,$(OBJS))

build/libft_core.a: $(CORE_OBJS)
	@rm -f $@
	ar rcs $@ $^

build/libft_gui.a: $(GUI_OBJS)
	@rm -f $@
	ar rcs $@ $^

# Windows'a ozgu testler: conpty_daemon (yerine pty_daemon_linux), database_odbc (surucu ister)
LINUX_TESTS := i18n theme_consistency opacity_accent osc_semantics session_recording \
    screen_resize_snippet ssh_loading_fleet agent_detector agent_ribbon pane_layout \
    swarm_workspace pty_daemon_linux mcp_panes window_presentation sftp_navigation \
    database database_ui database_native
GUI_TESTS := window_presentation database_ui
TEST_BINS := $(addprefix build/tests/test_,$(LINUX_TESTS))

build/tests/test_%: tests/test_%.cpp build/libft_core.a build/libft_gui.a $(NATIVE_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -UNDEBUG $(INCLUDES) $< -o $@ -Wl,--start-group \
	    $(if $(filter $*,$(GUI_TESTS)),build/libft_gui.a) build/libft_core.a $(NATIVE_LIBS) -Wl,--end-group $(LDFLAGS) $(LIBS)

test: $(TARGET) $(TEST_BINS)
	@echo "Running test suite..."
	@fail=0; for t in $(LINUX_TESTS); do \
	    d=build/test-runs/$$t; rt=/tmp/ft-rt-$$$$-$$t; rm -rf $$d $$rt; mkdir -p $$d/bin $$d/portable_data $$rt && chmod 700 $$rt; \
	    printf '%-24s ' "$$t"; \
	    if (cd $$d && FULLTERMINAL_DATA_DIR="$$PWD/portable_data" XDG_RUNTIME_DIR="$$rt" timeout 60 ../../tests/test_$$t > run.log 2>&1); then echo PASS; \
	    else echo "FAIL (build/test-runs/$$t/run.log)"; tail -n 20 $$d/run.log; fail=1; fi; \
	    rm -rf $$rt; \
	done; \
	if command -v python3 >/dev/null; then \
	    printf '%-24s ' http_transport; \
	    rt=/tmp/ft-rt-$$$$-http; rm -rf $$rt; mkdir -p $$rt && chmod 700 $$rt; \
	    if XDG_RUNTIME_DIR="$$rt" timeout 45 python3 tests/test_http_transport.py $(abspath $(TARGET)) $(abspath build/test-runs) > build/test-runs/http_transport.log 2>&1; then echo PASS; \
	    else rc=$$?; if [ $$rc = 77 ]; then echo SKIP; else echo "FAIL (build/test-runs/http_transport.log)"; tail -n 20 build/test-runs/http_transport.log; fail=1; fi; fi; \
	    rm -rf $$rt; \
	fi; \
	if [ $$fail = 0 ]; then echo "All tests passed successfully!"; else echo "Some tests FAILED"; exit 1; fi

portable appimage: $(TARGET)
	@chmod +x packaging/build_portable.sh
	@./packaging/build_portable.sh

deb: $(TARGET)
	@echo "Building Debian package (.deb)..."
	@chmod +x packaging/build_deb.sh
	@./packaging/build_deb.sh

release: $(TARGET)
	@chmod +x packaging/package_release_linux.sh
	@./packaging/package_release_linux.sh

clean:
	rm -rf build/obj build/tests build/test-runs build/libft_core.a build/libft_gui.a $(TARGET) packaging/deb/ build/*.deb dist/ /tmp/FullTerminal.AppDir /tmp/FullTerminal-portable
