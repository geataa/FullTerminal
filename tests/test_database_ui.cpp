#include "ui/MainWindowInternal.h"
#include <cassert>
#include <iostream>
#include "transport/daemon/SessionDaemon.h"

namespace ft {
class DatabaseUiTest {
    static void Wait(MainWindow& w,db::Session& s){const auto until=GetTickCount64()+8000;while(s.busy&&GetTickCount64()<until){w.Tick();Sleep(1);}assert(!s.busy);if(!s.error.empty())std::wcerr<<s.error<<L"\n";assert(s.error.empty());w.Render();w.Render();}
    static void Click(MainWindow& w,float x,float y){auto pt=MAKELPARAM((int)x,(int)y);w.WndProc(WM_MOUSEMOVE,0,pt);w.WndProc(WM_LBUTTONDOWN,MK_LBUTTON,pt);w.Render();w.WndProc(WM_LBUTTONUP,0,pt);w.Render();}
public:
    static int Wsl(){
        MainWindow w;w.m_dataDir=PortableDataDir();w.m_dpi=96;w.m_inst=GetModuleHandleW(nullptr);w.m_cfg.restoreSessions=false;
        WNDCLASSW cls{};cls.hInstance=w.m_inst;cls.lpszClassName=L"FullTerminal.WslRegression";cls.lpfnWndProc=MainWindow::WndProcStatic;assert(RegisterClassW(&cls));
        assert(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"WSL regression",WS_OVERLAPPEDWINDOW,100,100,1000,700,nullptr,nullptr,w.m_inst,&w));
        std::wstring error;assert(w.m_r.Init(w.m_hwnd,96,&error));w.OnResize();w.m_profiles=DiscoverShellProfiles();int tested=0;
        for(size_t n=0;n<w.m_profiles.size();++n){const auto profile=w.m_profiles[n];if(profile.kind!=ProfileKind::Wsl)continue;
            for(bool hub:{false,true}){
                bool started=false;if(hub){ConnectionNode node;node.type=NodeType::Wsl;node.id=WideToUtf8(profile.id);node.name=WideToUtf8(profile.wslDistro);started=w.LaunchNode(node);}else started=w.NewTab(n);
                assert(started);auto* tab=w.Active();assert(tab->profile().wslDistro==profile.wslDistro);
                auto until=GetTickCount64()+20000;bool marker=false;ULONGLONG sent=0;
                while(GetTickCount64()<until&&!marker&&!tab->Dead()&&!tab->Exited()){
                    tab->Pump();if(GetTickCount64()-sent>1000){tab->Write("printf '__FT_WSL_%s__\\n' READY\r");sent=GetTickCount64();}
                    for(const auto& line:tab->screen().GetTailLines(100))if(line.find("__FT_WSL_READY__")!=line.npos)marker=true;Sleep(20);
                }
                if(!marker){for(const auto& line:tab->screen().GetTailLines(100))std::cerr<<line<<"\n";}
                assert(marker);std::wcout<<(hub?L"Hub: ":L"Plus: ")<<profile.wslDistro<<L" OK\n";w.CloseTab(w.m_active);++tested;
            }
        }
        DestroyWindow(w.m_hwnd);auto request=json::Value::Object();json::Value response;request["action"]="stop";SessionDaemon::Request(request,response);return tested?0:77;
    }
    static void Run(){
        MainWindow w;w.m_dataDir=PortableDataDir();w.m_dpi=96;w.m_sidebarOpen=true;w.m_focused=true;w.m_cfg.restoreSessions=false;w.m_cfg.mcpHttp=false;
        w.m_inst=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=w.m_inst;cls.lpszClassName=L"FullTerminal.DatabaseRegression";cls.style=CS_DBLCLKS;cls.lpfnWndProc=MainWindow::WndProcStatic;assert(RegisterClassW(&cls));
        assert(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"Database regression",WS_OVERLAPPEDWINDOW,100,100,1400,900,nullptr,nullptr,w.m_inst,&w));
        w.m_ui.SetOwner(w.m_hwnd);std::wstring error;assert(w.m_r.Init(w.m_hwnd,96,&error));w.ApplyColorScheme();w.OnResize();
        db::Profile profile;profile.engine=db::Engine::SQLite;profile.database=L":memory:";profile.name=L"Analytics · local";profile.id=L"analytics";
        w.NewDatabaseTab(profile);auto* session=w.Active()->Database();assert(session);Wait(w,*session);assert(!w.TerminalHasKeyboard());
        // Type and execute via the actual UI dispatch; no key goes to a terminal PTY.
        Click(w,w.m_dbEditor.left+25,w.m_dbEditor.top+15);assert(w.m_ui.Focus()==session->Active()->editorId);
        w.m_in.keys.push_back({'A',true,false});w.Render();
        const std::wstring sql=L"CREATE TABLE sales(id INTEGER PRIMARY KEY, region TEXT, revenue REAL, note TEXT);\nINSERT INTO sales VALUES(1,'İstanbul',4200,NULL),(2,'Ankara',2800,'Q1'),(3,'İzmir',3500,''),(4,'Bursa',1800,'Q1');\nSELECT region,revenue FROM sales ORDER BY revenue DESC;";
        for(auto c:sql)w.OnChar(c,false);w.Render();assert(session->Active()->sql==sql);
        w.m_in.keys.push_back({'Z',true,false});w.Render();assert(session->Active()->sql==L"SELECT 1;");
        w.m_in.keys.push_back({'Y',true,false});w.Render();assert(session->Active()->sql==sql);
        w.OnKeyDown(VK_F5,false);Wait(w,*session);assert(session->Active()->result.sets.back().rows.size()==4);
        session->RefreshCatalog();Wait(w,*session);assert(session->tables.size()==1);
        const auto editor=session->active;session->OpenTable(session->tables[0]);Wait(w,*session);assert(session->documents.size()==2);assert(session->Active()->tableColumns[0].primaryKey);
        auto* d=session->Active();assert(d->result.sets[0].rows.size()==4);w.Render();w.Render();assert(w.SaveScreenshot(L"database-table.png"));
        // Header click dispatches server-side sorting.
        Click(w,w.m_dbGrid.left+370,w.m_dbGrid.top+14);Wait(w,*session);assert(d->sortColumn==2);assert(d->result.sets[0].rows[0][2].text==L"1800.0");
        // Grid click selection and row editor use the same data model as parameterized CRUD.
        Click(w,w.m_dbGrid.left+20,w.m_dbGrid.top+39);assert(d->selectedRow==0);
        d->editRow=d->result.sets[0].rows[0];d->include.assign(d->tableColumns.size(),true);d->editing=true;d->inserting=false;w.Render();
        assert(w.SaveScreenshot(L"database-row-editor.png"));
        d->editRow[3].text=L"Updated from UI fixture";assert(session->SaveRow(d->id,false,error));Wait(w,*session);assert(!d->editing);
        d->chart=true;d->chartColumn=2;w.Render();w.Render();assert(w.SaveScreenshot(L"database-chart.png"));
        d->chart=false;session->active=editor;w.m_ui.SetFocus(0);session->Active()->resultIndex=2;w.Render();w.Render();assert(w.SaveScreenshot(L"database-sql.png"));
        // Another connection has independent child tabs, SQL, results and selection.
        profile.id=L"second";profile.name=L"Scratchpad";w.NewDatabaseTab(profile);auto* second=w.Active()->Database();Wait(w,*second);assert(second!=session&&second->documents.size()==1);assert(second->tables.empty());
        w.SelectTab(0);assert(w.Active()->Database()==session&&session->documents.size()==2);w.SplitActiveTab(SplitDirection::Vertical);assert(w.m_tabLayouts[0]->PaneCount()==1);
        for(auto size:{SIZE{1024,700},SIZE{1600,920},SIZE{1280,820}}){SetWindowPos(w.m_hwnd,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER);w.OnResize();w.Render();assert(w.m_dbGrid.right>w.m_dbGrid.left);assert(session->documents.size()==2);}
        w.m_dbLoaded=true;w.m_dbProfiles={profile};w.SetView(MainWindow::View::Databases);w.Render();w.Render();assert(w.SaveScreenshot(L"database-connections.png"));
        w.m_dbDraft=profile;w.m_dbDraft.engine=db::Engine::PostgreSQL;w.m_dbDraft.name=L"Production PostgreSQL";w.m_dbDraft.host=L"db.example.test";w.m_dbDraft.database=L"analytics";w.m_dbDraft.user=L"analyst";w.m_dbEditing=true;w.Render();w.Render();assert(w.SaveScreenshot(L"database-profile.png"));
        // The native engines must save without an installed ODBC driver or DSN.
        w.m_dbDraft.id=L"native-profile";w.m_dbDraft.driver.clear();w.m_dbDraft.dsn.clear();
        const auto a=w.m_lay.main;Click(w,a.left+22+148+5+37,a.bottom-50+13);
        assert(!w.m_dbEditing&&w.m_dbError.empty());bool nativeSaved=false;
        for(const auto& p:w.m_dbProfiles)if(p.id==L"native-profile"){nativeSaved=true;assert(p.engine==db::Engine::PostgreSQL&&p.driver.empty());}assert(nativeSaved);
        // DB profiles restore disconnected; there is never an implicit SQL replay.
        auto node=json::Value::Object();node["type"]="database";node["databaseProfile"]="second";auto restored=w.RestoreTabFromNode(node,80,24);assert(restored&&restored->IsDatabase());assert(restored->Database()->state==db::State::Disconnected);
        node["databaseName"]="another_database";node["databaseConnectionName"]="Scratchpad / another_database";
        auto otherDb=w.RestoreTabFromNode(node,80,24);assert(otherDb->Database()->Connection().database==L"another_database");
        DestroyWindow(w.m_hwnd);std::cout<<"Database UI: input, execution, main/subtab isolation, table sorting/editing/chart, normal-window resize and disconnected restore passed.\n";
    }
};
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--daemon")return ft::SessionDaemon::RunStandalone();
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(argc>1&&std::string(argv[1])=="--wsl")return ft::DatabaseUiTest::Wsl();
    ft::DatabaseUiTest::Run();CoUninitialize();
}
