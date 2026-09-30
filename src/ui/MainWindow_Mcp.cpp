#include "ui/MainWindowInternal.h"
#include "mcp/McpBridge.h"
#include "mcp/Json.h"
#include <sstream>

namespace ft {

LRESULT MainWindow::OnCopyData(HWND fromHwnd, const COPYDATASTRUCT* cds) {
    (void)fromHwnd;
    if (!cds || cds->dwData != FT_IPC_MAGIC || !cds->lpData || cds->cbData == 0) {
        return 0;
    }

    std::string payload(static_cast<const char*>(cds->lpData), cds->cbData);
    bool ok = false;
    json::Value envelope = json::Value::parse(payload, &ok);
    if (!ok || !envelope.is_object()) {
        return 0;
    }

    const std::string mapNameU8 = envelope["map"].as_string();
    if (mapNameU8.empty()) return 0;
    const std::wstring mapName = Utf8ToWide(mapNameU8);

    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapName.c_str());
    if (!hMap) return 0;

    void* pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!pBuf) {
        CloseHandle(hMap);
        return 0;
    }

    json::Value resp = json::Value::Object();
    try {
        HandleMcpRequest(envelope["req"], resp);
    } catch (const std::exception& e) {
        resp["success"] = false;
        resp["error"] = std::string("Exception: ") + e.what();
    } catch (...) {
        resp["success"] = false;
        resp["error"] = "Unknown exception in FullTerminal GUI.";
    }

    const std::string respStr = resp.dump();
    const uint32_t len = static_cast<uint32_t>(respStr.size());

    memcpy(pBuf, &len, sizeof(uint32_t));
    memcpy(static_cast<char*>(pBuf) + sizeof(uint32_t), respStr.data(), len);

    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);
    return 1;
}

void MainWindow::HandleMcpRequest(const json::Value& req, json::Value& resp) {
    if (!req.is_object()) {
        resp["success"] = false;
        resp["error"] = "Invalid request format: expected JSON object.";
        return;
    }

    const std::string action = req["action"].as_string();
    if (action == "list") {
        HandleMcpList(resp);
    } else if (action == "split") {
        HandleMcpSplit(req, resp);
    } else if (action == "prompt") {
        HandleMcpPrompt(req, resp);
    } else if (action == "read") {
        HandleMcpRead(req, resp);
    } else if (action == "focus") {
        HandleMcpFocus(req, resp);
    } else if (action == "swarm") {
        HandleMcpSwarm(req, resp);
    } else if (action == "record") {
        HandleMcpRecord(req, resp);
    } else {
        resp["success"] = false;
        resp["error"] = "Unknown MCP action: '" + action + "'. Valid actions: list, split, prompt, read, focus, swarm, record.";
    }
}

void MainWindow::HandleMcpList(json::Value& resp) {
    resp["success"] = true;
    resp["active_tab"] = static_cast<int64_t>(m_active);
    uint32_t activePane = 0;
    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        activePane = m_tabLayouts[m_active]->GetFocusedPaneId();
    }
    resp["active_pane"] = static_cast<int64_t>(activePane);

    json::Value tabsArr = json::Value::Array();
    for (size_t t = 0; t < m_tabLayouts.size(); ++t) {
        if (!m_tabLayouts[t]) continue;
        json::Value tObj = json::Value::Object();
        tObj["tab_id"] = static_cast<int64_t>(t);
        tObj["is_active"] = (t == m_active);

        std::wstring tabTitle = L"Tab " + std::to_wstring(t + 1);
        if (t < m_tabs.size() && m_tabs[t]) {
            tabTitle = m_tabs[t]->Title();
        }
        tObj["title"] = WideToUtf8(tabTitle);

        auto paneInfos = m_tabLayouts[t]->ComputeLayout(m_lay.term);
        json::Value panesArr = json::Value::Array();
        for (const auto& pi : paneInfos) {
            if (!pi.tab) continue;
            json::Value pObj = json::Value::Object();
            pObj["pane_id"] = static_cast<int64_t>(pi.id);
            pObj["is_focused"] = pi.isFocused;
            pObj["title"] = WideToUtf8(pi.tab->Title());
            pObj["profile"] = WideToUtf8(pi.tab->profile().name);
            pObj["alive"] = pi.tab->Alive();
            pObj["exited"] = pi.tab->Exited();
            pObj["dead"] = pi.tab->Dead();

            // Canli ajan durumunu al
            const auto& as = pi.tab->GetAgentStatus();
            json::Value asObj = json::Value::Object();
            asObj["state"] = AgentDetector::StateToString(as.state);
            asObj["kind"] = AgentDetector::KindToString(as.kind);
            asObj["rule_id"] = as.ruleId;
            asObj["matched_pattern"] = as.matchedPattern;
            asObj["detail"] = as.detail;
            pObj["agent_status"] = asObj;

            // Panel geometri koordinatlari
            json::Value geo = json::Value::Object();
            geo["left"] = static_cast<double>(pi.area.left);
            geo["top"] = static_cast<double>(pi.area.top);
            geo["right"] = static_cast<double>(pi.area.right);
            geo["bottom"] = static_cast<double>(pi.area.bottom);
            pObj["geometry"] = geo;

            panesArr.push_back(pObj);
        }
        tObj["panes"] = panesArr;
        tabsArr.push_back(tObj);
    }
    resp["tabs"] = tabsArr;
}

void MainWindow::HandleMcpSplit(const json::Value& req, json::Value& resp) {
    if (!m_r.Ready()) {
        resp["success"] = false;
        resp["error"] = "Renderer is not ready.";
        return;
    }

    if (m_tabs.empty() || m_tabLayouts.empty()) {
        if (!NewTab(DefaultProfileIndex())) {
            resp["success"] = false;
            resp["error"] = "Failed to create initial tab.";
            return;
        }
    }

    int64_t reqTab = req["tab_id"].as_int(-1);
    size_t targetTabIdx = (reqTab >= 0 && static_cast<size_t>(reqTab) < m_tabLayouts.size())
                          ? static_cast<size_t>(reqTab) : m_active;

    if (targetTabIdx >= m_tabLayouts.size() || !m_tabLayouts[targetTabIdx]) {
        resp["success"] = false;
        resp["error"] = "Target tab not found.";
        return;
    }

    SelectTab(targetTabIdx);

    const std::string dirStr = req["direction"].as_string("vertical");
    const SplitDirection dir = (dirStr == "horizontal") ? SplitDirection::Horizontal : SplitDirection::Vertical;

    const std::string sys = req["system"].as_string();
    ShellProfile p;
    Host sshHost;
    bool isSsh = false;

    auto LowerStr = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string lowSys = LowerStr(sys);

    if (sys.empty() || sys == "local") {
        if (auto* cur = Active()) p = cur->profile();
        else if (DefaultProfileIndex() < m_profiles.size()) p = m_profiles[DefaultProfileIndex()];
    } else if (lowSys.rfind("wsl:", 0) == 0 || lowSys.rfind("wsl/", 0) == 0) {
        const std::string distro = sys.substr(4);
        p.id = Utf8ToWide(sys);
        p.name = Utf8ToWide(distro);
        p.kind = ProfileKind::Wsl;
        p.badge = L"WSL";
        p.accent = 0x22C55E;
        p.wslDistro = Utf8ToWide(distro);
        p.rawCommand = ResolveTool(L"wsl.exe") + L" -d " + Utf8ToWide(distro);
        p.startDir = UserHomeDir();
    } else if (lowSys.rfind("docker:", 0) == 0 || lowSys.rfind("docker/", 0) == 0) {
        const std::string dName = sys.substr(7);
        p.id = Utf8ToWide(sys);
        p.name = L"Docker: " + Utf8ToWide(dName);
        p.kind = ProfileKind::Custom;
        p.badge = L"DKR";
        p.accent = 0x0EA5E9;
        p.rawCommand = ResolveTool(L"docker.exe") + L" exec -it " + Utf8ToWide(dName) + L" sh";
        p.startDir = UserHomeDir();
    } else if (const Host* foundHost = m_inv.FindHost(Utf8ToWide(sys))) {
        sshHost = *foundHost;
        isSsh = true;
    } else {
        bool foundProf = false;
        for (const auto& prof : m_profiles) {
            if (_wcsicmp(prof.id.c_str(), Utf8ToWide(sys).c_str()) == 0 ||
                _wcsicmp(prof.name.c_str(), Utf8ToWide(sys).c_str()) == 0) {
                p = prof;
                foundProf = true;
                break;
            }
        }
        if (!foundProf) {
            if (sys.find('@') != std::string::npos) {
                const size_t at = sys.find('@');
                sshHost.username = Utf8ToWide(sys.substr(0, at));
                sshHost.address = Utf8ToWide(sys.substr(at + 1));
                isSsh = true;
            } else {
                if (DefaultProfileIndex() < m_profiles.size()) p = m_profiles[DefaultProfileIndex()];
            }
        }
    }

    if (isSsh) {
        std::wstring cmdErr;
        const std::wstring cmd = m_inv.BuildSshCommand(sshHost, &cmdErr);
        if (cmd.empty()) {
            resp["success"] = false;
            resp["error"] = "Failed to build SSH command: " + WideToUtf8(cmdErr);
            return;
        }
        p.id = L"host:" + sshHost.id;
        p.name = sshHost.Display();
        p.badge = sshHost.production ? L"PROD" : L"SSH";
        p.accent = sshHost.production ? theme::Red : (sshHost.accent ? sshHost.accent : theme::Ac());
        p.rawCommand = cmd;
        p.startDir = UserHomeDir();
        p.kind = ProfileKind::Custom;
    }

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, static_cast<int>((m_lay.term.right - m_lay.term.left) / (fm.cellW * 2)));
    const int rows = std::max(2, static_cast<int>((m_lay.term.bottom - m_lay.term.top) / (fm.cellH * 2)));

    auto newTab = std::make_shared<TerminalTab>();
    if (isSsh) newTab->SetSshSession(sshHost);

    std::wstring startErr;
    if (!newTab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &startErr)) {
        resp["success"] = false;
        resp["error"] = "Failed to start pane process: " + WideToUtf8(startErr);
        return;
    }

    m_tabLayouts[targetTabIdx]->SplitActive(dir, newTab);
    m_tabs[targetTabIdx] = m_tabLayouts[targetTabIdx]->GetFocusedTab();
    const uint32_t newPaneId = m_tabLayouts[targetTabIdx]->FindPaneIdByTab(newTab.get());

    const std::string initialCmd = req["command"].as_string();
    if (!initialCmd.empty()) {
        std::string cmdToSend = initialCmd;
        if (cmdToSend.back() != '\n') cmdToSend += "\n";
        newTab->Write(cmdToSend);
    }

    SetView(View::Terminal);
    SyncGridToArea();
    ComputeLayout();
    m_dirty = true;

    resp["success"] = true;
    resp["tab_id"] = static_cast<int64_t>(targetTabIdx);
    resp["pane_id"] = static_cast<int64_t>(newPaneId);
    resp["title"] = WideToUtf8(newTab->Title());
    resp["system"] = sys.empty() ? "local" : sys;
    resp["direction"] = (dir == SplitDirection::Horizontal) ? "horizontal" : "vertical";
}

void MainWindow::HandleMcpPrompt(const json::Value& req, json::Value& resp) {
    const int64_t tabId = req["tab_id"].as_int(-1);
    const int64_t paneId = req["pane_id"].as_int(0);
    std::string input = req["input"].as_string();
    const bool appendNewline = req.has("append_newline") ? req["append_newline"].as_bool(true) : true;

    const size_t tabIdx = (tabId >= 0 && static_cast<size_t>(tabId) < m_tabLayouts.size())
                          ? static_cast<size_t>(tabId) : m_active;

    if (tabIdx >= m_tabLayouts.size() || !m_tabLayouts[tabIdx]) {
        resp["success"] = false;
        resp["error"] = "Target tab not found.";
        return;
    }

    std::shared_ptr<TerminalTab> targetTab;
    uint32_t finalPaneId = 0;
    if (paneId <= 0) {
        targetTab = m_tabLayouts[tabIdx]->GetFocusedTab();
        finalPaneId = m_tabLayouts[tabIdx]->GetFocusedPaneId();
    } else {
        for (const auto& t : m_tabLayouts[tabIdx]->GetAllTabs()) {
            if (m_tabLayouts[tabIdx]->FindPaneIdByTab(t.get()) == static_cast<uint32_t>(paneId)) {
                targetTab = t;
                finalPaneId = static_cast<uint32_t>(paneId);
                break;
            }
        }
    }

    if (!targetTab) {
        resp["success"] = false;
        resp["error"] = "Target pane not found.";
        return;
    }

    if (appendNewline && (input.empty() || input.back() != '\n')) {
        input += "\n";
    }

    targetTab->Write(input);

    resp["success"] = true;
    resp["tab_id"] = static_cast<int64_t>(tabIdx);
    resp["pane_id"] = static_cast<int64_t>(finalPaneId);
    resp["bytes_written"] = static_cast<int64_t>(input.size());
}

void MainWindow::HandleMcpRead(const json::Value& req, json::Value& resp) {
    const int64_t tabId = req["tab_id"].as_int(-1);
    const int64_t paneId = req["pane_id"].as_int(0);
    const int64_t tailLines = std::clamp<int64_t>(req["tail_lines"].as_int(25), 1, 500);

    const size_t tabIdx = (tabId >= 0 && static_cast<size_t>(tabId) < m_tabLayouts.size())
                          ? static_cast<size_t>(tabId) : m_active;

    if (tabIdx >= m_tabLayouts.size() || !m_tabLayouts[tabIdx]) {
        resp["success"] = false;
        resp["error"] = "Target tab not found.";
        return;
    }

    std::shared_ptr<TerminalTab> targetTab;
    uint32_t finalPaneId = 0;
    if (paneId <= 0) {
        targetTab = m_tabLayouts[tabIdx]->GetFocusedTab();
        finalPaneId = m_tabLayouts[tabIdx]->GetFocusedPaneId();
    } else {
        for (const auto& t : m_tabLayouts[tabIdx]->GetAllTabs()) {
            if (m_tabLayouts[tabIdx]->FindPaneIdByTab(t.get()) == static_cast<uint32_t>(paneId)) {
                targetTab = t;
                finalPaneId = static_cast<uint32_t>(paneId);
                break;
            }
        }
    }

    if (!targetTab) {
        resp["success"] = false;
        resp["error"] = "Target pane not found.";
        return;
    }

    const std::vector<std::string> lines = targetTab->screen().GetTailLines(static_cast<int>(tailLines));
    const AgentDetectionResult as = AgentDetector::Instance().Detect(lines, targetTab->Title(), targetTab->screen().IsAltBuffer());

    std::ostringstream oss;
    json::Value linesArr = json::Value::Array();
    for (size_t i = 0; i < lines.size(); ++i) {
        linesArr.push_back(lines[i]);
        oss << lines[i];
        if (i + 1 < lines.size()) oss << "\n";
    }

    resp["success"] = true;
    resp["tab_id"] = static_cast<int64_t>(tabIdx);
    resp["pane_id"] = static_cast<int64_t>(finalPaneId);
    resp["title"] = WideToUtf8(targetTab->Title());
    resp["lines"] = linesArr;
    resp["text"] = oss.str();
    resp["alive"] = targetTab->Alive();
    resp["exited"] = targetTab->Exited();
    resp["dead"] = targetTab->Dead();
    resp["exit_code"] = static_cast<int64_t>(targetTab->ExitCode());

    json::Value asObj = json::Value::Object();
    asObj["state"] = AgentDetector::StateToString(as.state);
    asObj["kind"] = AgentDetector::KindToString(as.kind);
    asObj["rule_id"] = as.ruleId;
    asObj["matched_pattern"] = as.matchedPattern;
    asObj["detail"] = as.detail;
    resp["agent_status"] = asObj;
}

void MainWindow::HandleMcpFocus(const json::Value& req, json::Value& resp) {
    const int64_t tabId = req["tab_id"].as_int(-1);
    const int64_t paneId = req["pane_id"].as_int(0);

    if (tabId >= 0 && static_cast<size_t>(tabId) < m_tabLayouts.size()) {
        SelectTab(static_cast<size_t>(tabId));
    }
    if (paneId > 0 && m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        m_tabLayouts[m_active]->SetFocusedPane(static_cast<uint32_t>(paneId));
        m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
    }

    SetView(View::Terminal);
    m_dirty = true;

    if (InQuake()) {
        if (m_quakeState != QuakeState::Visible) QuakeShow();
        ForceForeground();
    } else {
        RestoreFromTray();
        ForceForeground();
    }

    resp["success"] = true;
    resp["tab_id"] = static_cast<int64_t>(m_active);
    resp["pane_id"] = static_cast<int64_t>(m_tabLayouts.empty() ? 0 : m_tabLayouts[m_active]->GetFocusedPaneId());
}

void MainWindow::ToggleSessionRecording() {
    TerminalTab* t = Active();
    if (!t) return;

    if (t->IsRecording()) {
        std::wstring savedPath;
        t->StopRecording(&savedPath);
        std::wstring name = savedPath;
        size_t slash = name.rfind(L'\\');
        if (slash != std::wstring::npos) name = name.substr(slash + 1);
        Toast(L"⏹️ Kayıt tamamlandı: " + name);
        m_dirty = true;
    } else {
        std::wstring recDir = m_dataDir + L"\\recordings";
        CreateDirectoryW(recDir.c_str(), nullptr);

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t fname[128];
        swprintf_s(fname, L"session_%04d%02d%02d_%02d%02d%02d.ftrec",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring fullPath = recDir + L"\\" + fname;

        if (t->StartRecording(fullPath)) {
            Toast(L"🔴 Kayıt Başlatıldı: " + std::wstring(fname));
            m_dirty = true;
        }
    }
}

void MainWindow::HandleMcpRecord(const json::Value& req, json::Value& resp) {
    const std::string subAction = req.has("sub_action") ? req["sub_action"].as_string() : "toggle";
    TerminalTab* t = Active();
    if (!t) {
        resp["success"] = false;
        resp["error"] = "No active terminal tab.";
        return;
    }

    if (subAction == "start") {
        if (t->IsRecording()) {
            resp["success"] = true;
            resp["message"] = "Session is already recording.";
            resp["recording"] = true;
            if (t->Recorder()) resp["file_path"] = WideToUtf8(t->Recorder()->FilePath());
            return;
        }
        std::wstring recDir = m_dataDir + L"\\recordings";
        CreateDirectoryW(recDir.c_str(), nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t fname[128];
        swprintf_s(fname, L"session_%04d%02d%02d_%02d%02d%02d.ftrec",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring fullPath = recDir + L"\\" + fname;
        if (req.has("file_path")) {
            std::string cp = req["file_path"].as_string();
            if (!cp.empty()) fullPath = Utf8ToWide(cp);
        }
        bool ok = t->StartRecording(fullPath);
        resp["success"] = ok;
        resp["recording"] = ok;
        resp["file_path"] = WideToUtf8(fullPath);
        if (ok) {
            Toast(L"🔴 Kayıt Başlatıldı: " + std::wstring(fname));
            m_dirty = true;
        }
    } else if (subAction == "stop") {
        if (!t->IsRecording()) {
            resp["success"] = true;
            resp["message"] = "Session was not recording.";
            resp["recording"] = false;
            return;
        }
        std::wstring saved;
        bool ok = t->StopRecording(&saved);
        resp["success"] = ok;
        resp["recording"] = false;
        resp["file_path"] = WideToUtf8(saved);
        Toast(L"⏹️ Kayıt kaydedildi");
        m_dirty = true;
    } else if (subAction == "status") {
        resp["success"] = true;
        resp["recording"] = t->IsRecording();
        if (t->IsRecording() && t->Recorder()) {
            resp["file_path"] = WideToUtf8(t->Recorder()->FilePath());
            resp["duration_seconds"] = t->Recorder()->DurationSeconds();
            resp["event_count"] = static_cast<int64_t>(t->Recorder()->EventCount());
            resp["total_bytes"] = static_cast<int64_t>(t->Recorder()->TotalBytes());
        }
    } else {
        ToggleSessionRecording();
        resp["success"] = true;
        resp["recording"] = t->IsRecording();
        if (t->IsRecording() && t->Recorder()) {
            resp["file_path"] = WideToUtf8(t->Recorder()->FilePath());
        }
    }
}

} // namespace ft
