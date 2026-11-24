/* SOMA_285_CHAT: la parte pura de la aplicacion de chat.
 *   chat_core.c  historial: altas, bajas, titulos, archivo (ida y vuelta exacta), orden y grupos de la lista, busqueda sin tildes, contexto.
 *   llm_proto.c  conversacion de varios turnos para los tres formatos (OpenAI/llama.cpp, Claude, Gemini) y que una sola pregunta sale igual que antes.
 *   chat_md.c    Markdown + LaTeX + HTML: bloques, zonas (enlaces, copiar codigo, imagenes), resaltado, limites y textos al azar sin romper nada. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chat_core.h"
#include "chat_md.h"
#include "llm_proto.h"

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static long g_alloc;
void *cc_alloc(uint32_t n) { g_alloc++; return malloc(n ? n : 1); }
void  cc_free(void *p) { if (p) g_alloc--; free(p); }

/* ---------------------------------------------------------------- medidas falsas: 0,55 px por px de letra (mono 0,6) */
static int fw(const char *s, int n, int px, int fl, void *u)
{
    (void)u; int ch = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) ch++;
    return ch * px * ((fl & MD_F_MONO) ? 60 : (fl & MD_F_BOLD) ? 58 : 55) / 100;
}
static int fa(int px, void *u) { (void)u; return px * 8 / 10; }
static int fd(int px, void *u) { (void)u; return px * 2 / 10 + 1; }
static int fimg(const char *s, int n, int *w, int *h, void *u) { (void)u; if (n > 4 && !memcmp(s, "/img", 4)) { *w = 1200; *h = 800; return 0; } if (n > 4 && !memcmp(s, "http", 4)) return 1; return -1; }
static const struct md_metrics M = { fw, fa, fd, fimg, 0 };
static const struct md_theme T = { 0xE6E6E6, 0x8B949E, 0xFFFFFF, 0x60A5FA, 0x2A3150, 0x0D1117, 0x161B22, 0x30363D, 0xE6EDF3, 0x1E2433, 0xF0ABFC, 0x6366F1,
                                   0x34D399, 0x1B2036, 0x141A2E, 0x6366F1, 0xFF7B72, 0xA5D6FF, 0x79C0FF, 0x8B949E, 0xD2A8FF, 0xFFA657, 0xEF4444 };

static struct md_op ops[30000]; static char pool[1 << 20]; static struct md_zone zs[2000];
static struct md_out o;
static void *scr;
static int lay(const char *s, int w)
{
    o.ops = ops; o.cap = 30000; o.pool = pool; o.poolcap = sizeof pool; o.z = zs; o.zcap = 2000;
    int h = md_layout(s, (uint32_t)strlen(s), w, 15, &M, &T, &o, scr);
    for (int i = 0; i < o.nops; i++) {
        if (ops[i].type == MD_TEXT || ops[i].type == MD_IMG) { if (ops[i].s + ops[i].n > o.npool) { printf("op %d fuera del pool\n", i); fails++; break; } }
        if (ops[i].zone >= o.nz) { printf("op %d zona %d de %d\n", i, ops[i].zone, o.nz); fails++; break; }
    }
    return h;
}
static int has(const char *t)
{
    size_t n = strlen(t);
    for (int i = 0; i < o.nops; i++) if (ops[i].type == MD_TEXT && ops[i].n == n && !memcmp(pool + ops[i].s, t, n)) return i;
    return -1;
}
static int zona(int kind, const char *t)
{
    size_t n = strlen(t);
    for (int i = 0; i < o.nz; i++) if (zs[i].kind == kind && zs[i].n == n && !memcmp(pool + zs[i].s, t, n)) return i;
    return -1;
}
static int max_right(void)
{
    int r = 0;
    for (int i = 0; i < o.nops; i++) {
        int x1 = ops[i].type == MD_TEXT ? ops[i].x + fw(pool + ops[i].s, (int)ops[i].n, ops[i].px, ops[i].fl, 0) : ops[i].type == MD_LINE ? (ops[i].x + ops[i].w) / 16 : ops[i].x + ops[i].w;
        if (x1 > r) r = x1;
    }
    return r;
}

static const char Q1[] = "## \302\277C\303\263mo funciona la **memoria** de SOMA cuando duerme y ordena los recuerdos en ramas?\nSegunda linea";
static const char R1[] = "Respuesta con \"comillas\"\ny saltos\n\nde l\303\255nea";
static const char B1[] = "Ab\303\223ut caf\303\251 y \303\261and\303\272";
static void prueba_core(void)
{
    struct cc_hist h; cc_init(&h);
    uint32_t hoy = 29000000u;                                  /* minutos */
    struct cc_conv *a = cc_nueva(&h, hoy - 3 * 1440, CC_MODO_LLM, 1);
    CK(a && a->id == 1, "nueva");
    cc_anade(a, CC_USER, Q1, (uint32_t)strlen(Q1), 0, hoy - 3 * 1440);
    struct cc_msg *r = cc_anade(a, CC_ASIS, R1, (uint32_t)strlen(R1), 0, hoy - 3 * 1440 + 1);
    r->dur = 4321; r->estado = CC_OK;
    cc_titulo_auto(a);
    CK(!strncmp(a->titulo, "\302\277C\303\263mo funciona la memoria de SOMA", 34) && strstr(a->titulo, "\342\200\246") && strlen(a->titulo) < CC_TIT_MAX, "titulo auto [%s]", a->titulo);
    struct cc_conv *b = cc_nueva(&h, hoy - 10, CC_MODO_AGENTE, 0);
    cc_anade(b, CC_USER, B1, (uint32_t)strlen(B1), "/usuario/foto.png\n/doc/a.txt", hoy - 10);
    cc_titulo_auto(b);
    struct cc_conv *c = cc_nueva(&h, hoy - 40 * 1440, CC_MODO_LLM, 1);
    cc_anade(c, CC_USER, "vieja", 5, 0, hoy - 40 * 1440);
    uint32_t cid = c->id;
    cc_titulo_pon(c, "  Mi t\303\255tulo  ");
    CK(!strcmp(c->titulo, "Mi t\303\255tulo") && c->titulo_manual, "titulo manual [%s]", c->titulo);
    cc_titulo_auto(c);
    CK(!strcmp(c->titulo, "Mi t\303\255tulo"), "el manual no se pisa");
    struct cc_conv *d = cc_nueva(&h, hoy - 1440 - 5, CC_MODO_LLM, 1); cc_anade(d, CC_USER, "ayer", 4, 0, hoy - 1440 - 5); d->fija = 0;
    struct cc_conv *e = cc_nueva(&h, hoy - 500 * 1440, CC_MODO_LLM, 1); cc_anade(e, CC_USER, "fijada", 6, 0, hoy - 500 * 1440); e->fija = 1;
    CK(cc_grupo(b, hoy) == 1 && cc_grupo(d, hoy) == 2 && cc_grupo(a, hoy) == 3 && cc_grupo(c, hoy) == 5 && cc_grupo(e, hoy) == 0, "grupos %d %d %d %d %d",
       cc_grupo(b, hoy), cc_grupo(d, hoy), cc_grupo(a, hoy), cc_grupo(c, hoy), cc_grupo(e, hoy));
    uint32_t ids[10]; int n = cc_orden(&h, 0, ids, 10);
    CK(n == 5 && ids[0] == e->id && ids[1] == b->id && ids[2] == d->id && ids[3] == a->id && ids[4] == c->id, "orden %d: %u %u %u %u %u", n, ids[0], ids[1], ids[2], ids[3], ids[4]);
    n = cc_orden(&h, "CAFE", ids, 10);
    CK(n == 1 && ids[0] == b->id, "buscar sin tildes ni mayusculas (%d)", n);
    n = cc_orden(&h, "nandu", ids, 10); CK(n == 1, "buscar \303\261 como n");
    n = cc_orden(&h, "saltos", ids, 10); CK(n == 1 && ids[0] == a->id, "buscar en las respuestas");
    n = cc_orden(&h, "titulo", ids, 10); CK(n == 1 && ids[0] == c->id, "buscar en el titulo");
    CK(cc_coincide("Murci\303\251lago", 11, "CIELA") && !cc_coincide("abc", 3, "abd") && cc_coincide("x", 1, ""), "coincide");
    /* archivo: ida y vuelta */
    uint32_t need = cc_serializa(&h, 0, 0);
    char *buf = malloc(need + 1); uint32_t got = cc_serializa(&h, buf, need);
    CK(got == need, "tamano del archivo %u %u", got, need);
    struct cc_hist h2; cc_init(&h2);
    int nc = cc_carga(&h2, buf, got);
    CK(nc == 5 && h2.n == 5 && h2.sig_id == h.sig_id, "carga %d (%u)", nc, h2.sig_id);
    uint32_t need2 = cc_serializa(&h2, 0, 0); char *buf2 = malloc(need2 + 1); cc_serializa(&h2, buf2, need2);
    CK(need2 == need && !memcmp(buf, buf2, need), "ida y vuelta exacta");
    struct cc_conv *b2 = cc_por_id(&h2, b->id);
    CK(b2 && b2->modo == CC_MODO_AGENTE && b2->mem == 0 && b2->nm == 1 && !strcmp(b2->m[0].adj, "/usuario/foto.png\n/doc/a.txt"), "adjuntos y modo");
    struct cc_conv *a2 = cc_por_id(&h2, a->id);
    CK(a2 && a2->m[1].dur == 4321 && a2->m[1].n == strlen(R1) && !memcmp(a2->m[1].txt, R1, strlen(R1)) && a2->t1 == a->t1, "mensaje con saltos y duracion");
    CK(cc_carga(&h2, "otra cosa", 9) == -1, "rechaza lo que no es un historial");
    { char *rot = malloc(got); memcpy(rot, buf, got); struct cc_hist h3; cc_init(&h3);
      for (uint32_t cut = 0; cut < got; cut += 7) { cc_carga(&h3, rot, cut); cc_libera(&h3); }
      for (uint32_t k = 12; k < got; k += 13) { char sv = rot[k]; rot[k] = (char)(rot[k] ^ 0x5A); cc_carga(&h3, rot, got); cc_libera(&h3); rot[k] = sv; }
      free(rot); CK(1, "archivos cortados o estropeados no rompen nada"); }
    /* contexto */
    struct cc_conv *k = cc_nueva(&h, hoy, CC_MODO_LLM, 1);
    for (int i = 0; i < 20; i++) { char t[64]; int l = snprintf(t, sizeof t, "%s %d", i & 1 ? "respuesta" : "pregunta", i); cc_anade(k, i & 1 ? CC_ASIS : CC_USER, t, (uint32_t)l, 0, hoy); }
    uint32_t v = cc_ventana(k, 20, 120);
    CK(v > 0 && v < 20 && k->m[v].rol == CC_USER, "ventana empieza por una pregunta (%u)", v);
    CK(cc_ventana(k, 19, 1) == 18, "ventana minima: al menos la pregunta");
    char ctx[300]; uint32_t cl = cc_contexto(k, 19, ctx, sizeof ctx);
    CK(cl > 0 && cl < sizeof ctx && strstr(ctx, "Usuario: pregunta 18") && strstr(ctx, "SOMA: respuesta 17") && !strstr(ctx, "respuesta 19"), "contexto [%s]", ctx);
    cc_trunca(k, 4); CK(k->nm == 4, "truncar");
    CK(cc_borra(&h, cid) == 1 && cc_por_id(&h, cid) == 0 && h.n == 5 && cc_borra(&h, 999) == 0, "borrar");
    char f[24]; cc_hora(hoy + 65, f);
    cc_fecha(hoy, hoy, f, sizeof f); CK(!strncmp(f, "hoy, ", 5), "fecha hoy [%s]", f);
    cc_fecha(hoy - 1440, hoy, f, sizeof f); CK(!strncmp(f, "ayer, ", 6), "fecha ayer [%s]", f);
    cc_fecha(28512000u + 14 * 60 + 5, 28512000u + 30 * 1440, f, sizeof f);      /* 2024-03-18 14:05 */
    CK(!strcmp(f, "18 mar, 14:05"), "fecha del mismo ano [%s]", f);
    cc_fecha(28512000u, 28512000u + 400 * 1440, f, sizeof f); CK(!strcmp(f, "18 mar 2024"), "fecha de otro ano [%s]", f);
    cc_hora(125, f); CK(!strcmp(f, "2:05"), "hora [%s]", f);
    free(buf); free(buf2);
    cc_libera(&h); cc_libera(&h2);
    CK(g_alloc == 0, "sin fugas en el historial (%ld)", g_alloc);
}

static void prueba_proto(void)
{
    static struct llm_conf c; static struct llm_http h; static char body[65536];
    llm_conf_defaults(&c);
    struct llm_msg m[6] = { { LLM_ROL_ASSISTANT, "hola, soy SOMA" }, { LLM_ROL_USER, "uno" }, { LLM_ROL_ASSISTANT, "dos \"q\"" }, { LLM_ROL_USER, "" },
                            { LLM_ROL_USER, "tres\nlinea" }, { LLM_ROL_USER, "cuatro" } };
    static const uint8_t px[3] = { 1, 2, 3 }; struct llm_img im = { px, 3, "image/png" };
    int n = llm_proto_chat_hist(&c, "Sistema", m, 6, 0, 0, 0, body, sizeof body, &h);
    CK(n > 0 && strstr(body, "\"messages\":[{\"role\":\"system\",\"content\":\"Sistema\"},{\"role\":\"assistant\",\"content\":\"hola, soy SOMA\"},{\"role\":\"user\",\"content\":\"uno\"},"
                         "{\"role\":\"assistant\",\"content\":\"dos \\\"q\\\"\"},{\"role\":\"user\",\"content\":\"tres\\nlinea\\n\\ncuatro\"}]"), "openai/local: %s", body);
    n = llm_proto_chat_hist(&c, "S", m + 1, 5, &im, 1, 0, body, sizeof body, &h);
    CK(n > 0 && strstr(body, "{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"tres\\nlinea\\n\\ncuatro\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,AQID\"}}]}]")
       && strstr(body, "{\"role\":\"user\",\"content\":\"uno\"}"), "imagen con la ultima pregunta: %s", body);
    llm_conf_set(&c, LLM_CLAUDE, "clave", "sk-ant-x"); c.prov = LLM_CLAUDE;
    n = llm_proto_chat_hist(&c, "S", m + 1, 5, &im, 1, 100, body, sizeof body, &h);
    CK(n > 0 && strstr(body, "\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"uno\"}]},{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"dos \\\"q\\\"\"}]},"
                         "{\"role\":\"user\",\"content\":[{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/png\",\"data\":\"AQID\"}},{\"type\":\"text\",\"text\":\"tres\\nlinea\\n\\ncuatro\"}]}]}"),
       "claude: %s", body);
    llm_conf_set(&c, LLM_GEMINI, "clave", "AIzaSyXX"); llm_conf_set(&c, LLM_GEMINI, "modelo", "gemini-2.5-flash"); c.prov = LLM_GEMINI;
    n = llm_proto_chat_hist(&c, "S", m + 1, 5, 0, 0, 100, body, sizeof body, &h);
    CK(n > 0 && strstr(body, "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"uno\"}]},{\"role\":\"model\",\"parts\":[{\"text\":\"dos \\\"q\\\"\"}]},{\"role\":\"user\",\"parts\":[{\"text\":\"tres\\nlinea\\n\\ncuatro\"}]}],\"generationConfig\":{\"maxOutputTokens\":100}}"),
       "gemini: %s", body);
    /* una sola pregunta: igual que llm_proto_chat_ex */
    static char b2[65536]; static struct llm_http h2;
    for (int p = 0; p < LLM_NPROV; p++) {
        c.prov = p; llm_conf_set(&c, p, "clave", "k1");
        struct llm_msg one = { LLM_ROL_USER, "pregunta \"x\"" };
        int a1 = llm_proto_chat_ex(&c, "sis", "pregunta \"x\"", &im, 1, 0, body, sizeof body, &h);
        int a2 = llm_proto_chat_hist(&c, "sis", &one, 1, &im, 1, 0, b2, sizeof b2, &h2);
        CK(a1 == a2 && !strcmp(body, b2) && !strcmp(h.url, h2.url), "una pregunta igual (%d)", p);
    }
    c.prov = LLM_LOCAL;
    n = llm_proto_chat_hist(&c, 0, 0, 0, 0, 0, 0, body, sizeof body, &h);
    CK(n > 0 && strstr(body, "{\"role\":\"user\",\"content\":\"\"}"), "sin mensajes: una pregunta vacia");
    n = llm_proto_chat_hist(&c, "S", m, 6, 0, 0, 0, body, 60, &h);
    CK(n == LLM_P_TOOBIG, "no cabe");
    CK(llm_proto_chat_hist_size("S", m, 6, &im, 1) > 4096 + 2 * 40, "tamano");
}

static void prueba_md(void)
{
    scr = malloc(MD_SCRATCH);
    const char *doc =
        "# Titulo principal\n\nUn parrafo con **negrita**, *cursiva*, ***las dos***, ~~tachado~~, `codigo` y un [enlace](https://soma.dev/a?b=1) y <https://ejemplo.org>.\n"
        "Una URL suelta https://github.com/soma/soma-os, y fin.  \nsalto duro\n\n"
        "## Lista\n\n- uno\n- dos con `x`\n  - anidado\n  - otro\n- [x] hecho\n- [ ] pendiente\n\n1. primero\n2. segundo\n10. decimo\n\n"
        "> una cita\n> de dos lineas\n\n> [!WARNING]\n> cuidado\n\n"
        "| Col A | Col B | Num |\n|:--|:-:|--:|\n| a | **b** | 1 |\n| c largo largo | d | 22 |\n\n"
        "```python\ndef f(x):\n    # comentario\n    return \"hola\" + 3\n```\n\n"
        "La formula $E = mc^2$ en linea y otra \\(\\alpha + \\beta\\).\n\n$$\\int_0^1 x\\,dx = \\frac{1}{2}$$\n\n"
        "\\[ \\sum_{i=1}^{n} i = \\frac{n(n+1)}{2} \\]\n\n---\n\n![grafica](/img/a.png)\n\n![remota](https://x.org/a.png)\n\n"
        "Texto <b>html</b> &amp; <i>cursiva</i> H<sub>2</sub>O x<sup>2</sup> &#233; &rarr; <br> <a href=\"https://w.org\">web</a> <span>quitada</span>\n\n"
        "Cuesta $5 y $10 no es formula. 2*3*4 no es cursiva. snake_case_name tampoco.\n";
    int h = lay(doc, 640);
    printf("md: alto %d ops %d pool %u zonas %d bloques %d formulas %d codigos %d tablas %d imagenes %d enlaces %d lleno %d\n", h, o.nops, o.npool, o.nz,
           o.blocks, o.maths, o.codes, o.tables, o.images, o.links, o.full);
    CK(h > 900 && !o.full, "alto %d", h);
    { int i = has("Titulo"); CK(i >= 0 && (ops[i].fl & MD_F_BOLD) && ops[i].px > 20, "titulo grande y en negrita"); }
    { int i = has("negrita"); CK(i >= 0 && (ops[i].fl & MD_F_BOLD) && !(ops[i].fl & MD_F_ITALIC), "negrita"); }
    { int i = has("cursiva"); CK(i >= 0 && (ops[i].fl & MD_F_ITALIC), "cursiva"); }
    { int i = has("las"); CK(i >= 0 && (ops[i].fl & MD_F_ITALIC) && (ops[i].fl & MD_F_BOLD), "negrita y cursiva"); }
    { int i = has("codigo"); CK(i >= 0 && (ops[i].fl & MD_F_MONO) && ops[i].rgb == T.inl_fg, "codigo en linea"); }
    CK(zona(MDZ_LINK, "https://soma.dev/a?b=1") >= 0 && zona(MDZ_LINK, "https://ejemplo.org") >= 0 && zona(MDZ_LINK, "https://github.com/soma/soma-os") >= 0 && zona(MDZ_LINK, "https://w.org") >= 0,
       "enlaces: [] () <> sueltos y <a href>");
    { int i = has("enlace"); CK(i >= 0 && ops[i].zone >= 0 && ops[i].rgb == T.link, "el texto del enlace lleva su zona y su color"); }
    CK(o.tables == 1 && has("Col") >= 0 && has("22") >= 0, "tabla");
    { int a = has("1"), b = has("22"), hd = has("Num"); CK(a >= 0 && b >= 0 && hd >= 0 && ops[b].x < ops[a].x && (ops[hd].fl & MD_F_BOLD), "columna alineada a la derecha y cabecera en negrita"); }
    CK(o.codes == 1 && zona(MDZ_COPY, "def f(x):\n    # comentario\n    return \"hola\" + 3") >= 0 && has("Copiar") >= 0 && has("python") >= 0, "bloque de codigo con copiar y lenguaje");
    { int i = has("def"), j = has("# comentario"), k = has("\"hola\""), q = has("3"); CK(i >= 0 && ops[i].rgb == T.kw && j >= 0 && ops[j].rgb == T.com && k >= 0 && ops[k].rgb == T.str && q >= 0 && ops[q].rgb == T.num, "resaltado"); }
    CK(o.maths == 4, "formulas: 2 en linea y 2 en bloque (%d)", o.maths);
    CK(has("\342\210\253") >= 0 && has("\316\261") >= 0 && has("\342\210\221") >= 0, "integral, alfa y sumatorio");
    CK(o.images == 2 && zona(MDZ_IMG, "/img/a.png") >= 0 && zona(MDZ_IMG, "https://x.org/a.png") >= 0, "imagenes");
    { int ok = 0; for (int i = 0; i < o.nops; i++) if (ops[i].type == MD_IMG && ops[i].w == 640 && ops[i].h == 426) ok = 1; CK(ok, "imagen grande ajustada al ancho"); }
    CK(has("html") >= 0 && (ops[has("html")].fl & MD_F_BOLD) && has("&") >= 0 && has("\303\251") >= 0 && has("\342\206\222") >= 0 && has("quitada") >= 0 && has("<span>quitada</span>") < 0,
       "html en linea y entidades");
    CK(has("$5") >= 0 && has("2*3*4") >= 0 && has("snake_case_name") >= 0, "falsos positivos de $ * _");
    CK(has("hecho") >= 0 && has("primero") >= 0 && has("3.") >= 0 && has("anidado") >= 0, "listas");
    { int a = has("anidado"), b = has("uno"); CK(a >= 0 && b >= 0 && ops[a].x > ops[b].x, "lista anidada con sangria"); }
    { int a = has("cuidado"), w = has("Atenci\303\263n"); CK(a >= 0 && w >= 0, "alerta de GitHub"); }
    CK(max_right() <= 640, "nada se sale del ancho (%d)", max_right());
    /* anchos estrechos: todo dentro */
    for (int w = 120; w <= 900; w += 47) { lay(doc, w); int r = max_right(); CK(r <= w + 2 || o.maths, "ancho %d: %d", w, r); }
    lay("palabramuylargasinespaciosquenocabeenningunalineadeesteanchotannnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn", 100);
    CK(max_right() <= 100 && o.nops >= 3, "palabra larga partida");
    lay("| a | b |\n|---|---|\n| unapalabralarguisimasinespacioalguno | x |\n", 160);
    CK(max_right() <= 162, "celda con palabra larga (%d)", max_right());
    lay("```\nuna linea de codigo muy larga que tiene que partirse porque no cabe en el ancho de la columna del chat\n```", 300);
    CK(max_right() <= 300, "codigo largo partido (%d)", max_right());
    /* resaltado suelto */
    { uint8_t cl[64]; const char *s = "int main(void) { return 0; } /* c"; int st = md_resalta("c", 1, s, (int)strlen(s), cl, 0);
      CK(cl[0] == MDK_TYP && cl[4] == MDK_FN && cl[17] == MDK_KW && cl[24] == MDK_NUM && cl[29] == MDK_COM && st == 1, "resaltado C");
      st = md_resalta("c", 1, "fin */ x", 8, cl, st); CK(st == 0 && cl[0] == MDK_COM && cl[7] == MDK_TXT, "comentario de bloque en dos lineas");
      md_resalta("bash", 4, "echo $HOME # x", 14, cl, 0); CK(cl[0] == MDK_KW && cl[12] == MDK_COM && cl[5] == MDK_TXT, "resaltado bash");
      md_resalta("html", 4, "<a href=\"x\">t</a>", 17, cl, 0); CK(cl[0] == MDK_KW && cl[3] == MDK_FN && cl[8] == MDK_STR && cl[12] == MDK_TXT, "resaltado html"); }
    /* limites: pocas operaciones */
    { o.ops = ops; o.cap = 50; o.pool = pool; o.poolcap = sizeof pool; o.z = zs; o.zcap = 2000;
      int hh = md_layout(doc, (uint32_t)strlen(doc), 600, 15, &M, &T, &o, scr); CK(o.full && o.nops <= 50 && hh > 0, "sin sitio: para sin salirse"); }
    { o.ops = ops; o.cap = 30000; o.pool = pool; o.poolcap = 300; o.z = zs; o.zcap = 3;
      md_layout(doc, (uint32_t)strlen(doc), 600, 15, &M, &T, &o, scr); CK(o.full && o.npool <= 300 && o.nz <= 3, "pool y zonas pequenos"); }
    /* al azar */
    { static const char *const P[] = { "**", "*", "_", "`", "```", "$", "$$", "\\[", "\\]", "|", "---", "\n", "\n\n", "- ", "1. ", "> ", "#", "[", "](", ")", "![", "<b>", "</b>",
                                      "<a href=\"", "&amp;", "&#", "\\", "~~", "  \n", "\t", "x", "palabra ", "https://a.b/c ", "\\frac{", "}", "{", "^", "_{", "\303\241", "\342\210\253", "[!NOTE]", "| --- |", "<" };
      unsigned seed = 12345; char *t = malloc(4096);
      for (int it = 0; it < 3000; it++) {
          int k = 0, parts = 1 + (int)((seed >> 8) % 60);
          for (int q = 0; q < parts; q++) { seed = seed * 1103515245u + 12345u; const char *p = P[(seed >> 10) % (sizeof P / sizeof P[0])]; while (*p && k < 4000) t[k++] = *p++; }
          t[k] = 0;
          seed = seed * 1103515245u + 12345u;
          int w = 60 + (int)((seed >> 12) % 800);
          lay(t, w);
      }
      free(t); CK(1, "3000 textos al azar sin romper nada (ASan)"); }
    free(scr);
}

int main(void)
{
    prueba_core();
    prueba_proto();
    prueba_md();
    printf("test_chat_285: %d comprobaciones, %d fallos\n", checks, fails);
    if (fails) { printf("HAY FALLOS\n"); return 1; }
    printf("TODO BIEN\n");
    return 0;
}
