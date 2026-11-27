/* SOMA_354_CLIC_MODAL: el clic va a lo que se ve encima. Los position:fixed se pintan los ultimos (encima de absolutos y flujo),
 * ordenados por z-index y, a igual z-index, por orden del documento; el clic tiene que seguir el mismo orden al reves.
 * Caso real: el aviso de cookies de Google ("Antes de ir a Google"): ningun boton respondia porque el clic se lo quedaba lo de
 * debajo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_dom.h"
#include "web_css.h"
#include "web_layout.h"
#include "web_ua.h"
#include "web_util.h"
uint64_t web_now_ms(void) { return 0; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1 << 20; }
void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static int fails, checks;
static struct wlay *maquetar(const char *html, int sy)
{
    struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
    struct wcss *c = wc_new(d); wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_compute(c, 1000, 700);
    struct wlay *L = wl_new(d, c, 0, 0); L->ar.limit = 14u << 20; wl_layout(L, 1000, 700); L->scroll_y = sy;
    return L;
}
/* el elemento con id (o su antepasado mas cercano con id) que recibe el clic en (x, y) */
static void clic(const char *nombre, struct wlay *L, int x, int y, const char *esperado)
{
    struct wnode *n = wl_hit(L, x, y + L->scroll_y, 0, 0);
    const char *id = 0; for (struct wnode *e = n; e && !id; e = e->parent) if (e->type == WN_ELEM) { const char *v = wd_attr(e, "id"); if (v && *v) id = v; }
    checks++;
    if (!id || strcmp(id, esperado)) { fails++; printf("FALLO %s: el clic en (%d,%d) va a '%s' (esperado '%s')\n", nombre, x, y, id ? id : "(nada)", esperado); }
}
int main(void)
{
    /* como Google: contenido absoluto debajo, una capa gris fija a pantalla completa y el dialogo fijo por encima */
    const char *g = "<!doctype html><body style='margin:0'>"
        "<div id='cabecera' style='position:absolute;top:0;left:0;width:1000px;height:600px;background:#eee'><a id='enlace' href='/x' style='display:block;height:600px'>pagina</a></div>"
        "<div id='capa' style='position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:990'></div>"
        "<div id='dialogo' style='position:fixed;top:100px;left:300px;width:400px;height:400px;background:#fff;z-index:991'>"
        "<button id='rechazar' style='margin:300px 0 0 20px;width:150px;height:40px'>Rechazar todo</button>"
        "<button id='aceptar' style='margin:0 0 0 20px;width:150px;height:40px'>Aceptar todo</button></div>"
        "<div id='flujo' style='height:2000px'>texto</div></body>";
    struct wlay *L = maquetar(g, 0);
    clic("boton del dialogo sobre contenido absoluto", L, 340, 418, "rechazar");
    clic("otro boton", L, 530, 418, "aceptar");
    clic("fondo del dialogo", L, 500, 200, "dialogo");
    clic("fuera del dialogo: la capa gris", L, 100, 300, "capa");
    clic("debajo de la capa no se puede pulsar", L, 100, 50, "capa");
    L = maquetar(g, 500);
    clic("con la pagina desplazada, el dialogo fijo sigue en su sitio", L, 340, 418, "rechazar");
    /* dos fijos: el de z-index alto va antes en el documento */
    const char *z = "<!doctype html><body style='margin:0'>"
        "<div id='alto' style='position:fixed;top:50px;left:50px;width:200px;height:200px;background:#fff;z-index:10'></div>"
        "<div id='bajo' style='position:fixed;top:0;left:0;width:500px;height:500px;background:#000;z-index:1'></div></body>";
    L = maquetar(z, 0);
    clic("z-index alto antes en el documento", L, 100, 100, "alto");
    clic("solo el de z-index bajo", L, 400, 400, "bajo");
    /* mismo z-index: gana el ultimo del documento, como al pintar */
    const char *e = "<!doctype html><body style='margin:0'>"
        "<div id='primero' style='position:fixed;top:0;left:0;width:300px;height:300px;background:#f00'></div>"
        "<div id='segundo' style='position:fixed;top:100px;left:100px;width:300px;height:300px;background:#0f0'></div></body>";
    L = maquetar(e, 0);
    clic("mismo z-index: el ultimo encima", L, 150, 150, "segundo");
    clic("mismo z-index: solo el primero", L, 50, 50, "primero");
    /* sin fijos, todo como antes: el absoluto por encima del flujo */
    const char *a = "<!doctype html><body style='margin:0'><div id='flujo2' style='height:500px;background:#ccc'>x</div>"
        "<div id='abs' style='position:absolute;top:10px;left:10px;width:100px;height:100px;background:#00f'></div></body>";
    L = maquetar(a, 0);
    clic("absoluto sobre el flujo", L, 50, 50, "abs");
    clic("flujo", L, 300, 300, "flujo2");
    printf("test_web_clic_354: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
