#include "ui.h"
#include "util.h"
#include "player.h"
#include "http.h"
#include "store.h"
#include "tags.h"
#include "media.h"

static HWND g_tip;
static HWND g_tagwnd;
static TagItem g_tag_items[TAG_DUMP_MAX];
static int g_tag_n;
static int g_tag_scroll;
static int g_tag_hot;
static int g_tag_press;
static int g_tag_hover;
static wchar_t g_tag_name[META_CAP * 2];

static void TipUpdate()
{
    if (!g_tip || !g_wnd)
        return;
    Layout l;
    GetLayout(&l);
    TOOLINFOW ti = { sizeof(ti) };
    ti.hwnd = g_wnd;
    ti.uId = HOT_CLOSE;
    ti.rect = l.close;
    SendMessageW(g_tip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
}

static void TipRelay(UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_tip)
        return;
    MSG m;
    m.hwnd = g_wnd;
    m.message = msg;
    m.wParam = wp;
    m.lParam = lp;
    m.time = GetMessageTime();
    GetCursorPos(&m.pt);
    SendMessageW(g_tip, TTM_RELAYEVENT, 0, (LPARAM)&m);
}

static void TipInit()
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, 0,
                            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                            0, 0, 0, 0, g_wnd, 0, GetModuleHandleW(0), 0);
    if (!g_tip)
        return;
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 400);
    SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 600);
    SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_RESHOW, 400);
    TOOLINFOW ti = { sizeof(ti) };
    ti.uFlags = TTF_TRANSPARENT;
    ti.hwnd = g_wnd;
    ti.uId = HOT_CLOSE;
    ti.lpszText = (LPWSTR)L"Hide to tray\nCtrl+click to exit";
    SendMessageW(g_tip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    TipUpdate();
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
void ApplyShape()
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

void LoadSettings(int *x, int *y)
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
        type == REG_DWORD && v >= WIN_H_MIN)
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

void SaveGeom()
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
    DWORD h = (DWORD)Clamp(MulDiv(r.bottom - r.top, 96, g_dpi), WIN_H_MIN, 10000);
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
void ApplyTopmost()
{
    SetWindowPos(g_wnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ToggleTopmost()
{
    g_topmost = !g_topmost;
    ApplyTopmost();
    SaveGeom();
    InvalidateRect(g_wnd, 0, FALSE);
}

void ToggleShuffle()
{
    g_shuffle = !g_shuffle;
    SaveGeom();
    InvalidateRect(g_wnd, 0, FALSE);
}
void ToggleMute()
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

void TrayAdd()
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

void TrayDel()
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_wnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayTip()
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

void RestoreWnd(HWND wnd)
{
    ShowWindow(wnd, SW_SHOW);
    SetWindowPos(wnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(wnd);
    ArmTimer();
}

static BOOL NeedProbe()
{
    for (int i = 0; i < g_count; ++i) {
        if (g_tracks[i].secs < 0)
            return TRUE;
    }
    return FALSE;
}

void ArmTimer()
{
    if (!g_wnd)
        return;
    BOOL vis = IsWindowVisible(g_wnd);
    BOOL play = g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING;
    if (vis && (play || NeedProbe()))
        SetTimer(g_wnd, TIMER_ID, play ? 100 : 250, 0);
    else
        KillTimer(g_wnd, TIMER_ID);
}

static void AdvanceIfEnded()
{
    if (!g_stream)
        return;
    if (BASS_ChannelIsActive(g_stream) != BASS_ACTIVE_STOPPED)
        return;
    if (!PlayNext(FALSE)) {
        BASS_StreamFree(g_stream);
        g_stream = 0;
        g_kbps = 0;
        g_bytes = 0;
        Copy(g_title, APP_NAME, MAX_PATH);
        if (g_wnd) {
            SetWindowTextW(g_wnd, g_title);
            TrayTip();
            InvalidateRect(g_wnd, 0, FALSE);
        }
    }
}
void GetLayout(Layout *l)
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

    int cy = l->close.bottom + pad;
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

    l->like.left = l->shuffle.right + gap;
    l->like.right = l->like.left + btn;
    l->like.top = cy;
    l->like.bottom = cy + btn;

    l->vol.right = rc.right - pad;
    l->vol.left = l->vol.right - S(VOL_W);
    l->vol.top = cy + (btn - bar) / 2;
    l->vol.bottom = l->vol.top + bar;

    l->speaker.right = l->vol.left - S(4);
    l->speaker.left = l->speaker.right - btn;
    l->speaker.top = cy;
    l->speaker.bottom = cy + btn;

    l->search.left = pad;
    l->search.top = cy + btn + pad;
    l->search.bottom = l->search.top + S(SEARCH_H);

    int sh = l->search.bottom - l->search.top;
    l->played.right = rc.right - pad;
    l->played.left = l->played.right - sh;
    l->played.top = l->search.top;
    l->played.bottom = l->search.bottom;
    l->fav.right = l->played.left - gap;
    l->fav.left = l->fav.right - sh;
    l->fav.top = l->search.top;
    l->fav.bottom = l->search.bottom;
    l->search.right = l->fav.left - gap;

    int clr = sh;
    l->clear.right = l->search.right - S(2);
    l->clear.left = l->clear.right - clr;
    l->clear.top = l->search.top;
    l->clear.bottom = l->search.bottom;

    l->list.left = 0;
    l->list.right = rc.right;
    l->list.top = l->search.bottom + gap;
    l->list.bottom = rc.bottom - pad;
}

int VisibleRows(const Layout *l)
{
    int h = l->list.bottom - l->list.top;
    int row = S(ROW_H);
    if (row <= 0)
        return 0;
    int n = h / row;
    return n < 0 ? 0 : n;
}
void PlaceSearch()
{
    TipUpdate();
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
static int LikeTarget()
{
    if (g_cur >= 0 && g_cur < g_count)
        return g_cur;
    if (g_sel >= 0 && g_sel < g_count)
        return g_sel;
    return -1;
}

static BOOL ViewRowInner(int view_i, RECT *inner)
{
    Layout l;
    GetLayout(&l);
    if (view_i < g_scroll || view_i >= g_view_n)
        return FALSE;
    if (view_i >= g_scroll + VisibleRows(&l))
        return FALSE;
    int row = S(ROW_H);
    inner->left = l.list.left + S(8);
    inner->right = l.list.right - S(8);
    inner->top = l.list.top + (view_i - g_scroll) * row;
    inner->bottom = inner->top + row;
    return TRUE;
}

static BOOL PlayingRowInner(RECT *inner)
{
    if (g_cur < 0 || !g_stream)
        return FALSE;
    int p = ViewIndex(g_cur);
    if (p < 0)
        return FALSE;
    return ViewRowInner(p, inner);
}

static void SeekAt(int x)
{
    RECT bar;
    if (!PlayingRowInner(&bar))
        return;
    int w = bar.right - bar.left;
    if (w <= 0)
        return;
    x -= bar.left;
    x = Clamp(x, 0, w);
    QWORD len = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
    if (!len || len == (QWORD)-1)
        return;
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

static void FillRound(HDC dc, const RECT *r, int rad, COLORREF c)
{
    HRGN rg = CreateRoundRectRgn(r->left, r->top, r->right + 1, r->bottom + 1, rad, rad);
    HBRUSH brush = CreateSolidBrush(c);
    FillRgn(dc, rg, brush);
    DeleteObject(brush);
    DeleteObject(rg);
}

static int StreamFill(int width)
{
    if (!g_stream || width <= 0)
        return 0;
    QWORD bytes = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
    if (!bytes || bytes == (QWORD)-1)
        return 0;
    QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
    if (at > bytes)
        at = bytes;
    return (int)((QWORD)width * at / bytes);
}

static int StreamSecs()
{
    if (!g_stream)
        return 0;
    QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
    return Seconds(at);
}

static void DrawBar(HDC dc, const RECT *bar, int fill, COLORREF back, COLORREF fg, int rad)
{
    FillRound(dc, bar, rad, back);
    if (fill > 0) {
        int saved = SaveDC(dc);
        IntersectClipRect(dc, bar->left, bar->top, bar->left + fill, bar->bottom);
        FillRound(dc, bar, rad, fg);
        RestoreDC(dc, saved);
    }
}

static BOOL CtrlDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static void InvalidateClose()
{
    if (!g_wnd)
        return;
    Layout l;
    GetLayout(&l);
    InvalidateRect(g_wnd, &l.close, FALSE);
}

static void DrawIconBtn(HDC dc, const RECT *r, const wchar_t *glyph, int hot_id, BOOL lit)
{
    BOOL close_hot = hot_id == HOT_CLOSE &&
                     (g_hot == HOT_CLOSE || g_press == HOT_CLOSE || CtrlDown());
    if (g_hot == hot_id || g_press == hot_id || close_hot)
        FillRound(dc, r, S(6), hot_id == HOT_CLOSE ? CLR_HOT : CLR_TRACK);
    SelectObject(dc, g_icons);
    COLORREF c = CLR_DIM;
    if (close_hot)
        c = CLR_LIT;
    else if (lit)
        c = CLR_LIT;
    SetTextColor(dc, c);
    DrawTextW(dc, glyph, 1, (RECT *)r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void DrawLikeBtn(HDC dc, const RECT *r)
{
    int t = LikeTarget();
    BOOL liked = t >= 0 && g_tracks[t].liked;
    if (g_hot == HOT_LIKE || g_press == HOT_LIKE)
        FillRound(dc, r, S(6), CLR_TRACK);
    SelectObject(dc, g_icons);
    COLORREF c = CLR_DIM;
    if (liked)
        c = CLR_HOT;
    else if (g_hot == HOT_LIKE)
        c = CLR_LIT;
    SetTextColor(dc, c);
    DrawTextW(dc, liked ? L"\xEB52" : L"\xEB51", 1, (RECT *)r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void DrawFavBtn(HDC dc, const RECT *r)
{
    if (g_hot == HOT_FAV || g_press == HOT_FAV)
        FillRound(dc, r, S(6), CLR_TRACK);
    SelectObject(dc, g_icons);
    COLORREF c = CLR_DIM;
    if (g_liked_only)
        c = CLR_HOT;
    else if (g_hot == HOT_FAV)
        c = CLR_LIT;
    SetTextColor(dc, c);
    DrawTextW(dc, g_liked_only ? L"\xEB52" : L"\xEB51", 1, (RECT *)r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void DrawRowTexts(HDC dc, const RECT *inner, const wchar_t *title, const wchar_t *dur,
                         int liked, COLORREF color)
{
    SetTextColor(dc, color);
    RECT tx = *inner;
    tx.left += S(8);
    tx.right -= S(8);
    int heart_w = S(16);
    if (dur && dur[0]) {
        SIZE dz = { 0 };
        GetTextExtentPoint32W(dc, L"00:00", 5, &dz);
        RECT dr = tx;
        dr.left = dr.right - dz.cx;
        DrawTextW(dc, dur, -1, &dr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        tx.right = dr.left;
    }
    RECT heart = tx;
    heart.left = heart.right - heart_w;
    if (liked) {
        SelectObject(dc, g_icons);
        DrawTextW(dc, L"\xEB51", 1, &heart,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, g_small);
    }
    tx.right = heart.left - S(4);
    DrawTextW(dc, title, -1, &tx,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void DrawTrackRow(HDC dc, int view_i, const Layout *l)
{
    int t = g_view[view_i];
    int row = S(ROW_H);
    RECT rr = { l->list.left, l->list.top + (view_i - g_scroll) * row, l->list.right, 0 };
    rr.bottom = rr.top + row;
    RECT inner = rr;
    inner.left += S(8);
    inner.right -= S(8);
    BOOL on = (t == g_cur && g_stream);
    BOOL sel = g_tracks[t].picked;
    int done = on ? StreamFill(inner.right - inner.left) : 0;
    if (on) {
        DrawBar(dc, &inner, done, sel ? CLR_ROW : CLR_TRACK, CLR_FILL, S(6));
    } else if (sel) {
        FillRound(dc, &inner, S(6), CLR_ROW);
    }
    if (t == g_cur && !on) {
        RECT acc = { inner.left, inner.top + S(6), inner.left + S(3), inner.bottom - S(6) };
        HBRUSH brush = CreateSolidBrush(CLR_FILL);
        FillRect(dc, &acc, brush);
        DeleteObject(brush);
    }
    wchar_t dur[8];
    dur[0] = 0;
    if (on)
        *PutTime(dur, StreamSecs()) = 0;
    else if (g_tracks[t].secs > 0)
        *PutTime(dur, g_tracks[t].secs) = 0;
    const wchar_t *title = TrackTitle(&g_tracks[t]);
    SelectObject(dc, g_small);
    if (on) {
        int saved = SaveDC(dc);
        IntersectClipRect(dc, inner.left, inner.top, inner.left + done, inner.bottom);
        DrawRowTexts(dc, &inner, title, dur, g_tracks[t].liked, CLR_LIT);
        RestoreDC(dc, saved);
        saved = SaveDC(dc);
        IntersectClipRect(dc, inner.left + done, inner.top, inner.right, inner.bottom);
        DrawRowTexts(dc, &inner, title, dur, g_tracks[t].liked, CLR_DIM);
        RestoreDC(dc, saved);
    } else {
        DrawRowTexts(dc, &inner, title, dur, g_tracks[t].liked,
                     t == g_cur ? CLR_LIT : CLR_DIM);
    }
}

static BOOL Hits(const RECT *a, const RECT *b)
{
    return a->left < b->right && b->left < a->right &&
           a->top < b->bottom && b->top < a->bottom;
}

static void Render(HDC dc, const RECT *rc, const RECT *dirty)
{
    Layout l;
    GetLayout(&l);

    HBRUSH brush = CreateSolidBrush(CLR_BACK);
    FillRect(dc, dirty, brush);
    DeleteObject(brush);
    if (dirty->left <= rc->left || dirty->top <= rc->top ||
        dirty->right >= rc->right || dirty->bottom >= rc->bottom) {
        brush = CreateSolidBrush(CLR_EDGE);
        FrameRect(dc, rc, brush);
        DeleteObject(brush);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_font);

    if (Hits(dirty, &l.vol))
        DrawBar(dc, &l.vol, MulDiv(l.vol.right - l.vol.left, g_vol, 100), CLR_TRACK, CLR_FILL, S(BAR_H));

    if ((g_kbps > 0 || g_bytes > 0) && Hits(dirty, &l.head)) {
        wchar_t rate[48];
        rate[0] = 0;
        if (g_kbps > 0) {
            wsprintfW(rate, L"%d kbps", g_kbps);
            if (g_bytes > 0)
                Append(rate, 48, L"  ");
        }
        if (g_bytes > 0) {
            unsigned tenths = (unsigned)((g_bytes * 10 + (1024ull * 1024 / 2)) / (1024ull * 1024));
            wchar_t mb[24];
            wsprintfW(mb, L"%u.%u MB", tenths / 10, tenths % 10);
            Append(rate, 48, mb);
        }
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, rate, -1, &l.head, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    BOOL playing = g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING;
    if (Hits(dirty, &l.prev))
        DrawIconBtn(dc, &l.prev, L"\xE892", HOT_PREV, FALSE);
    if (Hits(dirty, &l.play))
        DrawIconBtn(dc, &l.play, playing ? L"\xE769" : L"\xE768", HOT_PLAY, playing);
    if (Hits(dirty, &l.next))
        DrawIconBtn(dc, &l.next, L"\xE893", HOT_NEXT, FALSE);
    if (Hits(dirty, &l.shuffle))
        DrawIconBtn(dc, &l.shuffle, L"\xE8B1", HOT_SHUFFLE, g_shuffle);
    if (Hits(dirty, &l.like))
        DrawLikeBtn(dc, &l.like);
    if (Hits(dirty, &l.pin))
        DrawIconBtn(dc, &l.pin, g_topmost ? L"\xE840" : L"\xE718", HOT_PIN, g_topmost);
    if (Hits(dirty, &l.close))
        DrawIconBtn(dc, &l.close, L"\xE8BB", HOT_CLOSE, FALSE);
    if (Hits(dirty, &l.speaker))
        DrawIconBtn(dc, &l.speaker, g_vol > 0 ? L"\xE767" : L"\xE74F", HOT_MUTE, g_vol == 0);

    RECT split = { 0, l.search.top - S(6), rc->right, l.search.top - S(5) };
    if (Hits(dirty, &split)) {
        brush = CreateSolidBrush(CLR_EDGE);
        FillRect(dc, &split, brush);
        DeleteObject(brush);
    }

    if (Hits(dirty, &l.search)) {
        FillRound(dc, &l.search, S(8), CLR_TRACK);
        SelectObject(dc, g_icons);
        SetTextColor(dc, g_filter[0] ? CLR_LIT : CLR_DIM);
        RECT mag = l.search;
        mag.right = mag.left + S(26);
        DrawTextW(dc, L"\xE721", 1, &mag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (g_filter[0])
            DrawIconBtn(dc, &l.clear, L"\xE711", HOT_CLEAR, FALSE);
    }
    if (Hits(dirty, &l.fav))
        DrawFavBtn(dc, &l.fav);
    if (Hits(dirty, &l.played))
        DrawIconBtn(dc, &l.played, L"\xE768", HOT_PLAYED, g_played_only);

    if (!Hits(dirty, &l.list))
        return;

    int vis = VisibleRows(&l);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, l.list.left, l.list.top, l.list.right, l.list.bottom);

    if (g_view_n == 0) {
        SelectObject(dc, g_small);
        SetTextColor(dc, CLR_DIM);
        DrawTextW(dc, (g_filter[0] || g_liked_only || g_played_only) ? L"No matches" : L"Drop files or folders", -1, &l.list,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        SelectObject(dc, g_small);
        int last = g_scroll + vis;
        if (last > g_view_n)
            last = g_view_n;
        int row = S(ROW_H);
        for (int i = g_scroll; i < last; ++i) {
            RECT rr = { l.list.left, l.list.top + (i - g_scroll) * row, l.list.right, 0 };
            rr.bottom = rr.top + row;
            if (Hits(&rr, dirty))
                DrawTrackRow(dc, i, &l);
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
            if (Hits(&sb, dirty))
                FillRound(dc, &sb, S(2), CLR_GRIP);
        }
    }
    RestoreDC(dc, saved);
}

static void Paint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(g_wnd, &ps);
    RECT rc;
    GetClientRect(g_wnd, &rc);
    RECT d = ps.rcPaint;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int dw = d.right - d.left;
    int dh = d.bottom - d.top;
    if (w <= 0 || h <= 0 || dw <= 0 || dh <= 0) {
        EndPaint(g_wnd, &ps);
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    IntersectClipRect(mem, d.left, d.top, d.right, d.bottom);
    Render(mem, &rc, &d);
    BitBlt(dc, d.left, d.top, dw, dh, mem, d.left, d.top, SRCCOPY);
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
    if (PtInRect(&l.head, pt)) return HOT_HEAD;
    if (PtInRect(&l.prev, pt)) return HOT_PREV;
    if (PtInRect(&l.play, pt)) return HOT_PLAY;
    if (PtInRect(&l.next, pt)) return HOT_NEXT;
    if (PtInRect(&l.shuffle, pt)) return HOT_SHUFFLE;
    if (PtInRect(&l.like, pt)) return HOT_LIKE;
    if (PtInRect(&l.speaker, pt)) return HOT_MUTE;
    if (PtInRect(&l.vol, pt)) return HOT_VOL;
    if (PtInRect(&l.played, pt)) return HOT_PLAYED;
    if (PtInRect(&l.fav, pt)) return HOT_FAV;
    if (g_filter[0] && PtInRect(&l.clear, pt)) return HOT_CLEAR;
    if (PtInRect(&l.search, pt)) return HOT_SEARCH;
    if (PtInRect(&l.list, pt)) {
        RECT bar;
        if (PlayingRowInner(&bar) && PtInRect(&bar, pt))
            return HOT_SEEK;
        return HOT_LIST;
    }
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
    int min_h = S(WIN_H_MIN);
    int max_h = 10000;
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST), &mi))
        max_h = mi.rcMonitor.bottom - mi.rcMonitor.top;
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

static void OpenDataFolder()
{
    wchar_t dir[MAX_PATH];
    if (!StoreDataDir(dir, MAX_PATH))
        return;
    ShellExecuteW(g_wnd, L"open", dir, 0, 0, SW_SHOWNORMAL);
}

static void ShowAbout()
{
    MessageBoxW(g_wnd,
                APP_NAME L"\nVersion " APP_VERSION L"\n\n" APP_DESC L"\n\n" APP_COMPANY,
                APP_NAME, MB_OK | MB_ICONINFORMATION);
}

#define TAG_HOT_CLOSE 1
#define TAG_HOT_HEAD  2

static void CopyClip(const wchar_t *s)
{
    int n = Len(s) + 1;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, n * sizeof(wchar_t));
    if (!mem)
        return;
    wchar_t *p = (wchar_t *)GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return;
    }
    Copy(p, s, n);
    GlobalUnlock(mem);
    if (!OpenClipboard(g_wnd) || !EmptyClipboard()) {
        GlobalFree(mem);
        return;
    }
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
}

static void GetTagLayout(HWND wnd, RECT *close, RECT *head, RECT *list)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    int pad = S(PAD);
    int btn = S(BTN);
    close->right = rc.right - pad;
    close->left = close->right - btn;
    close->top = pad;
    close->bottom = close->top + btn;
    head->left = pad;
    head->right = close->left - S(6);
    head->top = close->top;
    head->bottom = close->bottom;
    list->left = pad;
    list->right = rc.right - pad;
    list->top = close->bottom + S(8);
    list->bottom = rc.bottom - pad;
}

static int TagVis(HWND wnd)
{
    RECT close, head, list;
    GetTagLayout(wnd, &close, &head, &list);
    int h = list.bottom - list.top;
    int row = S(ROW_H);
    if (h < row)
        return 0;
    return h / row;
}

static void ClampTagScroll(HWND wnd)
{
    int maxs = g_tag_n - TagVis(wnd);
    if (maxs < 0)
        maxs = 0;
    g_tag_scroll = Clamp(g_tag_scroll, 0, maxs);
}

static void ApplyTagShape(HWND wnd)
{
    RECT rc;
    GetClientRect(wnd, &rc);
    SetWindowRgn(wnd, CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, S(10), S(10)), FALSE);
}

static void CenterTagWnd(int w, int h)
{
    RECT wr, wa;
    GetWindowRect(g_wnd, &wr);
    if (!WorkArea(&wr, &wa))
        return;
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top - h) / 2;
    SetWindowPos(g_tagwnd, g_topmost ? HWND_TOPMOST : HWND_TOP, x, y, w, h,
                 SWP_SHOWWINDOW);
    ApplyTagShape(g_tagwnd);
}

static int TagHit(HWND wnd, POINT pt)
{
    RECT close, head, list;
    GetTagLayout(wnd, &close, &head, &list);
    if (PtInRect(&close, pt))
        return TAG_HOT_CLOSE;
    if (PtInRect(&head, pt))
        return TAG_HOT_HEAD;
    return 0;
}

static int TagRowAt(HWND wnd, POINT pt)
{
    RECT close, head, list;
    GetTagLayout(wnd, &close, &head, &list);
    if (!PtInRect(&list, pt) || g_tag_n == 0)
        return -1;
    int row = S(ROW_H);
    int i = g_tag_scroll + (pt.y - list.top) / row;
    if (i < 0 || i >= g_tag_n)
        return -1;
    if (i >= g_tag_scroll + TagVis(wnd))
        return -1;
    return i;
}

static void PaintTags(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    GetClientRect(wnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) {
        EndPaint(wnd, &ps);
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH brush = CreateSolidBrush(CLR_BACK);
    FillRect(mem, &rc, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(CLR_EDGE);
    FrameRect(mem, &rc, brush);
    DeleteObject(brush);

    RECT close, head, list;
    GetTagLayout(wnd, &close, &head, &list);
    SetBkMode(mem, TRANSPARENT);

    if (g_tag_hot == TAG_HOT_CLOSE || g_tag_press == TAG_HOT_CLOSE)
        FillRound(mem, &close, S(6), CLR_HOT);
    SelectObject(mem, g_icons);
    SetTextColor(mem, g_tag_hot == TAG_HOT_CLOSE ? CLR_LIT : CLR_DIM);
    DrawTextW(mem, L"\xE8BB", 1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(mem, g_font);
    SetTextColor(mem, CLR_LIT);
    DrawTextW(mem, g_tag_name, -1, &head,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT split = { 0, list.top - S(5), rc.right, list.top - S(4) };
    brush = CreateSolidBrush(CLR_EDGE);
    FillRect(mem, &split, brush);
    DeleteObject(brush);

    int vis = TagVis(wnd);
    int saved = SaveDC(mem);
    IntersectClipRect(mem, list.left, list.top, list.right, list.bottom);
    SelectObject(mem, g_small);
    int last = g_tag_scroll + vis;
    if (last > g_tag_n)
        last = g_tag_n;
    int row = S(ROW_H);
    int key_w = S(118);
    for (int i = g_tag_scroll; i < last; ++i) {
        RECT rr = { list.left, list.top + (i - g_tag_scroll) * row, list.right, 0 };
        rr.bottom = rr.top + row;
        RECT inner = rr;
        inner.left += S(4);
        inner.right -= S(4);
        if (i == g_tag_hover)
            FillRound(mem, &inner, S(6), CLR_ROW);
        RECT kr = inner;
        kr.right = kr.left + key_w;
        RECT vr = inner;
        vr.left = kr.right + S(8);
        SetTextColor(mem, CLR_DIM);
        DrawTextW(mem, g_tag_items[i].k, -1, &kr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(mem, CLR_LIT);
        DrawTextW(mem, g_tag_items[i].v, -1, &vr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (g_tag_n > vis && vis > 0) {
        int track_h = list.bottom - list.top - S(8);
        int thumb = vis * track_h / g_tag_n;
        if (thumb < S(16))
            thumb = S(16);
        int maxs = g_tag_n - vis;
        int ty = list.top + S(4);
        if (maxs > 0)
            ty += g_tag_scroll * (track_h - thumb) / maxs;
        RECT sb = { list.right - S(7), ty, list.right - S(4), ty + thumb };
        FillRound(mem, &sb, S(2), CLR_GRIP);
    }
    RestoreDC(mem, saved);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(wnd, &ps);
}

static LRESULT CALLBACK TagProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        PaintTags(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        ApplyTagShape(wnd);
        return 0;
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(wnd, &pt);
        SetCursor(LoadCursorW(0, TagHit(wnd, pt) == TAG_HOT_HEAD ? IDC_SIZEALL : IDC_ARROW));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
        TrackMouseEvent(&tme);
        int hit = TagHit(wnd, pt);
        int row = TagRowAt(wnd, pt);
        if (hit != g_tag_hot || row != g_tag_hover) {
            g_tag_hot = hit;
            g_tag_hover = row;
            InvalidateRect(wnd, 0, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tag_hot = 0;
        g_tag_press = 0;
        g_tag_hover = -1;
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        SetFocus(wnd);
        int hit = TagHit(wnd, pt);
        g_tag_press = hit;
        if (hit == TAG_HOT_CLOSE) {
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if (hit == TAG_HOT_HEAD) {
            ReleaseCapture();
            SendMessageW(wnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = TagHit(wnd, pt);
        if (g_tag_press == TAG_HOT_CLOSE && hit == TAG_HOT_CLOSE)
            DestroyWindow(wnd);
        g_tag_press = 0;
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int vis = TagVis(wnd);
        int maxs = g_tag_n - vis;
        if (maxs < 0)
            maxs = 0;
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3;
        g_tag_scroll = Clamp(g_tag_scroll + d, 0, maxs);
        InvalidateRect(wnd, 0, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        if (wnd == g_tagwnd)
            g_tagwnd = 0;
        g_tag_hot = 0;
        g_tag_press = 0;
        g_tag_hover = -1;
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static void ShowTags(int i)
{
    if (i < 0 || i >= g_count)
        return;
    Copy(g_tag_name, TrackTitle(&g_tracks[i]), META_CAP * 2);
    g_tag_n = DumpTags(g_tracks[i].path, g_tag_items, TAG_DUMP_MAX);
    g_tag_scroll = 0;
    g_tag_hover = -1;
    HINSTANCE inst = GetModuleHandleW(0);
    if (!g_tagwnd) {
        static BOOL reg;
        if (!reg) {
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.style = CS_DROPSHADOW | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = TagProc;
            wc.hInstance = inst;
            wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
            wc.hCursor = LoadCursorW(0, IDC_ARROW);
            wc.lpszClassName = WND_TAGS;
            RegisterClassExW(&wc);
            reg = TRUE;
        }
        g_tagwnd = CreateWindowExW(0, WND_TAGS, L"Tags",
                                   WS_POPUP | WS_CLIPCHILDREN,
                                   0, 0, S(480), S(400), g_wnd, 0, inst, 0);
        if (!g_tagwnd)
            return;
    }
    SetWindowTextW(g_tagwnd, g_tag_name);
    int vis = g_tag_n;
    if (vis < 10)
        vis = 10;
    if (vis > 18)
        vis = 18;
    int tw = S(520);
    int th = S(PAD) + S(BTN) + S(8) + vis * S(ROW_H) + S(PAD);
    CenterTagWnd(tw, th);
    ClampTagScroll(g_tagwnd);
    InvalidateRect(g_tagwnd, 0, FALSE);
    SetForegroundWindow(g_tagwnd);
}

static BOOL RunMenu(UINT cmd)
{
    if (cmd == IDM_ABOUT) {
        ShowAbout();
        return TRUE;
    }
    if (cmd == IDM_DATA) {
        OpenDataFolder();
        return TRUE;
    }
    if (cmd == IDM_SHOW) {
        RestoreWnd(g_wnd);
        return TRUE;
    }
    return FALSE;
}

static void ShowMenu(int x, int y)
{
    POINT screen = { x, y };
    POINT client = screen;
    ScreenToClient(g_wnd, &client);
    int track = RowAt(client);
    if (track >= 0) {
        if (!g_tracks[track].picked)
            PickOnly(track);
        g_sel = track;
        InvalidateRect(g_wnd, 0, FALSE);
    }
    if (track < 0 && g_sel >= 0 && g_sel < g_count)
        track = g_sel;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    UINT en = MF_STRING | (track < 0 ? MF_GRAYED : 0);
    AppendMenuW(menu, en, IDM_COPY_TITLE, L"Copy title");
    AppendMenuW(menu, en, IDM_TAGS, L"Tags");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, en, IDM_REMOVE, L"Remove");
    AppendMenuW(menu, en, IDM_DELETE_FILE, L"Delete file");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              x, y, 0, g_wnd, 0);
    DestroyMenu(menu);
    if (cmd == IDM_COPY_TITLE)
        CopyClip(TrackTitle(&g_tracks[track]));
    else if (cmd == IDM_TAGS)
        ShowTags(track);
    else if (cmd == IDM_REMOVE)
        RemovePicked(FALSE);
    else if (cmd == IDM_DELETE_FILE) {
        int n = PickedCount();
        if (n <= 0 && track >= 0)
            n = 1;
        wchar_t msg[MAX_PATH + 80];
        msg[0] = 0;
        if (n <= 1) {
            Append(msg, MAX_PATH + 80, L"Delete from disk?\n\n");
            Append(msg, MAX_PATH + 80, FileName(g_tracks[track].path));
        } else {
            Append(msg, MAX_PATH + 80, L"Delete ");
            wchar_t num[12];
            int v = n, k = 0;
            wchar_t tmp[12];
            if (v == 0)
                tmp[k++] = L'0';
            while (v && k < 11) {
                tmp[k++] = (wchar_t)(L'0' + v % 10);
                v /= 10;
            }
            int p = 0;
            while (k)
                num[p++] = tmp[--k];
            num[p] = 0;
            Append(msg, MAX_PATH + 80, num);
            Append(msg, MAX_PATH + 80, L" files from disk?");
        }
        if (MessageBoxW(g_wnd, msg, APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            RemovePicked(TRUE);
    }
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
    AppendMenuW(menu, MF_STRING, IDM_DATA, L"Open data folder");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Exit");
    SetForegroundWindow(g_wnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, g_wnd, 0);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == IDM_QUIT)
        Quit();
    else
        RunMenu(cmd);
}

static void OnTimer()
{
    AdvanceIfEnded();
    int probed = 0;
    if (IsWindowVisible(g_wnd))
        probed = ProbePending();
    BOOL play = g_stream && BASS_ChannelIsActive(g_stream) == BASS_ACTIVE_PLAYING;
    if (IsWindowVisible(g_wnd) && (play || probed)) {
        Layout l;
        GetLayout(&l);
        if (probed) {
            InvalidateRect(g_wnd, &l.list, FALSE);
        } else {
            RECT row;
            if (PlayingRowInner(&row)) {
                row.left = l.list.left;
                row.right = l.list.right;
                InvalidateRect(g_wnd, &row, FALSE);
            }
        }
    }
    ArmTimer();
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
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) &&
        (wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL))
        SendMessageW(g_wnd, msg, wp, lp);
    if (msg == WM_KEYDOWN) {
        if ((wp == 'A' || wp == 'a') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            PickAllView();
            InvalidateRect(g_wnd, 0, FALSE);
            return 0;
        }
        if ((wp == 'L' || wp == 'l') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            ToggleLike(LikeTarget());
            return 0;
        }
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
        if (wp == TRAY_CLICK_ID) {
            KillTimer(wnd, TRAY_CLICK_ID);
            RestoreWnd(wnd);
            return 0;
        }
        OnTimer();
        return 0;
    case WM_TRACK_END:
        AdvanceIfEnded();
        ArmTimer();
        return 0;
    case WM_SHOWWINDOW:
        ArmTimer();
        return DefWindowProcW(wnd, msg, wp, lp);
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
        else if (hit == HOT_HEAD) id = IDC_SIZEALL;
        SetCursor(LoadCursorW(0, id));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        TipRelay(msg, wp, lp);
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        int hit = HitTest(pt);
        if (hit == HOT_SEARCH) {
            SetFocus(g_search);
            return 0;
        }
        SetFocus(wnd);
        g_press = hit;
        if (hit == HOT_CLOSE || hit == HOT_PREV || hit == HOT_PLAY || hit == HOT_NEXT ||
            hit == HOT_PIN || hit == HOT_MUTE || hit == HOT_CLEAR || hit == HOT_SHUFFLE ||
            hit == HOT_LIKE || hit == HOT_FAV || hit == HOT_PLAYED) {
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
            if (g_cur >= 0)
                PickOnly(g_cur);
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
                if (wp & MK_SHIFT)
                    PickRange(row);
                else if (wp & MK_CONTROL)
                    PickToggle(row);
                else
                    PickOnly(row);
                InvalidateRect(wnd, 0, FALSE);
            }
            return 0;
        }
        if (hit == HOT_HEAD) {
            ReleaseCapture();
            SendMessageW(wnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (HitTest(pt) == HOT_SEEK)
            return 0;
        int row = RowAt(pt);
        if (row >= 0)
            PlayIndex(row, TRUE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TipRelay(msg, wp, lp);
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
            hit == HOT_PIN || hit == HOT_MUTE || hit == HOT_CLEAR || hit == HOT_SHUFFLE ||
            hit == HOT_LIKE || hit == HOT_FAV || hit == HOT_PLAYED)
            hot = hit;
        if (hot != g_hot) {
            g_hot = hot;
            InvalidateRect(wnd, 0, FALSE);
        }
        TrackLeave();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_tip)
            SendMessageW(g_tip, TTM_POP, 0, 0);
        if (g_hot || g_press) {
            g_hot = 0;
            g_press = 0;
            InvalidateRect(wnd, 0, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        TipRelay(msg, wp, lp);
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
            if (hit == HOT_CLOSE) {
                g_press = 0;
                if (GetKeyState(VK_CONTROL) & 0x8000)
                    Quit();
                else
                    SendMessageW(wnd, WM_CLOSE, 0, 0);
                return 0;
            }
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
            else if (hit == HOT_LIKE)
                ToggleLike(LikeTarget());
            else if (hit == HOT_FAV) {
                g_liked_only = !g_liked_only;
                FilterChanged();
            }
            else if (hit == HOT_PLAYED) {
                g_played_only = !g_played_only;
                FilterChanged();
            }
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
    case WM_ACTIVATE:
        InvalidateClose();
        break;
    case WM_KEYUP:
        if (wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL)
            InvalidateClose();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL) {
            if (!(lp & 0x40000000))
                InvalidateClose();
        }
        if ((wp == 'A' || wp == 'a') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            PickAllView();
            InvalidateRect(wnd, 0, FALSE);
            return 0;
        }
        if ((wp == 'V' || wp == 'v') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SetFocus(g_search);
            SendMessageW(g_search, WM_PASTE, 0, 0);
            return 0;
        }
        if ((wp == 'L' || wp == 'l') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            ToggleLike(LikeTarget());
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
            SeekBy(-5);
            return 0;
        }
        if (wp == VK_RIGHT && g_stream) {
            SeekBy(5);
            return 0;
        }
        return 0;
    case WM_APPCOMMAND:
        if (MediaCommand(GET_APPCOMMAND_LPARAM(lp)))
            return TRUE;
        break;
    case WM_MEDIA:
        MediaSmtcButton(wp);
        return 0;
    case WM_HOTKEY:
        if (wp == 1)
            MediaCommand(APPCOMMAND_MEDIA_PLAY_PAUSE);
        else if (wp == 2)
            MediaCommand(APPCOMMAND_MEDIA_STOP);
        else if (wp == 3)
            MediaCommand(APPCOMMAND_MEDIA_NEXTTRACK);
        else if (wp == 4)
            MediaCommand(APPCOMMAND_MEDIA_PREVIOUSTRACK);
        return 0;
    case WM_CLOSE:
        SaveGeom();
        SavePlaylist();
        if (!g_exit) {
            ShowWindow(wnd, SW_HIDE);
            ArmTimer();
            return 0;
        }
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        if (g_tagwnd)
            DestroyWindow(g_tagwnd);
        MediaKeysShutdown();
        HttpShutdown();
        StoreClose();
        TrayDel();
        if (g_search_br)
            DeleteObject(g_search_br);
        PostQuitMessage(0);
        return 0;
    case WM_TRAY: {
        static DWORD dbl_at;
        if (lp == WM_LBUTTONDBLCLK) {
            KillTimer(wnd, TRAY_CLICK_ID);
            dbl_at = GetTickCount();
            if (g_stream)
                TogglePause();
            return 0;
        }
        if (lp == WM_LBUTTONUP || lp == NIN_SELECT) {
            if (dbl_at && GetTickCount() - dbl_at < GetDoubleClickTime())
                return 0;
            SetTimer(wnd, TRAY_CLICK_ID, GetDoubleClickTime(), 0);
            return 0;
        }
        if (lp == WM_RBUTTONUP)
            ShowTrayMenu();
        return 0;
    }
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

int Run()
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
    g_icons = MakeFont(L"Segoe MDL2 Assets", 12);
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
                            WS_POPUP | WS_CLIPCHILDREN, x, y, S(g_width), S(g_height),
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
    TipInit();
    ApplyTopmost();
    ApplyVol();

    if (!BASS_Init(-1, 44100, 0, g_wnd, 0)) {
        TrayDel();
        return 1;
    }
    MediaKeysInit(g_wnd);

    StoreOpen();
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

    ShowWindow(g_wnd, SW_SHOW);
    ArmTimer();

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (g_tagwnd && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (msg.hwnd == g_tagwnd || IsChild(g_tagwnd, msg.hwnd))) {
            DestroyWindow(g_tagwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_stream)
        BASS_StreamFree(g_stream);
    BASS_Free();
    StoreClose();
    if (g_tracks)
        HeapFree(Heap(), 0, g_tracks);
    if (g_view)
        HeapFree(Heap(), 0, g_view);
    return 0;
}
