/* SOMA_183: pruebas en el anfitrion de las corrutinas del motor JavaScript: generadores, async/await real, for await,
 * generadores asincronos, try/finally con return(), var en flechas y el modo soma.jsco=off. Se compila con ASan/UBSan. */
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
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *src, *post, *exp; };
static const struct tc CASES[] = {
    { "(()=>{ var z = 7; return z })()", 0, "7" },
    { "(()=>{ var z = 7; return z+1 })()", 0, "8" },
    { "(()=>{ var z; z = 7; return z })()", 0, "7" },
    { "((...r)=>r.length)(1,2,3)", 0, "3" },
    { "((a,...r)=>r.length)(1,2,3)", 0, "2" },
    { "(async()=>{ for await (const x of [1,2]) {} })() instanceof Promise", 0, "true" },
    { "(async()=>{ var z = 7; return z })() instanceof Promise", 0, "true" },
    { "(function(){ 'use strict'; return this })()", 0, "undefined" },
    { "(function(){ var z = 7; return z })()", 0, "7" },
    { "0b101+0o7", 0, "12" },
    { "1_000", 0, "1000" },
    { "Array.isArray(Array.of(1))", 0, "true" },
    { "Number('0x10')", 0, "16" },
    { "Object.hasOwn({a:1},'a')", 0, "true" },
    { "Promise.all([1,Promise.resolve(2)]).then(v=>v) instanceof Promise", 0, "true" },
    { "Promise.allSettled([Promise.reject(1)]).then(r=>r) instanceof Promise", 0, "true" },
    { "[...\"ab\"].entries().next().value.join()", 0, "0,a" },
    { "[3,1,2].toSorted().join()", 0, "1,2,3" },
    { "async function f(){ return 5 } typeof f().then", 0, "function" },
    { "async function* ag(){ yield 1; } typeof ag().next", 0, "function" },
    { "class C{ *items(){ yield 1; } } [...new C().items()].length", 0, "1" },
    { "class T{ static *s(){ yield 1 } } [...T.s()].length", 0, "1" },
    { "function* a(){ yield 1; yield* b(); yield 4 } function* b(){ yield 2; yield 3 } [...a()].join()", 0, "1,2,3,4" },
    { "function* fib(){ var [a,b]=[0,1]; for(;;){ yield a; [a,b]=[b,a+b] } } var r=[]; for (const x of fib()) { if (x>50) break; r.push(x) } r.join()", 0, "0,1,1,2,3,5,8,13,21,34" },
    { "function* g(){ const x = yield 1; const y = yield x+1; return x+y } var it=g(); it.next(); it.next(10); it.next(20).value", 0, "30" },
    { "function* g(){ for (var i=0;i<3;i++) yield i } var s=0; for (const v of g()) { if (v==2) break; s+=v } s", 0, "1" },
    { "function* g(){ try { yield 1; yield 2; } finally { log.push('fin') } } var log=[]; var it=g(); it.next(); it.return(9); log.join()", 0, "fin" },
    { "function* g(){ try { yield 1; } catch(e){ yield 'c'+e } } var it=g(); it.next(); it.throw('E').value", 0, "cE" },
    { "function* g(){ var i=0; while(true) yield i++; } var it=g(); it.next(); it.next(); it.next().value", 0, "2" },
    { "function* g(){ var x = yield 1; yield x*2; } var it=g(); it.next(); it.next(5).value", 0, "10" },
    { "function* g(){ yield 1 } Object.prototype.toString.call(g())", 0, "[object Generator]" },
    { "function* g(){ yield 1 } g().next.name", 0, "next" },
    { "function* g(){ yield 1; return 5 } var it=g(); it.next(); JSON.stringify(it.next())", 0, "{\"value\":5,\"done\":true}" },
    { "function* g(){ yield 1; yield 2; } [...g()].join()", 0, "1,2" },
    { "function* g(){ yield* [1,2,3] } Array.from(g()).length", 0, "3" },
    { "function* g(){ yield* [1,2]; yield 3; } Array.from(g()).join()", 0, "1,2,3" },
    { "new Error('x',{cause:1}).cause", 0, "1" },
    { "new Function('a','b','return a+b')(1,2)", 0, "3" },
    { "structuredClone({a:[1]}).a[0]", 0, "1" },
    { "typeof Array.prototype.findLast", 0, "function" },
    { "typeof Promise.any", 0, "function" },
    { "typeof Promise.withResolvers", 0, "function" },
    { "typeof eval('1')", 0, "number" },
    { "typeof g", 0, "undefined" },
    { "var d=[]; function* g(){ d.push(1); yield; d.push(2) } var it=g(); it.next(); it.next(); d.join()", 0, "1,2" },
    { "var depth=0; async function rec(n){ if(n==0) return 0; return 1+await rec(n-1) } var r; rec(60).then(v=>r=v);", "r", "60" },
    { "var log=[]; (async()=>{ log.push('a'); await null; log.push('c'); })(); log.push('b');", "log.join()", "a,b,c" },
    { "var log=[]; (async()=>{ log.push('a'); await null; log.push('c'); })(); log.push('b'); log.join()", 0, "a,b" },
    { "var log=[]; Promise.resolve().then(()=>log.push('p1')); (async()=>{ await null; log.push('a1'); })(); Promise.resolve().then(()=>log.push('p2'));", "log.join()", "p1,a1,p2" },
    { "var log=[]; Promise.resolve(1).then(v=>log.push('p'+v)); log.push('s'); log.join()", 0, "s" },
    { "var log=[]; async function f(){ log.push(1); await 0; log.push(3); return 9 } f().then(v=>log.push('r'+v)); log.push(2);", "log.join()", "1,2,3,r9" },
    { "var log=[]; function* g(){ try { yield 1 } finally { log.push('a'); yield 2; log.push('b') } } var it=g(); it.next(); var x=it.return(7); log.join()+JSON.stringify(x)+JSON.stringify(it.next())", 0, "a{\"value\":2,\"done\":false}{\"value\":7,\"done\":true}" },
    { "var n=0; function* g(){ n++; yield 1 } var it=g(); n", 0, "0" },
    { "var o; (async()=>{ var z = 7; await 0; o=z })();", "o", "7" },
    { "var o; (async()=>{ var z = 7; o=z })(); o", 0, "7" },
    { "var o=[]; (async function(){ await 0; var z = 7; o.push('c'+z) })();", "o.join()", "c7" },
    { "var o=[]; (async()=>{ await 0; let z = 7; o.push('b'+z) })();", "o.join()", "b7" },
    { "var o=[]; (async()=>{ await 0; var z = 7; o.push('a'+z) })();", "o.join()", "a7" },
    { "var o=[]; (async()=>{ await 0; var z = 7; o.push('f'+(z+1)) })();", "o.join()", "f8" },
    { "var o=[]; (async()=>{ await 0; var z = 7; var w = z; o.push('g'+w) })();", "o.join()", "g7" },
    { "var o=[]; (async()=>{ await Promise.all([new Promise(r=>setTimeout(r,50))]); o.push('A'); })();", "o.join()", "A" },
    { "var o=[]; (async()=>{ await new Promise(r=>setTimeout(r,5)); o.push('B5'); })();", "o.join()", "B5" },
    { "var o=[]; (async()=>{ await new Promise(r=>setTimeout(r,5)); return 8 })().then(v=>o.push('B7'+v));", "o.join()", "B78" },
    { "var o=[]; (async()=>{ var ps=[1,2].map(async x=>{ await new Promise(r=>setTimeout(r,10)); return x }); o.push('E0'); var rs = await Promise.all(ps); o.push('E'+rs.join()); })();", "o.join()", "E0,E1,2" },
    { "var o=[]; (async()=>{ var rs = await Promise.all([1,2].map(async x=>{ await new Promise(r=>setTimeout(r,10)); return x })); o.push('D'+rs.join()); })();", "o.join()", "D1,2" },
    { "var o=[]; (async()=>{ var rs = await Promise.all([1,2].map(async x=>{ await null; return x })); o.push('C'+rs.join()); })();", "o.join()", "C1,2" },
    { "var o=[]; (async()=>{ var x = await (async()=>{ await new Promise(r=>r()); return 5 })(); o.push('B2'+x); })();", "o.join()", "B25" },
    { "var o=[]; (async()=>{ var x = await (async()=>{ await new Promise(r=>setTimeout(()=>r(),5)); return 5 })(); o.push('B4'+x); })();", "o.join()", "B45" },
    { "var o=[]; (async()=>{ var x = await (async()=>{ await new Promise(r=>setTimeout(r,5)); return 5 })(); o.push('B3'+x); })();", "o.join()", "B35" },
    { "var o=[]; (async()=>{ var x = await (async()=>{ await new Promise(r=>setTimeout(r,50)); return 5 })(); o.push('B'+x); })();", "o.join()", "B5" },
    { "var o=[]; (async()=>{ var x = await (async()=>{ await null; return 5 })(); o.push('B1'+x); })();", "o.join()", "B15" },
    { "var o=[]; (async()=>{ var y = await new Promise(r=>setTimeout(r,5)); var z = 7; o.push('B6'+z); })();", "o.join()", "B67" },
    { "var o=[]; (async()=>{ var z = 7; await 0; o.push('d'+z) })();", "o.join()", "d7" },
    { "var o=[]; (async()=>{ var z; await 0; z = 7; o.push('e'+z) })();", "o.join()", "e7" },
    { "var o=[]; var ps=[1,2].map(async x=>{ await new Promise(r=>setTimeout(r,10)); return x }); Promise.all(ps).then(v=>o.push('F'+v.join()));", "o.join()", "F1,2" },
    { "var o={ *g(){ yield this.x }, x:6 }; [...o.g()][0]", 0, "6" },
    { "var o={ [Symbol.iterator]() { var i=0; return { next:()=>({done:i>=2, value:i++}) }; } }; Array.from(o).join()", 0, "0,1" },
    { "var o={*[Symbol.iterator](){ yield 1; yield 2; }}; [...o].join()", 0, "1,2" },
    { "var r; (async()=>{ r = await (async()=>{ return await 5 })() })();", "r", "5" },
    { "var r; (async()=>{ r = await Promise.race([new Promise(r=>setTimeout(()=>r('slow'),200)), new Promise(r=>setTimeout(()=>r('fast'),10))]) })();", "r", "fast" },
    { "var r; (async()=>{ r = await new Promise(res=>setTimeout(()=>res('late'),100)) })();", "r", "late" },
    { "var r; (async()=>{ try { await (async()=>{ throw 1 })() } catch(e){ r='c'+e } finally { r+='f' } })();", "r", "c1f" },
    { "var r; (async()=>{ try { await Promise.reject(new Error('x')); } catch(e){ r=e.message } })();", "r", "x" },
    { "var r; async function f(){ await 0; function g(n){ return n==0?0:1+g(n-1) } return g(80) } f().then(v=>r=v);", "r", "80" },
    { "var r; async function f(){ await 0; function g(n){ return n==0?0:1+g(n-1) } try { return g(100000) } catch(e){ return e instanceof RangeError } } f().then(v=>r=v);", "r", "true" },
    { "var r; async function f(){ await 0; return 'ñandú'.replace(/ñ/g,'n').toUpperCase() } f().then(v=>r=v);", "r", "NANDÚ" },
    { "var r; async function f(){ await 0; return JSON.stringify(Array.from({length:200},(_, i)=>({a:[i,{b:i}]}))).length } f().then(v=>r=v);", "r", "4181" },
    { "var r; async function f(){ await 0; return JSON.stringify(Array.from({length:3},(_, i)=>({a:[i,{b:i}]}))) } f().then(v=>r=v);", "r", "[{\"a\":[0,{\"b\":0}]},{\"a\":[1,{\"b\":1}]},{\"a\":[2,{\"b\":2}]}]" },
    { "var r; async function f(){ document.body.innerHTML='<p id=z>x</p>'; await 0; document.getElementById('z').textContent='y'; return document.getElementById('z').textContent } f().then(v=>r=v);", "r", "y" },
    { "var r; async function f(){ return JSON.stringify(Array.from({length:200},(_, i)=>({a:[i,{b:i}]}))).length } f().then(v=>r=v);", "r", "4181" },
    { "var r; async function f(){ throw new TypeError('t') } f().catch(e=>r=e instanceof TypeError);", "r", "true" },
    { "var r; async function* ag(){ yield 'a' } var it=ag(); it.next().then(v=>r=JSON.stringify(v));", "r", "{\"value\":\"a\",\"done\":false}" },
    { "var r; class K{ async m(){ await 0; return this.v } constructor(){this.v=7} } new K().m().then(v=>r=v);", "r", "7" },
    { "var r; function* gen(){ function g(n){ return n==0?0:1+g(n-1) } try { yield g(100000) } catch(e){ yield e instanceof RangeError } } r=gen().next().value; r", 0, "true" },
    { "var r; var o={ async m(){ return await 3 } }; o.m().then(v=>r=v);", "r", "3" },
    { "var r; var p=new Promise(res=>res(5)); (async()=>{ r=(await p)+(await p) })();", "r", "10" },
    { "var r=0; (async()=>{ for (let i=0;i<5;i++){ r+=await Promise.resolve(i) } })();", "r", "10" },
    { "var r=[]; (async function(){ for (const x of [1,2,3]) { await null; r.push(x) } })();", "r.join()", "1,2,3" },
    { "var r=[]; (async()=>{ const [a,b]=await Promise.all([1,Promise.resolve(2)]); r.push(a+b) })();", "r[0]", "3" },
    { "var r=[]; (async()=>{ for await (const v of [Promise.resolve(1),2]) r.push(v) })();", "r.join()", "1,2" },
    { "var r=[]; async function a(){ r.push('a1'); await b(); r.push('a2') } async function b(){ r.push('b1'); await null; r.push('b2') } a(); r.push('main');", "r.join()", "a1,b1,main,b2,a2" },
    { "var r=[]; async function* ag(){ try { yield 1; yield 2 } finally { r.push('f') } } (async()=>{ for await (const v of ag()) { break } })();", "r.join()", "f" },
    { "var r=[]; async function* ag(){ yield 1; await null; yield 2 } (async()=>{ for await (const v of ag()) r.push(v) })();", "r.join()", "1,2" },
    { "var r=[]; for (var i=0;i<400;i++){ (async()=>{ await null; r.push(i) })() }", "r.length", "400" },
    { "var s=0; function* g(n){ for(var i=0;i<n;i++) yield i } for (var k=0;k<50;k++){ for (const v of g(20)) s+=v } s", 0, "9500" },
    { "var x=1; { let x=2 } x", 0, "1" },
    { "var {length}='abc'; length", 0, "3" },
};
/* con soma.jsco=off el motor se comporta como antes del parche: await en sincrono y sin generadores */
static const struct tc OFF[] = {
    { "var r=[]; async function f(){ r.push(1); var v = await 5; r.push(v) } f(); r.join()", 0, "1,5" },
    { "var r; (async()=>{ var z = 7; await 0; r=z })(); r", 0, "7" },
    { "var r; try { var g=(function*(){ yield 1 })(); r=typeof g } catch(e) { r='EXC' } r", 0, "EXC" },
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
    g_jsco_off = 0; fail += run(CASES, (int)(sizeof CASES / sizeof CASES[0]), "co");
    g_jsco_off = 1; fail += run(OFF, (int)(sizeof OFF / sizeof OFF[0]), "off");
    if (fail) { printf("test_web_js_183: %d fallos\n", fail); return 1; }
    printf("test_web_js_183: %d casos OK (corrutinas) + %d (modo off)\n", (int)(sizeof CASES / sizeof CASES[0]), (int)(sizeof OFF / sizeof OFF[0]));
    return 0;
}
