#include "app.h"
#include "util.h"
#include "ui.h"

HWND    g_wnd;
HWND    g_search;
HSTREAM g_stream;
HFONT   g_font, g_small, g_icons;
HBRUSH  g_search_br;
WNDPROC g_search_proc;
int     g_dpi = 96;
int     g_hot;
int     g_width = WIN_W;
int     g_height = WIN_H;
int     g_dragging;
POINT   g_grab;
wchar_t g_title[MAX_PATH];
wchar_t g_filter[128];
Track  *g_tracks;
int    *g_view;
int     g_count, g_cap, g_view_n, g_cur = -1, g_sel = -1, g_scroll;
int     g_vol = 80;
int     g_vol_pre = 80;
int     g_press;
int     g_kbps;
BOOL    g_exit;
BOOL    g_solo;
int     g_http_port = HTTP_PORT;
BOOL    g_loading;
BOOL    g_topmost = TRUE;
BOOL    g_shuffle;
unsigned g_rng;
HICON   g_tray_icon;
UINT    g_taskbar_msg;
SOCKET  g_http_listen = INVALID_SOCKET;

extern "C" void __stdcall AppEntry()
{
    ExitProcess((UINT)Run());
}
