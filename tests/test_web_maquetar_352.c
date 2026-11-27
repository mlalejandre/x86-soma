/* SOMA_352_MENOS_MAQUETAR: el JavaScript solo pide recalcular estilos y maquetar (host->dirty) cuando algo visible cambia.
 * Escribir el mismo valor que ya habia (un carrusel que repite style.transition, un reloj con el mismo texto) o tocar un nodo
 * que no esta en el documento (new Image().src de un contador, una plantilla con innerHTML) no maqueta. Lo que si cambia algo
 * sigue avisando, y los MutationObserver siguen recibiendo sus registros. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
uint64_t web_now_ms(void) { return 10000; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
static int errors, fails, checks, dirties;
static void jlog(void *ud, int level, const char *msg) { (void)ud; fprintf(stderr, "JS [%d] %s\n", level, msg); if (level >= 2) errors++; }
static void dirty(void *ud, int val) { (void)ud; if (val == 1) dirties++; }
/* maqueta = 1: el codigo debe pedir maquetar (al menos una vez); 0: no debe pedirlo */
static void caso(struct wjs *j, const char *nombre, const char *src, int maqueta)
{
    char out[256]; int d0 = dirties; int rc = wjs_eval_str(j, src, out, sizeof out); int pidio = dirties > d0;
    checks++;
    if (rc || pidio != maqueta) { fails++; printf("FALLO %s: rc=%d %s maquetar (esperado: %s) [%s]\n", nombre, rc, pidio ? "pide" : "no pide", maqueta ? "si" : "no", out); }
}
static void valor(struct wjs *j, const char *nombre, const char *src, const char *esperado)
{
    char out[256]; int rc = wjs_eval_str(j, src, out, sizeof out);
    checks++;
    if (rc || strcmp(out, esperado)) { fails++; printf("FALLO %s: [%s] (esperado [%s])\n", nombre, out, esperado); }
}
int main(void)
{
    struct wdoc *doc = wd_new(); if (!doc) return 2;
    const char *html = "<html><body><div id='p' class='a' style='color: red'><span id='x'>x</span></div><ul id='car'></ul></body></html>";
    wd_parse(doc, html, (uint32_t)strlen(html));
    struct wjs_host host = {0}; host.log = jlog; host.dirty = dirty; host.url = "https://example.org";
    struct wjs *j = wjs_new(doc, &host); if (!j || errors) { fprintf(stderr, "Error inicializando JS\n"); return 2; }
    caso(j, "style nuevo", "var car=document.getElementById('car'); car.style.transition='transform 350ms ease'; 1", 1);
    caso(j, "style igual", "car.style.transition='transform 350ms ease'; 1", 0);
    caso(j, "transform cambia", "car.style.transform='translate3d(-100px,0,0)'; 1", 1);
    caso(j, "transform igual", "car.style.transform='translate3d(-100px,0,0)'; 1", 0);
    caso(j, "quitar style ausente", "car.style.removeProperty('top'); 1", 0);
    caso(j, "quitar style", "car.style.removeProperty('transform'); 1", 1);
    caso(j, "className igual", "var p=document.getElementById('p'); p.className='a'; 1", 0);
    caso(j, "className cambia", "p.className='a b'; 1", 1);
    caso(j, "classList ya estaba", "p.classList.add('b'); 1", 0);
    caso(j, "classList nueva", "p.classList.add('c'); 1", 1);
    caso(j, "setAttribute igual", "p.setAttribute('data-k','1'); p.setAttribute('title','t'); 1", 1);
    caso(j, "setAttribute repetido", "p.setAttribute('data-k','1'); p.setAttribute('title','t'); 1", 0);
    caso(j, "removeAttribute ausente", "p.removeAttribute('data-nada'); 1", 0);
    caso(j, "removeAttribute", "p.removeAttribute('title'); 1", 1);
    caso(j, "id igual", "p.id='p'; 1", 0);
    caso(j, "hidden sin cambio", "p.hidden=false; 1", 0);
    caso(j, "hidden", "p.hidden=true; 1", 1);
    caso(j, "dataset igual", "p.dataset.k='1'; 1", 0);
    caso(j, "dataset cambia", "p.dataset.k='2'; 1", 1);
    caso(j, "texto igual", "var t=document.getElementById('x').firstChild; t.nodeValue='x'; t.textContent='x'; 1", 0);
    caso(j, "texto cambia", "t.textContent='y'; 1", 1);
    caso(j, "suelto: plantilla", "var d=document.createElement('div'); d.className='w'; d.style.color='blue'; d.setAttribute('data-a','b'); d.innerHTML='<b>hola</b>'; d.firstChild.textContent='adios'; d.hidden=true; d.hidden=false; 1", 0);
    caso(j, "suelto: contador", "var im=new Image(); im.src='https://contador.example/p.gif'; im.width=1; 1", 0);
    caso(j, "suelto al documento", "document.body.appendChild(d); 1", 1);
    caso(j, "ya en el documento", "d.className='w2'; 1", 1);
    caso(j, "sacado del documento", "d.remove(); 1", 1);
    caso(j, "sacado: ya no maqueta", "d.className='w3'; d.textContent='z'; 1", 0);
    caso(j, "value de un input", "var i=document.createElement('input'); document.body.appendChild(i); i.setAttribute('value','a'); i.setAttribute('value','a'); 1", 1);
    /* el DOM sigue siendo el de siempre aunque no se maquete */
    valor(j, "valores", "[car.style.transition, p.className, p.getAttribute('data-k'), d.className, d.textContent, im.getAttribute('src')].join('|')",
          "transform 350ms ease|a b c|2|w3|z|https://contador.example/p.gif");
    valor(j, "style como Chrome", "var q=document.createElement('i'); q.style.color='red'; q.style.top='1px'; q.style.color='blue'; q.style.setProperty('left','2px'); q.getAttribute('style')",
          "color: blue; top: 1px; left: 2px;");
    valor(j, "style al quitar", "q.style.removeProperty('top'); q.getAttribute('style')", "color: blue; left: 2px;");
    /* los MutationObserver siguen recibiendo los registros de setAttribute aunque el valor no cambie (como Chrome) */
    caso(j, "observador", "var rec=[]; var ob=new MutationObserver(function(r){ for (var k=0;k<r.length;k++) rec.push(r[k].attributeName); }); ob.observe(p,{attributes:true}); p.className='a b c'; p.dataset.k='2'; 1", 0);
    valor(j, "observador recibe", "rec.join(',')", "class,data-k");
    /* analisis del JavaScript (SOMA_352): los intentos de flecha ya no formatean su error, pero los errores de verdad conservan la
     * columna y el fragmento, tambien dentro de un "(" que se probo como flecha; operadores y palabras reservadas como antes */
    valor(j, "error con columna", "var m1; try { eval('var q = (a, b) + (c * ); 1') } catch (e) { m1 = e.message } m1", "token inesperado (col 23: r q = (a, b) + (c * ); 1) (eval, linea 1)");
    valor(j, "error en una flecha", "var m2; try { eval('f((x) => x + , 2)') } catch (e) { m2 = e.message } m2", "token inesperado (col 14: f((x) => x + , 2)) (eval, linea 1)");
    valor(j, "flechas y parentesis", "var g = (a, b = (1+2)) => a + b; [(1 + 2) * (3, 4), g(1), [1,2].map((v) => (v * 2)).join()].join('|')", "12|4|2,4");
    valor(j, "operadores", "var o = {a:{b:5}}; var z = null; var r = 7; r >>>= 1; r **= 2; [o?.a?.b, z?.q, z ?? 'n', 1 < 2 && 3 >= 3, 5 % 3, r, 2 ** 3, -1 >>> 28].join(',')", "5,,n,true,2,9,8,15");
    valor(j, "palabras parecidas", "var dox = 1, inx = 2, instanceofx = 3, newer = 4, yieldx = 5, b = 6, y = 7; dox + inx + instanceofx + newer + yieldx + b + y", "28");
    printf("test_web_maquetar_352: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
