#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PtySessionClient.h"
#include "transport/daemon/PipeIo.h"
#include "core/ShellProfiles.h"
#include "core/Utf8.h"
#include "model/K8sManager.h"
#include "mcp/Json.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <string>
#include <cstring>
#include <chrono>
#include <mutex>
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
    std::mutex rxMtx;
    auto hasSubstr = [&](const std::string& buf, const char* needle) {
        std::lock_guard<std::mutex> lk(rxMtx);
        return buf.find(needle) != std::string::npos;
    };

    std::string received1;
    PtySessionClient client1;
    client1.onOutput = [&](const char* data, size_t len) {
        std::lock_guard<std::mutex> lk(rxMtx);
        received1.append(data, len);
    };

    bool ok = client1.Attach(sessionId, &err);
    assert(ok);
    assert(client1.IsConnected());
    std::cout << "Test 2 (Client 1 attached to named pipe): PASSED" << std::endl;

    // Wait for initial prompt before sending input
#if defined(_WIN32)
    auto startPrompt = std::chrono::steady_clock::now();
    while (!hasSubstr(received1, ">") &&
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
    while (!hasSubstr(received1, "HELLO_DAEMON_PERSISTENCE") &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    assert(hasSubstr(received1, "HELLO_DAEMON_PERSISTENCE"));
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
        std::lock_guard<std::mutex> lk(rxMtx);
        received2.append(data, len);
    };

    ok = client2.Attach(sessionId, &err);
    assert(ok);
    assert(client2.IsConnected());

    // Wait for replay history to arrive
    start = std::chrono::steady_clock::now();
    while (!hasSubstr(received2, "HELLO_DAEMON_PERSISTENCE") &&
           std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    assert(hasSubstr(received2, "HELLO_DAEMON_PERSISTENCE"));
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
        client3.onOutput = [&](const char* data, size_t len) {
            std::lock_guard<std::mutex> lk(rxMtx);
            received3.append(data, len);
        };
        std::vector<std::pair<std::wstring, std::wstring>> env3{{L"FT_DAEMON_TEST", L"retained-value-731"}};
        ok = client3.Start(cmd, startDir, env3, 80, 24, &err);
        assert(ok);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const char* echo = "echo VAL=$FT_DAEMON_TEST\n";
        client3.Write(echo, strlen(echo));
        start = std::chrono::steady_clock::now();
        while (!hasSubstr(received3, "VAL=retained-value-731") &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        assert(hasSubstr(received3, "VAL=retained-value-731"));
        json::Value list = json::Value::Object(), listResp;
        list["action"] = "list";
        assert(SessionDaemon::Request(list, listResp) && listResp["success"].as_bool());
        client3.Kill();
    }
    SessionDaemon::Instance().StopDaemonPipe();
    std::cout << "Test 7 (Daemon control socket: ping/create/attach/env/kill): PASSED\n";

#if !defined(_WIN32)
    // 8. pkill / SIGKILL sonrasi diskte bayat d.<Scope>.sock kaldiginda (ECONNREFUSED)
    //    otomatik temizlenip yeni --daemon surecinin baslatildigini dogrula.
    {
        const std::string sockPath = pipeio::SocketPath(pipeio::DaemonName().c_str());
        assert(!sockPath.empty());
        ::unlink(sockPath.c_str());
        const int staleFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        assert(staleFd >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
        assert(::bind(staleFd, (sockaddr*)&addr, sizeof(addr)) == 0);
        ::close(staleFd); // Dosya diskte kaldi ama dinleyen surec yok (ECONNREFUSED)!

        std::string received4;
        PtySessionClient client4;
        client4.onOutput = [&](const char* data, size_t len) {
            std::lock_guard<std::mutex> lk(rxMtx);
            received4.append(data, len);
        };
        ok = client4.Start(cmd, startDir, {}, 80, 24, &err);
        assert(ok);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const char* echo4 = "echo STALE_SOCK_RECOVERED\n";
        client4.Write(echo4, strlen(echo4));
        start = std::chrono::steady_clock::now();
        while (!hasSubstr(received4, "STALE_SOCK_RECOVERED") &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        assert(hasSubstr(received4, "STALE_SOCK_RECOVERED"));
        client4.Kill();
        std::cout << "Test 8 (Stale UNIX socket ECONNREFUSED auto-recovery): PASSED\n";
    }

    // 9. Kubernetes K8sManager: coklu kubeconfig (k3s default context), aktif kume kaliciligi,
    //    yeni terminalde otomatik KUBECONFIG ve acik terminalde canli export dogrulamasi.
    {
        namespace fs = std::filesystem;
        const fs::path fx = fs::absolute("k8s-env-fixture");
        std::error_code ec;
        fs::remove_all(fx, ec);
        fs::create_directories(fx / "src");
        fs::create_directories(fx / "data");

        auto writeKube = [&](const fs::path& p, const char* server) {
            std::ofstream ofs(p);
            ofs << "apiVersion: v1\n"
                << "kind: Config\n"
                << "clusters:\n"
                << "- cluster:\n"
                << "    server: " << server << "\n"
                << "  name: default\n"
                << "contexts:\n"
                << "- context:\n"
                << "    cluster: default\n"
                << "    user: default\n"
                << "  name: default\n"
                << "current-context: default\n"
                << "users:\n"
                << "- name: default\n"
                << "  user:\n"
                << "    token: test-dummy\n";
        };

        writeKube(fx / "src" / "akdeniz.yaml", "https://127.0.0.1:6441");
        writeKube(fx / "src" / "ankara.yaml",  "https://127.0.0.1:6442");

        K8sManager& km = K8sManager::Instance();
        km.Init((fx / "data").wstring());
        assert(km.ImportYamlFile((fx / "src" / "akdeniz.yaml").wstring()));
        assert(km.ImportYamlFile((fx / "src" / "ankara.yaml").wstring()));
        assert(km.KubeconfigCount() == 2);

        // Son eklenen ankara.yaml aktif (0. indeks) olmali ve Rescan sonrasi korunmali
        assert(WideToUtf8(km.Kubeconfigs()[0].path).find("ankara.yaml") != std::string::npos);
        km.Rescan((fx / "data").wstring());
        assert(WideToUtf8(km.Kubeconfigs()[0].path).find("ankara.yaml") != std::string::npos);

        // Disaridan klasore kopyalanan YAML dosyasi RefreshIfChanged ile aninda yakalanmali
        writeKube(fx / "data" / "k8s" / "izmir.yaml", "https://127.0.0.1:6443");
        assert(km.RefreshIfChanged());
        assert(km.KubeconfigCount() == 3);
        assert(WideToUtf8(km.Kubeconfigs()[0].path).find("ankara.yaml") != std::string::npos);

        // Yeni terminal acildiginda KUBECONFIG ortam degiskeni otomatik gelmeli (ilk sirada ankara.yaml)
        std::string received5;
        PtySessionClient client5;
        client5.onOutput = [&](const char* data, size_t len) {
            std::lock_guard<std::mutex> lk(rxMtx);
            received5.append(data, len);
        };
        ok = client5.Start(L"/bin/bash -l", startDir, km.TerminalEnv(false), 80, 24, &err);
        assert(ok);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const char* checkAuto = "printf 'AUTO_KC=%s\\n' \"$KUBECONFIG\"\n";
        client5.Write(checkAuto, strlen(checkAuto));
        start = std::chrono::steady_clock::now();
        while (!hasSubstr(received5, "AUTO_KC=/") &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        {
            std::lock_guard<std::mutex> lk(rxMtx);
            const size_t pAuto = received5.find("AUTO_KC=/");
            assert(pAuto != std::string::npos);
            const size_t pAnk = received5.find("ankara.yaml", pAuto);
            const size_t pAkd = received5.find("akdeniz.yaml", pAuto);
            assert(pAnk != std::string::npos && pAkd != std::string::npos && pAnk < pAkd);
        }

        // Acik terminalde aktif kume degistirilip ShellExportCommand calistirildiginda guncellenmeli
        size_t izmirIdx = 0;
        for (size_t i = 0; i < km.Kubeconfigs().size(); ++i) {
            if (WideToUtf8(km.Kubeconfigs()[i].path).find("izmir.yaml") != std::string::npos) izmirIdx = i;
        }
        assert(km.SetActiveKubeconfig(izmirIdx));
        const std::string liveCmd = km.ShellExportCommand(false) + "\nprintf 'LIVE_KC=%s\\n' \"$KUBECONFIG\"\n";
        client5.Write(liveCmd.c_str(), liveCmd.size());
        start = std::chrono::steady_clock::now();
        while (!hasSubstr(received5, "LIVE_KC=/") &&
               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        {
            std::lock_guard<std::mutex> lk(rxMtx);
            const size_t pLive = received5.find("LIVE_KC=/");
            assert(pLive != std::string::npos);
            const size_t pIzm = received5.find("izmir.yaml", pLive);
            const size_t pAnk = received5.find("ankara.yaml", pLive);
            assert(pIzm != std::string::npos && pAnk != std::string::npos && pIzm < pAnk);
        }

        client5.Kill();
        json::Value stopReq = json::Value::Object(), stopResp;
        stopReq["action"] = "stop";
        SessionDaemon::Request(stopReq, stopResp, false);
        fs::remove_all(fx, ec);
        std::cout << "Test 9 (K8sManager kubeconfig auto-env, persistence & live export): PASSED\n";
    }
#endif

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
