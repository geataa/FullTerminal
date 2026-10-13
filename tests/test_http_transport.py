"""Exercise the real portable HTTP process with isolated synthetic settings."""
import http.client
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import time
import uuid

root = pathlib.Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
fixture = root / ("http-" + uuid.uuid4().hex)
fixture.mkdir()
exe = fixture / "FullTerminal.exe"
shutil.copy2(sys.argv[1], exe)
data = fixture / "portable_data"
data.mkdir()
with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
token = "test-only-" + uuid.uuid4().hex

def settings(enabled=True, readonly=True):
    temporary = data / "settings.tmp"
    temporary.write_text(
        f"mcpEnabled={int(enabled)}\nmcpHttp=1\nmcpStdio=0\nmcpPort={port}\n"
        f"mcpToken={token}\nmcpReadOnly={int(readonly)}\nmcpAllowRun=1\nmcpAllowFiles=1\nmcpApproval=0\nmcpAudit=0\n", encoding="utf-8")
    temporary.replace(data / "settings.ini")

def request(body=None, method="POST", extra=None):
    headers = {"Authorization": f"Bearer {token}", "Content-Type": "application/json",
               "Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2025-06-18"}
    if extra:
        headers.update(extra)
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=8)
    try:
        conn.request(method, "/mcp", json.dumps(body) if body is not None else "", headers)
        response = conn.getresponse()
        return response.status, response.read()
    finally:
        conn.close()

def fragmented_rejection(header, expected):
    body = b'{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
    raw = (f'POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n'
           f'Authorization: Bearer {token}\r\n{header}\r\n'
           f'Content-Length: {len(body)}\r\n\r\n').encode()
    with socket.create_connection(('127.0.0.1', port), timeout=5) as conn:
        conn.sendall(raw)
        # Force a header-only receive: the rejection must survive a late body.
        time.sleep(0.03)
        conn.sendall(body)
        response = http.client.HTTPResponse(conn)
        response.begin()
        assert response.status == expected
        response.read()

settings()
process = subprocess.Popen([str(exe), "--mcp-http"], cwd=fixture, creationflags=subprocess.CREATE_NO_WINDOW)
try:
    for _ in range(100):
        try:
            status, body = request({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
            break
        except OSError:
            if process.poll() is not None:
                raise AssertionError(f"HTTP process exited: {process.returncode}")
            time.sleep(0.05)
    else:
        raise AssertionError("HTTP server did not start")
    assert status == 200, (status, body)
    assert json.loads(body)["result"]["protocolVersion"] == "2025-06-18"
    assert request({"jsonrpc": "2.0", "method": "notifications/initialized"}) == (202, b"")
    rpc = {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}
    status, body = request(rpc)
    assert status == 200
    names = {t["name"] for t in json.loads(body)["result"]["tools"]}
    assert "ft_pane_read" in names and "ft_exec" not in names
    settings(readonly=False)
    status, body = request(rpc)
    assert len(json.loads(body)["result"]["tools"]) == 11
    settings()
    assert request(rpc, extra={"Authorization": "Bearer wrong"})[0] == 401
    assert request(rpc, extra={"Origin": "https://untrusted.example"})[0] == 403
    for _ in range(5):
        fragmented_rejection('Origin: https://untrusted.example', 403)
    assert request(rpc, extra={"Host": "untrusted.example"})[0] == 403
    assert request(rpc, extra={"MCP-Protocol-Version": "invalid"})[0] == 400
    assert request(method="GET")[0] == 405
    for tool, args in [("ft_exec", {"command": "echo forbidden"}), ("ft_session_record", {"action": "start"})]:
        status, body = request({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": tool, "arguments": args}})
        assert status == 200 and json.loads(body)["result"]["isError"] is True
    assert request([rpc])[0] == 400
    settings(False)
    process.wait(timeout=5)
    assert process.returncode == 0
    print("PASS: HTTP initialize, 11 tools, notification, token, origin, host, protocol, read-only, shutdown")
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
