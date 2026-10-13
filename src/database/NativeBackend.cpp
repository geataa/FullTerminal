#include "database/NativeBackend.h"
#include "database/SqlSyntax.h"
#include <stdexcept>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>

namespace ft::db {
WindowsTrustBundle::~WindowsTrustBundle(){if(!path.empty())DeleteFileW(path.c_str());}
std::wstring WindowsTrustBundle::Create(){
    wchar_t temp[MAX_PATH+1]{},file[MAX_PATH+1]{};
    if(!GetTempPathW(MAX_PATH,temp)||!GetTempFileNameW(temp,L"ftc",0,file))return {};
    path=file;std::ofstream out(std::filesystem::path(path),std::ios::binary);
    HCERTSTORE store=CertOpenSystemStoreW(0,L"ROOT");if(!store)return {};
    PCCERT_CONTEXT cert=nullptr;size_t count=0;
    while((cert=CertEnumCertificatesInStore(store,cert))){DWORD n=0;
        if(CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,CRYPT_STRING_BASE64HEADER,nullptr,&n)){
            std::string text(n,0);if(CryptBinaryToStringA(cert->pbCertEncoded,cert->cbCertEncoded,CRYPT_STRING_BASE64HEADER,text.data(),&n)){while(!text.empty()&&!text.back())text.pop_back();out<<text;++count;}
        }
    }CertCloseStore(store,0);out.close();return count&&!out.fail()?path:L"";
}
bool NativeEngine(Engine e){return e==Engine::PostgreSQL||e==Engine::MySQL||e==Engine::SqlServer;}
Result NativeError(const wchar_t* message){Result r;r.error=message;return r;}
Value NativeText(const char* data,size_t length,size_t& bytes){
    Value v;if(!data)return v;
    // Bound both individual cells and retained result memory, not just rows.
    const auto n=std::min({length,size_t(131072),bytes<16*1024*1024?(16*1024*1024-bytes)/2:size_t(0)});v.text=Utf8ToWide({data,n});
    v.truncated=n<length;bytes+=v.text->size()*sizeof(wchar_t);return v;
}
std::wstring NumberParameters(const std::wstring& sql,const wchar_t* prefix,size_t expected){
    auto spans=HighlightSql(sql);std::wstring out;size_t count=0;
    for(const auto& s:spans)for(size_t i=s.start;i<s.start+s.length;++i){
        if(sql[i]==L'?'&&s.kind!=Syntax::String&&s.kind!=Syntax::Comment)out+=std::wstring(prefix)+std::to_wstring(++count);
        else out+=sql[i];
    }
    if(count!=expected)throw std::runtime_error("SQL parameter count mismatch.");return out;
}
Result NativeBackend::Finish(Result r){
    if(r.lost){connected=false;transaction=false;}
    r.transaction=transaction;
    if(!profile.password.empty())for(size_t p=0;(p=r.error.find(profile.password,p))!=r.error.npos;p+=3)r.error.replace(p,profile.password.size(),L"***");
    return r;
}
Result NativeBackend::Query(const std::wstring& sql,const std::vector<Parameter>& params,int limit){
    auto r=Execute(sql,params,std::clamp(limit,1,10000));transaction=r.transaction;return Finish(std::move(r));
}
static Parameter Param(const std::wstring& s){Parameter p;p.value.text=s;return p;}
Result NativeBackend::Catalog(){
    const bool pg=profile.engine==Engine::PostgreSQL,my=profile.engine==Engine::MySQL;
    auto r=Query(pg?L"SELECT current_database(),table_schema,table_name,table_type FROM information_schema.tables WHERE table_schema NOT IN ('pg_catalog','information_schema') ORDER BY table_schema,table_name":
        my?L"SELECT table_schema,table_schema,table_name,table_type FROM information_schema.tables WHERE table_schema=DATABASE() ORDER BY table_name":
        L"SELECT TABLE_CATALOG,TABLE_SCHEMA,TABLE_NAME,TABLE_TYPE FROM INFORMATION_SCHEMA.TABLES ORDER BY TABLE_SCHEMA,TABLE_NAME");
    if(!r.error.empty())return r;
    if(!r.sets.empty())for(const auto& row:r.sets[0].rows)if(row.size()>=4)r.tables.push_back({row[0].text.value_or(L""),row[1].text.value_or(L""),row[2].text.value_or(L""),row[3].text.value_or(L"")});
    if(!r.sets.empty()&&r.sets[0].limited)r.information=L"The object list is limited to 10,000 tables.";
    r.sets.clear();
    auto databases=Query(pg?L"SELECT datname FROM pg_database WHERE datallowconn AND has_database_privilege(datname,'CONNECT') ORDER BY datname":my?L"SHOW DATABASES":L"SELECT name FROM sys.databases WHERE HAS_DBACCESS(name)=1 ORDER BY name");
    if(databases.lost)return databases;
    if(databases.error.empty()&&!databases.sets.empty())for(const auto& row:databases.sets[0].rows)if(!row.empty()&&row[0].text)r.databases.push_back(*row[0].text);
    return r;
}
Result NativeBackend::Describe(const Table& t){
    std::wstring sql;
    if(profile.engine==Engine::PostgreSQL)sql=LR"(SELECT a.attname,format_type(a.atttypid,a.atttypmod),NOT a.attnotnull,
        EXISTS(SELECT 1 FROM pg_index i WHERE i.indrelid=c.oid AND i.indisprimary AND a.attnum=ANY(i.indkey)),
        (a.attidentity<>'' OR a.attgenerated<>'' OR COALESCE(pg_get_expr(d.adbin,d.adrelid),'') LIKE 'nextval(%')
        FROM pg_attribute a JOIN pg_class c ON c.oid=a.attrelid JOIN pg_namespace n ON n.oid=c.relnamespace
        LEFT JOIN pg_attrdef d ON d.adrelid=c.oid AND d.adnum=a.attnum
        WHERE n.nspname=? AND c.relname=? AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum)";
    else if(profile.engine==Engine::MySQL)sql=LR"(SELECT COLUMN_NAME,COLUMN_TYPE,IS_NULLABLE='YES',COLUMN_KEY='PRI',
        (EXTRA LIKE '%auto_increment%' OR EXTRA LIKE '%GENERATED%') FROM information_schema.columns
        WHERE TABLE_SCHEMA=? AND TABLE_NAME=? ORDER BY ORDINAL_POSITION)";
    else sql=LR"(SELECT c.name,TYPE_NAME(c.user_type_id),c.is_nullable,
        CASE WHEN EXISTS(SELECT 1 FROM sys.index_columns ic JOIN sys.indexes i ON i.object_id=ic.object_id AND i.index_id=ic.index_id
          WHERE ic.object_id=c.object_id AND ic.column_id=c.column_id AND i.is_primary_key=1) THEN 1 ELSE 0 END,
        CASE WHEN c.is_identity=1 OR c.is_computed=1 OR c.system_type_id=189 THEN 1 ELSE 0 END
        FROM sys.columns c JOIN sys.objects o ON o.object_id=c.object_id JOIN sys.schemas s ON s.schema_id=o.schema_id
        WHERE s.name=? AND o.name=? ORDER BY c.column_id)";
    auto r=Query(sql,{Param(t.schema.empty()?t.catalog:t.schema),Param(t.name)});
    if(!r.error.empty()||r.sets.empty())return r;
    ResultSet structure;for(const auto& row:r.sets[0].rows)if(row.size()>=5){
        auto yes=[&](size_t i){return row[i].text==L"1"||row[i].text==L"t";};
        Column c;c.name=row[0].text.value_or(L"");c.type=row[1].text.value_or(L"");c.nullable=yes(2);c.primaryKey=yes(3);c.generated=yes(4);structure.columns.push_back(std::move(c));
    }
    r.sets.clear();r.sets.push_back(std::move(structure));return r;
}
Result NativeBackend::Edit(const Request& r){
    if(profile.readOnly)return NativeError(L"This connection is read-only.");
    if(r.edits.empty())return {};
    const bool own=!transaction;const bool ms=profile.engine==Engine::SqlServer;
    const auto name=L"ft_grid_edit";
    auto result=Query(own?(profile.engine==Engine::MySQL?L"START TRANSACTION":L"BEGIN TRANSACTION"):ms?L"SAVE TRANSACTION ft_grid_edit":L"SAVEPOINT ft_grid_edit");
    if(!result.error.empty())return result;
    for(const auto& edit:r.edits){
        result=Query(edit.sql,edit.parameters,1);
        if(!result.error.empty())break;
        int64_t affected=0;for(const auto& set:result.sets)if(set.affected>0)affected+=set.affected;
        if(edit.expectOne&&affected!=1){result.error=L"The row changed on the server. No grid edits were applied; refresh the table.";break;}
    }
    if(!result.error.empty()){
        // Recover just our savepoint; never commit or discard the user's transaction.
        if(!result.lost){auto rollback=Query(own?L"ROLLBACK":ms?L"ROLLBACK TRANSACTION ft_grid_edit":L"ROLLBACK TO SAVEPOINT ft_grid_edit");
            if(!rollback.error.empty()){result.error+=L" Rollback failed: "+rollback.error;result.lost|=rollback.lost;}}
    }else if(own&&autoCommit)result=Query(L"COMMIT");
    if(!own&&!ms&&!result.lost){auto release=Query(L"RELEASE SAVEPOINT ft_grid_edit");if(result.error.empty()&&!release.error.empty())result=std::move(release);}
    return result;
}
Result NativeBackend::Run(const Request& r){
    if(r.action==Action::Disconnect){Close();connected=false;transaction=false;return {};}
    if(!connected){auto e=NativeError(L"Database is disconnected. Reconnect to continue.");e.lost=true;return e;}
    Result result;
    // Emulate manual commit consistently without toggling server-global state.
    if(!autoCommit&&!transaction&&(r.action==Action::Query||r.action==Action::Catalog||r.action==Action::Describe)){
        result=Query(profile.engine==Engine::MySQL?L"START TRANSACTION":L"BEGIN TRANSACTION");if(!result.error.empty())return result;
    }
    switch(r.action){
    case Action::Query:result=Query(r.sql,r.parameters,r.rowLimit);break;
    case Action::Catalog:result=Catalog();break;
    case Action::Describe:result=Describe(r.table);break;
    case Action::Edit:result=Edit(r);break;
    case Action::Health:if(!transaction)result=Query(L"SELECT 1",{},1);break;
    case Action::AutoCommit:if(transaction)result.error=L"Commit or rollback before changing auto-commit.";else autoCommit=r.flag;break;
    case Action::Commit:case Action::Rollback:if(transaction)result=Query(r.action==Action::Commit?L"COMMIT":L"ROLLBACK");break;
    default:break;
    }
    return Finish(std::move(result));
}
}
