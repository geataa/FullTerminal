#include "database/Database.h"
#include "database/SqlSyntax.h"
#include <sql.h>
#include <sqlext.h>
#include <algorithm>
#include <cwctype>

namespace ft::db {
namespace {
bool Ok(SQLRETURN rc){return SQL_SUCCEEDED(rc);}
SQLWCHAR* W(const std::wstring& s){return (SQLWCHAR*)s.c_str();}
std::wstring Lower(std::wstring s){for(auto& c:s)c=(wchar_t)towlower(c);return s;}
std::wstring Attribute(const std::wstring& name,const std::wstring& value){
    std::wstring escaped;for(auto c:value){escaped+=c;if(c==L'}')escaped+=c;}
    return name+L"={"+escaped+L"};";
}
}
std::vector<std::wstring> InstalledDrivers(){
    std::vector<std::wstring> drivers;SQLHENV env=SQL_NULL_HENV;
    if(!Ok(SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&env)))return drivers;
    SQLSetEnvAttr(env,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0);
    SQLWCHAR name[1024],attributes[4096];SQLSMALLINT len=0,attrLen=0;
    for(SQLUSMALLINT dir=SQL_FETCH_FIRST;Ok(SQLDriversW(env,dir,name,1024,&len,attributes,4096,&attrLen));dir=SQL_FETCH_NEXT)
        drivers.emplace_back((wchar_t*)name,std::min<int>(len,1023));
    SQLFreeHandle(SQL_HANDLE_ENV,env);return drivers;
}
std::wstring DefaultDriver(Engine engine,const std::vector<std::wstring>& drivers){
    std::wstring found;
    for(const auto& d:drivers){auto s=Lower(d);bool match=false;
        switch(engine){
        case Engine::PostgreSQL:match=s.find(L"postgresql")!=s.npos&&s.find(L"unicode")!=s.npos;break;
        case Engine::MySQL:match=s.find(L"mariadb")!=s.npos||(s.find(L"mysql")!=s.npos&&s.find(L"unicode")!=s.npos);break;
        case Engine::SqlServer:match=s.find(L"odbc driver")!=s.npos&&s.find(L"sql server")!=s.npos;break;
        case Engine::Oracle:match=s.find(L"oracle")!=s.npos;break;
        default:break;
        }if(match&&(found.empty()||d>found))found=d;
    }return found;
}
std::wstring ConnectionString(const Profile& p){
    std::wstring s=p.dsn.empty()?Attribute(L"DRIVER",p.driver):Attribute(L"DSN",p.dsn);
    if(p.engine==Engine::PostgreSQL){
        // psqlODBC removes braces from PWD, but treats braces literally in scalar fields.
        // Reject attribute delimiters at Open before using these driver-specific scalars.
        if(p.dsn.empty())s+=L"Servername="+p.host+L";Port="+std::to_wstring(p.port)+L";SSLmode="+(p.encrypt?(p.trustCertificate?L"require":L"verify-full"):L"disable")+L";";
        if(!p.database.empty())s+=L"Database="+p.database+L";";
        if(!p.user.empty())s+=L"UID="+p.user+L";";
        if(!p.password.empty())s+=Attribute(L"PWD",p.password);
        if(p.readOnly)s+=L"ReadOnly=1;";
        return s;
    }
    if(p.dsn.empty())switch(p.engine){
    case Engine::SqlServer:
        s+=Attribute(L"SERVER",p.host+(p.port?L","+std::to_wstring(p.port):L""));
        s+=L"Encrypt="+(std::wstring)(p.encrypt?L"yes;":L"no;")+L"TrustServerCertificate="+(p.trustCertificate?L"yes;":L"no;");break;
    case Engine::PostgreSQL:
        s+=Attribute(L"Servername",p.host)+Attribute(L"Port",std::to_wstring(p.port));
        s+=Attribute(L"SSLmode",p.encrypt?(p.trustCertificate?L"require":L"verify-full"):L"disable");break;
    case Engine::MySQL:
        s+=Attribute(L"SERVER",p.host)+L"PORT="+std::to_wstring(p.port)+L";";
        if(Lower(p.driver).find(L"mariadb")!=std::wstring::npos){s+=L"FORCETLS="+(std::wstring)(p.encrypt?L"1;":L"0;");s+=L"SSLVERIFY="+(std::wstring)(p.trustCertificate?L"0;":L"1;");}
        else s+=L"SSLMODE="+(std::wstring)(p.encrypt?(p.trustCertificate?L"REQUIRED":L"VERIFY_IDENTITY"):L"DISABLED")+L";";break;
    case Engine::Oracle:s+=Attribute(L"DBQ",L"//"+p.host+L":"+std::to_wstring(p.port)+L"/"+p.database);break;
    default:break;
    }
    if(!p.database.empty()&&p.engine!=Engine::Oracle)s+=Attribute(L"DATABASE",p.database);
    if(p.integrated&&p.engine==Engine::SqlServer)s+=L"Trusted_Connection=yes;";
    else {if(!p.user.empty())s+=Attribute(L"UID",p.user);if(!p.password.empty())s+=Attribute(L"PWD",p.password);}
    return s;
}

class OdbcBackend final:public Backend {
    SQLHENV m_env=SQL_NULL_HENV;SQLHDBC m_db=SQL_NULL_HDBC;SQLHSTMT m_stmt=SQL_NULL_HSTMT;
    std::mutex m_handle;Profile m_profile;bool m_auto=true,m_transaction=false,m_connected=false,m_sqlTransaction=false;
    Result Diagnostic(SQLSMALLINT kind,SQLHANDLE handle){
        Result r;r.transaction=m_transaction;
        SQLWCHAR state[6]{},message[4096]{};SQLINTEGER native=0;SQLSMALLINT len=0;
        for(SQLSMALLINT n=1;n<16&&Ok(SQLGetDiagRecW(kind,handle,n,state,&native,message,4096,&len));++n){
            const std::wstring st((wchar_t*)state);if(r.sqlState.empty())r.sqlState=st;
            r.lost|=st.starts_with(L"08")||st==L"57P01"||st==L"57P02"||st==L"57P03";
            r.cancelled|=st==L"HY008"||st==L"57014";
            if(!r.error.empty())r.error+=L"\n";r.error+=L"["+st+L"] "+std::wstring((wchar_t*)message,std::min<int>(len,4095));
        }
        if(r.error.empty())r.error=L"The ODBC driver could not complete the operation.";
        if(m_db&&!r.lost){SQLUINTEGER dead=SQL_CD_FALSE;if(Ok(SQLGetConnectAttr(m_db,SQL_ATTR_CONNECTION_DEAD,&dead,0,nullptr)))r.lost=dead==SQL_CD_TRUE;}
        if(!m_profile.password.empty())for(size_t at=0;(at=r.error.find(m_profile.password,at))!=r.error.npos;at+=3)r.error.replace(at,m_profile.password.size(),L"***");
        return r;
    }
    void FreeStatement(){std::lock_guard lock(m_handle);if(m_stmt){SQLFreeHandle(SQL_HANDLE_STMT,m_stmt);m_stmt=SQL_NULL_HSTMT;}}
    void Close(){
        FreeStatement();std::lock_guard lock(m_handle);
        if(m_db){if(!m_auto)SQLEndTran(SQL_HANDLE_DBC,m_db,SQL_ROLLBACK);SQLDisconnect(m_db);SQLFreeHandle(SQL_HANDLE_DBC,m_db);m_db=SQL_NULL_HDBC;}
        if(m_env){SQLFreeHandle(SQL_HANDLE_ENV,m_env);m_env=SQL_NULL_HENV;}m_connected=false;m_transaction=false;m_sqlTransaction=false;
    }
    Result StatementHandle(){
        FreeStatement();SQLRETURN rc;
        {std::lock_guard lock(m_handle);rc=SQLAllocHandle(SQL_HANDLE_STMT,m_db,&m_stmt);}
        if(!Ok(rc))return Diagnostic(SQL_HANDLE_DBC,m_db);
        SQLSetStmtAttr(m_stmt,SQL_ATTR_QUERY_TIMEOUT,(SQLPOINTER)30,0);return {};
    }
    Result Read(int limit,bool multiple=true){
        Result result;size_t bytes=0;
        for(;;){
            SQLSMALLINT count=0;SQLNumResultCols(m_stmt,&count);ResultSet set;
            SQLLEN affected=-1;SQLRowCount(m_stmt,&affected);set.affected=affected;
            for(SQLUSMALLINT i=1;i<=count;++i){
                SQLWCHAR name[1024];SQLSMALLINT len=0,type=0,scale=0,nullable=0;SQLULEN size=0;
                if(!Ok(SQLDescribeColW(m_stmt,i,name,1024,&len,&type,&size,&scale,&nullable)))return Diagnostic(SQL_HANDLE_STMT,m_stmt);
                Column c;c.name.assign((wchar_t*)name,std::min<int>(len,1023));c.sqlType=type;c.size=(int64_t)size;c.nullable=nullable!=SQL_NO_NULLS;
                SQLWCHAR typeName[256]{};SQLSMALLINT typeLen=0;
                SQLColAttributeW(m_stmt,i,SQL_DESC_TYPE_NAME,typeName,sizeof(typeName),&typeLen,nullptr);
                c.type=(wchar_t*)typeName;set.columns.push_back(std::move(c));
            }
            while(count){
                const auto rc=SQLFetch(m_stmt);if(rc==SQL_NO_DATA)break;
                if(!Ok(rc)){auto e=Diagnostic(SQL_HANDLE_STMT,m_stmt);result.error=e.error;result.sqlState=e.sqlState;result.lost=e.lost;result.cancelled=e.cancelled;break;}
                if((int)set.rows.size()>=limit||bytes>=16*1024*1024){set.limited=true;break;}
                Row row;
                for(SQLUSMALLINT i=1;i<=count;++i){
                    Value v;const int t=set.columns[i-1].sqlType;
                    if(t==SQL_BINARY||t==SQL_VARBINARY||t==SQL_LONGVARBINARY){
                        char buffer[1];SQLLEN length=0;const auto rc=SQLGetData(m_stmt,i,SQL_C_BINARY,buffer,1,&length);
                        if(!Ok(rc)&&rc!=SQL_NO_DATA){result=Diagnostic(SQL_HANDLE_STMT,m_stmt);break;}
                        if(length!=SQL_NULL_DATA){v.binary=true;v.text=L"[BINARY]";}
                    }else{
                        std::wstring text;SQLWCHAR buffer[2048]{};SQLLEN length=0;bool null=false;
                        for(;;){
                            buffer[0]=0;const auto rc=SQLGetData(m_stmt,i,SQL_C_WCHAR,buffer,sizeof(buffer),&length);
                            if(rc==SQL_NO_DATA)break;
                            if(!Ok(rc)){result=Diagnostic(SQL_HANDLE_STMT,m_stmt);break;}
                            if(length==SQL_NULL_DATA){null=true;break;}
                            size_t n=wcsnlen((wchar_t*)buffer,2047);
                            if(text.size()+n>131072){v.truncated=true;break;}
                            text.append((wchar_t*)buffer,n);
                            if(rc==SQL_SUCCESS)break;
                            // Only truncation has more chunks; other success-with-info warnings must not loop.
                            SQLWCHAR st[6]{};SQLGetDiagRecW(SQL_HANDLE_STMT,m_stmt,1,st,nullptr,nullptr,0,nullptr);
                            if(std::wstring((wchar_t*)st)!=L"01004")break;
                        }
                        if(!null){bytes+=text.size()*2;v.text=std::move(text);}
                    }
                    row.push_back(std::move(v));if(!result.error.empty())break;
                }
                if(!result.error.empty())break;set.rows.push_back(std::move(row));
            }
            result.sets.push_back(std::move(set));if(!result.error.empty()||!multiple)break;
            if(result.sets.size()>=32){result.information=L"Only the first 32 result sets are displayed.";break;}
            const auto rc=SQLMoreResults(m_stmt);if(rc==SQL_NO_DATA)break;
            if(!Ok(rc)){auto e=Diagnostic(SQL_HANDLE_STMT,m_stmt);result.error=e.error;result.sqlState=e.sqlState;result.lost=e.lost;result.cancelled=e.cancelled;break;}
        }result.transaction=m_transaction;return result;
    }
    Result Query(const std::wstring& sql,const std::vector<Parameter>& params,int limit){
        auto result=StatementHandle();if(!result.error.empty())return result;
        SQLRETURN rc;
        std::vector<SQLLEN> lengths(params.size());
        if(params.empty())rc=SQLExecDirectW(m_stmt,W(sql),SQL_NTS);
        else {
            rc=SQLPrepareW(m_stmt,W(sql),SQL_NTS);
            for(size_t i=0;Ok(rc)&&i<params.size();++i){
                const auto& p=params[i];lengths[i]=p.value.text?SQL_NTS:SQL_NULL_DATA;
                SQLSMALLINT type=(SQLSMALLINT)(p.sqlType?p.sqlType:SQL_WVARCHAR),scale=0,nullable=0;SQLULEN size=(SQLULEN)std::max<int64_t>(p.size,1);
                // Drivers know numeric precision/scale better than a text-grid representation.
                SQLSMALLINT inferredType=type,inferredScale=0;SQLULEN inferredSize=size;
                if(Ok(SQLDescribeParam(m_stmt,(SQLUSMALLINT)i+1,&inferredType,&inferredSize,&inferredScale,&nullable))){type=inferredType;size=inferredSize;scale=inferredScale;}
                const auto* data=p.value.text?p.value.text->c_str():L"";
                rc=SQLBindParameter(m_stmt,(SQLUSMALLINT)i+1,SQL_PARAM_INPUT,SQL_C_WCHAR,type,size,scale,(SQLPOINTER)data,
                    p.value.text?(SQLLEN)((p.value.text->size()+1)*sizeof(wchar_t)):sizeof(wchar_t),&lengths[i]);
            }
            if(Ok(rc))rc=SQLExecute(m_stmt);
        }
        if(rc==SQL_NO_DATA){ResultSet set;set.affected=0;result.sets.push_back(set);}
        else if(!Ok(rc))result=Diagnostic(SQL_HANDLE_STMT,m_stmt);else result=Read(limit);
        FreeStatement();return result;
    }
    Result Catalog(){
        auto result=StatementHandle();if(!result.error.empty())return result;
        if(!Ok(SQLTablesW(m_stmt,nullptr,0,nullptr,0,nullptr,0,(SQLWCHAR*)L"'TABLE','VIEW'",SQL_NTS)))return Diagnostic(SQL_HANDLE_STMT,m_stmt);
        auto rows=Read(10000,false);FreeStatement();if(!rows.error.empty())return rows;
        for(const auto& r:rows.sets[0].rows)if(r.size()>=4){
            Table t{r[0].text.value_or(L""),r[1].text.value_or(L""),r[2].text.value_or(L""),r[3].text.value_or(L"")};
            if(!t.catalog.empty()&&std::find(result.databases.begin(),result.databases.end(),t.catalog)==result.databases.end())result.databases.push_back(t.catalog);
            result.tables.push_back(std::move(t));
        }
        std::wstring sql;
        switch(m_profile.engine){
        case Engine::PostgreSQL:sql=L"SELECT datname FROM pg_database WHERE datallowconn ORDER BY datname";break;
        case Engine::SqlServer:sql=L"SELECT name FROM sys.databases WHERE HAS_DBACCESS(name)=1 ORDER BY name";break;
        case Engine::MySQL:sql=L"SHOW DATABASES";break;default:break;
        }
        if(!sql.empty()&&m_auto&&!m_sqlTransaction){auto dbs=Query(sql,{},10000);if(dbs.lost)return dbs;if(dbs.error.empty()&&!dbs.sets.empty()){result.databases.clear();for(const auto& r:dbs.sets[0].rows)if(!r.empty()&&r[0].text)result.databases.push_back(*r[0].text);}}
        return result;
    }
    Result Describe(const Table& t){
        auto result=StatementHandle();if(!result.error.empty())return result;
        // SQLColumns takes patterns. Escape wildcard characters; filter returned identities as well.
        SQLWCHAR escape[8]{};SQLGetInfoW(m_db,SQL_SEARCH_PATTERN_ESCAPE,escape,sizeof(escape),nullptr);
        const wchar_t esc=escape[0]?(wchar_t)escape[0]:L'\\';
        const auto pattern=[&](const std::wstring& s){std::wstring p;for(auto c:s){if(c==L'%'||c==L'_'||c==esc)p+=esc;p+=c;}return p;};
        auto cat=pattern(t.catalog),schema=pattern(t.schema),name=pattern(t.name);
        if(!Ok(SQLColumnsW(m_stmt,cat.empty()?nullptr:W(cat),SQL_NTS,schema.empty()?nullptr:W(schema),SQL_NTS,W(name),SQL_NTS,nullptr,0)))return Diagnostic(SQL_HANDLE_STMT,m_stmt);
        auto info=Read(10000,false);FreeStatement();if(!info.error.empty())return info;
        ResultSet set;
        for(const auto& r:info.sets[0].rows)if(r.size()>=11&&r[2].text==t.name&&(t.schema.empty()||r[1].text==t.schema)){
            Column c;c.name=r[3].text.value_or(L"");c.sqlType=_wtoi(r[4].text.value_or(L"0").c_str());c.type=r[5].text.value_or(L"");
            c.size=_wtoi64(r[6].text.value_or(L"0").c_str());c.nullable=r[10].text!=L"0";set.columns.push_back(std::move(c));
        }
        result=StatementHandle();if(!result.error.empty())return result;
        if(Ok(SQLPrimaryKeysW(m_stmt,t.catalog.empty()?nullptr:W(t.catalog),SQL_NTS,t.schema.empty()?nullptr:W(t.schema),SQL_NTS,W(t.name),SQL_NTS))){
            auto keys=Read(10000,false);if(keys.error.empty()&&!keys.sets.empty())for(const auto& r:keys.sets[0].rows)if(r.size()>3)for(auto& c:set.columns)if(r[3].text==c.name)c.primaryKey=true;
        }
        FreeStatement();result.sets.push_back(std::move(set));return result;
    }
public:
    ~OdbcBackend(){Close();if(!m_profile.password.empty())SecureZeroMemory(m_profile.password.data(),m_profile.password.size()*2);}
    void Cancel() override {std::lock_guard lock(m_handle);if(m_stmt)SQLCancel(m_stmt);else if(m_db)SQLCancelHandle(SQL_HANDLE_DBC,m_db);}
    Result Open(const Profile& profile) override {
        Close();m_profile=profile;m_auto=true;
        if(profile.engine==Engine::PostgreSQL)for(const auto* field:{&profile.host,&profile.database,&profile.user}) {
            if(field->find_first_of(L";{}\r\n")!=std::wstring::npos || field->find(L'\0')!=std::wstring::npos) {
                Result r;r.error=L"psqlODBC cannot safely encode delimiters in host/database/user. Use a configured DSN for this identifier.";return r;
            }
        }

        if(profile.secretLocked){Result r;r.error=L"The saved password belongs to another Windows account. Enter it again.";return r;}
        if(profile.driver.empty()&&profile.dsn.empty()){Result r;r.error=L"Install a 64-bit ODBC driver for this engine, then choose it in the connection settings.";return r;}
        {std::lock_guard lock(m_handle);
        SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&m_env);
        if(!m_env){Result r;r.error=L"Could not initialize Windows ODBC.";return r;}
        SQLSetEnvAttr(m_env,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0);
        SQLAllocHandle(SQL_HANDLE_DBC,m_env,&m_db);}
        if(!m_db)return Diagnostic(SQL_HANDLE_ENV,m_env);
        SQLSetConnectAttr(m_db,SQL_LOGIN_TIMEOUT,(SQLPOINTER)10,0);
        SQLSetConnectAttr(m_db,SQL_ATTR_CONNECTION_TIMEOUT,(SQLPOINTER)10,0);
        if(profile.readOnly&&!Ok(SQLSetConnectAttr(m_db,SQL_ATTR_ACCESS_MODE,(SQLPOINTER)SQL_MODE_READ_ONLY,0))){auto r=Diagnostic(SQL_HANDLE_DBC,m_db);r.error=L"Driver cannot enforce read-only mode. "+r.error;return r;}
        auto connection=ConnectionString(profile);
        const auto rc=SQLDriverConnectW(m_db,nullptr,W(connection),SQL_NTS,nullptr,0,nullptr,SQL_DRIVER_NOPROMPT);
        SecureZeroMemory(connection.data(),connection.size()*2);
        if(!Ok(rc))return Diagnostic(SQL_HANDLE_DBC,m_db);m_connected=true;
        SQLWCHAR product[512]{};SQLGetInfoW(m_db,SQL_DBMS_NAME,product,sizeof(product),nullptr);
        Result r;r.information=(wchar_t*)product;return r;
    }
    Result Run(const Request& r) override {
        if(r.action==Action::Disconnect){Close();return {};}
        if(!m_connected){Result e;e.lost=true;e.error=L"Database is disconnected.";return e;}
        Result result;
        switch(r.action){
        case Action::Query:
            if(!m_auto)m_transaction=true;result=Query(r.sql,r.parameters,std::clamp(r.rowLimit,1,10000));
            if(result.error.empty()){m_sqlTransaction=SqlTransactionState(r.sql,m_sqlTransaction);if(!SqlTransactionState(r.sql,true))m_transaction=false;}
            else if(SqlTransactionState(r.sql,false,true))m_sqlTransaction=true;
            break;
        case Action::Catalog:if(!m_auto)m_transaction=true;result=Catalog();break;
        case Action::Describe:if(!m_auto)m_transaction=true;result=Describe(r.table);break;
        case Action::Health:{
            SQLUINTEGER dead=SQL_CD_FALSE;
            if(Ok(SQLGetConnectAttr(m_db,SQL_ATTR_CONNECTION_DEAD,&dead,0,nullptr))&&dead==SQL_CD_TRUE){result.lost=true;result.error=L"Connection is no longer available.";}
            else if(m_auto&&!m_sqlTransaction)result=Query(m_profile.engine==Engine::Oracle?L"SELECT 1 FROM DUAL":L"SELECT 1",{},1);
            break;}
        case Action::AutoCommit:
            if(m_transaction||m_sqlTransaction){result.error=L"Commit or rollback before changing auto-commit.";break;}
            if(!Ok(SQLSetConnectAttr(m_db,SQL_ATTR_AUTOCOMMIT,(SQLPOINTER)(uintptr_t)(r.flag?SQL_AUTOCOMMIT_ON:SQL_AUTOCOMMIT_OFF),0)))result=Diagnostic(SQL_HANDLE_DBC,m_db);
            else m_auto=r.flag;break;
        case Action::Commit:case Action::Rollback:
            if(m_auto&&m_sqlTransaction){result=Query(r.action==Action::Commit?L"COMMIT":L"ROLLBACK",{},1);if(result.error.empty())m_transaction=m_sqlTransaction=false;break;}
            if(!Ok(SQLEndTran(SQL_HANDLE_DBC,m_db,r.action==Action::Commit?SQL_COMMIT:SQL_ROLLBACK)))result=Diagnostic(SQL_HANDLE_DBC,m_db);
            else m_transaction=m_sqlTransaction=false;break;
        case Action::Edit:{
            if(m_profile.readOnly){result.error=L"This connection is read-only.";break;}
            // Preserve the user's transaction: a grid operation has its own savepoint.
            const bool own=m_auto&&!m_sqlTransaction;std::wstring save,rollback,release;
            if(!own){
                switch(m_profile.engine){
                case Engine::SqlServer:save=L"SAVE TRANSACTION ft_grid_edit";rollback=L"ROLLBACK TRANSACTION ft_grid_edit";break;
                case Engine::PostgreSQL:case Engine::MySQL:save=L"SAVEPOINT ft_grid_edit";rollback=L"ROLLBACK TO SAVEPOINT ft_grid_edit";release=L"RELEASE SAVEPOINT ft_grid_edit";break;
                case Engine::Oracle:save=L"SAVEPOINT ft_grid_edit";rollback=L"ROLLBACK TO ft_grid_edit";break;
                default:result.error=L"Grid editing in a manual transaction requires a supported savepoint dialect. Use SQL or auto-commit.";break;
                }
                if(!result.error.empty())break;
                if(m_profile.engine==Engine::SqlServer&&!m_transaction&&!m_sqlTransaction){result=Query(L"BEGIN TRANSACTION",{},1);if(!result.error.empty())break;}
                result=Query(save,{},1);if(!result.error.empty())break;
            }else if(!Ok(SQLSetConnectAttr(m_db,SQL_ATTR_AUTOCOMMIT,(SQLPOINTER)SQL_AUTOCOMMIT_OFF,0))){result=Diagnostic(SQL_HANDLE_DBC,m_db);break;}
            m_transaction=true;
            for(const auto& edit:r.edits){result=Query(edit.sql,edit.parameters,1);
                if(result.error.empty()&&edit.expectOne&&(result.sets.empty()||result.sets.back().affected!=1))result.error=L"The row changed or could not be uniquely verified. The grid operation was rolled back; refresh before editing.";
                if(!result.error.empty())break;
            }
            if(own){
                if(!Ok(SQLEndTran(SQL_HANDLE_DBC,m_db,result.error.empty()?SQL_COMMIT:SQL_ROLLBACK))){auto e=Diagnostic(SQL_HANDLE_DBC,m_db);result=e;}
                else m_transaction=false;
                if(!Ok(SQLSetConnectAttr(m_db,SQL_ATTR_AUTOCOMMIT,(SQLPOINTER)SQL_AUTOCOMMIT_ON,0))&&result.error.empty())result=Diagnostic(SQL_HANDLE_DBC,m_db);
            }else{
                if(!result.error.empty()){auto e=Query(rollback,{},1);if(!e.error.empty())result.error+=L"\nRollback failed: "+e.error;}
                if(!release.empty()){auto e=Query(release,{},1);if(result.error.empty())result=e;}
            }break;
        }
        default:break;
        }
        if(result.lost)m_connected=false;result.transaction=m_transaction||m_sqlTransaction;return result;
    }
};
std::unique_ptr<Backend> MakeOdbcBackend(){return std::make_unique<OdbcBackend>();}
} // namespace ft::db
