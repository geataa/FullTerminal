#pragma once
//
// Kubernetes YAML Yoneticisi ve Manifest Motoru.
// YAML dosyalarini ayristirir, Pod'lari envanter agacina baglar
// ve FullTerminal altyapisini Kubernetes manifesti olarak disa aktarir.
// Eklenen kubeconfig'leri (kind: Config) yeni terminallere KUBECONFIG olarak verir.
//
// Yalnizca UI thread'inden kullanilir (kilit yok).
//
#include "model/Inventory.h"
#include <string>
#include <utility>
#include <vector>

namespace ft {

struct K8sResource {
    std::string kind;           // Pod, Deployment, Service, ConfigMap
    std::string name;           // DNS-1123 subdomain olarak dogrulanmis
    std::string ns = "default"; // DNS-1123 label olarak dogrulanmis
    std::string image;
    std::string containerName;
    int port = 0;
    std::string sourceFile;
};

// kubeconfig icindeki tek bir contexts[] girdisi
struct KubeContext {
    std::string name;
    std::string cluster;
    std::string ns;
};

// <dataDir>\k8s altinda bulunan bir kubeconfig dosyasi
struct KubeconfigFile {
    std::wstring path;                  // tam yol
    std::string currentContext;
    std::vector<KubeContext> contexts;
};

class K8sManager {
public:
    static K8sManager& Instance();

    void Init(const std::wstring& dataDir);
    // Klasoru bastan okur: eklenen/silinen dosyalar yansir.
    void Rescan(const std::wstring& dataDir);

    // Dosyayi <dataDir>\k8s altina kopyalar (ayni adli farkli dosya varsa ad-N.yaml) ve
    // klasoru yeniden tarar.
    // Icinde gecerli kaynak ya da kubeconfig yoksa false doner.
    bool ImportYamlFile(const std::wstring& filePath, std::wstring* err = nullptr);
    // Docker dugumleri icin inv.BuildNodeTree() cagirir (docker.exe calistirir, yavas).
    bool ExportInventory(const Inventory& inv, const std::wstring& outputPath, std::wstring* err = nullptr);
    // UI'daki hazir dugum listesiyle: surec baslatmaz.
    bool ExportInventory(const Inventory& inv, const std::vector<ConnectionNode>& nodes,
                         const std::wstring& outputPath, std::wstring* err = nullptr);

    // Yalnizca kubectl exec ile baglanilabilen kaynaklar icin dugum uretir.
    std::vector<ConnectionNode> BuildNodes() const;
    const std::vector<K8sResource>& Resources() const { return m_resources; }
    const std::vector<K8sResource>& Manifests() const { return m_resources; }
    // Terminal acilabilen kaynaklar (Pod, Deployment, StatefulSet, DaemonSet, ReplicaSet, Job)
    std::vector<K8sResource> Pods() const {
        std::vector<K8sResource> pods;
        for (const auto& r : m_resources) {
            if (IsExecKind(r.kind)) pods.push_back(r);
        }
        return pods;
    }
    size_t FileCount() const { return m_loadedFiles.size(); }
    const std::vector<std::wstring>& LoadedFiles() const { return m_loadedFiles; }

    // kubectl exec ile baglanilabilen tur mu
    static bool IsExecKind(const std::string& kind);
    // Kaynak icin guvenli, tirnakli "kubectl.exe exec -it -n <ns> <tur/ad> -- sh" komutu.
    // Tur exec'e uygun degilse ya da ad/ad alani DNS-1123 degilse bos doner.
    static std::string ExecCommand(const K8sResource& r);

    // --- kubeconfig / otomatik ortam (FR-K8S-010) ---------------------------
    // Yeni yerel terminal icin ortam degiskenleri. Kubeconfig yoksa bos.
    //  wsl=false: KUBECONFIG=<';' ile birlesik Windows yollari>
    //  wsl=true : KUBECONFIG=</mnt/c/... ':' ile birlesik> ve onu ileten WSLENV
    std::vector<std::pair<std::wstring, std::wstring>> TerminalEnv(bool wsl) const;
    size_t KubeconfigCount() const { return m_kubeconfigs.size(); }
    const std::vector<KubeconfigFile>& Kubeconfigs() const { return m_kubeconfigs; }
    size_t ActiveKubeconfigIndex() const { return m_activeKubeconfig; }
    bool SetActiveKubeconfig(size_t index);
    bool SetActiveContext(size_t configIndex, const std::string& contextName);
    std::vector<std::wstring> KubeconfigFiles() const;
    // "prod-cluster / namespace: payments" gibi; kubeconfig yoksa bos.
    std::wstring KubeContextSummary() const;

    static bool HasKubectl();

private:
    K8sManager() = default;
    void RescanDir();
    void ParseYamlContent(const std::string& content, const std::string& sourceName,
                          const std::wstring& fullPath);

    std::vector<K8sResource> m_resources;
    std::vector<std::wstring> m_loadedFiles;
    std::vector<KubeconfigFile> m_kubeconfigs;
    size_t m_activeKubeconfig = 0;
    std::wstring m_k8sDir;
};

} // namespace ft
