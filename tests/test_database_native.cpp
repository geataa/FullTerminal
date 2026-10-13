#include <winsock2.h>
#include "database/NativeBackend.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace ft;using namespace ft::db;
static Parameter P(std::optional<std::wstring> s){Parameter p;p.value.text=std::move(s);return p;}
static Result Query(Backend& db,const wchar_t* sql,std::vector<Parameter> params={}){Request r;r.sql=sql;r.parameters=std::move(params);return db.Run(r);}
static void Good(const Result& r){if(!r.error.empty()){std::fprintf(stderr,"Database operation failed: %s\n",WideToUtf8(r.error).c_str());}assert(r.error.empty());assert(!r.lost);}
static std::wstring Env(const wchar_t* key){wchar_t* value=nullptr;size_t n=0;_wdupenv_s(&value,&n,key);std::wstring out=value?value:L"";if(value){SecureZeroMemory(value,n*sizeof(wchar_t));free(value);}return out;}

static void PostgresLive(){
    Profile p;p.host=Env(L"PGHOST");p.port=_wtoi(Env(L"PGPORT").c_str());p.database=Env(L"PGDATABASE");p.user=Env(L"PGUSER");p.password=Env(L"PGPASSWORD");p.encrypt=Env(L"PGSSLMODE")!=L"disable";p.trustCertificate=true;
    assert(!p.password.empty());auto db=MakeBackend(Engine::PostgreSQL);Good(db->Open(p));
    auto r=Query(*db,L"SELECT current_database(),current_user,version()");Good(r);assert(r.sets[0].rows[0][0].text==p.database);assert(r.sets[0].rows[0][1].text==p.user);
    Request req;req.action=Action::Catalog;r=db->Run(req);Good(r);std::printf("PostgreSQL live: catalogue read (%zu tables).\n",r.tables.size());
    Good(Query(*db,L"CREATE TEMPORARY TABLE ft_native_test(id integer PRIMARY KEY, txt text, amount numeric(20,4), generated_id bigint GENERATED ALWAYS AS IDENTITY)"));
    r=Query(*db,L"SELECT n.nspname FROM pg_namespace n WHERE n.oid=pg_my_temp_schema()");Good(r);Table table{p.database,*r.sets[0].rows[0][0].text,L"ft_native_test",L"TABLE"};
    req={};req.action=Action::Describe;req.table=table;r=db->Run(req);Good(r);auto columns=r.sets[0].columns;assert(columns.size()==4&&columns[0].primaryKey&&columns[3].generated);
    Good(Query(*db,L"INSERT INTO ft_native_test(id,txt,amount) VALUES (?,?,?),(?,?,?),(?,?,?)",{P(L"1"),P(L"Türkçe 😃 ' ; --"),P(L"1234567890123.4567"),P(L"2"),P(L""),P(L"0"),P(L"3"),P(std::nullopt),P(std::nullopt)}));
    r=Query(*db,L"SELECT txt,amount FROM ft_native_test ORDER BY id");Good(r);assert(r.sets[0].rows.size()==3);assert(r.sets[0].rows[0][0].text==L"Türkçe 😃 ' ; --");assert(r.sets[0].rows[0][1].text==L"1234567890123.4567");assert(r.sets[0].rows[1][0].text==L"");assert(!r.sets[0].rows[2][0].text);
    req={};req.sql=L"SELECT generate_series(1,100)";req.rowLimit=3;r=db->Run(req);Good(r);assert(r.sets[0].limited&&r.sets[0].rows.size()==3);
    r=Query(*db,L"SELECT 1; SELECT 2;");Good(r);assert(r.sets.size()==2);
    r=Query(*db,L"SELECT * FROM ft_native_test ORDER BY id");Good(r);auto original=r.sets[0].rows[0],updated=original;updated[1].text=L"edited";std::wstring error;
    auto edit=UpdateRow(Engine::PostgreSQL,table,columns,original,updated,false,error);assert(edit);
    req={};req.action=Action::Edit;req.edits={*edit};Good(db->Run(req));
    r=db->Run(req);assert(!r.error.empty()&&!r.transaction); // stale edit rolls back
    req={};req.action=Action::AutoCommit;req.flag=false;Good(db->Run(req));
    r=Query(*db,L"UPDATE ft_native_test SET txt='rollback' WHERE id=1");Good(r);assert(r.transaction);
    req.action=Action::Rollback;r=db->Run(req);Good(r);assert(!r.transaction);
    req.action=Action::AutoCommit;req.flag=true;Good(db->Run(req));
    r=Query(*db,L"SELECT txt FROM ft_native_test WHERE id=1");Good(r);assert(r.sets[0].rows[0][0].text==L"edited");
    Good(Query(*db,L"BEGIN; UPDATE ft_native_test SET txt='outer transaction' WHERE id=1"));
    r=Query(*db,L"SELECT * FROM ft_native_test WHERE id=1");Good(r);original=r.sets[0].rows[0];updated=original;updated[1].text=L"savepoint edit";
    edit=UpdateRow(Engine::PostgreSQL,table,columns,original,updated,false,error);assert(edit);req={};req.action=Action::Edit;req.edits={*edit};
    r=db->Run(req);Good(r);assert(r.transaction);r=db->Run(req);assert(!r.error.empty()&&r.transaction);
    req.action=Action::Rollback;Good(db->Run(req));r=Query(*db,L"SELECT txt FROM ft_native_test WHERE id=1");Good(r);assert(r.sets[0].rows[0][0].text==L"edited");
    r=Query(*db,L"SELECT definitely_missing_column FROM ft_native_test");assert(!r.error.empty()&&!r.lost);
    Good(Query(*db,L"SELECT 1"));
    std::thread cancel([&]{Sleep(200);db->Cancel();});r=Query(*db,L"SELECT pg_sleep(10)");cancel.join();assert(r.cancelled&&!r.lost);Good(Query(*db,L"SELECT 1"));
    Good(Query(*db,L"DROP TABLE pg_temp.ft_native_test"));req={};req.action=Action::Disconnect;Good(db->Run(req));
    Good(db->Open(p));Good(Query(*db,L"SELECT 1"));
    p.readOnly=true;Good(db->Open(p));r=Query(*db,L"SHOW transaction_read_only");Good(r);assert(r.sets[0].rows[0][0].text==L"on");
    // Terminate this fixture's own backend only, never another user's session.
    r=Query(*db,L"SELECT pg_terminate_backend(pg_backend_pid())");assert(r.lost);
    Good(db->Open(p));Good(Query(*db,L"SELECT 1"));
    puts("PostgreSQL live PASS: auth, metadata, temporary-table CRUD, Unicode/NULL/empty, decimal, row limit, multiple results, rollback, stale edits, cancellation, reconnect, read-only.");
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--postgres-live"){PostgresLive();return 0;}
    assert(NumberParameters(L"SELECT ? AS \"?\", '?' /* ? */ -- ?\n, ?",L"$",2)==L"SELECT $1 AS \"?\", '?' /* ? */ -- ?\n, $2");
    assert(NumberParameters(L"SELECT $$?$$, $body$ ? $body$, ?",L"@p",1)==L"SELECT $$?$$, $body$ ? $body$, @p1");
    bool caught=false;try{NumberParameters(L"SELECT ?",L"$",2);}catch(...){caught=true;}assert(caught);
    size_t bytes=16*1024*1024;auto value=NativeText("hello",5,bytes);assert(value.truncated&&value.text==L"");
    WSADATA ws{};assert(WSAStartup(MAKEWORD(2,2),&ws)==0);auto reserved=socket(AF_INET,SOCK_STREAM,0);assert(reserved!=INVALID_SOCKET);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(reserved,(sockaddr*)&addr,sizeof(addr))==0);int len=sizeof(addr);getsockname(reserved,(sockaddr*)&addr,&len);
    for(auto engine:{Engine::PostgreSQL,Engine::MySQL,Engine::SqlServer}){
        Profile p;p.engine=engine;p.host=L"127.0.0.1";p.port=ntohs(addr.sin_port);p.user=L"fixture";p.password=L"DO_NOT_LOG_TEST_PASSWORD";
        auto backend=MakeBackend(engine);auto r=backend->Open(p);assert(!r.error.empty());assert(r.error.find(p.password)==r.error.npos);assert(r.error.find(L"ODBC")==r.error.npos);
        Request request;request.sql=L"SELECT 1";assert(backend->Run(request).lost);
    }
    closesocket(reserved);WSACleanup();puts("Native adapters PASS: parameter lexer, bounded cells, no-driver connection errors, password redaction, disconnected state.");
}
