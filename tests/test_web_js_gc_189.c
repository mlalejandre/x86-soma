/* SOMA 189: pruebas en el anfitrion del recolector de basura del interprete JavaScript (src/apps/web_js_gc.c) y de los
 * indices nuevos (ambitos grandes, Map/Set, puntos de control UTF-16).
 * Se ejecuta con ASan y con una recoleccion cada pocas reservas (HL_GC_STRESS): un objeto liberado antes de tiempo se ve como
 * acceso a memoria envenenada. Necesita detect_stack_use_after_return=0: con la "pila falsa" de ASan las variables locales no
 * estan en la pila real y el recolector, que es conservador, no se activa. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"

void *kmalloc(size_t n) { void *p = malloc(n ? n : 1); if (p) memset(p, 0xDD, n ? n : 1); return p; }   /* como el nucleo: sin limpiar */
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; if (level >= 2) printf("   [js] %s\n", msg); }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *name, *src, *post, *exp; uint32_t max_mib; };
static const struct tc CASES[] = {
    { "basura en bucle: la memoria no crece",
      "var keep=[]; function churn(n){var a=[];for(var i=0;i<n;i++)a.push({i:i,s:'x'+i,f:function(){return i},arr:[i,[i]]});return a.length}"
      "for(var r=0;r<60;r++){churn(6000); if(r%10==0) keep.push({r:r,t:'k'+r});} keep.map(function(k){return k.t}).join()", 0, "k0,k10,k20,k30,k40,k50", 96 },
    { "cierres, prototipos y arrays sobreviven",
      "function mk(i){var priv={n:i,list:[i,i+1]};return {get:function(){return priv.n+priv.list[1]},inc:function(){priv.n++;return this}}}"
      "var os=[];for(var i=0;i<3000;i++)os.push(mk(i)); for(var r=0;r<10;r++){var g=[];for(var j=0;j<10000;j++)g.push('s'+j+r)} var t=0;for(var i=0;i<3000;i++)t+=os[i].inc().get(); t", 0, "9003000", 128 },
    { "Map, Set y WeakMap con miles de claves y borrados",
      "var m=new Map(),st=new Set(),o=[];for(var i=0;i<3000;i++){var k=i%3==0?'k'+i:i%3==1?i:{i:i};o.push(k);m.set(k,i);st.add(k)}"
      "var bad=0;for(var i=0;i<3000;i++){if(m.get(o[i])!==i||!st.has(o[i]))bad++} for(var i=0;i<3000;i+=2){m.delete(o[i]);st.delete(o[i])}"
      "for(var r=0;r<8;r++){var g=[];for(var j=0;j<10000;j++)g.push({j:j})}"
      "for(var i=0;i<3000;i++){if(m.has(o[i])!==(i%2==1)||st.has(o[i])!==(i%2==1))bad++; if(i%2==1&&m.get(o[i])!==i)bad++}"
      "m.set(NaN,1);m.set(0,2); [bad,m.get(NaN),m.get(-0),m.size,[...m.keys()][0]===o[1]].join()", 0, "0,1,2,1502,true", 128 },
    { "ambito con cientos de variables (indice por nombre)",
      "function big(){for(var i=0;i<400;i++)eval('var q'+i+'='+i);var t=0;for(var i=0;i<400;i++)t+=eval('q'+i);eval('var q7=1000');return t+q7+q399}big()", 0, "81199", 64 },
    { "indices UTF-16 al azar en una cadena larga",
      "var parts=[];for(var i=0;i<1500;i++)parts.push(i%7==0?'\\u00f1and\\u00fa\\ud83d\\ude00':i%5==0?'\\u20acuro':'abc'+i);var s=parts.join(' ');"
      "var ref=[];for(var i=0;i<s.length;i++)ref.push(s.charCodeAt(i));var bad=0,seed=12345;function rnd(n){seed=(seed*1103515245+12345)&0x7fffffff;return seed%n}"
      "for(var k=0;k<4000;k++){var i=rnd(s.length);if(s.charCodeAt(i)!==ref[i])bad++}"
      "var s2=s+'x';for(var k=0;k<1000;k++){var a=rnd(s.length),b=Math.min(s.length,a+rnd(40));var t=(k&1?s:s2).slice(a,b);for(var j=0;j<t.length;j++)if(t.charCodeAt(j)!==ref[a+j]){bad++;break}}"
      "for(var i=s.length-1;i>=0;i-=3)if(s.charCodeAt(i)!==ref[i])bad++; s.length+':'+bad", 0, "10467:0", 96 },
    { "DOM: envoltorios, propiedades propias y manejadores",
      "var d=document.getElementById('p');for(var i=0;i<400;i++){var e=document.createElement('p');e.textContent='n'+i;e.dataset.i=i;e.addEventListener('click',function(){});e.mio={big:new Array(40).fill(i)};d.appendChild(e)}"
      "for(var r=0;r<8;r++){var g=[];for(var j=0;j<10000;j++)g.push({j:j,s:'t'+j})}"
      "Array.prototype.every.call(d.children,function(p,i){return p.mio.big.length===40&&p.mio.big[39]===i&&p.dataset.i==i&&p.textContent==='n'+i})+':'+d.children.length", 0, "true:400", 128 },
    { "temporizadores y promesas entre recolecciones",
      "var out=[];function step(i){return new Promise(function(res){setTimeout(function(){var g=[];for(var j=0;j<8000;j++)g.push({j:j});out.push('p'+i);res(i)},1)})}"
      "var chain=Promise.resolve();for(var i=0;i<12;i++)(function(i){chain=chain.then(function(){return step(i)})})(i); chain.then(function(v){out.push('fin'+v)}); 1",
      "out.join()", "p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,fin11", 128 },
    { "analizar y descartar codigo (eval y Function)",
      "var t=0;for(var i=0;i<400;i++){var f=new Function('a','var o={v:a*2,l:[a,a,a]};return o.v+o.l.length+'+i);t+=f(i)} t", 0, "240600", 96 },
};
int main(void)
{
    const char *html = "<!doctype html><html><body><div id='p'></div></body></html>";
    struct wjs_host h = {0}; h.log = jlog; h.dirty = dirty; h.url = "https://example.org";
    int n = (int)(sizeof CASES / sizeof CASES[0]), fail = 0; uint32_t total_gc = 0;
    g_jsco_off = 1;                                  /* con ASan, sin pilas de corrutina */
    static const char *STRESS[] = { "0", "6000", "1500" };
    for (int s = 0; s < 3; s++) {
        setenv("HL_GC_STRESS", STRESS[s], 1);
        for (int i = 0; i < n; i++) {
            struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
            struct wjs *j = wjs_new(d, &h); static char out[4096]; out[0] = 0;
            int rc = wjs_eval_str(j, CASES[i].src, out, sizeof out);
            if (CASES[i].post && rc == 0) {
                for (int k = 0; k < 400; k++) { int ran = 0; g_now += 50; int nx = wjs_pump(j, g_now, &ran); if (nx < 0 && !ran) break; }
                rc = wjs_eval_str(j, CASES[i].post, out, sizeof out);
            }
            uint32_t gc = 0, live = 0; wjs_gc_stats(j, &gc, &live); total_gc += gc;
            uint32_t mib = (uint32_t)wjs_heap_used(j) >> 20;
            if (rc != 0 || strcmp(out, CASES[i].exp)) { fail++; printf("FAIL [%s] estres %s\n   esperado [%s] obtenido [%s] rc=%d\n", CASES[i].name, STRESS[s], CASES[i].exp, out, rc); }
            else if (mib > CASES[i].max_mib) { fail++; printf("FAIL [%s] estres %s: el monton quedo en %u MiB (maximo %u): no se libera\n", CASES[i].name, STRESS[s], mib, CASES[i].max_mib); }
            else if (s == 0 && i == 0 && gc == 0) { fail++; printf("FAIL [%s]: no hubo ninguna recoleccion\n", CASES[i].name); }
            wjs_free(j); wd_free(d);
        }
    }
    if (fail) { printf("test_web_js_gc_189: %d fallos\n", fail); return 1; }
    printf("test_web_js_gc_189: %d casos x 3 ritmos de recoleccion OK (%u recolecciones)\n", n, total_gc);
    return 0;
}
