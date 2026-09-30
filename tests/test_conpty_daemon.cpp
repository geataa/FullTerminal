#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PtySessionClient.h"
#include "core/ShellProfiles.h"
#include <iostream>
#include <cassert>
#include <string>
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
    std::wstring cmd = L"cmd.exe";
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

    // Wait for cmd.exe initial prompt before sending input
    auto startPrompt = std::chrono::steady_clock::now();
    while (received1.find(">") == std::string::npos &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startPrompt).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

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

    std::cout << "\n============================================\n";
    std::cout << "  ALL CONPTY DAEMON PERSISTENCE TESTS PASSED!\n";
    std::cout << "============================================\n";
}

int main() {
    TestConptyDaemon();
    return 0;
}
