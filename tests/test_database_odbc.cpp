#include <winsock2.h>
#include "database/Database.h"
#include "core/Utf8.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>
using namespace ft::db;
// The driver is registered only inside this test process via a redirected HKLM.
// No system ODBC configuration is installed or overwritten.
struct TestDriverRegistry {
    HKEY root=nullptr;std::wstring path;
    explicit TestDriverRegistry(const std::wstring& dll){
        path=L"Software\\FullTerminalTestODBC\\"+std::to_wstring(GetCurrentProcessId());
        assert(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&root,nullptr)==ERROR_SUCCESS);
        HKEY entry=nullptr;assert(RegCreateKeyExW(root,L"SOFTWARE\\ODBC\\ODBCINST.INI\\FullTerminal Test PostgreSQL",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&entry,nullptr)==ERROR_SUCCESS);
        assert(RegSetValueExW(entry,L"Driver",0,REG_SZ,(const BYTE*)dll.c_str(),(DWORD)(dll.size()+1)*2)==ERROR_SUCCESS);RegCloseKey(entry);
        assert(RegCreateKeyExW(root,L"SOFTWARE\\ODBC\\ODBCINST.INI\\ODBC Drivers",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&entry,nullptr)==ERROR_SUCCESS);
        RegSetValueExW(entry,L"FullTerminal Test PostgreSQL",0,REG_SZ,(const BYTE*)L"Installed",20);RegCloseKey(entry);
        assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE,root)==ERROR_SUCCESS);
    }
    ~TestDriverRegistry(){RegOverridePredefKey(HKEY_LOCAL_MACHINE,nullptr);if(root)RegCloseKey(root);RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());}
};
static void Check(const Result& r){if(!r.error.empty()){std::cerr<<ft::WideToUtf8(r.error)<<"\n";throw std::runtime_error("ODBC fixture failed");}}
static Result Query(Backend& b,const std::wstring& sql){Request r;r.sql=sql;auto out=b.Run(r);Check(out);return out;}
static int RunFixture(int argc,wchar_t** argv){
    if(argc!=3){std::cerr<<"Usage: test_database_odbc driver-dll localhost-port (isolated PostgreSQL fixture)\n";return 77;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    WSADATA winsock{};assert(WSAStartup(MAKEWORD(2,2),&winsock)==0);
    SOCKET warm=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(warm!=INVALID_SOCKET);closesocket(warm);
    const auto driver=std::filesystem::absolute(argv[1]);SetDllDirectoryW(driver.parent_path().c_str());
    auto crypto=LoadLibraryW((driver.parent_path()/L"libcrypto-3-x64.dll").c_str());assert(crypto);
    auto random=(int(*)(unsigned char*,int))GetProcAddress(crypto,"RAND_bytes");assert(random);unsigned char seed[32];assert(random(seed,sizeof(seed))==1);
    TestDriverRegistry registry(driver.wstring());
    Profile p;p.engine=Engine::PostgreSQL;p.driver=L"FullTerminal Test PostgreSQL";p.host=L"127.0.0.1";p.port=_wtoi(argv[2]);p.user=L"ft_test";p.password=L"ft-local-fixture-only-8842";p.database=L"ft_test";p.encrypt=false;
    auto b=MakeOdbcBackend();Check(b->Open(p));
    Query(*b,L"DROP TABLE IF EXISTS odd_table; CREATE TABLE \"odd_table\"(id INTEGER PRIMARY KEY, title TEXT, amount NUMERIC(15,4), note TEXT); INSERT INTO \"odd_table\" VALUES(1,'İstanbul',12.3400,NULL),(2,'Other',8.5,'');");
    Table t{L"ft_test",L"public",L"odd_table",L"TABLE"};Request req;req.action=Action::Catalog;auto catalog=b->Run(req);Check(catalog);bool found=false;for(const auto& item:catalog.tables)if(item.name==t.name)found=true;assert(found);
    req.action=Action::Describe;req.table=t;auto meta=b->Run(req);Check(meta);assert(meta.sets.size()==1&&meta.sets[0].columns.size()==4);auto cols=meta.sets[0].columns;assert(cols[0].primaryKey);
    auto data=Query(*b,SelectTable(p.engine,t,cols,0,false,0,201));assert(data.sets[0].rows.size()==2);Row original=data.sets[0].rows[0],updated=original;updated[1].text=L"çığ ÖĞ ' ; --";updated[2].text=L"25.1250";
    std::wstring why;auto edit=UpdateRow(p.engine,t,cols,original,updated,false,why);assert(edit);req={};req.action=Action::Edit;req.edits={*edit};Check(b->Run(req));
    auto after=Query(*b,L"SELECT title,amount,note FROM odd_table WHERE id=1");assert(after.sets[0].rows[0][0].text==updated[1].text&&after.sets[0].rows[0][1].text==L"25.1250"&&!after.sets[0].rows[0][2].text);
    auto conflict=b->Run(req);assert(!conflict.error.empty());
    auto multi=Query(*b,L"SELECT 'a; b'; SELECT repeat('ç',12000) AS long_text;");assert(multi.sets.size()==2&&multi.sets[1].rows[0][0].text->size()==12000);
    req={};req.action=Action::AutoCommit;req.flag=false;Check(b->Run(req));Query(*b,L"UPDATE odd_table SET amount=999 WHERE id=1");
    original=Query(*b,L"SELECT * FROM odd_table WHERE id=1").sets[0].rows[0];updated=original;updated[1].text=L"transaction edit";edit=UpdateRow(p.engine,t,cols,original,updated,false,why);assert(edit);req.action=Action::Edit;req.edits={*edit};Check(b->Run(req));
    req.action=Action::Rollback;Check(b->Run(req));assert(Query(*b,L"SELECT amount FROM odd_table WHERE id=1").sets[0].rows[0][0].text==L"25.1250");
    req.action=Action::Rollback;Check(b->Run(req));req.action=Action::AutoCommit;req.flag=true;Check(b->Run(req));
    // SQL BEGIN must be reflected in the UI and must not be committed by a grid edit.
    assert(Query(*b,L"BEGIN; UPDATE odd_table SET note='pending' WHERE id=1;").transaction);
    original=Query(*b,L"SELECT * FROM odd_table WHERE id=1").sets[0].rows[0];updated=original;updated[1].text=L"SQL transaction grid edit";
    edit=UpdateRow(p.engine,t,cols,original,updated,false,why);req.action=Action::Edit;req.edits={*edit};Check(b->Run(req));
    req.action=Action::Rollback;Check(b->Run(req));assert(!Query(*b,L"SELECT note FROM odd_table WHERE id=1").sets[0].rows[0][0].text);
    Result cancelled;std::thread running([&]{Request q;q.sql=L"SELECT pg_sleep(20)";cancelled=b->Run(q);});Sleep(200);b->Cancel();running.join();assert(cancelled.cancelled);
    Check(Query(*b,L"SELECT 1"));
    const auto pid=Query(*b,L"SELECT pg_backend_pid()").sets[0].rows[0][0].text.value();auto killer=MakeOdbcBackend();Check(killer->Open(p));Query(*killer,L"SELECT pg_terminate_backend("+pid+L")");
    Request ping;ping.action=Action::Health;auto lost=b->Run(ping);assert(lost.lost);Check(b->Open(p));assert(Query(*b,L"SELECT count(*) FROM odd_table").sets[0].rows[0][0].text==L"2");
    std::cout<<"PostgreSQL ODBC: connection, metadata/PK, Unicode/long values, numeric parameter CRUD, conflict rollback, manual savepoints, cancellation, loss and explicit reconnect passed.\n";
    return 0;
}

int wmain(int argc,wchar_t** argv){try{return RunFixture(argc,argv);}catch(const std::exception&){return 2;}}
