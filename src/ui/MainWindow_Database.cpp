#include "ui/MainWindowInternal.h"
#include <filesystem>
#include <fstream>
#include <limits>

namespace ft {
namespace {

std::wstring PickDbFile(HWND owner,bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* suggested=L""){
    wchar_t file[32768]{};wcsncpy_s(file,suggested,_TRUNCATE);OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner=owner;ofn.lpstrFilter=filter;ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrDefExt=extension;
    ofn.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?file:L"";
}
bool WriteDbFile(const std::wstring& path,const std::wstring& text){
    std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);const auto bytes=WideToUtf8(text);out.write(bytes.data(),bytes.size());out.close();return !out.fail();
}
const wchar_t* DbStatus(const db::Session& s){
    if(s.state==db::State::Lost)return TrText(L"Connection lost",L"Bağlantı koptu");
    if(s.state==db::State::Connecting)return TrText(L"Connecting...",L"Bağlanıyor...");
    if(s.busy)return TrText(L"Working...",L"Çalışıyor...");
    if(s.state==db::State::Connected)return TrText(L"Connected",L"Bağlı");
    return TrText(L"Disconnected",L"Bağlı değil");
}
// Wrapping toolbar keeps actions usable in normal, maximized and drop-down windows.
struct Toolbar {
    Ui& ui;float scale,left,right,x,y;int next;
    bool Button(const std::wstring& text,float width,bool primary=false,bool enabled=true,bool danger=false){
        const float w=width*scale;if(x+w>right&&x>left){x=left;y+=32*scale;}
        auto r=D2D1::RectF(x,y,std::min(right,x+w),y+27*scale);x+=w+5*scale;
        return ui.Button(next++,r,text,primary,danger,enabled);
    }
    float Bottom()const{return y+34*scale;}
};
}
void MainWindow::NewDatabaseTab(const db::Profile& profile){
    if(!m_r.Ready())return;
    auto tab=std::make_shared<TerminalTab>();tab->StartDatabase(profile);
    m_tabs.push_back(tab);m_tabLayouts.push_back(std::make_unique<PaneLayout>(tab));m_active=m_tabs.size()-1;
    m_ui.SetFocus(0);ClearSelection();ComputeLayout();SetView(View::Terminal);m_dirty=true;SaveSession();
}
void MainWindow::DrawDatabaseConnections(const D2D1_RECT_F& a){
    if(!m_dbLoaded){m_dbLoaded=true;db::LoadProfiles(m_dataDir,m_dbProfiles,m_dbError);m_dbDrivers=db::InstalledDrivers();}
    const float s=m_lay.scale,x=a.left+22*s,right=a.right-22*s;float y=a.top+14*s;
    m_r.Text(TrText(L"Databases",L"Veritabanları"),D2D1::RectF(x,y,right,y+34*s),theme::TextHi,23*s,Renderer::Align::Left,true);y+=40*s;
    m_r.Text(TrText(L"One connection per tab. Tables and SQL editors stay inside it.",L"Her ana sekme bir bağlantı. Tablolar ve SQL editörleri onun içinde."),D2D1::RectF(x,y,right,y+24*s),theme::TextMuted,12*s);y+=32*s;
    if(!m_dbEditing){
        Toolbar bar{m_ui,s,x,right,x,y,36100};
        if(bar.Button(TrText(L"+ Connection",L"+ Bağlantı"),130,true)){m_dbDraft={};m_dbDraft.name=L"PostgreSQL";m_dbDraft.driver=db::DefaultDriver(m_dbDraft.engine,m_dbDrivers);m_dbEditing=true;m_ui.SetFocus(0);return;}
        if(bar.Button(TrText(L"Open SQLite",L"SQLite aç"),120)){
            const auto file=PickDbFile(m_hwnd,false,L"SQLite\0*.db;*.sqlite;*.sqlite3\0All files\0*.*\0",L"db");
            if(!file.empty()){m_dbDraft={};m_dbDraft.engine=db::Engine::SQLite;m_dbDraft.database=file;m_dbDraft.name=std::filesystem::path(file).stem().wstring();m_dbEditing=true;}return;
        }
        if(bar.Button(TrText(L"New SQLite file",L"SQLite oluştur"),140)){
            const auto file=PickDbFile(m_hwnd,true,L"SQLite\0*.db\0",L"db",L"database.db");
            if(!file.empty()){m_dbDraft={};m_dbDraft.engine=db::Engine::SQLite;m_dbDraft.database=file;m_dbDraft.name=std::filesystem::path(file).stem().wstring();m_dbEditing=true;}return;
        }
        y=bar.Bottom()+8*s;
        if(m_dbProfiles.empty())m_r.Text(TrText(L"Add a server connection, or open a local SQLite database to begin.",L"Başlamak için sunucu bağlantısı ekle veya yerel bir SQLite dosyası aç."),D2D1::RectF(x,y,right,y+50*s),theme::TextMuted,13*s);
        const int visible=std::max(1,(int)((a.bottom-y-70*s)/(76*s)));m_dbScroll=std::clamp(m_dbScroll,0,std::max(0,(int)m_dbProfiles.size()-visible));
        if(m_ui.Hot(a)&&m_in.wheel)m_dbScroll=std::clamp(m_dbScroll-m_in.wheel*2,0,std::max(0,(int)m_dbProfiles.size()-visible));
        for(int i=m_dbScroll;i<(int)m_dbProfiles.size()&&i<m_dbScroll+visible;++i){
            const auto& p=m_dbProfiles[i];const auto card=D2D1::RectF(x,y,right,y+66*s);m_r.FillRound(card,8*s,theme::Surface);
            m_r.Text(p.name,D2D1::RectF(x+14*s,y+6*s,right-194*s,y+30*s),theme::TextHi,14*s,Renderer::Align::Left,true);
            m_r.Text(db::EngineName(p.engine)+L"   "+(p.engine==db::Engine::SQLite?p.database:p.host+L" / "+p.database),D2D1::RectF(x+14*s,y+32*s,right-194*s,y+58*s),theme::TextMuted,11*s);
            if(m_ui.Button(36200+i*2,D2D1::RectF(right-184*s,y+18*s,right-94*s,y+47*s),TrText(L"Connect",L"Bağlan"),true)){NewDatabaseTab(p);return;}
            if(m_ui.Button(36201+i*2,D2D1::RectF(right-86*s,y+18*s,right-10*s,y+47*s),TrText(L"Edit",L"Düzenle"))){m_dbDraft=p;m_dbEditing=true;m_ui.SetFocus(0);return;}
            y+=76*s;
        }
    }else{
        const float top=y;const float bottom=a.bottom-62*s;const float contentH=(m_dbDraft.engine==db::Engine::SQLite?250.f:720.f)*s;
        m_mainScroll=std::clamp(m_mainScroll,0.f,std::max(0.f,contentH-(bottom-top)));
        y-=m_mainScroll;m_r.PushClip(D2D1::RectF(x,top,right,bottom));
        const float oldX=m_in.mx,oldPX=m_in.px;if(m_in.my<top||m_in.my>bottom||m_in.py<top||m_in.py>bottom){m_in.mx=m_in.px=-100000;}
        auto field=[&](int id,const wchar_t* label,std::wstring& value,bool secret=false){
            m_r.Text(label,D2D1::RectF(x,y,right,y+20*s),theme::TextMuted,11*s);y+=21*s;
            m_ui.Field(id,D2D1::RectF(x,y,std::min(right,x+600*s),y+30*s),value,L"",secret);y+=41*s;
        };
        field(36300,TrText(L"Connection name",L"Bağlantı adı"),m_dbDraft.name);
        const db::Engine choices[]={db::Engine::SQLite,db::Engine::PostgreSQL,db::Engine::MySQL,db::Engine::SqlServer,db::Engine::Odbc};
        std::vector<std::wstring> engines;int engine=4;for(int e=0;e<5;++e){engines.push_back(db::EngineName(choices[e]));if(choices[e]==m_dbDraft.engine)engine=e;}
        if(m_ui.Choice(36301,D2D1::RectF(x,y,std::min(right,x+600*s),y+30*s),engines,engine)){
            m_dbDraft.engine=choices[engine];m_dbDraft.driver=db::DefaultDriver(m_dbDraft.engine,m_dbDrivers);m_dbDraft.dsn.clear();
            m_dbDraft.port=engine==1?5432:engine==2?3306:engine==3?1433:engine==4?1521:0;
        }y+=42*s;
        if(m_dbDraft.engine!=db::Engine::SQLite){
            field(36302,TrText(L"Host",L"Sunucu"),m_dbDraft.host);
            m_r.Text(L"Port",D2D1::RectF(x,y,x+80*s,y+30*s),theme::TextMuted,11*s);
            m_ui.NumField(36303,D2D1::RectF(x+84*s,y,x+230*s,y+30*s),m_dbDraft.port,0,65535);y+=40*s;
            field(36304,TrText(L"Database",L"Veritabanı"),m_dbDraft.database);
            field(36305,TrText(L"User",L"Kullanıcı"),m_dbDraft.user);
            if(m_dbDraft.secretLocked)m_r.Text(TrText(L"Password must be re-entered on this Windows account.",L"Bu Windows hesabında parola yeniden girilmeli."),D2D1::RectF(x,y,right,y+22*s),theme::Amber,11*s);
            auto before=m_dbDraft.password;field(36306,TrText(L"Password (Windows encrypted storage)",L"Parola (Windows ile şifreli kayıt)"),m_dbDraft.password,true);
            if(before!=m_dbDraft.password){m_dbDraft.secretLocked=false;m_dbDraft.protectedPassword.clear();}
            if(m_dbDraft.engine==db::Engine::Odbc||m_dbDraft.engine==db::Engine::Oracle){
            m_r.Text(TrText(L"64-bit ODBC driver",L"64 bit ODBC sürücüsü"),D2D1::RectF(x,y,right,y+20*s),theme::TextMuted,11*s);y+=21*s;
            if(m_ui.Button(36307,D2D1::RectF(x,y,std::min(right,x+600*s),y+30*s),m_dbDraft.driver.empty()?TrText(L"Select installed driver",L"Kurulu sürücü seç"):m_dbDraft.driver)){
                m_dbDrivers=db::InstalledDrivers();POINT pt{(LONG)x,(LONG)(y+30*s)};ClientToScreen(m_hwnd,&pt);int n=ShowListMenu(pt,m_dbDrivers);if(n>=0)m_dbDraft.driver=m_dbDrivers[n];else if(m_dbDrivers.empty())Toast(TrText(L"No 64-bit ODBC drivers installed.",L"Kurulu 64 bit ODBC sürücüsü yok."));
            }y+=42*s;
            field(36308,L"DSN (optional)",m_dbDraft.dsn);
            }else{m_r.Text(TrText(L"Built-in client · no driver installation needed",L"Gömülü istemci · sürücü kurulumu gerektirmez"),D2D1::RectF(x,y,right,y+28*s),theme::AcHi(),12*s);y+=36*s;}
            if(m_dbDraft.engine==db::Engine::SqlServer){m_ui.Check(36309,D2D1::RectF(x,y,right,y+26*s),m_dbDraft.integrated,TrText(L"Windows authentication",L"Windows kimlik doğrulaması"));y+=30*s;}
            m_ui.Check(36310,D2D1::RectF(x,y,right,y+26*s),m_dbDraft.encrypt,TrText(L"Encrypt connection (TLS)",L"Bağlantıyı şifrele (TLS)"));y+=30*s;
            m_ui.Check(36311,D2D1::RectF(x,y,right,y+26*s),m_dbDraft.trustCertificate,TrText(L"Trust server certificate without verification",L"Sunucu sertifikasına doğrulamadan güven"));y+=30*s;
            if(m_dbDraft.engine!=db::Engine::Odbc&&m_dbDraft.engine!=db::Engine::Oracle)field(36314,TrText(L"CA certificate PEM file (optional; Windows roots by default)",L"CA sertifikası PEM dosyası (isteğe bağlı; varsayılan Windows kökleri)"),m_dbDraft.caFile);
        }else field(36312,TrText(L"SQLite file (or :memory:)",L"SQLite dosyası (veya :memory:)"),m_dbDraft.database);
        m_ui.Check(36313,D2D1::RectF(x,y,right,y+26*s),m_dbDraft.readOnly,TrText(L"Read-only connection",L"Salt okunur bağlantı"));
        m_in.mx=oldX;m_in.px=oldPX;m_r.PopClip();
        Toolbar bar{m_ui,s,x,right,x,a.bottom-50*s,36320};
        auto save=[&](bool connect){
            const bool external=m_dbDraft.engine==db::Engine::Odbc||m_dbDraft.engine==db::Engine::Oracle;
            if(m_dbDraft.name.empty()||(external&&m_dbDraft.driver.empty()&&m_dbDraft.dsn.empty())){m_dbError=TrText(L"A connection name is required. Generic ODBC also needs a driver or DSN.",L"Bağlantı adı gerekli. Genel ODBC için ayrıca sürücü veya DSN seçilmeli.");return;}
            if(!external&&m_dbDraft.engine!=db::Engine::SQLite&&(m_dbDraft.host.empty()||m_dbDraft.port<1)){m_dbError=TrText(L"Host and a valid port are required.",L"Sunucu ve geçerli bir port gerekli.");return;}
            auto profiles=m_dbProfiles;if(m_dbDraft.id.empty())m_dbDraft.id=Inventory::NewId();bool found=false;
            for(auto& p:profiles)if(p.id==m_dbDraft.id){p=m_dbDraft;found=true;break;}if(!found)profiles.push_back(m_dbDraft);
            if(!db::SaveProfiles(m_dataDir,profiles,m_dbError))return;
            m_dbProfiles=std::move(profiles);m_dbEditing=false;m_mainScroll=0;m_dbError.clear();m_ui.SetFocus(0);
            if(connect)NewDatabaseTab(m_dbDraft);
        };
        if(bar.Button(TrText(L"Save & connect",L"Kaydet ve bağlan"),148,true)){save(true);return;}
        if(bar.Button(TrText(L"Save",L"Kaydet"),74)){save(false);return;}
        if(bar.Button(TrText(L"Cancel",L"Vazgeç"),76)){m_dbEditing=false;m_mainScroll=0;m_ui.SetFocus(0);return;}
        if(!m_dbDraft.id.empty()&&bar.Button(TrText(L"Delete profile",L"Profili sil"),120,false,true,true)){
            if(MessageBoxW(m_hwnd,TrText(L"Delete the saved connection profile? Open connections stay open.",L"Kayıtlı bağlantı profili silinsin mi? Açık bağlantılar açık kalır."),L"FullTerminal",MB_OKCANCEL|MB_ICONQUESTION)==IDOK){auto profiles=m_dbProfiles;std::erase_if(profiles,[&](const auto& p){return p.id==m_dbDraft.id;});if(db::SaveProfiles(m_dataDir,profiles,m_dbError)){m_dbProfiles=std::move(profiles);m_dbEditing=false;}}return;
        }
    }
    if(!m_dbError.empty())m_r.Text(m_dbError,D2D1::RectF(x,a.bottom-82*s,right,a.bottom-56*s),theme::Red,11*s);
}

void MainWindow::DatabaseExecute(bool selection){
    auto* tab=Active();if(!tab||!tab->Database())return;auto& session=*tab->Database();auto* d=session.Active();if(!d)return;
    if(d->tableView){session.LoadTable(d->id);return;}
    session.Execute(d->id,selection?m_ui.SelectedText(d->editorId,d->sql):L"");m_dirty=true;
}
void MainWindow::DatabaseFile(bool save){
    auto* tab=Active();if(!tab||!tab->Database())return;auto& session=*tab->Database();auto* d=session.Active();if(!d)return;
    const auto file=PickDbFile(m_hwnd,save,L"SQL scripts\0*.sql\0All files\0*.*\0",L"sql",save?L"query.sql":L"");if(file.empty())return;
    if(save){if(WriteDbFile(file,d->sql)){d->savedSql=d->sql;Toast(TrText(L"SQL saved",L"SQL kaydedildi"));}else Toast(TrText(L"Could not save SQL file",L"SQL dosyası kaydedilemedi"));}
    else{
        std::error_code ec;const auto size=std::filesystem::file_size(file,ec);if(ec||size>1024*1024){Toast(TrText(L"SQL file could not be read (maximum 1 MiB).",L"SQL dosyası okunamadı (en fazla 1 MiB)."));return;}
        std::ifstream f(std::filesystem::path(file),std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(f)),{});
        if(bytes.starts_with("\xEF\xBB\xBF"))bytes.erase(0,3);session.NewEditor(Utf8ToWide(bytes));m_ui.SetFocus(0);
    }
}
bool MainWindow::DatabaseKey(WPARAM key,bool ctrl,bool shift){
    if(m_view!=View::Terminal||!Active()||!Active()->Database())return false;
    auto& s=*Active()->Database();auto* d=s.Active();if(!d)return false;
    if(d->editing)return false;
    if((ctrl&&key==VK_RETURN)||key==VK_F5){DatabaseExecute(ctrl&&!shift);return true;}
    if(key==VK_ESCAPE&&s.busy){s.Cancel();return true;}
    if(ctrl&&!shift&&key=='S'){DatabaseFile(true);return true;}
    if(ctrl&&!shift&&key=='O'){DatabaseFile(false);return true;}
    if(ctrl&&!shift&&key=='N'){s.NewEditor();m_ui.SetFocus(0);return true;}
    return false;
}

void MainWindow::DrawDatabaseWorkspace(const D2D1_RECT_F& a,db::Session& session){
    const float s=m_lay.scale,pad=10*s;const auto& p=session.Connection();
    m_r.Fill(a,theme::Base);m_r.PushClip(a);
    auto* d=session.Active();const bool modal=d&&d->editing;
    const float oldX=m_in.mx,oldPX=m_in.px;if(modal){m_in.mx=m_in.px=-100000;}
    const float x=a.left+pad,right=a.right-pad;float y=a.top+5*s;
    m_r.Text(p.name+L"  /  "+db::EngineName(p.engine)+L"  /  "+p.database,D2D1::RectF(x,y,right-150*s,y+24*s),theme::TextHi,13*s,Renderer::Align::Left,true);
    m_r.Text(DbStatus(session),D2D1::RectF(right-150*s,y,right,y+24*s),session.state==db::State::Lost?theme::Red:session.busy?theme::Amber:theme::AcHi(),11*s,Renderer::Align::Right);y+=29*s;
    Toolbar bar{m_ui,s,x,right,x,y,36500};const bool idle=!session.busy,online=idle&&session.state==db::State::Connected;
    if(bar.Button(session.state==db::State::Connected?TrText(L"Disconnect",L"Bağlantıyı kes"):TrText(L"Connect",L"Bağlan"),102,false,idle)){
        if(session.state==db::State::Connected){if(!session.transaction||MessageBoxW(m_hwnd,TrText(L"Rollback pending transaction and disconnect?",L"Bekleyen işlem geri alınıp bağlantı kesilsin mi?"),L"FullTerminal",MB_OKCANCEL|MB_ICONQUESTION)==IDOK){db::Request r;r.action=db::Action::Disconnect;session.Submit(r);}}
        else session.Connect();
    }
    if(bar.Button(TrText(L"Refresh",L"Yenile"),74,false,online))session.RefreshCatalog();
    if(bar.Button(L"+ SQL",70,true)){session.NewEditor();m_ui.SetFocus(0);m_r.PopClip();return;}
    if(bar.Button(TrText(L"Cancel query",L"Sorguyu iptal et"),126,false,session.busy))session.Cancel();
    if(bar.Button(session.autoCommit?L"Auto-commit: ON":L"Auto-commit: OFF",154,false,online)){
        db::Request r;r.action=db::Action::AutoCommit;r.flag=!session.autoCommit;session.Submit(r);
    }
    if(bar.Button(L"Commit",72,false,online&&session.transaction)){db::Request r;r.action=db::Action::Commit;session.Submit(r);}
    if(bar.Button(L"Rollback",78,false,online&&session.transaction)){db::Request r;r.action=db::Action::Rollback;session.Submit(r);}
    y=bar.Bottom();
    const float messageH=(!session.error.empty()||!session.notice.empty())?46*s:0;
    const float bottom=a.bottom-pad-messageH;
    const float explorerW=std::clamp((right-x)*.24f,145*s,245*s);const float split=x+explorerW;
    m_dbExplorer=D2D1::RectF(x,y,split,bottom);m_r.Fill(m_dbExplorer,theme::Surface);
    m_ui.Field(36600,D2D1::RectF(x+6*s,y+5*s,split-6*s,y+31*s),session.search,TrText(L"Find table / schema...",L"Tablo / şema ara..."));
    float ey=y+39*s;
    if(m_ui.Button(36601,D2D1::RectF(x+6*s,ey,split-6*s,ey+26*s),TrText(L"Databases...",L"Veritabanları..."),false,false,online)){
        POINT pt{(LONG)x,(LONG)(ey+26*s)};ClientToScreen(m_hwnd,&pt);int selected=ShowListMenu(pt,session.databases);
        if(selected>=0&&p.engine!=db::Engine::SQLite){auto copy=p;copy.database=session.databases[selected];copy.name=p.name+L" / "+copy.database;NewDatabaseTab(copy);m_r.PopClip();return;}
    }ey+=33*s;
    std::wstring needle=session.search;for(auto& c:needle)c=(wchar_t)towlower(c);
    std::vector<size_t> visible;
    for(size_t i=0;i<session.tables.size();++i){auto name=session.tables[i].schema+L"."+session.tables[i].name;for(auto& c:name)c=(wchar_t)towlower(c);if(needle.empty()||name.find(needle)!=name.npos)visible.push_back(i);}
    int rows=std::max(1,(int)((bottom-ey)/(34*s)));session.explorerScroll=std::clamp(session.explorerScroll,0,std::max(0,(int)visible.size()-rows));
    if(m_ui.Hot(m_dbExplorer)&&m_in.wheel)session.explorerScroll=std::clamp(session.explorerScroll-m_in.wheel*3,0,std::max(0,(int)visible.size()-rows));
    for(int n=session.explorerScroll;n<(int)visible.size()&&n<session.explorerScroll+rows;++n){const auto& t=session.tables[visible[n]];
        const auto r=D2D1::RectF(x+3*s,ey,split-3*s,ey+32*s);const bool selected=d&&d->tableView&&d->table==t;
        if(m_ui.Row(36700+n,r,selected)&&online){session.OpenTable(t);m_ui.SetFocus(0);m_r.PopClip();return;}
        m_r.Text(t.name,D2D1::RectF(r.left+7*s,ey,r.right-5*s,ey+18*s),selected?theme::AcHi():theme::Text,11*s);
        m_r.Text(t.schema+L"  "+t.type,D2D1::RectF(r.left+7*s,ey+17*s,r.right-5*s,ey+31*s),theme::TextDim,9*s);ey+=34*s;
    }
    if(visible.empty())m_r.Text(TrText(L"No tables. Use SQL to create one.",L"Tablo yok. SQL ile oluşturabilirsin."),D2D1::RectF(x+8*s,ey,split-8*s,ey+64*s),theme::TextMuted,11*s,Renderer::Align::Left,false,false,1,true);
    const float cx=split+10*s;float sy=y;
    const int slots=std::max(1,(int)((right-cx-60*s)/(142*s)));
    int activeIndex=0;for(int n=0;n<(int)session.documents.size();++n)if(session.documents[n].id==session.active)activeIndex=n;
    if(activeIndex<session.subScroll)session.subScroll=activeIndex;if(activeIndex>=session.subScroll+slots)session.subScroll=activeIndex-slots+1;
    float tx=cx;
    for(int n=session.subScroll;n<(int)session.documents.size()&&n<session.subScroll+slots;++n){auto& doc=session.documents[n];const bool selected=doc.id==session.active;
        const auto tr=D2D1::RectF(tx,sy,std::min(right-50*s,tx+137*s),sy+29*s);
        if(m_ui.Button(37000+n,tr,Trunc(doc.name,14)+(!doc.tableView&&doc.sql!=doc.savedSql?L" *":L""),selected)){session.active=doc.id;m_ui.SetFocus(0);m_r.PopClip();return;}tx+=142*s;
    }
    if(m_ui.Button(36990,D2D1::RectF(right-45*s,sy,right,sy+29*s),L"...")){
        std::vector<std::wstring> names;for(const auto& doc:session.documents)names.push_back(doc.name);POINT pt{(LONG)(right-45*s),(LONG)(sy+29*s)};ClientToScreen(m_hwnd,&pt);int n=ShowListMenu(pt,names);if(n>=0){session.active=session.documents[n].id;m_ui.SetFocus(0);}m_r.PopClip();return;
    }
    sy+=36*s;
    if(d){
        Toolbar actions{m_ui,s,cx,right,cx,sy,37100};
        if(actions.Button(d->tableView?TrText(L"Refresh data",L"Veriyi yenile"):TrText(L"Run · F5",L"Çalıştır · F5"),112,true,online)){if(d->tableView)session.LoadTable(d->id);else DatabaseExecute(true);}
        if(!d->tableView){
            if(actions.Button(TrText(L"Open SQL",L"SQL aç"),82)){DatabaseFile(false);m_r.PopClip();return;}
            if(actions.Button(TrText(L"Save SQL",L"SQL kaydet"),90))DatabaseFile(true);
            if(actions.Button(TrText(L"History",L"Geçmiş"),78)){
                std::vector<std::wstring> labels;for(auto text:session.history){std::replace(text.begin(),text.end(),L'\n',L' ');labels.push_back(Trunc(text,90));}
                POINT pt{(LONG)cx,(LONG)actions.Bottom()};ClientToScreen(m_hwnd,&pt);int n=ShowListMenu(pt,labels);if(n>=0){session.NewEditor(session.history[n]);m_ui.SetFocus(0);m_r.PopClip();return;}
            }
            if(actions.Button(L"EXPLAIN",82,false,online&&p.engine!=db::Engine::SqlServer&&p.engine!=db::Engine::Odbc))session.Execute(d->id,(p.engine==db::Engine::SQLite?L"EXPLAIN QUERY PLAN ":p.engine==db::Engine::Oracle?L"EXPLAIN PLAN FOR ":L"EXPLAIN ")+d->sql);
        }else{
            if(actions.Button(TrText(L"Structure",L"Yapı"),82))d->structure=!d->structure;
            if(actions.Button(TrText(L"Insert row",L"Satır ekle"),100,false,online&&!p.readOnly&&!d->tableColumns.empty())){d->editRow.assign(d->tableColumns.size(),{});d->include.assign(d->tableColumns.size(),false);d->inserting=d->editing=true;d->editScroll=0;m_ui.SetFocus(0);}
            if(actions.Button(TrText(L"Edit row",L"Satırı düzenle"),108,false,online&&!p.readOnly&&d->selectedRow>=0&&!d->result.sets.empty())){d->editRow=d->result.sets[0].rows[d->selectedRow];d->include.assign(d->tableColumns.size(),true);d->inserting=false;d->editing=true;d->editScroll=0;m_ui.SetFocus(0);}
        }
        if(actions.Button(TrText(L"Close subtab",L"Alt sekmeyi kapat"),124,false,idle)){
            if(d->tableView||d->sql==d->savedSql||MessageBoxW(m_hwnd,TrText(L"Discard unsaved SQL?",L"Kaydedilmemiş SQL silinsin mi?"),L"FullTerminal",MB_OKCANCEL|MB_ICONQUESTION)==IDOK){const auto id=d->id;m_ui.ForgetArea(d->editorId);std::erase_if(session.documents,[&](const auto& doc){return doc.id==id;});if(session.documents.empty())session.NewEditor();else session.active=session.documents.back().id;m_ui.SetFocus(0);m_r.PopClip();return;}
        }
        sy=actions.Bottom();
        if(!d->tableView){
            const float h=std::clamp((bottom-sy)*.40f,70*s,260*s);m_dbEditor=D2D1::RectF(cx,sy,right,std::min(bottom,sy+h));
            m_ui.TextArea(d->editorId,m_dbEditor,d->sql,L"SELECT ...",true,true);sy=m_dbEditor.bottom+8*s;
        }else{
            Toolbar filter{m_ui,s,cx,right,cx,sy,37200};
            const std::wstring column=d->filterColumn<0?TrText(L"Filter column",L"Filtre sütunu"):d->tableColumns[d->filterColumn].name;
            if(filter.Button(Trunc(column,18),140,false,online)){
                std::vector<std::wstring> names{TrText(L"No filter",L"Filtre yok")};for(const auto& c:d->tableColumns)names.push_back(c.name);POINT pt{(LONG)cx,(LONG)filter.Bottom()};ClientToScreen(m_hwnd,&pt);int n=ShowListMenu(pt,names);if(n>=0)d->filterColumn=n-1;
            }
            if(filter.Button(d->filterNull?L"IS NULL":L"= value",80))d->filterNull=!d->filterNull;
            if(filter.Button(TrText(L"Apply",L"Uygula"),72,false,online)){d->offset=0;session.LoadTable(d->id);}
            sy=filter.Bottom();m_ui.Field(37290,D2D1::RectF(cx,sy,right,sy+27*s),d->filter,TrText(L"Exact value (bound parameter, not SQL)",L"Tam değer (parametre olarak gönderilir)"));sy+=34*s;
        }
        DrawDatabaseGrid(D2D1::RectF(cx,sy,right,bottom),session,*d);
    }
    if(messageH){m_r.Fill(D2D1::RectF(x,bottom+4*s,right,a.bottom-4*s),theme::Surface);m_r.Text(session.error.empty()?session.notice:session.error,D2D1::RectF(x+8*s,bottom+5*s,right-8*s,a.bottom-5*s),session.error.empty()?theme::TextMuted:theme::Red,11*s,Renderer::Align::Left,false,false,1,true);
        if(m_ui.DblClicked(D2D1::RectF(x,bottom,right,a.bottom)))ClipboardSetText(m_hwnd,session.error.empty()?session.notice:session.error);
    }
    m_in.mx=oldX;m_in.px=oldPX;
    if(d&&d->editing)DrawDatabaseRowEditor(a,session,*d);
    m_r.PopClip();
}

void MainWindow::DrawDatabaseGrid(const D2D1_RECT_F& a,db::Session& session,db::Document& d){
    const float s=m_lay.scale;float y=a.top;const bool online=!session.busy&&session.state==db::State::Connected;
    if(a.bottom-a.top<55*s)return;
    Toolbar bar{m_ui,s,a.left,a.right,a.left,y,37300};
    if(d.tableView){
        if(bar.Button(L"<",30,false,online&&d.offset>0)){d.offset=std::max(0,d.offset-d.pageSize);session.LoadTable(d.id);}
        if(bar.Button(L">",30,false,online&&!d.result.sets.empty()&&d.result.sets[0].limited&&session.Connection().engine!=db::Engine::Odbc)){d.offset+=d.pageSize;session.LoadTable(d.id);}
    }else if(d.result.sets.size()>1){
        if(bar.Button(L"Result "+std::to_wstring(d.resultIndex+1),96))d.resultIndex=(d.resultIndex+1)%d.result.sets.size();
    }
    if(bar.Button(d.chart?TrText(L"Grid",L"Tablo"):TrText(L"Chart",L"Grafik"),70)){d.chart=!d.chart;d.structure=false;}
    if(bar.Button(L"CSV",46,false,!d.result.sets.empty())){
        const auto path=PickDbFile(m_hwnd,true,L"CSV\0*.csv\0",L"csv",L"results.csv");if(!path.empty()&&!WriteDbFile(path,db::Csv(d.result.sets[d.resultIndex])))Toast(L"CSV export failed");
    }
    if(bar.Button(L"JSON",52,false,!d.result.sets.empty())){
        const auto path=PickDbFile(m_hwnd,true,L"JSON\0*.json\0",L"json",L"results.json");if(!path.empty()&&!WriteDbFile(path,db::JsonRows(d.result.sets[d.resultIndex])))Toast(L"JSON export failed");
    }
    if(bar.Button(L"←",30))d.scrollColumn=std::max(0,d.scrollColumn-1);
    if(bar.Button(L"→",30))++d.scrollColumn;
    if(bar.Button(TrText(L"Copy row",L"Satırı kopyala"),106,false,d.selectedRow>=0&&!d.result.sets.empty())){
        const auto& set=d.result.sets[d.resultIndex];if(d.selectedRow<(int)set.rows.size()){db::ResultSet one;one.columns=set.columns;one.rows.push_back(set.rows[d.selectedRow]);ClipboardSetText(m_hwnd,db::Csv(one));}
    }
    y=bar.Bottom();m_dbGrid=D2D1::RectF(a.left,y,a.right,a.bottom-22*s);m_r.PushClip(m_dbGrid);
    if(d.structure){
        int n=0;for(const auto& c:d.tableColumns){float cy=y+n*24*s-d.scrollRow*24*s;if(cy>=y&&cy+24*s<=m_dbGrid.bottom)m_r.Text(c.name+L"   "+c.type+(c.primaryKey?L"   PK":L"")+(c.nullable?L"   NULL":L"   NOT NULL")+(c.generated?L"   generated":L""),D2D1::RectF(a.left+6*s,cy,a.right,cy+24*s),c.primaryKey?theme::AcHi():theme::Text,11*s);++n;}
        if(m_ui.Hot(m_dbGrid)&&m_in.wheel)d.scrollRow=std::clamp(d.scrollRow-m_in.wheel*3,0,std::max(0,n-1));m_r.PopClip();return;
    }
    if(d.result.sets.empty()){m_r.Text(TrText(L"Run SQL to see results here. Ctrl+Enter runs the selection or the full script.",L"Sonuçları görmek için SQL çalıştır. Ctrl+Enter seçili metni veya tüm betiği çalıştırır."),m_dbGrid,theme::TextMuted,12*s,Renderer::Align::Left,false,false,1,true);m_r.PopClip();return;}
    d.resultIndex=std::min(d.resultIndex,d.result.sets.size()-1);const auto& set=d.result.sets[d.resultIndex];
    const int maxRows=std::max(1,(int)((m_dbGrid.bottom-y-29*s)/(24*s)));
    d.scrollRow=std::clamp(d.scrollRow,0,std::max(0,(int)set.rows.size()-maxRows));
    if(m_ui.Hot(m_dbGrid)&&m_in.wheel)d.scrollRow=std::clamp(d.scrollRow-m_in.wheel*3,0,std::max(0,(int)set.rows.size()-maxRows));
    d.scrollColumn=std::clamp(d.scrollColumn,0,std::max(0,(int)set.columns.size()-1));
    if(d.chart&&!set.columns.empty()){
        d.chartColumn=std::clamp(d.chartColumn,0,(int)set.columns.size()-1);
        if(m_ui.Button(37400,D2D1::RectF(a.left,y,a.right,y+26*s),L"Y: "+set.columns[d.chartColumn].name)){
            std::vector<std::wstring> columns;for(const auto& c:set.columns)columns.push_back(c.name);POINT pt{(LONG)a.left,(LONG)(y+26*s)};ClientToScreen(m_hwnd,&pt);int selected=ShowListMenu(pt,columns);if(selected>=0)d.chartColumn=selected;
        }y+=31*s;
        double maximum=0;std::vector<std::pair<int,double>> numbers;
        for(int i=0;i<(int)set.rows.size();++i){const auto& v=set.rows[i][d.chartColumn];if(!v.text)continue;wchar_t* end=nullptr;const double val=wcstod(v.text->c_str(),&end);if(end==v.text->c_str()||*end||!std::isfinite(val))continue;maximum=std::max(maximum,std::abs(val));numbers.push_back({i,val});}
        if(numbers.empty())m_r.Text(TrText(L"Choose a numeric column.",L"Sayısal bir sütun seç."),D2D1::RectF(a.left,y,a.right,y+40*s),theme::TextMuted,12*s);
        const float origin=a.left+(a.right-a.left)*.58f,extent=(a.right-a.left)*.36f;
        m_r.Line(origin,y,origin,m_dbGrid.bottom,theme::Border,1);
        int rendered=0;for(int n=d.scrollRow;n<(int)numbers.size()&&y+24*s<m_dbGrid.bottom;++n){const auto [i,val]=numbers[n];const float w=maximum?(float)(val/maximum)*extent:0;
            m_r.Text(set.rows[i][0].text.value_or(L"NULL"),D2D1::RectF(a.left,y,a.left+(a.right-a.left)*.20f,y+24*s),theme::TextMuted,10*s);
            m_r.FillRound(D2D1::RectF(std::min(origin,origin+w),y+3*s,std::max(origin,origin+w),y+21*s),2*s,val<0?theme::Red:theme::Ac(),.65f);
            m_r.Text(set.rows[i][d.chartColumn].text.value_or(L""),D2D1::RectF(a.left,y,a.right-4*s,y+24*s),theme::TextHi,10*s,Renderer::Align::Right);y+=25*s;++rendered;
        }
    }else{
        const float colW=174*s,headerH=29*s;float colX=a.left;
        for(int c=d.scrollColumn;c<(int)set.columns.size()&&colX<a.right;++c){
            auto header=D2D1::RectF(colX,y,std::min(a.right,colX+colW),y+headerH);m_r.Fill(header,theme::Elevated);
            m_r.Text(set.columns[c].name+(d.sortColumn==c?(d.descending?L" ↓":L" ↑"):L""),D2D1::RectF(colX+7*s,y,header.right-5*s,y+headerH),theme::TextHi,11*s,Renderer::Align::Left,true);
            if(d.tableView&&m_ui.Clicked(header)&&online){if(d.sortColumn==c)d.descending=!d.descending;else{d.sortColumn=c;d.descending=false;}d.offset=0;session.LoadTable(d.id);}
            colX+=colW;
        }y+=headerH;
        for(int row=d.scrollRow;row<(int)set.rows.size()&&row<d.scrollRow+maxRows;++row){
            const auto rr=D2D1::RectF(a.left,y,a.right,y+24*s);m_r.Fill(rr,row==d.selectedRow?theme::Elevated:(row%2?theme::Surface:theme::Base));
            if(m_ui.Clicked(rr)){d.selectedRow=row;m_ui.SetFocus(0);}
            if(m_ui.DblClicked(rr)){
                d.selectedRow=row;int c=std::clamp(d.scrollColumn+(int)((m_in.mx-a.left)/colW),0,std::max(0,(int)set.columns.size()-1));d.selectedColumn=c;
                if(c<(int)set.rows[row].size())ClipboardSetText(m_hwnd,set.rows[row][c].text.value_or(L"NULL"));
            }
            colX=a.left;for(int c=d.scrollColumn;c<(int)set.rows[row].size()&&colX<a.right;++c){const auto& v=set.rows[row][c];
                m_r.Text(v.text.value_or(L"NULL"),D2D1::RectF(colX+7*s,y,std::min(a.right,colX+colW-5*s),y+24*s),v.text?theme::Text:theme::TextDim,11*s,Renderer::Align::Left,false,true);
                m_r.Line(colX+colW-1,y,colX+colW-1,y+24*s,theme::Border,1);colX+=colW;
            }y+=24*s;
        }
    }
    m_r.PopClip();
    std::wstring summary=std::to_wstring(set.rows.size())+TrText(L" rows",L" satır")+(set.limited?TrText(L" · more available",L" · devamı var"):L"");
    if(set.affected>=0)summary+=L"  |  "+std::to_wstring(set.affected)+TrText(L" affected",L" etkilendi");
    summary+=L"  |  "+std::to_wstring((int)d.result.milliseconds)+L" ms"+(d.tableView?L"  | offset "+std::to_wstring(d.offset):L"");
    m_r.Text(summary,D2D1::RectF(a.left,a.bottom-21*s,a.right,a.bottom),theme::TextMuted,10*s);
}

void MainWindow::DrawDatabaseRowEditor(const D2D1_RECT_F& a,db::Session& session,db::Document& d){
    const float s=m_lay.scale;const float w=std::min(660*s,a.right-a.left-20*s),h=std::min(520*s,a.bottom-a.top-20*s);
    const float x=(a.left+a.right-w)/2,top=(a.top+a.bottom-h)/2;const auto r=D2D1::RectF(x,top,x+w,top+h);m_dbRowEditor=r;
    m_r.Fill(a,0x000000,.72f);m_r.FillRound(r,10*s,theme::Surface);m_r.Stroke(r,theme::BorderHi,1);
    float y=top+14*s;const float right=x+w-16*s,left=x+16*s;
    m_r.Text(d.inserting?TrText(L"Insert row · unchecked fields use defaults",L"Satır ekle · işaretsiz alanlar varsayılanı kullanır"):TrText(L"Edit row · primary key required",L"Satırı düzenle · birincil anahtar gerekli"),D2D1::RectF(left,y,right,y+30*s),theme::TextHi,13*s,Renderer::Align::Left,true);y+=40*s;
    int count=std::max(1,(int)((h-130*s)/(58*s)));d.editScroll=std::clamp(d.editScroll,0,std::max(0,(int)d.tableColumns.size()-count));
    if(m_ui.Hot(r)&&m_in.wheel)d.editScroll=std::clamp(d.editScroll-m_in.wheel*2,0,std::max(0,(int)d.tableColumns.size()-count));
    for(int i=d.editScroll;i<(int)d.tableColumns.size()&&i<d.editScroll+count;++i){const auto& c=d.tableColumns[i];if(i>=(int)d.editRow.size())break;auto& v=d.editRow[i];
        m_r.Text(c.name+L" · "+c.type+(c.primaryKey?L" · PK":L""),D2D1::RectF(left,y,right,y+20*s),theme::TextMuted,10*s);y+=21*s;
        const bool editable=!c.generated&&!v.binary&&!v.truncated&&!session.busy;
        if(d.inserting){bool include=d.include[i];if(m_ui.Check(37500+i*4,D2D1::RectF(left,y,left+27*s,y+28*s),include,L"")&&editable)d.include[i]=include;}
        const float fieldX=left+(d.inserting?32*s:0);auto text=v.text.value_or(L"");
        if(editable){if(m_ui.Field(37501+i*4,D2D1::RectF(fieldX,y,right-90*s,y+28*s),text,v.text?L"":L"NULL")){v.text=text;if(d.inserting)d.include[i]=true;}}
        else m_r.Text(v.text.value_or(L"NULL")+TrText(L" (read-only)",L" (salt okunur)"),D2D1::RectF(fieldX,y,right-90*s,y+28*s),theme::TextDim,11*s);
        bool null=!v.text;if(editable&&m_ui.Check(37502+i*4,D2D1::RectF(right-84*s,y,right,y+28*s),null,L"NULL")){v.text=null?std::nullopt:std::optional<std::wstring>(L"");if(d.inserting)d.include[i]=true;}
        y+=37*s;
    }
    Toolbar bar{m_ui,s,left,right,left,top+h-71*s,37800};
    if(bar.Button(TrText(L"Save row",L"Satırı kaydet"),104,true,!session.busy)){std::wstring error;if(!session.SaveRow(d.id,false,error))Toast(error);}
    if(bar.Button(TrText(L"Cancel",L"Vazgeç"),80,false,!session.busy)){d.editing=d.inserting=false;m_ui.SetFocus(0);}
    if(!d.inserting&&bar.Button(TrText(L"Delete row",L"Satırı sil"),104,false,!session.busy,true)){
        if(MessageBoxW(m_hwnd,TrText(L"Delete the selected row?",L"Seçilen satır silinsin mi?"),L"FullTerminal",MB_OKCANCEL|MB_ICONWARNING)==IDOK){std::wstring error;if(!session.SaveRow(d.id,true,error))Toast(error);}
    }
    m_r.Text(session.error.empty()?TrText(L"NULL and empty text are different. Scroll for more columns.",L"NULL ve boş metin farklıdır. Diğer sütunlar için kaydır."):session.error,D2D1::RectF(left,top+h-34*s,right,top+h-6*s),session.error.empty()?theme::TextMuted:theme::Red,10*s,Renderer::Align::Left,false,false,1,true);
}
} // namespace ft
