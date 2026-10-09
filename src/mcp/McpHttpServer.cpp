#include <winsock2.h>
#include <ws2tcpip.h>
#include "mcp/McpHttpServer.h"
#include "mcp/McpServer.h"
#include "mcp/Json.h"
#include "core/Settings.h"
#include "core/Utf8.h"
#include "transport/agentless/AgentlessExecutor.h"
#include "model/K8sManager.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
#include <cstdio>

namespace ft {
namespace {
constexpr size_t MaxBody = 1024 * 1024;
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n"), last = s.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, last - first + 1);
}
bool TokenValid(const std::string& token) {
    return token.size() >= 32 && token.size() <= 1024 &&
        std::all_of(token.begin(), token.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
bool Equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0; for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}
void Reply(SOCKET s, int status, const std::string& body = "") {
    const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 401 ? "Unauthorized" :
        status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" :
        status == 413 ? "Content Too Large" : status == 503 ? "Service Unavailable" : "Bad Request";
    std::string msg = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nConnection: close\r\n";
    msg += "Content-Type: application/json\r\nCache-Control: no-store\r\n";
    if (status == 405) msg += "Allow: POST\r\n";
    if (status == 401) msg += "WWW-Authenticate: Bearer\r\n";
    msg += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    size_t offset = 0;
    while (offset < msg.size()) { int n = send(s, msg.data() + offset, (int)(msg.size() - offset), 0); if (n <= 0) break; offset += n; }
}
void Handle(SOCKET socket, int port) {
    DWORD timeout = 5000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    std::string data; char buffer[4096]; size_t end;
    const auto deadline = GetTickCount64() + 10000;
    while ((end = data.find("\r\n\r\n")) == std::string::npos) {
        int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n <= 0 || data.size() + n > 16384 || GetTickCount64() > deadline) { Reply(socket, 400); return; }
        data.append(buffer, n);
    }
    std::istringstream stream(data.substr(0, end));
    std::string method, path, version, line;
    std::getline(stream, line); std::istringstream requestLine(line); requestLine >> method >> path >> version;
    if (version != "HTTP/1.1" || path != "/mcp") { Reply(socket, path != "/mcp" ? 404 : 400); return; }
    std::map<std::string, std::string> headers;
    while (std::getline(stream, line)) {
        auto colon = line.find(':'); if (colon == std::string::npos) { Reply(socket, 400); return; }
        auto key = Lower(Trim(line.substr(0, colon)));
        if (headers.count(key)) { Reply(socket, 400); return; }
        headers[key] = Trim(line.substr(colon + 1));
    }
    const auto local = "127.0.0.1:" + std::to_string(port), named = "localhost:" + std::to_string(port);
    if (headers["host"] != local && headers["host"] != named) { Reply(socket, 403); return; }
    if (headers.count("origin") && headers["origin"] != "http://" + local && headers["origin"] != "http://" + named) { Reply(socket, 403); return; }
    Settings cfg; cfg.Load(PortableDataDir());
    if (!cfg.mcpEnabled || !cfg.mcpHttp || cfg.mcpPort != port) { Reply(socket, 503); return; }
    auto token = WideToUtf8(cfg.mcpToken);
    if (!TokenValid(token) || !Equal(headers["authorization"], "Bearer " + token)) { Reply(socket, 401); return; }
    const auto protocol = headers["mcp-protocol-version"];
    if (!protocol.empty() && protocol != "2025-03-26" && protocol != "2025-06-18") { Reply(socket, 400); return; }
    if (method != "POST") { Reply(socket, 405); return; }
    auto contentType = Lower(headers["content-type"]);
    if ((contentType != "application/json" && contentType != "application/json; charset=utf-8") ||
        headers["accept"].find("application/json") == std::string::npos || headers.count("transfer-encoding")) { Reply(socket, 400); return; }
    size_t length = 0;
    auto& lengthText = headers["content-length"];
    auto result = std::from_chars(lengthText.data(), lengthText.data() + lengthText.size(), length);
    if (result.ec != std::errc{} || result.ptr != lengthText.data() + lengthText.size() || length == 0) { Reply(socket, 400); return; }
    if (length > MaxBody) { Reply(socket, 413); return; }
    std::string body = data.substr(end + 4);
    while (body.size() < length) {
        int n = recv(socket, buffer, (int)std::min(sizeof(buffer), length - body.size()), 0);
        if (n <= 0 || GetTickCount64() > deadline) { Reply(socket, 400); return; }
        body.append(buffer, n);
    }
    if (body.size() != length) { Reply(socket, 400); return; }
    bool valid = false; auto req = json::Value::parse(body, &valid);
    if (!valid || !req.is_object() || req["jsonrpc"].as_string() != "2.0") { Reply(socket, 400); return; }
    auto response = McpServer::ProcessMessage(body, true);
    Reply(socket, response.empty() ? 202 : 200, response);
}
}
int McpHttpServer::Run() {
    AgentlessExecutor::SetDataDir(PortableDataDir());
    K8sManager::Instance().Rescan(PortableDataDir());
    Settings cfg; cfg.Load(PortableDataDir());
    if (!cfg.mcpEnabled || !cfg.mcpHttp || !TokenValid(WideToUtf8(cfg.mcpToken))) return 2;
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 3;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE; setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons((u_short)cfg.mcpPort); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener == INVALID_SOCKET || bind(listener, (sockaddr*)&address, sizeof(address)) || listen(listener, 8)) {
        closesocket(listener); WSACleanup(); return 4;
    }
    for (;;) {
        Settings current; current.Load(PortableDataDir());
        if (!current.mcpEnabled || !current.mcpHttp || current.mcpPort != cfg.mcpPort) break;
        fd_set ready; FD_ZERO(&ready); FD_SET(listener, &ready); timeval wait{0, 200000};
        int count = select(0, &ready, nullptr, nullptr, &wait);
        if (count < 0) break;
        if (!count) continue;
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client != INVALID_SOCKET) {
            try { Handle(client, cfg.mcpPort); } catch (...) { Reply(client, 400); }
            shutdown(client, SD_SEND); closesocket(client);
        }
    }
    closesocket(listener); WSACleanup(); return 0;
}
}
