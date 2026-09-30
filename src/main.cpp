#include "ui/MainWindow.h"
#include "mcp/McpServer.h"
#include "transport/daemon/SessionDaemon.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <string>

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE, _In_ PWSTR pCmdLine, _In_ int nCmdShow) {
    // OpenSSH AskPass helper (SFTP / SSH arka plan surecleri icin)
    wchar_t passBuf[4096]{};
    DWORD passLen = GetEnvironmentVariableW(L"FULLTERMINAL_SFTP_PASS", passBuf, 4096);
    if (passLen > 0) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
            int u8len = WideCharToMultiByte(CP_UTF8, 0, passBuf, passLen, nullptr, 0, nullptr, nullptr);
            if (u8len > 0) {
                std::string u8(u8len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, passBuf, passLen, u8.data(), u8len, nullptr, nullptr);
                u8 += "\n";
                DWORD written = 0;
                WriteFile(hOut, u8.data(), (DWORD)u8.size(), &written, nullptr);
                FlushFileBuffers(hOut);
            }
        }
        return 0;
    }

    // Check if launched as an MCP server or background ConPTY session daemon
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool runMcp = false;
    bool runDaemon = false;
    bool multi = false;
    std::wstring screenshotPath;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            // --mcp-stdio: Ayarlar > MCP sayfasinin kopyaladigi yapilandirma bu adi kullanir
            if (_wcsicmp(argv[i], L"--mcp") == 0 || _wcsicmp(argv[i], L"--mcp-stdio") == 0 ||
                _wcsicmp(argv[i], L"-mcp") == 0) runMcp = true;
            else if (_wcsicmp(argv[i], L"--daemon") == 0 || _wcsicmp(argv[i], L"--ftagent") == 0 ||
                     _wcsicmp(argv[i], L"-daemon") == 0) runDaemon = true;
            else if (_wcsicmp(argv[i], L"--multi") == 0) multi = true;
            else if (_wcsicmp(argv[i], L"--screenshot") == 0 && i + 1 < argc) {
                screenshotPath = argv[++i];
                multi = true;
            }
        }
        LocalFree(argv);
    }

    if (runMcp) {
        return ft::McpServer::RunStdio();
    }
    if (runDaemon) {
        return ft::SessionDaemon::RunStandalone();
    }

    // Tek ornek: tepside veya Guake'de gizli calisan kopya varken exe'ye tekrar
    // tiklamak ikinci bir pencere (ve kaydedilemeyen ikinci bir kisayol) acmasin;
    // mevcut pencere one gelsin. Ad exe yoluna bagli: ayri tasinabilir kopyalar
    // birbirini engellemez. --multi ile atlanir.
    HANDLE instanceMutex = nullptr;
    if (!multi) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        uint32_t h = 2166136261u;
        for (const wchar_t* p = exe; *p; ++p) { h ^= (uint32_t)towlower(*p); h *= 16777619u; }
        wchar_t name[96];
        swprintf_s(name, L"Local\\FullTerminal.SingleInstance.%08X", h);
        instanceMutex = CreateMutexW(nullptr, FALSE, name);
        if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = nullptr;
            for (int i = 0; i < 20 && !other; ++i) {   // ilk kopya henuz pencere acmamis olabilir
                other = FindWindowW(ft::MainWindow::ClassName(), nullptr);
                if (!other) Sleep(100);
            }
            if (other) {
                DWORD pid = 0;
                GetWindowThreadProcessId(other, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(other, ft::MainWindow::WM_SUMMON, 0, 0);
            }
            CloseHandle(instanceMutex);
            return 0;
        }
    }

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ft::MainWindow win;
    std::wstring err;
    if (!win.Create(inst, &err)) {
        MessageBoxW(nullptr, err.empty() ? L"Bilinmeyen hata." : err.c_str(),
                    L"FullTerminal", MB_ICONERROR | MB_OK);
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return 1;
    }
    win.Show(nCmdShow);

    if (!screenshotPath.empty()) {
        win.ClearToast();
        win.ForceRender();
        Sleep(150);
        win.ClearToast();
        win.ForceRender();
        win.SaveScreenshot(screenshotPath.c_str());
        if (SUCCEEDED(hrCo)) CoUninitialize();
        if (instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }

    int exitCode = 0;
    MSG msg{};
    for (;;) {
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                exitCode = (int)msg.wParam;
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        if (!win.Alive()) continue; // WM_QUIT siradaki turda gelecek

        // Tick bir kare cizdiyse Present zaten vsync'e sabitledi.
        // Cizmediyse mesaj bekleyerek CPU'yu bosa harcamayalim.
        if (!win.Tick()) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    if (SUCCEEDED(hrCo)) CoUninitialize();
    if (instanceMutex) CloseHandle(instanceMutex);
    return exitCode;
}
