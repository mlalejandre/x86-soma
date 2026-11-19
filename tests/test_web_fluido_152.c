/* SOMA 152: navegador fluido. Pruebas en el anfitrion (ASan/UBSan):
 *  1. anillo de la instantanea: girar y leer equivale a desplazar una copia lineal;
 *  2. desplazamiento suave: llega exacto, nunca se pasa, siempre avanza;
 *  3. invalidacion selectiva por :hover (wc_hover_affects);
 *  4. filtro de Bloom de clases: mismos resultados que sin filtro, tambien tras cambiar class entre consultas;
 *  5. variables CSS internadas y cache de selectores: el arena del documento no crece al recalcular/consultar,
 *     y los valores calculados no cambian. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_css.h"
#include "web_fluido_152.h"

uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }

static unsigned long checks;
#define CK(x) do { if (!(x)) { fprintf(stderr, "SOMA152 FALLO %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } checks++; } while (0)

static uint32_t seed = 0x152u;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void test_ring(void)
{
    enum { H = 37, W = 3 };
    int lin[H], ring[H], r0 = 0, top = 0;               /* la "pagina" es la secuencia de enteros: fila y = valor y */
    for (int i = 0; i < H; i++) { lin[i] = i; ring[i] = i; }
    for (int it = 0; it < 20000; it++) {
        int d = (int)(rnd() % (2 * H - 1)) - (H - 1);    /* |d| < H, como exige el plan 149 */
        top += d;
        r0 = soma152_ring_rotate(r0, d, H);
        /* filas nuevas (franja danada): se "pintan" en el anillo */
        if (d > 0) for (int i = H - d; i < H; i++) ring[soma152_ring_row(r0, i, H)] = top + i;
        else for (int i = 0; i < -d; i++) ring[soma152_ring_row(r0, i, H)] = top + i;
        for (int i = 0; i < H; i++) lin[i] = top + i;
        for (int i = 0; i < H; i++) CK(ring[soma152_ring_row(r0, i, H)] == lin[i]);
        CK(r0 >= 0 && r0 < H);
    }
    CK(soma152_ring_rotate(5, -6, 10) == 9);
    CK(soma152_ring_rotate(0, 0, 0) == 0);
    (void)W;
}

static void test_smooth(void)
{
    for (int it = 0; it < 5000; it++) {
        int sy = (int)(rnd() % 5000), tgt = (int)(rnd() % 5000), frames = 0;
        while (sy != tgt) {
            int d = tgt - sy; uint32_t dt = 1 + rnd() % 80;
            int st = soma152_smooth_step(d, dt);
            CK(st != 0);
            CK((d > 0 && st > 0 && st <= d) || (d < 0 && st < 0 && st >= d));   /* nunca se pasa */
            sy += st; frames++;
            CK(frames < 10000);
        }
    }
    CK(soma152_smooth_step(0, 16) == 0);
    CK(soma152_smooth_step(100, 0) == 0);
    CK(soma152_clamp_target(10, -50, 100) == 0);
    CK(soma152_clamp_target(90, 50, 100) == 100);
    CK(soma152_clamp_target(90, 50, -5) == 0);
}

static struct wdoc *doc_of(const char *html)
{
    struct wdoc *d = wd_new(); CK(d != 0);
    wd_parse(d, html, (uint32_t)strlen(html));
    return d;
}

static void test_hover(void)
{
    struct wdoc *d = doc_of("<!doctype html><html><body>"
        "<div id='card' class='card'><p id='inner'>uno</p></div>"
        "<div id='plain'><span id='sp'>dos</span></div>"
        "<a id='lnk' href='#'>tres</a>"
        "</body></html>");
    struct wnode *card = wd_by_id(d->root, "card"), *inner = wd_by_id(d->root, "inner");
    struct wnode *plain = wd_by_id(d->root, "plain"), *sp = wd_by_id(d->root, "sp"), *lnk = wd_by_id(d->root, "lnk");
    CK(card && inner && plain && sp && lnk);
    struct wcss *c = wc_new(d); CK(c != 0);
    const char *sheet = ".card:hover{color:red} a:hover{color:blue}";
    wc_add_sheet(c, sheet, (uint32_t)strlen(sheet), 1);
    wc_compute(c, 800, 600);
    CK(wc_hover_affects(c, sp, plain) == 0);       /* dentro de un bloque sin reglas :hover */
    CK(wc_hover_affects(c, 0, sp) == 0);
    CK(wc_hover_affects(c, sp, inner) == 1);       /* entra en .card (ancestro de inner) */
    CK(wc_hover_affects(c, inner, card) == 0);     /* card ya estaba en hover por su hijo y lo sigue estando */
    CK(wc_hover_affects(c, card, sp) == 1);        /* sale de .card */
    CK(wc_hover_affects(c, sp, lnk) == 1);         /* a:hover */
    CK(wc_hover_affects(c, lnk, lnk) == 0);
    {   /* SOMA_204: una hoja con :has() no puede tumbar wc_hover_affects (leia una lista nula), y :hover dentro de :has() cuenta */
        struct wcss *c2 = wc_new(d); CK(c2 != 0);
        const char *sh2 = "div:has(p){color:red} div:has(> span) a{color:blue}";
        wc_add_sheet(c2, sh2, (uint32_t)strlen(sh2), 1); wc_compute(c2, 800, 600);
        CK(wc_hover_affects(c2, sp, inner) == 0);
        const char *sh3 = "div:has(p:hover){color:green}";
        wc_add_sheet(c2, sh3, (uint32_t)strlen(sh3), 1); wc_compute(c2, 800, 600);
        CK(wc_hover_affects(c2, sp, inner) == 1);
        wc_free(c2);
    }
    wc_free(c);
    /* :hover anidado en :not()/:is(): respuesta conservadora */
    c = wc_new(d); CK(c != 0);
    const char *s2 = "div:not(:hover){color:red}";
    wc_add_sheet(c, s2, (uint32_t)strlen(s2), 1);
    wc_compute(c, 800, 600);
    CK(wc_hover_affects(c, sp, plain) == 1);
    wc_free(c);
    wd_free(d);
}

/* referencia sin filtro: comprobar clases leyendo el atributo */
static int has_cls_ref(struct wnode *n, const char *cl)
{
    const char *v = wd_attr(n, "class"); size_t l = strlen(cl);
    while (v && *v) {
        while (*v == ' ') v++;
        const char *s = v; while (*v && *v != ' ') v++;
        if ((size_t)(v - s) == l && !strncmp(s, cl, l)) return 1;
    }
    return 0;
}

static void test_bloom(void)
{
    static const char *names[] = { "a", "b", "card", "btn", "btn-primary", "x1", "x2", "nav-link", "active", "row" };
    char html[20000]; int o = 0;
    o += snprintf(html + o, sizeof html - o, "<!doctype html><html><body>");
    for (int i = 0; i < 120; i++) {
        o += snprintf(html + o, sizeof html - o, "<div id='n%d' class='", i);
        for (int k = 0; k < 10; k++) if (rnd() % 3 == 0) o += snprintf(html + o, sizeof html - o, "%s ", names[k]);
        o += snprintf(html + o, sizeof html - o, "'>t</div>");
    }
    o += snprintf(html + o, sizeof html - o, "</body></html>");
    struct wdoc *d = doc_of(html);
    struct wcss *c = wc_new(d); CK(c != 0);
    for (int round = 0; round < 6; round++) {
        for (int i = 0; i < 120; i++) {
            char id[16]; snprintf(id, sizeof id, "n%d", i);
            struct wnode *n = wd_by_id(d->root, id); CK(n != 0);
            for (int a = 0; a < 10; a++) for (int b = 0; b < 10; b++) {
                char sel[64]; snprintf(sel, sizeof sel, ".%s.%s", names[a], names[b]);
                int ref = has_cls_ref(n, names[a]) && has_cls_ref(n, names[b]);
                CK(wc_matches(c, n, sel) == ref);
            }
        }
        /* JS cambia class entre consultas: el filtro se rehace (nueva generacion) */
        for (int i = 0; i < 120; i += 3) {
            char id[16], cls[64]; snprintf(id, sizeof id, "n%d", i);
            snprintf(cls, sizeof cls, "%s %s", names[rnd() % 10], names[rnd() % 10]);
            wd_set_attr(d, wd_by_id(d->root, id), "class", cls);
        }
    }
    wc_free(c);
    wd_free(d);
}

static void test_no_arena_growth(void)
{
    struct wdoc *d = doc_of("<!doctype html><html><body>"
        "<div class='card'><p class='t'>uno</p><a class='btn'>x</a></div>"
        "<div class='card'><p class='t'>dos</p><a class='btn'>y</a></div>"
        "<div class='card' style='--accent:#00ff00'><p class='t'>tres</p><a class='btn'>z</a></div>"
        "</body></html>");
    struct wcss *c = wc_new(d); CK(c != 0);
    const char *sheet =
        ":root{--fg:#112233;--pad:4px;--accent:#ff0000}"
        ".card{--card-bg:#fafafa;background-color:var(--card-bg);padding:var(--pad)}"
        ".t{color:var(--fg)} .btn{--b:var(--accent);color:var(--b)}";
    wc_add_sheet(c, sheet, (uint32_t)strlen(sheet), 1);
    wc_compute(c, 800, 600);
    struct wnode *out[16];
    int nb = wc_query(c, d->root, ".card .btn", out, 16, 0);
    CK(nb == 3);
    CK((out[0]->st->color & 0xFFFFFFu) == 0xFF0000u);
    CK((out[2]->st->color & 0xFFFFFFu) == 0x00FF00u);           /* --accent en linea, heredado por .btn */
    uint32_t a0 = d->ar.total;
    for (int i = 0; i < 200; i++) {
        wc_compute(c, 800, 600);
        CK(wc_query(c, d->root, ".card .btn", out, 16, 0) == 3);
        CK(wc_query(c, d->root, "div > p.t", out, 16, 0) == 3);
        CK(wc_matches(c, out[0], ".card > .t"));
    }
    CK(d->ar.total == a0);                                       /* antes: crecia en cada recalculo y cada consulta */
    CK((out[0]->st->color & 0xFFFFFFu) == 0x112233u);
    wc_free(c);
    wd_free(d);
}

int main(void)
{
    test_ring();
    test_smooth();
    test_hover();
    test_bloom();
    test_no_arena_growth();
    printf("SOMA 152: anillo, desplazamiento suave, :hover selectivo, Bloom de clases y arena estable OK (%lu comprobaciones)\n", checks);
    return 0;
}
