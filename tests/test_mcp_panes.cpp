#include "mcp/McpServer.h"
#include "mcp/Json.h"
#include "core/Settings.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace ft;

void TestMcpPanes() {
    std::cout << "[TEST] Starting MCP Pane Orchestration tools unit tests...\n";

    // Set up test settings so MCP is enabled
    {
        Settings cfg;
        cfg.mcpEnabled = true;
        cfg.mcpStdio = true;
        cfg.mcpAllowRun = true;
        cfg.mcpReadOnly = false;
        cfg.mcpAllowFiles = true;
        cfg.Save(PortableDataDir());
    }

    // 1. Test initialize
    {
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        assert(v["id"].as_int() == 1);
        assert(v["result"]["serverInfo"]["name"].as_string() == "FullTerminal");
        std::cout << "Test 1 (Initialize): PASSED\n";
    }

    // 2. Test tools/list contains all 5 new ft_pane_* tools
    {
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        assert(v["id"].as_int() == 2);
        const auto& tools = v["result"]["tools"];
        assert(tools.is_array());

        bool hasList = false;
        bool hasSplit = false;
        bool hasPrompt = false;
        bool hasRead = false;
        bool hasWait = false;

        for (const auto& t : tools.arrVal) {
            std::string name = t["name"].as_string();
            if (name == "ft_pane_list") hasList = true;
            if (name == "ft_pane_split") hasSplit = true;
            if (name == "ft_pane_prompt") {
                hasPrompt = true;
                // verify required field "input"
                const auto& reqArr = t["inputSchema"]["required"];
                assert(reqArr.is_array() && reqArr.size() > 0);
                assert(reqArr[0].as_string() == "input");
            }
            if (name == "ft_pane_read") hasRead = true;
            if (name == "ft_pane_wait") hasWait = true;
        }

        std::cout << "Tools detected -> list: " << hasList
                  << ", split: " << hasSplit
                  << ", prompt: " << hasPrompt
                  << ", read: " << hasRead
                  << ", wait: " << hasWait << "\n";

        assert(hasList);
        assert(hasSplit);
        assert(hasPrompt);
        assert(hasRead);
        assert(hasWait);
        std::cout << "Test 2 (tools/list has all ft_pane_* tools): PASSED\n";
    }

    // 3. Test calling ft_pane_list when GUI is not active (graceful error handling)
    {
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"ft_pane_list\",\"arguments\":{}}}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        assert(v["id"].as_int() == 3);
        const auto& result = v["result"];
        assert(result.is_object());
        const auto& content = result["content"];
        assert(content.is_array() && content.size() > 0);
        std::string text = content[0]["text"].as_string();
        std::cout << "ft_pane_list response: " << text.substr(0, std::min<size_t>(text.size(), 80)) << "...\n";
        assert(!text.empty());
        std::cout << "Test 3 (ft_pane_list graceful call): PASSED\n";
    }

    // 4. Test calling ft_pane_prompt validation
    {
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"ft_pane_prompt\",\"arguments\":{\"input\":\"\"}}}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        assert(v["id"].as_int() == 4);
        assert(v["result"]["isError"].as_bool() == true);
        std::cout << "Test 4 (ft_pane_prompt empty input validation): PASSED\n";
    }

    // 5. Test ft_terminal backward compatibility routing
    {
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"ft_terminal\",\"arguments\":{}}}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        assert(v["id"].as_int() == 5);
        assert(v["result"].is_object());
        std::cout << "Test 5 (ft_terminal routing): PASSED\n";
    }

    std::cout << "\n========================================\n";
    std::cout << "  ALL 5 MCP PANE TESTS PASSED CLEANLY!\n";
    std::cout << "========================================\n";
}

int main() {
    TestMcpPanes();
    return 0;
}
