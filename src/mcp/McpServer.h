#pragma once

#include <string>

namespace ft {

class McpServer {
public:
    // Run the MCP server over stdio (JSON-RPC 2.0 newline-delimited)
    static int RunStdio();

    // Process a single JSON-RPC line and return the response line (or empty if notification)
    static std::string ProcessMessage(const std::string& line);
};

} // namespace ft
