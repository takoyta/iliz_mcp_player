#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include "bass.h"
#include "appinfo.h"

#define WND_CLASS L"IlizMcpPlayer"
#define REG_KEY   L"Software\\IlizMcpPlayer"

#define WIN_W     400
#define WIN_H     460
#define WIN_W_MIN 320
#define WIN_H_MIN 220
#define WIN_W_MAX 900
#define WIN_H_MAX 1200
#define PAD       10
#define EDGE      6
#define SNAP      16
#define BTN       24
#define BAR_H     24
#define ROW_H     26
#define SEARCH_H  22
#define RADIUS    10
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
#define WM_TRAY       (WM_APP + 1)
#define WM_HTTP       (WM_APP + 2)
#define HTTP_SEARCH   1
#define HTTP_PLAY     2
#define HTTP_STOP     3
#define HTTP_LIMIT    50
#define IDM_ABOUT     1001
#define IDM_SHOW      1002
#define IDM_QUIT      1003
#define IDC_SEARCH    2001
#define TRAY_ID       1
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

#define MAX_TRACKS    10000
#define WALK_DEPTH    12

extern "C" int _fltused = 1;

#pragma function(memset)
extern "C" void *memset(void *dst, int c, size_t n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

#pragma function(memcpy)
extern "C" void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

struct Track {
    wchar_t path[MAX_PATH];
    int secs;
};

static HWND    g_wnd;
static HWND    g_search;
static HSTREAM g_stream;
static HFONT   g_font, g_small, g_icons;
static HBRUSH  g_search_br;
static WNDPROC g_search_proc;
static int     g_dpi = 96;
static int     g_hot;
static int     g_width = WIN_W;
static int     g_height = WIN_H;
static int     g_dragging;
static POINT   g_grab;
static wchar_t g_title[MAX_PATH];
static wchar_t g_filter[128];
static Track  *g_tracks;
static int    *g_view;
static int     g_count, g_cap, g_view_n, g_cur = -1, g_sel = -1, g_scroll;
static int     g_vol = 80;
static int     g_vol_pre = 80;
static int     g_press;
static int     g_kbps;
static BOOL    g_exit;
static BOOL    g_solo;
static int     g_http_port = HTTP_PORT;
static BOOL    g_loading;
static BOOL    g_topmost = TRUE;
static BOOL    g_shuffle;
static unsigned g_rng;
static HICON   g_tray_icon;
static UINT    g_taskbar_msg;
static SOCKET  g_http_listen = INVALID_SOCKET;

struct HttpJob {
    int op;
    int id;
    int slim;
    wchar_t q[256];
    char *out;
    int cap;
    int n;
};

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int Abs(int v) { return v < 0 ? -v : v; }

static int Clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static HANDLE Heap() { return GetProcessHeap(); }

static void Copy(wchar_t *dst, const wchar_t *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int Len(const wchar_t *s)
{
    int n = 0;
    while (s[n]) ++n;
    return n;
}

static void Append(wchar_t *dst, int cap, const wchar_t *src)
{
    int i = Len(dst);
    while (*src && i < cap - 1)
        dst[i++] = *src++;
    dst[i] = 0;
}

static wchar_t Lower(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return (wchar_t)(c + 32);
    return c;
}

static BOOL EqI(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b) {
        if (Lower(*a) != Lower(*b))
            return FALSE;
        ++a;
        ++b;
    }
    return *a == *b;
}

static BOOL ContainsI(const wchar_t *hay, const wchar_t *needle)
{
    if (!needle || !needle[0])
        return TRUE;
    for (; *hay; ++hay) {
        const wchar_t *h = hay, *n = needle;
        while (*n && *h && Lower(*h) == Lower(*n)) {
            ++h;
            ++n;
        }
        if (!*n)
            return TRUE;
    }
    return FALSE;
}

static const wchar_t *FileName(const wchar_t *path)
{
    const wchar_t *name = path;
    for (const wchar_t *p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    return name;
}

static const wchar_t *Ext(const wchar_t *path)
{
    const wchar_t *dot = 0;
    const wchar_t *name = FileName(path);
    for (const wchar_t *p = name; *p; ++p)
        if (*p == L'.')
            dot = p;
    return dot ? dot + 1 : L"";
}

static BOOL IsAudio(const wchar_t *path)
{
    const wchar_t *e = Ext(path);
    return EqI(e, L"mp3") || EqI(e, L"mp2") || EqI(e, L"mp1") ||
           EqI(e, L"ogg") || EqI(e, L"wav") || EqI(e, L"aif") || EqI(e, L"aiff");
}

static BOOL WorkArea(const RECT *r, RECT *out)
{
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi))
        return FALSE;
    *out = mi.rcWork;
    return TRUE;
}

static void SnapMove(RECT *r)
{
    RECT wa;
    if (!WorkArea(r, &wa))
        return;
    int snap = S(SNAP);
    int w = r->right - r->left;
    int h = r->bottom - r->top;
    if (Abs(r->left - wa.left) <= snap) {
        r->left = wa.left;
        r->right = r->left + w;
    } else if (Abs(r->right - wa.right) <= snap) {
        r->right = wa.right;
        r->left = r->right - w;
    }
    if (Abs(r->top - wa.top) <= snap) {
        r->top = wa.top;
        r->bottom = r->top + h;
    } else if (Abs(r->bottom - wa.bottom) <= snap) {
        r->bottom = wa.bottom;
        r->top = r->bottom - h;
    }
}

static void ApplyShape()
{
    RECT rc;
    GetClientRect(g_wnd, &rc);
    SetWindowRgn(g_wnd, CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, S(10), S(10)), FALSE);
}

static HKEY OpenReg(REGSAM access, BOOL create)
{
    HKEY key = 0;
    if (create)
        RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, 0, 0, access, 0, &key, 0);
    else
        RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, access, &key);
    return key;
}

static void LoadSettings(int *x, int *y)
{
    *x = *y = CW_USEDEFAULT;
    g_width = WIN_W;
    g_height = WIN_H;
    g_vol = 80;
    g_vol_pre = 80;
    g_topmost = TRUE;
    g_shuffle = FALSE;

    HKEY key = OpenReg(KEY_READ, FALSE);
    if (!key)
        return;

    DWORD v, size, type;
    size = sizeof(v);
    if (RegQueryValueExW(key, L"Width", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && v >= WIN_W_MIN && v <= WIN_W_MAX)
        g_width = (int)v;
    size = sizeof(v);
    if (RegQueryValueExW(key, L"Height", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && v >= WIN_H_MIN && v <= WIN_H_MAX)
        g_height = (int)v;
    size = sizeof(v);
    if (RegQueryValueExW(key, L"Volume", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && v <= 100)
        g_vol = (int)v;

    size = sizeof(v);
    if (RegQueryValueExW(key, L"VolumePre", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD && v <= 100 && v > 0)
        g_vol_pre = (int)v;
    else if (g_vol > 0)
        g_vol_pre = g_vol;

    size = sizeof(v);
    if (RegQueryValueExW(key, L"Topmost", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        g_topmost = v != 0;

    size = sizeof(v);
    if (RegQueryValueExW(key, L"Shuffle", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        g_shuffle = v != 0;

    size = sizeof(v);
    if (RegQueryValueExW(key, L"Sel", 0, &type, (BYTE *)&v, &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        g_sel = (int)v;

    LONG pos[2];
    size = sizeof(pos);
    if (RegQueryValueExW(key, L"Pos", 0, &type, (BYTE *)pos, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size == sizeof(pos)) {
        RECT r = { pos[0], pos[1], pos[0] + S(g_width), pos[1] + S(g_height) };
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            *x = pos[0];
            *y = pos[1];
        }
    }
    RegCloseKey(key);
}

static void SaveGeom()
{
    RECT r;
    if (!GetWindowRect(g_wnd, &r))
        return;
    HKEY key = OpenReg(KEY_WRITE, TRUE);
    if (!key)
        return;
    LONG v[2] = { r.left, r.top };
    RegSetValueExW(key, L"Pos", 0, REG_BINARY, (const BYTE *)v, sizeof(v));
    DWORD w = (DWORD)Clamp(MulDiv(r.right - r.left, 96, g_dpi), WIN_W_MIN, WIN_W_MAX);
    DWORD h = (DWORD)Clamp(MulDiv(r.bottom - r.top, 96, g_dpi), WIN_H_MIN, WIN_H_MAX);
    g_width = (int)w;
    g_height = (int)h;
    RegSetValueExW(key, L"Width", 0, REG_DWORD, (const BYTE *)&w, sizeof(w));
    RegSetValueExW(key, L"Height", 0, REG_DWORD, (const BYTE *)&h, sizeof(h));
    DWORD vol = (DWORD)g_vol;
    RegSetValueExW(key, L"Volume", 0, REG_DWORD, (const BYTE *)&vol, sizeof(vol));
    DWORD pre = (DWORD)(g_vol_pre > 0 ? g_vol_pre : 80);
    RegSetValueExW(key, L"VolumePre", 0, REG_DWORD, (const BYTE *)&pre, sizeof(pre));
    DWORD top = g_topmost ? 1 : 0;
    RegSetValueExW(key, L"Topmost", 0, REG_DWORD, (const BYTE *)&top, sizeof(top));
    DWORD sh = g_shuffle ? 1 : 0;
    RegSetValueExW(key, L"Shuffle", 0, REG_DWORD, (const BYTE *)&sh, sizeof(sh));
    DWORD cur = (DWORD)(g_sel < 0 ? 0 : g_sel);
    RegSetValueExW(key, L"Sel", 0, REG_DWORD, (const BYTE *)&cur, sizeof(cur));
    RegCloseKey(key);
}

static void ApplyVol()
{
    BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, (DWORD)(g_vol * 100));
}

static void ApplyTopmost()
{
    SetWindowPos(g_wnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ToggleTopmost()
{
    g_topmost = !g_topmost;
    ApplyTopmost();
    SaveGeom();
    InvalidateRect(g_wnd, 0, FALSE);
}

static void ToggleShuffle()
{
    g_shuffle = !g_shuffle;
    SaveGeom();
    InvalidateRect(g_wnd, 0, FALSE);
}

static unsigned RandU()
{
    if (!g_rng)
        g_rng = GetTickCount() ^ (unsigned)(ULONG_PTR)g_wnd;
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static void ToggleMute()
{
    if (g_vol > 0) {
        g_vol_pre = g_vol;
        g_vol = 0;
    } else {
        g_vol = g_vol_pre > 0 ? g_vol_pre : 80;
    }
    ApplyVol();
    SaveGeom();
    InvalidateRect(g_wnd, 0, FALSE);
}

static void TrayAdd()
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_wnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_tray_icon;
    Copy(nid.szTip, g_title, 128);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void TrayDel()
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_wnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void TrayTip()
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_wnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_TIP;
    Copy(nid.szTip, g_title, 128);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void RestoreWnd(HWND wnd)
{
    ShowWindow(wnd, SW_SHOW);
    SetWindowPos(wnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(wnd);
}

struct Layout {
    RECT close, pin, seek, prev, play, next, shuffle, vol, speaker, list, head, search, clear;
};

static void GetLayout(Layout *l)
{
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int pad = S(PAD);
    int btn = S(BTN);
    int bar = S(BAR_H);
    int gap = S(6);

    l->close.right = rc.right - pad;
    l->close.left = l->close.right - btn;
    l->close.top = pad;
    l->close.bottom = l->close.top + btn;

    l->pin.right = l->close.left - gap;
    l->pin.left = l->pin.right - btn;
    l->pin.top = l->close.top;
    l->pin.bottom = l->close.bottom;

    l->head.left = pad;
    l->head.right = l->pin.left - gap;
    l->head.top = l->close.top;
    l->head.bottom = l->close.bottom;

    l->seek.left = pad;
    l->seek.right = rc.right - pad;
    l->seek.top = l->close.bottom + pad;
    l->seek.bottom = l->seek.top + bar;

    int cy = l->seek.bottom + pad;
    l->prev.left = pad;
    l->prev.right = l->prev.left + btn;
    l->prev.top = cy;
    l->prev.bottom = cy + btn;

    l->play.left = l->prev.right + gap;
    l->play.right = l->play.left + btn;
    l->play.top = cy;
    l->play.bottom = cy + btn;

    l->next.left = l->play.right + gap;
    l->next.right = l->next.left + btn;
    l->next.top = cy;
    l->next.bottom = cy + btn;

    l->shuffle.left = l->next.right + gap;
    l->shuffle.right = l->shuffle.left + btn;
    l->shuffle.top = cy;
    l->shuffle.bottom = cy + btn;

    l->vol.right = rc.right - pad;
    l->vol.left = l->vol.right - S(VOL_W);
    l->vol.top = cy + (btn - bar) / 2;
    l->vol.bottom = l->vol.top + bar;

    l->speaker.right = l->vol.left - S(4);
    l->speaker.left = l->speaker.right - btn;
    l->speaker.top = cy;
    l->speaker.bottom = cy + btn;

    l->search.left = pad;
    l->search.right = rc.right - pad;
    l->search.top = cy + btn + pad;
    l->search.bottom = l->search.top + S(SEARCH_H);

    int clr = l->search.bottom - l->search.top;
    l->clear.right = l->search.right - S(2);
    l->clear.left = l->clear.right - clr;
    l->clear.top = l->search.top;
    l->clear.bottom = l->search.bottom;

    l->list.left = 0;
    l->list.right = rc.right;
    l->list.top = l->search.bottom + gap;
    l->list.bottom = rc.bottom - pad;
}

static int VisibleRows(const Layout *l)
{
    int h = l->list.bottom - l->list.top;
    int row = S(ROW_H);
    if (row <= 0)
        return 0;
    int n = h / row;
    return n < 0 ? 0 : n;
}

static int ViewIndex(int track)
{
    for (int i = 0; i < g_view_n; ++i) {
        if (g_view[i] == track)
            return i;
    }
    return -1;
}

static void ClampScroll()
{
    Layout l;
    GetLayout(&l);
    int vis = VisibleRows(&l);
    int maxs = g_view_n - vis;
    if (maxs < 0) maxs = 0;
    g_scroll = Clamp(g_scroll, 0, maxs);
}

static void EnsureVisible(int i)
{
    Layout l;
    GetLayout(&l);
    int vis = VisibleRows(&l);
    if (vis <= 0)
        return;
    int p = ViewIndex(i);
    if (p < 0)
        return;
    if (p < g_scroll)
        g_scroll = p;
    else if (p >= g_scroll + vis)
        g_scroll = p - vis + 1;
    ClampScroll();
}

static void PlaceSearch()
{
    if (!g_search)
        return;
    Layout l;
    GetLayout(&l);
    int inset = S(26);
    int y = l.search.top + S(2);
    int h = l.search.bottom - l.search.top - S(4);
    MoveWindow(g_search, l.search.left + inset, y,
               l.search.right - l.search.left - inset - S(22), h, TRUE);
}

static void RebuildView(BOOL reset_scroll)
{
    if (!g_view)
        g_view = (int *)HeapAlloc(Heap(), 0, MAX_TRACKS * sizeof(int));
    if (!g_view) {
        g_view_n = 0;
        return;
    }
    g_view_n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (ContainsI(FileName(g_tracks[i].path), g_filter))
            g_view[g_view_n++] = i;
    }
    if (g_view_n > 0 && ViewIndex(g_sel) < 0)
        g_sel = g_view[0];
    if (reset_scroll)
        g_scroll = 0;
    ClampScroll();
}

static void FilterChanged()
{
    if (g_search)
        GetWindowTextW(g_search, g_filter, 128);
    RebuildView(TRUE);
    InvalidateRect(g_wnd, 0, FALSE);
}

static void MoveSelBy(int delta)
{
    if (g_view_n <= 0)
        return;
    int p = ViewIndex(g_sel);
    if (p < 0)
        p = 0;
    p = Clamp(p + delta, 0, g_view_n - 1);
    g_sel = g_view[p];
    EnsureVisible(g_sel);
}

static BOOL SeekAbs(HANDLE file, QWORD pos)
{
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)pos;
    return SetFilePointerEx(file, li, 0, FILE_BEGIN);
}

static BOOL ReadBytes(HANDLE file, void *buf, DWORD n)
{
    DWORD got = 0;
    return ReadFile(file, buf, n, &got, 0) && got == n;
}

static QWORD AudioOffset(const wchar_t *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;

    BYTE hdr[12];
    QWORD pos = 0;
    if (!ReadBytes(file, hdr, 10)) {
        CloseHandle(file);
        return 0;
    }

    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        pos = 10 + (((QWORD)(hdr[6] & 0x7f) << 21) | ((QWORD)(hdr[7] & 0x7f) << 14) |
                    ((QWORD)(hdr[8] & 0x7f) << 7) | (hdr[9] & 0x7f));
        if (hdr[5] & 0x10)
            pos += 10;
        if (!SeekAbs(file, pos)) {
            CloseHandle(file);
            return 0;
        }
    } else if (!SeekAbs(file, 0)) {
        CloseHandle(file);
        return 0;
    }

    if (!ReadBytes(file, hdr, 12) ||
        hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
        hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
        CloseHandle(file);
        return 0;
    }

    QWORD riff = pos;
    pos += 12;
    WORD format = 0;
    QWORD data = 0;

    for (int i = 0; i < 64; ++i) {
        BYTE ck[8];
        if (!ReadBytes(file, ck, 8))
            break;
        DWORD size = (DWORD)ck[4] | ((DWORD)ck[5] << 8) | ((DWORD)ck[6] << 16) | ((DWORD)ck[7] << 24);
        pos += 8;
        if (ck[0] == 'f' && ck[1] == 'm' && ck[2] == 't' && ck[3] == ' ') {
            BYTE fmt[2];
            if (size < 2 || !ReadBytes(file, fmt, 2))
                break;
            format = (WORD)fmt[0] | ((WORD)fmt[1] << 8);
            if (size > 2 && !SeekAbs(file, pos + size))
                break;
        } else if (ck[0] == 'd' && ck[1] == 'a' && ck[2] == 't' && ck[3] == 'a') {
            data = pos;
            break;
        } else if (!SeekAbs(file, pos + size)) {
            break;
        }
        pos += size + (size & 1);
        if (!SeekAbs(file, pos))
            break;
    }

    CloseHandle(file);
    if ((format == 0x50 || format == 0x55) && data)
        return data;
    if (riff)
        return riff;
    return 0;
}

static HSTREAM OpenDecode(const wchar_t *path)
{
    DWORD flags = BASS_UNICODE | BASS_STREAM_DECODE;
    HSTREAM s = BASS_StreamCreateFile(FALSE, path, 0, 0, flags);
    if (s)
        return s;
    QWORD off = AudioOffset(path);
    if (off)
        return BASS_StreamCreateFile(FALSE, path, off, 0, flags);
    return 0;
}

static int ProbeSecs(const wchar_t *path)
{
    HSTREAM s = OpenDecode(path);
    if (!s)
        return 0;
    QWORD bytes = BASS_ChannelGetLength(s, BASS_POS_BYTE);
    int secs = 0;
    if (bytes && bytes != (QWORD)-1)
        secs = (int)BASS_ChannelBytes2Seconds(s, bytes);
    BASS_StreamFree(s);
    return secs;
}

static int BitrateOf(HSTREAM s)
{
    float br = 0;
    if (BASS_ChannelGetAttribute(s, BASS_ATTRIB_BITRATE, &br) && br > 0)
        return (int)(br + 0.5f);
    BASS_CHANNELINFO inf;
    memset(&inf, 0, sizeof(inf));
    if (!BASS_ChannelGetInfo(s, &inf))
        return 0;
    DWORD bits = inf.origres & 0xFFFF;
    if (!bits)
        bits = 16;
    if (inf.freq && inf.chans)
        return (int)((inf.freq * inf.chans * bits) / 1000);
    return 0;
}

static void ProbePending()
{
    int n = 0;
    Layout l;
    GetLayout(&l);
    int vis = VisibleRows(&l);
    int last = g_scroll + vis;
    if (last > g_view_n)
        last = g_view_n;
    for (int i = g_scroll; i < last && n < 6; ++i) {
        int t = g_view[i];
        if (g_tracks[t].secs < 0) {
            g_tracks[t].secs = ProbeSecs(g_tracks[t].path);
            n++;
        }
    }
    for (int i = 0; i < g_count && n < 6; ++i) {
        if (g_tracks[i].secs < 0) {
            g_tracks[i].secs = ProbeSecs(g_tracks[i].path);
            n++;
        }
    }
}

static void ShowPlayError(const wchar_t *path)
{
    int err = BASS_ErrorGetCode();
    wchar_t msg[640];
    msg[0] = 0;
    if (err == BASS_ERROR_FILEFORM || err == BASS_ERROR_CODEC ||
        err == BASS_ERROR_NOTAUDIO || err == BASS_ERROR_UNSTREAMABLE) {
        Append(msg, 640, L"This format is not supported:\n\n");
        Append(msg, 640, FileName(path));
        Append(msg, 640,
               L"\n\nSupported formats:\n"
               L"MP3 (.mp3)\n"
               L"MP2 (.mp2)\n"
               L"MP1 (.mp1)\n"
               L"OGG (.ogg)\n"
               L"WAV (.wav)\n"
               L"AIFF (.aiff, .aif)");
    } else if (err == BASS_ERROR_FILEOPEN || err == BASS_ERROR_DENIED) {
        Append(msg, 640, L"Cannot open file:\n\n");
        Append(msg, 640, FileName(path));
    } else {
        Append(msg, 640, L"Failed to play:\n\n");
        Append(msg, 640, FileName(path));
    }
    MessageBoxW(g_wnd, msg, APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static BOOL Grow()
{
    if (g_count >= MAX_TRACKS)
        return FALSE;
    if (g_count < g_cap)
        return TRUE;
    int cap = g_cap ? g_cap * 2 : 64;
    if (cap > MAX_TRACKS)
        cap = MAX_TRACKS;
    SIZE_T bytes = (SIZE_T)cap * sizeof(Track);
    void *p = g_tracks
        ? HeapReAlloc(Heap(), HEAP_ZERO_MEMORY, g_tracks, bytes)
        : HeapAlloc(Heap(), HEAP_ZERO_MEMORY, bytes);
    if (!p)
        return FALSE;
    g_tracks = (Track *)p;
    g_cap = cap;
    return TRUE;
}

static int FindPath(const wchar_t *path)
{
    for (int i = 0; i < g_count; ++i) {
        if (EqI(g_tracks[i].path, path))
            return i;
    }
    return -1;
}

static int AddTrack(const wchar_t *path)
{
    if (!path || !path[0] || !IsAudio(path))
        return -1;
    int exist = FindPath(path);
    if (exist >= 0)
        return exist;
    if (!Grow())
        return -1;
    Copy(g_tracks[g_count].path, path, MAX_PATH);
    g_tracks[g_count].secs = -1;
    if (g_sel < 0)
        g_sel = g_count;
    return g_count++;
}

static void JoinPath(wchar_t *out, const wchar_t *dir, const wchar_t *name)
{
    Copy(out, dir, MAX_PATH);
    int n = Len(out);
    if (n && out[n - 1] != L'\\' && out[n - 1] != L'/' && n < MAX_PATH - 1) {
        out[n++] = L'\\';
        out[n] = 0;
    }
    Append(out, MAX_PATH, name);
}

static void WalkDir(const wchar_t *dir, int depth)
{
    if (depth > WALK_DEPTH)
        return;
    wchar_t spec[MAX_PATH];
    JoinPath(spec, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        wchar_t path[MAX_PATH];
        JoinPath(path, dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            WalkDir(path, depth + 1);
        else
            AddTrack(path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int AddPath(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    int before = g_count;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        WalkDir(path, 0);
    else
        AddTrack(path);
    if (!g_loading)
        RebuildView(FALSE);
    return g_count - before;
}

static BOOL WriteBytes(HANDLE f, const void *p, DWORD n)
{
    DWORD w = 0;
    return WriteFile(f, p, n, &w, 0) && w == n;
}

static BOOL WriteStr(HANDLE f, const char *s)
{
    int n = 0;
    while (s[n]) ++n;
    return WriteBytes(f, s, (DWORD)n);
}

static BOOL PlaylistPath(wchar_t *out)
{
    if (!GetEnvironmentVariableW(L"APPDATA", out, MAX_PATH))
        return FALSE;
    Append(out, MAX_PATH, L"\\iliz");
    CreateDirectoryW(out, 0);
    Append(out, MAX_PATH, L"\\iliz_mcp_player");
    CreateDirectoryW(out, 0);
    Append(out, MAX_PATH, L"\\playlist.m3u8");
    return TRUE;
}

static void SavePlaylist()
{
    if (g_loading)
        return;
    wchar_t path[MAX_PATH];
    if (!PlaylistPath(path))
        return;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE)
        return;
    WriteStr(f, "#EXTM3U\r\n");
    for (int i = 0; i < g_count; ++i) {
        WriteStr(f, "#EXTINF:");
        int secs = g_tracks[i].secs;
        if (secs < 0)
            secs = 0;
        char num[12];
        int n = 0, v = secs;
        char tmp[12];
        int t = 0;
        if (v == 0)
            tmp[t++] = '0';
        while (v && t < 11) {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        }
        while (t)
            num[n++] = tmp[--t];
        num[n] = 0;
        WriteStr(f, num);
        WriteStr(f, ",\r\n");
        char utf[MAX_PATH * 3];
        int bytes = WideCharToMultiByte(CP_UTF8, 0, g_tracks[i].path, -1, utf, sizeof(utf), 0, 0);
        if (bytes > 1)
            WriteBytes(f, utf, (DWORD)(bytes - 1));
        WriteStr(f, "\r\n");
    }
    CloseHandle(f);
}

static void LoadPlaylist()
{
    wchar_t path[MAX_PATH];
    if (!PlaylistPath(path))
        return;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(f);
        return;
    }
    DWORD n = (DWORD)sz.QuadPart;
    char *buf = (char *)HeapAlloc(Heap(), 0, n + 1);
    if (!buf) {
        CloseHandle(f);
        return;
    }
    DWORD got = 0;
    if (!ReadFile(f, buf, n, &got, 0))
        got = 0;
    CloseHandle(f);
    buf[got] = 0;

    g_loading = TRUE;
    DWORD i = 0;
    if (got >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF)
        i = 3;
    while (i < got) {
        DWORD start = i;
        while (i < got && buf[i] != '\n' && buf[i] != '\r')
            ++i;
        DWORD len = i - start;
        while (i < got && (buf[i] == '\n' || buf[i] == '\r'))
            ++i;
        if (!len || buf[start] == '#')
            continue;
        while (len && (buf[start + len - 1] == ' ' || buf[start + len - 1] == '\t'))
            --len;
        if (!len)
            continue;
        wchar_t item[MAX_PATH];
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf + start, (int)len, item, MAX_PATH - 1);
        if (wlen <= 0)
            continue;
        item[wlen] = 0;
        AddTrack(item);
    }
    g_loading = FALSE;
    HeapFree(Heap(), 0, buf);
    RebuildView(FALSE);
    if (g_count) {
        g_sel = Clamp(g_sel, 0, g_count - 1);
        EnsureVisible(g_sel);
    } else {
        g_sel = -1;
    }
}

static BOOL PlayIndex(int i, BOOL show_err)
{
    if (i < 0 || i >= g_count)
        return FALSE;
    const wchar_t *path = g_tracks[i].path;
    DWORD flags = BASS_UNICODE | BASS_SAMPLE_FLOAT;
    HSTREAM next = BASS_StreamCreateFile(FALSE, path, 0, 0, flags);
    if (!next) {
        QWORD off = AudioOffset(path);
        if (off)
            next = BASS_StreamCreateFile(FALSE, path, off, 0, flags);
    }
    if (!next) {
        if (show_err)
            ShowPlayError(path);
        return FALSE;
    }
    if (g_stream)
        BASS_StreamFree(g_stream);
    g_stream = next;
    ApplyVol();
    BASS_ChannelPlay(g_stream, FALSE);
    g_cur = i;
    g_sel = i;
    g_kbps = BitrateOf(g_stream);
    QWORD bytes = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
    if (bytes && bytes != (QWORD)-1)
        g_tracks[i].secs = (int)BASS_ChannelBytes2Seconds(g_stream, bytes);
    Copy(g_title, FileName(path), MAX_PATH);
    SetWindowTextW(g_wnd, g_title);
    TrayTip();
    EnsureVisible(i);
    InvalidateRect(g_wnd, 0, FALSE);
    return TRUE;
}

static BOOL PlayNext(BOOL wrap)
{
    if (g_count <= 0)
        return FALSE;
    if (g_shuffle) {
        int tries = g_count * 2;
        while (tries-- > 0) {
            int i = (int)(RandU() % (unsigned)g_count);
            if (i == g_cur && g_count > 1)
                continue;
            if (PlayIndex(i, FALSE))
                return TRUE;
        }
        return FALSE;
    }
    int start = g_cur < 0 ? 0 : g_cur + 1;
    if (start >= g_count) {
        if (!wrap)
            return FALSE;
        start = 0;
    }
    int i = start;
    for (;;) {
        if (PlayIndex(i, FALSE))
            return TRUE;
        i++;
        if (i >= g_count)
            i = 0;
        if (i == start)
            return FALSE;
    }
}

static void PlayPrev()
{
    if (g_count <= 0)
        return;
    if (g_stream) {
        QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
        if (BASS_ChannelBytes2Seconds(g_stream, at) > 3) {
            BASS_ChannelSetPosition(g_stream, 0, BASS_POS_BYTE);
            BASS_ChannelPlay(g_stream, FALSE);
            InvalidateRect(g_wnd, 0, FALSE);
            return;
        }
    }
    int i = g_cur <= 0 ? g_count - 1 : g_cur - 1;
    int guard = i;
    for (;;) {
        if (PlayIndex(i, FALSE))
            return;
        i = i <= 0 ? g_count - 1 : i - 1;
        if (i == guard)
            return;
    }
}

static void TogglePause()
{
    if (!g_stream) {
        if (g_sel >= 0)
            PlayIndex(g_sel, TRUE);
        return;
    }
    if (BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING)
        BASS_ChannelPause(g_stream);
    else
        BASS_ChannelPlay(g_stream, FALSE);
    InvalidateRect(g_wnd, 0, FALSE);
}

static void StopPlayback()
{
    if (g_stream) {
        BASS_StreamFree(g_stream);
        g_stream = 0;
    }
    g_cur = -1;
    g_kbps = 0;
    Copy(g_title, APP_NAME, MAX_PATH);
    if (g_wnd) {
        SetWindowTextW(g_wnd, g_title);
        TrayTip();
        InvalidateRect(g_wnd, 0, FALSE);
    }
}

static void JPut(HttpJob *j, const char *s)
{
    while (*s && j->n < j->cap - 1)
        j->out[j->n++] = *s++;
    j->out[j->n] = 0;
}

static void JChar(HttpJob *j, char c)
{
    if (j->n < j->cap - 1) {
        j->out[j->n++] = c;
        j->out[j->n] = 0;
    }
}

static void JInt(HttpJob *j, int v)
{
    char tmp[16];
    int n = 0;
    if (v < 0) {
        JChar(j, '-');
        v = -v;
    }
    if (v == 0)
        tmp[n++] = '0';
    while (v && n < 15) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        JChar(j, tmp[--n]);
}

static void JEsc(HttpJob *j, const char *s)
{
    JChar(j, '"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            JChar(j, '\\');
            JChar(j, (char)c);
        } else if (c < 32) {
            JPut(j, "\\u00");
            JChar(j, "0123456789abcdef"[c >> 4]);
            JChar(j, "0123456789abcdef"[c & 15]);
        } else {
            JChar(j, (char)c);
        }
    }
    JChar(j, '"');
}

static void JWstr(HttpJob *j, const wchar_t *w)
{
    char utf[MAX_PATH * 3];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, utf, sizeof(utf), 0, 0);
    if (n <= 0) {
        JPut(j, "\"\"");
        return;
    }
    JEsc(j, utf);
}

static void JErr(HttpJob *j, const char *err)
{
    j->n = 0;
    JPut(j, "{\"ok\":false,\"error\":");
    JEsc(j, err);
    JPut(j, "}");
}

static void TrackJson(HttpJob *j, int i)
{
    JPut(j, "{\"id\":");
    JInt(j, i);
    JPut(j, ",\"title\":");
    JWstr(j, FileName(g_tracks[i].path));
    if (!j->slim) {
        JPut(j, ",\"path\":");
        JWstr(j, g_tracks[i].path);
    }
    JPut(j, ",\"duration\":");
    JInt(j, g_tracks[i].secs < 0 ? 0 : g_tracks[i].secs);
    JChar(j, '}');
}

static void HttpSearch(HttpJob *j)
{
    int total = 0;
    int n = 0;
    int first[HTTP_LIMIT];
    for (int i = 0; i < g_count; ++i) {
        if (!ContainsI(FileName(g_tracks[i].path), j->q))
            continue;
        if (n < HTTP_LIMIT)
            first[n] = i;
        n++;
        total++;
    }
    if (n > HTTP_LIMIT)
        n = HTTP_LIMIT;
    JPut(j, "{\"ok\":true,\"query\":");
    JWstr(j, j->q);
    JPut(j, ",\"count\":");
    JInt(j, total);
    JPut(j, ",\"returned\":");
    JInt(j, n);
    JPut(j, ",\"tracks\":[");
    for (int i = 0; i < n; ++i) {
        if (i)
            JChar(j, ',');
        TrackJson(j, first[i]);
    }
    JPut(j, "]}");
}

static void HttpPlay(HttpJob *j)
{
    int i = j->id;
    if (i < 0 && j->q[0]) {
        for (int t = 0; t < g_count; ++t) {
            if (ContainsI(FileName(g_tracks[t].path), j->q)) {
                i = t;
                break;
            }
        }
    }
    if (i < 0) {
        JErr(j, j->q[0] ? "no match" : "id or q required");
        return;
    }
    if (!PlayIndex(i, FALSE)) {
        JErr(j, "cannot play");
        return;
    }
    JPut(j, "{\"ok\":true,\"id\":");
    JInt(j, i);
    JPut(j, ",\"title\":");
    JWstr(j, FileName(g_tracks[i].path));
    if (!j->slim) {
        JPut(j, ",\"path\":");
        JWstr(j, g_tracks[i].path);
    }
    JPut(j, "}");
}

static void HttpStop(HttpJob *j)
{
    StopPlayback();
    JPut(j, "{\"ok\":true}");
}

static void HttpOp(HttpJob *j)
{
    j->n = 0;
    if (j->op == HTTP_SEARCH)
        HttpSearch(j);
    else if (j->op == HTTP_PLAY)
        HttpPlay(j);
    else if (j->op == HTTP_STOP)
        HttpStop(j);
    else
        JErr(j, "unknown endpoint");
}

static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void UrlDecode(const char *in, int in_n, wchar_t *out, int cap)
{
    char tmp[512];
    int n = 0;
    for (int i = 0; i < in_n && n < (int)sizeof(tmp) - 1; ) {
        if (in[i] == '%' && i + 2 < in_n) {
            int h = HexVal(in[i + 1]);
            int l = HexVal(in[i + 2]);
            if (h >= 0 && l >= 0) {
                tmp[n++] = (char)((h << 4) | l);
                i += 3;
                continue;
            }
        }
        tmp[n++] = in[i] == '+' ? ' ' : in[i];
        ++i;
    }
    tmp[n] = 0;
    wchar_t wide[512];
    int w = MultiByteToWideChar(CP_UTF8, 0, tmp, n, wide, 511);
    if (w <= 0) {
        out[0] = 0;
        return;
    }
    wide[w] = 0;
    Copy(out, wide, cap);
}

static BOOL PathEq(const char *path, int n, const char *want)
{
    int i = 0;
    while (want[i]) {
        if (i >= n || path[i] != want[i])
            return FALSE;
        ++i;
    }
    return i == n;
}

static BOOL QueryVal(const char *qs, const char *key, wchar_t *out, int cap)
{
    int klen = 0;
    while (key[klen])
        ++klen;
    out[0] = 0;
    if (!qs)
        return FALSE;
    while (*qs && *qs != ' ' && *qs != '\r' && *qs != '\n') {
        const char *amp = qs;
        while (*amp && *amp != '&' && *amp != ' ' && *amp != '\r' && *amp != '\n')
            ++amp;
        const char *eq = qs;
        while (eq < amp && *eq != '=')
            ++eq;
        int kn = (int)(eq - qs);
        if (kn == klen) {
            int same = 1;
            for (int i = 0; i < kn; ++i) {
                if (qs[i] != key[i])
                    same = 0;
            }
            if (same) {
                if (eq < amp)
                    ++eq;
                UrlDecode(eq, (int)(amp - eq), out, cap);
                return TRUE;
            }
        }
        qs = *amp == '&' ? amp + 1 : amp;
    }
    return FALSE;
}

static int QueryInt(const char *qs, const char *key)
{
    wchar_t tmp[16];
    if (!QueryVal(qs, key, tmp, 16) || !tmp[0])
        return -1;
    int v = 0;
    for (int i = 0; tmp[i]; ++i) {
        if (tmp[i] < L'0' || tmp[i] > L'9')
            return -1;
        v = v * 10 + (tmp[i] - L'0');
    }
    return v;
}

static const char *JSkipWs(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        ++s;
    return s;
}

static const char *JSkipStr(const char *s)
{
    if (*s != '"')
        return s;
    ++s;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1])
            s += 2;
        else
            ++s;
    }
    if (*s == '"')
        ++s;
    return s;
}

static const char *JSkipVal(const char *s)
{
    s = JSkipWs(s);
    if (*s == '"')
        return JSkipStr(s);
    if (*s == '{' || *s == '[') {
        char open = *s;
        char close = open == '{' ? '}' : ']';
        int d = 1;
        ++s;
        while (*s && d) {
            if (*s == '"') {
                s = JSkipStr(s);
                continue;
            }
            if (*s == open)
                ++d;
            else if (*s == close)
                --d;
            ++s;
        }
        return s;
    }
    if (*s == 't' || *s == 'f' || *s == 'n' || *s == '-' || (*s >= '0' && *s <= '9')) {
        while (*s && *s != ',' && *s != '}' && *s != ']' && *s != ' ' &&
               *s != '\t' && *s != '\r' && *s != '\n')
            ++s;
        return s;
    }
    return s;
}

static int JKeyEq(const char *s, const char *key)
{
    if (*s != '"')
        return 0;
    ++s;
    while (*key && *s == *key) {
        ++s;
        ++key;
    }
    return *key == 0 && *s == '"';
}

static const char *JFind(const char *obj, const char *key)
{
    obj = JSkipWs(obj);
    if (*obj != '{')
        return 0;
    ++obj;
    while (*obj && *obj != '}') {
        obj = JSkipWs(obj);
        if (*obj != '"')
            break;
        int hit = JKeyEq(obj, key);
        obj = JSkipStr(obj);
        obj = JSkipWs(obj);
        if (*obj == ':')
            ++obj;
        obj = JSkipWs(obj);
        if (hit)
            return obj;
        obj = JSkipVal(obj);
        obj = JSkipWs(obj);
        if (*obj == ',')
            ++obj;
    }
    return 0;
}

static void JCopyRaw(const char *v, char *out, int cap)
{
    v = JSkipWs(v);
    int i = 0;
    if (*v == '"') {
        const char *e = JSkipStr(v);
        while (v < e && i < cap - 1)
            out[i++] = *v++;
        out[i] = 0;
        return;
    }
    while (*v && *v != ',' && *v != '}' && *v != ']' && *v != ' ' &&
           *v != '\t' && *v != '\r' && *v != '\n' && i < cap - 1)
        out[i++] = *v++;
    out[i] = 0;
}

static void JCopyStr(const char *v, char *out, int cap)
{
    v = JSkipWs(v);
    out[0] = 0;
    if (*v != '"')
        return;
    ++v;
    int i = 0;
    while (*v && *v != '"' && i < cap - 1) {
        if (*v == '\\' && v[1]) {
            char e = v[1];
            if (e == 'n')
                out[i++] = '\n';
            else if (e == 't')
                out[i++] = '\t';
            else if (e == 'r')
                out[i++] = '\r';
            else
                out[i++] = e;
            v += 2;
        } else {
            out[i++] = *v++;
        }
    }
    out[i] = 0;
}

static int JCopyInt(const char *v)
{
    v = JSkipWs(v);
    int neg = 0, n = 0, any = 0;
    if (*v == '-') {
        neg = 1;
        ++v;
    }
    while (*v >= '0' && *v <= '9') {
        n = n * 10 + (*v - '0');
        any = 1;
        ++v;
    }
    if (!any)
        return -1;
    return neg ? -n : n;
}

static int CEqN(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb + 32);
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int CEq(const char *a, const char *b)
{
    int n = 0;
    while (b[n])
        ++n;
    int i = 0;
    while (a[i])
        ++i;
    return i == n && CEqN(a, b, n);
}

static int HdrInt(const char *req, const char *name)
{
    int nlen = 0;
    while (name[nlen])
        ++nlen;
    for (const char *p = req; *p; ++p) {
        if (p[0] != '\r' || p[1] != '\n')
            continue;
        p += 2;
        if (p[0] == '\r' && p[1] == '\n')
            break;
        if (!CEqN(p, name, nlen) || p[nlen] != ':')
            continue;
        p += nlen + 1;
        while (*p == ' ')
            ++p;
        int v = 0, any = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            any = 1;
            ++p;
        }
        return any ? v : 0;
    }
    return 0;
}

static int IsOkFalse(const char *s)
{
    const char *p = "{\"ok\":false";
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }
    return 1;
}

static void HttpWrite(SOCKET s, const char *p, int n)
{
    while (n > 0) {
        int w = send(s, p, n, 0);
        if (w <= 0)
            return;
        p += w;
        n -= w;
    }
}

static void HttpReply(SOCKET s, int status, const char *body, int n)
{
    char hdr[640];
    int i = 0;
    const char *pre = status == 404 ? "HTTP/1.1 404 Not Found\r\n" :
                      status == 400 ? "HTTP/1.1 400 Bad Request\r\n" :
                      status == 202 ? "HTTP/1.1 202 Accepted\r\n" :
                      status == 204 ? "HTTP/1.1 204 No Content\r\n" :
                      status == 405 ? "HTTP/1.1 405 Method Not Allowed\r\n" :
                                      "HTTP/1.1 200 OK\r\n";
    while (pre[i]) {
        hdr[i] = pre[i];
        ++i;
    }
    const char *mid =
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Session-Id, MCP-Protocol-Version\r\n"
        "Access-Control-Expose-Headers: Mcp-Session-Id, MCP-Protocol-Version\r\n"
        "Mcp-Session-Id: iliz\r\n"
        "Connection: close\r\n"
        "Content-Length: ";
    for (int k = 0; mid[k] && i < 600; ++k)
        hdr[i++] = mid[k];
    char num[12];
    int tn = 0, v = n;
    if (v < 0)
        v = 0;
    if (v == 0)
        num[tn++] = '0';
    while (v && tn < 11) {
        num[tn++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (tn)
        hdr[i++] = num[--tn];
    hdr[i++] = '\r';
    hdr[i++] = '\n';
    hdr[i++] = '\r';
    hdr[i++] = '\n';
    HttpWrite(s, hdr, i);
    if (n > 0)
        HttpWrite(s, body, n);
}

static void McpWrap(HttpJob *j, const char *idraw, const char *inner)
{
    j->n = 0;
    JPut(j, "{\"jsonrpc\":\"2.0\",\"id\":");
    JPut(j, idraw && idraw[0] ? idraw : "null");
    JPut(j, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    JEsc(j, inner);
    JPut(j, "}],\"isError\":");
    JPut(j, IsOkFalse(inner) ? "true" : "false");
    JPut(j, "}}");
}

static void McpInit(HttpJob *j, const char *idraw, const char *body)
{
    char ver[24];
    ver[0] = 0;
    const char *params = JFind(body, "params");
    if (params) {
        const char *pv = JFind(params, "protocolVersion");
        if (pv)
            JCopyStr(pv, ver, 24);
    }
    j->n = 0;
    JPut(j, "{\"jsonrpc\":\"2.0\",\"id\":");
    JPut(j, idraw && idraw[0] ? idraw : "null");
    JPut(j, ",\"result\":{\"protocolVersion\":");
    if (ver[0] && ((ver[0] == '2' && ver[1] == '0' && ver[2] == '2' &&
                    (ver[3] == '4' || ver[3] == '5'))))
        JEsc(j, ver);
    else
        JEsc(j, "2025-03-26");
    JPut(j, ",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"iliz-mcp-player\",\"version\":");
    JEsc(j, APP_VERSION_A);
    JPut(j, "}}}");
}

static void McpTools(HttpJob *j, const char *idraw)
{
    j->n = 0;
    JPut(j, "{\"jsonrpc\":\"2.0\",\"id\":");
    JPut(j, idraw && idraw[0] ? idraw : "null");
    JPut(j,
         ",\"result\":{\"tools\":["
         "{\"name\":\"search\",\"description\":\"Search playlist titles. Empty query returns the playlist (capped).\","
         "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}}}},"
         "{\"name\":\"play\",\"description\":\"Play by id (preferred) or first search hit for query.\","
         "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"number\"},\"query\":{\"type\":\"string\"}}}},"
         "{\"name\":\"stop\",\"description\":\"Stop playback (not pause).\","
         "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
         "]}}");
}

static int McpRun(HttpJob *j)
{
    DWORD_PTR handled = 0;
    if (!g_wnd || !SendMessageTimeoutW(g_wnd, WM_HTTP, 0, (LPARAM)j,
                                       SMTO_ABORTIFHUNG, 2000, &handled)) {
        JErr(j, "player not ready");
        return 0;
    }
    return 1;
}

static void McpCall(HttpJob *j, const char *idraw, const char *body)
{
    const char *params = JFind(body, "params");
    char name[32];
    name[0] = 0;
    if (params) {
        const char *nv = JFind(params, "name");
        if (nv)
            JCopyStr(nv, name, 32);
    }
    const char *args = params ? JFind(params, "arguments") : 0;
    j->slim = 1;
    j->id = -1;
    j->q[0] = 0;
    j->n = 0;
    if (args) {
        const char *idv = JFind(args, "id");
        if (idv)
            j->id = JCopyInt(idv);
        char qutf[256];
        qutf[0] = 0;
        const char *qv = JFind(args, "query");
        if (qv) {
            JCopyStr(qv, qutf, 256);
            MultiByteToWideChar(CP_UTF8, 0, qutf, -1, j->q, 256);
            j->q[255] = 0;
        }
    }
    if (CEq(name, "search"))
        j->op = HTTP_SEARCH;
    else if (CEq(name, "play"))
        j->op = HTTP_PLAY;
    else if (CEq(name, "stop"))
        j->op = HTTP_STOP;
    else {
        JErr(j, "unknown endpoint");
        char inner[256];
        int n = 0;
        while (j->out[n] && n < 255) {
            inner[n] = j->out[n];
            ++n;
        }
        inner[n] = 0;
        McpWrap(j, idraw, inner);
        return;
    }
    McpRun(j);
    char *inner = (char *)HeapAlloc(Heap(), 0, (SIZE_T)j->n + 1);
    if (!inner)
        return;
    for (int i = 0; i < j->n; ++i)
        inner[i] = j->out[i];
    inner[j->n] = 0;
    McpWrap(j, idraw, inner);
    HeapFree(Heap(), 0, inner);
}

static void McpDispatch(SOCKET s, const char *http_m, const char *body)
{
    HttpJob job;
    memset(&job, 0, sizeof(job));
    job.id = -1;
    job.cap = 64 * 1024;
    job.out = (char *)HeapAlloc(Heap(), HEAP_ZERO_MEMORY, job.cap);
    if (!job.out) {
        HttpReply(s, 400, "{\"ok\":false,\"error\":\"oom\"}", 26);
        return;
    }
    if (CEq(http_m, "OPTIONS")) {
        HttpReply(s, 204, "", 0);
        HeapFree(Heap(), 0, job.out);
        return;
    }
    if (CEq(http_m, "DELETE")) {
        HttpReply(s, 200, "{\"jsonrpc\":\"2.0\",\"result\":{}}", 29);
        HeapFree(Heap(), 0, job.out);
        return;
    }
    if (CEq(http_m, "GET")) {
        HttpReply(s, 405, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"Method not allowed\"}}", 72);
        HeapFree(Heap(), 0, job.out);
        return;
    }
    if (!body)
        body = "{}";
    char method[64];
    method[0] = 0;
    const char *mv = JFind(body, "method");
    if (mv)
        JCopyStr(mv, method, 64);
    char idraw[48];
    idraw[0] = 0;
    const char *iv = JFind(body, "id");
    if (iv)
        JCopyRaw(iv, idraw, 48);

    int notify = 0;
    const char *np = "notifications/";
    int ni = 0;
    while (np[ni] && method[ni] == np[ni])
        ++ni;
    if (np[ni] == 0)
        notify = 1;

    if (notify) {
        HttpReply(s, 202, "", 0);
        HeapFree(Heap(), 0, job.out);
        return;
    }
    if (CEq(method, "initialize"))
        McpInit(&job, idraw, body);
    else if (CEq(method, "tools/list"))
        McpTools(&job, idraw);
    else if (CEq(method, "tools/call"))
        McpCall(&job, idraw, body);
    else if (CEq(method, "ping")) {
        job.n = 0;
        JPut(&job, "{\"jsonrpc\":\"2.0\",\"id\":");
        JPut(&job, idraw[0] ? idraw : "null");
        JPut(&job, ",\"result\":{}}");
    } else if (CEq(method, "resources/list")) {
        job.n = 0;
        JPut(&job, "{\"jsonrpc\":\"2.0\",\"id\":");
        JPut(&job, idraw[0] ? idraw : "null");
        JPut(&job, ",\"result\":{\"resources\":[]}}");
    } else if (CEq(method, "prompts/list")) {
        job.n = 0;
        JPut(&job, "{\"jsonrpc\":\"2.0\",\"id\":");
        JPut(&job, idraw[0] ? idraw : "null");
        JPut(&job, ",\"result\":{\"prompts\":[]}}");
    } else {
        job.n = 0;
        JPut(&job, "{\"jsonrpc\":\"2.0\",\"id\":");
        JPut(&job, idraw[0] ? idraw : "null");
        JPut(&job, ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    }
    HttpReply(s, 200, job.out, job.n);
    HeapFree(Heap(), 0, job.out);
}

static void HttpClient(SOCKET s)
{
    const int cap = 65536;
    char *req = (char *)HeapAlloc(Heap(), HEAP_ZERO_MEMORY, cap);
    if (!req)
        return;
    int got = 0;
    int hdr = -1;
    while (got < cap - 1) {
        int r = recv(s, req + got, cap - 1 - got, 0);
        if (r <= 0)
            break;
        got += r;
        req[got] = 0;
        if (hdr < 0) {
            for (int i = 0; i + 3 < got; ++i) {
                if (req[i] == '\r' && req[i + 1] == '\n' &&
                    req[i + 2] == '\r' && req[i + 3] == '\n') {
                    hdr = i + 4;
                    break;
                }
            }
        }
        if (hdr >= 0) {
            int cl = HdrInt(req, "content-length");
            if (got >= hdr + cl)
                break;
        }
    }
    if (got <= 0 || hdr < 0) {
        HeapFree(Heap(), 0, req);
        return;
    }
    req[got] = 0;

    char http_m[8];
    int mi = 0;
    const char *p = req;
    while (*p && *p != ' ' && mi < 7)
        http_m[mi++] = *p++;
    http_m[mi] = 0;
    while (*p == ' ')
        ++p;
    const char *path = p;
    while (*p && *p != ' ' && *p != '?' && *p != '\r')
        ++p;
    int path_n = (int)(p - path);
    const char *qs = 0;
    if (*p == '?') {
        qs = p + 1;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n')
            ++p;
        if (*p)
            *(char *)p = 0;
    }

    if (PathEq(path, path_n, "/mcp")) {
        int cl = HdrInt(req, "content-length");
        const char *body = req + hdr;
        if (hdr + cl < got)
            req[hdr + cl] = 0;
        McpDispatch(s, http_m, body);
        HeapFree(Heap(), 0, req);
        return;
    }

    HttpJob job;
    memset(&job, 0, sizeof(job));
    job.id = -1;
    job.cap = 64 * 1024;
    job.out = (char *)HeapAlloc(Heap(), HEAP_ZERO_MEMORY, job.cap);
    if (!job.out) {
        HttpReply(s, 400, "{\"ok\":false,\"error\":\"oom\"}", 26);
        HeapFree(Heap(), 0, req);
        return;
    }

    int status = 200;
    if (PathEq(path, path_n, "/search")) {
        job.op = HTTP_SEARCH;
        QueryVal(qs, "q", job.q, 256);
    } else if (PathEq(path, path_n, "/play")) {
        job.op = HTTP_PLAY;
        job.id = QueryInt(qs, "id");
        QueryVal(qs, "q", job.q, 256);
    } else if (PathEq(path, path_n, "/stop")) {
        job.op = HTTP_STOP;
    } else {
        JErr(&job, "unknown endpoint");
        status = 404;
        HttpReply(s, status, job.out, job.n);
        HeapFree(Heap(), 0, job.out);
        HeapFree(Heap(), 0, req);
        return;
    }

    DWORD_PTR handled = 0;
    if (!g_wnd || !SendMessageTimeoutW(g_wnd, WM_HTTP, 0, (LPARAM)&job,
                                       SMTO_ABORTIFHUNG, 2000, &handled))
        JErr(&job, "player not ready");
    HttpReply(s, status, job.out, job.n);
    HeapFree(Heap(), 0, job.out);
    HeapFree(Heap(), 0, req);
}

static DWORD WINAPI HttpThread(void *)
{
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET)
        return 0;
    int on = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((u_short)g_http_port);
    if (bind(ls, (sockaddr *)&a, sizeof(a)) != 0 || listen(ls, 8) != 0) {
        closesocket(ls);
        return 0;
    }
    g_http_listen = ls;
    for (;;) {
        SOCKET c = accept(ls, 0, 0);
        if (c == INVALID_SOCKET)
            break;
        HttpClient(c);
        closesocket(c);
    }
    return 0;
}

static void HttpStart()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return;
    HANDLE th = CreateThread(0, 0, HttpThread, 0, 0, 0);
    if (th)
        CloseHandle(th);
}

static void HttpShutdown()
{
    SOCKET ls = g_http_listen;
    g_http_listen = INVALID_SOCKET;
    if (ls != INVALID_SOCKET)
        closesocket(ls);
}

static void RemoveSel()
{
    if (g_sel < 0 || g_sel >= g_count)
        return;
    int i = g_sel;
    if (i == g_cur && g_stream) {
        BASS_StreamFree(g_stream);
        g_stream = 0;
        g_cur = -1;
        g_kbps = 0;
        Copy(g_title, APP_NAME, MAX_PATH);
        SetWindowTextW(g_wnd, g_title);
        TrayTip();
    }
    for (int n = i; n < g_count - 1; ++n)
        g_tracks[n] = g_tracks[n + 1];
    g_count--;
    if (g_cur > i)
        g_cur--;
    if (g_sel >= g_count)
        g_sel = g_count - 1;
    RebuildView(FALSE);
    ClampScroll();
    SavePlaylist();
    InvalidateRect(g_wnd, 0, FALSE);
}

static void SeekAt(int x)
{
    Layout l;
    GetLayout(&l);
    int w = l.seek.right - l.seek.left;
    if (!g_stream || w <= 0)
        return;
    x -= l.seek.left;
    x = Clamp(x, 0, w);
    QWORD len = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
    BASS_ChannelSetPosition(g_stream, len * (QWORD)x / (QWORD)w, BASS_POS_BYTE);
    if (BASS_ChannelIsActive(g_stream) != BASS_ACTIVE_PLAYING)
        BASS_ChannelPlay(g_stream, FALSE);
    InvalidateRect(g_wnd, 0, FALSE);
}

static void VolAt(int x)
{
    Layout l;
    GetLayout(&l);
    int w = l.vol.right - l.vol.left;
    if (w <= 0)
        return;
    if (x < l.vol.left)
        return;
    x -= l.vol.left;
    g_vol = Clamp(MulDiv(x, 100, w), 0, 100);
    if (g_vol > 0)
        g_vol_pre = g_vol;
    ApplyVol();
    InvalidateRect(g_wnd, 0, FALSE);
}

static int Seconds(QWORD bytes)
{
    if (!g_stream || bytes == (QWORD)-1)
        return 0;
    return (int)BASS_ChannelBytes2Seconds(g_stream, bytes);
}

static wchar_t *PutTime(wchar_t *out, int secs)
{
    int m = secs / 60, s = secs % 60;
    if (m >= 100) m = 99;
    if (m >= 10)
        *out++ = (wchar_t)(L'0' + m / 10);
    *out++ = (wchar_t)(L'0' + m % 10);
    *out++ = L':';
    *out++ = (wchar_t)(L'0' + s / 10);
    *out++ = (wchar_t)(L'0' + s % 10);
    return out;
}

static void TimeText(wchar_t *out, int pos, int len)
{
    out = PutTime(out, pos);
    *out++ = L' ';
    *out++ = L'/';
    *out++ = L' ';
    out = PutTime(out, len);
    *out = 0;
}

static void DrawSeekLabels(HDC dc, const RECT *bar, COLORREF color, const wchar_t *time)
{
    SetTextColor(dc, color);
    SIZE sz = { 0 };
    GetTextExtentPoint32W(dc, time, Len(time), &sz);
    RECT r = { bar->left + S(8), bar->top - S(1), bar->right - S(8), bar->bottom - S(1) };
    RECT t = r;
    t.left = t.right - sz.cx;
    DrawTextW(dc, time, -1, &t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    wchar_t label[MAX_PATH + 4];
    const wchar_t *name = g_title;
    if (g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PAUSED) {
        label[0] = 0x23F8;
        label[1] = L' ';
        Copy(label + 2, g_title, MAX_PATH);
        name = label;
    }
    r.right = t.left - S(8);
    DrawTextW(dc, name, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void FillRound(HDC dc, const RECT *r, int rad, COLORREF c)
{
    HRGN rg = CreateRoundRectRgn(r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);
    HBRUSH brush = CreateSolidBrush(c);
    FillRgn(dc, rg, brush);
    DeleteObject(brush);
    DeleteObject(rg);
}

static void DrawBar(HDC dc, const RECT *bar, int fill, COLORREF back, COLORREF fg)
{
    int rad = S(RADIUS);
    HRGN pill = CreateRoundRectRgn(bar->left, bar->top, bar->right, bar->bottom + 1, rad, rad);
    HBRUSH brush = CreateSolidBrush(back);
    FillRgn(dc, pill, brush);
    DeleteObject(brush);
    if (fill > 0) {
        HRGN clip = CreateRectRgn(bar->left, bar->top, bar->left + fill, bar->bottom);
        CombineRgn(clip, clip, pill, RGN_AND);
        brush = CreateSolidBrush(fg);
        FillRgn(dc, clip, brush);
        DeleteObject(brush);
        DeleteObject(clip);
    }
    DeleteObject(pill);
}

static void DrawIconBtn(HDC dc, const RECT *r, const wchar_t *glyph, int hot_id, BOOL lit)
{
    if (g_hot == hot_id || g_press == hot_id)
        FillRound(dc, r, S(6), hot_id == HOT_CLOSE ? CLR_HOT : CLR_TRACK);
    SelectObject(dc, g_icons);
    COLORREF c = CLR_DIM;
    if (hot_id == HOT_CLOSE && g_hot == HOT_CLOSE)
        c = CLR_LIT;
    else if (lit)
        c = CLR_LIT;
    SetTextColor(dc, c);
    DrawTextW(dc, glyph, 1, (RECT *)r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void Render(HDC dc, const RECT *rc)
{
    Layout l;
    GetLayout(&l);

    HBRUSH brush = CreateSolidBrush(CLR_BACK);
    FillRect(dc, rc, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(CLR_EDGE);
    FrameRect(dc, rc, brush);
    DeleteObject(brush);

    int done = 0, pos = 0, len = 0;
    wchar_t time[16];
    if (g_stream) {
        QWORD bytes = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
        if (bytes && bytes != (QWORD)-1) {
            QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
            if (at > bytes) at = bytes;
            done = (int)((QWORD)(l.seek.right - l.seek.left) * at / bytes);
            pos = Seconds(at);
            len = Seconds(bytes);
        }
    }
    TimeText(time, pos, len);

    DrawBar(dc, &l.seek, done, CLR_TRACK, CLR_FILL);
    DrawBar(dc, &l.vol, MulDiv(l.vol.right - l.vol.left, g_vol, 100), CLR_TRACK, CLR_FILL);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_font);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, l.seek.left, l.seek.top, l.seek.left + done, l.seek.bottom);
    DrawSeekLabels(dc, &l.seek, CLR_LIT, time);
    RestoreDC(dc, saved);
    saved = SaveDC(dc);
    IntersectClipRect(dc, l.seek.left + done, l.seek.top, l.seek.right, l.seek.bottom);
    DrawSeekLabels(dc, &l.seek, CLR_DIM, time);
    RestoreDC(dc, saved);

    if (g_kbps > 0) {
        wchar_t rate[24];
        rate[0] = 0;
        int v = g_kbps, n = 0;
        wchar_t tmp[8];
        if (v == 0)
            tmp[n++] = L'0';
        while (v && n < 7) {
            tmp[n++] = (wchar_t)(L'0' + v % 10);
            v /= 10;
        }
        int i = 0;
        while (n)
            rate[i++] = tmp[--n];
        rate[i] = 0;
        Append(rate, 24, L" kbps");
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, rate, -1, &l.head, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BOOL playing = g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING;
    DrawIconBtn(dc, &l.prev, L"\xE892", HOT_PREV, FALSE);
    DrawIconBtn(dc, &l.play, playing ? L"\xE769" : L"\xE768", HOT_PLAY, playing);
    DrawIconBtn(dc, &l.next, L"\xE893", HOT_NEXT, FALSE);
    DrawIconBtn(dc, &l.shuffle, L"\xE8B1", HOT_SHUFFLE, g_shuffle);
    DrawIconBtn(dc, &l.pin, g_topmost ? L"\xE840" : L"\xE718", HOT_PIN, g_topmost);
    DrawIconBtn(dc, &l.close, L"\xE8BB", HOT_CLOSE, FALSE);
    DrawIconBtn(dc, &l.speaker, g_vol > 0 ? L"\xE767" : L"\xE74F", HOT_MUTE, g_vol == 0);

    RECT split = { 0, l.search.top - S(6), rc->right, l.search.top - S(5) };
    brush = CreateSolidBrush(CLR_EDGE);
    FillRect(dc, &split, brush);
    DeleteObject(brush);

    FillRound(dc, &l.search, S(8), CLR_TRACK);
    SelectObject(dc, g_icons);
    SetTextColor(dc, g_filter[0] ? CLR_LIT : CLR_DIM);
    RECT mag = l.search;
    mag.right = mag.left + S(26);
    DrawTextW(dc, L"\xE721", 1, &mag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (g_filter[0])
        DrawIconBtn(dc, &l.clear, L"\xE711", HOT_CLEAR, FALSE);

    int row = S(ROW_H);
    int vis = VisibleRows(&l);
    ClampScroll();
    HRGN clip = CreateRectRgn(l.list.left, l.list.top, l.list.right, l.list.bottom);
    SelectClipRgn(dc, clip);
    DeleteObject(clip);

    if (g_count == 0) {
        SelectObject(dc, g_small);
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, L"Drop files or folders", -1, &l.list,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (g_view_n == 0) {
        SelectObject(dc, g_small);
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, L"No matches", -1, &l.list,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        SelectObject(dc, g_small);
        int last = g_scroll + vis;
        if (last > g_view_n)
            last = g_view_n;
        for (int i = g_scroll; i < last; ++i) {
            int t = g_view[i];
            RECT rr = { l.list.left, l.list.top + (i - g_scroll) * row, l.list.right, 0 };
            rr.bottom = rr.top + row;
            RECT inner = rr;
            inner.left += S(8);
            inner.right -= S(8);
            if (t == g_sel)
                FillRound(dc, &inner, S(6), CLR_ROW);
            if (t == g_cur) {
                RECT acc = { inner.left, inner.top + S(6), inner.left + S(3), inner.bottom - S(6) };
                brush = CreateSolidBrush(CLR_FILL);
                FillRect(dc, &acc, brush);
                DeleteObject(brush);
            }
            RECT tx = inner;
            tx.left += S(10);
            tx.right -= S(8);
            SIZE dz = { 0 };
            GetTextExtentPoint32W(dc, L"00:00", 5, &dz);
            RECT dr = tx;
            dr.left = dr.right - dz.cx;
            if (g_tracks[t].secs > 0) {
                wchar_t dur[8];
                *PutTime(dur, g_tracks[t].secs) = 0;
                SetTextColor(dc, CLR_DIM);
                DrawTextW(dc, dur, -1, &dr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            tx.right = dr.left - S(8);
            SetTextColor(dc, t == g_cur ? CLR_LIT : CLR_DIM);
            DrawTextW(dc, FileName(g_tracks[t].path), -1, &tx,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (g_view_n > vis && vis > 0) {
            int track_h = l.list.bottom - l.list.top - S(8);
            int thumb = vis * track_h / g_view_n;
            if (thumb < S(16)) thumb = S(16);
            int maxs = g_view_n - vis;
            int ty = l.list.top + S(4);
            if (maxs > 0)
                ty += g_scroll * (track_h - thumb) / maxs;
            RECT sb = { l.list.right - S(7), ty, l.list.right - S(4), ty + thumb };
            FillRound(dc, &sb, S(2), CLR_GRIP);
        }
    }
    SelectClipRgn(dc, 0);
}

static void Paint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_wnd, &ps);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    Render(mem, &rc);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(g_wnd, &ps);
}

static BOOL IsSizeHit(int hit)
{
    return hit == HOT_LEFT || hit == HOT_RIGHT || hit == HOT_TOP || hit == HOT_BOTTOM ||
           hit == HOT_TOPLEFT || hit == HOT_TOPRIGHT || hit == HOT_BOTTOMLEFT ||
           hit == HOT_BOTTOMRIGHT;
}

static int HitTest(POINT pt)
{
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int edge = S(EDGE);
    int on_l = pt.x < edge;
    int on_r = pt.x >= rc.right - edge;
    int on_t = pt.y < edge;
    int on_b = pt.y >= rc.bottom - edge;
    if (on_t && on_l) return HOT_TOPLEFT;
    if (on_t && on_r) return HOT_TOPRIGHT;
    if (on_b && on_l) return HOT_BOTTOMLEFT;
    if (on_b && on_r) return HOT_BOTTOMRIGHT;
    if (on_l) return HOT_LEFT;
    if (on_r) return HOT_RIGHT;
    if (on_t) return HOT_TOP;
    if (on_b) return HOT_BOTTOM;

    Layout l;
    GetLayout(&l);
    if (PtInRect(&l.close, pt)) return HOT_CLOSE;
    if (PtInRect(&l.pin, pt)) return HOT_PIN;
    if (PtInRect(&l.prev, pt)) return HOT_PREV;
    if (PtInRect(&l.play, pt)) return HOT_PLAY;
    if (PtInRect(&l.next, pt)) return HOT_NEXT;
    if (PtInRect(&l.shuffle, pt)) return HOT_SHUFFLE;
    if (PtInRect(&l.speaker, pt)) return HOT_MUTE;
    if (PtInRect(&l.vol, pt)) return HOT_VOL;
    if (PtInRect(&l.seek, pt)) return HOT_SEEK;
    if (g_filter[0] && PtInRect(&l.clear, pt)) return HOT_CLEAR;
    if (PtInRect(&l.search, pt)) return HOT_SEARCH;
    if (PtInRect(&l.list, pt)) return HOT_LIST;
    return 0;
}

static int RowAt(POINT pt)
{
    Layout l;
    GetLayout(&l);
    if (!PtInRect(&l.list, pt) || g_view_n == 0)
        return -1;
    int row = S(ROW_H);
    int i = g_scroll + (pt.y - l.list.top) / row;
    if (i < 0 || i >= g_view_n)
        return -1;
    if (i >= g_scroll + VisibleRows(&l))
        return -1;
    return g_view[i];
}

static void ResizeTo(int edge, POINT screen)
{
    RECT wr;
    GetWindowRect(g_wnd, &wr);
    int x = wr.left, y = wr.top;
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    int min_w = S(WIN_W_MIN), max_w = S(WIN_W_MAX);
    int min_h = S(WIN_H_MIN), max_h = S(WIN_H_MAX);
    BOOL left = edge == HOT_LEFT || edge == HOT_TOPLEFT || edge == HOT_BOTTOMLEFT;
    BOOL right = edge == HOT_RIGHT || edge == HOT_TOPRIGHT || edge == HOT_BOTTOMRIGHT;
    BOOL top = edge == HOT_TOP || edge == HOT_TOPLEFT || edge == HOT_TOPRIGHT;
    BOOL bottom = edge == HOT_BOTTOM || edge == HOT_BOTTOMLEFT || edge == HOT_BOTTOMRIGHT;

    if (right)
        w = Clamp(screen.x - wr.left, min_w, max_w);
    else if (left) {
        w = Clamp(wr.right - screen.x, min_w, max_w);
        x = wr.right - w;
    }
    if (bottom)
        h = Clamp(screen.y - wr.top, min_h, max_h);
    else if (top) {
        h = Clamp(wr.bottom - screen.y, min_h, max_h);
        y = wr.bottom - h;
    }

    RECT r = { x, y, x + w, y + h };
    RECT wa;
    if (WorkArea(&r, &wa)) {
        int snap = S(SNAP);
        if (right && Abs(r.right - wa.right) <= snap)
            w = Clamp(wa.right - x, min_w, max_w);
        else if (left && Abs(r.left - wa.left) <= snap) {
            w = Clamp(wr.right - wa.left, min_w, max_w);
            x = wr.right - w;
        }
        if (bottom && Abs(r.bottom - wa.bottom) <= snap)
            h = Clamp(wa.bottom - y, min_h, max_h);
        else if (top && Abs(r.top - wa.top) <= snap) {
            h = Clamp(wr.bottom - wa.top, min_h, max_h);
            y = wr.bottom - h;
        }
    }

    SetWindowPos(g_wnd, 0, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    ApplyShape();
    PlaceSearch();
    ClampScroll();
    RedrawWindow(g_wnd, 0, 0, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void TrackLeave()
{
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, g_wnd, 0 };
    TrackMouseEvent(&tme);
}

static void ShowAbout()
{
    MessageBoxW(g_wnd,
                APP_NAME L"\nVersion " APP_VERSION L"\n\n" APP_DESC L"\n\n" APP_COMPANY,
                APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static void ShowMenu(int x, int y)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              x, y, 0, g_wnd, 0);
    DestroyMenu(menu);
    if (cmd == IDM_ABOUT)
        ShowAbout();
}

static void Quit()
{
    g_exit = TRUE;
    SaveGeom();
    SavePlaylist();
    DestroyWindow(g_wnd);
}

static void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, IDM_SHOW, L"Show");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Exit");
    SetForegroundWindow(g_wnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, g_wnd, 0);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == IDM_SHOW)
        RestoreWnd(g_wnd);
    else if (cmd == IDM_ABOUT)
        ShowAbout();
    else if (cmd == IDM_QUIT)
        Quit();
}

static void OnTimer()
{
    if (g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_STOPPED) {
        if (!PlayNext(FALSE)) {
            BASS_StreamFree(g_stream);
            g_stream = 0;
            g_kbps = 0;
        }
    }
    ProbePending();
    InvalidateRect(g_wnd, 0, FALSE);
}

struct DropFiles {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
};

static void OnDrop(HGLOBAL mem)
{
    const BYTE *data = (const BYTE *)GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        return;
    }
    const DropFiles *df = (const DropFiles *)data;
    SIZE_T n = GlobalSize(mem);
    int added = 0;
    int first = g_count;
    if (df->pFiles < n) {
        if (df->fWide) {
            const wchar_t *p = (const wchar_t *)(data + df->pFiles);
            while (*p) {
                added += AddPath(p);
                while (*p) ++p;
                ++p;
            }
        } else {
            const char *p = (const char *)(data + df->pFiles);
            while (*p) {
                wchar_t path[MAX_PATH];
                MultiByteToWideChar(CP_ACP, 0, p, -1, path, MAX_PATH);
                path[MAX_PATH - 1] = 0;
                added += AddPath(path);
                while (*p) ++p;
                ++p;
            }
        }
    }
    GlobalUnlock(mem);
    GlobalFree(mem);
    if (added > 0) {
        ClampScroll();
        SavePlaylist();
        if (!g_stream)
            PlayIndex(first, TRUE);
        InvalidateRect(g_wnd, 0, FALSE);
    }
}

static void Enqueue(const wchar_t *path)
{
    int first = g_count;
    if (AddPath(path) <= 0)
        return;
    SavePlaylist();
    if (!g_stream)
        PlayIndex(first, TRUE);
    else
        InvalidateRect(g_wnd, 0, FALSE);
}

static const wchar_t *SkipArg(const wchar_t *p)
{
    if (*p == L'"') {
        for (++p; *p && *p != L'"'; ++p) {}
        if (*p) ++p;
    } else {
        while (*p && *p != L' ') ++p;
    }
    while (*p == L' ') ++p;
    return p;
}

static int ReadArg(const wchar_t **pp, wchar_t *out, int cap)
{
    const wchar_t *p = *pp;
    if (!*p)
        return 0;
    int quoted = 0;
    if (*p == L'"') {
        quoted = 1;
        ++p;
    }
    int i = 0;
    while (*p && i < cap - 1) {
        if (quoted) {
            if (*p == L'"') {
                ++p;
                break;
            }
        } else if (*p == L' ')
            break;
        out[i++] = *p++;
    }
    out[i] = 0;
    while (*p == L' ') ++p;
    *pp = p;
    return 1;
}

static int ParseDec(const wchar_t *s)
{
    int v = 0;
    if (!s || !*s)
        return -1;
    for (; *s; ++s) {
        if (*s < L'0' || *s > L'9')
            return -1;
        v = v * 10 + (*s - L'0');
        if (v > 65535)
            return -1;
    }
    return v;
}

static void EatFlags(const wchar_t **pp)
{
    for (;;) {
        const wchar_t *save = *pp;
        wchar_t a[MAX_PATH];
        if (!ReadArg(pp, a, MAX_PATH))
            return;
        if (EqI(a, L"--port")) {
            if (!ReadArg(pp, a, MAX_PATH))
                return;
            int v = ParseDec(a);
            if (v >= 1) {
                g_http_port = v;
                g_solo = TRUE;
            }
            continue;
        }
        *pp = save;
        return;
    }
}

static LRESULT CALLBACK SearchProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            if (GetWindowTextLengthW(wnd) > 0)
                SetWindowTextW(wnd, L"");
            else
                SendMessageW(g_wnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT ||
            wp == VK_RETURN || wp == VK_DELETE) {
            SendMessageW(g_wnd, msg, wp, lp);
            return 0;
        }
        if ((wp == VK_LEFT || wp == VK_RIGHT) && GetWindowTextLengthW(wnd) == 0) {
            SendMessageW(g_wnd, msg, wp, lp);
            return 0;
        }
    }
    if (msg == WM_CHAR && wp == VK_ESCAPE)
        return 0;
    if (msg == WM_MOUSEWHEEL) {
        SendMessageW(g_wnd, msg, wp, lp);
        return 0;
    }
    return CallWindowProcW(g_search_proc, wnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbar_msg && msg == g_taskbar_msg) {
        TrayAdd();
        return 0;
    }
    switch (msg) {
    case WM_ENTERSIZEMOVE: {
        RECT r;
        POINT pt;
        GetWindowRect(wnd, &r);
        GetCursorPos(&pt);
        g_grab.x = pt.x - r.left;
        g_grab.y = pt.y - r.top;
        return 0;
    }
    case WM_MOVING: {
        POINT pt;
        GetCursorPos(&pt);
        RECT *r = (RECT *)lp;
        int w = r->right - r->left;
        int h = r->bottom - r->top;
        r->left = pt.x - g_grab.x;
        r->top = pt.y - g_grab.y;
        r->right = r->left + w;
        r->bottom = r->top + h;
        SnapMove(r);
        return TRUE;
    }
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        OnTimer();
        return 0;
    case WM_HTTP:
        if (lp)
            HttpOp((HttpJob *)lp);
        return 0;
    case WM_CONTEXTMENU: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (pt.x == -1 && pt.y == -1) {
            RECT r;
            GetWindowRect(wnd, &r);
            pt.x = r.left + (r.right - r.left) / 2;
            pt.y = r.top + (r.bottom - r.top) / 2;
        }
        ShowMenu(pt.x, pt.y);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        if (WindowFromPoint(pt) != wnd) {
            SetCursor(LoadCursorW(0, IDC_ARROW));
            return TRUE;
        }
        ScreenToClient(wnd, &pt);
        RECT rc;
        GetClientRect(wnd, &rc);
        if (!PtInRect(&rc, pt)) {
            SetCursor(LoadCursorW(0, IDC_ARROW));
            return TRUE;
        }
        int hit = HitTest(pt);
        LPCWSTR id = IDC_ARROW;
        if (hit == HOT_SEEK || hit == HOT_VOL) id = IDC_HAND;
        else if (hit == HOT_SEARCH) id = IDC_IBEAM;
        else if (hit == HOT_LEFT || hit == HOT_RIGHT) id = IDC_SIZEWE;
        else if (hit == HOT_TOP || hit == HOT_BOTTOM) id = IDC_SIZENS;
        else if (hit == HOT_TOPLEFT || hit == HOT_BOTTOMRIGHT) id = IDC_SIZENWSE;
        else if (hit == HOT_TOPRIGHT || hit == HOT_BOTTOMLEFT) id = IDC_SIZENESW;
        else if (hit == 0) id = IDC_SIZEALL;
        SetCursor(LoadCursorW(0, id));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = HitTest(pt);
        if (hit == HOT_SEARCH) {
            SetFocus(g_search);
            return 0;
        }
        SetFocus(wnd);
        g_press = hit;
        if (hit == HOT_CLOSE || hit == HOT_PREV || hit == HOT_PLAY || hit == HOT_NEXT ||
            hit == HOT_PIN || hit == HOT_MUTE || hit == HOT_CLEAR || hit == HOT_SHUFFLE) {
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (IsSizeHit(hit)) {
            g_dragging = hit;
            SetCapture(wnd);
            return 0;
        }
        if (hit == HOT_SEEK) {
            g_dragging = HOT_SEEK;
            SetCapture(wnd);
            SeekAt(pt.x);
            return 0;
        }
        if (hit == HOT_VOL) {
            g_dragging = HOT_VOL;
            SetCapture(wnd);
            VolAt(pt.x);
            return 0;
        }
        if (hit == HOT_LIST) {
            int row = RowAt(pt);
            if (row >= 0) {
                g_sel = row;
                InvalidateRect(wnd, 0, FALSE);
            }
            return 0;
        }
        ReleaseCapture();
        SendMessageW(wnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int row = RowAt(pt);
        if (row >= 0)
            PlayIndex(row, TRUE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_dragging == HOT_SEEK) {
            SeekAt((short)LOWORD(lp));
            return 0;
        }
        if (g_dragging == HOT_VOL) {
            VolAt((short)LOWORD(lp));
            return 0;
        }
        if (IsSizeHit(g_dragging)) {
            POINT pt;
            GetCursorPos(&pt);
            ResizeTo(g_dragging, pt);
            return 0;
        }
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = HitTest(pt);
        int hot = 0;
        if (hit == HOT_CLOSE || hit == HOT_PREV || hit == HOT_PLAY || hit == HOT_NEXT ||
            hit == HOT_PIN || hit == HOT_MUTE || hit == HOT_CLEAR || hit == HOT_SHUFFLE)
            hot = hit;
        if (hot != g_hot) {
            g_hot = hot;
            InvalidateRect(wnd, 0, FALSE);
        }
        TrackLeave();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hot || g_press) {
            g_hot = 0;
            g_press = 0;
            InvalidateRect(wnd, 0, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = HitTest(pt);
        if (g_dragging) {
            if (IsSizeHit(g_dragging))
                SaveGeom();
            if (g_dragging == HOT_VOL)
                SaveGeom();
            g_dragging = 0;
            ReleaseCapture();
        } else if (g_press && g_press == hit) {
            if (hit == HOT_CLOSE)
                SendMessageW(wnd, WM_CLOSE, 0, 0);
            else if (hit == HOT_PLAY)
                TogglePause();
            else if (hit == HOT_NEXT)
                PlayNext(TRUE);
            else if (hit == HOT_PREV)
                PlayPrev();
            else if (hit == HOT_PIN)
                ToggleTopmost();
            else if (hit == HOT_MUTE)
                ToggleMute();
            else if (hit == HOT_SHUFFLE)
                ToggleShuffle();
            else if (hit == HOT_CLEAR)
                SetWindowTextW(g_search, L"");
        }
        g_press = 0;
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(wnd, &pt);
        int hit = HitTest(pt);
        int delta = (short)HIWORD(wp);
        if (hit == HOT_VOL || hit == HOT_MUTE) {
            g_vol = Clamp(g_vol + (delta > 0 ? 5 : -5), 0, 100);
            if (g_vol > 0)
                g_vol_pre = g_vol;
            ApplyVol();
            SaveGeom();
        } else {
            g_scroll += delta > 0 ? -3 : 3;
            ClampScroll();
        }
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SEARCH && HIWORD(wp) == EN_CHANGE) {
            FilterChanged();
            return 0;
        }
        return 0;
    case WM_CTLCOLOREDIT:
        if ((HWND)lp == g_search) {
            SetTextColor((HDC)wp, CLR_LIT);
            SetBkColor((HDC)wp, CLR_TRACK);
            return (LRESULT)g_search_br;
        }
        break;
    case WM_CHAR:
        if (wp >= 32 && GetFocus() != g_search) {
            SetFocus(g_search);
            SendMessageW(g_search, WM_CHAR, wp, lp);
            return 0;
        }
        if (wp == VK_BACK && GetFocus() != g_search && g_filter[0]) {
            SetFocus(g_search);
            SendMessageW(g_search, WM_CHAR, wp, lp);
            return 0;
        }
        return 0;
    case WM_KEYDOWN:
        if ((wp == 'V' || wp == 'v') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SetFocus(g_search);
            SendMessageW(g_search, WM_PASTE, 0, 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (g_filter[0]) {
                SetWindowTextW(g_search, L"");
                return 0;
            }
            SendMessageW(wnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_SPACE && GetFocus() != g_search) {
            TogglePause();
            return 0;
        }
        if (wp == VK_RETURN && g_sel >= 0) {
            PlayIndex(g_sel, TRUE);
            return 0;
        }
        if (wp == VK_DELETE) {
            RemoveSel();
            return 0;
        }
        if (wp == VK_UP) {
            MoveSelBy(-1);
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (wp == VK_DOWN) {
            MoveSelBy(1);
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (wp == VK_PRIOR) {
            Layout lay;
            GetLayout(&lay);
            MoveSelBy(-VisibleRows(&lay));
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (wp == VK_NEXT) {
            Layout lay;
            GetLayout(&lay);
            MoveSelBy(VisibleRows(&lay));
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (wp == VK_LEFT && g_stream) {
            QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
            double sec = BASS_ChannelBytes2Seconds(g_stream, at) - 5;
            if (sec < 0) sec = 0;
            BASS_ChannelSetPosition(g_stream, BASS_ChannelSeconds2Bytes(g_stream, sec), BASS_POS_BYTE);
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (wp == VK_RIGHT && g_stream) {
            QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
            double sec = BASS_ChannelBytes2Seconds(g_stream, at) + 5;
            BASS_ChannelSetPosition(g_stream, BASS_ChannelSeconds2Bytes(g_stream, sec), BASS_POS_BYTE);
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        SaveGeom();
        SavePlaylist();
        if (!g_exit) {
            ShowWindow(wnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        HttpShutdown();
        TrayDel();
        if (g_search_br)
            DeleteObject(g_search_br);
        PostQuitMessage(0);
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONUP || lp == NIN_SELECT)
            RestoreWnd(wnd);
        else if (lp == WM_RBUTTONUP)
            ShowTrayMenu();
        return 0;
    case WM_COPYDATA: {
        const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
        if (cd && cd->lpData && cd->cbData >= sizeof(wchar_t)) {
            ((wchar_t *)cd->lpData)[cd->cbData / sizeof(wchar_t) - 1] = 0;
            Enqueue((const wchar_t *)cd->lpData);
        }
        RestoreWnd(wnd);
        return TRUE;
    }
    case WM_DROPFILES:
        OnDrop((HGLOBAL)wp);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HFONT MakeFont(const wchar_t *face, int px)
{
    return CreateFontW(-S(px), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

static int Run()
{
    const wchar_t *cmd = SkipArg(GetCommandLineW());
    EatFlags(&cmd);
    HWND prev = g_solo ? 0 : FindWindowW(WND_CLASS, 0);
    if (prev) {
        wchar_t path[MAX_PATH];
        while (ReadArg(&cmd, path, MAX_PATH)) {
            int n = Len(path);
            COPYDATASTRUCT cd = { 0, (DWORD)((n + 1) * sizeof(wchar_t)), path };
            SendMessageW(prev, WM_COPYDATA, 0, (LPARAM)&cd);
        }
        RestoreWnd(prev);
        return 0;
    }

    SetProcessDPIAware();
    HDC screen = GetDC(0);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(0, screen);

    g_font = MakeFont(L"Segoe UI", 13);
    g_small = MakeFont(L"Segoe UI", 12);
    g_icons = MakeFont(L"Segoe MDL2 Assets", 10);
    Copy(g_title, APP_NAME, MAX_PATH);

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DROPSHADOW | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    g_taskbar_msg = RegisterWindowMessageW(L"TaskbarCreated");
    g_tray_icon = wc.hIconSm;

    int x, y;
    LoadSettings(&x, &y);
    g_wnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_ACCEPTFILES, WND_CLASS, g_title,
                            WS_POPUP, x, y, S(g_width), S(g_height),
                            0, 0, wc.hInstance, 0);
    if (!g_wnd)
        return 1;

    g_search_br = CreateSolidBrush(CLR_TRACK);
    g_search = CreateWindowExW(0, L"Edit", L"",
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                               0, 0, 0, 0, g_wnd, (HMENU)(INT_PTR)IDC_SEARCH, inst, 0);
    if (g_search) {
        SendMessageW(g_search, WM_SETFONT, (WPARAM)g_small, TRUE);
        SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search");
        g_search_proc = (WNDPROC)SetWindowLongPtrW(g_search, GWLP_WNDPROC, (LONG_PTR)SearchProc);
        PlaceSearch();
    }

    TrayAdd();

    ApplyShape();
    PlaceSearch();
    ApplyTopmost();
    ApplyVol();

    if (!BASS_Init(-1, 44100, 0, g_wnd, 0)) {
        TrayDel();
        return 1;
    }

    LoadPlaylist();
    RebuildView(FALSE);
    HttpStart();
    int first = g_count;
    wchar_t path[MAX_PATH];
    while (ReadArg(&cmd, path, MAX_PATH))
        AddPath(path);
    if (g_count > first) {
        SavePlaylist();
        PlayIndex(first, TRUE);
    }

    SetTimer(g_wnd, TIMER_ID, 16, 0);
    ShowWindow(g_wnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_stream)
        BASS_StreamFree(g_stream);
    BASS_Free();
    if (g_tracks)
        HeapFree(Heap(), 0, g_tracks);
    if (g_view)
        HeapFree(Heap(), 0, g_view);
    return 0;
}

extern "C" void __stdcall AppEntry()
{
    ExitProcess((UINT)Run());
}
