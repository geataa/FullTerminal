#pragma once
#include "database/Database.h"
#include "core/Utf8.h"
#include <algorithm>

namespace ft::db {
// The SQL workspace has one serialized worker per connection. These adapters
// share catalogue/edit semantics, but use each vendor's wire-protocol library.
class NativeBackend : public Backend {
protected:
    Profile profile;
    bool connected=false, autoCommit=true, transaction=false;
    virtual Result Execute(const std::wstring&,const std::vector<Parameter>&,int)=0;
    virtual void Close()=0;
    Result Query(const std::wstring& sql,const std::vector<Parameter>& params={},int limit=10000);
    Result Catalog();
    Result Describe(const Table&);
    Result Edit(const Request&);
    Result Finish(Result);
public:
    ~NativeBackend(){if(!profile.password.empty())SecureZeroMemory(profile.password.data(),profile.password.size()*sizeof(wchar_t));}
    Result Run(const Request&) override;
};
std::unique_ptr<Backend> MakePostgresBackend();
std::unique_ptr<Backend> MakeMysqlBackend();
std::unique_ptr<Backend> MakeSqlServerBackend();
bool NativeEngine(Engine);
// Placeholders in generated statements only, never in quoted names/literals.
std::wstring NumberParameters(const std::wstring&,const wchar_t* prefix,size_t expected);
Value NativeText(const char* data,size_t length,size_t& bytes);
Result NativeError(const wchar_t* message);
// Public trust anchors only; the temporary bundle is removed after TLS setup.
class WindowsTrustBundle {
    std::wstring path;
public:
    ~WindowsTrustBundle();
    std::wstring Create();
};
}
