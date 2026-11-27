#!/bin/sh
# SOMA_274_JS: compila y ejecuta en el anfitrion (Linux x86_64) el programa de ring 3 REAL de la orden js (src/user/jsrun.c) con el
# motor compilado como en SOMA (cambio de pila incluido) y comprueba salida y codigo de salida. Uso: sh tests/jsrun_host_274/run.sh
set -e
cd "$(dirname "$0")/../.."
if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then echo "== jsrun_host_274: solo en Linux x86_64: SE OMITE"; exit 0; fi
O=$(mktemp -d); WS=$O/ws; mkdir -p "$WS"; export JSR_WS=$WS
INC="-Isrc/core -Isrc/apps -Isrc/net -Isrc/arch/x86_64 -Isrc/drivers -Isrc/ui -Isrc/fs -Isrc/fonts -Isrc/icons -Isrc/lang -Isrc/agent -Isrc -Isrc/user"
F="-m64 -O2 -w -fno-stack-protector -fno-strict-aliasing -fwrapv -USOMA_WEB_ASYNC_IMAGES -DSOMA_WEB_ASYNC_IMAGES=0 -DSOMA_WEBSVC=1"
for f in src/apps/jsrun_core.c src/apps/web_html.c src/apps/web_css.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c; do
  ${CC:-gcc} $F $INC -c "$f" -o "$O/$(basename "$f" .c).o" & done; wait
${CC:-gcc} $F $INC -Itests/jsrun_host_274 -c tests/jsrun_host_274/harness.c -o "$O/harness.o"
${CC:-gcc} -no-pie "$O"/*.o -Wl,--allow-multiple-definition -o "$O/jsh"
H=$O/jsh; ok=0; bad=0
# caso NOMBRE CODIGO_ESPERADO SALIDA_ESPERADA ARGS [ARCHIVO] [VENTANA] [STDIN]
caso() {
  n=$1; ec=$2; exp=$3; shift 3
  got=$(printf '%s' "${STDIN_TXT:-}" | "$H" "$@" 2>"$O/err") && rc=0 || rc=$?
  if [ "$rc" = "$ec" ] && [ "$got" = "$exp" ]; then ok=$((ok+1)); else bad=$((bad+1)); echo "FAIL $n: codigo $rc (esperado $ec), salida [$got] (esperada [$exp])"; fi
}
caso "-c print"        0 "2"                 '-c print(1+1)'
caso "console.log"     0 "a [ 1, 2 ] { k: 1 }" '-c console.log("a",[1,2],{k:1})'
caso "clase privada"   0 "3"                 '-c class P{#x=3;get x(){return this.#x}} print(new P().x)'
caso "async/timers"    0 "b
a"                                           '-c setTimeout(()=>print("a"),40); (async()=>{await new Promise(r=>setTimeout(r,5)); print("b")})()'
caso "generadores"     0 "1,2,3"             '-c function* g(){yield 1;yield 2;yield 3} print([...g()].join())'
caso "bigint"          0 "18446744073709551616" '-c print(2n**64n)'
caso "error"           1 "[error] Uncaught TypeError: No se pueden leer propiedades de null (leyendo 'x') (l:1) en <cadena>:1" '-c null.x'
caso "exit(7)"         7 "1"                 '-c print(1); exit(7); print(2)'
caso "no existe"       2 "js: no existe /workspace/nada.js" 'nada.js'
caso "sin args"        2 "uso: js archivo.js | js -c \"codigo\"" ''
printf 'print(fs.write("/workspace/o.txt","dato"), fs.read("o.txt"), fs.exists("zz"), fs.write("/etc/x","no"), fs.read("/etc/passwd"));\n' > "$WS/a.js"
caso "archivos"        0 "true dato false false null" 'a.js'
mkdir -p "$WS/lib"; printf 'exports.dobla = x => x * 2;\n' > "$WS/lib/util.js"; printf 'const u = require("./lib/util"); print(u.dobla(21)); try { require("./nada") } catch (e) { print(e.message) }\n' > "$WS/r.js"
caso "require"         0 "42
no existe el modulo ./nada (buscado en /workspace/nada)" 'r.js'
printf 'import x from "./m.js"\n' > "$WS/i.js"
caso "import da pista" 1 "[error] Uncaught SyntaxError: los modulos ES (import/export) no estan soportados (col 8: import x from \"./m.js\" ) (i.js, linea 1) en <anonymous>  -> en js usa require('./modulo.js') y module.exports" 'i.js'
printf 'let a = (\n' > "$O/mal.js"; printf 'function f(){ return 1 }\n' > "$O/bien.js"; printf 'print("x:", 6*7)\n' > "$O/x.js"
caso "-s sintaxis mal" 1 "[error] Uncaught SyntaxError: token inesperado (col 1: ) (programa.js, linea 2) (l:1) en programa.js:1" -s "$O/mal.js"
caso "-s sintaxis bien" 0 "OK"              -s "$O/bien.js"
caso "-s no ejecuta"   0 "OK"                -s "$O/x.js"
caso "-x ejecuta"      0 "x: 42"             -x "$O/x.js"
STDIN_TXT='Ana
'; caso "input"        0 "N: Hola Ana"       '-c print("Hola "+input("N: "))'
STDIN_TXT='';          caso "input sin mas"  0 "null"              '-c print(input())'
caso "recursion"       0 "RangeError"        '-c function f(n){return f(n+1)+1} try{f(0)}catch(e){print(e.name)}'
caso "memoria agotada" 0 "cazado RangeError" '-c const a=[]; try{ for(;;) a.push(new Array(100000).fill(1)) }catch(e){ print("cazado", e.name) }'
printf 'let t=0; for (let i=0;i<200;i++){ const a=[]; for(let j=0;j<2000;j++) a.push({i,j,s:"x"+j}); t+=a.length } print(t); const m=new Map(); for(let i=0;i<30000;i++) m.set("k"+i,i); print(m.size)\n' > "$WS/gc.js"
caso "recolector"      0 "400000
30000"                                      'gc.js'
cat > "$WS/w.js" <<'X'
win.create("t",300,200); let n=0; while(win.active()){ win.clear(0); win.rect(n%200,10,20,20,255); win.text(5,5,"hi",0xFFFFFF); win.present(); sleep(1); n++ } print("frames",n)
X
caso "ventana"         0 "frames 6
[win cerrada draws=84 presents=6]" 'w.js' '' 6
printf 'print(typeof win.mouse().x, win.key()===win.UP, win.size().join("x"), win.rgb(1,2,3)); win.close()\n' > "$WS/w2.js"
caso "win.mouse/key"   0 "number true 300x200 66051
[win cerrada draws=0 presents=0]" 'w2.js' '' 3
# SOMA_344_JS: cadenas con la huella perezosa: construidas con +=, usadas como claves de Map, nombres de propiedad, ===, switch,
# Object.keys, JSON y la cadena vacia; y que un += de 20000 vueltas (montar HTML) no tarde segundos
cat > "$WS/cad344.js" <<'FINJS'
var s = ""; for (var i = 0; i < 3000; i++) s += "ab" + i;
var m = new Map(); m.set("k" + "1", 5);
var o = {}; o["x" + "y"] = 1;
var t = ""; for (var j = 0; j < 3; j++) t += (j ? "," : "") + "k" + j;
var w = {}; w[t.split(",")[0]] = 1; w["k1"] = 1; w["k" + 2] = 1;
var sw; switch ("a" + "b") { case "ab": sw = "si"; break; default: sw = "no"; }
print(s.length, m.get("k1"), o.xy, ("a" + "b") === "ab", JSON.parse(JSON.stringify({ ["c" + "d"]: 2 })).cd, sw,
      JSON.stringify({ ["c" + "d"]: 2 }), Object.keys(w).join(), ("" + "") === "", ("ab" + "cdefg").length, m.has("k" + 1), "k1" in w);
FINJS
caso "cadenas 344"     0 '16890 5 1 true 2 si {"cd":2} k0,k1,k2 true 7 true true' 'cad344.js'
cat > "$WS/html344.js" <<'FINJS'
var s = ""; for (var i = 0; i < 20000; i++) s += '<li class="item-' + (i % 7) + '">' + i + "</li>"; print(s.length);
FINJS
t0=$(date +%s%N); caso "+= de 20000"  0 "568890" 'html344.js'
dt=$(( ($(date +%s%N) - t0) / 1000000 )); echo "   (+= de 20000 vueltas: $dt ms)"
# SOMA_345_NAVEGADOR_REAL: lo que faltaba de JavaScript moderno (yarnpkg.com, hecho con esbuild, se rompia con
# "__getOwnPropDescs no es una funcion") y los simbolos en Reflect.ownKeys / getOwnPropertySymbols / Object.assign / {...o}
cat > "$WS/es345.js" <<'FINJS'
var o = { a: 1, get b() { return 2; } }, s = Symbol("s"); o[s] = 3;
var d = Object.getOwnPropertyDescriptors(o), sp = Object.defineProperties({}, Object.getOwnPropertyDescriptors({ x: 1, y: 2 }));
var gb = Object.groupBy([1, 2, 3, 4, 5], function (n) { return n % 2 ? "impar" : "par"; }), mg = Map.groupBy([1, 2, 3], function (n) { return n > 1; });
var a = [1, 2, 3, 4], ts = a.toSpliced(1, 2, 9);
var u = new Set([1, 2, 3]).union(new Set([3, 4])), it = new Set([1, 2, 3]).intersection(new Set([2, 3, 9])), df = new Set([1, 2, 3]).difference(new Set([2]));
var w = {}; w.__defineGetter__("k", function () { return 7; });
print(typeof d.b.get, d.a.value, d[s].value, sp.x + sp.y, gb.impar.join(), gb.par.join(), Object.getPrototypeOf(gb) === null, mg.get(true).join(),
      ts.join(), a.join(), [...u].join(), [...it].join(), [...df].join(), new Set([1]).isSubsetOf(new Set([1, 2])), new Set([1]).isDisjointFrom(new Set([2])),
      "ab".isWellFormed(), "\u{1F600}x".toWellFormed() === "\u{1F600}x", w.k, typeof w.__lookupGetter__("k"));
Array.fromAsync([Promise.resolve(1), 2]).then(function (r) { print("fromAsync", r.join()); });
var c1 = new AbortController(), c2 = new AbortController(), any = AbortSignal.any([c1.signal, c2.signal]); c2.abort("x"); print("any", any.aborted, any.reason);
var k = Symbol("k"), src = { x: 1 }; src[k] = 2; Object.defineProperty(src, "oculta", { value: 3, enumerable: false });
var cp = { ...src }, as = Object.assign({}, src), el = document.createElement("div"); el.style.color = "red"; el.classList.add("c");
print(Reflect.ownKeys(src).length, Object.getOwnPropertySymbols(src).length, cp[k], as[k], Object.keys(cp).join(), Object.getOwnPropertySymbols(el).length,
      Reflect.ownKeys(el).indexOf("@@st"), Object.getOwnPropertySymbols(Array.prototype).length > 0);
FINJS
caso "es2024 345"      0 'function 1 3 3 1,3,5 2,4 true 2,3 1,9,4 1,2,3,4 1,2,3,4 2,3 1,3 true true true true 7 function
any true x
3 1 2 2 x 0 -1 true
fromAsync 1,2' 'es345.js'
# SOMA_346_NAVEGADOR_RENDER: el "prototype" de una funcion se creaba al leerlo por primera vez; Object.defineProperty(F, "prototype",
# {writable: false}) sin haberlo leido (lo hace Babel en cada clase) lo dejaba en undefined: "Cannot call a class as a function" (20minutos)
cat > "$WS/proto346.js" <<'FINJS'
function rt(t) { Object.defineProperty(t, "prototype", { writable: false }); return t; }
var A = rt((function t() { if (!(this instanceof t)) throw new TypeError("Cannot call a class as a function"); this.v = 1; }));
var a = new A(); function f() {} function g() {} function h() {}
var d = Object.getOwnPropertyDescriptor(f, "prototype");
print(a.v, a instanceof A, typeof d.value, d.writable, g.hasOwnProperty("prototype"), Object.getOwnPropertyNames(h).indexOf("prototype") >= 0,
      Reflect.defineProperty(function k() {}, "prototype", { enumerable: false }), Object.getOwnPropertyDescriptor(() => 1, "prototype") === undefined);
FINJS
caso "prototype 346"   0 '1 true object true true true true true' 'proto346.js'
# SOMA_346: window hereda de Window.prototype y document de HTMLDocument.prototype (la biblioteca Arrive anade metodos ahi)
cat > "$WS/win346.js" <<'FINJS'
HTMLDocument.prototype.zz346 = function () { return 1; }; Window.prototype.ww346 = function () { return 2; };
print(document instanceof HTMLDocument, document instanceof Document, window instanceof Window, window instanceof EventTarget,
      document.zz346(), window.ww346(), typeof window.addEventListener);
FINJS
caso "window 346"      0 'true true true true 1 2 function' 'win346.js'
# SOMA_347_NAVEGADOR_INTL: Intl sin new, metodos en el prototipo, resolvedOptions().timeZone (20minutos), formatos es/en
cat > "$WS/intl347.js" <<'FINJS'
var d = new Date(Date.UTC(2026, 10, 26, 21, 5, 9));
print(typeof Intl.DateTimeFormat().resolvedOptions().timeZone, new Intl.DateTimeFormat('es-ES').format(d), [d].map(new Intl.DateTimeFormat('en-US').format).join(),
      typeof Intl.DateTimeFormat.prototype.resolvedOptions, Object.prototype.toString.call(Intl.NumberFormat()));
var rtf = new Intl.RelativeTimeFormat('es', { numeric: 'auto' });
print(rtf.format(-1, 'day'), '/', rtf.format(-3, 'minutes'), '/', new Intl.RelativeTimeFormat('en').format(2, 'hour'), '/', new Intl.ListFormat('es').format(['a', 'b', 'c']),
      '/', Intl.NumberFormat('es-ES', { style: 'currency', currency: 'EUR' }).format(12.5).replace(/\u00a0/g, '_'), '/', new Intl.PluralRules('en', { type: 'ordinal' }).select(22));
FINJS
caso "intl 347"        0 'string 26/11/2026 11/26/2026 function [object Intl.NumberFormat]
ayer / hace 3 minutos / in 2 hours / a, b y c / 12,50_€ / two' 'intl347.js'
cat > "$WS/san347.js" <<'FINJS'
var d = document.createElement('div'); d.innerHTML = '<p><span>x</span></p>';
var f = document.createDocumentFragment(); f.appendChild(document.createElement('b')).appendChild(document.createElement('i'));
d.setHTML('<b onclick="x()">hola</b><script>bad()</scr' + 'ipt><a href="javascript:alert(1)">l</a><img src=x onerror=y>');
var st = document.createElement('style'); st.setHTML('p{color:red}');
var u = document.createElement('div'); u.setHTMLUnsafe('<i onclick="z">u</i>');
print(f.querySelectorAll('*').length, f.querySelector('i').tagName, document.querySelectorAll('*').length, '|', d.innerHTML, '|', st.textContent, '|', u.innerHTML, '|', Document.parseHTML('<p onclick=1>x</p>').body.innerHTML, typeof Sanitizer);
FINJS
caso "sanitizer 347"   0 '2 I 3 | <b>hola</b><a>l</a><img src="x"> | p{color:red} | <i onclick="z">u</i> | <p>x</p> function' 'san347.js'
cat > "$WS/ff348.js" <<'FINJS'
var f = new FontFace("X", "url(a.woff2)", { weight: "700" }), e;
try { FontFace("Y", "u"); } catch (x) { e = x.name; }
f.load().then(function (r) { print(r === f, r.status, r.weight, typeof document.fonts.ready.then, e); });
FINJS
caso "fontface 348"    0 'true loaded 700 function TypeError' 'ff348.js'
cat > "$WS/fechas349.js" <<'FINJS'
var d = new Date(2026, 10, 26, 22, 15, 7), C = [["toLocaleDateString", "es-ES", {"day": "numeric", "month": "long"}], ["toLocaleDateString", "es-ES", {"month": "long", "year": "numeric"}], ["toLocaleDateString", "es-ES", {"day": "numeric", "month": "short", "year": "numeric"}], ["toLocaleDateString", "es-ES", {"weekday": "long"}], ["toLocaleDateString", "es-ES", {"weekday": "short", "day": "numeric", "month": "short"}], ["toLocaleDateString", "es-ES", {"day": "2-digit", "month": "2-digit", "year": "numeric"}], ["toLocaleDateString", "es-ES", {"day": "numeric", "month": "numeric"}], ["toLocaleDateString", "es-ES", {"year": "numeric"}], ["toLocaleDateString", "es-ES", {"month": "long"}], ["toLocaleDateString", "es-ES", {"year": "2-digit", "month": "2-digit", "day": "2-digit"}], ["toLocaleTimeString", "es-ES", {"hour": "2-digit", "minute": "2-digit"}], ["toLocaleTimeString", "es-ES", {"hour": "numeric", "minute": "2-digit"}], ["toLocaleTimeString", "es-ES", {}], ["toLocaleTimeString", "es-ES", {"hour": "2-digit", "minute": "2-digit", "hour12": true}], ["toLocaleString", "es-ES", {"day": "numeric", "month": "long", "hour": "2-digit", "minute": "2-digit"}], ["toLocaleString", "es-ES", {}], ["toLocaleDateString", "en-US", {"month": "short", "day": "numeric"}], ["toLocaleDateString", "en-US", {"month": "long", "day": "numeric", "year": "numeric"}], ["toLocaleDateString", "en-US", {"weekday": "long", "month": "long", "day": "numeric"}], ["toLocaleDateString", "en-US", {}], ["toLocaleTimeString", "en-US", {"hour": "2-digit", "minute": "2-digit"}], ["toLocaleTimeString", "en-US", {"hour": "numeric", "minute": "2-digit"}], ["toLocaleTimeString", "en-US", {}], ["toLocaleString", "en-US", {}], ["toLocaleDateString", "en-GB", {"day": "numeric", "month": "long", "year": "numeric"}], ["toLocaleDateString", "en-US", {"month": "long", "year": "numeric"}]];
print(C.map(function (c) { return d[c[0]](c[1], c[2]); }).join(" | "));
FINJS
caso "fechas 349"      0 '26 de noviembre | noviembre de 2026 | 26 nov 2026 | jueves | jue, 26 nov | 26/11/2026 | 26/11 | 2026 | noviembre | 26/11/26 | 22:15 | 22:15 | 22:15:07 | 10:15 p. m. | 26 de noviembre a las 22:15 | 26/11/2026, 22:15:07 | Nov 26 | November 26, 2026 | Thursday, November 26 | 11/26/2026 | 10:15 PM | 10:15 PM | 10:15:07 PM | 11/26/2026, 10:15:07 PM | 26 November 2026 | November 2026' 'fechas349.js'
if [ "$1" = "todo" ]; then   # tambien el nucleo con ASan/UBSan (lo pide parche_274 --comprobar; make pruebas-js lo hace aparte)
  if ${CC:-gcc} -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST $INC -Itests tests/test_jsrun_274.c src/apps/jsrun_core.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o "$O/uj" 2>/dev/null; then
    ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 "$O/uj" || bad=$((bad+1))
  else echo "== test_jsrun_274: no compila en este equipo: SE OMITE"; fi
fi
rm -rf "$O"
if [ "$bad" -ne 0 ]; then echo "jsrun_host_274: $bad fallos"; exit 1; fi
echo "jsrun_host_274: $ok casos OK (programa de ring 3 real en el anfitrion)"
