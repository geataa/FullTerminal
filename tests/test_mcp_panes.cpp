#include "mcp/McpServer.h"
#include "mcp/Json.h"
#include "core/Settings.h"
#include "ui/MainWindow.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
namespace ft { const wchar_t* MainWindow::ClassName() { return L"FullTerminalTestWindow"; } }
int main() {
    using namespace ft;
    Settings cfg; cfg.mcpEnabled = true; cfg.mcpStdio = true; cfg.mcpReadOnly = false;
    cfg.mcpAllowRun = cfg.mcpAllowFiles = true; cfg.mcpApproval = false; cfg.mcpAudit = false;
    cfg.mcpToken = L"test-token-for-dpapi-storage-123456789";
    cfg.Save(PortableDataDir());
    Settings restored; restored.Load(PortableDataDir());
    assert(restored.mcpToken == cfg.mcpToken);
    std::ifstream in(PortableDataDir() + L"\\settings.ini");
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    assert(text.find("test-token-for-dpapi") == std::string::npos);
    restored.mcpToken.clear(); restored.Save(PortableDataDir());
    Settings cleared; cleared.Load(PortableDataDir()); assert(cleared.mcpToken.empty());
    auto parse = [](const std::string& request) {
        bool ok = false; auto result = json::Value::parse(McpServer::ProcessMessage(request), &ok); assert(ok); return result;
    };
    auto init = parse(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
    assert(init["result"]["serverInfo"]["name"].as_string() == "FullTerminal");
    auto list = parse(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    assert(list["result"]["tools"].size() == 11);
    assert(McpServer::ProcessMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
    cfg.mcpReadOnly = true; cfg.Save(PortableDataDir());
    for (const auto* name : {"ft_exec", "ft_pane_prompt", "ft_pane_split", "ft_swarm_spawn", "ft_session_record"}) {
        auto req = json::Value::Object(); req["jsonrpc"] = "2.0"; req["id"] = 3; req["method"] = "tools/call";
        req["params"]["name"] = name; req["params"]["arguments"]["action"] = "start";
        auto result = parse(req.dump());
        assert(result["result"]["isError"].as_bool());
    }
    assert(parse("{")["error"]["code"].as_int() == -32700);
    cfg.mcpEnabled = false; cfg.Save(PortableDataDir());
    assert(parse(R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})")["result"]["tools"].size() == 0);
    std::cout << "PASS: MCP schemas, notifications, read-only gates, disabled server, encrypted token and token clearing\n";
}
