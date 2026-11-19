/* SOMA_186: pruebas en el anfitrion de los datos binarios del motor JavaScript: ArrayBuffer (slice, resize, transfer), arrays tipados (conversion de
 * elementos, vistas que comparten memoria, Float32 con redondeo real, ordenacion, iteracion), DataView (orden de bytes), TextEncoder/TextDecoder
 * (UTF-8, UTF-16, latin1, BOM, fatal, streaming), Blob/File/FileReader, cuerpos de Response, fetch de data:/blob:, y el modo soma.jsbin=off.
 * Se compila con ASan/UBSan. */
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
extern int g_ju16_off, g_jbin_off;
static void jlog(void *ud, int level, const char *msg) { (void)ud; (void)level; (void)msg; }
static void dirty(void *ud, int v) { (void)ud; (void)v; }

struct tc { const char *src, *post, *exp; };
static const struct tc CASES[] = {
    { "new Uint8Array(4).length", 0, "4" },
    { "new Uint8Array([1,2,3]).join()", 0, "1,2,3" },
    { "var a=new Uint8Array(2); a[0]=300; a[1]=-1; a.join()", 0, "44,255" },
    { "var a=new Int8Array(2); a[0]=200; a[1]=-129; a.join()", 0, "-56,127" },
    { "var a=new Uint8ClampedArray(4); a[0]=300; a[1]=-5; a[2]=1.5; a[3]=2.5; a.join()", 0, "255,0,2,2" },
    { "var a=new Int16Array(1); a[0]=40000; a[0]", 0, "-25536" },
    { "var a=new Uint32Array(1); a[0]=-1; a[0]", 0, "4294967295" },
    { "var a=new Int32Array(1); a[0]=2147483648; a[0]", 0, "-2147483648" },
    { "var a=new Float32Array(1); a[0]=0.1; a[0]", 0, "0.10000000149011612" },
    { "var a=new Float32Array(1); a[0]=16777217; a[0]", 0, "16777216" },
    { "var a=new Float32Array(1); a[0]=1e40; a[0]", 0, "Infinity" },
    { "var a=new Float32Array(1); a[0]=1e-50; a[0]", 0, "0" },
    { "var a=new Float32Array(1); a[0]=1.401298464324817e-45; a[0]", 0, "1.401298464324817e-45" },
    { "var a=new Float64Array(2); a[0]=Math.PI; a[1]=-0; a[0]+','+(1/a[1])", 0, "3.141592653589793,-Infinity" },
    { "Math.fround(5.5)", 0, "5.5" },
    { "Math.fround(5.05)", 0, "5.050000190734863" },
    { "var b=new ArrayBuffer(8); var u=new Uint8Array(b); var d=new DataView(b); d.setUint32(0,0x01020304); u.join()", 0, "1,2,3,4,0,0,0,0" },
    { "var b=new ArrayBuffer(8); var d=new DataView(b); d.setUint32(0,0x01020304,true); new Uint8Array(b).join()", 0, "4,3,2,1,0,0,0,0" },
    { "var b=new ArrayBuffer(8); var d=new DataView(b); d.setFloat64(0,1.5); d.getFloat64(0)", 0, "1.5" },
    { "var b=new ArrayBuffer(8); var d=new DataView(b); d.setFloat32(0,-2.25,true); d.getFloat32(0,true)", 0, "-2.25" },
    { "var b=new ArrayBuffer(8); var d=new DataView(b); d.setInt16(2,-2); d.getUint16(2)", 0, "65534" },
    { "var b=new ArrayBuffer(8); var f=new Float32Array(b); var u=new Uint32Array(b); f[0]=1; u[0].toString(16)", 0, "3f800000" },
    { "var b=new ArrayBuffer(8); var u=new Uint8Array(b,2,4); u.byteOffset+':'+u.length+':'+u.byteLength", 0, "2:4:4" },
    { "var b=new ArrayBuffer(8); new Uint8Array(b).set([1,2,3],2); new Uint8Array(b).join()", 0, "0,0,1,2,3,0,0,0" },
    { "var b=new ArrayBuffer(8); var a=new Uint8Array(b), c=new Uint16Array(b); a[0]=1; a[1]=1; c[0]", 0, "257" },
    { "var a=new Uint8Array([5,6,7,8]); var s=a.subarray(1,3); s[0]=99; a.join()", 0, "5,99,7,8" },
    { "var a=new Uint8Array([5,6,7,8]); var s=a.slice(1,3); s[0]=99; a.join()+'|'+s.join()", 0, "5,6,7,8|99,7" },
    { "new Uint8Array([1,2,3]).map(function(x){return x*2}).join()", 0, "2,4,6" },
    { "new Uint8Array([1,2,3,4]).filter(function(x){return x%2==0}).join()", 0, "2,4" },
    { "new Uint8Array([1,2,3]).reduce(function(a,b){return a+b})", 0, "6" },
    { "new Uint8Array([3,1,2]).sort().join()", 0, "1,2,3" },
    { "new Float32Array([3,NaN,1,-0,0]).sort().join()", 0, "0,0,1,3,NaN" },
    { "new Int8Array([3,1,2]).sort(function(a,b){return b-a}).join()", 0, "3,2,1" },
    { "new Uint8Array([1,2,3]).reverse().join()", 0, "3,2,1" },
    { "new Uint8Array([1,2,3]).indexOf(2)", 0, "1" },
    { "new Uint8Array([1,2,3]).includes(4)", 0, "false" },
    { "new Float64Array([NaN]).includes(NaN)", 0, "true" },
    { "new Uint8Array([1,2,3]).at(-1)", 0, "3" },
    { "new Uint8Array([1,2,3]).fill(7,1).join()", 0, "1,7,7" },
    { "new Uint8Array([1,2,3,4,5]).copyWithin(0,3).join()", 0, "4,5,3,4,5" },
    { "new Uint8Array([1,2,3]).find(function(x){return x>1})", 0, "2" },
    { "new Uint8Array([1,2,3]).findLastIndex(function(x){return x<3})", 0, "1" },
    { "new Uint8Array([1,2,3]).every(function(x){return x>0})", 0, "true" },
    { "new Uint8Array([1,2,3]).some(function(x){return x>2})", 0, "true" },
    { "var s=0; new Uint8Array([1,2,3]).forEach(function(x){s+=x}); s", 0, "6" },
    { "[...new Uint8Array([1,2,3])].join()", 0, "1,2,3" },
    { "Array.from(new Uint8Array([1,2,3])).join()", 0, "1,2,3" },
    { "Array.from(new Uint8Array([1,2,3]), function(x){return x*2}).join()", 0, "2,4,6" },
    { "var o=[]; for (var x of new Uint8Array([4,5])) o.push(x); o.join()", 0, "4,5" },
    { "var o=[]; for (var k in new Uint8Array([4,5])) o.push(k); o.join()", 0, "0,1" },
    { "Object.keys(new Uint8Array([4,5])).join()", 0, "0,1" },
    { "JSON.stringify(new Uint8Array([4,5]))", 0, "{\"0\":4,\"1\":5}" },
    { "Uint8Array.from([1,2,3]).join()", 0, "1,2,3" },
    { "Uint8Array.of(1,2,3).length", 0, "3" },
    { "Uint8Array.from('123').join()", 0, "1,2,3" },
    { "Uint8Array.BYTES_PER_ELEMENT + ':' + Float64Array.BYTES_PER_ELEMENT", 0, "1:8" },
    { "new Uint16Array(3).BYTES_PER_ELEMENT", 0, "2" },
    { "new Uint16Array(3).byteLength", 0, "6" },
    { "Object.prototype.toString.call(new Uint8Array(1))", 0, "[object Uint8Array]" },
    { "Object.prototype.toString.call(new ArrayBuffer(1))", 0, "[object ArrayBuffer]" },
    { "Object.prototype.toString.call(new DataView(new ArrayBuffer(1)))", 0, "[object DataView]" },
    { "new Uint8Array(2) instanceof Uint8Array", 0, "true" },
    { "ArrayBuffer.isView(new Uint8Array(1)) + ',' + ArrayBuffer.isView(new ArrayBuffer(1)) + ',' + ArrayBuffer.isView([])", 0, "true,false,false" },
    { "Array.isArray(new Uint8Array(2))", 0, "false" },
    { "new ArrayBuffer(10).slice(2,6).byteLength", 0, "4" },
    { "new ArrayBuffer(10).slice(-3).byteLength", 0, "3" },
    { "var b=new ArrayBuffer(4); new Uint8Array(b)[1]=9; new Uint8Array(b.slice(1))[0]", 0, "9" },
    { "var t=new ArrayBuffer(4); var u=new Uint8Array(t); u[0]=5; var n=t.transfer(); t.byteLength+':'+n.byteLength+':'+new Uint8Array(n)[0]+':'+t.detached", 0, "0:4:5:true" },
    { "var b=new ArrayBuffer(2,{maxByteLength:8}); var u=new Uint8Array(b); b.resize(6); b.resizable+':'+u.length", 0, "true:6" },
    { "var u=new Uint8Array(4); u[10]=5; u[10]+':'+u.length", 0, "undefined:4" },
    { "var u=new Uint8Array(4); 2 in u", 0, "true" },
    { "var u=new Uint8Array(4); 4 in u", 0, "false" },
    { "var r=false; try { new Uint8Array(-1) } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "var r=false; try { new Uint16Array(new ArrayBuffer(3)) } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "var r=false; try { new Uint16Array(new ArrayBuffer(4),1) } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "var r=false; try { Uint8Array(2) } catch(e) { r = e instanceof TypeError } r", 0, "true" },
    { "var r=false; try { new DataView(new ArrayBuffer(2)).getInt32(0) } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "var r=false; try { new ArrayBuffer(1e12) } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "new Uint8Array(new Uint16Array([257,2])).join()", 0, "1,2" },
    { "new Uint16Array(new Uint8Array([1,2])).join()", 0, "1,2" },
    { "new Uint8Array({length:2,0:7,1:8}).join()", 0, "7,8" },
    { "new Uint8Array(new Set([1,2])).join()", 0, "1,2" },
    { "new Uint8Array(3).toString()", 0, "0,0,0" },
    { "new Float32Array([1.5,2.5]).toString()", 0, "1.5,2.5" },
    { "new Uint8Array([1,2]).with(0,9).join()", 0, "9,2" },
    { "new Uint8Array([2,1]).toSorted().join()", 0, "1,2" },
    { "new Uint8Array([1,2]).toReversed().join()", 0, "2,1" },
    { "var a=new Uint8Array([1,2,3]); var it=a.entries().next().value; it.join()", 0, "0,1" },
    { "var a=new Uint8Array([1,2,3]); [...a.keys()].join()", 0, "0,1,2" },
    { "class M extends Uint8Array { sum(){ var s=0; for (var i=0;i<this.length;i++) s+=this[i]; return s } } new M([1,2,3]).sum()", 0, "6" },
    { "structuredClone(new Uint8Array([1,2,3])).join()", 0, "1,2,3" },
    { "var a=new Uint8Array([1,2,3]); var c=structuredClone(a); c[0]=9; a[0]", 0, "1" },
    { "new TextEncoder().encode('héllo').join()", 0, "104,195,169,108,108,111" },
    { "new TextEncoder().encode('😀').join()", 0, "240,159,152,128" },
    { "new TextEncoder().encode('\\uD83D').join()", 0, "239,191,189" },
    { "new TextDecoder().decode(new Uint8Array([104,195,169]))", 0, "hé" },
    { "new TextDecoder().decode(new Uint8Array([240,159,152,128])) === '😀'", 0, "true" },
    { "new TextDecoder().decode(new Uint8Array([0xFF,65]))", 0, "�A" },
    { "new TextDecoder().decode(new Uint8Array([0xE2,0x82]))", 0, "�" },
    { "new TextDecoder().decode(new Uint8Array([0xEF,0xBB,0xBF,65]))", 0, "A" },
    { "new TextDecoder('utf-8',{ignoreBOM:true}).decode(new Uint8Array([0xEF,0xBB,0xBF,65])).length", 0, "2" },
    { "var r=false; try { new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array([0xFF])) } catch(e) { r = e instanceof TypeError } r", 0, "true" },
    { "new TextDecoder('utf-16le').decode(new Uint8Array([104,0,233,0]))", 0, "hé" },
    { "new TextDecoder('latin1').decode(new Uint8Array([233,128]))", 0, "é€" },
    { "new TextDecoder().decode(new Uint8Array([0xE2,0x82]).buffer)", 0, "�" },
    { "var d=new TextDecoder(); d.decode(new Uint8Array([0xE2,0x82]),{stream:true})+d.decode(new Uint8Array([0xAC]))", 0, "€" },
    { "new TextDecoder().encoding", 0, "utf-8" },
    { "var r=false; try { new TextDecoder('nope') } catch(e) { r = e instanceof RangeError } r", 0, "true" },
    { "var u=new Uint8Array(4); var r=new TextEncoder().encodeInto('hé😀',u); r.read+':'+r.written", 0, "2:3" },
    { "var u=new Uint8Array(8); var r=new TextEncoder().encodeInto('hé😀',u); r.read+':'+r.written", 0, "4:7" },
    { "new Blob(['héllo']).size", 0, "6" },
    { "new Blob([new Uint8Array([1,2,3])]).size", 0, "3" },
    { "new Blob([new Uint8Array([1,2,3]).buffer, 'ab']).size", 0, "5" },
    { "new Blob([new Blob(['ab']), 'c']).size", 0, "3" },
    { "new Blob(['abc'],{type:'Text/Plain'}).type", 0, "text/plain" },
    { "new Blob(['abcdef']).slice(1,4).size", 0, "3" },
    { "new Blob(['abcdef']).slice(-2).size", 0, "2" },
    { "var r; new Blob(['héllo']).text().then(function(t){r=t})", "r", "héllo" },
    { "var r; new Blob([new Uint8Array([1,2,3])]).arrayBuffer().then(function(b){r=new Uint8Array(b).join()})", "r", "1,2,3" },
    { "var r; var f=new FileReader(); f.onload=function(){}; f.addEventListener('load',function(){r=f.result}); f.readAsDataURL(new Blob(['hi'],{type:'text/plain'}))", "r", "data:text/plain;base64,aGk=" },
    { "var r; var f=new FileReader(); f.addEventListener('load',function(){r=f.result.byteLength}); f.readAsArrayBuffer(new Blob(['hi']))", "r", "2" },
    { "var r; new Response(new Uint8Array([104,105])).text().then(function(t){r=t})", "r", "hi" },
    { "var r; new Response(new Blob(['abc'])).arrayBuffer().then(function(b){r=b.byteLength})", "r", "3" },
    { "var r; new Response('héllo').arrayBuffer().then(function(b){r=b.byteLength})", "r", "6" },
    { "var r; new Response(new Uint8Array([1,2,3])).arrayBuffer().then(function(b){r=new Uint8Array(b).join()})", "r", "1,2,3" },
    { "var r; fetch('data:text/plain;base64,aGVsbG8=').then(function(x){return x.text()}).then(function(t){r=t})", "r", "hello" },
    { "var r; fetch('data:application/octet-stream;base64,AQID').then(function(x){return x.arrayBuffer()}).then(function(b){r=new Uint8Array(b).join()})", "r", "1,2,3" },
    { "var r; var u=URL.createObjectURL(new Blob(['xyz'])); fetch(u).then(function(x){return x.text()}).then(function(t){r=t})", "r", "xyz" },
    { "btoa('abc')", 0, "YWJj" },
    { "atob('YWJj')", 0, "abc" },
    { "typeof Atomics.add", 0, "function" },
    { "var a=new Int32Array(2); Atomics.add(a,0,5); Atomics.load(a,0)", 0, "5" },
    { "var a=new Uint8Array(1<<20); for (var i=0;i<a.length;i++) a[i]=i; a[777777]", 0, "49" },
    { "var a=new Float64Array(100000); for (var i=0;i<a.length;i++) a[i]=(i*7919)%10007; a.sort(); a[0]+':'+a[99999]", 0, "0:10006" },
    { "var a=new Uint32Array(200000); for (var i=0;i<a.length;i++) a[i]=(i*2654435761)>>>0; a.sort(); var ok=true; for (var i=1;i<a.length;i++) if (a[i-1]>a[i]) ok=false; ok", 0, "true" },
    { "var n=0, r='none'; try { var keep=[]; for (var i=0;i<100;i++) { keep.push(new ArrayBuffer(4<<20)); n++; } } catch(e) { r = e instanceof RangeError } n > 1 && r === true", 0, "true" },
    { "var b=new ArrayBuffer(1<<20); var u=new Uint8Array(b); u.fill(7); var s=0; u.forEach(function(x){s+=x}); s", 0, "7340032" },
    { "var u=new Uint8Array(1<<20); u.set(new Uint8Array(1<<20).fill(3)); u[500000]", 0, "3" },
    { "new TextDecoder().decode(new TextEncoder().encode('a'.repeat(1000000))).length", 0, "1000000" },
};
/* con soma.jsbin=off se conservan los arrays disfrazados del prelude anterior */
static const struct tc OFF[] = {
    { "typeof DataView", 0, "undefined" },
    { "typeof Float32Array", 0, "function" },
    { "new Uint8Array([1,2,3]).join()", 0, "1,2,3" },
    { "var a=new Uint8Array(2); a[0]=300; a[0]", 0, "300" },
    { "new Uint8Array(4).buffer.byteLength", 0, "4" },
    { "typeof ArrayBuffer.isView", 0, "function" },
    { "typeof Atomics", 0, "undefined" },
    { "new Blob(['abc']).size", 0, "3" },
    { "typeof __utf8enc", 0, "undefined" },
    { "typeof __nativeBin", 0, "undefined" },
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
    g_ju16_off = 0; g_jbin_off = 0; fail += run(CASES, (int)(sizeof CASES / sizeof CASES[0]), "bin");
    g_jbin_off = 1; fail += run(OFF, (int)(sizeof OFF / sizeof OFF[0]), "off");
    if (fail) { printf("test_web_js_186: %d fallos\n", fail); return 1; }
    printf("test_web_js_186: %d casos OK (binarios) + %d (modo off)\n", (int)(sizeof CASES / sizeof CASES[0]), (int)(sizeof OFF / sizeof OFF[0]));
    return 0;
}
