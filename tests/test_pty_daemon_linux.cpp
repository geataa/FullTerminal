#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PtySessionClient.h"
#include "core/ShellProfiles.h"
#include "mcp/Json.h"
#include <iostream>
#include <cassert>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>

using namespace ft;

void TestConptyDaemon() {
    std::cout << "[TEST] Starting ConPTY Daemon & Session Persistence unit tests...\n";

    if (!ConPty::Available()) {
        std::cout << "ConPTY not available on this Windows version. Skipping test.\n";
        return;
    }

    const std::string sessionId = "test_persist_1";
#if defined(_WIN32)
    std::wstring cmd = L"cmd.exe";
#else
    std::wstring cmd = L"/bin/sh";
#endif
    std::wstring startDir = UserHomeDir();
    std::vector<std::pair<std::wstring, std::wstring>> env;

    // 1. Daemon creates and starts session
    std::wstring err;
    auto session = SessionDaemon::Instance().CreateSession(
        sessionId, cmd, startDir, env, 80, 24, &err);

    assert(session != nullptr);
    assert(session->IsAlive());
    assert(!session->HasClient());
    std::cout << "Test 1 (Daemon session started): PASSED\n";

    // 2. Client 1 attaches to named pipe
    std::string received1;
    PtySessionClient client1;
    client1.onOutput = [&](const char* data, size_t len) {
        received1.append(data, len);
    };

    bool ok = client1.Attach(sessionId, &err);
    assert(ok);
    assert(client1.IsConnected());
    std::cout << "Test 2 (Client 1 attached to named pipe): PASSED" << std::endl;

    // Wait for initial prompt before sending input
#if defined(_WIN32)
    auto startPrompt = std::chrono::steady_clock::now();
    while (received1.find(">") == std::string::npos &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startPrompt).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif

    // 3. Write input through client 1 and verify output
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    client1.Write("echo HELLO_DAEMON_PERSISTENCE\r\n", 31);
    
    // Wait up to 3 seconds for output
    auto start = std::chrono::steady_clock::now();
    while (received1.find("HELLO_DAEMON_PERSISTENCE") == std::string::npos &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    assert(received1.find("HELLO_DAEMON_PERSISTENCE") != std::string::npos);
    std::cout << "Test 3 (Client 1 input/output roundtrip): PASSED\n";

    // 4. Client 1 detaches (simulating GUI closing / crash)
    client1.Detach();
    assert(!client1.IsConnected());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // CRUCIAL ACCEPTANCE CRITERIA: Session MUST still be alive!
    assert(session->IsAlive());
    assert(!session->HasClient());
    std::cout << "Test 4 (GUI detached, session SURVIVES in daemon): PASSED\n";

    // 5. Client 2 attaches (simulating GUI relaunch or new terminal window)
    std::string received2;
    PtySessionClient client2;
    client2.onOutput = [&](const char* data, size_t len) {
        received2.append(data, len);
    };

    ok = client2.Attach(sessionId, &err);
    assert(ok);
    assert(client2.IsConnected());

    // Wait for replay history to arrive
    start = std::chrono::steady_clock::now();
    while (received2.find("HELLO_DAEMON_PERSISTENCE") == std::string::npos &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    assert(received2.find("HELLO_DAEMON_PERSISTENCE") != std::string::npos);
    std::cout << "Test 5 (Client 2 attached and received buffer replay): PASSED\n";

    // 6. Terminate session cleanly
    client2.Kill();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(!session->IsAlive());
    std::cout << "Test 6 (Session clean kill): PASSED\n";

    // 7. Daemon kontrol kanali (Windows'taki named pipe = Linux'ta UNIX soket) uzerinden
    //    ping + create + attach + roundtrip + kill: GUI'nin kullandigi yol.
    SessionDaemon::Instance().StartDaemonPipe();
    {
        json::Value req = json::Value::Object(), resp;
        req["action"] = "ping";
        bool pinged = false;
        for (int i = 0; i < 50 && !pinged; ++i) {
            pinged = SessionDaemon::Request(req, resp) && resp["success"].as_bool();
            if (!pinged) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        assert(pinged);
        std::string received3;
        PtySessionClient client3;
        client3.onOutput = [&](const char* data, size_t len) { received3.append(data, len); };
        std::vector<std::pair<std::wstring, std::wstring>> env3{{L"FT_DAEMON_TEST", L"retained-value-731"}};
        ok = client3.Start(cmd, startDir, env3, 80, 24, &err);
        assert(ok);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const char* echo = "echo VAL=$FT_DAEMON_TEST\n";
        client3.Write(echo, strlen(echo));
        start = std::chrono::steady_clock::now();
        while (received3.find("VAL=retained-value-731") == std::string::npos &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        assert(received3.find("VAL=retained-value-731") != std::string::npos);
        json::Value list = json::Value::Object(), listResp;
        list["action"] = "list";
        assert(SessionDaemon::Request(list, listResp) && listResp["success"].as_bool());
        client3.Kill();
    }
    SessionDaemon::Instance().StopDaemonPipe();
    std::cout << "Test 7 (Daemon control socket: ping/create/attach/env/kill): PASSED\n";

    std::cout << "\n============================================\n";
    std::cout << "  ALL CONPTY DAEMON PERSISTENCE TESTS PASSED!\n";
    std::cout << "============================================\n";
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--daemon") {
        return SessionDaemon::RunStandalone();
    }
    TestConptyDaemon();
    return 0;
}
