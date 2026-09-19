#include "player.h"
#include "util.h"
#include "ui.h"

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

void RebuildView(BOOL reset_scroll)
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

void FilterChanged()
{
    if (g_search)
        GetWindowTextW(g_search, g_filter, 128);
    RebuildView(TRUE);
    InvalidateRect(g_wnd, 0, FALSE);
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

void ProbePending()
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

void SavePlaylist()
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

void LoadPlaylist()
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

BOOL PlayNext(BOOL wrap)
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

void PlayPrev()
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
}

void StopPlayback()
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
void RemoveSel()
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
