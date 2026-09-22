#include "tags.h"
#include "util.h"
#include <string.h>

static int Utf8Multi(const unsigned char *s, int n)
{
    int i = 0, multi = 0;
    while (i < n) {
        unsigned c = s[i];
        int need = 0;
        if (c < 0x80)
            need = 0;
        else if (c >= 0xC2 && c <= 0xDF)
            need = 1;
        else if (c >= 0xE0 && c <= 0xEF)
            need = 2;
        else if (c >= 0xF0 && c <= 0xF4)
            need = 3;
        else
            return 0;
        ++i;
        for (int k = 0; k < need; ++k) {
            if (i >= n || (s[i] & 0xC0) != 0x80)
                return 0;
            ++i;
        }
        if (need)
            ++multi;
    }
    return multi;
}

static void Decode8(const char *s, int n, wchar_t *out, int cap)
{
    if (n < 0) {
        n = 0;
        while (s[n]) ++n;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == 0))
        --n;
    UINT cp = 28591;
    if (Utf8Multi((const unsigned char *)s, n) > 0)
        cp = CP_UTF8;
    else {
        for (int i = 0; i < n; ++i) {
            if ((unsigned char)s[i] >= 0x80) {
                cp = 1251;
                break;
            }
        }
    }
    int w = MultiByteToWideChar(cp, 0, s, n, out, cap - 1);
    if (w < 0)
        w = 0;
    out[w] = 0;
}

BOOL RepairLegacyMeta(wchar_t *s, int cap)
{
    if (!s || !s[0])
        return FALSE;
    char raw[META_CAP];
    int n = 0;
    for (; s[n]; ++n) {
        if (s[n] > 0xFF || n >= META_CAP - 1)
            return FALSE;
        raw[n] = (char)s[n];
    }
    if (n < 2)
        return FALSE;
    wchar_t tmp[META_CAP];
    Decode8(raw, n, tmp, META_CAP);
    if (!tmp[0] || !lstrcmpW(tmp, s))
        return FALSE;
    int cyr = 0;
    for (int i = 0; tmp[i]; ++i) {
        if (tmp[i] >= 0x0400 && tmp[i] <= 0x04FF)
            ++cyr;
    }
    if (cyr < 2)
        return FALSE;
    Copy(s, tmp, cap);
    return TRUE;
}

static void Utf8(const char *s, int n, wchar_t *out, int cap)
{
    if (n < 0)
        n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == 0))
        --n;
    int w = MultiByteToWideChar(CP_UTF8, 0, s, n, out, cap - 1);
    if (w < 0)
        w = 0;
    out[w] = 0;
}

static void Utf16(const BYTE *p, int n, wchar_t *out, int cap)
{
    int be = 0;
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        be = 1;
        p += 2;
        n -= 2;
    } else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        p += 2;
        n -= 2;
    }
    int i = 0, o = 0;
    while (i + 1 < n && o < cap - 1) {
        unsigned c = be ? ((unsigned)p[i] << 8) | p[i + 1]
                        : p[i] | ((unsigned)p[i + 1] << 8);
        i += 2;
        if (!c)
            break;
        out[o++] = (wchar_t)c;
    }
    out[o] = 0;
}

static void EncText(int enc, const BYTE *p, int n, wchar_t *out, int cap)
{
    out[0] = 0;
    if (n <= 0)
        return;
    if (enc == 0)
        Decode8((const char *)p, n, out, cap);
    else if (enc == 3)
        Utf8((const char *)p, n, out, cap);
    else
        Utf16(p, n, out, cap);
}

static DWORD Synch(const BYTE *p)
{
    return ((DWORD)(p[0] & 0x7f) << 21) | ((DWORD)(p[1] & 0x7f) << 14) |
           ((DWORD)(p[2] & 0x7f) << 7) | (DWORD)(p[3] & 0x7f);
}

static DWORD Be32(const BYTE *p)
{
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
}

static void TakeIfEmpty(wchar_t *dst, int cap, const wchar_t *src)
{
    if (dst[0] || !src[0])
        return;
    Copy(dst, src, cap);
}

static BOOL IdEq(const char *id, int n, const char *s)
{
    int i = 0;
    while (s[i]) {
        if (i >= n || id[i] != s[i])
            return FALSE;
        ++i;
    }
    return i == n;
}

static void WalkId3v2(const BYTE *p, DWORD n,
                     void (*fn)(const char *id, int idn, const BYTE *d, DWORD fs, void *ctx),
                     void *ctx)
{
    if (n < 10 || p[0] != 'I' || p[1] != 'D' || p[2] != '3')
        return;
    int ver = p[3];
    DWORD size = Synch(p + 6);
    const BYTE *q = p + 10;
    const BYTE *end = p + n;
    if (p + 10 + size < end)
        end = p + 10 + size;
    if (p[5] & 0x40) {
        if (q + 4 > end)
            return;
        DWORD ex = ver >= 4 ? Synch(q) : Be32(q);
        if (q + ex > end)
            return;
        q += ex;
    }
    while (q + 6 <= end && q[0]) {
        if (ver == 2) {
            if (q + 6 > end)
                break;
            DWORD fs = ((DWORD)q[3] << 16) | ((DWORD)q[4] << 8) | q[5];
            const BYTE *d = q + 6;
            if (d + fs > end)
                break;
            fn((const char *)q, 3, d, fs, ctx);
            q = d + fs;
            continue;
        }
        if (q + 10 > end)
            break;
        DWORD fs = ver >= 4 ? Synch(q + 4) : Be32(q + 4);
        const BYTE *d = q + 10;
        if (d + fs > end)
            break;
        fn((const char *)q, 4, d, fs, ctx);
        q = d + fs;
    }
}

struct Read3 {
    wchar_t *title;
    wchar_t *artist;
    wchar_t *album;
    int cap;
};

static void OnCoreFrame(const char *id, int idn, const BYTE *d, DWORD fs, void *ctx)
{
    Read3 *c = (Read3 *)ctx;
    if (fs < 2)
        return;
    int which = 0;
    if (IdEq(id, idn, "TIT2") || IdEq(id, idn, "TT2"))
        which = 1;
    else if (IdEq(id, idn, "TPE1") || IdEq(id, idn, "TP1"))
        which = 2;
    else if (IdEq(id, idn, "TALB") || IdEq(id, idn, "TAL"))
        which = 3;
    if (!which)
        return;
    wchar_t tmp[META_CAP];
    EncText(d[0], d + 1, (int)fs - 1, tmp, META_CAP);
    if (which == 1)
        TakeIfEmpty(c->title, c->cap, tmp);
    else if (which == 2)
        TakeIfEmpty(c->artist, c->cap, tmp);
    else
        TakeIfEmpty(c->album, c->cap, tmp);
}

static void ParseId3v2(const BYTE *p, DWORD n, wchar_t *title, wchar_t *artist, wchar_t *album, int cap)
{
    Read3 c = { title, artist, album, cap };
    WalkId3v2(p, n, OnCoreFrame, &c);
}

static int KeyIs(const char *s, const char *k)
{
    while (*k) {
        char a = *s, b = *k;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b)
            return 0;
        ++s;
        ++k;
    }
    return *s == '=' || *s == ':';
}

static void ParseKv(const char *p, wchar_t *title, wchar_t *artist, wchar_t *album, int cap)
{
    if (!p)
        return;
    while (*p) {
        const char *eq = p;
        while (*eq && *eq != '=' && *eq != ':')
            ++eq;
        const char *v = *eq ? eq + 1 : eq;
        wchar_t tmp[META_CAP];
        Utf8(v, -1, tmp, META_CAP);
        if (KeyIs(p, "title") || KeyIs(p, "TIT2"))
            TakeIfEmpty(title, cap, tmp);
        else if (KeyIs(p, "artist") || KeyIs(p, "TPE1"))
            TakeIfEmpty(artist, cap, tmp);
        else if (KeyIs(p, "album") || KeyIs(p, "TALB"))
            TakeIfEmpty(album, cap, tmp);
        p += strlen(p) + 1;
    }
}

void ReadTags(const wchar_t *path, wchar_t *title, wchar_t *artist, wchar_t *album, int cap)
{
    title[0] = artist[0] = album[0] = 0;
    HSTREAM s = BASS_StreamCreateFile(FALSE, path, 0, 0, BASS_UNICODE | BASS_STREAM_DECODE);
    if (!s)
        return;
    const TAG_BINARY *bin = (const TAG_BINARY *)BASS_ChannelGetTags(s, BASS_TAG_ID3V2_BINARY);
    if (bin && bin->data && bin->length >= 10)
        ParseId3v2((const BYTE *)bin->data, bin->length, title, artist, album, cap);
    else {
        const BYTE *v2 = (const BYTE *)BASS_ChannelGetTags(s, BASS_TAG_ID3V2);
        if (v2)
            ParseId3v2(v2, 10 + Synch(v2 + 6) + 10, title, artist, album, cap);
    }
    ParseKv(BASS_ChannelGetTags(s, BASS_TAG_OGG), title, artist, album, cap);
    ParseKv(BASS_ChannelGetTags(s, BASS_TAG_APE), title, artist, album, cap);
    ParseKv(BASS_ChannelGetTags(s, BASS_TAG_MP4), title, artist, album, cap);
    ParseKv(BASS_ChannelGetTags(s, BASS_TAG_WMA), title, artist, album, cap);
    const TAG_ID3 *v1 = (const TAG_ID3 *)BASS_ChannelGetTags(s, BASS_TAG_ID3);
    if (v1) {
        wchar_t tmp[META_CAP];
        Decode8(v1->title, 30, tmp, META_CAP);
        TakeIfEmpty(title, cap, tmp);
        Decode8(v1->artist, 30, tmp, META_CAP);
        TakeIfEmpty(artist, cap, tmp);
        Decode8(v1->album, 30, tmp, META_CAP);
        TakeIfEmpty(album, cap, tmp);
    }
    BASS_StreamFree(s);
}

static void AddTag(TagItem *items, int *n, int cap, const wchar_t *k, const wchar_t *v)
{
    if (!v || !v[0] || *n >= cap)
        return;
    Copy(items[*n].k, k, 48);
    Copy(items[*n].v, v, META_CAP);
    ++*n;
}

static int SkipTerm(const BYTE *p, int n, int enc)
{
    if (enc == 1 || enc == 2) {
        int i = 0;
        while (i + 1 < n && (p[i] || p[i + 1]))
            i += 2;
        return i + 1 < n ? i + 2 : n;
    }
    int i = 0;
    while (i < n && p[i])
        ++i;
    return i < n ? i + 1 : n;
}

static void FrameKey(const char *id, int n, wchar_t *out, int cap)
{
    static const struct { const char *id; const wchar_t *name; } map[] = {
        {"TIT2", L"Title"}, {"TT2", L"Title"},
        {"TPE1", L"Artist"}, {"TP1", L"Artist"},
        {"TPE2", L"Album artist"}, {"TP2", L"Album artist"},
        {"TALB", L"Album"}, {"TAL", L"Album"},
        {"TRCK", L"Track"}, {"TRK", L"Track"},
        {"TYER", L"Year"}, {"TYE", L"Year"}, {"TDRC", L"Date"},
        {"TCON", L"Genre"}, {"TCO", L"Genre"},
        {"COMM", L"Comment"}, {"COM", L"Comment"},
        {"TCOM", L"Composer"}, {"TCM", L"Composer"},
        {"TIT1", L"Content group"}, {"TIT3", L"Subtitle"},
        {"TPOS", L"Disc"}, {"TCOP", L"Copyright"},
        {"USLT", L"Lyrics"}, {"ULT", L"Lyrics"},
        {"TBPM", L"BPM"}, {"TENC", L"Encoded by"},
        {"TSSE", L"Encoder"}, {"TPUB", L"Publisher"},
        {"APIC", L"Cover"}, {"PIC", L"Cover"},
        {0, 0}
    };
    for (int i = 0; map[i].id; ++i) {
        if (IdEq(id, n, map[i].id)) {
            Copy(out, map[i].name, cap);
            return;
        }
    }
    int o = 0;
    for (; o < n && o < cap - 1 && id[o] > 32; ++o)
        out[o] = (wchar_t)(unsigned char)id[o];
    out[o] = 0;
}

static void FrameVal(const char *id, int idn, const BYTE *d, DWORD fs, wchar_t *key, int kcap, wchar_t *val, int vcap)
{
    key[0] = val[0] = 0;
    FrameKey(id, idn, key, kcap);
    if (fs < 1)
        return;
    if (IdEq(id, idn, "APIC") || IdEq(id, idn, "PIC")) {
        wsprintfW(val, L"[image, %u bytes]", fs);
        return;
    }
    if (id[0] != 'T' && !IdEq(id, idn, "COMM") && !IdEq(id, idn, "COM") &&
        !IdEq(id, idn, "USLT") && !IdEq(id, idn, "ULT"))
        return;
    int enc = d[0];
    const BYTE *p = d + 1;
    int n = (int)fs - 1;
    if (n <= 0)
        return;
    BOOL comm = IdEq(id, idn, "COMM") || IdEq(id, idn, "COM") ||
                IdEq(id, idn, "USLT") || IdEq(id, idn, "ULT");
    BOOL txxx = IdEq(id, idn, "TXXX") || IdEq(id, idn, "TXX");
    if (comm) {
        if (n < 3)
            return;
        p += 3;
        n -= 3;
        int skip = SkipTerm(p, n, enc);
        EncText(enc, p + skip, n - skip, val, vcap);
        return;
    }
    if (txxx) {
        int skip = SkipTerm(p, n, enc);
        wchar_t desc[48];
        EncText(enc, p, skip, desc, 48);
        if (desc[0])
            Copy(key, desc, kcap);
        EncText(enc, p + skip, n - skip, val, vcap);
        return;
    }
    EncText(enc, p, n, val, vcap);
}

struct Dump3 {
    TagItem *items;
    int *n;
    int cap;
};

static void OnDumpFrame(const char *id, int idn, const BYTE *d, DWORD fs, void *ctx)
{
    Dump3 *c = (Dump3 *)ctx;
    wchar_t k[48], v[META_CAP];
    FrameVal(id, idn, d, fs, k, 48, v, META_CAP);
    AddTag(c->items, c->n, c->cap, k, v);
}

static void DumpKv(const char *p, TagItem *items, int *n, int cap)
{
    if (!p)
        return;
    while (*p) {
        const char *eq = p;
        while (*eq && *eq != '=' && *eq != ':')
            ++eq;
        wchar_t k[48], v[META_CAP];
        Utf8(p, (int)(eq - p), k, 48);
        Utf8(*eq ? eq + 1 : eq, -1, v, META_CAP);
        AddTag(items, n, cap, k, v);
        p += strlen(p) + 1;
    }
}

static const wchar_t *CtypeName(DWORD t)
{
    if (t & BASS_CTYPE_STREAM_WAV) return L"WAV";
    if (t == BASS_CTYPE_STREAM_MP3) return L"MP3";
    if (t == BASS_CTYPE_STREAM_MP2) return L"MP2";
    if (t == BASS_CTYPE_STREAM_MP1) return L"MP1";
    if (t == BASS_CTYPE_STREAM_OGG) return L"OGG";
    if (t == BASS_CTYPE_STREAM_AIFF) return L"AIFF";
    if (t == BASS_CTYPE_STREAM_MF) return L"Media Foundation";
    return L"Audio";
}

static void WalkId3From(HSTREAM s, void (*fn)(const char *id, int idn, const BYTE *d, DWORD fs, void *ctx), void *ctx)
{
    const TAG_BINARY *bin = (const TAG_BINARY *)BASS_ChannelGetTags(s, BASS_TAG_ID3V2_BINARY);
    if (bin && bin->data && bin->length >= 10)
        WalkId3v2((const BYTE *)bin->data, bin->length, fn, ctx);
    else {
        const BYTE *v2 = (const BYTE *)BASS_ChannelGetTags(s, BASS_TAG_ID3V2);
        if (v2)
            WalkId3v2(v2, 10 + Synch(v2 + 6) + 10, fn, ctx);
    }
}

int DumpTags(const wchar_t *path, TagItem *items, int cap)
{
    int n = 0;
    AddTag(items, &n, cap, L"File", path);
    HSTREAM s = BASS_StreamCreateFile(FALSE, path, 0, 0, BASS_UNICODE | BASS_STREAM_DECODE);
    if (!s)
        return n;
    BASS_CHANNELINFO inf = { 0 };
    if (BASS_ChannelGetInfo(s, &inf)) {
        AddTag(items, &n, cap, L"Format", CtypeName(inf.ctype));
        wchar_t buf[64];
        if (inf.freq) {
            wsprintfW(buf, L"%u Hz", inf.freq);
            AddTag(items, &n, cap, L"Sample rate", buf);
        }
        if (inf.chans) {
            wsprintfW(buf, L"%u", inf.chans);
            AddTag(items, &n, cap, L"Channels", buf);
        }
    }
    QWORD len = BASS_ChannelGetLength(s, BASS_POS_BYTE);
    if (len != (QWORD)-1) {
        int sec = (int)(BASS_ChannelBytes2Seconds(s, len) + 0.5);
        if (sec >= 0) {
            wchar_t buf[32];
            wsprintfW(buf, L"%d:%02d", sec / 60, sec % 60);
            AddTag(items, &n, cap, L"Duration", buf);
        }
    }
    float br = 0;
    if (BASS_ChannelGetAttribute(s, BASS_ATTRIB_BITRATE, &br) && br > 0) {
        wchar_t buf[32];
        wsprintfW(buf, L"%d kbps", (int)(br + 0.5f));
        AddTag(items, &n, cap, L"Bitrate", buf);
    }
    Dump3 d = { items, &n, cap };
    WalkId3From(s, OnDumpFrame, &d);
    DumpKv(BASS_ChannelGetTags(s, BASS_TAG_OGG), items, &n, cap);
    DumpKv(BASS_ChannelGetTags(s, BASS_TAG_APE), items, &n, cap);
    DumpKv(BASS_ChannelGetTags(s, BASS_TAG_MP4), items, &n, cap);
    DumpKv(BASS_ChannelGetTags(s, BASS_TAG_WMA), items, &n, cap);
    const TAG_ID3 *v1 = (const TAG_ID3 *)BASS_ChannelGetTags(s, BASS_TAG_ID3);
    if (v1) {
        wchar_t tmp[META_CAP];
        Decode8(v1->title, 30, tmp, META_CAP);
        AddTag(items, &n, cap, L"ID3v1 Title", tmp);
        Decode8(v1->artist, 30, tmp, META_CAP);
        AddTag(items, &n, cap, L"ID3v1 Artist", tmp);
        Decode8(v1->album, 30, tmp, META_CAP);
        AddTag(items, &n, cap, L"ID3v1 Album", tmp);
        Decode8(v1->year, 4, tmp, META_CAP);
        AddTag(items, &n, cap, L"ID3v1 Year", tmp);
        int cn = 30;
        if (v1->comment[28] == 0) {
            cn = 28;
            if (v1->comment[29]) {
                wsprintfW(tmp, L"%u", (unsigned)v1->comment[29]);
                AddTag(items, &n, cap, L"ID3v1 Track", tmp);
            }
        }
        Decode8(v1->comment, cn, tmp, META_CAP);
        AddTag(items, &n, cap, L"ID3v1 Comment", tmp);
        wsprintfW(tmp, L"%u", (unsigned)v1->genre);
        AddTag(items, &n, cap, L"ID3v1 Genre", tmp);
    }
    BASS_StreamFree(s);
    return n;
}
