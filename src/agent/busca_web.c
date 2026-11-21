/* SOMA_251_RUTAS: ver busca_web.h */
#include "busca_web.h"

static void put(char *o, uint32_t cap, uint32_t *q, const char *s, uint32_t n) { for (uint32_t i = 0; i < n && s[i] && *q + 1 < cap; i++) o[(*q)++] = s[i]; if (cap) o[*q < cap ? *q : cap - 1] = 0; }
static void puts_(char *o, uint32_t cap, uint32_t *q, const char *s) { uint32_t n = 0; while (s[n]) n++; put(o, cap, q, s, n); }
static const char *find(const char *h, const char *n) { for (; h && *h; h++) { uint32_t i = 0; while (n[i] && h[i] == n[i]) i++; if (!n[i]) return h; } return 0; }

void bw_urlenc(const char *in, char *out, uint32_t cap)
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < cap; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else if (c == ' ') out[o++] = '+';
        else { out[o++] = '%'; out[o++] = hx[c >> 4]; out[o++] = hx[c & 15]; }
    }
    if (cap) out[o] = 0;
}

/* texto sin etiquetas ni entidades basicas, con los espacios juntados */
static void clean(const char *a, const char *e, char *o, uint32_t cap, uint32_t *q, int json)
{
    int sp = 0;
    for (const char *p = a; p < e && *q + 4 < cap; p++) {
        if (*p == '<') { while (p < e && *p != '>') p++; continue; }
        if (json && *p == '\\') {
            p++; if (p >= e) break;
            if (*p == 'u' && p + 4 < e) {                                  /* \uXXXX */
                unsigned v = 0; for (int k = 1; k <= 4; k++) { char c = p[k]; v = v * 16 + (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0); }
                p += 4;
                if (v == '<') { while (p < e && *p != '>') { if (*p == '\\' && p[1] == 'u' && p + 5 < e && p[2] == '0' && p[3] == '0' && p[4] == '3' && (p[5] == 'e' || p[5] == 'E')) { p += 5; break; } p++; } continue; }
                if (v < 0x80) o[(*q)++] = (char)v;
                else if (v < 0x800) { o[(*q)++] = (char)(0xC0 | (v >> 6)); o[(*q)++] = (char)(0x80 | (v & 0x3F)); }
                else { o[(*q)++] = (char)(0xE0 | (v >> 12)); o[(*q)++] = (char)(0x80 | ((v >> 6) & 0x3F)); o[(*q)++] = (char)(0x80 | (v & 0x3F)); }
                sp = 0; continue;
            }
            char c = *p == 'n' || *p == 't' || *p == 'r' ? ' ' : *p;
            if (c == ' ') { if (!sp && *q) { o[(*q)++] = ' '; sp = 1; } } else { o[(*q)++] = c; sp = 0; }
            continue;
        }
        if (*p == '&') {
            static const char *const ent[][2] = { { "&amp;", "&" }, { "&quot;", "\"" }, { "&#39;", "'" }, { "&#x27;", "'" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&nbsp;", " " } };
            int hit = 0;
            for (unsigned k = 0; k < sizeof ent / sizeof ent[0]; k++) { uint32_t l = 0; while (ent[k][0][l]) l++; if (p + l <= e && find(p, ent[k][0]) == p) { o[(*q)++] = ent[k][1][0]; p += l - 1; hit = 1; break; } }
            if (hit) { sp = 0; continue; }
        }
        if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') { if (!sp && *q) { o[(*q)++] = ' '; sp = 1; } continue; }
        o[(*q)++] = *p; sp = 0;
    }
    while (*q && o[*q - 1] == ' ') (*q)--;
    o[*q] = 0;
}

int bw_parse_wikipedia(const char *j, const char *lang, char *out, uint32_t cap, int max)
{
    uint32_t q = 0; int n = 0;
    if (cap) out[0] = 0;
    const char *p = j ? find(j, "\"search\"") : 0;
    while (p && n < max) {
        const char *t = find(p, "\"title\":\""); if (!t) break;
        t += 9; const char *te = t; while (*te && !(*te == '"' && te[-1] != '\\')) te++;
        const char *sn = find(te, "\"snippet\":\""); const char *nt = find(te, "\"title\":\"");
        char title[160]; uint32_t tq = 0; clean(t, te, title, sizeof title, &tq, 1);
        puts_(out, cap, &q, "- "); puts_(out, cap, &q, title); puts_(out, cap, &q, ": ");
        if (sn && (!nt || sn < nt)) { sn += 11; const char *se = sn; while (*se && !(*se == '"' && se[-1] != '\\')) se++; clean(sn, se, out, cap, &q, 1); if (q + 1 < cap) out[q++] = '.'; out[q] = 0; p = se; }
        else p = te;
        puts_(out, cap, &q, " (https://"); puts_(out, cap, &q, lang); puts_(out, cap, &q, ".wikipedia.org/wiki/");
        for (uint32_t i = 0; title[i] && q + 2 < cap; i++) out[q++] = title[i] == ' ' ? '_' : title[i];
        puts_(out, cap, &q, ")\n");
        n++;
    }
    return n;
}

int bw_parse_ddg_lite(const char *h, char *out, uint32_t cap, int max)
{
    uint32_t q = 0; int n = 0;
    if (cap) out[0] = 0;
    const char *p = h;
    while (p && n < max) {
        const char *a = find(p, "result-link"); if (!a) break;
        const char *open = a; while (open > h && *open != '<') open--;           /* <a ... href="..." class='result-link'>Titulo</a> */
        const char *gt = find(a, ">"); if (!gt) break;
        const char *ae = find(gt, "</a>"); if (!ae) break;
        char url[300]; uint32_t uq = 0; url[0] = 0;
        const char *hr = find(open, "href=");
        if (hr && hr < gt) { hr += 5; char qc = *hr; if (qc == '"' || qc == '\'') hr++; else qc = ' ';
            const char *he = hr; while (he < gt && *he != qc && *he != '>') he++;
            /* //duckduckgo.com/l/?uddg=URL-codificada&... -> URL */
            const char *ud = find(hr, "uddg="); if (ud && ud < he) { hr = ud + 5; const char *e2 = hr; while (e2 < he && *e2 != '&') e2++; he = e2;
                for (const char *c = hr; c < he && uq + 2 < sizeof url; c++) { if (*c == '%' && c + 2 < he) { int v = 0; for (int k = 1; k <= 2; k++) { char d = c[k]; v = v * 16 + (d >= '0' && d <= '9' ? d - '0' : (d | 32) - 'a' + 10); } url[uq++] = (char)v; c += 2; } else url[uq++] = *c; }
                url[uq] = 0; }
            else { for (const char *c = hr; c < he && uq + 2 < sizeof url; c++) url[uq++] = *c; url[uq] = 0; } }
        puts_(out, cap, &q, "- "); clean(gt + 1, ae, out, cap, &q, 0);
        const char *sn = find(ae, "result-snippet"); const char *na = find(ae, "result-link");
        if (sn && (!na || sn < na)) { const char *sg = find(sn, ">"); const char *se = sg ? find(sg, "</td>") : 0; if (sg && se) { puts_(out, cap, &q, ": "); clean(sg + 1, se, out, cap, &q, 0); } }
        if (url[0]) { puts_(out, cap, &q, " ("); puts_(out, cap, &q, url); puts_(out, cap, &q, ")"); }
        puts_(out, cap, &q, "\n");
        n++; p = ae;
    }
    return n;
}
