#include "database/Database.h"
#include "core/Utf8.h"
#include "sqlite3.h"
#include <algorithm>
#include <cwctype>

namespace ft::db {
class SqliteBackend final:public Backend {
    sqlite3* m_db=nullptr;
    std::mutex m_handle;
    std::atomic<bool> m_cancel{false};
    bool m_auto=true,m_readOnly=false;
    std::chrono::steady_clock::time_point m_deadline;
    static int Progress(void* self) {
        auto* p=(SqliteBackend*)self;
        return p->m_cancel || std::chrono::steady_clock::now()>p->m_deadline;
    }
    static int Authorize(void* self,int action,const char*,const char*,const char*,const char*) {
        if(!((SqliteBackend*)self)->m_readOnly)return SQLITE_OK;
        switch(action){
        case SQLITE_SELECT:case SQLITE_READ:case SQLITE_FUNCTION:case SQLITE_RECURSIVE:
        case SQLITE_TRANSACTION:case SQLITE_SAVEPOINT:return SQLITE_OK;
        case SQLITE_PRAGMA:return SQLITE_OK; // Main database is opened SQLITE_OPEN_READONLY; ATTACH is denied.
        default:return SQLITE_DENY;
        }
    }
    void Close() {std::lock_guard lock(m_handle);if(m_db){sqlite3_close_v2(m_db);m_db=nullptr;}}
    Result Error(int code) {
        Result r;r.error=m_db?Utf8ToWide(sqlite3_errmsg(m_db)):L"Database is not connected.";
        r.cancelled=code==SQLITE_INTERRUPT;r.sqlState=r.cancelled?L"HY008":L"SQLITE "+std::to_wstring(code);
        if(r.cancelled)r.error=L"Query cancelled or timed out.";
        return r;
    }
    Result Query(const std::wstring& sql,const std::vector<Parameter>& params,int limit) {
        Result result;
        const auto utf=WideToUtf8(sql);const char* next=utf.c_str();size_t bytes=0;
        while(*next) {
            sqlite3_stmt* statement=nullptr;const char* tail=nullptr;
            int code=sqlite3_prepare_v2(m_db,next,-1,&statement,&tail);
            if(code!=SQLITE_OK){auto e=Error(code);result.error=e.error;result.sqlState=e.sqlState;result.cancelled=e.cancelled;break;}
            next=tail;if(!statement)continue;
            if(m_readOnly&&!sqlite3_stmt_readonly(statement)){sqlite3_finalize(statement);result.error=L"This connection is read-only.";break;}
            if(sqlite3_bind_parameter_count(statement)!=(int)params.size()) {
                sqlite3_finalize(statement);result.error=L"Parameter count does not match the query.";break;
            }
            for(size_t i=0;i<params.size();++i) {
                if(!params[i].value.text)sqlite3_bind_null(statement,(int)i+1);
                else {const auto p=WideToUtf8(*params[i].value.text);sqlite3_bind_text(statement,(int)i+1,p.data(),(int)p.size(),SQLITE_TRANSIENT);}
            }
            ResultSet set;const int count=sqlite3_column_count(statement);
            for(int i=0;i<count;++i) {
                Column c;c.name=Utf8ToWide(sqlite3_column_name(statement,i));
                if(const auto type=sqlite3_column_decltype(statement,i))c.type=Utf8ToWide(type);
                set.columns.push_back(std::move(c));
            }
            while((code=sqlite3_step(statement))==SQLITE_ROW) {
                if((int)set.rows.size()>=limit||bytes>16*1024*1024){set.limited=true;break;}
                Row row;
                for(int i=0;i<count;++i) {
                    Value v;
                    const int type=sqlite3_column_type(statement,i);
                    if(type==SQLITE_BLOB){v.binary=true;v.text=L"[BLOB "+std::to_wstring(sqlite3_column_bytes(statement,i))+L" bytes]";}
                    else if(type!=SQLITE_NULL) {
                        const auto* p=(const char*)sqlite3_column_text(statement,i);const int n=sqlite3_column_bytes(statement,i);
                        v.truncated=n>262144;v.text=Utf8ToWide(std::string(p,std::min(n,262144)));bytes+=v.text->size()*2;
                    }
                    row.push_back(std::move(v));
                }set.rows.push_back(std::move(row));
            }
            if(code!=SQLITE_DONE&&code!=SQLITE_ROW){auto e=Error(code);result.error=e.error;result.sqlState=e.sqlState;result.cancelled=e.cancelled;}
            if(!sqlite3_stmt_readonly(statement))set.affected=sqlite3_changes64(m_db);
            sqlite3_finalize(statement);
            result.sets.push_back(std::move(set));
            if(!result.error.empty())break;
            if(result.sets.size()>=32){result.information=L"Stopped after 32 statements; remaining statements were not executed.";break;}
        }
        return result;
    }
public:
    ~SqliteBackend(){Close();}
    void Cancel() override {m_cancel=true;std::lock_guard lock(m_handle);if(m_db)sqlite3_interrupt(m_db);}
    Result Open(const Profile& p) override {
        Close();m_auto=true;m_readOnly=p.readOnly;m_cancel=false;m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        const auto name=WideToUtf8(p.database.empty()?L":memory:":p.database);
        int code;
        {std::lock_guard lock(m_handle);code=sqlite3_open_v2(name.c_str(),&m_db,(p.readOnly?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE)|SQLITE_OPEN_FULLMUTEX,nullptr);}
        if(code!=SQLITE_OK)return Error(code);
        sqlite3_busy_timeout(m_db,2000);sqlite3_progress_handler(m_db,1000,&Progress,this);
        sqlite3_exec(m_db,"PRAGMA foreign_keys=ON",nullptr,nullptr,nullptr);
        sqlite3_set_authorizer(m_db,&Authorize,this);
        Result r;r.information=L"SQLite "+Utf8ToWide(sqlite3_libversion());return r;
    }
    Result Run(const Request& request) override {
        if(request.action==Action::Disconnect){Close();return {};}
        if(!m_db){Result r;r.error=L"Database is disconnected.";r.lost=true;return r;}
        m_cancel=false;m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        Result result;
        const auto control=[&](const wchar_t* sql){return Query(sql,{},1);};
        switch(request.action) {
        case Action::AutoCommit:
            if(request.flag&&!sqlite3_get_autocommit(m_db)){result.error=L"Commit or rollback before enabling auto-commit.";break;}
            m_auto=request.flag;break;
        case Action::Commit: if(!sqlite3_get_autocommit(m_db))result=control(L"COMMIT");break;
        case Action::Rollback: if(!sqlite3_get_autocommit(m_db))result=control(L"ROLLBACK");break;
        case Action::Health: if(sqlite3_get_autocommit(m_db))result=control(L"SELECT 1");break;
        case Action::Query:
            if(!m_auto&&sqlite3_get_autocommit(m_db)){result=control(L"BEGIN");if(!result.error.empty())break;}
            result=Query(request.sql,request.parameters,std::clamp(request.rowLimit,1,10000));break;
        case Action::Edit: {
            if(m_readOnly){result.error=L"This connection is read-only.";break;}
            if(!m_auto&&sqlite3_get_autocommit(m_db)){result=control(L"BEGIN");if(!result.error.empty())break;}
            result=control(L"SAVEPOINT ft_grid_edit");if(!result.error.empty())break;
            for(const auto& edit:request.edits) {
                result=Query(edit.sql,edit.parameters,1);
                if(result.error.empty()&&edit.expectOne&&(result.sets.empty()||result.sets.back().affected!=1))
                    result.error=L"The row changed or was removed by another client. Refresh before editing; no change was saved.";
                if(!result.error.empty())break;
            }
            m_cancel=false;m_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            if(!result.error.empty())control(L"ROLLBACK TO ft_grid_edit");
            auto release=control(L"RELEASE ft_grid_edit");if(result.error.empty())result=std::move(release);
            break;
        }
        case Action::Catalog: {
            auto databases=Query(L"PRAGMA database_list",{},10000);
            if(!databases.error.empty()){result=std::move(databases);break;}
            for(const auto& row:databases.sets[0].rows) {
                if(row.size()<2||!row[1].text)continue;const auto schema=*row[1].text;result.databases.push_back(schema);
                auto tables=Query(L"SELECT name,type FROM "+QuoteIdentifier(Engine::SQLite,schema)+L".sqlite_schema WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name",{},10000);
                if(!tables.error.empty()){result.error=tables.error;break;}
                for(const auto& item:tables.sets[0].rows)result.tables.push_back({L"",schema,*item[0].text,*item[1].text});
            }break;
        }
        case Action::Describe: {
            auto info=Query(L"PRAGMA "+QuoteIdentifier(Engine::SQLite,request.table.schema.empty()?L"main":request.table.schema)+L".table_xinfo("+QuoteIdentifier(Engine::SQLite,request.table.name)+L")",{},10000);
            if(!info.error.empty()){result=std::move(info);break;}
            ResultSet set;
            for(const auto& row:info.sets[0].rows) {
                Column c;c.name=*row[1].text;c.type=row[2].text.value_or(L"");c.nullable=row[3].text!=L"1";
                c.primaryKey=row[5].text!=L"0";c.generated=row.size()>6&&row[6].text!=L"0";
                set.columns.push_back(std::move(c));
            }
            result.sets.push_back(std::move(set));break;
        }
        default:break;
        }
        result.transaction=m_db&&!sqlite3_get_autocommit(m_db);return result;
    }
};
std::unique_ptr<Backend> MakeSqliteBackend(){return std::make_unique<SqliteBackend>();}
} // namespace ft::db
