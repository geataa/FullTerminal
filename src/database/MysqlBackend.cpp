#include <winsock2.h>
#include "database/NativeBackend.h"
#include <mysql.h>
#include <array>

namespace ft::db {
class MysqlBackend final:public NativeBackend {
    MYSQL* db=nullptr;
    std::atomic<bool> cancel=false;
    std::mutex socketMutex;
    SOCKET socket=INVALID_SOCKET;
    bool threadInitialized=false;
    void Close() override {
        {std::lock_guard lock(socketMutex);socket=INVALID_SOCKET;if(db)mysql_close(db);db=nullptr;}
        connected=false;transaction=false;
        if(threadInitialized){mysql_thread_end();threadInitialized=false;}
    }
    Result Error(MYSQL_STMT* stmt=nullptr){
        Result r;auto code=stmt?mysql_stmt_errno(stmt):mysql_errno(db);
        r.error=Utf8ToWide(stmt?mysql_stmt_error(stmt):mysql_error(db));r.sqlState=Utf8ToWide(stmt?mysql_stmt_sqlstate(stmt):mysql_sqlstate(db));
        r.lost=code==2006||code==2013||code==2055||cancel;r.cancelled=cancel||code==1317;
        if(r.error.empty())r.error=L"MySQL client could not complete the operation.";
        r.transaction=db&&(db->server_status&SERVER_STATUS_IN_TRANS);return r;
    }
    static bool Binary(const MYSQL_FIELD& f){return f.charsetnr==63&&(f.type==MYSQL_TYPE_BLOB||f.type==MYSQL_TYPE_LONG_BLOB||f.type==MYSQL_TYPE_MEDIUM_BLOB||f.type==MYSQL_TYPE_TINY_BLOB||f.type==MYSQL_TYPE_STRING||f.type==MYSQL_TYPE_VAR_STRING||f.type==MYSQL_TYPE_BIT||f.type==MYSQL_TYPE_GEOMETRY);}
    static void Columns(ResultSet& set,MYSQL_RES* meta){
        auto fields=mysql_fetch_fields(meta);for(unsigned i=0;i<mysql_num_fields(meta);++i){const auto& f=fields[i];Column c;c.name=Utf8ToWide({f.name,f.name_length});c.type=L"MySQL "+std::to_wstring(f.type);c.size=f.length;c.nullable=!(f.flags&NOT_NULL_FLAG);c.primaryKey=f.flags&PRI_KEY_FLAG;c.generated=f.flags&AUTO_INCREMENT_FLAG;c.sqlType=Binary(f)?-3:0;set.columns.push_back(std::move(c));}
    }
    Result Execute(const std::wstring& sql,const std::vector<Parameter>& params,int limit) override {
        cancel=false;const auto deadline=GetTickCount64()+30000;const auto query=WideToUtf8(sql);Result r;size_t bytes=0;
        auto expired=[&](){if(cancel)return true;if(GetTickCount64()>deadline){Cancel();return true;}return false;};
        if(params.empty()){
            if(mysql_real_query(db,query.data(),(unsigned long)query.size()))return Error();
            do{
                ResultSet set;auto* res=mysql_use_result(db);const bool keep=r.sets.size()<32;
                if(res){if(keep)Columns(set,res);MYSQL_ROW row;
                    while((row=mysql_fetch_row(res))){
                        if(expired())break;
                        if(keep&&(int)set.rows.size()<limit&&bytes<16*1024*1024){Row values;auto lengths=mysql_fetch_lengths(res);auto fields=mysql_fetch_fields(res);
                            for(unsigned i=0;i<mysql_num_fields(res);++i){Value v;if(row[i]){if(Binary(fields[i])){v.binary=true;v.text=L"[BINARY]";}else v=NativeText(row[i],lengths[i],bytes);}values.push_back(std::move(v));}set.rows.push_back(std::move(values));
                        }else set.limited=true;
                    }
                    if(mysql_errno(db)||cancel){auto e=Error();mysql_free_result(res);return e;}mysql_free_result(res);
                }else if(mysql_field_count(db))return Error();
                const auto count=mysql_affected_rows(db);if(count!=(my_ulonglong)-1)set.affected=(int64_t)count;
                if(keep)r.sets.push_back(std::move(set));else r.information=L"Only the first 32 result sets are displayed.";
                const auto next=mysql_next_result(db);if(next<0)break;if(next>0)return Error();
            }while(!expired());
        }else{
            auto* stmt=mysql_stmt_init(db);if(!stmt)return NativeError(L"Could not allocate a MySQL statement.");
            struct Guard{MYSQL_STMT* stmt;~Guard(){mysql_stmt_close(stmt);}}guard{stmt};
            if(mysql_stmt_prepare(stmt,query.data(),(unsigned long)query.size()))return Error(stmt);
            if(mysql_stmt_param_count(stmt)!=params.size())return NativeError(L"SQL parameter count mismatch.");
            std::vector<std::string> data(params.size());std::vector<MYSQL_BIND> binds(params.size());std::vector<unsigned long> lengths(params.size());std::vector<my_bool> nulls(params.size());
            for(size_t i=0;i<params.size();++i){data[i]=params[i].value.text?WideToUtf8(*params[i].value.text):"";lengths[i]=(unsigned long)data[i].size();nulls[i]=!params[i].value.text;auto& b=binds[i];b.buffer_type=MYSQL_TYPE_STRING;b.buffer=data[i].data();b.buffer_length=lengths[i];b.length=&lengths[i];b.is_null=&nulls[i];}
            if(mysql_stmt_bind_param(stmt,binds.data())||mysql_stmt_execute(stmt))return Error(stmt);
            do{
                ResultSet set;auto* meta=mysql_stmt_result_metadata(stmt);const bool keep=r.sets.size()<32;
                if(meta){const auto n=mysql_num_fields(meta);if(keep)Columns(set,meta);
                    std::vector<std::array<char,4096>> buffers(n);std::vector<MYSQL_BIND> output(n);std::vector<unsigned long> sizes(n);std::vector<my_bool> isNull(n),truncated(n);
                    for(unsigned i=0;i<n;++i){auto& b=output[i];b.buffer_type=MYSQL_TYPE_STRING;b.buffer=buffers[i].data();b.buffer_length=(unsigned long)buffers[i].size();b.length=&sizes[i];b.is_null=&isNull[i];b.error=&truncated[i];}
                    if(mysql_stmt_bind_result(stmt,output.data())){mysql_free_result(meta);return Error(stmt);}
                    int code;while((code=mysql_stmt_fetch(stmt))!=MYSQL_NO_DATA){
                        if(code!=0&&code!=MYSQL_DATA_TRUNCATED){mysql_free_result(meta);return Error(stmt);}if(expired())break;
                        if(keep&&(int)set.rows.size()<limit&&bytes<16*1024*1024){Row row;auto fields=mysql_fetch_fields(meta);
                            for(unsigned i=0;i<n;++i){Value v;if(!isNull[i]){
                                if(Binary(fields[i])){v.binary=true;v.text=L"[BINARY]";}
                                else if(sizes[i]<=buffers[i].size())v=NativeText(buffers[i].data(),sizes[i],bytes);
                                else {std::string text(std::min<unsigned long>(sizes[i],131072),0);auto b=output[i];b.buffer=text.data();b.buffer_length=(unsigned long)text.size();
                                    if(mysql_stmt_fetch_column(stmt,&b,i,0)){mysql_free_result(meta);return Error(stmt);}v=NativeText(text.data(),text.size(),bytes);v.truncated|=sizes[i]>text.size();}
                            }row.push_back(std::move(v));}set.rows.push_back(std::move(row));
                        }else set.limited=true;
                    }mysql_free_result(meta);if(cancel)return Error(stmt);
                }
                const auto count=mysql_stmt_affected_rows(stmt);if(count!=(my_ulonglong)-1)set.affected=(int64_t)count;
                if(keep)r.sets.push_back(std::move(set));else r.information=L"Only the first 32 result sets are displayed.";
                auto next=mysql_stmt_next_result(stmt);if(next<0)break;if(next>0)return Error(stmt);
            }while(!expired());
        }
        if(cancel)return Error();r.transaction=(db->server_status&SERVER_STATUS_IN_TRANS)!=0;return r;
    }
public:
    ~MysqlBackend(){Close();}
    void Cancel() override {cancel=true;std::lock_guard lock(socketMutex);if(socket!=INVALID_SOCKET)shutdown(socket,SD_BOTH);}
    Result Open(const Profile& p) override {
        Close();profile=p;autoCommit=true;cancel=false;
        if(p.secretLocked)return NativeError(L"Enter the database password again on this Windows account.");
        static std::once_flag once;static bool initialized=false;std::call_once(once,[]{initialized=mysql_library_init(0,nullptr,nullptr)==0;});
        if(!initialized)return NativeError(L"Could not initialize the embedded MariaDB client.");
        if(mysql_thread_init())return NativeError(L"Could not initialize MySQL client thread.");threadInitialized=true;
        db=mysql_init(nullptr);if(!db)return NativeError(L"Could not initialize the embedded MariaDB client.");
        unsigned connect=10,query=30,localFiles=0;my_bool tls=p.encrypt,verify=p.encrypt&&!p.trustCertificate,reconnect=0;unsigned protocol=MYSQL_PROTOCOL_TCP;
        // Connector/C applies these socket timeouts during connect. Changing
        // mysql_options afterwards would leave the live socket on its old limit.
        mysql_options(db,MYSQL_OPT_CONNECT_TIMEOUT,&connect);mysql_options(db,MYSQL_OPT_READ_TIMEOUT,&query);mysql_options(db,MYSQL_OPT_WRITE_TIMEOUT,&query);
        mysql_options(db,MYSQL_OPT_LOCAL_INFILE,&localFiles);
        mysql_options(db,MYSQL_OPT_PROTOCOL,&protocol);mysql_options(db,MYSQL_OPT_RECONNECT,&reconnect);mysql_options(db,MYSQL_SET_CHARSET_NAME,"utf8mb4");
        mysql_options(db,MYSQL_OPT_SSL_ENFORCE,&tls);mysql_options(db,MYSQL_OPT_SSL_VERIFY_SERVER_CERT,&verify);
        const auto ca=WideToUtf8(p.caFile);if(!ca.empty())mysql_ssl_set(db,nullptr,nullptr,ca.c_str(),nullptr,nullptr);
        const auto host=WideToUtf8(p.host),user=WideToUtf8(p.user),database=WideToUtf8(p.database);auto password=WideToUtf8(p.password);
        const bool ok=mysql_real_connect(db,host.c_str(),user.c_str(),password.c_str(),database.empty()?nullptr:database.c_str(),p.port,nullptr,CLIENT_MULTI_STATEMENTS|CLIENT_MULTI_RESULTS|CLIENT_FOUND_ROWS)!=nullptr;
        SecureZeroMemory(password.data(),password.size());if(!ok){auto r=Finish(Error());Close();return r;}
        {std::lock_guard lock(socketMutex);socket=(SOCKET)mysql_get_socket(db);}
        if(cancel){Close();return NativeError(L"Connection cancelled.");}
        if(p.encrypt&&!mysql_get_ssl_cipher(db)){Close();return NativeError(L"The server did not negotiate the required TLS connection.");}
        connected=true;if(p.readOnly){auto r=Query(L"SET SESSION TRANSACTION READ ONLY");if(!r.error.empty()){Close();return r;}}
        Result r;r.information=L"MySQL/MariaDB "+Utf8ToWide(mysql_get_server_info(db))+L" · embedded Connector/C · "+(mysql_get_ssl_cipher(db)?L"TLS":L"unencrypted");return r;
    }
};
std::unique_ptr<Backend> MakeMysqlBackend(){return std::make_unique<MysqlBackend>();}
}
