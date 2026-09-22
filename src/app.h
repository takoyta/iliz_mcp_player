#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include "bass.h"
#include "appinfo.h"

#define WND_CLASS L"IlizMcpPlayer"
#define REG_KEY   L"Software\\IlizMcpPlayer"

#define WIN_W     400
#define WIN_H     460
#define WIN_W_MIN 320
#define WIN_H_MIN 186
#define WIN_W_MAX 900
#define PAD       10
#define EDGE      6
#define SNAP      16
#define BTN       28
#define BAR_H     8
#define ROW_H     26
#define SEARCH_H  22
#define VOL_W     76

#define CLR_BACK  RGB(22, 22, 26)
#define CLR_EDGE  RGB(44, 44, 54)
#define CLR_TRACK RGB(35, 35, 43)
#define CLR_ROW   RGB(30, 30, 36)
#define CLR_FILL  RGB(46, 120, 230)
#define CLR_DIM   RGB(138, 138, 150)
#define CLR_LIT   RGB(255, 255, 255)
#define CLR_HOT   RGB(196, 56, 46)
#define CLR_GRIP  RGB(84, 84, 98)

#define TIMER_ID      1
#define TRAY_CLICK_ID 2
#define HOT_CLOSE     1
#define HOT_SEEK      2
#define HOT_VOL       3
#define HOT_PREV      4
#define HOT_PLAY      5
#define HOT_NEXT      6
#define HOT_LEFT        7
#define HOT_RIGHT       8
#define HOT_BOTTOM      9
#define HOT_TOP         16
#define HOT_TOPLEFT     17
#define HOT_TOPRIGHT    18
#define HOT_BOTTOMLEFT  19
#define HOT_BOTTOMRIGHT 20
#define HOT_LIST      10
#define HOT_SEARCH    11
#define HOT_PIN       12
#define HOT_MUTE      13
#define HOT_CLEAR     14
#define HOT_SHUFFLE   15
#define HOT_HEAD      21
#define HOT_LIKE      22
#define HOT_FAV       23
#define HOT_PLAYED    24
#define WM_TRAY       (WM_APP + 1)
#define WM_HTTP       (WM_APP + 2)
#define WM_TRACK_END  (WM_APP + 3)
#define WM_MEDIA      (WM_APP + 4)
#define HTTP_SEARCH   1
#define HTTP_PLAY     2
#define HTTP_STOP     3
#define HTTP_LIKE     4
#define HTTP_NOW      5
#define HTTP_SKIP     6
#define HTTP_LIMIT    50
#define META_CAP      256
#define IDM_ABOUT     1001
#define IDM_SHOW      1002
#define IDM_QUIT      1003
#define IDM_DATA      1004
#define IDM_TAGS      1005
#define IDM_COPY_TITLE 1006
#define IDM_REMOVE    1007
#define IDM_DELETE_FILE 1008
#define IDC_SEARCH    2001
#define TAG_DUMP_MAX  96
#define WND_TAGS      L"IlizMcpPlayerTags"
#define TRAY_ID       1

#define MAX_TRACKS    10000
#define WALK_DEPTH    12

struct Track {
    int id;
    wchar_t path[MAX_PATH];
    wchar_t title[META_CAP];
    wchar_t artist[META_CAP];
    wchar_t album[META_CAP];
    int secs;
    int liked;
    int plays;
    int pos;
    int picked;
};

struct Layout {
    RECT close, pin, prev, play, next, shuffle, like, vol, speaker, list, head, search, clear, fav, played;
};

struct HttpJob {
    int op;
    int id;
    int liked;
    int dir;
    int slim;
    wchar_t q[256];
    char *out;
    int cap;
    int n;
};

extern HWND    g_wnd;
extern HWND    g_search;
extern HSTREAM g_stream;
extern HFONT   g_font, g_small, g_icons;
extern HBRUSH  g_search_br;
extern WNDPROC g_search_proc;
extern int     g_dpi;
extern int     g_hot;
extern int     g_width;
extern int     g_height;
extern int     g_dragging;
extern POINT   g_grab;
extern wchar_t g_title[MAX_PATH];
extern wchar_t g_filter[128];
extern Track  *g_tracks;
extern int    *g_view;
extern int     g_count, g_cap, g_view_n, g_cur, g_sel, g_scroll;
extern int     g_vol;
extern int     g_vol_pre;
extern int     g_press;
extern int     g_kbps;
extern ULONGLONG g_bytes;
extern BOOL    g_exit;
extern BOOL    g_solo;
extern int     g_http_port;
extern BOOL    g_loading;
extern BOOL    g_topmost;
extern BOOL    g_shuffle;
extern BOOL    g_liked_only;
extern BOOL    g_played_only;
extern unsigned g_rng;
extern HICON   g_tray_icon;
extern UINT    g_taskbar_msg;
extern SOCKET  g_http_listen;
