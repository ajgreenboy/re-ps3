// H5 stand-in game: a Win32 window that takes the foreground, then ends after N seconds in one of
// several ways. Usage: stubgame.exe <mode> <seconds> <logfile>
//   exit   - PostQuitMessage, exit code 0 (normal exit)
//   abort  - CRT abort(), exit code 3 (no message box, no WER report)
//   crash  - access violation, exit code 0xC0000005 (WER UI suppressed with SetErrorMode)
//   altf4  - posts WM_SYSKEYDOWN VK_F4 with the Alt context bit to its own window; DefWindowProc turns
//            that into WM_SYSCOMMAND SC_CLOSE exactly as a real Alt-F4 does. No global input is sent.
//   wait   - stays up until closed or killed (the harness uses taskkill /PID on this process only)
// Every line in the log carries Unix epoch milliseconds so it can be matched against the XMB log.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <share.h>

static FILE* g_log=nullptr;
static char g_mode[16]="exit";
static int g_seconds=3;
static HWND g_window=nullptr;

static long long epochMs() {
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER v; v.LowPart=ft.dwLowDateTime; v.HighPart=ft.dwHighDateTime;
    return (long long)(v.QuadPart/10000ULL)-11644473600000LL;
}
static void logLine(const char* text) {
    if(!g_log)return;
    fprintf(g_log,"%lld %s\n",epochMs(),text); fflush(g_log);
}

static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_TIMER:
        if(wp==1) {
            KillTimer(hwnd,1);
            if(!strcmp(g_mode,"exit")) { logLine("ending: exit 0"); DestroyWindow(hwnd); return 0; }
            if(!strcmp(g_mode,"abort")) { logLine("ending: abort"); if(g_log)fclose(g_log); g_log=nullptr; abort(); }
            if(!strcmp(g_mode,"crash")) { logLine("ending: access violation"); if(g_log)fclose(g_log); g_log=nullptr;
                volatile int* p=nullptr; *p=1; }
            if(!strcmp(g_mode,"altf4")) {
                logLine(GetForegroundWindow()==hwnd?"ending: alt-f4 (stub is foreground)":"ending: alt-f4 (stub NOT foreground)");
                // lParam: repeat 1, scan code of F4 (0x3E), bit 29 = ALT held (context code)
                PostMessageW(hwnd,WM_SYSKEYDOWN,VK_F4,(LPARAM)(1|(0x3E<<16)|(1<<29)));
                return 0;
            }
        }
        if(wp==2) { // foreground probe once a second
            char line[96]; snprintf(line,sizeof line,"foreground=%d",GetForegroundWindow()==hwnd?1:0); logLine(line);
        }
        return 0;
    case WM_SYSCOMMAND:
        if((wp&0xFFF0)==SC_CLOSE)logLine("WM_SYSCOMMAND SC_CLOSE");
        break;
    case WM_CLOSE: logLine("WM_CLOSE received"); DestroyWindow(hwnd); return 0;
    case WM_DESTROY: logLine("window destroyed"); PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        RECT r; GetClientRect(hwnd,&r); FillRect(dc,&r,(HBRUSH)GetStockObject(DKGRAY_BRUSH));
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
        char text[64]; snprintf(text,sizeof text,"H5 Stub Game - mode %s",g_mode);
        DrawTextA(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0; }
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE inst,HINSTANCE,LPSTR,int) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    if(__argc>1)strncpy_s(g_mode,__argv[1],_TRUNCATE);
    if(__argc>2)g_seconds=atoi(__argv[2]);
    if(__argc>3)g_log=_fsopen(__argv[3],"a",_SH_DENYNO);
    char line[128]; snprintf(line,sizeof line,"started pid=%lu mode=%s seconds=%d",GetCurrentProcessId(),g_mode,g_seconds); logLine(line);
    WNDCLASSW wc{}; wc.lpfnWndProc=proc; wc.hInstance=inst; wc.lpszClassName=L"H5StubGame"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassW(&wc);
    g_window=CreateWindowExW(0,L"H5StubGame",L"H5 Stub Game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,800,450,nullptr,nullptr,inst,nullptr);
    ShowWindow(g_window,SW_SHOW);
    SetForegroundWindow(g_window);
    logLine(GetForegroundWindow()==g_window?"foreground claimed":"foreground NOT claimed");
    if(strcmp(g_mode,"wait"))SetTimer(g_window,1,g_seconds*1000,nullptr);
    SetTimer(g_window,2,1000,nullptr);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    logLine("exited 0");
    if(g_log)fclose(g_log);
    return 0;
}

