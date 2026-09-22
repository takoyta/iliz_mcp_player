#include "store.h"
#include "util.h"
#include "tags.h"
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>

static sqlite3 *g_db;
static wchar_t **g_vocab;
static int g_vocab_n, g_vocab_cap;

static void Utf8FromWide(const wchar_t *w, char *out, int cap)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, 0, 0);
    if (n <= 0)
        out[0] = 0;
}

static void WideFromUtf8(const char *s, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    if (n <= 0)
        out[0] = 0;
}

static int Sql(const char *s)
{
    char *err = 0;
    int rc = sqlite3_exec(g_db, s, 0, 0, &err);
    sqlite3_free(err);
    return rc == SQLITE_OK;
}

const wchar_t *TrackTitle(const Track *t)
{
    static wchar_t buf[META_CAP * 2];
    const wchar_t *title = t->title[0] ? t->title : FileName(t->path);
    if (!t->artist[0])
        return title;
    int n = Len(t->artist);
    int i = 0;
    while (i < n && title[i] && title[i] == t->artist[i])
        ++i;
    if (i == n && title[i] == L' ' && title[i + 1] == L'-' && title[i + 2] == L' ')
        return title;
    Copy(buf, t->artist, META_CAP * 2);
    Append(buf, META_CAP * 2, L" - ");
    Append(buf, META_CAP * 2, title);
    return buf;
}

void FillFromFileName(Track *t)
{
    Copy(t->title, FileName(t->path), META_CAP);
    t->artist[0] = 0;
    t->album[0] = 0;
}

int IndexById(int id)
{
    for (int i = 0; i < g_count; ++i) {
        if (g_tracks[i].id == id)
            return i;
    }
    return -1;
}

BOOL StoreDataDir(wchar_t *out, int cap)
{
    out[0] = 0;
    if (cap < 8)
        return FALSE;
    if (!GetEnvironmentVariableW(L"APPDATA", out, cap))
        return FALSE;
    Append(out, cap, L"\\");
    Append(out, cap, APP_COMPANY);
    CreateDirectoryW(out, 0);
    Append(out, cap, L"\\");
    Append(out, cap, APP_INTERNAL);
    CreateDirectoryW(out, 0);
    return out[0] != 0;
}

static void DbPath(wchar_t *out)
{
    out[0] = 0;
    if (!StoreDataDir(out, MAX_PATH))
        return;
    Append(out, MAX_PATH, L"\\library.db");
}

static wchar_t FoldChar(wchar_t c)
{
    CharLowerBuffW(&c, 1);
    if (c == 0x451 || c == 0x401)
        c = 0x435;
    return c;
}

static void ToLatin(const wchar_t *in, wchar_t *out, int cap)
{
    int o = 0;
    for (int i = 0; in[i] && o < cap - 1; ++i) {
        wchar_t c = in[i];
        const wchar_t *rep = 0;
        switch (c) {
        case 0x430: case 0x410: rep = L"a"; break;
        case 0x431: case 0x411: rep = L"b"; break;
        case 0x432: case 0x412: rep = L"v"; break;
        case 0x433: case 0x413: rep = L"g"; break;
        case 0x434: case 0x414: rep = L"d"; break;
        case 0x435: case 0x415: case 0x451: case 0x401: rep = L"e"; break;
        case 0x436: case 0x416: rep = L"zh"; break;
        case 0x437: case 0x417: rep = L"z"; break;
        case 0x438: case 0x418: rep = L"i"; break;
        case 0x439: case 0x419: rep = L"y"; break;
        case 0x43a: case 0x41a: rep = L"k"; break;
        case 0x43b: case 0x41b: rep = L"l"; break;
        case 0x43c: case 0x41c: rep = L"m"; break;
        case 0x43d: case 0x41d: rep = L"n"; break;
        case 0x43e: case 0x41e: rep = L"o"; break;
        case 0x43f: case 0x41f: rep = L"p"; break;
        case 0x440: case 0x420: rep = L"r"; break;
        case 0x441: case 0x421: rep = L"s"; break;
        case 0x442: case 0x422: rep = L"t"; break;
        case 0x443: case 0x423: rep = L"u"; break;
        case 0x444: case 0x424: rep = L"f"; break;
        case 0x445: case 0x425: rep = L"kh"; break;
        case 0x446: case 0x426: rep = L"ts"; break;
        case 0x447: case 0x427: rep = L"ch"; break;
        case 0x448: case 0x428: rep = L"sh"; break;
        case 0x449: case 0x429: rep = L"shch"; break;
        case 0x44b: case 0x42b: rep = L"y"; break;
        case 0x44d: case 0x42d: rep = L"e"; break;
        case 0x44e: case 0x42e: rep = L"yu"; break;
        case 0x44f: case 0x42f: rep = L"ya"; break;
        case 0x44a: case 0x42a: case 0x44c: case 0x42c: rep = L""; break;
        default:
            out[o++] = FoldChar(c);
            continue;
        }
        while (*rep && o < cap - 1)
            out[o++] = *rep++;
    }
    out[o] = 0;
}

static wchar_t LatCyr(wchar_t c)
{
    switch (FoldChar(c)) {
    case L'a': return 0x430;
    case L'b': return 0x431;
    case L'c': return 0x43a;
    case L'd': return 0x434;
    case L'e': return 0x435;
    case L'f': return 0x444;
    case L'g': return 0x433;
    case L'h': return 0x445;
    case L'i': return 0x438;
    case L'j': return 0x439;
    case L'k': return 0x43a;
    case L'l': return 0x43b;
    case L'm': return 0x43c;
    case L'n': return 0x43d;
    case L'o': return 0x43e;
    case L'p': return 0x43f;
    case L'q': return 0x43a;
    case L'r': return 0x440;
    case L's': return 0x441;
    case L't': return 0x442;
    case L'u': return 0x443;
    case L'v': return 0x432;
    case L'w': return 0x432;
    case L'y': return 0x438;
    case L'z': return 0x437;
    default: return 0;
    }
}

static void ToCyr(const wchar_t *in, wchar_t *out, int cap)
{
    int o = 0;
    for (int i = 0; in[i] && o < cap - 1; ++i) {
        wchar_t c = LatCyr(in[i]);
        out[o++] = c ? c : FoldChar(in[i]);
    }
    out[o] = 0;
}

#define PHON_MAX 8
#define ALT_MAX  12

static int HasCyr(const wchar_t *s)
{
    for (; *s; ++s) {
        if (*s >= 0x400 && *s <= 0x4FF)
            return 1;
    }
    return 0;
}

static int AddAlt(wchar_t alts[][64], int n, int cap, const wchar_t *s)
{
    if (!s || !s[0] || n >= cap)
        return n;
    for (int i = 0; i < n; ++i) {
        if (!lstrcmpW(alts[i], s))
            return n;
    }
    Copy(alts[n], s, 64);
    return n + 1;
}

struct PhonRule {
    wchar_t from[8];
    const wchar_t *to[4];
};

static const PhonRule kPhon[] = {
    { { 0x432, 0x44d, 0x439, 0x432, 0 }, { L"wave", L"waive", 0 } },
    { { 0x432, 0x435, 0x439, 0x432, 0 }, { L"wave", L"waive", 0 } },
    { { 0x44d, 0x439, 0x432, 0 }, { L"ave", L"eiv", 0 } },
    { { 0x435, 0x439, 0x432, 0 }, { L"ave", L"eiv", 0 } },
    { { 0x432, 0x44d, 0x439, 0 }, { L"wa", L"way", L"we", 0 } },
    { { 0x432, 0x435, 0x439, 0 }, { L"wa", L"way", L"we", 0 } },
    { { 0x432, 0x430, 0x439, 0 }, { L"wa", L"way", L"wi", 0 } },
    { { 0x434, 0x436, 0 }, { L"j", L"dzh", 0 } },
    { { 0x43a, 0x441, 0 }, { L"x", L"ks", 0 } },
    { { 0x44d, 0x439, 0 }, { L"ey", L"ay", L"ei", 0 } },
    { { 0x435, 0x439, 0 }, { L"ey", L"ay", L"ei", 0 } },
    { { 0x430, 0x439, 0 }, { L"ay", L"ai", L"i", 0 } },
    { { 0x43e, 0x439, 0 }, { L"oy", L"oi", 0 } },
    { { 0x447, 0 }, { L"ch", L"tch", 0 } },
    { { 0x448, 0 }, { L"sh", 0 } },
    { { 0x449, 0 }, { L"sh", L"sch", 0 } },
    { { 0x446, 0 }, { L"ts", L"c", 0 } },
    { { 0x436, 0 }, { L"zh", L"j", 0 } },
    { { 0x44e, 0 }, { L"yu", L"ju", L"u", 0 } },
    { { 0x44f, 0 }, { L"ya", L"ja", 0 } },
    { { 0x445, 0 }, { L"h", L"kh", L"x", 0 } },
    { { 0x44a, 0 }, { L"", 0 } },
    { { 0x44c, 0 }, { L"", 0 } },
    { { 0x432, 0 }, { L"v", L"w", 0 } },
    { { 0x443, 0 }, { L"u", L"w", 0 } },
};

static int MatchFrom(const wchar_t *s, const wchar_t *from)
{
    int i = 0;
    while (from[i]) {
        if (s[i] != from[i])
            return 0;
        ++i;
    }
    return i;
}

static void PhonWalk(const wchar_t *s, wchar_t *cur, int clen,
                     wchar_t out[][64], int *n, int cap)
{
    if (*n >= cap)
        return;
    if (!s[0]) {
        cur[clen] = 0;
        *n = AddAlt(out, *n, cap, cur);
        return;
    }
    int best = 0;
    for (int r = 0; r < (int)(sizeof(kPhon) / sizeof(kPhon[0])); ++r) {
        int m = MatchFrom(s, kPhon[r].from);
        if (m > best)
            best = m;
    }
    if (best == 0) {
        wchar_t one[4];
        wchar_t tmp[2] = { s[0], 0 };
        ToLatin(tmp, one, 4);
        int tl = Len(one);
        if (clen + tl >= 63) {
            cur[clen] = 0;
            *n = AddAlt(out, *n, cap, cur);
            return;
        }
        for (int i = 0; i < tl; ++i)
            cur[clen + i] = one[i];
        PhonWalk(s + 1, cur, clen + tl, out, n, cap);
        return;
    }
    for (int r = 0; r < (int)(sizeof(kPhon) / sizeof(kPhon[0])); ++r) {
        if (MatchFrom(s, kPhon[r].from) != best)
            continue;
        for (int t = 0; t < 4 && kPhon[r].to[t]; ++t) {
            const wchar_t *rep = kPhon[r].to[t];
            int tl = Len(rep);
            if (clen + tl >= 63)
                continue;
            for (int i = 0; i < tl; ++i)
                cur[clen + i] = rep[i];
            PhonWalk(s + best, cur, clen + tl, out, n, cap);
            if (*n >= cap)
                return;
        }
    }
}

static int TokenAlts(const wchar_t *tok, wchar_t alts[][64], int cap)
{
    int n = 0;
    n = AddAlt(alts, n, cap, tok);
    wchar_t buf[64];
    ToLatin(tok, buf, 64);
    n = AddAlt(alts, n, cap, buf);
    ToCyr(tok, buf, 64);
    n = AddAlt(alts, n, cap, buf);
    if (HasCyr(tok)) {
        wchar_t cur[64];
        PhonWalk(tok, cur, 0, alts, &n, cap);
    }
    return n;
}

static void FoldField(const wchar_t *in, wchar_t *out, int cap)
{
    wchar_t low[512], lat[512], cyr[512];
    int n = 0;
    for (; in[n] && n < 511; ++n)
        low[n] = FoldChar(in[n]);
    low[n] = 0;
    ToLatin(low, lat, 512);
    ToCyr(low, cyr, 512);
    out[0] = 0;
    Copy(out, low, cap);
    if (lat[0] && lstrcmpW(lat, low)) {
        Append(out, cap, L" ");
        Append(out, cap, lat);
    }
    if (cyr[0] && lstrcmpW(cyr, low) && lstrcmpW(cyr, lat)) {
        Append(out, cap, L" ");
        Append(out, cap, cyr);
    }
}

static BOOL IsTok(wchar_t c)
{
    WORD t = 0;
    GetStringTypeW(CT_CTYPE1, &c, 1, &t);
    return (t & (C1_ALPHA | C1_DIGIT)) != 0;
}

static int SplitTok(const wchar_t *s, wchar_t tok[][64], int maxn)
{
    int n = 0, i = 0;
    while (s[i] && n < maxn) {
        while (s[i] && !IsTok(s[i]))
            ++i;
        if (!s[i])
            break;
        int o = 0;
        while (s[i] && IsTok(s[i]) && o < 63)
            tok[n][o++] = FoldChar(s[i++]);
        tok[n][o] = 0;
        if (o)
            ++n;
        while (s[i] && IsTok(s[i]))
            ++i;
    }
    return n;
}

static void VocabAdd(const wchar_t *t)
{
    if (!t[0] || Len(t) < 2)
        return;
    for (int i = 0; i < g_vocab_n; ++i) {
        if (!lstrcmpW(g_vocab[i], t))
            return;
    }
    if (g_vocab_n >= g_vocab_cap) {
        int cap = g_vocab_cap ? g_vocab_cap * 2 : 256;
        wchar_t **p = (wchar_t **)realloc(g_vocab, (size_t)cap * sizeof(wchar_t *));
        if (!p)
            return;
        g_vocab = p;
        g_vocab_cap = cap;
    }
    size_t n = (size_t)Len(t) + 1;
    wchar_t *c = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (!c)
        return;
    Copy(c, t, (int)n);
    g_vocab[g_vocab_n++] = c;
}

static void VocabAddField(const wchar_t *s)
{
    wchar_t tok[32][64];
    wchar_t fold[1024];
    FoldField(s, fold, 1024);
    int n = SplitTok(fold, tok, 32);
    for (int i = 0; i < n; ++i)
        VocabAdd(tok[i]);
}

static void VocabClear()
{
    for (int i = 0; i < g_vocab_n; ++i)
        free(g_vocab[i]);
    free(g_vocab);
    g_vocab = 0;
    g_vocab_n = g_vocab_cap = 0;
}

static int Lev(const wchar_t *a, const wchar_t *b)
{
    int na = Len(a), nb = Len(b);
    if (na > 48 || nb > 48)
        return 99;
    int d = na > nb ? na - nb : nb - na;
    if (d > 2)
        return 99;
    int prev[49], cur[49];
    for (int j = 0; j <= nb; ++j)
        prev[j] = j;
    for (int i = 1; i <= na; ++i) {
        cur[0] = i;
        int rowmin = i;
        for (int j = 1; j <= nb; ++j) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int v = del < ins ? del : ins;
            if (sub < v)
                v = sub;
            cur[j] = v;
            if (v < rowmin)
                rowmin = v;
        }
        if (rowmin > 2)
            return 99;
        memcpy(prev, cur, ((size_t)nb + 1) * sizeof(int));
    }
    return prev[nb];
}

static void FixTypos(wchar_t tok[][64], int n)
{
    for (int i = 0; i < n; ++i) {
        int len = Len(tok[i]);
        int maxd = len >= 6 ? 2 : len >= 4 ? 1 : 0;
        if (!maxd)
            continue;
        int hit = 0;
        for (int v = 0; v < g_vocab_n; ++v) {
            if (!lstrcmpW(g_vocab[v], tok[i])) {
                hit = 1;
                break;
            }
        }
        if (hit)
            continue;
        int best = 99;
        const wchar_t *who = 0;
        for (int v = 0; v < g_vocab_n; ++v) {
            int d = Lev(tok[i], g_vocab[v]);
            if (d < best) {
                best = d;
                who = g_vocab[v];
            }
        }
        if (who && best >= 1 && best <= maxd)
            Copy(tok[i], who, 64);
    }
}

static void BindWide(sqlite3_stmt *st, int i, const wchar_t *w)
{
    char utf[MAX_PATH * 4];
    Utf8FromWide(w ? w : L"", utf, sizeof(utf));
    sqlite3_bind_text(st, i, utf, -1, SQLITE_TRANSIENT);
}

static void ColWide(sqlite3_stmt *st, int i, wchar_t *out, int cap)
{
    const char *s = (const char *)sqlite3_column_text(st, i);
    if (!s)
        out[0] = 0;
    else
        WideFromUtf8(s, out, cap);
}

static void FtsUpsert(const Track *t)
{
    sqlite3_stmt *del = 0, *ins = 0;
    sqlite3_prepare_v2(g_db, "DELETE FROM tracks_fts WHERE rowid=?", -1, &del, 0);
    sqlite3_bind_int(del, 1, t->id);
    sqlite3_step(del);
    sqlite3_finalize(del);
    sqlite3_prepare_v2(g_db,
        "INSERT INTO tracks_fts(rowid, title, artist, album, file) VALUES (?,?,?,?,?)",
        -1, &ins, 0);
    sqlite3_bind_int(ins, 1, t->id);
    wchar_t fold[1024];
    char utf[4096];
    FoldField(t->title, fold, 1024);
    Utf8FromWide(fold, utf, sizeof(utf));
    sqlite3_bind_text(ins, 2, utf, -1, SQLITE_TRANSIENT);
    FoldField(t->artist, fold, 1024);
    Utf8FromWide(fold, utf, sizeof(utf));
    sqlite3_bind_text(ins, 3, utf, -1, SQLITE_TRANSIENT);
    FoldField(t->album, fold, 1024);
    Utf8FromWide(fold, utf, sizeof(utf));
    sqlite3_bind_text(ins, 4, utf, -1, SQLITE_TRANSIENT);
    FoldField(FileName(t->path), fold, 1024);
    Utf8FromWide(fold, utf, sizeof(utf));
    sqlite3_bind_text(ins, 5, utf, -1, SQLITE_TRANSIENT);
    sqlite3_step(ins);
    sqlite3_finalize(ins);
    VocabAddField(t->title);
    VocabAddField(t->artist);
    VocabAddField(t->album);
    VocabAddField(FileName(t->path));
}

static void RebuildFts()
{
    Sql("DELETE FROM tracks_fts");
    VocabClear();
    for (int i = 0; i < g_count; ++i)
        FtsUpsert(&g_tracks[i]);
}

static BOOL Schema()
{
    return Sql(
        "CREATE TABLE IF NOT EXISTS tracks ("
        " id INTEGER PRIMARY KEY,"
        " path TEXT NOT NULL UNIQUE,"
        " title TEXT NOT NULL DEFAULT '',"
        " artist TEXT NOT NULL DEFAULT '',"
        " album TEXT NOT NULL DEFAULT '',"
        " file TEXT NOT NULL DEFAULT '',"
        " secs INTEGER NOT NULL DEFAULT -1,"
        " liked INTEGER NOT NULL DEFAULT 0,"
        " plays INTEGER NOT NULL DEFAULT 0,"
        " pos INTEGER);"
        "CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5("
        " title, artist, album, file, tokenize='unicode61 remove_diacritics 2');");
}

BOOL StoreOpen()
{
    wchar_t path[MAX_PATH];
    DbPath(path);
    if (!path[0])
        return FALSE;
    char utf[MAX_PATH * 3];
    Utf8FromWide(path, utf, sizeof(utf));
    if (sqlite3_open(utf, &g_db) != SQLITE_OK)
        return FALSE;
    Sql("PRAGMA journal_mode=WAL");
    Sql("PRAGMA busy_timeout=3000");
    if (!Schema())
        return FALSE;
    Sql("ALTER TABLE tracks ADD COLUMN plays INTEGER NOT NULL DEFAULT 0");
    return TRUE;
}

void StoreClose()
{
    VocabClear();
    if (g_db) {
        sqlite3_close(g_db);
        g_db = 0;
    }
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

void StoreLoad()
{
    g_count = 0;
    if (!g_db)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db,
        "SELECT id, path, title, artist, album, secs, liked, pos, plays FROM tracks ORDER BY id",
        -1, &st, 0);
    g_loading = TRUE;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (!Grow())
            break;
        Track *t = &g_tracks[g_count];
        t->id = sqlite3_column_int(st, 0);
        ColWide(st, 1, t->path, MAX_PATH);
        ColWide(st, 2, t->title, META_CAP);
        ColWide(st, 3, t->artist, META_CAP);
        ColWide(st, 4, t->album, META_CAP);
        t->secs = sqlite3_column_int(st, 5);
        t->liked = sqlite3_column_int(st, 6);
        t->pos = sqlite3_column_type(st, 7) == SQLITE_NULL ? -1 : sqlite3_column_int(st, 7);
        t->plays = sqlite3_column_int(st, 8);
        t->picked = 0;
        if (!t->title[0])
            Copy(t->title, FileName(t->path), META_CAP);
        ++g_count;
    }
    sqlite3_finalize(st);
    g_loading = FALSE;
    for (int i = 0; i < g_count; ++i) {
        Track *t = &g_tracks[i];
        BOOL d = RepairLegacyMeta(t->title, META_CAP);
        d |= RepairLegacyMeta(t->artist, META_CAP);
        d |= RepairLegacyMeta(t->album, META_CAP);
        if (d)
            StoreUpsert(t);
    }
    RebuildFts();
}

void StoreUpsert(Track *t)
{
    if (!g_db || g_loading)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db,
        "INSERT INTO tracks(path,title,artist,album,file,secs,liked,plays,pos) VALUES(?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET title=excluded.title, artist=excluded.artist,"
        " album=excluded.album, file=excluded.file, secs=excluded.secs, pos=excluded.pos"
        " RETURNING id, liked, plays",
        -1, &st, 0);
    BindWide(st, 1, t->path);
    BindWide(st, 2, t->title);
    BindWide(st, 3, t->artist);
    BindWide(st, 4, t->album);
    BindWide(st, 5, FileName(t->path));
    sqlite3_bind_int(st, 6, t->secs);
    sqlite3_bind_int(st, 7, t->liked);
    sqlite3_bind_int(st, 8, t->plays);
    if (t->pos < 0)
        sqlite3_bind_null(st, 9);
    else
        sqlite3_bind_int(st, 9, t->pos);
    if (sqlite3_step(st) == SQLITE_ROW) {
        t->id = sqlite3_column_int(st, 0);
        t->liked = sqlite3_column_int(st, 1);
        t->plays = sqlite3_column_int(st, 2);
    }
    sqlite3_finalize(st);
    if (t->id)
        FtsUpsert(t);
}

void StoreSetLiked(int id, int liked)
{
    if (!g_db)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db, "UPDATE tracks SET liked=? WHERE id=?", -1, &st, 0);
    sqlite3_bind_int(st, 1, liked ? 1 : 0);
    sqlite3_bind_int(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void StoreBumpPlays(int id)
{
    if (!g_db)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db, "UPDATE tracks SET plays=plays+1 WHERE id=?", -1, &st, 0);
    sqlite3_bind_int(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void StoreSetSecs(int id, int secs)
{
    if (!g_db || id <= 0)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db, "UPDATE tracks SET secs=? WHERE id=?", -1, &st, 0);
    sqlite3_bind_int(st, 1, secs);
    sqlite3_bind_int(st, 2, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void StoreDelete(int id)
{
    if (!g_db)
        return;
    sqlite3_stmt *st = 0;
    sqlite3_prepare_v2(g_db, "DELETE FROM tracks_fts WHERE rowid=?", -1, &st, 0);
    sqlite3_bind_int(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sqlite3_prepare_v2(g_db, "DELETE FROM tracks WHERE id=?", -1, &st, 0);
    sqlite3_bind_int(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void StoreSaveQueue()
{
    if (!g_db || g_loading)
        return;
    int *ord = (int *)malloc((size_t)g_count * sizeof(int));
    if (!ord)
        return;
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_tracks[i].pos >= 0)
            ord[n++] = i;
    }
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (g_tracks[ord[b]].pos < g_tracks[ord[a]].pos) {
                int x = ord[a];
                ord[a] = ord[b];
                ord[b] = x;
            }
        }
    }
    Sql("BEGIN");
    sqlite3_stmt *clr = 0, *up = 0;
    sqlite3_prepare_v2(g_db, "UPDATE tracks SET pos=NULL", -1, &clr, 0);
    sqlite3_step(clr);
    sqlite3_finalize(clr);
    sqlite3_prepare_v2(g_db, "UPDATE tracks SET pos=? WHERE id=?", -1, &up, 0);
    for (int i = 0; i < n; ++i) {
        g_tracks[ord[i]].pos = i;
        sqlite3_reset(up);
        sqlite3_bind_int(up, 1, i);
        sqlite3_bind_int(up, 2, g_tracks[ord[i]].id);
        sqlite3_step(up);
    }
    sqlite3_finalize(up);
    Sql("COMMIT");
    free(ord);
}

static void EscTok(const wchar_t *t, char *out, int cap)
{
    char utf[256];
    Utf8FromWide(t, utf, sizeof(utf));
    int o = 0;
    if (o < cap - 1)
        out[o++] = '"';
    for (int i = 0; utf[i] && o < cap - 4; ++i) {
        if (utf[i] == '"')
            continue;
        out[o++] = utf[i];
    }
    if (o < cap - 3) {
        out[o++] = '"';
        out[o++] = '*';
    }
    out[o] = 0;
}

static void EscLike(const wchar_t *t, char *out, int cap)
{
    char utf[256];
    Utf8FromWide(t, utf, sizeof(utf));
    int o = 0;
    if (o < cap - 1)
        out[o++] = '%';
    for (int i = 0; utf[i] && o < cap - 3; ++i) {
        if (utf[i] == '%' || utf[i] == '_' || utf[i] == '\\') {
            if (o < cap - 4)
                out[o++] = '\\';
        }
        out[o++] = utf[i];
    }
    if (o < cap - 1)
        out[o++] = '%';
    out[o] = 0;
}

static void BuildMatch(wchar_t tok[][64], int n, char *out, int cap)
{
    out[0] = 0;
    int o = 0;
    for (int i = 0; i < n; ++i) {
        wchar_t alts[ALT_MAX][64];
        int na = TokenAlts(tok[i], alts, ALT_MAX);
        if (o && o < cap - 5) {
            memcpy(out + o, " AND ", 5);
            o += 5;
        }
        if (o < cap - 1)
            out[o++] = '(';
        int first = 1;
        for (int a = 0; a < na; ++a) {
            if (!first && o < cap - 4) {
                memcpy(out + o, " OR ", 4);
                o += 4;
            }
            first = 0;
            char esc[80];
            EscTok(alts[a], esc, 80);
            int k = 0;
            while (esc[k] && o < cap - 2)
                out[o++] = esc[k++];
        }
        if (o < cap - 1)
            out[o++] = ')';
    }
    out[o] = 0;
}

static int RunMatch(const char *match, int *ids, int cap, int *total)
{
    sqlite3_stmt *st = 0;
    *total = 0;
    int n = 0;
    if (sqlite3_prepare_v2(g_db,
            "SELECT t.id, bm25(tracks_fts, 4.0, 1.5, 2.0, 2.5) FROM tracks_fts"
            " JOIN tracks t ON t.id = tracks_fts.rowid"
            " WHERE tracks_fts MATCH ? ORDER BY 2, t.title LIMIT 500",
            -1, &st, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, match, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        if (n < cap)
            ids[n] = id;
        n++;
        (*total)++;
    }
    sqlite3_finalize(st);
    if (n > cap)
        n = cap;
    return n;
}

static int HasId(const int *ids, int n, int id)
{
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id)
            return 1;
    }
    return 0;
}

static int RunLike(wchar_t tok[][64], int ntok, int *ids, int cap, int *total)
{
    *total = 0;
    if (!ntok)
        return 0;
    char sql[8192];
    const char *hay = "(f.title||' '||f.artist||' '||f.album||' '||f.file)";
    int o = 0;
    const char *pre =
        "SELECT t.id FROM tracks_fts f JOIN tracks t ON t.id = f.rowid WHERE 1";
    while (pre[o]) {
        sql[o] = pre[o];
        ++o;
    }
    char pats[16][ALT_MAX][80];
    int nalt[16];
    for (int i = 0; i < ntok; ++i) {
        wchar_t alts[ALT_MAX][64];
        int na = TokenAlts(tok[i], alts, ALT_MAX);
        nalt[i] = 0;
        if (o >= (int)sizeof(sql) - 80)
            return 0;
        memcpy(sql + o, " AND (", 6);
        o += 6;
        int first = 1;
        for (int a = 0; a < na; ++a) {
            if (!first) {
                if (o >= (int)sizeof(sql) - 16)
                    return 0;
                memcpy(sql + o, " OR ", 4);
                o += 4;
            }
            first = 0;
            int k = 0;
            while (hay[k] && o < (int)sizeof(sql) - 12)
                sql[o++] = hay[k++];
            memcpy(sql + o, " LIKE ? ESCAPE '\\'", 18);
            o += 18;
            EscLike(alts[a], pats[i][nalt[i]], 80);
            nalt[i]++;
        }
        if (first)
            return 0;
        if (o < (int)sizeof(sql) - 1)
            sql[o++] = ')';
    }
    const char *tail = " ORDER BY t.title LIMIT 500";
    int k = 0;
    while (tail[k] && o < (int)sizeof(sql) - 1)
        sql[o++] = tail[k++];
    sql[o] = 0;

    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, 0) != SQLITE_OK)
        return 0;
    int b = 1;
    for (int i = 0; i < ntok; ++i) {
        for (int a = 0; a < nalt[i]; ++a)
            sqlite3_bind_text(st, b++, pats[i][a], -1, SQLITE_TRANSIENT);
    }
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        if (n < cap)
            ids[n] = id;
        n++;
        (*total)++;
    }
    sqlite3_finalize(st);
    if (n > cap)
        n = cap;
    return n;
}

static int MergeIds(int *ids, int n, int cap, const int *add, int na, int *total)
{
    for (int i = 0; i < na; ++i) {
        if (HasId(ids, n, add[i]))
            continue;
        if (n < cap)
            ids[n] = add[i];
        n++;
        (*total)++;
    }
    if (n > cap)
        n = cap;
    return n;
}

static int SearchOnce(wchar_t tok[][64], int ntok, int *ids, int cap, int *total)
{
    char match[2048];
    BuildMatch(tok, ntok, match, sizeof(match));
    int n = RunMatch(match, ids, cap, total);
    int extra[500];
    int et = 0;
    int ne = RunLike(tok, ntok, extra, 500, &et);
    return MergeIds(ids, n, cap, extra, ne, total);
}

int StoreSearch(const wchar_t *q, int *ids, int cap, int *total)
{
    *total = 0;
    if (!g_db || !q || !q[0])
        return 0;
    wchar_t tok[16][64];
    int ntok = SplitTok(q, tok, 16);
    if (!ntok)
        return 0;
    int n = SearchOnce(tok, ntok, ids, cap, total);
    if (n)
        return n;
    FixTypos(tok, ntok);
    return SearchOnce(tok, ntok, ids, cap, total);
}
