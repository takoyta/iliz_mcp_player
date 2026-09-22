#include "util.h"
int S(int v) { return MulDiv(v, g_dpi, 96); }
int Abs(int v) { return v < 0 ? -v : v; }

int Clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

HANDLE Heap() { return GetProcessHeap(); }

void Copy(wchar_t *dst, const wchar_t *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

int Len(const wchar_t *s)
{
    int n = 0;
    while (s[n]) ++n;
    return n;
}

void Append(wchar_t *dst, int cap, const wchar_t *src)
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

BOOL EqI(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b) {
        if (Lower(*a) != Lower(*b))
            return FALSE;
        ++a;
        ++b;
    }
    return *a == *b;
}

BOOL ContainsI(const wchar_t *hay, const wchar_t *needle)
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

const wchar_t *FileName(const wchar_t *path)
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

BOOL IsAudio(const wchar_t *path)
{
    const wchar_t *e = Ext(path);
    return EqI(e, L"mp3") || EqI(e, L"mp2") || EqI(e, L"mp1") ||
           EqI(e, L"ogg") || EqI(e, L"wav") || EqI(e, L"aif") || EqI(e, L"aiff");
}
BOOL WorkArea(const RECT *r, RECT *out)
{
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi))
        return FALSE;
    *out = mi.rcWork;
    return TRUE;
}
