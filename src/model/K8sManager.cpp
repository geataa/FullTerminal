#include "model/K8sManager.h"
#include "core/ShellProfiles.h"
#include "core/Utf8.h"
#include "transport/agentless/AgentlessExecutor.h"

#include <windows.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <initializer_list>
#include <iterator>
#include <string_view>

namespace ft {
namespace {

// Uygulamanin kendi Export ciktisi da k8s klasorune yazilir; kaynak diye okunursa
// kumede olmayan "fullterminal-workloads" gibi kartlar uretir, taramada atlanir.
constexpr wchar_t kExportFileName[] = L"fullterminal-export.yaml";

// DNS-1123: [a-z0-9]([-a-z0-9]*[a-z0-9])?
bool IsLabelChars(std::string_view s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (c == '-' && i != 0 && i + 1 != s.size()) continue;
        return false;
    }
    return true;
}

// Ad alani / konteyner adi: DNS-1123 label, en fazla 63
bool IsDnsLabel(std::string_view s) {
    return s.size() <= 63 && IsLabelChars(s);
}

// Pod/Deployment adi: noktayla ayrilmis label'lar, en fazla 253
bool IsDnsSubdomain(std::string_view s) {
    if (s.empty() || s.size() > 253) return false;
    size_t start = 0;
    for (;;) {
        const size_t dot = s.find('.', start);
        const size_t len = (dot == std::string_view::npos) ? std::string_view::npos : dot - start;
        if (!IsLabelChars(s.substr(start, len))) return false;
        if (dot == std::string_view::npos) return true;
        start = dot + 1;
    }
}

// kubectl exec'in TYPE/NAME olarak baglanabildigi turler; digerleri (Service,
// ConfigMap, Secret...) terminal hedefi degildir.
const char* KindToken(const std::string& kind) {
    if (kind == "Pod")         return "pod";
    if (kind == "Deployment")  return "deploy";
    if (kind == "StatefulSet") return "sts";
    if (kind == "DaemonSet")   return "ds";
    if (kind == "ReplicaSet")  return "rs";
    if (kind == "Job")         return "job";
    return nullptr;
}

std::string QuoteArgU8(const std::string& s) {
    return WideToUtf8(QuoteArg(Utf8ToWide(s)));
}

std::wstring GetEnv(const wchar_t* name) {
    const DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) return {};
    std::wstring v(need, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, v.data(), need);
    if (got == 0 || got >= need) return {};
    v.resize(got);
    return v;
}

bool HasYamlExt(const std::wstring& fname) {
    const size_t dot = fname.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = fname.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext == L".yaml" || ext == L".yml";
}

std::wstring FullPathOf(const std::wstring& p) {
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (n == 0) return p;
    std::wstring out(n, L'\0');
    n = GetFullPathNameW(p.c_str(), n, out.data(), nullptr);
    if (n == 0 || n >= out.size()) return p;
    out.resize(n);
    return out;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    // Manifest/kubeconfig icin fazlasiyla yeter; yanlis secilen dev dosya UI'yi kilitlemesin
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 32ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    const bool ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok) { out.clear(); return false; }
    out.resize(got);
    return true;
}

bool WriteWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)data.size();
}

// BOM'u atar, UTF-16 (PowerShell 5.1 ">" yonlendirmesinin varsayilani) metni UTF-8'e cevirir.
std::string DecodeYamlText(const std::string& raw) {
    const auto b = [&](size_t i) { return (unsigned char)raw[i]; };
    if (raw.size() >= 3 && b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) return raw.substr(3);
    bool le = raw.size() >= 2 && b(0) == 0xFF && b(1) == 0xFE;
    bool be = raw.size() >= 2 && b(0) == 0xFE && b(1) == 0xFF;
    size_t start = 2;
    // BOM'suz UTF-16: YAML ASCII ile baslar, ciftin bir yarisi 0 olur
    if (!le && !be && raw.size() >= 2 && b(0) != 0 && b(1) == 0) { le = true; start = 0; }
    if (!le && !be && raw.size() >= 2 && b(0) == 0 && b(1) != 0) { be = true; start = 0; }
    if (!le && !be) return raw;
    std::wstring w;
    w.reserve((raw.size() - start) / 2);
    for (size_t i = start; i + 1 < raw.size(); i += 2) {
        w.push_back(le ? (wchar_t)(b(i) | (b(i + 1) << 8)) : (wchar_t)((b(i) << 8) | b(i + 1)));
    }
    return WideToUtf8(w);
}

// C:\a\b.yaml -> /mnt/c/a/b.yaml (WSL varsayilan automount koku). UNC yollar cevrilemez.
std::wstring ToWslPath(std::wstring p) {
    if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);
    if (p.size() < 3 || p[1] != L':' || (p[2] != L'\\' && p[2] != L'/')) return {};
    const wchar_t d = (wchar_t)towlower(p[0]);
    if (d < L'a' || d > L'z') return {};
    std::wstring out = L"/mnt/";
    out.push_back(d);
    for (size_t i = 2; i < p.size(); ++i) out.push_back(p[i] == L'\\' ? L'/' : p[i]);
    return out;
}

// ---- kucuk, girintiye duyarli YAML okuyucu ---------------------------------

struct ParsedYaml {
    std::vector<K8sResource> resources;
    bool isKubeconfig = false;
    std::string currentContext;
    std::vector<KubeContext> contexts;
};

std::string_view TrimSv(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// Skaler deger: tirnaklari cozer, satir sonu yorumunu (" #") atar.
std::string ScalarValue(std::string_view v) {
    v = TrimSv(v);
    if (v.empty() || v[0] == '#') return {};
    std::string out;
    if (v[0] == '"') {
        for (size_t i = 1; i < v.size(); ++i) {
            const char c = v[i];
            if (c == '"') break;
            if (c == '\\' && i + 1 < v.size()) {
                const char e = v[++i];
                out.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                continue;
            }
            out.push_back(c);
        }
        return out;
    }
    if (v[0] == '\'') {
        for (size_t i = 1; i < v.size(); ++i) {
            if (v[i] == '\'') {
                if (i + 1 < v.size() && v[i + 1] == '\'') { out.push_back('\''); ++i; continue; }
                break;
            }
            out.push_back(v[i]);
        }
        return out;
    }
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] == '#' && (v[i - 1] == ' ' || v[i - 1] == '\t')) { v = v.substr(0, i); break; }
    }
    return std::string(TrimSv(v));
}

// "anahtar: deger" ayiran iki nokta (ardindan bosluk/satir sonu). Yoksa npos.
size_t FindKeyColon(std::string_view s) {
    size_t i = 0;
    if (!s.empty() && (s[0] == '"' || s[0] == '\'')) {
        const char q = s[0];
        for (i = 1; i < s.size(); ++i) {
            if (q == '"' && s[i] == '\\') { ++i; continue; }
            if (s[i] == q) {
                if (q == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
                break;
            }
        }
        ++i;
    } else if (!s.empty() && (s[0] == '{' || s[0] == '[')) {
        return std::string_view::npos;   // akis stili, bizim alanlarimizi tasimaz
    }
    for (; i < s.size(); ++i) {
        if (s[i] == '#' && i > 0 && (s[i - 1] == ' ' || s[i - 1] == '\t')) break;
        if (s[i] == ':' && (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t')) return i;
    }
    return std::string_view::npos;
}

// Yalnizca ihtiyac duyulan yollari okur: kok kind, metadata.name/namespace,
// (template.)spec.containers[], kind: List -> items[], kubeconfig contexts[].
// Ic ice anahtarlar (ownerReferences[].name, scaleTargetRef.kind...) kokun yerine gecmez.
void ParseYamlText(const std::string& content, const std::string& sourceName, ParsedYaml& out) {
    struct Frame { int indent; std::string key; };   // key "[]": liste elemani
    struct Doc {
        K8sResource root;
        std::vector<K8sResource> items;
        std::string apiVersion;
        std::string currentContext;
        std::vector<KubeContext> contexts;
        bool anyKey = false;
        int kubeSections = 0;        // bit 1: clusters, 2: contexts, 4: users
    };

    std::vector<Frame> stack;
    std::vector<std::string> path;
    Doc doc;
    int docIndex = 0;
    int blockIndent = -1;

    auto commitRes = [&](K8sResource& r) {
        if (r.kind.empty() || r.name.empty()) return;
        // Ad ve ad alani kubectl komut satirina girer; DNS-1123 disindakiler hic alinmaz
        // (orn. "name: web --kubeconfig=..." arguman enjeksiyonu).
        if (!IsDnsSubdomain(r.name) || !IsDnsLabel(r.ns)) return;
        if (!IsDnsLabel(r.containerName)) r.containerName = r.name;
        r.sourceFile = sourceName;
        out.resources.push_back(std::move(r));
    };

    auto commitDoc = [&]() {
        if (doc.anyKey) {
            commitRes(doc.root);
            for (auto& it : doc.items) commitRes(it);
            // kubectl bir kubeconfig dosyasinin yalnizca ilk belgesini okur; "kind: Config"
            // yazmayan elle hazirlanmis kubeconfig'ler clusters + contexts ile taninir
            const bool kubeKind = (doc.root.kind == "Config" && doc.kubeSections != 0) ||
                                  (doc.root.kind.empty() && (doc.kubeSections & 3) == 3);
            if (docIndex == 0 && kubeKind && (doc.apiVersion.empty() || doc.apiVersion == "v1")) {
                out.isKubeconfig = true;
                out.currentContext = doc.currentContext;
                out.contexts = std::move(doc.contexts);
            }
            ++docIndex;
        }
        doc = Doc{};
        stack.clear();
        blockIndent = -1;
    };

    auto onSeqItem = [&]() {
        if (stack.size() != 1) return;
        if (stack[0].key == "items") doc.items.emplace_back();
        else if (stack[0].key == "contexts") doc.contexts.emplace_back();
    };

    auto onKey = [&](const std::string& val) {
        doc.anyKey = true;
        const size_t n = path.size();
        K8sResource* res = &doc.root;
        size_t from = 0;
        if (n >= 2 && path[0] == "items" && path[1] == "[]") {
            if (doc.items.empty()) return;
            res = &doc.items.back();
            from = 2;
        }
        auto is = [&](std::initializer_list<const char*> want) {
            if (n - from != want.size()) return false;
            size_t i = from;
            for (const char* w : want) {
                if (path[i++] != w) return false;
            }
            return true;
        };

        if (is({ "kind" })) res->kind = val;
        else if (is({ "metadata", "name" })) res->name = val;
        else if (is({ "metadata", "namespace" })) { if (!val.empty()) res->ns = val; }
        else if (is({ "spec", "containers", "[]", "name" }) ||
                 is({ "spec", "template", "spec", "containers", "[]", "name" })) {
            if (res->containerName.empty()) res->containerName = val;
        } else if (is({ "spec", "containers", "[]", "image" }) ||
                   is({ "spec", "template", "spec", "containers", "[]", "image" })) {
            if (res->image.empty()) res->image = val;
        } else if (is({ "spec", "containers", "[]", "ports", "[]", "containerPort" }) ||
                   is({ "spec", "template", "spec", "containers", "[]", "ports", "[]", "containerPort" })) {
            if (res->port == 0) res->port = std::atoi(val.c_str());
        }

        if (from != 0) return;
        if (is({ "apiVersion" })) doc.apiVersion = val;
        else if (is({ "clusters" })) doc.kubeSections |= 1;
        else if (is({ "contexts" })) doc.kubeSections |= 2;
        else if (is({ "users" })) doc.kubeSections |= 4;
        else if (is({ "current-context" })) doc.currentContext = val;
        else if (!doc.contexts.empty()) {
            if (is({ "contexts", "[]", "name" })) doc.contexts.back().name = val;
            else if (is({ "contexts", "[]", "context", "cluster" })) doc.contexts.back().cluster = val;
            else if (is({ "contexts", "[]", "context", "namespace" })) doc.contexts.back().ns = val;
        }
    };

    std::istringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t ind = 0;
        while (ind < line.size() && line[ind] == ' ') ++ind;
        std::string_view rest = std::string_view(line).substr(ind);
        const bool blank = TrimSv(rest).empty();

        if (blockIndent >= 0) {
            // Blok skaler (| veya >) govdesi duz metindir; icindeki "name:" satirlari anahtar degil
            if (blank || (int)ind > blockIndent) continue;
            blockIndent = -1;
        }
        if (blank) continue;

        if (ind == 0) {
            const std::string_view head = rest.substr(0, 3);
            if ((head == "---" || head == "...") &&
                (rest.size() == 3 || rest[3] == ' ' || rest[3] == '\t')) {
                commitDoc();
                continue;
            }
            if (rest[0] == '%') continue;   // %YAML / %TAG direktifi
        }
        if (rest[0] == '#') continue;

        int cur = (int)ind;
        int dashCol = -1;
        // "- " liste elemanlari; ayni girintideki anahtar ("items:\n- ...") ebeveyn kalir
        while (!rest.empty() && rest[0] == '-' && (rest.size() == 1 || rest[1] == ' ' || rest[1] == '\t')) {
            dashCol = cur;
            while (!stack.empty() && (stack.back().indent > cur ||
                                      (stack.back().indent == cur && stack.back().key == "[]"))) {
                stack.pop_back();
            }
            onSeqItem();
            stack.push_back({ cur, "[]" });
            size_t k = 1;
            while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) ++k;
            cur += (int)k;
            rest.remove_prefix(k);
        }
        if (rest.empty() || rest[0] == '#') continue;
        // "- |" blok skaler liste elemani: govdesi anahtar diye okunmasin
        if (dashCol >= 0 && (rest[0] == '|' || rest[0] == '>')) {
            blockIndent = dashCol;
            continue;
        }

        const size_t colon = FindKeyColon(rest);
        if (colon == std::string_view::npos) continue;   // duz skaler liste elemani vb.
        std::string key = ScalarValue(rest.substr(0, colon));
        const std::string_view rawVal = TrimSv(rest.substr(colon + 1));

        while (!stack.empty() && stack.back().indent >= cur) stack.pop_back();
        stack.push_back({ cur, std::move(key) });
        path.clear();
        for (const auto& f : stack) path.push_back(f.key);

        const bool block = !rawVal.empty() && (rawVal[0] == '|' || rawVal[0] == '>');
        onKey(block ? std::string() : ScalarValue(rawVal));
        if (block) blockIndent = cur;
    }
    commitDoc();
}

} // namespace

K8sManager& K8sManager::Instance() {
    static K8sManager s_inst;
    return s_inst;
}

bool K8sManager::HasKubectl() {
    // SearchPathW(nullptr, ...) calisma dizinine de bakar; yalniz PATH.
    return !AgentlessExecutor::FindExecutableOnPath(L"kubectl.exe").empty();
}

void K8sManager::Init(const std::wstring& dataDir) {
    m_k8sDir = dataDir + L"\\k8s";
    CreateDirectoryW(m_k8sDir.c_str(), nullptr);
    Rescan(dataDir);
}

void K8sManager::Rescan(const std::wstring& dataDir) {
    m_k8sDir = dataDir + L"\\k8s";
    RescanDir();
}

void K8sManager::RescanDir() {
    m_resources.clear();
    m_loadedFiles.clear();
    m_kubeconfigs.clear();
    m_activeKubeconfig = 0;
    if (m_k8sDir.empty()) return;

    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd{};
    std::wstring searchPattern = m_k8sDir + L"\\*.*";
    HANDLE h = FindFirstFileW(searchPattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fname = fd.cFileName;
        if (!HasYamlExt(fname) || _wcsicmp(fname.c_str(), kExportFileName) == 0) continue;
        names.push_back(std::move(fname));
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    // KUBECONFIG sirasi (current-context'i ilk dosya belirler) dosya sisteminden bagimsiz olsun
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const auto& fname : names) {
        const std::wstring fullPath = m_k8sDir + L"\\" + fname;
        m_loadedFiles.push_back(fullPath);
        std::string raw;
        if (ReadWholeFile(fullPath, raw)) {
            ParseYamlContent(DecodeYamlText(raw), WideToUtf8(fname), fullPath);
        }
    }
}

void K8sManager::ParseYamlContent(const std::string& content, const std::string& sourceName,
                                  const std::wstring& fullPath) {
    ParsedYaml py;
    ParseYamlText(content, sourceName, py);
    for (auto& r : py.resources) {
        // Ayni tur/ad alani/ad baska bir dosyada tekrar gelirse ilki kalir: dugum kimlikleri tekil
        const bool dup = std::any_of(m_resources.begin(), m_resources.end(), [&](const K8sResource& e) {
            return e.kind == r.kind && e.ns == r.ns && e.name == r.name;
        });
        if (!dup) m_resources.push_back(std::move(r));
    }
    if (py.isKubeconfig) {
        // KUBECONFIG terminalin calisma dizininden bagimsiz olsun: her zaman mutlak yol
        m_kubeconfigs.push_back({ FullPathOf(fullPath), std::move(py.currentContext), std::move(py.contexts) });
    }
}

bool K8sManager::ImportYamlFile(const std::wstring& filePath, std::wstring* err) {
    if (m_k8sDir.empty()) {
        if (err) *err = L"K8s klasoru hazir degil.";
        return false;
    }
    std::string raw;
    if (!ReadWholeFile(filePath, raw)) {
        if (err) *err = L"Dosya acilamadi: " + filePath;
        return false;
    }
    const std::string content = DecodeYamlText(raw);

    size_t slash = filePath.find_last_of(L"\\/");
    std::wstring fname = (slash != std::wstring::npos) ? filePath.substr(slash + 1) : filePath;
    // Uzantisiz kubeconfig ("config") taramada da bulunsun
    if (!HasYamlExt(fname)) fname += L".yaml";
    // Export ciktisiyla ayni ad taramada atlanir; kullanicinin dosyasi kaybolmasin
    if (_wcsicmp(fname.c_str(), kExportFileName) == 0) fname = L"imported-" + fname;

    ParsedYaml probe;
    ParseYamlText(content, WideToUtf8(fname), probe);
    if (probe.resources.empty() && !probe.isKubeconfig) {
        if (err) *err = L"Dosyada gecerli Kubernetes kaynagi veya kubeconfig bulunamadi: " + fname;
        return false;
    }

    std::wstring dest = m_k8sDir + L"\\" + fname;
    bool write = true;
    if (_wcsicmp(FullPathOf(filePath).c_str(), FullPathOf(dest).c_str()) == 0) {
        write = (raw != content);   // zaten klasorde; yalnizca kodlama UTF-8'e cevrilir
    } else {
        // Ayni adli ama farkli icerikli dosyanin (baska projenin deployment.yaml'i, baska
        // kumenin "config"i) uzerine yazma: ad-1.yaml, ad-2.yaml... Icerigi ayni olan
        // bulunursa zaten ice aktarilmistir, kopya birakilmaz.
        const size_t dot = fname.rfind(L'.');
        const std::wstring stem = fname.substr(0, dot);
        const std::wstring ext = fname.substr(dot);
        for (int n = 1; GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES; ++n) {
            std::string existing;
            if (ReadWholeFile(dest, existing) && DecodeYamlText(existing) == content) {
                write = false;
                break;
            }
            if (n > 999) {
                if (err) *err = L"K8s klasorunde bos dosya adi bulunamadi: " + fname;
                return false;
            }
            dest = m_k8sDir + L"\\" + stem + L"-" + std::to_wstring(n) + ext;
        }
    }
    if (write) {
        if (!WriteWholeFile(dest, content)) {
            if (err) *err = L"Dosya k8s klasorune kopyalanamadi: " + dest;
            return false;
        }
    }

    // Bellege eklemek yerine klasoru bastan oku: yeniden ice aktarma cift kayit uretmesin
    RescanDir();
    return true;
}

bool K8sManager::ExportInventory(const Inventory& inv, const std::wstring& outputPath, std::wstring* err) {
    return ExportInventory(inv, inv.BuildNodeTree(), outputPath, err);
}

bool K8sManager::ExportInventory(const Inventory& inv, const std::vector<ConnectionNode>& nodes,
                                 const std::wstring& outputPath, std::wstring* err) {
    std::ofstream ofs(outputPath);
    if (!ofs.is_open()) {
        if (err) *err = L"Disa aktarma dosyasi olusturulamadi: " + outputPath;
        return false;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    char timeBuf[64]{};
    ctime_s(timeBuf, sizeof(timeBuf), &now);

    ofs << "# ========================================================\n"
        << "#  FullTerminal Kubernetes Manifest Export\n"
        << "#  Tarih: " << timeBuf
        << "# ========================================================\n\n";

    // 1. ConfigMap (Hostlar ve WSL Altyapisi)
    ofs << "apiVersion: v1\n"
        << "kind: ConfigMap\n"
        << "metadata:\n"
        << "  name: fullterminal-infrastructure-config\n"
        << "  namespace: default\n"
        << "  labels:\n"
        << "    app.kubernetes.io/managed-by: fullterminal\n"
        << "data:\n"
        << "  hosts.ini: |\n";

    for (const auto& h : inv.hosts()) {
        ofs << "    [host]\n"
            << "    label=" << WideToUtf8(h.label) << "\n"
            << "    address=" << WideToUtf8(h.address) << "\n"
            << "    port=" << h.port << "\n"
            << "    username=" << WideToUtf8(h.username) << "\n\n";
    }

    ofs << "---\n";

    // 2. Deployment (Docker konteynerleri ve calisan servisler icin K8s is yuku)
    ofs << "apiVersion: apps/v1\n"
        << "kind: Deployment\n"
        << "metadata:\n"
        << "  name: fullterminal-workloads\n"
        << "  namespace: default\n"
        << "  labels:\n"
        << "    app: fullterminal-workload\n"
        << "spec:\n"
        << "  replicas: 1\n"
        << "  selector:\n"
        << "    matchLabels:\n"
        << "      app: fullterminal-workload\n"
        << "  template:\n"
        << "    metadata:\n"
        << "      labels:\n"
        << "        app: fullterminal-workload\n"
        << "    spec:\n"
        << "      containers:\n";

    int cntCount = 0;
    for (const auto& nd : nodes) {
        if (nd.type == NodeType::DockerContainer) {
            std::string img = "nginx:alpine";
            if (nd.notes.find("image: ") != std::string::npos) {
                img = nd.notes.substr(nd.notes.find("image: ") + 7);
            }
            ofs << "        - name: " << nd.name << "\n"
                << "          image: " << img << "\n"
                << "          ports:\n"
                << "            - containerPort: 80\n";
            cntCount++;
        }
    }

    if (cntCount == 0) {
        ofs << "        - name: default-dev-container\n"
            << "          image: alpine:latest\n"
            << "          command: [\"/bin/sh\", \"-c\", \"sleep 86400\"]\n";
    }

    ofs << "---\n";

    // 3. Service
    ofs << "apiVersion: v1\n"
        << "kind: Service\n"
        << "metadata:\n"
        << "  name: fullterminal-gateway-svc\n"
        << "  namespace: default\n"
        << "spec:\n"
        << "  type: ClusterIP\n"
        << "  selector:\n"
        << "    app: fullterminal-workload\n"
        << "  ports:\n"
        << "    - name: http\n"
        << "      port: 80\n"
        << "      targetPort: 80\n";

    ofs.close();
    return true;
}

bool K8sManager::IsExecKind(const std::string& kind) {
    return KindToken(kind) != nullptr;
}

std::string K8sManager::ExecCommand(const K8sResource& r) {
    const char* token = KindToken(r.kind);
    if (!token) return {};
    // Parse'ta dogrulandi; komut satiri buradan da uretildigi icin yeniden kontrol et
    if (!IsDnsSubdomain(r.name) || !IsDnsLabel(r.ns)) return {};
    // Pod eski kubectl surumleriyle de calissin diye yalin adla, digerleri TYPE/NAME ile
    const std::string ref = (r.kind == "Pod") ? r.name : (std::string(token) + "/" + r.name);
    return "kubectl.exe exec -it -n " + QuoteArgU8(r.ns) + " " + QuoteArgU8(ref) + " -- sh";
}

std::vector<ConnectionNode> K8sManager::BuildNodes() const {
    std::vector<ConnectionNode> out;
    const bool kubectl = HasKubectl();
    for (const auto& r : m_resources) {
        // Service/ConfigMap vb. terminal dugumu olmaz, Manifests() listesinde kalir
        std::string cmd = ExecCommand(r);
        if (cmd.empty()) continue;
        // Kimlik: Pod icin eski bicim k8s:<ns>/<ad> (MCP ft_exec ve kayitli kimlikler bozulmasin),
        // is yukleri icin k8s:<ns>/<tur>/<ad>; ayni adli Deployment ve Pod cakismasin
        const std::string ref = std::string(KindToken(r.kind)) + "/" + r.name;
        ConnectionNode nd;
        nd.id = "k8s:" + r.ns + "/" + (r.kind == "Pod" ? r.name : ref);
        nd.parentId = "k8s";
        nd.name = r.name + (r.containerName != r.name && !r.containerName.empty() ? (" (" + r.containerName + ")") : "");
        nd.type = NodeType::K8sPod;
        nd.path = "/k8s/" + r.ns + "/" + ref;
        nd.status = kubectl ? "online" : "yaml-manifest";
        nd.target = std::move(cmd);
        nd.notes = r.kind + (!r.image.empty() ? (" | " + r.image) : "") + " [" + r.sourceFile + "]";
        out.push_back(std::move(nd));
    }
    return out;
}

std::vector<std::pair<std::wstring, std::wstring>> K8sManager::TerminalEnv(bool wsl) const {
    std::vector<std::pair<std::wstring, std::wstring>> env;
    if (m_kubeconfigs.empty()) return env;

    if (!wsl) {
        std::wstring list;
        for (const auto& k : m_kubeconfigs) {
            if (!list.empty()) list += L';';
            list += k.path;
        }
        // Kullanicinin kendi kubeconfig'i gizlenmesin diye sona eklenir; kubectl
        // birlestirirken current-context'i ilk dosyadan (eklenenlerden) alir.
        std::wstring existing = GetEnv(L"KUBECONFIG");
        if (existing.empty()) {
            const std::wstring def = UserHomeDir() + L"\\.kube\\config";
            const DWORD a = GetFileAttributesW(def.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) existing = def;
        }
        if (!existing.empty()) list += L';' + existing;
        env.emplace_back(L"KUBECONFIG", std::move(list));
        return env;
    }

    std::wstring list;
    for (const auto& k : m_kubeconfigs) {
        const std::wstring p = ToWslPath(k.path);
        if (p.empty()) continue;
        if (!list.empty()) list += L':';
        list += p;
    }
    if (list.empty()) return env;
    env.emplace_back(L"KUBECONFIG", std::move(list));

    // WSLENV: mevcut girdiler korunur; deger zaten Linux yolu oldugundan KUBECONFIG
    // bayraksiz (/p, /l olmadan) iletilir, eski bayrakli girdisi atilir.
    const std::wstring cur = GetEnv(L"WSLENV");
    std::wstring wslenv;
    size_t start = 0;
    while (start < cur.size()) {
        size_t colon = cur.find(L':', start);
        if (colon == std::wstring::npos) colon = cur.size();
        const std::wstring tok = cur.substr(start, colon - start);
        const std::wstring name = tok.substr(0, tok.find(L'/'));
        if (!tok.empty() && _wcsicmp(name.c_str(), L"KUBECONFIG") != 0) {
            if (!wslenv.empty()) wslenv += L':';
            wslenv += tok;
        }
        start = colon + 1;
    }
    if (!wslenv.empty()) wslenv += L':';
    wslenv += L"KUBECONFIG";
    env.emplace_back(L"WSLENV", std::move(wslenv));
    return env;
}

bool K8sManager::SetActiveKubeconfig(size_t index) {
    if (index >= m_kubeconfigs.size()) return false;
    if (index > 0) {
        std::rotate(m_kubeconfigs.begin(), m_kubeconfigs.begin() + index, m_kubeconfigs.begin() + index + 1);
    }
    m_activeKubeconfig = 0;
    return true;
}

bool K8sManager::SetActiveContext(size_t configIndex, const std::string& contextName) {
    if (configIndex >= m_kubeconfigs.size()) return false;
    auto& cfg = m_kubeconfigs[configIndex];
    cfg.currentContext = contextName;

    // Dosyadaki current-context alanini guncelle
    std::string content;
    if (ReadWholeFile(cfg.path, content)) {
        size_t pos = content.find("current-context:");
        if (pos != std::string::npos) {
            size_t nl = content.find('\n', pos);
            if (nl == std::string::npos) nl = content.size();
            std::string before = content.substr(0, pos);
            std::string after = content.substr(nl);
            std::string newContent = before + "current-context: " + contextName + after;
            WriteWholeFile(cfg.path, newContent);
        } else {
            std::string newContent = "current-context: " + contextName + "\n" + content;
            WriteWholeFile(cfg.path, newContent);
        }
    }

    return SetActiveKubeconfig(configIndex);
}

std::vector<std::wstring> K8sManager::KubeconfigFiles() const {
    std::vector<std::wstring> files;
    files.reserve(m_kubeconfigs.size());
    for (const auto& k : m_kubeconfigs) files.push_back(k.path);
    return files;
}

std::wstring K8sManager::KubeContextSummary() const {
    if (m_kubeconfigs.empty()) return {};
    // kubectl birlestirme kurali: current-context'i tanimlayan ilk dosya kazanir
    std::string cur;
    for (const auto& k : m_kubeconfigs) {
        if (!k.currentContext.empty()) { cur = k.currentContext; break; }
    }
    if (cur.empty()) return L"current-context tanimli degil";
    for (const auto& k : m_kubeconfigs) {
        for (const auto& c : k.contexts) {
            if (c.name != cur) continue;
            const std::string cluster = c.cluster.empty() ? c.name : c.cluster;
            return Utf8ToWide(cluster + " / namespace: " + (c.ns.empty() ? std::string("default") : c.ns));
        }
    }
    return Utf8ToWide(cur + " (context bulunamadi)");
}

} // namespace ft
