/* SOMA_275_HTML: la orden "html": proyectos web (paginas de un solo archivo o con CSS y JS aparte) para el navegador de SOMA.
 *   html nuevo NOMBRE [basica|app|juego]   crea /workspace/NOMBRE/{index.html,estilo.css,app.js}
 *   html comprobar ARCHIVO                 el "compilador": etiquetas, ids, enlaces locales y sintaxis de los <script> (errores con su linea)
 *   html empaquetar ARCHIVO [SALIDA]       mete CSS, JS e imagenes locales en un solo .html
 *   html abrir ARCHIVO                     lo abre en el navegador
 * Se incluye en shell.c (una sola unidad de traduccion), despues de shell_js.c. Codigo puro en src/apps/htmlproj.c. */
#include "htmlproj.h"

static void html_abs(const char *arg, char *path, uint32_t cap)
{
    uint32_t p = 0;
    if (arg[0] != '/') { const char *pre = "/workspace/"; while (*pre && p + 1 < cap) path[p++] = *pre++; }
    for (uint32_t i = 0; arg[i] && p + 1 < cap; ++i) path[p++] = arg[i];
    path[p] = 0;
}
static void html_dir_of(const char *path, char *dir, uint32_t cap)
{
    uint32_t n = 0, last = 0; while (path[n]) { if (path[n] == '/') last = n; n++; }
    if (last == 0) last = 1;
    uint32_t i = 0; for (; i < last && i + 1 < cap; ++i) dir[i] = path[i];
    dir[i] = 0;
}
static int hv_exists(void *ud, const char *path) { (void)ud; uint32_t l = 0; char *b = vfs_read_alloc(path, &l); if (!b) return 0; kfree(b); return 1; }
static int hv_read(void *ud, const char *path, const char **data, uint32_t *len)
{
    (void)ud; uint32_t l = 0; char *b = vfs_read_alloc(path, &l); if (!b) return -1;
    *data = b; *len = l; return 0;
}
static void hv_release(void *ud, const char *d) { (void)ud; kfree((void *)d); }
static int hv_js_check(void *ud, const char *code, uint32_t len, int *line, char *msg, uint32_t cap)
{
    (void)ud;
    char o[512];
    int r = shell_js_source(1, code, len, "", o, sizeof o, 8000);
    if (r == 0) return 0;
    uint32_t ln = 0; char m[160];
    if (!cc_js_error(o, &ln, m, sizeof m)) { ln = 0; uint32_t q = 0; soma_str_put(m, sizeof m, &q, r < 0 ? "no se pudo analizar el script" : "error en el script"); }
    *line = (int)ln;
    uint32_t i = 0; for (; m[i] && i + 1 < cap; ++i) msg[i] = m[i];
    msg[i] = 0;
    return 1;
}
static const struct html_env g_hv = { 0, hv_exists, hv_read, hv_release, hv_js_check };

static int html_write_tpl(const char *dir, const char *kind, const char *titulo, int file)
{
    static char buf[16384];
    char path[160]; uint32_t p = 0;
    for (const char *d = dir; *d && p + 1 < sizeof path; ++d) path[p++] = *d;
    if (p + 1 < sizeof path) path[p++] = '/';
    for (const char *n = html_template_name(file); *n && p + 1 < sizeof path; ++n) path[p++] = *n;
    path[p] = 0;
    int n = html_template(kind, file, titulo, buf, sizeof buf);
    if (n < 0) return -1;
    return vfs_write(path, buf, (uint32_t)n) >= 0 ? 0 : -1;
}

static int cmd_html(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    char sub[24]; uint32_t sn = 0;
    while (args && *args && *args != ' ' && sn + 1 < sizeof sub) sub[sn++] = *args++;
    sub[sn] = 0;
    while (args && *args == ' ') args++;
    if (!sub[0] || str_eq(sub, "ayuda")) {
        soma_str_put(out_buf, max_out, &pos, "Webs en SOMA. html nuevo NOMBRE [basica|app|juego] (crea /workspace/NOMBRE con index.html, estilo.css y app.js) | "
                                              "html comprobar ARCHIVO (etiquetas, ids, enlaces y sintaxis del JavaScript, con su linea) | "
                                              "html empaquetar ARCHIVO [SALIDA] (CSS, JS e imagenes locales dentro de un solo .html) | html abrir ARCHIVO. "
                                              "Rutas relativas: /workspace. JavaScript sin pagina: js ayuda");
        return 1;
    }
    char a1[96], a2[96]; a1[0] = a2[0] = 0;
    { uint32_t i = 0; while (*args && *args != ' ' && i + 1 < sizeof a1) a1[i++] = *args++; a1[i] = 0; while (*args == ' ') args++;
      i = 0; while (*args && *args != ' ' && i + 1 < sizeof a2) a2[i++] = *args++; a2[i] = 0; }
    if (str_eq(sub, "nuevo")) {
        if (!a1[0]) { soma_str_put(out_buf, max_out, &pos, "uso: html nuevo NOMBRE [basica|app|juego]"); return 1; }
        char dir[120]; html_abs(a1, dir, sizeof dir);
        const char *kind = a2[0] ? a2 : "basica";
        char mk[160]; { uint32_t q = 0; soma_str_put(mk, sizeof mk, &q, dir); soma_str_put(mk, sizeof mk, &q, "/.dir"); } vfs_write(mk, "", 0);
        int bad = html_write_tpl(dir, kind, a1, 0) || html_write_tpl(dir, kind, a1, 1) || html_write_tpl(dir, kind, a1, 2);
        if (bad) soma_str_put(out_buf, max_out, &pos, "html nuevo: no se pudo crear el proyecto (tipos: basica, app, juego)");
        else { soma_str_put(out_buf, max_out, &pos, "html: proyecto creado en "); soma_str_put(out_buf, max_out, &pos, dir); soma_str_put(out_buf, max_out, &pos, " (index.html, estilo.css, app.js). Abrelo con: html abrir "); soma_str_put(out_buf, max_out, &pos, a1); soma_str_put(out_buf, max_out, &pos, "/index.html"); }
        return 1;
    }
    if (!a1[0]) { soma_str_put(out_buf, max_out, &pos, "uso: html comprobar|empaquetar|abrir ARCHIVO"); return 1; }
    char path[160]; html_abs(a1, path, sizeof path);
    char dir[160]; html_dir_of(path, dir, sizeof dir);
    uint32_t len = 0; char *src = vfs_read_alloc(path, &len);
    if (!src) { soma_str_put(out_buf, max_out, &pos, "html: no existe "); soma_str_put(out_buf, max_out, &pos, path); return 1; }
    if (str_eq(sub, "comprobar")) {
        struct html_report rep; char *rep_txt = (char *)kmalloc(8192);
        if (!rep_txt) { kfree(src); soma_str_put(out_buf, max_out, &pos, "html: sin memoria"); return 1; }
        int e = html_check(src, len, a1, dir, &g_hv, rep_txt, 8192, &rep);
        soma_str_put(out_buf, max_out, &pos, rep_txt);
        if (rep_txt[0]) soma_str_put(out_buf, max_out, &pos, "\n");
        soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)e); soma_str_put(out_buf, max_out, &pos, " errores, ");
        soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)rep.avisos); soma_str_put(out_buf, max_out, &pos, " avisos, ");
        soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)rep.scripts); soma_str_put(out_buf, max_out, &pos, " scripts, ");
        soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)rep.enlaces); soma_str_put(out_buf, max_out, &pos, " enlaces");
        kfree(rep_txt);
    } else if (str_eq(sub, "empaquetar")) {
        const uint32_t cap = 1u << 20;
        char *o = (char *)kmalloc(cap);
        if (!o) { kfree(src); soma_str_put(out_buf, max_out, &pos, "html: sin memoria"); return 1; }
        int miss = 0; int n = html_bundle(src, len, dir, &g_hv, o, cap, &miss);
        if (n < 0) soma_str_put(out_buf, max_out, &pos, "html empaquetar: el resultado no cabe (1 MiB)");
        else {
            char dst[160];
            if (a2[0]) html_abs(a2, dst, sizeof dst); else { uint32_t q = 0; soma_str_put(dst, sizeof dst, &q, path); soma_str_put(dst, sizeof dst, &q, ".empaquetado.html"); }
            if (vfs_write(dst, o, (uint32_t)n) < 0) soma_str_put(out_buf, max_out, &pos, "html empaquetar: no se pudo escribir la salida");
            else {
                soma_str_put(out_buf, max_out, &pos, "html: empaquetado en "); soma_str_put(out_buf, max_out, &pos, dst);
                soma_str_put(out_buf, max_out, &pos, " ("); soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)n); soma_str_put(out_buf, max_out, &pos, " bytes, ");
                soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)miss); soma_str_put(out_buf, max_out, &pos, " recursos sin encontrar)");
            }
        }
        kfree(o);
    } else if (str_eq(sub, "abrir")) {
        gui_open_browser(path);
        soma_str_put(out_buf, max_out, &pos, "html: abierto en el navegador: "); soma_str_put(out_buf, max_out, &pos, path);
    } else soma_str_put(out_buf, max_out, &pos, "html: subcomando desconocido (nuevo, comprobar, empaquetar, abrir, ayuda)");
    kfree(src);
    return 1;
}

/* Para el programador: comprueba (o "ejecuta" = comprueba) una pagina sin tocar el disco. Formato de salida de cc: "programa.html:LINEA: error: texto".
 * 0 bien, 1 errores (texto en out). */
int shell_html_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms)
{
    (void)check; (void)input; (void)max_ms;
    if (out && cap) out[0] = 0;
    if (!out || cap < 2) return -1;
    struct html_report rep;
    int e = html_check(src, len, "programa.html", "/workspace", &g_hv, out, cap, &rep);
    return e ? 1 : 0;
}
