#pragma once

#include <string>
#include <vector>

namespace ft {

struct Snippet {
    std::wstring id;
    std::wstring title;
    std::wstring command;
    std::wstring category; // "Sistem", "Ağ", "Docker", "K8s", "Servis", "Git", "Güvenlik", "Özel"
    std::wstring description;
    bool isCustom = false;
    bool isYaml = false;
    std::wstring yamlContent;
};

class SnippetModel {
public:
    SnippetModel();

    void Load(const std::wstring& dataDir);
    void Save(const std::wstring& dataDir) const;

    std::vector<Snippet> GetFiltered(const std::wstring& category, const std::wstring& search) const;
    std::vector<std::wstring> GetCategories() const;

    bool AddSnippet(const std::wstring& title, const std::wstring& command,
                    const std::wstring& category, const std::wstring& description,
                    bool isYaml = false, const std::wstring& yamlContent = L"");
    bool UpdateSnippet(const std::wstring& id, const std::wstring& title, const std::wstring& command,
                       const std::wstring& category, const std::wstring& description,
                       bool isYaml = false, const std::wstring& yamlContent = L"");
    bool DeleteSnippet(const std::wstring& id);
    bool ExportYaml(const Snippet& s, const std::wstring& outDir, std::wstring& outFilePath) const;
    const Snippet* FindSnippet(const std::wstring& id) const;

    const std::vector<Snippet>& AllSnippets() const { return m_snippets; }

private:
    void InitDefaults();

    std::vector<Snippet> m_snippets;
    std::wstring m_dataDir;
};

} // namespace ft
