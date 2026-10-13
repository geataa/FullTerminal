#include "database/NativeBackend.h"
#include "database/SqlSyntax.h"
#include <sybfront.h>
#include <sybdb.h>

namespace ft::db {
class SqlServerBackend final:public NativeBackend {
    DBPROCESS* db=nullptr;
    std::atomic<bool> cancel=false;
    std::wstring diagnostic;
    ULONGLONG deadline=0;
    static thread_local SqlServerBackend* current;
    static int ErrorHandler(DBPROCESS*,int,int,int,char* text,char*){if(current&&text){if(!current->diagnostic.empty())current->diagnostic+=L"\n";current->diagnostic+=Utf8ToWide(text);}return INT_CANCEL;}
    static int MessageHandler(DBPROCESS*,DBINT,int,int severity,char* text,char*,char*,int){if(current&&severity>=11&&text){if(!current->diagnostic.empty())current->diagnostic+=L"\n";current->diagnostic+=Utf8ToWide(text);}return 0;}
    static int CheckInterrupt(void* db){auto* self=(SqlServerBackend*)dbgetuserdata((DBPROCESS*)db);return self&&(self->cancel||GetTickCount64()>self->deadline);}
    static int HandleInterrupt(void*){return INT_CANCEL;}
    void Close() override {if(db)dbclose(db);db=nullptr;connected=false;transaction=false;}
    Result Error(){auto r=NativeError(diagnostic.empty()?L"SQL Server could not complete the operation.":diagnostic.c_str());r.lost=db&&dbdead(db);r.cancelled=cancel||GetTickCount64()>deadline;r.transaction=transaction;return r;}
    Result Raw(const std::wstring& sql,const std::vector<Parameter>& params,int limit){
        diagnostic.clear();Result r;size_t bytes=0;auto text=WideToUtf8(params.empty()?sql:NumberParameters(sql,L"@ft",params.size()));
        if(params.empty()){if(dbcmd(db,text.c_str())==FAIL||dbsqlexec(db)==FAIL)return Error();}
        else{
            std::string declarations;std::vector<std::string> data;for(size_t i=0;i<params.size();++i){
                if(i)declarations+=",";declarations+="@ft"+std::to_string(i+1)+" nvarchar(max)";
                data.push_back(params[i].value.text?WideToUtf8(*params[i].value.text):"");
            }
            // DB-Library represents a zero-length RPC string as NULL. Replace only
            // generated placeholder tokens with the constant N'' for empty text.
            if(std::any_of(params.begin(),params.end(),[](const auto& p){return p.value.text&&p.value.text->empty();})){
                auto rewritten=NumberParameters(sql,L"@ft",params.size());
                // Re-run the original token stream rather than replacing substrings
                // (a quoted identifier might itself contain "@ft1").
                auto spans=HighlightSql(sql);rewritten.clear();size_t n=0;
                for(const auto& span:spans)for(size_t i=span.start;i<span.start+span.length;++i){if(sql[i]==L'?'&&span.kind!=Syntax::String&&span.kind!=Syntax::Comment){const auto& p=params[n++];rewritten+=p.value.text&&p.value.text->empty()?L"N''":L"@ft"+std::to_wstring(n);}else rewritten+=sql[i];}
                text=WideToUtf8(rewritten);
            }
            if(dbrpcinit(db,"sp_executesql",0)==FAIL)return Error();
            auto bind=[&](const char* name,const std::string& value,bool null=false){return dbrpcparam(db,name,0,SYBNTEXT,-1,null?0:(DBINT)value.size(),null?nullptr:(BYTE*)value.data());};
            if(bind("@stmt",text)==FAIL||bind("@params",declarations)==FAIL)return Error();
            for(size_t i=0;i<data.size();++i)if(bind(("@ft"+std::to_string(i+1)).c_str(),data[i],!params[i].value.text||data[i].empty())==FAIL)return Error();
            if(dbrpcsend(db)==FAIL||dbsqlok(db)==FAIL)return Error();
        }
        int status;while((status=dbresults(db))!=NO_MORE_RESULTS){
            if(status==FAIL)return Error();ResultSet set;const bool keep=r.sets.size()<32;const int count=dbnumcols(db);
            if(keep)for(int i=1;i<=count;++i){Column c;c.name=Utf8ToWide(dbcolname(db,i));c.type=L"TDS "+std::to_wstring(dbcoltype(db,i));c.size=dbcollen(db,i);DBCOL info{};info.SizeOfStruct=sizeof(info);if(dbcolinfo(db,CI_REGULAR,i,0,&info)==SUCCEED){c.nullable=info.Null!=0;c.generated=info.Identity!=0;}set.columns.push_back(std::move(c));}
            int row;while((row=dbnextrow(db))!=NO_MORE_ROWS){
                if(row==FAIL)return Error();if(row!=REG_ROW)continue;
                if(cancel||GetTickCount64()>deadline){dbcancel(db);return Error();}
                if(!keep||(int)set.rows.size()>=limit||bytes>=16*1024*1024){set.limited=true;continue;}
                Row values;for(int i=1;i<=count;++i){Value v;auto* data=dbdata(db,i);const auto size=dbdatlen(db,i);auto type=dbcoltype(db,i);
                    if(data){
                        if(type==SYBBINARY||type==SYBVARBINARY||type==SYBIMAGE){v.binary=true;v.text=L"[BINARY]";}
                        else if(type==SYBCHAR||type==SYBVARCHAR||type==SYBTEXT||type==SYBNTEXT||type==SYBNVARCHAR)v=NativeText((char*)data,size,bytes);
                        else {char buffer[4096]{};const auto n=dbconvert(db,type,data,size,SYBCHAR,(BYTE*)buffer,sizeof(buffer));if(n<0)return Error();v=NativeText(buffer,n,bytes);}
                    }values.push_back(std::move(v));
                }set.rows.push_back(std::move(values));
            }
            if(dbiscount(db))set.affected=dbcount(db);if(keep)r.sets.push_back(std::move(set));else r.information=L"Only the first 32 result sets are displayed.";
        }
        if(!diagnostic.empty())return Error();return r;
    }
    Result Execute(const std::wstring& sql,const std::vector<Parameter>& params,int limit) override {
        current=this;cancel=false;deadline=GetTickCount64()+30000;auto r=Raw(sql,params,limit);
        if(!r.error.empty()&&!r.lost)dbcancel(db);
        if(!r.lost){auto state=Raw(L"SELECT @@TRANCOUNT",{},1);
            if(state.error.empty()&&!state.sets.empty()&&!state.sets[0].rows.empty())transaction=state.sets[0].rows[0][0].text!=L"0";
            else {r.lost=state.lost;if(r.error.empty())r.error=state.error;}}
        r.transaction=transaction;current=nullptr;return r;
    }
public:
    ~SqlServerBackend(){Close();}
    void Cancel() override {cancel=true;}
    Result Open(const Profile& p) override {
        Close();profile=p;autoCommit=true;cancel=false;current=this;deadline=GetTickCount64()+10000;diagnostic.clear();
        struct CurrentGuard{~CurrentGuard(){SqlServerBackend::current=nullptr;}} currentGuard;
        if(p.secretLocked)return NativeError(L"Enter the database password again on this Windows account.");
        if(p.readOnly)return NativeError(L"SQL Server requires a database login with read-only grants. Use that login and disable the client read-only option; application intent does not enforce permissions.");
        static std::once_flag once;static bool initialized=false;std::call_once(once,[]{initialized=dbinit()==SUCCEED;if(initialized){dberrhandle(ErrorHandler);dbmsghandle(MessageHandler);dbsetlogintime(10);dbsettime(30);}});
        if(!initialized)return NativeError(L"Could not initialize embedded FreeTDS.");
        auto* login=dblogin();if(!login)return NativeError(L"Could not allocate SQL Server login.");
        struct LoginGuard{LOGINREC* p;~LoginGuard(){dbloginfree(p);}} guard{login};
        DBSETLAPP(login,"FullTerminal");DBSETLCHARSET(login,"UTF-8");DBSETLVERSION(login,DBVERSION_74);
        auto user=WideToUtf8(p.user),database=WideToUtf8(p.database),host=WideToUtf8(p.host);auto password=WideToUtf8(p.password);
        if(p.integrated)DBSETLNETWORKAUTH(login,1);else {DBSETLUSER(login,user.c_str());DBSETLPWD(login,password.c_str());}
        SecureZeroMemory(password.data(),password.size());if(!database.empty())DBSETLDBNAME(login,database.c_str());
        DBSETLPORT(login,p.port);DBSETLENCRYPTION(login,p.encrypt?"require":"off");
        WindowsTrustBundle roots;const auto ca=p.caFile.empty()&&p.encrypt&&!p.trustCertificate?roots.Create():p.caFile;
        if(p.encrypt&&!p.trustCertificate&&ca.empty())return NativeError(L"Could not read Windows trusted certificates. Select a PEM CA file.");
        auto caUtf8=WideToUtf8(p.encrypt&&!p.trustCertificate?ca:L"");dbsetlname(login,caUtf8.c_str(),1007);
        db=dbopen(login,host.c_str());if(!db){auto r=Finish(Error());current=nullptr;return r;}
        dbsetuserdata(db,(BYTE*)this);dbsetinterrupt(db,CheckInterrupt,HandleInterrupt);
        connected=true;current=nullptr;
        auto r=Query(L"SET TEXTSIZE 2147483647; SET QUOTED_IDENTIFIER ON; SELECT CAST(SERVERPROPERTY('ProductVersion') AS varchar(64))",{},1);
        if(!r.error.empty()){Close();return r;}r.sets.clear();r.information=L"SQL Server · embedded FreeTDS · "+std::wstring(p.encrypt?L"TLS":L"encryption optional");return r;
    }
};
thread_local SqlServerBackend* SqlServerBackend::current=nullptr;
std::unique_ptr<Backend> MakeSqlServerBackend(){return std::make_unique<SqlServerBackend>();}
}
