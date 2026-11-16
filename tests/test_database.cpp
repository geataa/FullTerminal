#include "database/Database.h"
#include "core/Utf8.h"
#include "database/SqlSyntax.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace ft::db;
static Result Run(Backend& b,const std::wstring& sql){Request r;r.sql=sql;auto out=b.Run(r);if(!out.error.empty())std::wcerr<<sql<<L": "<<out.error<<L"\n";return out;}
static Result Act(Backend& b,Action a){Request r;r.action=a;return b.Run(r);}
static void Wait(Session& s){auto until=GetTickCount64()+8000;while(s.busy&&GetTickCount64()<until){s.Poll();Sleep(1);}assert(!s.busy);}
static void Require(const Result& r){if(!r.error.empty())std::wcerr<<r.error<<L"\n";assert(r.error.empty());}
class FailingBackend:public Backend {
public:
 int writes=0;
 Result Open(const Profile&)override{return {};}
 Result Run(const Request& r)override{Result out;if(r.action==Action::Query){++writes;out.lost=true;out.error=L"connection dropped";}return out;}
 void Cancel()override{}
};
int main(){
    assert(SqlTransactionState(L"-- BEGIN\n SELECT 'BEGIN';",false)==false);
    assert(SqlTransactionState(L"BEGIN; SELECT 1;",false));
    assert(!SqlTransactionState(L"BEGIN; SELECT 1; COMMIT;",false));
    assert(SqlTransactionState(L"BEGIN; bad query; COMMIT;",false,true));
    assert(SqlTransactionState(L"ROLLBACK TO savepoint_name",true));
    const auto root=std::filesystem::current_path();const auto file=(root/(L"test-"+std::to_wstring(GetCurrentProcessId())+L".db")).wstring();
    Profile p;p.engine=Engine::SQLite;p.database=file;p.id=L"fixture";p.name=L"SQLite test";
    auto b=MakeBackend(p.engine);Require(b->Open(p));
    Require(Run(*b,L"CREATE TABLE \"odd table\" (id INTEGER PRIMARY KEY, name TEXT, amount REAL, note TEXT DEFAULT 'default', doubled REAL GENERATED ALWAYS AS (amount*2) STORED); INSERT INTO \"odd table\"(id,name,amount,note) VALUES(1,'İstanbul',25.5,NULL),(2,'O''Reilly',-12.25,''); CREATE VIEW v AS SELECT id,name FROM \"odd table\"; ATTACH ':memory:' AS auxiliary; CREATE TABLE auxiliary.extra (id INTEGER);"));
    auto catalog=Act(*b,Action::Catalog);Require(catalog);assert(catalog.databases.size()==2);assert(catalog.tables.size()==3);
    Table table{L"",L"main",L"odd table",L"table"};Request describe;describe.action=Action::Describe;describe.table=table;
    auto meta=b->Run(describe);Require(meta);const auto cols=meta.sets[0].columns;assert(cols.size()==5&&cols[0].primaryKey&&cols[4].generated);
    auto read=Run(*b,SelectTable(Engine::SQLite,table,cols,0,false,0,201));Require(read);assert(read.sets[0].rows.size()==2);
    Row original=read.sets[0].rows[0],updated=original;updated[1].text=L"Türkçe ' SQL; --";updated[3].text=L"";std::wstring error;
    auto edit=UpdateRow(Engine::SQLite,table,cols,original,updated,false,error);assert(edit);
    Request write;write.action=Action::Edit;write.edits={*edit};Require(b->Run(write));
    auto verify=Run(*b,L"SELECT name,note FROM \"odd table\" WHERE id=1");Require(verify);assert(verify.sets[0].rows[0][0].text==updated[1].text&&verify.sets[0].rows[0][1].text==L"");
    // Stale original must fail and leave the concurrent value intact.
    auto stale=b->Run(write);assert(!stale.error.empty());verify=Run(*b,L"SELECT name FROM \"odd table\" WHERE id=1");assert(verify.sets[0].rows[0][0].text==updated[1].text);
    Row inserted(cols.size());inserted[0].text=L"3";inserted[1].text=L"new";inserted[2].text=L"40";std::vector<bool> include{true,true,true,false,false};
    auto ins=InsertRow(Engine::SQLite,table,cols,inserted,include,error);assert(ins);write.edits={*ins};Require(b->Run(write));verify=Run(*b,L"SELECT note,doubled FROM \"odd table\" WHERE id=3");assert(verify.sets[0].rows[0][0].text==L"default"&&verify.sets[0].rows[0][1].text==L"80.0");
    Request mode;mode.action=Action::AutoCommit;mode.flag=false;Require(b->Run(mode));
    Require(Run(*b,L"UPDATE \"odd table\" SET amount=100 WHERE id=1"));
    write.edits={*ins};assert(!b->Run(write).error.empty()); // duplicate key rollback only grid savepoint
    verify=Run(*b,L"SELECT amount FROM \"odd table\" WHERE id=1");assert(verify.sets[0].rows[0][0].text==L"100.0");
    mode.flag=true;assert(!b->Run(mode).error.empty());Require(Act(*b,Action::Rollback));Require(b->Run(mode));
    verify=Run(*b,L"SELECT amount FROM \"odd table\" WHERE id=1");assert(verify.sets[0].rows[0][0].text==L"25.5");
    // Semicolons inside strings/triggers are parsed by the engine, not split naively.
    auto multi=Run(*b,L"SELECT ';' AS text; SELECT NULL AS x, '' AS x;");Require(multi);assert(multi.sets.size()==2&&multi.sets[0].rows[0][0].text==L";");
    const auto csv=Csv(multi.sets[1]);assert(csv==L"\"x\",\"x\"\r\n,\"\"\r\n");assert(JsonRows(multi.sets[1]).find(L"null")!=std::wstring::npos);
    Request paged;paged.sql=SelectTable(p.engine,table,cols,2,true,1,2);paged.rowLimit=1;auto page=b->Run(paged);Require(page);assert(page.sets[0].limited&&page.sets[0].rows[0][0].text==L"1");
    Request filter;filter.sql=SelectTable(p.engine,table,cols,0,false,0,20,1,false);filter.parameters.push_back({Value{updated[1].text}});verify=b->Run(filter);Require(verify);assert(verify.sets[0].rows.size()==1);
    auto row=Run(*b,L"SELECT * FROM \"odd table\" WHERE id=3").sets[0].rows[0];auto del=UpdateRow(p.engine,table,cols,row,row,true,error);assert(del);write.edits={*del};Require(b->Run(write));
    std::vector<Column> noKey=cols;noKey[0].primaryKey=false;assert(!UpdateRow(p.engine,table,noKey,row,row,true,error));
    Require(Act(*b,Action::Disconnect));p.readOnly=true;Require(b->Open(p));assert(!Run(*b,L"DELETE FROM \"odd table\"").error.empty());assert(!Run(*b,L"ATTACH ':memory:' AS bypass").error.empty());Require(Act(*b,Action::Disconnect));b.reset();
    // Two independent sessions, child editor identity, cancellation and no replay on loss.
    p.database=L":memory:";p.readOnly=false;Session s(p),other(p);s.Connect();other.Connect();Wait(s);Wait(other);assert(s.state==State::Connected&&other.state==State::Connected);
    const auto first=s.active;const auto second=s.NewEditor(L"SELECT 42 AS answer;");assert(s.Execute(first,L"SELECT 17;"));s.active=second;Wait(s);assert(s.Find(first)->result.sets[0].rows[0][0].text==L"17");assert(s.Find(second)->result.sets.empty());
    assert(s.Execute(second,L"WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<1000000000) SELECT sum(x) FROM n;"));
    Sleep(80);const auto start=GetTickCount64();assert(other.Execute(other.active,L"SELECT 9;"));Wait(other);assert(GetTickCount64()-start<2000);s.Cancel();Wait(s);assert(s.Find(second)->result.cancelled);
    auto fail=std::make_unique<FailingBackend>();auto* inspect=fail.get();Session disconnected(p,std::move(fail));disconnected.Connect();Wait(disconnected);disconnected.Execute(disconnected.active,L"INSERT ...");Wait(disconnected);assert(disconnected.state==State::Lost&&inspect->writes==1);assert(!disconnected.Execute(disconnected.active));disconnected.Connect();Wait(disconnected);assert(inspect->writes==1);
    // Connection attributes cannot escape their braces, even with a password containing '};'.
    Profile o;o.engine=Engine::SqlServer;o.driver=L"ODBC Driver 18 for SQL Server";o.password=L"p};Trusted_Connection=yes;";const auto conn=ConnectionString(o);assert(conn.find(L"PWD={p}};Trusted_Connection=yes;};")!=conn.npos);
    auto missing=MakeOdbcBackend();o.driver=L"FullTerminal missing test driver";o.password.clear();auto bad=missing->Open(o);assert(!bad.error.empty());
    assert(QuoteIdentifier(Engine::SqlServer,L"a]b")==L"[a]]b]");assert(QuoteIdentifier(Engine::PostgreSQL,L"a\"b")==L"\"a\"\"b\"");
    // DPAPI roundtrip; inspect serialized bytes for plaintext leakage.
    p.password=L"test-secret-ÖĞ-unique";p.database=file;std::vector<Profile> profiles{p};const auto dir=(root/L"profiles").wstring();assert(SaveProfiles(dir,profiles,error));
    std::ifstream f(std::filesystem::path(dir)/L"databases.json");std::string text((std::istreambuf_iterator<char>(f)),{});assert(text.find("test-secret")==text.npos);std::vector<Profile> loaded;assert(LoadProfiles(dir,loaded,error));assert(loaded.size()==1&&loaded[0].password==p.password);
    std::filesystem::remove(file);
    std::cout<<"Database: CRUD, metadata, NULL/Unicode, optimistic concurrency, savepoints, rollback, paging, cancellation, isolation, loss/no-replay, ODBC errors and DPAPI passed.\n";
}
