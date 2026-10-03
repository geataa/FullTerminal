#include "model/SnippetModel.h"
#include "core/Utf8.h"
#include "core/I18n.h"

#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ft {

namespace {

std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring GenerateId() {
    uint64_t tick = GetTickCount64();
    static uint32_t seq = 0;
    wchar_t buf[64];
    swprintf_s(buf, L"snp_%llx_%x", tick, ++seq);
    return buf;
}

std::wstring EncodeMultiline(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\\') out += L"\\\\";
        else out += c;
    }
    return out;
}

std::wstring DecodeMultiline(const std::wstring& in) {
    std::wstring out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'\\' && i + 1 < in.size()) {
            if (in[i+1] == L'n') { out += L'\n'; ++i; }
            else if (in[i+1] == L'r') { out += L'\r'; ++i; }
            else if (in[i+1] == L'\\') { out += L'\\'; ++i; }
            else { out += in[i]; }
        } else {
            out += in[i];
        }
    }
    return out;
}

} // namespace

SnippetModel::SnippetModel() {
    InitDefaults();
}

void SnippetModel::InitDefaults() {
    m_snippets = {
        { L"sys_1", TrText(L"System & Distro Info", L"Sistem & Dağıtım Bilgisi"), L"uname -a && uptime && (lsb_release -a 2>/dev/null || cat /etc/os-release)", L"System", TrText(L"Lists kernel, uptime and Linux distro version", L"Çekirdek, uptime ve Linux dağıtım sürümünü listeler"), false },
        { L"sys_2", TrText(L"Memory & Disk Usage", L"Bellek & Disk Tüketimi"), L"free -h && echo \"--- DISK ---\" && df -h -x tmpfs -x devtmpfs", L"System", TrText(L"Shows available RAM, swap and physical disk partitions", L"Kullanılabilir RAM, swap ve fiziksel disk bölümlerini gösterir"), false },
        { L"sys_3", TrText(L"Top Resource Consumers", L"En Çok Kaynak Harcayanlar"), L"ps aux --sort=-%mem | head -n 15", L"System", TrText(L"Lists top 15 processes by memory usage", L"En çok RAM kullanan ilk 15 işlemi listeler"), false },
        
        { L"net_1", TrText(L"Listening Ports & Services", L"Dinlenen Portlar & Servisler"), L"ss -tulpn || netstat -tulpn", L"Network", TrText(L"Lists TCP/UDP listening ports and process PIDs", L"TCP/UDP dinleme portlarını ve süreç PID'lerini listeler"), false },
        { L"net_2", TrText(L"Network Interfaces & IPs", L"Ağ Arayüzleri & IP Adresleri"), L"ip -br a && ip route", L"Network", TrText(L"Shows network adapters and default gateway", L"Tüm ağ bağdaştırıcılarını ve varsayılan ağ geçidini gösterir"), false },
        { L"net_3", TrText(L"Quick DNS & Ping Test", L"Hızlı DNS & Ping Testi"), L"ping -c 4 1.1.1.1 && ping -c 4 google.com", L"Network", TrText(L"Tests internet connectivity and DNS resolution", L"İnternet ve DNS çözümleme hızını test eder"), false },

        { L"doc_1", TrText(L"Running Docker Containers", L"Çalışan Docker Konteynerleri"), L"docker ps --format \"table {{.ID}}\\t{{.Names}}\\t{{.Status}}\\t{{.Ports}}\"", L"Docker", TrText(L"Shows name, status and port mappings of active containers", L"Aktif konteynerlerin ad, durum ve port eşlemelerini gösterir"), false },
        { L"doc_2", TrText(L"Container Resource Stats", L"Konteyner Kaynak İstatistikleri"), L"docker stats --no-stream", L"Docker", TrText(L"Presents real-time CPU, RAM and Network I/O metrics", L"Anlık CPU, RAM, Network I/O tüketimini tablo halinde sunar"), false },
        { L"doc_3", TrText(L"Last 100 Container Logs", L"Son 100 Konteyner Logu"), L"docker logs --tail 100 -f <container_name>", L"Docker", TrText(L"Follows the last 100 log lines of specified container", L"Belirtilen konteynerin son 100 satır logunu canlı takip eder"), false },

        { L"k8s_1", TrText(L"K8s All Pods (Wide)", L"K8s Tüm Pod'lar (Wide)"), L"kubectl get pods -A -o wide", L"K8s", TrText(L"Shows pod status and node distribution across all namespaces", L"Tüm namespace'lerdeki pod'ların durum ve node dağılımını gösterir"), false },
        { L"k8s_2", TrText(L"K8s Node Resource Usage", L"K8s Node Kaynak Tüketimi"), L"kubectl top nodes && echo \"---\" && kubectl top pods -A", L"K8s", TrText(L"Lists cluster node and pod CPU/RAM metrics", L"Cluster node ve pod CPU/RAM metriklerini listeler"), false },
        { L"k8s_yaml_sample", TrText(L"K8s Nginx Pod Manifest", L"K8s Nginx Pod Manifesti"), L"cat << 'EOF' | kubectl apply -f -\napiVersion: v1\nkind: Pod\nmetadata:\n  name: nginx-pod\n  labels:\n    app: nginx\nspec:\n  containers:\n  - name: nginx\n    image: nginx:alpine\n    ports:\n    - containerPort: 80\nEOF", L"K8s", TrText(L"Sample Kubernetes Nginx Pod YAML manifest (Applicable & Exportable)", L"Örnek Kubernetes Nginx Pod YAML manifesti (Apply & Export edilebilir)"), false, true,
          L"apiVersion: v1\nkind: Pod\nmetadata:\n  name: nginx-pod\n  labels:\n    app: nginx\nspec:\n  containers:\n  - name: nginx\n    image: nginx:alpine\n    ports:\n    - containerPort: 80\n" },

        { L"srv_1", TrText(L"Failed Services", L"Hatalı / Başarısız Servisler"), L"systemctl --failed", L"Service", TrText(L"Lists all failed or crashed systemd services", L"Başlatılamamış veya çökmüş tüm systemd servislerini listeler"), false },
        { L"srv_2", TrText(L"Live System Journal", L"Canlı Sistem Günlüğü"), L"journalctl -n 100 -f -o cat", L"Service", TrText(L"Follows the last 100 system-wide log entries live", L"Sistem genelindeki son 100 log girdisini canlı izler"), false },

        { L"git_1", TrText(L"Git Status & Recent Commits", L"Git Durumu & Son Commitler"), L"git status -s && git log -n 5 --oneline --graph", L"Git", TrText(L"Shows working tree status and recent commit tree", L"Çalışma ağacındaki değişiklikleri ve son commit ağacını gösterir"), false },

        { L"sec_1", TrText(L"Recent Logged-in Users", L"Son Oturum Açan Kullanıcılar"), L"last -n 10", L"Security", TrText(L"Shows last 10 successful login sessions", L"Sunucuya başarıyla giriş yapmış son 10 SSH oturumunu gösterir"), false },
        { L"sec_2", TrText(L"Failed SSH Attempts", L"Başarısız SSH Denemeleri"), L"grep \"Failed password\" /var/log/auth.log 2>/dev/null | tail -n 20", L"Security", TrText(L"Lists last 20 failed SSH authentication attempts", L"auth.log içindeki son 20 başarısız şifre denemesini listeler"), false }
    };
}

void SnippetModel::Load(const std::wstring& dataDir) {
    m_dataDir = dataDir;
    InitDefaults();

    std::wstring path = dataDir + L"\\snippets.ini";
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    std::string line;
    Snippet cur;
    bool inSnippet = false;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::wstring wline = Utf8ToWide(line);
        wline = Trim(wline);
        if (wline.empty() || wline[0] == L'#') continue;

        if (wline == L"[snippet]") {
            if (inSnippet && !cur.title.empty() && (!cur.command.empty() || !cur.yamlContent.empty())) {
                cur.isCustom = true;
                m_snippets.push_back(cur);
            }
            cur = Snippet();
            cur.isCustom = true;
            cur.category = L"Custom";
            inSnippet = true;
            continue;
        }

        size_t eq = wline.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Trim(wline.substr(0, eq));
        std::wstring v = Trim(wline.substr(eq + 1));

        if (k == L"id") cur.id = v;
        else if (k == L"title") cur.title = v;
        else if (k == L"cmd") cur.command = DecodeMultiline(v);
        else if (k == L"cat") cur.category = v;
        else if (k == L"desc") cur.description = v;
        else if (k == L"is_yaml") cur.isYaml = (v == L"1" || v == L"true");
        else if (k == L"yaml") cur.yamlContent = DecodeMultiline(v);
    }

    if (inSnippet && !cur.title.empty() && (!cur.command.empty() || !cur.yamlContent.empty())) {
        cur.isCustom = true;
        m_snippets.push_back(cur);
    }
}

void SnippetModel::Save(const std::wstring& dataDir) const {
    std::wstring path = dataDir + L"\\snippets.ini";
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return;

    ofs << "# FullTerminal Snippet Manager\n";
    for (const auto& s : m_snippets) {
        if (!s.isCustom) continue;
        ofs << "\n[snippet]\n";
        ofs << "id=" << WideToUtf8(s.id) << "\n";
        ofs << "title=" << WideToUtf8(s.title) << "\n";
        ofs << "cmd=" << WideToUtf8(EncodeMultiline(s.command)) << "\n";
        ofs << "cat=" << WideToUtf8(s.category.empty() ? L"Custom" : s.category) << "\n";
        ofs << "desc=" << WideToUtf8(s.description) << "\n";
        if (s.isYaml) {
            ofs << "is_yaml=1\n";
            ofs << "yaml=" << WideToUtf8(EncodeMultiline(s.yamlContent)) << "\n";
        }
    }
}

static bool MatchCategory(const std::wstring& itemCat, const std::wstring& selectedCat) {
    if (selectedCat.empty() || selectedCat == L"All" || selectedCat == L"Tümü") return true;
    if (itemCat == selectedCat) return true;
    auto norm = [](const std::wstring& c) -> std::wstring {
        if (c == L"Tümü" || c == L"All") return L"All";
        if (c == L"Sistem" || c == L"System") return L"System";
        if (c == L"Ağ" || c == L"Network") return L"Network";
        if (c == L"Servis" || c == L"Service") return L"Service";
        if (c == L"Güvenlik" || c == L"Security") return L"Security";
        if (c == L"Özel" || c == L"Custom") return L"Custom";
        return c;
    };
    return norm(itemCat) == norm(selectedCat);
}

std::vector<Snippet> SnippetModel::GetFiltered(const std::wstring& category, const std::wstring& search) const {
    std::vector<Snippet> out;
    for (const auto& s : m_snippets) {
        if (!MatchCategory(s.category, category)) continue;
        if (!search.empty()) {
            if (StrStrIW(s.title.c_str(), search.c_str()) == nullptr &&
                StrStrIW(s.command.c_str(), search.c_str()) == nullptr &&
                StrStrIW(s.description.c_str(), search.c_str()) == nullptr &&
                StrStrIW(s.yamlContent.c_str(), search.c_str()) == nullptr) {
                continue;
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<std::wstring> SnippetModel::GetCategories() const {
    return { L"All", L"System", L"Network", L"Docker", L"K8s", L"Service", L"Git", L"Security", L"Custom" };
}

bool SnippetModel::AddSnippet(const std::wstring& title, const std::wstring& command,
                             const std::wstring& category, const std::wstring& description,
                             bool isYaml, const std::wstring& yamlContent) {
    if (title.empty()) return false;
    if (!isYaml && command.empty()) return false;
    if (isYaml && yamlContent.empty() && command.empty()) return false;

    Snippet s;
    s.id = GenerateId();
    s.title = title;
    s.command = command;
    s.category = category.empty() ? (isYaml ? L"K8s" : L"Custom") : category;
    s.description = description;
    s.isCustom = true;
    s.isYaml = isYaml;
    s.yamlContent = yamlContent;

    // Eger YAML girilmisse ve command bossa, varsayilan bir apply komutu olusturalim
    if (s.isYaml && s.command.empty()) {
        s.command = L"cat << 'EOF' | kubectl apply -f -\n" + s.yamlContent + L"\nEOF";
    }

    m_snippets.push_back(s);
    if (!m_dataDir.empty()) Save(m_dataDir);
    return true;
}

bool SnippetModel::UpdateSnippet(const std::wstring& id, const std::wstring& title,
                                const std::wstring& command, const std::wstring& category,
                                const std::wstring& description, bool isYaml,
                                const std::wstring& yamlContent) {
    if (id.empty() || title.empty()) return false;
    auto it = std::find_if(m_snippets.begin(), m_snippets.end(), [&](const Snippet& s) {
        return s.id == id;
    });
    if (it == m_snippets.end()) return false;

    it->title = title;
    it->command = command;
    it->category = category.empty() ? (isYaml ? L"K8s" : L"Custom") : category;
    it->description = description;
    it->isCustom = true;
    it->isYaml = isYaml;
    it->yamlContent = yamlContent;

    if (it->isYaml && it->command.empty() && !it->yamlContent.empty()) {
        it->command = L"cat << 'EOF' | kubectl apply -f -\n" + it->yamlContent + L"\nEOF";
    }

    if (!m_dataDir.empty()) Save(m_dataDir);
    return true;
}

const Snippet* SnippetModel::FindSnippet(const std::wstring& id) const {
    for (const auto& s : m_snippets) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

bool SnippetModel::DeleteSnippet(const std::wstring& id) {
    auto it = std::find_if(m_snippets.begin(), m_snippets.end(), [&](const Snippet& s) {
        return s.id == id && s.isCustom;
    });
    if (it != m_snippets.end()) {
        m_snippets.erase(it);
        if (!m_dataDir.empty()) Save(m_dataDir);
        return true;
    }
    return false;
}

bool SnippetModel::ExportYaml(const Snippet& s, const std::wstring& outDir, std::wstring& outFilePath) const {
    if (s.yamlContent.empty()) return false;
    CreateDirectoryW(outDir.c_str(), NULL);
    std::wstring cleanTitle = s.title;
    for (auto& c : cleanTitle) {
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L' ') {
            c = L'_';
        }
    }
    if (cleanTitle.empty()) cleanTitle = L"manifest";
    outFilePath = outDir + L"\\" + cleanTitle + L".yaml";
    
    std::string utf8 = WideToUtf8(s.yamlContent);
    std::ofstream ofs(outFilePath, std::ios::trunc | std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(utf8.data(), utf8.size());
    return true;
}

} // namespace ft
