#include <cassert>
#include <iostream>
#include <string>
#include "ui/TerminalTab.h"
#include "model/Inventory.h"
#include "core/Utf8.h"

namespace ft {

class SshLoadingTest {
public:
    static void RunAll() {
        std::cout << "[TEST] 1. SSH Debug Bastirma ve Loading Ekrani Gecis Testi..." << std::endl;

        TerminalTab tab;
        Host h;
        h.id = L"rpi5";
        h.label = L"Raspberry Pi 5 SkyCortex";
        h.address = L"192.168.1.135";
        h.port = 22;
        h.username = L"pi";
        h.kind = AuthKind::Key;

        tab.SetSshSession(h);
        assert(tab.GetSshStage() == SshStage::Connecting);
        std::cout << "  [OK] Baslangicta SshStage::Connecting dogrulandi." << std::endl;

        // 1. OpenSSH baslangic debug loglari parca parca gonderiliyor
        std::string chunk1 = 
            "debug1: Connecting to 192.168.1.135 [192.168.1.135] port 22.\n"
            "debug1: Connection established.\n"
            "debug1: identity file C:\\\\Users\\\\ilter_zbhki5f/.ssh/id_rsa type -1\n"
            "debug1: Local version string SSH-2.0-OpenSSH_for_Windows_9.5\n"
            "debug1: Remote protocol version 2.0, remote software version OpenSSH_9.2p1 Debian-2+deb12u3\n";

        tab.ProcessSshOutput(chunk1);

        // Baglanma suruyor olmali, ASLA Connected olmamali!
        assert(tab.GetSshStage() == SshStage::Connecting);
        // Ekrana hicbir debug ciktisi yazilmamis olmali!
        assert(tab.screen().CursorLineText().empty());
        std::cout << "  [OK] chunk1 debug1: satirlari ekrani kirletmedi, Connecting korundu." << std::endl;

        // 2. KEX ve Kimlik dogrulama adimlari
        std::string chunk2 =
            "debug1: kex: algorithm: curve25519-sha256\n"
            "debug1: kex: host key algorithm: ssh-ed25519\n"
            "debug1: Host '192.168.1.135' is known and matches the ED25519 host key.\n"
            "debug1: Authenticating to 192.168.1.135:22 as 'pi'\n"
            "debug1: Server accepts key: ...\n"
            "debug1: Authentication succeeded (publickey).\n";

        tab.ProcessSshOutput(chunk2);

        // Kimlik dogrulandi ancak henuz interaktif oturuma girilmedi:
        assert(tab.GetSshStage() == SshStage::Connecting);
        assert(tab.screen().CursorLineText().empty());
        std::wstring currentStep = tab.GetCurrentSshStep();
        std::cout << "  [OK] KEX/Auth islendi. Guncel asama: " << WideToUtf8(currentStep) << std::endl;
        assert(currentStep.find(L"Kimlik") != std::wstring::npos || currentStep.find(L"pi") != std::wstring::npos);

        // 3. OpenSSH interaktif oturuma geciyor ve Linux MOTD / kabuk istemi geliyor
        std::string chunk3 =
            "debug1: Entering interactive session.\n"
            "Linux skycortex 6.6.20+rpt-rpi-2712\r\n"
            "pi@skycortex:~ $ ";

        tab.ProcessSshOutput(chunk3);

        // ARTIK Baglanti kuruldu (Connected)!
        assert(tab.GetSshStage() == SshStage::Connected);
        std::cout << "  [OK] Entering interactive session goruldu -> SshStage::Connected gecisi basarili!" << std::endl;

        // Ekranda SADECE Linux ciktisi olmali, ASLA debug1: olmamali!
        std::string screenText = tab.screen().CursorLineText();
        std::cout << "  [OK] Ekrana basilan kabuk satiri: \"" << screenText << "\"" << std::endl;
        assert(screenText.find("debug1:") == std::string::npos);
        assert(screenText.find("pi@skycortex") != std::string::npos || screenText.find("$") != std::string::npos);
    }
};

} // namespace ft

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  FullTerminal - SSH Loading & Fleet Engine Testi" << std::endl;
    std::cout << "====================================================" << std::endl;

    ft::SshLoadingTest::RunAll();

    std::cout << "\n====================================================" << std::endl;
    std::cout << "  TUM TESTLER EKSIKSIZ BASARIYLA GECTI! (VERIFIED)" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
