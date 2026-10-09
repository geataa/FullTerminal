#pragma once
namespace ft {
class McpHttpServer {
public:
    // Stateless Streamable HTTP, loopback only; token and permissions come from Settings.
    static int Run();
};
}
