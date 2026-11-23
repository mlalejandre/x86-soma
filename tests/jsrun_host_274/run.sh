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
if [ "$1" = "todo" ]; then   # tambien el nucleo con ASan/UBSan (lo pide parche_274 --comprobar; make pruebas-js lo hace aparte)
  if ${CC:-gcc} -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST $INC -Itests tests/test_jsrun_274.c src/apps/jsrun_core.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o "$O/uj" 2>/dev/null; then
    ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 "$O/uj" || bad=$((bad+1))
  else echo "== test_jsrun_274: no compila en este equipo: SE OMITE"; fi
fi
rm -rf "$O"
if [ "$bad" -ne 0 ]; then echo "jsrun_host_274: $bad fallos"; exit 1; fi
echo "jsrun_host_274: $ok casos OK (programa de ring 3 real en el anfitrion)"
