/* SOMA_187: pruebas en el anfitrion de Streams (ReadableStream/WritableStream/TransformStream/TextEncoderStream/TextDecoderStream), Response.body y
 * Blob.stream, indexedDB en memoria (almacenes, indices, cursores, transacciones, abortos, versiones), CacheStorage, WeakRef/FinalizationRegistry,
 * Symbol.prototype.description y el modo soma.jsstream=off. Se compila con ASan/UBSan. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_js.h"

void *kmalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_now = 10000;
uint64_t web_now_ms(void) { return g_now; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
int g_jsco_off;
extern int g_ju16_off, g_jbin_off, g_jstream_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *src, *post, *exp; };
static const struct tc CASES[] = {
    { "Symbol('a').toString()", 0, "Symbol(a)" },
    { "Symbol('a').description", 0, "a" },
    { "Symbol().description", 0, "undefined" },
    { "Symbol().toString()", 0, "Symbol()" },
    { "String(Symbol('x'))", 0, "Symbol(x)" },
    { "Symbol.iterator.description", 0, "Symbol.iterator" },
    { "Symbol.iterator.toString()", 0, "Symbol(Symbol.iterator)" },
    { "String(Symbol.asyncIterator)", 0, "Symbol(Symbol.asyncIterator)" },
    { "Symbol.for('k').description", 0, "k" },
    { "typeof Symbol('a')", 0, "symbol" },
    { "Symbol('a#1').description", 0, "a#1" },
    { "var s=Symbol('q'); var o={}; o[s]=5; o[s]", 0, "5" },
    { "typeof ReadableStream + typeof WritableStream + typeof TransformStream", 0, "functionfunctionfunction" },
    { "var o=[]; var rs=new ReadableStream({start:function(c){c.enqueue(1);c.enqueue(2);c.close()}}); var r=rs.getReader(); r.read().then(function(x){o.push(x.value); return r.read()}).then(function(x){o.push(x.value); return r.read()}).then(function(x){o.push(x.done)})", "o.join()", "1,2,true" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue('a');c.enqueue('b');c.close()}}); for await (var v of rs) o.push(v); })()", "o.join('')", "ab" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue(1);c.enqueue(2);c.enqueue(3);c.close()}}); var ts=new TransformStream({transform:function(ch,c){c.enqueue(ch*10)}}); for await (var v of rs.pipeThrough(ts)) o.push(v); })()", "o.join()", "10,20,30" },
    { "var o=[]; (async function(){ var ws=new WritableStream({write:function(c){o.push(c)},close:function(){o.push('end')}}); var w=ws.getWriter(); await w.write(1); await w.write(2); await w.close(); })()", "o.join()", "1,2,end" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue('x');c.enqueue('y');c.close()}}); var a=rs.tee(); var s=''; for await (var v of a[0]) s+=v; var t=''; for await (var v of a[1]) t+=v; o.push(s+t); })()", "o.join()", "xyxy" },
    { "var o=[]; new Blob(['héllo wörld']).stream().getReader().read().then(function(x){o.push(x.value.length)})", "o.join()", "13" },
    { "var o=[]; (async function(){ var chunks=[]; var r=new Response('héllo').body.getReader(); for(;;){var x=await r.read(); if(x.done)break; chunks.push(x.value.length)} o.push(chunks.join()) })()", "o.join()", "6" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue(new Uint8Array([104,105]));c.close()}}); o.push(await new Response(rs).text()) })()", "o.join()", "hi" },
    { "var o=[]; (async function(){ var res=new Response('abc'); res.body; try { await res.text(); o.push('ok'); } catch(e) { o.push('err') } })()", "o.join()", "ok" },
    { "var o=[]; (async function(){ var res=new Response('abc'); await res.body.getReader().read(); try { await res.text(); o.push('ok'); } catch(e) { o.push(e.constructor.name) } })()", "o.join()", "TypeError" },
    { "new Response(null).body === null", 0, "true" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue(new Uint8Array([0xE2,0x82]));c.enqueue(new Uint8Array([0xAC]));c.close()}}); var s=''; for await (var v of rs.pipeThrough(new TextDecoderStream())) s+=v; o.push(s) })()", "o.join()", "€" },
    { "var o=[]; (async function(){ var rs=new ReadableStream({start:function(c){c.enqueue('hé');c.close()}}); for await (var v of rs.pipeThrough(new TextEncoderStream())) o.push(v.length) })()", "o.join()", "3" },
    { "var w=new WeakRef({a:1}); w.deref().a", 0, "1" },
    { "typeof FinalizationRegistry", 0, "function" },
    { "var r=[]; var db; var q=indexedDB.open('t1',1); q.onupgradeneeded=function(e){ var d=q.result; var s=d.createObjectStore('s',{keyPath:'id'}); s.put({id:2,n:'b'}); s.put({id:1,n:'a'}); r.push('up'+e.oldVersion+e.newVersion); }; q.onsuccess=function(){ var d=q.result; var t=d.transaction('s','readonly'); var g=t.objectStore('s').getAll(); g.onsuccess=function(){ r.push(g.result.map(function(x){return x.n}).join('')) } }", "r.join()", "up01,ab" },
    { "var r=[]; var q=indexedDB.open('t2',1); q.onupgradeneeded=function(){ q.result.createObjectStore('s',{autoIncrement:true}) }; q.onsuccess=function(){ var d=q.result; var t=d.transaction('s','readwrite'); var s=t.objectStore('s'); var a=s.add('x'); var b=s.add('y'); b.onsuccess=function(){ r.push(a.result+':'+b.result) }; t.oncomplete=function(){ r.push('done') } }", "r.join()", "1:2,done" },
    { "var r=[]; var q=indexedDB.open('t3',1); q.onupgradeneeded=function(){ var s=q.result.createObjectStore('s',{keyPath:'id'}); s.createIndex('byName','n',{unique:true}); s.put({id:1,n:'a'}) }; q.onsuccess=function(){ var d=q.result; var t=d.transaction('s','readwrite'); var s=t.objectStore('s'); var a=s.add({id:2,n:'a'}); a.onerror=function(e){ r.push(a.error.name); e.preventDefault() }; var g=s.index('byName').get('a'); g.onsuccess=function(){ r.push(g.result.id) } }", "r.join()", "ConstraintError,1" },
    { "var r=[]; var q=indexedDB.open('t4',1); q.onupgradeneeded=function(){ var s=q.result.createObjectStore('s',{keyPath:'id'}); for (var i=1;i<=5;i++) s.put({id:i}) }; q.onsuccess=function(){ var t=q.result.transaction('s'); var c=t.objectStore('s').openCursor(IDBKeyRange.bound(2,4)); c.onsuccess=function(){ var cur=c.result; if(cur){ r.push(cur.key); cur.continue() } } }", "r.join()", "2,3,4" },
    { "var r=[]; var q=indexedDB.open('t5',1); q.onupgradeneeded=function(){ var s=q.result.createObjectStore('s',{keyPath:'id'}); for (var i=1;i<=3;i++) s.put({id:i}) }; q.onsuccess=function(){ var t=q.result.transaction('s'); var c=t.objectStore('s').openCursor(null,'prev'); c.onsuccess=function(){ var cur=c.result; if(cur){ r.push(cur.key); cur.continue() } } }", "r.join()", "3,2,1" },
    { "var r=[]; var q=indexedDB.open('t6',1); q.onupgradeneeded=function(){ q.result.createObjectStore('s') }; q.onsuccess=function(){ var d=q.result; var t=d.transaction('s','readwrite'); t.objectStore('s').put(1,'a'); t.abort(); t.onabort=function(){ var t2=d.transaction('s'); var g=t2.objectStore('s').count(); g.onsuccess=function(){ r.push(g.result) } } }", "r.join()", "0" },
    { "var r=[]; var q=indexedDB.open('t7',2); q.onupgradeneeded=function(e){ r.push(e.oldVersion+'>'+e.newVersion) }; q.onsuccess=function(){ q.result.close(); var q2=indexedDB.open('t7',1); q2.onerror=function(){ r.push(q2.error.name) } }", "r.join()", "0>2,VersionError" },
    { "var r=[]; indexedDB.databases().then(function(l){ r.push(typeof l.length) })", "r.join()", "number" },
    { "indexedDB.cmp(1,2)+','+indexedDB.cmp('b','a')+','+indexedDB.cmp([1,2],[1,2])+','+indexedDB.cmp(5,'a')", 0, "-1,1,0,-1" },
    { "IDBKeyRange.bound(1,5).includes(3)+','+IDBKeyRange.lowerBound(3,true).includes(3)", 0, "true,false" },
    { "var r=[]; var q=indexedDB.open('t8',1); q.onupgradeneeded=function(){ var s=q.result.createObjectStore('s',{keyPath:'id'}); s.put({id:1,d:new Date(5),m:new Map([[1,2]])}) }; q.onsuccess=function(){ var g=q.result.transaction('s').objectStore('s').get(1); g.onsuccess=function(){ r.push((g.result.d instanceof Date)+':'+g.result.d.getTime()+':'+g.result.m.get(1)) } }", "r.join()", "true:5:2" },
    { "var r=[]; (async function(){ var c=await caches.open('c1'); await c.put('/a.txt', new Response('hello')); var m=await c.match('/a.txt'); r.push(await m.text()); r.push((await c.keys()).length); r.push(await c.delete('/a.txt')); r.push(await c.match('/a.txt')) })()", "r.join()", "hello,1,true," },
    { "var r=[]; (async function(){ await caches.open('c2'); r.push(await caches.has('c2')); r.push((await caches.keys()).join()); r.push(await caches.delete('c2')); r.push(await caches.has('c2')) })()", "r.join()", "true,c2,true,false" },
};
static const struct tc OFF[] = {
    { "typeof ReadableStream+typeof indexedDB+typeof caches+typeof WeakRef", 0, "undefinedundefinedundefinedundefined" },
    { "var r=new Response('hi'); r.body === null || typeof r.body === 'object'", 0, "true" },
    { "var o=[]; new Response('héy').text().then(function(t){o.push(t)})", "o.join()", "héy" },
};
static int run(const struct tc *t, int n, const char *name)
{
    const char *html = "<!doctype html><html><body><div id='p'></div></body></html>";
    struct wjs_host h = {0}; h.log = jlog; h.dirty = dirty; h.url = "https://example.org";
    int fail = 0;
    for (int i = 0; i < n; i++) {
        struct wdoc *d = wd_new(); wd_parse(d, html, (uint32_t)strlen(html));
        struct wjs *j = wjs_new(d, &h); char out[2048] = "";
        int rc = wjs_eval_str(j, t[i].src, out, sizeof out);
        if (t[i].post && rc == 0) {
            for (int k = 0; k < 200; k++) { int ran = 0; g_now += 50; int nx = wjs_pump(j, g_now, &ran); if (nx < 0 && !ran) break; }
            rc = wjs_eval_str(j, t[i].post, out, sizeof out);
        }
        if (rc != 0 || strcmp(out, t[i].exp)) { fail++; printf("FAIL %s #%d: %s\n   esperado [%s] obtenido [%s] rc=%d\n", name, i, t[i].src, t[i].exp, out, rc); }
        wjs_free(j); wd_free(d);
    }
    return fail;
}
int main(void)
{
    int fail = 0;
    g_ju16_off = 0; g_jbin_off = 0; g_jstream_off = 0; fail += run(CASES, (int)(sizeof CASES / sizeof CASES[0]), "streams");
    g_jstream_off = 1; fail += run(OFF, (int)(sizeof OFF / sizeof OFF[0]), "off");
    if (fail) { printf("test_web_js_187: %d fallos\n", fail); return 1; }
    printf("test_web_js_187: %d casos OK (streams/idb/caches) + %d (modo off)\n", (int)(sizeof CASES / sizeof CASES[0]), (int)(sizeof OFF / sizeof OFF[0]));
    return 0;
}
