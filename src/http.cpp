#include "http.h"
#include "util.h"
#include "player.h"

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

void HttpOp(HttpJob *j)
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

void HttpStart()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return;
    HANDLE th = CreateThread(0, 0, HttpThread, 0, 0, 0);
    if (th)
        CloseHandle(th);
}

void HttpShutdown()
{
    SOCKET ls = g_http_listen;
    g_http_listen = INVALID_SOCKET;
    if (ls != INVALID_SOCKET)
        closesocket(ls);
}
