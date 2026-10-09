#include "database/Database.h"
#include "core/Utf8.h"
#include "mcp/Json.h"
#include <wincrypt.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace ft::db {
static std::string Protect(const std::wstring& value) {
    DATA_BLOB in{(DWORD)(value.size()*sizeof(wchar_t)),(BYTE*)value.data()},out{};
    if(!CryptProtectData(&in,L"FullTerminal database",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return {};
    DWORD n=0; CryptBinaryToStringA(out.pbData,out.cbData,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&n);
    std::string text(n,'\0'); CryptBinaryToStringA(out.pbData,out.cbData,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,text.data(),&n);
    SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);while(!text.empty()&&!text.back())text.pop_back();return text;
}
static bool Unprotect(const std::string& text,std::wstring& value) {
    DWORD n=0; if(!CryptStringToBinaryA(text.c_str(),(DWORD)text.size(),CRYPT_STRING_BASE64,nullptr,&n,nullptr,nullptr))return false;
    std::vector<BYTE> bytes(n);if(!CryptStringToBinaryA(text.c_str(),(DWORD)text.size(),CRYPT_STRING_BASE64,bytes.data(),&n,nullptr,nullptr))return false;
    DATA_BLOB in{n,bytes.data()},out{};
    if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return false;
    value.assign((wchar_t*)out.pbData,out.cbData/sizeof(wchar_t));SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return true;
}
bool LoadProfiles(const std::wstring& dir,std::vector<Profile>& profiles,std::wstring& error) {
    const auto path=std::filesystem::path(dir)/L"databases.json";
    if(!std::filesystem::exists(path))return true;
    std::ifstream file(path,std::ios::binary); std::string text((std::istreambuf_iterator<char>(file)),{});
    bool ok=false;auto root=json::Value::parse(text,&ok);
    if(!ok||!root["connections"].is_array()){error=L"Cannot read databases.json; the existing file was not changed.";return false;}
    std::vector<Profile> loaded;
    for(const auto& item:root["connections"].arrVal) {
        Profile p; p.id=Utf8ToWide(item["id"].as_string());p.name=Utf8ToWide(item["name"].as_string());
        p.engine=(Engine)std::clamp((int)item["engine"].as_int(),0,5);
        p.host=Utf8ToWide(item["host"].as_string());p.port=(int)item["port"].as_int(5432);
        p.database=Utf8ToWide(item["database"].as_string());p.user=Utf8ToWide(item["user"].as_string());
        p.driver=Utf8ToWide(item["driver"].as_string());p.dsn=Utf8ToWide(item["dsn"].as_string());
        p.integrated=item["integrated"].as_bool();p.readOnly=item["readOnly"].as_bool();
        p.encrypt=item["encrypt"].as_bool(true);p.trustCertificate=item["trustCertificate"].as_bool();
        p.protectedPassword=item["secret"].as_string();
        if(!p.protectedPassword.empty())p.secretLocked=!Unprotect(p.protectedPassword,p.password);
        if(!p.id.empty())loaded.push_back(std::move(p));
    }
    profiles=std::move(loaded);return true;
}
bool SaveProfiles(const std::wstring& dir,const std::vector<Profile>& profiles,std::wstring& error) {
    auto root=json::Value::Object(),list=json::Value::Array();root["version"]=1;
    for(const auto& p:profiles) {
        auto item=json::Value::Object();item["id"]=WideToUtf8(p.id);item["name"]=WideToUtf8(p.name);item["engine"]=(int)p.engine;
        item["host"]=WideToUtf8(p.host);item["port"]=p.port;item["database"]=WideToUtf8(p.database);
        item["user"]=WideToUtf8(p.user);item["driver"]=WideToUtf8(p.driver);item["dsn"]=WideToUtf8(p.dsn);
        item["integrated"]=p.integrated;item["readOnly"]=p.readOnly;item["encrypt"]=p.encrypt;item["trustCertificate"]=p.trustCertificate;
        std::string secret=p.secretLocked?p.protectedPassword:std::string{};
        if(!p.password.empty()){secret=Protect(p.password);if(secret.empty()){error=L"Windows could not encrypt the database password. Nothing was saved.";return false;}}
        item["secret"]=secret;list.push_back(item);
    }
    root["connections"]=list;
    std::error_code ec;std::filesystem::create_directories(dir,ec);
    const auto path=std::filesystem::path(dir)/L"databases.json";
    const auto temp=path.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    std::ofstream file(temp,std::ios::binary|std::ios::trunc);file<<root.dump();file.close();
    if(file.fail()||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());error=L"Could not save database connections.";return false;
    }return true;
}
std::unique_ptr<Backend> MakeBackend(Engine e) {return e==Engine::SQLite?MakeSqliteBackend():MakeOdbcBackend();}
Session::Session(Profile p,std::unique_ptr<Backend> backend):m_profile(std::move(p)),m_backend(backend?std::move(backend):MakeBackend(m_profile.engine)) {
    m_healthAt=std::chrono::steady_clock::now(); m_worker=std::thread([this]{Work();});NewEditor();
}
Session::~Session() {
    {std::lock_guard lock(m_mutex);m_stop=true;}m_backend->Cancel();m_wake.notify_one();if(m_worker.joinable())m_worker.join();
    if(!m_profile.password.empty())SecureZeroMemory(m_profile.password.data(),m_profile.password.size()*sizeof(wchar_t));
}
void Session::Work() {
    for(;;) {
        Request request;
        {std::unique_lock lock(m_mutex);m_wake.wait(lock,[&]{return m_stop||m_request.has_value();});if(m_stop)break;request=std::move(*m_request);m_request.reset();}
        Result result; const auto start=std::chrono::steady_clock::now();
        try {result=request.action==Action::Connect?m_backend->Open(m_profile):m_backend->Run(request);}
        catch(const std::exception& e){result.error=Utf8ToWide(e.what());}
        catch(...){result.error=L"Database driver failed.";}
        result.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        {std::lock_guard lock(m_mutex);m_completed=Completion{std::move(request),std::move(result)};}
    }
    // Drivers that require thread affinity are also disconnected on their owning worker.
    try { Request r;r.action=Action::Disconnect;m_backend->Run(r); } catch(...) {}
}
bool Session::Submit(Request r) {
    if(busy || (r.action!=Action::Connect && r.action!=Action::Disconnect && state!=State::Connected))return false;
    busy=true;error.clear();notice.clear();if(r.action==Action::Connect)state=State::Connecting;
    {std::lock_guard lock(m_mutex);m_request=std::move(r);}m_wake.notify_one();return true;
}
void Session::Cancel(){
    if(!busy)return;
    {std::lock_guard lock(m_mutex);if(m_request){Result r;r.cancelled=true;r.error=L"Operation cancelled before execution.";r.transaction=transaction;m_completed=Completion{std::move(*m_request),std::move(r)};m_request.reset();return;}}
    m_backend->Cancel();
}
void Session::Connect(){Request r;r.action=Action::Connect;Submit(r);}
void Session::RefreshCatalog(){Request r;r.action=Action::Catalog;Submit(r);}
Document* Session::Find(uint64_t id){for(auto& d:documents)if(d.id==id)return &d;return nullptr;}
Document* Session::Active(){return Find(active);}
uint64_t Session::NewEditor(const std::wstring& sql) {
    static std::atomic<int> editorIds{1000000};
    Document d;d.id=m_nextDocument++;d.editorId=editorIds.fetch_add(1);d.name=L"SQL "+std::to_wstring(d.id);d.sql=d.savedSql=sql;active=d.id;documents.push_back(std::move(d));return active;
}
uint64_t Session::OpenTable(const Table& table) {
    for(auto& d:documents)if(d.tableView&&d.table==table){active=d.id;return active;}
    if(busy)return 0;
    Document d;d.id=m_nextDocument++;d.name=table.name;d.tableView=true;d.table=table;active=d.id;documents.push_back(std::move(d));
    Request r;r.action=Action::Describe;r.document=active;r.table=table;Submit(r);return active;
}
bool Session::Execute(uint64_t id,const std::wstring& sql) {
    auto* doc=Find(id);if(!doc)return false;
    Request r;r.document=id;r.sql=sql.empty()?doc->sql:sql;r.rowLimit=1000;
    if(r.sql.find_first_not_of(L" \r\n\t")==std::wstring::npos)return false;
    if(!Submit(r))return false;
    history.insert(history.begin(),r.sql);if(history.size()>50)history.pop_back();return true;
}
bool Session::LoadTable(uint64_t id) {
    auto* d=Find(id);if(!d||!d->tableView)return false;
    Request r;r.document=id;r.rowLimit=d->pageSize;
    r.sql=SelectTable(m_profile.engine,d->table,d->tableColumns,d->sortColumn,d->descending,d->offset,d->pageSize+1,d->filterColumn,d->filterNull);
    if(d->filterColumn>=0&&d->filterColumn<(int)d->tableColumns.size()&&!d->filterNull) {
        const auto& c=d->tableColumns[d->filterColumn];r.parameters.push_back({Value{d->filter},c.sqlType,c.size});
    }
    d->sql=r.sql;return Submit(r);
}
bool Session::SaveRow(uint64_t id,bool remove,std::wstring& why) {
    auto* d=Find(id);if(!d||m_profile.readOnly){why=L"This connection is read-only.";return false;}
    std::optional<Statement> edit;
    if(d->inserting)edit=InsertRow(m_profile.engine,d->table,d->tableColumns,d->editRow,d->include,why);
    else if(!d->result.sets.empty()&&d->selectedRow>=0&&d->selectedRow<(int)d->result.sets[0].rows.size())
        edit=UpdateRow(m_profile.engine,d->table,d->tableColumns,d->result.sets[0].rows[d->selectedRow],d->editRow,remove,why);
    if(!edit)return false;
    Request r;r.action=Action::Edit;r.document=id;r.edits.push_back(std::move(*edit));return Submit(r);
}
bool Session::HasUnsaved() const {
    if(transaction)return true;
    for(const auto& d:documents)if(d.editing||(!d.tableView&&d.sql!=d.savedSql))return true;
    return false;
}
bool Session::Poll() {
    std::optional<Completion> done;
    {std::lock_guard lock(m_mutex);done.swap(m_completed);}
    if(!done) {
        if(!busy&&state==State::Connected&&std::chrono::steady_clock::now()-m_healthAt>std::chrono::seconds(30)) {
            Request r;r.action=Action::Health;Submit(r);return true;
        }return false;
    }
    busy=false;m_healthAt=std::chrono::steady_clock::now();auto& r=done->request;auto& result=done->result;
    error=result.error;notice=result.information;transaction=result.transaction;
    if(result.lost){state=State::Lost;notice=L"Connection lost. Query outcome may be unknown; SQL and results are retained. Reconnect explicitly.";}
    if(r.action==Action::Connect) {
        state=error.empty()?State::Connected:State::Disconnected;
        autoCommit=true;transaction=false;
        if(error.empty())RefreshCatalog();
    } else if(r.action==Action::Disconnect){state=State::Disconnected;transaction=false;}
    else if(r.action==Action::Catalog&&error.empty()){tables=std::move(result.tables);databases=std::move(result.databases);}
    else if(r.action==Action::AutoCommit&&error.empty())autoCommit=r.flag;
    else if(auto* d=Find(r.document)) {
        if(r.action==Action::Describe&&error.empty()&&!result.sets.empty()) {
            d->tableColumns=std::move(result.sets[0].columns);LoadTable(d->id);
        } else if(r.action==Action::Edit&&error.empty()) {d->editing=false;d->inserting=false;LoadTable(d->id);}
        else if(r.action==Action::Query || !error.empty()) {
            if(!error.empty()&&result.sets.empty())result.sets=std::move(d->result.sets);
            d->result=std::move(result);d->resultIndex=0;d->scrollRow=0;d->selectedRow=-1;
        }
    }
    return true;
}
} // namespace ft::db
