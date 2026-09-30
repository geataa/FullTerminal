#pragma once

#include "model/Inventory.h"

#include <string>
#include <vector>
#include <windows.h>

namespace ft {

enum class TunnelType {
    Local = 0,    // -L localPort:remoteHost:remotePort
    Remote = 1,   // -R remotePort:localHost:localPort
    Dynamic = 2   // -D localPort (SOCKS5 Proxy)
};

struct TunnelRule {
    std::wstring id;
    std::wstring name;
    std::wstring hostId;
    std::wstring hostDisplay;
    TunnelType   type = TunnelType::Local;
    int          localPort = 8080;
    std::wstring remoteHost = L"localhost";
    int          remotePort = 80;
    bool         autoStart = false;

    // Çalışma durumu
    bool         running = false;
    HANDLE       hProcess = nullptr;
    DWORD        pid = 0;
    std::wstring lastError;
};

class TunnelModel {
public:
    TunnelModel(Inventory& inv);
    ~TunnelModel();

    void Load(const std::wstring& dataDir);
    void Save(const std::wstring& dataDir) const;

    std::vector<TunnelRule>& Rules() { return m_rules; }
    const std::vector<TunnelRule>& Rules() const { return m_rules; }

    bool AddRule(const std::wstring& name, const std::wstring& hostId,
                 TunnelType type, int localPort, const std::wstring& remoteHost, int remotePort);
    bool DeleteRule(const std::wstring& id);

    bool StartTunnel(const std::wstring& id, std::wstring* err = nullptr);
    bool StopTunnel(const std::wstring& id);
    void StopAll();

    void CheckStatus();

private:
    std::wstring BuildTunnelCommand(const TunnelRule& rule, const Host& host, std::wstring* err) const;

    Inventory& m_inv;
    std::vector<TunnelRule> m_rules;
    std::wstring m_dataDir;
};

} // namespace ft
