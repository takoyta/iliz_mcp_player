#include "player.h"
#include "util.h"
#include "ui.h"
#include "store.h"
#include "tags.h"
#include "media.h"
#include <string.h>

void ApplyVol()
{
    BASS_SetConfig(BASS_CONFIG_GVOL_STREAM, (DWORD)(g_vol * 100));
}
unsigned RandU()
{
    if (!g_rng)
        g_rng = GetTickCount() ^ (unsigned)(ULONG_PTR)g_wnd;
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}
int ViewIndex(int track)
{
    for (int i = 0; i < g_view_n; ++i) {
        if (g_view[i] == track)
            return i;
    }
    return -1;
}

void ClampScroll()
{
    Layout l;
    GetLayout(&l);
    int vis = VisibleRows(&l);
    int maxs = g_view_n - vis;
    if (maxs < 0) maxs = 0;
    g_scroll = Clamp(g_scroll, 0, maxs);
}

void EnsureVisible(int i)
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

static void CenterIfHidden(int i)
{
    Layout l;
    GetLayout(&l);
    int vis = VisibleRows(&l);
    if (vis <= 0)
        return;
    int p = ViewIndex(i);
    if (p < 0)
        return;
    if (p >= g_scroll && p < g_scroll + vis)
        return;
    g_scroll = p - vis / 2;
    ClampScroll();
}

void RebuildView(BOOL reset_scroll)
{
    if (!g_view)
        g_view = (int *)HeapAlloc(Heap(), 0, MAX_TRACKS * sizeof(int));
    if (!g_view) {
        g_view_n = 0;
        return;
    }
    g_view_n = 0;
    if (!g_filter[0]) {
        g_view_n = CollectQueue(g_view);
    } else {
        int ids[MAX_TRACKS];
        int total = 0;
        int n = StoreSearch(g_filter, ids, MAX_TRACKS, &total);
        for (int i = 0; i < n; ++i) {
            int ix = IndexById(ids[i]);
            if (ix >= 0)
                g_view[g_view_n++] = ix;
        }
    }
    if (g_liked_only || g_played_only) {
        int n = 0;
        for (int i = 0; i < g_view_n; ++i) {
            int t = g_view[i];
            if (g_liked_only && !g_tracks[t].liked)
                continue;
            if (g_played_only && g_tracks[t].plays <= 0)
                continue;
            g_view[n++] = t;
        }
        g_view_n = n;
    }
    if (g_view_n > 0 && ViewIndex(g_sel) < 0)
        g_sel = g_view[0];
    if (reset_scroll)
        g_scroll = 0;
    ClampScroll();
}

void FilterChanged()
{
    if (g_search)
        GetWindowTextW(g_search, g_filter, 128);
    RebuildView(TRUE);
    if (g_sel >= 0 && g_sel < g_count)
        PickOnly(g_sel);
    InvalidateRect(g_wnd, 0, FALSE);
}

static int g_anchor = -1;

static void ClearPicked()
{
    for (int i = 0; i < g_count; ++i)
        g_tracks[i].picked = 0;
}

int PickedCount()
{
    int n = 0;
    for (int i = 0; i < g_count; ++i)
        n += g_tracks[i].picked != 0;
    return n;
}

void PickOnly(int i)
{
    ClearPicked();
    if (i < 0 || i >= g_count)
        return;
    g_tracks[i].picked = 1;
    g_sel = i;
    g_anchor = i;
}

void PickToggle(int i)
{
    if (i < 0 || i >= g_count)
        return;
    g_tracks[i].picked = !g_tracks[i].picked;
    g_sel = i;
    g_anchor = i;
}

void PickRange(int i)
{
    if (i < 0 || i >= g_count)
        return;
    int a = ViewIndex(g_anchor);
    int b = ViewIndex(i);
    if (b < 0)
        return;
    if (a < 0)
        a = b;
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    ClearPicked();
    for (int v = a; v <= b; ++v)
        g_tracks[g_view[v]].picked = 1;
    g_sel = i;
}

void PickAllView()
{
    ClearPicked();
    for (int v = 0; v < g_view_n; ++v)
        g_tracks[g_view[v]].picked = 1;
    if (g_view_n > 0 && ViewIndex(g_sel) < 0)
        g_sel = g_view[0];
    if (g_sel >= 0)
        g_anchor = g_sel;
}

void MoveSelBy(int delta)
{
    if (g_view_n <= 0)
        return;
    int p = ViewIndex(g_sel);
    if (p < 0)
        p = 0;
    p = Clamp(p + delta, 0, g_view_n - 1);
    g_sel = g_view[p];
    if (GetKeyState(VK_SHIFT) & 0x8000)
        PickRange(g_sel);
    else
        PickOnly(g_sel);
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

static void CALLBACK StreamEnded(HSYNC, DWORD, DWORD, void *)
{
    if (g_wnd)
        PostMessageW(g_wnd, WM_TRACK_END, 0, 0);
}

int ProbePending()
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
            StoreSetSecs(g_tracks[t].id, g_tracks[t].secs);
            n++;
        }
    }
    for (int i = 0; i < g_count && n < 6; ++i) {
        if (g_tracks[i].pos >= 0 && g_tracks[i].secs < 0) {
            g_tracks[i].secs = ProbeSecs(g_tracks[i].path);
            StoreSetSecs(g_tracks[i].id, g_tracks[i].secs);
            n++;
        }
    }
    return n;
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
        if (!lstrcmpiW(g_tracks[i].path, path))
            return i;
    }
    return -1;
}

static int MaxPos()
{
    int m = -1;
    for (int i = 0; i < g_count; ++i) {
        if (g_tracks[i].pos > m)
            m = g_tracks[i].pos;
    }
    return m;
}

int CollectQueue(int *out)
{
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_tracks[i].pos >= 0)
            out[n++] = i;
    }
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (g_tracks[out[b]].pos < g_tracks[out[a]].pos) {
                int x = out[a];
                out[a] = out[b];
                out[b] = x;
            }
        }
    }
    return n;
}

static int AddTrack(const wchar_t *path)
{
    if (!path || !path[0] || !IsAudio(path))
        return -1;
    int exist = FindPath(path);
    if (exist >= 0) {
        if (g_tracks[exist].pos < 0) {
            g_tracks[exist].pos = MaxPos() + 1;
            StoreUpsert(&g_tracks[exist]);
        }
        return exist;
    }
    if (!Grow())
        return -1;
    Track *t = &g_tracks[g_count];
    memset(t, 0, sizeof(Track));
    Copy(t->path, path, MAX_PATH);
    t->secs = -1;
    t->pos = MaxPos() + 1;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        ReadTags(path, t->title, t->artist, t->album, META_CAP);
    if (!t->title[0])
        FillFromFileName(t);
    StoreUpsert(t);
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

int AddPath(const wchar_t *path)
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
    if (g_count > before)
        ArmTimer();
    return g_count - before;
}

void SavePlaylist()
{
    StoreSaveQueue();
}

void LoadPlaylist()
{
    StoreLoad();
    RebuildView(FALSE);
    int q[MAX_TRACKS];
    int n = CollectQueue(q);
    if (n) {
        if (g_sel < 0 || g_sel >= g_count || g_tracks[g_sel].pos < 0)
            g_sel = q[0];
        EnsureVisible(g_sel);
        PickOnly(g_sel);
    } else {
        g_sel = -1;
    }
}

BOOL PlayIndex(int i, BOOL show_err)
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
    BASS_ChannelSetSync(g_stream, BASS_SYNC_END, 0, StreamEnded, 0);
    g_cur = i;
    g_tracks[i].plays++;
    StoreBumpPlays(g_tracks[i].id);
    if (PickedCount() <= 1)
        PickOnly(i);
    g_kbps = BitrateOf(g_stream);
    g_bytes = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER u;
        u.LowPart = fad.nFileSizeLow;
        u.HighPart = fad.nFileSizeHigh;
        g_bytes = u.QuadPart;
    }
    QWORD bytes = BASS_ChannelGetLength(g_stream, BASS_POS_BYTE);
    if (bytes && bytes != (QWORD)-1) {
        g_tracks[i].secs = (int)BASS_ChannelBytes2Seconds(g_stream, bytes);
        StoreSetSecs(g_tracks[i].id, g_tracks[i].secs);
    }
    Copy(g_title, TrackTitle(&g_tracks[i]), MAX_PATH);
    SetWindowTextW(g_wnd, g_title);
    TrayTip();
    CenterIfHidden(i);
    InvalidateRect(g_wnd, 0, FALSE);
    ArmTimer();
    MediaSync();
    return TRUE;
}

BOOL PlayById(int id, BOOL show_err)
{
    int i = IndexById(id);
    if (i < 0)
        return FALSE;
    if (g_tracks[i].pos < 0) {
        g_tracks[i].pos = MaxPos() + 1;
        StoreSaveQueue();
        RebuildView(FALSE);
    }
    return PlayIndex(i, show_err);
}

BOOL PlayNext(BOOL wrap)
{
    int q[MAX_TRACKS];
    int n = CollectQueue(q);
    if (n <= 0)
        return FALSE;
    if (g_shuffle) {
        int w = 0;
        for (int i = 0; i < n; ++i) {
            if (q[i] != g_cur)
                q[w++] = q[i];
        }
        if (w <= 0)
            return g_cur >= 0 ? PlayIndex(g_cur, FALSE) : FALSE;
        for (int i = w - 1; i > 0; --i) {
            int j = (int)(RandU() % (unsigned)(i + 1));
            int t = q[i];
            q[i] = q[j];
            q[j] = t;
        }
        for (int i = 0; i < w; ++i) {
            if (PlayIndex(q[i], FALSE))
                return TRUE;
        }
        return FALSE;
    }
    int curpos = g_cur >= 0 ? g_tracks[g_cur].pos : -1;
    int start = 0;
    while (start < n && g_tracks[q[start]].pos <= curpos)
        ++start;
    if (start >= n) {
        if (!wrap)
            return FALSE;
        start = 0;
    }
    int k = start;
    for (;;) {
        if (PlayIndex(q[k], FALSE))
            return TRUE;
        k++;
        if (k >= n)
            k = 0;
        if (k == start)
            return FALSE;
    }
}

BOOL PlayPrev()
{
    if (g_count <= 0)
        return FALSE;
    if (g_stream) {
        QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
        if (BASS_ChannelBytes2Seconds(g_stream, at) > 3) {
            BASS_ChannelSetPosition(g_stream, 0, BASS_POS_BYTE);
            BASS_ChannelPlay(g_stream, FALSE);
            InvalidateRect(g_wnd, 0, FALSE);
            MediaSync();
            return TRUE;
        }
    }
    int q[MAX_TRACKS];
    int n = CollectQueue(q);
    if (n <= 0)
        return FALSE;
    int curpos = g_cur >= 0 ? g_tracks[g_cur].pos : 0;
    int k = n - 1;
    while (k >= 0 && g_tracks[q[k]].pos >= curpos)
        --k;
    if (k < 0)
        k = n - 1;
    int guard = k;
    for (;;) {
        if (PlayIndex(q[k], FALSE))
            return TRUE;
        k = k <= 0 ? n - 1 : k - 1;
        if (k == guard)
            return FALSE;
    }
}

void TogglePause()
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
    ArmTimer();
    MediaSync();
}

void StopPlayback()
{
    if (g_stream) {
        BASS_StreamFree(g_stream);
        g_stream = 0;
    }
    g_cur = -1;
    g_kbps = 0;
    g_bytes = 0;
    Copy(g_title, APP_NAME, MAX_PATH);
    if (g_wnd) {
        SetWindowTextW(g_wnd, g_title);
        TrayTip();
        InvalidateRect(g_wnd, 0, FALSE);
    }
    ArmTimer();
    MediaSync();
}

void SeekBy(double delta)
{
    if (!g_stream)
        return;
    QWORD at = BASS_ChannelGetPosition(g_stream, BASS_POS_BYTE);
    double sec = BASS_ChannelBytes2Seconds(g_stream, at) + delta;
    if (sec < 0)
        sec = 0;
    BASS_ChannelSetPosition(g_stream, BASS_ChannelSeconds2Bytes(g_stream, sec), BASS_POS_BYTE);
    InvalidateRect(g_wnd, 0, FALSE);
}
static void DropTrack(int i, BOOL disk)
{
    if (i < 0 || i >= g_count)
        return;
    PickOnly(i);
    RemovePicked(disk);
}

void RemovePicked(BOOL disk)
{
    if (PickedCount() == 0 && g_sel >= 0 && g_sel < g_count)
        g_tracks[g_sel].picked = 1;
    int view_i = -1;
    for (int v = 0; v < g_view_n; ++v) {
        if (g_tracks[g_view[v]].picked) {
            view_i = v;
            break;
        }
    }
    if (g_cur >= 0 && g_cur < g_count && g_tracks[g_cur].picked && g_stream) {
        BASS_StreamFree(g_stream);
        g_stream = 0;
        g_cur = -1;
        g_kbps = 0;
        g_bytes = 0;
        Copy(g_title, APP_NAME, MAX_PATH);
        SetWindowTextW(g_wnd, g_title);
        TrayTip();
    }
    int fail = 0;
    if (disk) {
        for (int i = 0; i < g_count; ++i) {
            if (!g_tracks[i].picked)
                continue;
            if (!DeleteFileW(g_tracks[i].path)) {
                g_tracks[i].picked = 0;
                fail++;
            }
        }
    }
    int cur_id = g_cur >= 0 ? g_tracks[g_cur].id : -1;
    int w = 0;
    for (int i = 0; i < g_count; ++i) {
        Track t = g_tracks[i];
        if (t.picked) {
            if (t.liked && !disk) {
                t.pos = -1;
                t.picked = 0;
                g_tracks[w++] = t;
            } else {
                StoreDelete(t.id);
            }
            continue;
        }
        t.picked = 0;
        g_tracks[w++] = t;
    }
    g_count = w;
    g_cur = cur_id > 0 ? IndexById(cur_id) : -1;
    StoreSaveQueue();
    RebuildView(FALSE);
    if (g_view_n <= 0)
        g_sel = -1;
    else if (view_i >= 0) {
        if (view_i >= g_view_n)
            view_i = g_view_n - 1;
        PickOnly(g_view[view_i]);
        EnsureVisible(g_sel);
    }
    ClampScroll();
    if (fail)
        MessageBoxW(g_wnd, L"Could not delete some files.", APP_NAME, MB_OK | MB_ICONERROR);
    InvalidateRect(g_wnd, 0, FALSE);
    ArmTimer();
}

void RemoveTrack(int i)
{
    DropTrack(i, FALSE);
}

void RemoveSel()
{
    RemovePicked(FALSE);
}

void ToggleLike(int i)
{
    if (i < 0 || i >= g_count)
        return;
    SetLike(i, !g_tracks[i].liked);
}

void SetLike(int i, int liked)
{
    if (i < 0 || i >= g_count)
        return;
    liked = liked ? 1 : 0;
    if (g_tracks[i].liked == liked)
        return;
    g_tracks[i].liked = liked;
    StoreSetLiked(g_tracks[i].id, liked);
    if (g_liked_only)
        RebuildView(FALSE);
    InvalidateRect(g_wnd, 0, FALSE);
}
