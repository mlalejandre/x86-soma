/* SOMA_321_CHAT_MD_HOSTIL: el maquetador de Markdown del chat (src/apps/chat_md.c, con el LaTeX de tex.c) con lo que pueda escribir
 * un modelo manipulado (instrucciones escondidas en una pagina que lee el agente): documentos GENERADOS con estructura (tablas que
 * no cuadran, listas y citas muy anidadas, codigo sin cerrar, LaTeX, entidades enormes, HTML, enlaces e imagenes rotos) y
 * respuestas reales mutadas y cortadas. Cada texto en un bufer de su tamano exacto (sin NUL detras: md_layout recibe la longitud),
 * columnas de 1 a 900 px y capacidades pequenas para forzar los caminos de "no cabe". ASan y UBSan vigilan; ademas, ninguna
 * operacion apunta fuera del pool ni a una zona que no existe, y nada pasa de su capacidad. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "chat_md.h"

void *cc_alloc(uint32_t n) { return malloc(n ? n : 1); }        /* las da quien enlaza (chat_core.h) */
void  cc_free(void *p) { free(p); }

static uint64_t semilla = 321;
static uint32_t rnd(void) { semilla = semilla * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(semilla >> 33); }

static int fw(const char *s, int n, int px, int fl, void *u)
{
    (void)u; int ch = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) ch++;
    return ch * px * ((fl & MD_F_MONO) ? 60 : (fl & MD_F_BOLD) ? 58 : 55) / 100;
}
static int fa(int px, void *u) { (void)u; return px * 8 / 10; }
static int fd(int px, void *u) { (void)u; return px * 2 / 10 + 1; }
static int fimg(const char *s, int n, int *w, int *h, void *u) { (void)u; if (n > 4 && !memcmp(s, "/img", 4)) { *w = 30000; *h = 30000; return 0; } if (n > 4 && !memcmp(s, "http", 4)) return 1; return -1; }
static const struct md_metrics M = { fw, fa, fd, fimg, 0 };
static const struct md_theme T = { 0xE6E6E6, 0x8B949E, 0xFFFFFF, 0x60A5FA, 0x2A3150, 0x0D1117, 0x161B22, 0x30363D, 0xE6EDF3, 0x1E2433, 0xF0ABFC, 0x6366F1,
                                   0x34D399, 0x1B2036, 0x141A2E, 0x6366F1, 0xFF7B72, 0xA5D6FF, 0x79C0FF, 0x8B949E, 0xD2A8FF, 0xFFA657, 0xEF4444 };

static const char *const TROZOS[] = {
    "# ", "###### ", "\n", "\n\n", "  \n", "\\\n", "<br>", "> ", "> > > > > ", "- ", "  - ", "      - ", "1. ", "99999999999) ", "- [ ] ", "- [x] ",
    "```", "```c\n", "~~~python\n", "    codigo", "`", "``", "**", "*", "***", "_", "__", "~~", "==", "\\*", "\\",
    "| a | b |\n|---|:-:|\n| 1 |", "| --- |", "|", ":--:", "|||||||||||", "---\n", "***\n",
    "[", "]", "(", ")", "](", "[enlace](https://a.b/c)", "[x](", "![", "![alt](/img/a.png)", "![x](http://y/z.png)", "<https://u.v>", "https://w.x/y?z ",
    "$", "$$", "\\[", "\\]", "\\(", "\\)", "\\frac{", "\\sqrt[", "}", "{", "^", "_{", "\\begin{align}", "\\end{align}", "\\begin{pmatrix}", "&", "\\\\",
    "&amp;", "&#", "&#x", "&#4294967295;", "&#x110000;", "&#0;", "&lt;", "&nbsp", "&unknown;",
    "<b>", "</b>", "<a href=\"", "\">", "</a>", "<code>", "<kbd>", "<sup>", "<sub>", "<mark>", "<script>", "<!--", "-->", "<", ">",
    "\t", " ", "x", "palabra ", "\303\241", "\342\210\253", "\360\237\230\200", "\377", "\300\200", "[!NOTE]",
};

static struct md_out o;

static int lay(const char *s, uint32_t n, void *scr, int *malos)
{
    int cap = 1 + (int)(rnd() % 4000), pcap = 1 + (int)(rnd() % 65536), zcap = (int)(rnd() % 300);
    /* cada cosa de su tamano exacto (como en chat.c, que reserva el pool con kmalloc): ASan ve cualquier lectura fuera */
    struct md_op *ops = (struct md_op *)malloc((size_t)cap * sizeof *ops);
    char *pool = (char *)malloc((size_t)pcap);
    struct md_zone *zs = (struct md_zone *)malloc((size_t)(zcap ? zcap : 1) * sizeof *zs);
    o.ops = ops; o.cap = cap; o.pool = pool; o.poolcap = (uint32_t)pcap; o.z = zs; o.zcap = zcap;
    int w = (rnd() % 4 == 0) ? 1 + (int)(rnd() % 60) : 60 + (int)(rnd() % 840);
    int h = md_layout(s, n, w, 8 + (int)(rnd() % 30), &M, &T, &o, scr);
    if (o.nops > cap || o.npool > (uint32_t)pcap || o.nz > zcap || o.nops < 0 || h < 0) { (*malos)++; if (getenv("VER")) printf("A cap=%d nops=%d pcap=%d npool=%u zcap=%d nz=%d h=%d\n", cap, o.nops, pcap, o.npool, zcap, o.nz, h); }
    for (int i = 0; i < o.nops && i < cap; i++) {
        if ((ops[i].type == MD_TEXT || ops[i].type == MD_IMG) && (ops[i].s > o.npool || ops[i].n > o.npool - ops[i].s)) { (*malos)++; if (getenv("VER")) printf("B op %d tipo %d s=%u n=%u npool=%u\n", i, ops[i].type, ops[i].s, ops[i].n, o.npool); break; }
        if (ops[i].zone >= o.nz) { (*malos)++; if (getenv("VER")) printf("C op %d zona %d nz=%d zcap=%d\n", i, ops[i].zone, o.nz, zcap); break; }
    }
    for (int i = 0; i < o.nz && i < zcap; i++) if (zs[i].s > o.npool || zs[i].n > o.npool - zs[i].s) { (*malos)++; if (getenv("VER")) printf("D zona %d\n", i); break; }
    free(ops); free(pool); free(zs);
    return h;
}

int main(int argc, char **argv)
{
    int iter = argc > 1 ? atoi(argv[1]) : 60000;
    void *scr = malloc(MD_SCRATCH);
    char *t = (char *)malloc(16384);
    int malos = 0;
    for (int it = 0; it < iter; ++it) {
        uint32_t k = 0, partes = 1 + rnd() % 200;
        for (uint32_t q = 0; q < partes; ++q) {
            const char *p = TROZOS[rnd() % (sizeof TROZOS / sizeof TROZOS[0])];
            uint32_t rep = (rnd() % 16 == 0) ? 1 + rnd() % 40 : 1;          /* a veces, muchas veces seguidas (anidamiento) */
            for (uint32_t r = 0; r < rep; ++r) for (const char *c = p; *c && k < 16000; ++c) t[k++] = *c;
        }
        if (k && rnd() % 6 == 0) t[rnd() % k] = (char)rnd();
        char *e = (char *)malloc(k ? k : 1);                               /* tamano exacto, sin NUL */
        memcpy(e, t, k);
        lay(e, k, scr, &malos);
        free(e);
    }
    free(t); free(scr);
    CHECK_EQ_INT(malos, 0);
    printf("[chat_md_hostil_321] %d documentos generados\n", iter);
    TEST_MAIN_END("chat_md_hostil_321");
}
