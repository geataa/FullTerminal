#pragma once
//
// FullTerminal — MCP GUI IPC Bridge
// FullTerminal headless MCP sunucusu (--mcp) ile calisan Direct3D 11 GUI
// penceresi arasinda yuksek hizli (microsecond), sifir-DLL Win32 bellek
// eslemeli (memory-mapped file) ve WM_COPYDATA tabanli cift yonlu iletisim koprusu.
//

#include "mcp/Json.h"
#include <string>
#include <cstdint>
#include <windows.h>

namespace ft {

constexpr ULONG_PTR FT_IPC_MAGIC = 0x46544D43; // 'FTMC'

class McpBridge {
public:
    // Calisan FullTerminal GUI penceresinin acik olup olmadigini kontrol eder
    static bool IsGuiRunning();

    // GUI calismiyorsa otomatik baslatmayi dener ve pencerenin hazir olmasini bekler
    static bool EnsureGuiRunning(int timeoutMs = 3000);

    // GUI penceresine senkron JSON istegi gonderir ve JSON yanitini alir
    static bool CallGui(const json::Value& req, json::Value& resp, std::string& err);
};

} // namespace ft
