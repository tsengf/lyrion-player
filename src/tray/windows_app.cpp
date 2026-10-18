// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include "core.hpp"
#include "setting_help.hpp"
#include <algorithm>

namespace {
std::wstring W(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0'); if(n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n); return out;
}
std::string U(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0'); if(n) WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr); return out;
}
std::string text(HWND control) {
    std::wstring value(GetWindowTextLengthW(control)+1, L'\0');
    int count = GetWindowTextW(control, value.data(), (int)value.size()); value.resize(count); return U(value);
}
enum { ADD=101, EDIT, REMOVE, TOGGLE, LOGIN, CONFIG, START_ALL, STOP_ALL, QUIT, OPEN_LYRION, VIEW_LOG,
       SAVE=201, CANCEL, CHOICE };
constexpr UINT TRAY_MESSAGE = WM_APP+1, ACTIVATE_MESSAGE = WM_APP+2;
constexpr wchar_t MAIN_CLASS[] = L"LyrionPlayerTrayWindow", EDITOR_CLASS[] = L"LyrionPlayerInstanceEditor";
struct App {
    std::unique_ptr<tray::Manager> manager;
    HINSTANCE instance{};
    HWND window{}, table{}, summary{}, editor{}, name{}, host{}, port{}, mac{}, web{}, start{}, choice{};
    HWND helpTooltip{};
    std::deque<std::wstring> helpText;
    HFONT font{};
    NOTIFYICONDATAW icon{};
    tray::Instance editing;
    std::vector<tray::Server> editorServers;
    std::string tableSnapshot;
    bool lastScanning = false, smoke = false;
    int dpi = 96;
    int px(int value) { return MulDiv(value,dpi,96); }
    HWND control(HWND parent, const wchar_t *kind, const wchar_t *title, DWORD style,
                 int id, int x,int y,int width,int height) {
        HWND result = CreateWindowExW(!wcscmp(kind, WC_EDITW) ? WS_EX_CLIENTEDGE : 0, kind, title,
            WS_CHILD|WS_VISIBLE|style, px(x),px(y),px(width),px(height),parent,(HMENU)(INT_PTR)id,instance,nullptr);
        SendMessageW(result, WM_SETFONT, (WPARAM)font, TRUE); return result;
    }
    void error(const std::string &message) { MessageBoxW(editor ? editor : window,W(message).c_str(),L"Lyrion Player",MB_OK|MB_ICONERROR); }
    bool confirm(const std::string &message) { return MessageBoxW(window,W(message).c_str(),L"Lyrion Player",MB_OKCANCEL|MB_ICONQUESTION)==IDOK; }
    std::string selected() {
        int row = ListView_GetNextItem(table,-1,LVNI_SELECTED);
        return row >= 0 && row < (int)manager->instances.size() ? manager->instances[row].id : "";
    }
    void show(HMONITOR monitor=nullptr) {
        if(!monitor){POINT point;GetCursorPos(&point);monitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);}
        MONITORINFO info{sizeof(info)};
        ShowWindow(window,SW_RESTORE);
        if(GetMonitorInfoW(monitor,&info)){
            auto place=[&](HWND target){
                RECT frame;if(!GetWindowRect(target,&frame))return;
                LONG width=frame.right-frame.left,height=frame.bottom-frame.top;
                LONG x=frame.left,y=frame.top;
                if(MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST)!=monitor){
                    x=info.rcWork.left+(info.rcWork.right-info.rcWork.left-width)/2;
                    y=info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-height)/2;
                }
                x=std::max(info.rcWork.left,std::min(x,info.rcWork.right-width));
                y=std::max(info.rcWork.top,std::min(y,info.rcWork.bottom-height));
                SetWindowPos(target,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            };
            place(window);if(editor)place(editor);
        }
        SetForegroundWindow(editor?editor:window);
    }
    void addIcon() {
        icon.cbSize=sizeof(icon); icon.hWnd=window; icon.uID=1;
        icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;
        icon.uCallbackMessage=TRAY_MESSAGE;
        icon.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        wcscpy_s(icon.szTip,L"Lyrion Player");
        Shell_NotifyIconW(NIM_ADD,&icon); icon.uVersion=NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION,&icon);
    }
    void updatePlayerButtons() {
        bool enabled=!manager->quitting&&manager->find(selected())!=nullptr;
        for(int command:{EDIT,REMOVE,TOGGLE,OPEN_LYRION,VIEW_LOG})EnableWindow(GetDlgItem(window,command),enabled);
    }
    void build() {
        table=control(window,WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL,1,16,16,890,270);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t *columns[]={L"Player",L"Auto discovery",L"Server",L"Start with app",L"Status"};
        const int widths[]={180,120,170,120,280};
        for(int i=0;i<5;++i){ LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<LPWSTR>(columns[i]);column.cx=px(widths[i]);ListView_InsertColumn(table,i,&column); }
        const wchar_t *labels[]={L"Add...",L"Edit...",L"Remove",L"Start / Stop"};
        for(int i=0;i<4;++i) control(window,WC_BUTTONW,labels[i],WS_TABSTOP,ADD+i,16+i*128,302,120,30);
        control(window,WC_BUTTONW,L"Open configuration folder",WS_TABSTOP,CONFIG,16,346,220,30);
        control(window,WC_BUTTONW,L"Open Lyrion",WS_TABSTOP,OPEN_LYRION,256,346,120,30);
        control(window,WC_BUTTONW,L"View Log",WS_TABSTOP,VIEW_LOG,384,346,120,30);
        summary=control(window,WC_STATICW,L"",0,2,16,389,890,24);
        updatePlayerButtons();
    }
    void refresh() {
        auto selectedId=selected();
        std::string snapshot=tray::encodeConfig(manager->instances);
        for(const auto &i:manager->instances)snapshot+=manager->status(i.id)+"\n"+manager->serverAddress(i.id)+"\n";
        if(snapshot!=tableSnapshot){
        tableSnapshot=std::move(snapshot);
        ListView_DeleteAllItems(table);
        for(size_t row=0;row<manager->instances.size();++row){
            const auto &i=manager->instances[row];
            auto nameValue=W(i.name),serverValue=W(manager->serverAddress(i.id)),state=W(manager->status(i.id));
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=(int)row;item.pszText=nameValue.data();ListView_InsertItem(table,&item);
            auto discoveryValue=W(i.host.empty()?"Yes":"No");
            ListView_SetItemText(table,(int)row,1,discoveryValue.data());
            auto autoStartValue=W(i.autoStart?"Yes":"No");
            ListView_SetItemText(table,(int)row,2,serverValue.data());
            ListView_SetItemText(table,(int)row,3,autoStartValue.data()); ListView_SetItemText(table,(int)row,4,state.data());
            if(i.id==selectedId) ListView_SetItemState(table,(int)row,LVIS_SELECTED,LVIS_SELECTED);
        }
        }
        updatePlayerButtons();
        auto value=manager->quitting?"Stopping players...":manager->scanning?"Scanning servers...":
            std::to_string(manager->instances.size())+" instances; "+std::to_string(manager->servers.size())+" servers found";
        SetWindowTextW(summary,W(value).c_str());
    }
    void choices() {
        LRESULT previous=SendMessageW(choice,CB_GETCURSEL,0,0);
        std::string selectedHost;
        if(previous>=2&&previous-2<(LRESULT)editorServers.size())selectedHost=editorServers[previous-2].host;
        editorServers=manager->servers;
        if(previous>=2){
            previous=1;
            for(size_t i=0;i<editorServers.size();++i)if(editorServers[i].host==selectedHost)previous=(LRESULT)i+2;
            EnableWindow(host,previous==1);EnableWindow(port,previous==1);
        }
        SendMessageW(choice,CB_RESETCONTENT,0,0);
        SendMessageW(choice,CB_ADDSTRING,0,(LPARAM)L"Automatic - first server to reply");
        SendMessageW(choice,CB_ADDSTRING,0,(LPARAM)L"Manual address");
        for(auto &s:editorServers){auto title=W(s.name+" ("+s.host+")");SendMessageW(choice,CB_ADDSTRING,0,(LPARAM)title.c_str());}
        SendMessageW(choice,CB_SETCURSEL,previous>=0&&previous<(LRESULT)editorServers.size()+2?previous:0,0);
    }
    void updateAutomaticServer() {
        auto server=manager->automaticServer(editing.id);
        SetWindowTextW(host,W(server.host).c_str());
        SendMessageW(host,EM_SETCUEBANNER,TRUE,(LPARAM)L"Discovering server...");
        SetWindowTextW(port,W(std::to_string(server.port)).c_str());editing.webPort=server.webPort;
    }
    void choose() {
        auto index=SendMessageW(choice,CB_GETCURSEL,0,0);
        if(index==0)updateAutomaticServer();
        else SendMessageW(host,EM_SETCUEBANNER,TRUE,(LPARAM)L"Hostname or IPv4 address");
        if(index>=2&&index-2<(LRESULT)editorServers.size()){
            auto &s=editorServers[index-2];SetWindowTextW(host,W(s.host).c_str());SetWindowTextW(port,W(std::to_string(s.port)).c_str());editing.webPort=s.webPort;
        }
        EnableWindow(host,index==1);EnableWindow(port,index==1);
    }
    void info(const wchar_t *heading,const char *description,int x,int y) {
        HWND button=control(editor,WC_BUTTONW,L"\u24D8",WS_TABSTOP|BS_FLAT,0,x,y+2,20,22);
        // The tooltip owns no text buffer; deque entries stay valid for the editor lifetime.
        helpText.push_back(std::wstring(heading)+L": "+W(description));
        TTTOOLINFOW tool{};tool.cbSize=sizeof(tool);tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=editor;tool.uId=(UINT_PTR)button;tool.lpszText=helpText.back().data();
        SendMessageW(helpTooltip,TTM_ADDTOOLW,0,(LPARAM)&tool);
    }
    void closeEditor() {
        if(helpTooltip){DestroyWindow(helpTooltip);helpTooltip=nullptr;}
        if(editor){DestroyWindow(editor);editor=nullptr;EnableWindow(window,TRUE);SetForegroundWindow(window);}
        helpText.clear();
    }
    void edit(tray::Instance value) {
        closeEditor();editing=std::move(value);
        editor=CreateWindowExW(WS_EX_DLGMODALFRAME,EDITOR_CLASS,L"Configure player",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
            CW_USEDEFAULT,CW_USEDEFAULT,px(620),px(440),window,nullptr,instance,this);
        helpTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
            CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,editor,nullptr,instance,nullptr);
        if(!helpTooltip)throw std::runtime_error("Could not create setting tooltips");
        SendMessageW(helpTooltip,TTM_SETMAXTIPWIDTH,0,px(360));
        SendMessageW(helpTooltip,TTM_SETDELAYTIME,TTDT_INITIAL,350);
        SendMessageW(helpTooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);
        auto field=[&](const wchar_t *label,const std::string &value,const char *help,int y){
            control(editor,WC_STATICW,label,0,0,16,y,155,24);
            info(label,help,176,y);
            return control(editor,WC_EDITW,W(value).c_str(),WS_TABSTOP|ES_AUTOHSCROLL,0,210,y,365,26);
        };
        name=field(L"Player name",editing.name,tray::help::playerName,24);
        control(editor,WC_STATICW,L"Server",0,0,16,66,155,24);
        info(L"Server",tray::help::server,176,66);
        choice=control(editor,WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,CHOICE,210,66,365,190);
        choices();SendMessageW(choice,CB_SETCURSEL,editing.host.empty()?0:1,0);
        host=field(L"Host / IPv4 address",editing.host,tray::help::host,108);port=field(L"SlimProto port",std::to_string(editing.port),tray::help::port,150);
        mac=field(L"Player ID (advanced)",editing.mac,tray::help::playerId,192);web=field(L"Web URL (optional)",editing.webURL,tray::help::webUrl,234);
        start=control(editor,WC_BUTTONW,L"Start this instance with the app",WS_TABSTOP|BS_AUTOCHECKBOX,0,210,278,285,26);
        info(L"Start with app",tray::help::startWithApp,500,278);
        SendMessageW(start,BM_SETCHECK,editing.autoStart?BST_CHECKED:BST_UNCHECKED,0);
        control(editor,WC_BUTTONW,L"Cancel",WS_TABSTOP,CANCEL,360,335,100,30);
        control(editor,WC_BUTTONW,L"Save",WS_TABSTOP|BS_DEFPUSHBUTTON,SAVE,475,335,100,30);
        choose();EnableWindow(window,FALSE);ShowWindow(editor,SW_SHOW);SetFocus(name);
    }
    void save() {
        auto value=editing;value.name=text(name);
        value.host=SendMessageW(choice,CB_GETCURSEL,0,0)==0?"":text(host);
        value.mac=text(mac);value.webURL=text(web);
        value.port=tray::parsePort(text(port));value.autoStart=SendMessageW(start,BM_GETCHECK,0,0)==BST_CHECKED;
        manager->save(value);closeEditor();refresh();
    }
    void action(UINT command) {
        auto id=selected();
        if(command==ADD)edit(manager->draft());
        else if(command==EDIT){if(auto i=manager->find(id))edit(*i);}
        else if(command==REMOVE){if(auto i=manager->find(id))if(confirm("Remove "+i->name+"? Its player will stop; logs will be retained."))manager->remove(id);}
        else if(command==TOGGLE){if(manager->find(id)){if(manager->runtime[id].desired)manager->stop(id);else manager->start(id);}}
        else if(command==LOGIN)tray::platform::setLogin(tray::platform::loginStatus()!="on");
        else if(command==CONFIG){tray::fs::create_directories(tray::platform::configDir());tray::platform::openFile(tray::platform::configDir());}
        else if(command==OPEN_LYRION&&manager->find(id)){
            auto url=manager->url(id);if(url.empty())throw std::runtime_error("Start the instance first or set its web URL");tray::platform::openURL(url);
        }
        else if(command==VIEW_LOG&&manager->find(id)){
            auto path=manager->logPath(id);if(!tray::fs::exists(path))throw std::runtime_error("No log exists yet. Start the instance first.");tray::platform::openFile(path);
        }
        else if(command==START_ALL)manager->startAll();
        else if(command==STOP_ALL)manager->stopAll();
        else if(command==QUIT)manager->quit();
        refresh();
    }
    void menu(POINT point) {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,EDIT,L"Settings...");
        AppendMenuW(menu,MF_STRING|(tray::platform::loginStatus()=="on"?MF_CHECKED:MF_UNCHECKED),LOGIN,L"Start at login");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,START_ALL,L"Start all players");
        AppendMenuW(menu,MF_STRING,STOP_ALL,L"Stop all players");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,QUIT,L"Quit Lyrion Player");
        HMONITOR monitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);SetForegroundWindow(window);
        UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(command==EDIT){show(monitor);return;}
        if(command)action(command);
    }
};
LRESULT CALLBACK EditorProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    App *app=(App *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE){app=(App *)((CREATESTRUCTW *)l)->lpCreateParams;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)app);}
    if(!app)return DefWindowProcW(window,message,w,l);
    try{
        if(message==WM_CLOSE){app->closeEditor();return 0;}
        if(message==WM_COMMAND){
            if(LOWORD(w)==SAVE)app->save();
            if(LOWORD(w)==CANCEL)app->closeEditor();
            if(LOWORD(w)==CHOICE&&HIWORD(w)==CBN_SELCHANGE)app->choose();
            return 0;
        }
    }catch(const std::exception &e){app->error(e.what());return 0;}
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK MainProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    App *app=(App *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE){app=(App *)((CREATESTRUCTW *)l)->lpCreateParams;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)app);app->window=window;}
    if(!app)return DefWindowProcW(window,message,w,l);
    static UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    try{
        if(message==taskbarCreated){app->addIcon();return 0;}
        if(message==ACTIVATE_MESSAGE){app->show();return 0;}
        if(message==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
        if(message==WM_QUERYENDSESSION){app->manager->quit();return TRUE;}
        if(message==WM_ENDSESSION&&w){DestroyWindow(window);return 0;}
        if(message==WM_DESTROY){Shell_NotifyIconW(NIM_DELETE,&app->icon);PostQuitMessage(0);return 0;}
        if(message==WM_COMMAND){app->action(LOWORD(w));return 0;}
        if(message==WM_NOTIFY&&((NMHDR *)l)->hwndFrom==app->table&&((NMHDR *)l)->code==LVN_ITEMCHANGED){app->updatePlayerButtons();return 0;}
        if(message==WM_NOTIFY&&((NMHDR *)l)->hwndFrom==app->table&&((NMHDR *)l)->code==NM_DBLCLK){app->action(EDIT);return 0;}
        if(message==TRAY_MESSAGE){
            POINT point{GET_X_LPARAM(w),GET_Y_LPARAM(w)};
            if(point.x==-1&&point.y==-1)GetCursorPos(&point);
            if(LOWORD(l)==WM_CONTEXTMENU)app->menu(point);
            else if(LOWORD(l)==NIN_SELECT||LOWORD(l)==NIN_KEYSELECT)app->show(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST));
            return 0;
        }
        if(message==WM_TIMER){
            app->manager->tick();app->refresh();
            if(app->lastScanning&&!app->manager->scanning&&app->editor)app->choices();
            app->lastScanning=app->manager->scanning;
            if(app->editor&&SendMessageW(app->choice,CB_GETCURSEL,0,0)==0)app->updateAutomaticServer();
            if(app->manager->quitting&&app->manager->stopped())DestroyWindow(window);
            return 0;
        }
    }catch(const std::exception &e){app->error(e.what());return 0;}
    return DefWindowProcW(window,message,w,l);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int) {
    SetProcessDPIAware();
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 1;
    WSADATA socketData;if(WSAStartup(MAKEWORD(2,2),&socketData)){CoUninitialize();return 1;}
    bool smoke=std::wstring(command).find(L"--smoke-test")!=std::wstring::npos;
    HANDLE mutex=smoke?nullptr:CreateMutexW(nullptr,FALSE,L"Local\\LyrionPlayerTray");
    if(!smoke&&(!mutex||GetLastError()==ERROR_ALREADY_EXISTS)){
        if(HWND existing=FindWindowW(MAIN_CLASS,nullptr))SendMessageW(existing,ACTIVATE_MESSAGE,0,0);
        if(mutex)CloseHandle(mutex);WSACleanup();CoUninitialize();return 0;
    }
    int code=0;
    try{
        App app;app.instance=instance;app.smoke=smoke;
        HDC dc=GetDC(nullptr);app.dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);
        app.font=CreateFontW(-app.px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto directory=smoke?tray::fs::temp_directory_path()/("lyrion-tray-smoke-"+tray::newId()):tray::platform::configDir();
        app.manager=std::make_unique<tray::Manager>(directory);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        cls.lpszClassName=MAIN_CLASS;cls.lpfnWndProc=MainProc;RegisterClassExW(&cls);
        cls.lpszClassName=EDITOR_CLASS;cls.lpfnWndProc=EditorProc;RegisterClassExW(&cls);
        app.window=CreateWindowExW(0,MAIN_CLASS,L"Lyrion Player - Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,app.px(940),app.px(465),nullptr,nullptr,instance,&app);
        if(!app.window)throw std::runtime_error("Could not create tray settings window");
        app.build();app.refresh();
        if(smoke){app.edit(app.manager->draft());if(!app.choice||!app.mac)throw std::runtime_error("Could not create instance editor");app.closeEditor();tray::fs::remove_all(directory);DestroyWindow(app.window);}
        else{
            app.addIcon();SetTimer(app.window,1,250,nullptr);app.manager->startConfigured();app.manager->scan();
            if(app.manager->instances.empty()||!app.manager->error.empty())app.show();
            if(!app.manager->error.empty())app.error(app.manager->error);
            MSG message;
            while(GetMessageW(&message,nullptr,0,0)>0){
                if(!IsDialogMessageW(app.editor?app.editor:app.window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
            }
        }
        app.manager.reset();DeleteObject(app.font);
    }catch(const std::exception &e){MessageBoxW(nullptr,W(e.what()).c_str(),L"Lyrion Player",MB_OK|MB_ICONERROR);code=1;}
    if(mutex)CloseHandle(mutex);WSACleanup();CoUninitialize();return code;
}
