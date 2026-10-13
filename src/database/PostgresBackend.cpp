#include "database/NativeBackend.h"
#include <libpq-fe.h>

namespace ft::db {
class PostgresBackend final:public NativeBackend {
    PGconn* db=nullptr;
    std::atomic<bool> cancel=false;
    void Close() override {if(db)PQfinish(db);db=nullptr;connected=false;transaction=false;}
    Result Error(PGresult* res=nullptr){
        Result r;r.error=Utf8ToWide(res?PQresultErrorMessage(res):db?PQerrorMessage(db):"Could not initialize libpq.");
        if(res)if(auto state=PQresultErrorField(res,PG_DIAG_SQLSTATE))r.sqlState=Utf8ToWide(state);
        r.lost=db&&PQstatus(db)==CONNECTION_BAD;r.cancelled=r.sqlState==L"57014";
        if(db){const auto st=PQtransactionStatus(db);r.transaction=st==PQTRANS_INTRANS||st==PQTRANS_INERROR;}
        return r;
    }
    bool SendCancel(){
        auto* c=PQcancelCreate(db);if(!c)return false;
        bool ok=false;if(PQcancelStart(c)){auto until=GetTickCount64()+3000;
            while(GetTickCount64()<until){auto st=PQcancelPoll(c);if(st==PGRES_POLLING_OK){ok=true;break;}if(st==PGRES_POLLING_FAILED)break;Sleep(10);}}
        PQcancelFinish(c);return ok;
    }
    Result Execute(const std::wstring& sql,const std::vector<Parameter>& params,int limit) override {
        cancel=false;auto bytesSql=WideToUtf8(params.empty()?sql:NumberParameters(sql,L"$",params.size()));
        std::vector<std::string> data;std::vector<const char*> values;
        for(const auto& p:params)data.push_back(p.value.text?WideToUtf8(*p.value.text):"");
        for(size_t i=0;i<params.size();++i)values.push_back(params[i].value.text?data[i].c_str():nullptr);
        const bool sent=params.empty()?PQsendQuery(db,bytesSql.c_str()):PQsendQueryParams(db,bytesSql.c_str(),(int)params.size(),nullptr,values.data(),nullptr,nullptr,0);
        if(!sent)return Error();PQsetSingleRowMode(db);
        Result result;ResultSet set;size_t bytes=0;bool started=false,cancelling=false;auto deadline=GetTickCount64()+30000;
        auto interrupt=[&](){
            if(!cancelling&&(cancel||GetTickCount64()>deadline)){
                cancelling=true;result.cancelled=true;result.error=cancel?L"Query cancelled.":L"Query exceeded the 30 second limit.";
                if(!SendCancel())return false;deadline=GetTickCount64()+3000;
            }return !cancelling||GetTickCount64()<deadline;
        };
        for(;;){
            if(!interrupt()){Close();result.lost=true;return result;}
            if(PQflush(db)<0)return Error();
            if(!PQconsumeInput(db)){auto e=Error();e.cancelled=cancelling;return e;}
            if(PQisBusy(db)){Sleep(5);continue;}
            auto* res=PQgetResult(db);if(!res)break;
            const auto status=PQresultStatus(res);
            if(status==PGRES_SINGLE_TUPLE||status==PGRES_TUPLES_OK||status==PGRES_COMMAND_OK){
                const bool keep=result.sets.size()<32;
                if(!started){started=true;set={};if(keep)for(int i=0;i<PQnfields(res);++i){Column c;c.name=Utf8ToWide(PQfname(res,i));c.type=L"OID "+std::to_wstring(PQftype(res,i));c.sqlType=PQftype(res,i)==17?-3:0;c.size=PQfsize(res,i);set.columns.push_back(std::move(c));}}
                if(status==PGRES_SINGLE_TUPLE){
                    if(keep&&(int)set.rows.size()<limit&&bytes<16*1024*1024){Row row;for(int i=0;i<PQnfields(res);++i){Value v;if(!PQgetisnull(res,0,i)){if(PQftype(res,i)==17){v.binary=true;v.text=L"[BINARY]";}else v=NativeText(PQgetvalue(res,0,i),PQgetlength(res,0,i),bytes);}row.push_back(std::move(v));}set.rows.push_back(std::move(row));}
                    else set.limited=true;
                }else{
                    auto count=PQcmdTuples(res);if(count&&*count)set.affected=_strtoi64(count,nullptr,10);
                    if(keep)result.sets.push_back(std::move(set));else result.information=L"Only the first 32 result sets are displayed.";started=false;
                }
            }else if(status==PGRES_COPY_IN||status==PGRES_COPY_OUT||status==PGRES_COPY_BOTH){
                PQclear(res);Close();result.error=L"Streaming COPY is not supported in the SQL grid. The connection was closed; reconnect to continue.";result.lost=true;return result;
            }else if(status==PGRES_FATAL_ERROR||status==PGRES_BAD_RESPONSE){auto e=Error(res);result.error=e.error;result.sqlState=e.sqlState;result.lost=e.lost;result.cancelled|=e.cancelled;}
            PQclear(res);
        }
        const auto st=PQtransactionStatus(db);result.transaction=st==PQTRANS_INTRANS||st==PQTRANS_INERROR;result.lost|=PQstatus(db)==CONNECTION_BAD;return result;
    }
public:
    ~PostgresBackend(){Close();}
    void Cancel() override {cancel=true;}
    Result Open(const Profile& p) override {
        Close();profile=p;autoCommit=true;cancel=false;
        if(p.secretLocked)return NativeError(L"Enter the database password again on this Windows account.");
        WindowsTrustBundle roots;const auto ca=p.caFile.empty()&&p.encrypt&&!p.trustCertificate?roots.Create():p.caFile;
        if(p.encrypt&&!p.trustCertificate&&ca.empty())return NativeError(L"Could not read Windows trusted certificates. Select a PEM CA file.");
        std::vector<std::string> data{WideToUtf8(p.host),std::to_string(p.port),WideToUtf8(p.database),WideToUtf8(p.user),WideToUtf8(p.password),
            p.encrypt?(p.trustCertificate?"require":"verify-full"):"disable",WideToUtf8(ca),"10","UTF8","FullTerminal","disable"};
        const char* keys[]={"host","port","dbname","user","password","sslmode","sslrootcert","connect_timeout","client_encoding","application_name","gssencmode",nullptr};
        std::vector<const char*> values;for(const auto& v:data)values.push_back(v.c_str());values.push_back(nullptr);
        db=PQconnectStartParams(keys,values.data(),0);SecureZeroMemory(data[4].data(),data[4].size());
        if(!db)return Error();auto deadline=GetTickCount64()+10000;
        for(;;){auto st=PQconnectPoll(db);if(st==PGRES_POLLING_OK)break;
            if(st==PGRES_POLLING_FAILED){auto r=Finish(Error());Close();return r;}
            if(cancel||GetTickCount64()>deadline){Close();auto r=NativeError(cancel?L"Connection cancelled.":L"Connection timed out (10 seconds).");r.cancelled=cancel;return r;}Sleep(5);}
        if(PQsetnonblocking(db,1)!=0){auto r=Finish(Error());Close();return r;}
        connected=true;
        if(p.readOnly){auto r=Query(L"SET default_transaction_read_only=on");if(!r.error.empty()){Close();return r;}}
        Result r;r.information=L"PostgreSQL "+std::to_wstring(PQserverVersion(db))+L" · embedded libpq · "+(PQsslInUse(db)?L"TLS":L"unencrypted");return r;
    }
};
std::unique_ptr<Backend> MakePostgresBackend(){return std::make_unique<PostgresBackend>();}
}
