/* SOMA_200: fuzzing del motor de JavaScript y de su puente con el DOM en el anfitrion (ASan + UBSan).
 * Programas armados al azar con piezas que suelen romper motores: retrollamadas que cambian el array que se esta recorriendo,
 * indices y longitudes extremos, valueOf/toString con efectos, vistas sobre bufferes, nodos que se quitan dentro de su propio
 * manejador, recursion, cadenas enormes... mas mutaciones a nivel de caracter. Uso: fuzz_js_200 [iteraciones] [semilla] [archivo] */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "web_js.h"
#include "web_dom.h"
#include "web_css.h"
#include "web_ua.h"

static size_t g_alloc, g_alloc_max = 256u << 20;
void *kmalloc(size_t n) { if (n > (128u << 20) || g_alloc + n > g_alloc_max) return 0; g_alloc += n; return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now += 3; }           /* el reloj avanza: los limites de tiempo del motor actuan */
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off = 1;                                         /* con ASan las pilas de corrutina dan falsos positivos */
extern int g_ju16_off, g_jbin_off, g_jstream_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }
static int h_fetch(void *ud, const char *m, const char *url, const char *body, uint32_t blen, const char *rct, int *status, char **out, uint32_t *outlen, char *ctype, uint32_t ctmax)
{
    (void)ud; (void)m; (void)url; (void)body; (void)blen; (void)rct;
    static const char R[] = "{\"a\":[1,2,3],\"b\":\"x\"}";
    char *o = (char *)kmalloc(sizeof R); if (!o) return -1;
    memcpy(o, R, sizeof R); *out = o; *outlen = sizeof R - 1; *status = 200; if (ctmax > 17) strcpy(ctype, "application/json");
    return 0;
}

static uint32_t rs = 1;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
static char buf[1 << 15]; static uint32_t bl;
static void add(const char *s) { while (*s && bl < sizeof buf - 2) buf[bl++] = *s++; }
#define PICK(a) a[rn(sizeof a / sizeof a[0])]
static const char *const NUM[] = { "0", "1", "-1", "2", "7", "255", "256", "65535", "65536", "1e9", "2147483647", "2147483648", "-2147483648", "4294967295", "4294967296", "9007199254740993", "1e308", "-0", "NaN", "Infinity", "-Infinity", "0.5", "1.5",
    "{valueOf(){a.length=0;return 3}}", "{valueOf(){a.push(1,2,3,4,5,6,7,8);return 1}}", "{toString(){b=null;return '2'}}", "'3'", "null", "undefined", "true", "[]", "{}", "10n", "Symbol()", "a.length", "a.length-1", "a.length+1" };
static const char *const MK[] = { "a=[1,2,3,4,5,6,7,8];", "a=[];", "a=new Array(%N);", "a=[1,,3];a[%N]=1;", "a=Array.from({length:20},(x,i)=>i);", "a=new Uint8Array(%N);", "a=new Float64Array(8);", "a=new Int32Array(new ArrayBuffer(64),8,%N);",
    "b=new ArrayBuffer(%N);a=new DataView(b);", "a='abcdefghij'.split('');", "a=Object.create([1,2,3]);", "a=new Proxy([1,2,3],{get(t,k){return k==='length'?%N:t[k]}});", "a=document.querySelectorAll('*');", "a=Array.from(document.body.childNodes);",
    "a=[[1,[2,[3,[4]]]]];", "a={length:%N,0:1,1:2};", "a=new Map([[1,2],[3,4]]);", "a=new Set([1,2,3]);", "a='x'.repeat(%N);", "a=[3,1,2].concat(a||[]);", "a=arguments;", "a=new Uint8Array(16).subarray(%N,%N);", "a=new Uint16Array([1,2,3]).buffer;" };
static const char *const OP[] = { "a.length=%N;", "a[%N]=%N;", "x=a[%N];", "a.push(%N,%N);", "a.pop();", "a.shift();", "a.unshift(%N);", "a.splice(%N,%N,%N,%N);", "x=a.slice(%N,%N);", "a.copyWithin(%N,%N,%N);", "a.fill(%N,%N,%N);", "a.reverse();",
    "a.sort(function(p,q){a.length=%N;return p-q});", "a.sort(function(){return %N});", "a.forEach(function(v,i){if(i==1)a.length=%N;a.push(v)});", "x=a.map(function(v,i){a.pop();return v});", "x=a.filter(function(){a.shift();return true});",
    "x=a.reduce(function(s,v){a.splice(0,%N);return s+v},0);", "x=a.indexOf(%N,%N);", "x=a.lastIndexOf(%N,%N);", "x=a.includes(%N,%N);", "x=a.join(%N);", "x=a.flat(%N);", "x=a.at(%N);", "x=a.concat(a,a,%N);", "x=Array.prototype.slice.call(a,%N,%N);",
    "x=Array.prototype.splice.call(a,%N,%N);", "x=Array.prototype.fill.call(a,%N,%N,%N);", "a.set&&a.set([1,2,3],%N);", "x=a.subarray&&a.subarray(%N,%N);", "a.setUint32&&a.setUint32(%N,%N);", "x=a.getFloat64&&a.getFloat64(%N);", "a.setInt8&&a.setInt8(%N,1);",
    "x=String(a).substring(%N,%N);", "x=String(a).padStart(%N,'ab');", "x=String(a).charCodeAt(%N);", "x='ab'.repeat(%N%100000);", "x=String.fromCharCode.apply(null,a);", "x=JSON.stringify(a);", "x=JSON.parse(JSON.stringify(a));", "x=JSON.parse('['.repeat(%N%5000));",
    "x=Object.keys(a);", "x=Object.entries(a);", "Object.defineProperty(a,%N,{get(){a.length=0;return 1},configurable:true});", "Object.freeze(a);", "delete a[%N];", "x=[...a];", "for(var k in a){a.length=%N;break}", "for(var v of a){a.push&&a.push(1);if(a.length>50)break}",
    "x=a.toString();", "x=new Uint8Array(a);", "x=new Uint8Array(b||8,%N,%N);", "x=structuredClone?structuredClone(a):0;", "x=/(a+)+$/.test('a'.repeat(%N%30)+'b');", "x='abc'.replace(/b/g,function(){a.length=0;return 'x'});", "x='a,b'.split(',',%N);",
    "x=new RegExp('('.repeat(%N%200)+'a'+')'.repeat(%N%200));", "x=a.entries&&a.entries().next();", "x=Math.max.apply(null,a);", "(function r(n){if(n>0)r(n-1)})(%N%3000);", "x=eval('1+'+%N);", "x=new Function('return '+%N)();", "x=BigInt(%N%1000)**BigInt(%N%300);",
    "x=new Array(%N%1000).join('ab');", "x=parseInt(a,%N);", "x=Number(a).toFixed(%N%101);", "x=(%N).toString(%N%37||2);", "x=encodeURIComponent(String(a));", "x=atob(btoa(String(a)));", "x=new TextEncoder().encode(String(a));", "x=new TextDecoder().decode(a.buffer||new Uint8Array(4));",
    "x=Symbol.iterator in Object(a);", "a.__proto__=Uint8Array.prototype;", "a.constructor=Array;", "x=a.slice&&a.slice.call('abc',%N);", "x=Reflect.ownKeys(a);", "x=new WeakMap([[{},a]]);", "x=a.valueOf();" };
static const char *const DOM[] = { "d=document.createElement('div');document.body.appendChild(d);", "d.innerHTML='<p id=q><b>x</b></p><i>y</i>';", "d.appendChild(document.body);", "d.insertBefore(d,d.firstChild);", "document.body.innerHTML='';", "d.remove();", "d.parentNode&&d.parentNode.removeChild(d);",
    "d.addEventListener('click',function(e){d.remove();document.body.innerHTML='<a>z</a>';e.target.textContent=1});d.click();", "d.onclick=function(){this.onclick=null;this.click()};d.click();", "x=d.cloneNode(true);x.appendChild(d);", "d.outerHTML='<span></span>';",
    "x=document.createTreeWalker(document.body);x.nextNode();document.body.innerHTML='';x.nextNode();x.parentNode();", "x=document.createRange&&document.createRange();", "d.setAttribute('style','width:'+%N+'px');x=getComputedStyle(d).width;", "d.className='a b';d.classList.toggle('a');d.classList.remove('b','c');",
    "x=d.children;d.innerHTML='<i></i><i></i>';x[0].remove();x=x.length+x[%N];", "x=d.childNodes;for(var i=0;i<x.length;i++)d.removeChild(x[i]);", "d.textContent='x'.repeat(%N%5000);", "x=d.querySelector(':not(:nth-child(%N))');", "x=d.closest('body');d.replaceWith(d);",
    "d.attachShadow&&d.attachShadow({mode:'open'}).appendChild(document.createElement('slot'));", "x=document.createDocumentFragment();x.appendChild(d);x.appendChild(x);", "d.dataset.a=%N;x=d.attributes[%N];", "x=new MutationObserver(function(){d.remove()});x.observe(document.body,{childList:true,subtree:true});",
    "setTimeout(function(){d.remove();a.length=0},%N%50);", "Promise.resolve().then(function(){d.innerHTML='';throw 1});", "x=d.getBoundingClientRect();", "d.style.cssText='a:b;'.repeat(%N%500);", "document.title=String(a);", "x=d.insertAdjacentHTML&&d.insertAdjacentHTML('beforebegin','<p>');",
    "d.id='q';x=document.getElementById('q');d.id='';x.id='w';", "x=d.firstChild;d.innerHTML='';x&&x.appendChild(document.createTextNode('z'));", "x=document.createElement('select');x.innerHTML='<option>1<option>2';x.selectedIndex=%N;x.options[%N]=null;", "x=document.createElement('table').insertRow(%N%3-1);",
    "x=document.createElement('input');x.value='abc';x.setSelectionRange(%N,%N);x.maxLength=%N;", "fetch('/x').then(function(r){return r.json()}).then(function(j){a=j.a;d.remove()});", "x=new XMLHttpRequest();x.open('GET','/x',false);x.send();x=x.responseText;", "x=d.animate&&d.animate([],%N);", "history.pushState({a:a},'','#'+%N);" };
/* SOMA_200: nombres y textos largos para las API que copian a buferes fijos (propiedades de estilo, atributos, mensajes de error...) */
static const char *const LONG[] = { "'x'.repeat(130)", "'ab-'.repeat(400)", "'\\u00e9'.repeat(700)", "'a'.repeat(5000)", "'--v'+'y'.repeat(200)", "'on'+'k'.repeat(90)", "'data-'+'q'.repeat(300)", "'\\ud83d\\ude00'.repeat(200)", "'%'.repeat(600)", "'a b '.repeat(300)", "'http://h/'+'p/'.repeat(1500)",
    "'#'+'i'.repeat(700)", "'.'+'c'.repeat(700)", "'['+'z'.repeat(300)+']'", "':not('.repeat(300)", "'A'.repeat(63)", "'A'.repeat(64)", "'A'.repeat(65)", "'p'.repeat(119)", "'p'.repeat(120)", "'p'.repeat(121)", "'s'.repeat(599)+':1'", "'Z'.repeat(257)", "String.fromCharCode(0x10FFFF+1)", "'\\0'.repeat(300)" };
static const char *const LAPI[] = { "d.style[%L]=%L;", "x=d.style[%L];", "d.style.setProperty(%L,%L);", "x=d.style.getPropertyValue(%L);", "d.style.removeProperty(%L);", "d.style.cssText=%L+':'+%L;", "x=getComputedStyle(d)[%L];", "x=getComputedStyle(d).getPropertyValue(%L);",
    "d.setAttribute(%L,%L);", "x=d.getAttribute(%L);", "d.removeAttribute(%L);", "x=d.hasAttribute(%L);", "d.dataset[%L]=%L;", "x=d.dataset[%L];", "d.classList.add(%L);", "d.classList.toggle(%L);", "x=d.classList.contains(%L);", "d.className=%L;", "d.id=%L;x=document.getElementById(%L);",
    "x=document.createElement(%L);x=x.tagName+x.nodeName+x.localName;", "x=document.createElementNS('http://www.w3.org/2000/svg',%L);x=x.tagName;", "x=d.querySelector(%L);", "x=d.querySelectorAll(%L);", "x=d.matches(%L);", "x=d.closest(%L);", "x=document.getElementsByClassName(%L);x=x.length;",
    "x=document.getElementsByTagName(%L).length;", "d.addEventListener(%L,function(){});d.dispatchEvent(new Event(%L));", "d[%L]=function(){};d.setAttribute(%L,'1');", "x=new Event(%L);x=x.type;", "x=new CustomEvent(%L,{detail:%L});", "throw new Error(%L);", "x=new TypeError(%L).stack;",
    "x=Symbol(%L).toString();x=Symbol.for(%L).description;", "x=new RegExp(%L);", "x=new RegExp('['+%L);", "x=JSON.parse(%L);", "x=JSON.parse('{\"'+%L+'\":1}');", "x={};x[%L]=1;x=JSON.stringify(x);", "localStorage.setItem(%L,%L);x=localStorage.getItem(%L);x=localStorage.length;", "sessionStorage[%L]=%L;",
    "document.cookie=%L+'='+%L;x=document.cookie;", "x=new URL(%L,'http://a/b');x=x.href+x.pathname;", "x=new URL('http://'+%L+'/');", "x=new URLSearchParams(%L).toString();", "location.hash=%L;", "history.pushState(1,'',%L);", "document.title=%L;x=document.title;", "x=d.innerHTML='<'+%L+' '+%L+'=1>';",
    "d.innerHTML='<p style=\"'+%L+':'+%L+'\">';", "x=document.createElement('a');x.href=%L;x=x.href+x.host+x.pathname+x.search+x.hash;", "x=document.createElement('input');x.type=%L;x.name=%L;x.value=%L;x=x.type;", "x=CSS&&CSS.escape&&CSS.escape(%L);", "x=CSS&&CSS.supports&&CSS.supports(%L,%L);",
    "x=new Function(%L,'return 1');", "x=eval('var '+%L+'=1');", "x=({})[%L]();", "x=undefined[%L];", "null[%L]=1;", "x=(1)[%L]();", "x=%L in d;", "x=d.getElementsByTagName(%L)[%L];", "x=new Intl.NumberFormat(%L).format(1);", "x=(1).toLocaleString(%L,{style:%L,currency:%L});", "x=new Date(%L);x=Date.parse(%L);",
    "x=parseFloat(%L);x=Number(%L);x=parseInt(%L,36);", "x=BigInt('1'+'0'.repeat(400));x=x.toString(2);", "x=%L.normalize&&%L.normalize('NFD');", "x=%L.toUpperCase().toLowerCase();", "x=%L.localeCompare(%L);", "x=encodeURI(%L);x=decodeURIComponent(%L);", "x=escape(%L);x=unescape(%L);", "x=btoa(%L);",
    "x=new TextDecoder(%L);", "x=console.log(%L,%L);console.error(new Error(%L));", "x=d.insertAdjacentHTML(%L,'<p>');", "x=window[%L];window[%L]=1;", "x=document.createEvent(%L);", "x=d.getAttributeNode&&d.getAttributeNode(%L);", "x=matchMedia&&matchMedia(%L).matches;", "x=fetch(%L);",
    "x=new XMLHttpRequest();x.open(%L,%L);x.setRequestHeader(%L,%L);", "x=document.createElement('img');x.src=%L;x.alt=%L;", "x=document.createElement('script');x.src=%L;document.body.appendChild(x);", "x=document.createElement('link');x.rel='stylesheet';x.href=%L;document.head.appendChild(x);",
    "x=document.createElement('style');x.textContent=%L+'{'+%L+':'+%L+'}';document.head.appendChild(x);", "x=customElements&&customElements.define(%L,class extends HTMLElement{});", "x=import(%L);", "x=d.animate&&d.animate({[%L]:[1,2]},1);", "label:{x=%L;break label}", "x=`${%L}${%L}`.length;" };
static void emit(const char *t) { for (; *t && bl < sizeof buf - 300; t++) { if (t[0] == '%' && t[1] == 'N') { add(PICK(NUM)); t++; } else if (t[0] == '%' && t[1] == 'L') { add(rn(5) ? PICK(LONG) : "String(a)"); t++; } else buf[bl++] = *t; } }
static int g_long;
static void gen(void)
{
    g_long = (rnd() & 1) || getenv("FUZZ_LONG");
    add("var a,b,d=document.body,x;");
    for (int k = 3 + (int)rn(14); k > 0; k--) {
        int w = (int)rn(10);
        if (rn(3) == 0) add("try{");
        emit(g_long && w >= 3 ? PICK(LAPI) : w < 2 ? PICK(MK) : w < 7 ? PICK(OP) : PICK(DOM));
        if (buf[bl - 1] != ';') add(";");
        if (bl > 6 && rn(1) == 0) { /* cierra el try si se abrio */ const char *p = buf + bl; int open = 0; for (const char *q = buf; q < p; q++) { if (!strncmp(q, "try{", 4)) open++; if (!strncmp(q, "}catch(e){}", 11)) open--; } if (open > 0) add("}catch(e){}"); }
    }
    if (rn(4) == 0 && bl > 8) for (int m = 1 + (int)rn(3); m > 0; m--) { uint32_t p = rn(bl); int k = (int)rn(4); if (k == 0) buf[p] = "()[]{};,.'\"+-*/=<>!&|?:0 "[rn(25)]; else if (k == 1 && p + 8 < bl) { memmove(buf + p, buf + p + 4, bl - p - 4); bl -= 4; } else if (k == 2) buf[p] = (char)(32 + rn(95)); else if (p > 4) bl = p; }
    buf[bl] = 0;
}
static uint32_t g_iter;
static void on_alarm(int s) { (void)s; fprintf(stderr, "\nCUELGUE: el caso %u lleva mas de 30 s (guardado en /tmp/fuzz_js_cuelgue.js)\n", g_iter); FILE *f = fopen("/tmp/fuzz_js_cuelgue.js", "wb"); if (f) { fwrite(buf, 1, bl, f); fclose(f); } _exit(3); }
static void run_case(void)
{
    static const char html[] = "<!doctype html><html><head><style>p{color:red}</style></head><body><div id='p' class='a'><span>t</span><p>u</p></div><form><input name=i><select><option>1</select></form></body></html>";
    struct wjs_host h = {0}; h.log = jlog; h.dirty = dirty; h.url = "https://example.org/dir/"; h.fetch = h_fetch;
    g_alloc = 0;
    struct wdoc *d = wd_new(); if (!d) return;
    wd_parse(d, html, (uint32_t)(sizeof html - 1));
    struct wcss *c = wc_new(d);                               /* como en el navegador: la hoja de estilos existe antes que el motor JS */
    if (!c) { wd_free(d); return; }
    wc_add_sheet(c, web_ua_css, (uint32_t)(sizeof web_ua_css - 1), 0); wc_compute(c, 800, 600); d->sheets = c;
    struct wjs *j = wjs_new(d, &h);
    if (j) {
        (void)wjs_eval(j, buf, bl, "fuzz.js");
        for (int k = 0; k < 12; k++) { int ran = 0; g_now += 60; int nx = wjs_pump(j, g_now, &ran); if (nx < 0 && !ran) break; }
        if (d->root) { (void)wjs_dispatch(j, d->root->first ? d->root->first : d->root, "click", 5, 5, 0, "", 0); wjs_fire_load(j); }
        for (int k = 0; k < 4; k++) { int ran = 0; g_now += 60; (void)wjs_pump(j, g_now, &ran); }
        wjs_free(j);
    }
    wc_free(c);
    wd_free(d);
}
int main(int argc, char **argv)
{
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 3000, seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 1;
    g_ju16_off = 0; g_jbin_off = 0; g_jstream_off = 0;
    signal(SIGALRM, on_alarm);
    if (argc > 3) { FILE *f = fopen(argv[3], "rb"); if (!f) return 2; bl = (uint32_t)fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[bl] = 0; alarm(120); run_case(); printf("caso reproducido sin fallo\n"); return 0; }
    for (g_iter = getenv("FUZZ_START") ? (uint32_t)atoi(getenv("FUZZ_START")) : 0; g_iter < iters; g_iter++) {
        rs = (seed * 2654435761u) ^ (g_iter * 40503u + 1u); if (!rs) rs = 1;
        bl = 0; gen();
        FILE *f = fopen("/tmp/fuzz_js_ultimo.js", "wb"); if (f) { fwrite(buf, 1, bl, f); fclose(f); }
        alarm(30);
        run_case();
    }
    alarm(0);
    printf("fuzz_js_200: %u programas sin fallos de memoria ni cuelgues (semilla %u)\n", iters, seed);
    return 0;
}
