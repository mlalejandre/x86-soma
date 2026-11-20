/* PARCHE 096: nucleo agentico del programador de SOMA. Ver coder_core.h. Sin dependencias de la interfaz: se compila igual en el
 * kernel y en el host (tests/test_coder_sim.c lo ejercita con un LLM simulado en muchos casos de uso). */
#include "coder_core.h"
#include "lang.h"
#include "lang_diag.h"

/* ------------------------------------------------------------------ utilidades sin libc */
static uint32_t cl(const char *s) { uint32_t n = 0; while (s && s[n]) n++; return n; }
static void ccpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; if (!cap) return; if (!s) s = ""; while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void ccat(char *d, uint32_t cap, const char *s) { uint32_t n = cl(d); if (!s) return; while (*s && n + 1 < cap) d[n++] = *s++; d[n] = 0; }
static void ccatn(char *d, uint32_t cap, const char *s, uint32_t m) { uint32_t n = cl(d); while (m-- && *s && n + 1 < cap) d[n++] = *s++; d[n] = 0; }
static void cnum(char *d, uint32_t cap, uint32_t v) { char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); char o[12]; int j = 0; while (k) o[j++] = t[--k]; o[j] = 0; ccat(d, cap, o); }
static const char *cfind(const char *h, const char *n)
{
    if (!h || !n || !*n) return 0;
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return h; }
    return 0;
}
static int ci_has(const char *h, const char *n)       /* minusculas ASCII */
{
    for (; h && *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (x != y) break; a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}
static int is_id0(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_id(char c) { return is_id0(c) || (c >= '0' && c <= '9'); }
static void cmove(char *d, const char *s, uint32_t n)
{
    if (d < s) for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    else for (uint32_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
}
static uint32_t chash(const char *s, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; } return h ^ n; }

/* ------------------------------------------------------------------ conocimiento de SOMA-LANG (comprobado con el compilador real) */
static const char REF[] =
    "REFERENCIA SOMA-LANG (no es Python ni JavaScript):\n"
    "- Variables: let x = 0;  asignar: x = x + 1;  x += 2;  (NO existen var, const, ++ ni --)\n"
    "- Funciones: fn nombre(a, b) { return a + b; }  se declaran ANTES de llamarlas. No hay funciones anonimas.\n"
    "- Control: if (c) { } else if (c) { } else { }   while (c) { }   for (let i = 0; i < n; i = i + 1) { }   break; continue;\n"
    "- Logica: && || ! == != < <= > >=   Enteros de 64 bits (sin decimales: 7/2 = 3).  true false nil\n"
    "- Texto: \"a\" + to_str(n)   to_int(\"42\")   len(s)   substr(s, inicio, n)   find(s, trozo) (posicion o -1)   chr(65)   ord(s, i)\n"
    "- Matematicas enteras: abs(x)  min(a, b)  max(a, b)  clamp(x, minimo, maximo)  sqrt(x)  a % b   (NO hay decimales, ni math.*, ni Math.*)\n"
    "- Arrays: let a = [1, 2];  a[0]  len(a)  push(a, x)  pop(a)   Tablas: let t = { x: 1 };  t.x  t.x = 2;\n"
    "- PALABRAS RESERVADAS (no las uses como nombre de variable ni de funcion): fn let if else while for break continue return\n"
    "  print input import proc loop agent spawn ask mem vfs sys fb win true false nil. Ej.: llama nueva_comida(), no spawn().\n"
    "- El codigo se ejecuta de arriba abajo. Mejor SIN main: escribe el codigo principal al final, fuera de funciones. Si defines\n"
    "  fn juego() o fn run(), LLAMALA al final. (fn main() { } se ejecuta sola donde esta escrita: nunca escribas main();)\n"
    "- Consola: print(valor);  let s = input(\"pregunta\");\n"
    "- Sistema: sys.sleep(ms)  sys.millis() (ms desde el arranque, NO la hora)  rand(min, max)  vfs.read(ruta) (nil si no existe)  vfs.write(ruta, texto)\n"
    "- Hora del reloj del equipo: sys.hour()  sys.minute()  sys.second()  sys.day()  sys.month()  sys.year()\n"
    "- Teclado: sys.key() o win.key() devuelve el codigo de la tecla (0 si no hay). Flechas: arriba 16, abajo 14, izquierda 2,\n"
    "  derecha 6 (NO 37-40 ni 38). Enter 10, espacio 32, letras su ASCII (a 97, w 119, s 115, d 100). Esc cierra la ventana.\n"
    "- Ventana (UNA por programa, sin manejador): win.create(\"titulo\", x, y, ancho, alto);  win.clear(color);\n"
    "  win.rect(x, y, w, h, color);  win.text(x, y, \"texto\", color);  win.line(x1, y1, x2, y2, color);  win.circle(x, y, r, color);\n"
    "  win.present();  win.active() (falso al cerrarla)  let m = win.mouse(); (m.x, m.y, m.clicked)  win.close();\n"
    "  Colores 0xRRGGBB. Bucle obligatorio: while (win.active()) { leer raton/teclas; dibujar; win.present(); sys.sleep(30); }\n";

static const char EX_GUI[] =
    "// Contador grafico con botones\n"
    "win.create(\"Contador\", 100, 100, 320, 200);\n"
    "let n = 0;\n"
    "while (win.active()) {\n"
    "    let m = win.mouse();\n"
    "    if (m.clicked) {\n"
    "        if (m.x >= 20 && m.x < 140 && m.y >= 120 && m.y < 160) { n = n + 1; }\n"
    "        if (m.x >= 180 && m.x < 300 && m.y >= 120 && m.y < 160) { n = 0; }\n"
    "    }\n"
    "    win.clear(0x101828);\n"
    "    win.text(20, 30, \"Pulsaciones: \" + to_str(n), 0xFFFFFF);\n"
    "    win.rect(20, 120, 120, 40, 0x2563EB);\n"
    "    win.text(50, 132, \"+1\", 0xFFFFFF);\n"
    "    win.rect(180, 120, 120, 40, 0xDC2626);\n"
    "    win.text(205, 132, \"Reset\", 0xFFFFFF);\n"
    "    win.present();\n"
    "    sys.sleep(30);\n"
    "}\n";

static const char EX_CLI[] =
    "// Suma de un array y paridad\n"
    "fn suma(a) {\n"
    "    let t = 0;\n"
    "    let i = 0;\n"
    "    while (i < len(a)) {\n"
    "        t = t + a[i];\n"
    "        i = i + 1;\n"
    "    }\n"
    "    return t;\n"
    "}\n"
    "let nums = [3, 5, 7];\n"
    "push(nums, 10);\n"
    "print(\"Suma: \" + to_str(suma(nums)));\n"
    "for (let k = 1; k <= 3; k = k + 1) {\n"
    "    if (k % 2 == 0) { print(to_str(k) + \" es par\"); } else { print(to_str(k) + \" es impar\"); }\n"
    "}\n";

/* SOMA_239_PYTHON_GRAFICO: el mismo programador, en Python (MicroPython de SOMA en ring 3; ventana con soma.win) */
static const char REF_PY[] =
    "REFERENCIA: Python 3 de SOMA (MicroPython, un subconjunto de Python 3):\n"
    "- Hay: print, input, int, float, str, list, dict, set, tuple, clases, excepciones, f-strings, comprensiones, generadores,\n"
    "  import math, random, time, json, re, collections, struct. open() lee y escribe archivos de texto en /workspace.\n"
    "- NO hay: os, sys.stdin, subprocess, threading, tkinter, pygame, turtle, numpy ni red. No los importes.\n"
    "- Tiempo: time.sleep(s)  time.sleep_ms(ms)  time.ticks_ms() (ms desde el arranque)  time.localtime() -> (anio, mes, dia, h, m, s, ...)\n"
    "- Ventana grafica (UNA por programa): from soma import win\n"
    "  win.create(\"titulo\", ancho, alto)   win.clear(color)   win.rect(x, y, w, h, color)   win.text(x, y, \"texto\", color)\n"
    "  win.line(x1, y1, x2, y2, color)   win.circle(x, y, r, color)   win.pixel(x, y, color)   win.present()\n"
    "  win.active() (False al cerrarla)   m = win.mouse() -> m.x, m.y, m.clicked (clic desde la ultima vez), m.button (pulsado)\n"
    "  k = win.key() -> 0 si no hay tecla; flechas win.UP win.DOWN win.LEFT win.RIGHT; Intro 10, espacio 32, letras ord('a').\n"
    "  win.size() -> (ancho, alto)   win.rgb(r, g, b)   win.close().  Colores 0xRRGGBB. Texto: 8x8 pixeles por caracter. Esc cierra.\n"
    "  Bucle obligatorio: while win.active(): leer raton/teclas; dibujar todo; win.present(); time.sleep_ms(16)\n"
    "- Un programa con ventana NO usa input(): todo se maneja con el raton y el teclado de la ventana.\n";

static const char EX_GUI_PY[] =
    "# Contador grafico con botones\n"
    "from soma import win\n"
    "import time\n"
    "\n"
    "win.create(\"Contador\", 320, 200)\n"
    "n = 0\n"
    "while win.active():\n"
    "    m = win.mouse()\n"
    "    if m.clicked:\n"
    "        if 20 <= m.x < 140 and 120 <= m.y < 160:\n"
    "            n += 1\n"
    "        if 180 <= m.x < 300 and 120 <= m.y < 160:\n"
    "            n = 0\n"
    "    if win.key() == 32:\n"
    "        n += 1\n"
    "    win.clear(0x101828)\n"
    "    win.text(20, 30, \"Pulsaciones: \" + str(n), 0xFFFFFF)\n"
    "    win.rect(20, 120, 120, 40, 0x2563EB)\n"
    "    win.text(70, 136, \"+1\", 0xFFFFFF)\n"
    "    win.rect(180, 120, 120, 40, 0xDC2626)\n"
    "    win.text(220, 136, \"Reset\", 0xFFFFFF)\n"
    "    win.present()\n"
    "    time.sleep_ms(16)\n";

static const char EX_CLI_PY[] =
    "# Suma de una lista y paridad\n"
    "def suma(a):\n"
    "    t = 0\n"
    "    for x in a:\n"
    "        t += x\n"
    "    return t\n"
    "\n"
    "nums = [3, 5, 7]\n"
    "nums.append(10)\n"
    "print(\"Suma:\", suma(nums))\n"
    "for k in range(1, 4):\n"
    "    print(k, \"es par\" if k % 2 == 0 else \"es impar\")\n";

static int g_py;                                                          /* el trabajo en curso es Python */
static const char *ref_txt(void) { return g_py ? REF_PY : REF; }
static const char *lang_name(void) { return g_py ? "Python (el de SOMA)" : "SOMA-LANG"; }
static const char *fence_tag(void) { return g_py ? "python" : "soma"; }

int cc_mission_wants_python(const char *m)
{
    return m && (ci_has(m, "en python") || ci_has(m, "con python") || ci_has(m, "usando python") || ci_has(m, "lenguaje python")
                 || ci_has(m, "python:") || ci_has(m, "en py ") || ci_has(m, ".py"));
}

/* "  File \"programa.py\", line 7, in <module>\nNameError: ..." -> 7 y "NameError: ..." (la ultima linea con texto) */
int cc_py_error(const char *out, uint32_t *line, char *msg, uint32_t cap)
{
    *line = 0; if (cap) msg[0] = 0;
    if (!out) return 0;
    for (const char *p = out; (p = cfind(p, "line ")) != 0; p += 5) {
        const char *d = p + 5; uint32_t v = 0; int any = 0;
        while (*d >= '0' && *d <= '9' && v < 100000) { v = v * 10 + (uint32_t)(*d - '0'); d++; any = 1; }
        if (any) *line = v;
    }
    const char *last = 0, *p = out;
    while (*p) {
        const char *ls = p; while (*p && *p != '\n') p++;
        const char *t = ls; while (t < p && (*t == ' ' || *t == '\r')) t++;
        if (t < p) last = ls;
        if (*p) p++;
    }
    if (last) { const char *e = last; while (*e && *e != '\n' && *e != '\r') e++; while (*last == ' ') last++; ccatn(msg, cap, last, (uint32_t)(e - last)); }
    return *line != 0 || (cap && msg[0]);
}

/* Normalizador de Python: solo lo que nunca cambia el significado (tabuladores, \r, comillas tipograficas, prompts ">>> ") */
int cc_py_normalize(char *src, uint32_t *len, uint32_t cap)
{
    static char o[CC_SRC_MAX + 4096];
    uint32_t n = 0, L = *len; int fix = 0, bol = 1;
    if (cap > sizeof o) cap = sizeof o;
    for (uint32_t i = 0; i < L && n + 5 < cap; ) {
        char c = src[i];
        if (c == '\r') { i++; continue; }
        if (bol && (cfind(src + i, ">>> ") == src + i || cfind(src + i, "... ") == src + i)) { i += 4; fix++; continue; }
        if (c == '\t') { for (int k = 0; k < 4; k++) o[n++] = ' '; i++; fix++; bol = 0; continue; }
        if ((uint8_t)c == 0xE2 && i + 2 < L && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0x9C || (uint8_t)src[i + 2] == 0x9D)) { o[n++] = '"'; i += 3; fix++; bol = 0; continue; }
        if ((uint8_t)c == 0xE2 && i + 2 < L && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0x98 || (uint8_t)src[i + 2] == 0x99)) { o[n++] = '\''; i += 3; fix++; bol = 0; continue; }
        o[n++] = c; i++;
        bol = (c == '\n');
    }
    cmove(src, o, n); src[n] = 0; *len = n;
    return fix;
}

/* SOMA_235_PROGRAMADOR: hay palabras que piden ventana siempre (ventana, boton, raton, juego...) y otras que solo la sugieren
 * (calculadora, pantalla): estas ceden si la tarea habla de consola o de preguntar al usuario ("que pida dos numeros"). */
int cc_mission_is_gui(const char *m)
{
    static const char *const strong[] = { "ventana", "grafic", "boton", "botón", "juego", "raton", "ratón", "click", "clic", "pong", "snake",
                                          "serpiente", "dibuj", "interfaz", "window", "gui", "canvas", "anima", "win.", 0 };
    static const char *const weak[] = { "calculadora", "pantalla", 0 };
    static const char *const console[] = { "consola", "terminal", "pida", "pide", "pregunt", "escriba el usuario", "introduzca", "introduce",
                                           "teclee", "input(", 0 };
    for (int i = 0; strong[i]; i++) if (ci_has(m, strong[i])) return 1;
    for (int i = 0; console[i]; i++) if (ci_has(m, console[i])) return 0;
    for (int i = 0; weak[i]; i++) if (ci_has(m, weak[i])) return 1;
    return 0;
}

/* ------------------------------------------------------------------ extraccion del codigo de la respuesta del modelo */
uint32_t cc_extract_code(const char *reply, char *out, uint32_t cap)
{
    const char *r = reply, *t;
    out[0] = 0;
    if (!reply || cap < 2) return 0;
    if ((t = cfind(r, "</think>")) != 0) r = t + 8;                       /* razonamiento visible: fuera */
    /* bloque ``` mas largo */
    const char *best = 0; uint32_t bestn = 0;
    for (const char *p = cfind(r, "```"); p; ) {
        const char *s = p + 3;
        while (*s && *s != '\n') s++;                                     /* etiqueta de lenguaje */
        if (*s == '\n') s++;
        const char *e = cfind(s, "```");
        uint32_t n = e ? (uint32_t)(e - s) : cl(s);                        /* bloque sin cerrar: hasta el final */
        if (n > bestn) { best = s; bestn = n; }
        if (!e) break;
        p = cfind(e + 3, "```");
    }
    if (best) { if (bestn >= cap) bestn = cap - 1; cmove(out, best, bestn); out[bestn] = 0; return bestn; }
    /* sin bloques: salta la prosa inicial hasta la primera linea con pinta de codigo */
    const char *p = r;
    while (*p) {
        const char *ls = p; while (*ls == ' ') ls++;
        int code = (ls[0] == '/' && ls[1] == '/') || cfind(ls, "let ") == ls || cfind(ls, "fn ") == ls || cfind(ls, "win.") == ls ||
                   cfind(ls, "print(") == ls || cfind(ls, "while") == ls || cfind(ls, "if ") == ls || cfind(ls, "if(") == ls ||
                   cfind(ls, "for ") == ls || cfind(ls, "sys.") == ls || cfind(ls, "import ") == ls || cfind(ls, "from ") == ls ||
                   cfind(ls, "def ") == ls || cfind(ls, "class ") == ls || ls[0] == '#';   /* SOMA_239: Python */
        if (code) break;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    if (!*p) p = r;
    uint32_t n = cl(p); if (n >= cap) n = cap - 1;
    cmove(out, p, n); out[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ normalizador determinista */
static int g_fix;
static void put(char *o, uint32_t *n, uint32_t cap, const char *s, uint32_t k) { while (k-- && *n + 1 < cap) o[(*n)++] = *s++; }
static void puts_(char *o, uint32_t *n, uint32_t cap, const char *s) { put(o, n, cap, s, cl(s)); }

int cc_normalize(char *src, uint32_t *len, uint32_t cap)
{
    static char o[CC_SRC_MAX + 4096];
    static char handle[40];
    uint32_t n = 0, L = *len, i = 0;
    int bol = 1;                                       /* principio de linea */
    g_fix = 0; handle[0] = 0;
    if (cap > sizeof o) cap = sizeof o;
    /* manejador de ventana: let NOMBRE = win.create( */
    {
        const char *c = cfind(src, "win.create(");
        if (c) {
            const char *b = c; while (b > src && (b[-1] == ' ' || b[-1] == '=')) b--;
            const char *e = b; while (b > src && is_id(b[-1])) b--;
            if (e > b && (uint32_t)(e - b) < sizeof handle) { ccpy(handle, (uint32_t)(e - b) + 1, b); if (cfind(handle, "let") == handle && cl(handle) == 3) handle[0] = 0; }
        }
    }
    while (i < L) {
        char c = src[i];
        if (c == '\r') { i++; continue; }
        if (c == '\t') { puts_(o, &n, cap, "    "); i++; g_fix++; continue; }
        /* comillas tipograficas fuera de cadenas */
        if ((uint8_t)c == 0xE2 && i + 2 < L && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0x9C || (uint8_t)src[i + 2] == 0x9D)) { c = '"'; src[i + 2] = '"'; i += 2; g_fix++; }
        else if ((uint8_t)c == 0xE2 && i + 2 < L && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0x98 || (uint8_t)src[i + 2] == 0x99)) { c = '\''; src[i + 2] = '\''; i += 2; g_fix++; }
        if (c == '\n') { put(o, &n, cap, "\n", 1); i++; bol = 1; continue; }
        if (bol && c == ' ') { put(o, &n, cap, &c, 1); i++; continue; }
        if (bol && c == '#') { puts_(o, &n, cap, "//"); i++; g_fix++; bol = 0; continue; }     /* comentario de Python */
        bol = 0;
        if (c == '/' && i + 1 < L && src[i + 1] == '/') { while (i < L && src[i] != '\n') { put(o, &n, cap, &src[i], 1); i++; } continue; }
        if (c == '"' || c == '\'') {                                          /* cadena: se copia tal cual */
            char q = c; put(o, &n, cap, &c, 1); i++;
            while (i < L && src[i] != q && src[i] != '\n') {
                if (q == '"' && (uint8_t)src[i] == 0xE2 && i + 2 < L && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0x9C || (uint8_t)src[i + 2] == 0x9D)) { i += 2; src[i] = '"'; g_fix++; break; }   /* cierre tipografico */
                if (src[i] == '\\' && i + 1 < L) { put(o, &n, cap, &src[i], 1); i++; }
                put(o, &n, cap, &src[i], 1); i++;
            }
            if (i < L && src[i] == q) { put(o, &n, cap, &q, 1); i++; }
            continue;
        }
        if (c == '`') {                                                       /* plantilla `a ${x} b` -> ("a " + to_str(x) + " b") */
            uint32_t j = i + 1; int ok = 0;
            while (j < L && src[j] != '\n') { if (src[j] == '`') { ok = 1; break; } j++; }
            if (ok) {
                puts_(o, &n, cap, "(\""); i++;
                while (i < j) {
                    if (src[i] == '$' && i + 1 < j && src[i + 1] == '{') {
                        uint32_t e = i + 2; while (e < j && src[e] != '}') e++;
                        puts_(o, &n, cap, "\" + to_str("); put(o, &n, cap, src + i + 2, e - i - 2); puts_(o, &n, cap, ") + \"");
                        i = e + 1; continue;
                    }
                    if (src[i] == '"') puts_(o, &n, cap, "\\\""); else put(o, &n, cap, &src[i], 1);
                    i++;
                }
                puts_(o, &n, cap, "\")"); i++; g_fix++; continue;
            }
        }
        if (is_id0(c) && (i == 0 || !is_id(src[i - 1]))) {
            uint32_t s = i; while (i < L && is_id(src[i])) i++;
            uint32_t k = i - s; const char *w = src + s;
            char prev = s > 0 ? src[s - 1] : ' ';
#define IS(lit) (k == sizeof(lit) - 1 && cfind(w, lit) == w)
            if (prev != '.') {
                if (IS("elif")) { puts_(o, &n, cap, "else if"); g_fix++; continue; }
                if (IS("True")) { puts_(o, &n, cap, "true"); g_fix++; continue; }
                if (IS("False")) { puts_(o, &n, cap, "false"); g_fix++; continue; }
                if (IS("None") || IS("null") || IS("undefined")) { puts_(o, &n, cap, "nil"); g_fix++; continue; }
                if (IS("var") || IS("const")) { puts_(o, &n, cap, "let"); g_fix++; continue; }
                if (IS("function")) { puts_(o, &n, cap, "fn"); g_fix++; continue; }
                if (IS("console") && i + 4 <= L && cfind(src + i, ".log") == src + i) { puts_(o, &n, cap, "print"); i += 4; g_fix++; continue; }
                /* PARCHE 100: palabra reservada usada como nombre (fn spawn() / spawn() / let loop = ...) -> nombre_ */
                if (IS("spawn") || IS("agent") || IS("proc") || IS("loop") || IS("import") || IS("ask") || IS("mem") || IS("fb")) {
                    uint32_t j = i; while (j < L && src[j] == ' ') j++;
                    int as_name = (j < L && (src[j] == '(' || src[j] == '=' || src[j] == ',' || src[j] == ')' || src[j] == ';' || src[j] == '+' || src[j] == '-' || src[j] == '<' || src[j] == '>' || src[j] == '['));
                    if (IS("ask") && j < L && src[j] == '(') as_name = 0;          /* ask("...") es una funcion del sistema */
                    if (IS("loop") && j < L && src[j] == '{') as_name = 0;
                    if (as_name) { put(o, &n, cap, w, k); puts_(o, &n, cap, "_"); g_fix++; continue; }
                }
                /* rand(n) con un solo argumento -> rand(0, n) */
                if ((IS("rand") || IS("randint") || IS("random")) && i < L && src[i] == '(') {
                    uint32_t j = i + 1; int d = 0, comma = 0;
                    while (j < L && src[j] != '\n') { if (src[j] == '(') d++; else if (src[j] == ')') { if (!d) break; d--; } else if (src[j] == ',' && !d) comma = 1; j++; }
                    if (!comma && j < L && src[j] == ')' && j > i + 1) { put(o, &n, cap, w, k); puts_(o, &n, cap, "(0, "); i++; g_fix++; continue; }
                }
                /* x++ / x-- */
                if (i + 1 < L && ((src[i] == '+' && src[i + 1] == '+') || (src[i] == '-' && src[i + 1] == '-'))) {
                    put(o, &n, cap, w, k); puts_(o, &n, cap, " = "); put(o, &n, cap, w, k); puts_(o, &n, cap, src[i] == '+' ? " + 1" : " - 1");
                    i += 2; g_fix++; continue;
                }
            } else {
                if (IS("length") && (i >= L || src[i] != '(')) {                /* a.length -> len(a) via .len() */
                    puts_(o, &n, cap, "len()"); g_fix++; continue;
                }
            }
            /* win.metodo(MANEJADOR, ...) -> win.metodo(...) */
            if (handle[0] && prev == '.' && s >= 4 && cfind(src + s - 4, "win.") == src + s - 4 && i < L && src[i] == '(') {
                uint32_t hl = cl(handle), j = i + 1;
                while (j < L && src[j] == ' ') j++;
                if (j + hl <= L && cfind(src + j, handle) == src + j && (j + hl >= L || !is_id(src[j + hl]))) {
                    uint32_t e = j + hl; while (e < L && src[e] == ' ') e++;
                    if (e < L && (src[e] == ',' || src[e] == ')')) {
                        put(o, &n, cap, w, k); put(o, &n, cap, "(", 1);
                        i = (src[e] == ',') ? e + 1 : e;
                        while (i < L && src[i] == ' ' && src[e] == ',') i++;
                        g_fix++; continue;
                    }
                }
            }
#undef IS
            put(o, &n, cap, w, k);
            continue;
        }
        put(o, &n, cap, &c, 1); i++;
    }
    if (n >= cap) n = cap - 1;
    cmove(src, o, n); src[n] = 0; *len = n;
    /* PARCHE 098: bucle de ventana sin pausa = programa que acapara la CPU y congela el escritorio: se anade sys.sleep(16) */
    {
        const char *w = cfind(src, "while (win.active())"); if (!w) w = cfind(src, "while(win.active())");
        if (w) {
            const char *b = w; while (*b && *b != '{') b++;
            if (*b == '{') {
                int d = 0; const char *e = b; char q = 0;
                for (; *e; e++) {
                    if (q) { if (*e == '\\' && e[1]) { e++; continue; } if (*e == q) q = 0; continue; }
                    if (*e == '"' || *e == '\'') q = *e;
                    else if (*e == '{') d++;
                    else if (*e == '}' && --d == 0) break;
                }
                if (*e == '}') {
                    int has = 0; for (const char *p = b; p < e; p++) if (p[0] == 's' && cfind(p, "sys.sleep") == p) { has = 1; break; }
                    if (!has) {
                        const char *ins = "    sys.sleep(16);\n"; uint32_t il = cl(ins), at = (uint32_t)(e - src);
                        while (at > 0 && src[at - 1] == ' ') at--;
                        if (*len + il + 1 < cap) { cmove(src + at + il, src + at, *len - at + 1); cmove(src + at, ins, il); *len += il; g_fix++; }
                    }
                }
            }
        }
    }
    /* PARCHE 100: "fn main() { }" se ejecuta sola en su sitio y llamar a main() es un error: se quitan las lineas "main();" */
    if (cfind(src, "fn main")) {
        char *p = src;
        while ((p = (char *)cfind(p, "main();")) != 0) {
            char *ls = p; while (ls > src && ls[-1] == ' ') ls--;
            if ((ls == src || ls[-1] == '\n') && (p[7] == '\n' || p[7] == 0)) {
                uint32_t cut = (uint32_t)(p + 7 - ls) + (p[7] == '\n' ? 1 : 0);
                cmove(ls, ls + cut, *len - (uint32_t)(ls - src) - cut + 1); *len -= cut; g_fix++; p = ls;
            } else p += 7;
        }
    }
    /* PARCHE 100: el modelo define fn main() / fn juego() con todo dentro y no la llama: el programa "termina con exito" sin hacer
     * nada. Si una funcion sin parametros llamada main/run/start/inicio/juego/game (o la que contiene el bucle de ventana) no se
     * llama nunca, se anade la llamada al final. */
    {
        static const char *const ent[] = { "run", "start", "inicio", "iniciar", "juego", "game", "principal", 0 };
        char name[48]; name[0] = 0;
        const char *wl = cfind(src, "win.active");
        for (const char *p = src; (p = cfind(p, "fn ")) != 0; p += 3) {
            if (p > src && p[-1] != '\n' && p[-1] != ' ' && p[-1] != '}') continue;
            const char *nm = p + 3; while (*nm == ' ') nm++;
            const char *ne = nm; while (is_id(*ne)) ne++;
            uint32_t nl = (uint32_t)(ne - nm); if (!nl || nl >= sizeof name) continue;
            const char *q = ne; while (*q == ' ') q++;
            if (q[0] != '(') continue;
            q++; while (*q == ' ') q++;
            if (*q != ')') continue;                                   /* solo funciones sin parametros */
            const char *b = q; while (*b && *b != '{') b++;
            int d = 0; const char *e = b; char qq = 0;
            for (; *e; e++) { if (qq) { if (*e == '\\' && e[1]) { e++; continue; } if (*e == qq) qq = 0; continue; }
                              if (*e == '"' || *e == '\'') qq = *e; else if (*e == '{') d++; else if (*e == '}' && --d == 0) break; }
            int holds_loop = wl && wl > b && wl < e, known = 0;
            char cand[48]; ccpy(cand, nl + 1, nm);
            for (int k = 0; ent[k]; k++) if (cl(ent[k]) == nl && cfind(cand, ent[k]) == cand) known = 1;
            if (!holds_loop && !known) continue;
            int calls = 0;                                             /* apariciones de NOMBRE( fuera de su definicion */
            for (const char *c = src; (c = cfind(c, cand)) != 0; c += nl) {
                if (c == nm) continue;
                if ((c > src && is_id(c[-1])) || is_id(c[nl])) continue;
                const char *a = c + nl; while (*a == ' ') a++;
                if (*a == '(') calls++;
            }
            if (!calls && (holds_loop || !name[0])) ccpy(name, sizeof name, cand);
            if (!calls && holds_loop) break;
        }
        if (name[0] && *len + cl(name) + 8 < cap) {
            uint32_t at = *len;
            if (at && src[at - 1] != '\n') src[at++] = '\n';
            src[at] = 0; ccat(src, cap, name); ccat(src, cap, "();\n");
            *len = cl(src); g_fix++;
        }
    }
    return g_fix;
}

/* ------------------------------------------------------------------ verificacion */
struct verdict {
    int      compiles, hard, warns, issues, truncated, unchanged, tfail, tpass;
    uint32_t line, bc;
    char     msg[160], token[48], issue[300], ltext[120];
    int      score;
};
static uint8_t g_bc[65536];
static const struct cc_env *g_te;      /* PARCHE 116: entorno con pruebas durante cc_run */
static char g_tout[4096];
static struct soma_lint_report g_rep;

static int balance(const char *s, uint32_t n, int *str_open)
{
    int b = 0; char q = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (q) { if (c == '\\') { i++; continue; } if (c == q || c == '\n') q = 0; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') i++; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '{' || c == '(' || c == '[') b++;
        else if (c == '}' || c == ')' || c == ']') b--;
    }
    *str_open = q != 0;
    return b;
}
/* PARCHE 116: compara ignorando espacios y saltos de linea finales */
static int out_equal(const char *a, const char *b)
{
    uint32_t la = cl(a), lb = cl(b);
    while (la && (a[la - 1] == '\n' || a[la - 1] == ' ' || a[la - 1] == '\r')) la--;
    while (lb && (b[lb - 1] == '\n' || b[lb - 1] == ' ' || b[lb - 1] == '\r')) lb--;
    if (la != lb) return 0;
    for (uint32_t i = 0; i < la; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static void oneline(char *d, uint32_t cap, const char *s, uint32_t maxc)
{
    uint32_t n = 0; d[0] = 0;
    for (uint32_t i = 0; s[i] && n + 4 < cap && i < maxc; i++) { char c = s[i]; if (c == '\n') { d[n++] = '\\'; d[n++] = 'n'; } else if (c == '\r') continue; else d[n++] = c; }
    if (s[0] && cl(s) > maxc && n + 4 < cap) { d[n++] = '.'; d[n++] = '.'; d[n++] = '.'; }
    d[n] = 0;
}
/* Ejecuta el bytecode g_bc[0..bl) contra los casos de prueba; rellena tpass/tfail y describe el PRIMER fallo en v->issue. */
static uint32_t g_tm_hash; static int g_tm_valid, g_tm_pass, g_tm_fail; static char g_tm_issue[300];
static void run_tests(uint32_t bl, struct verdict *v)
{
    const struct cc_env *e = g_te;
    v->tpass = v->tfail = 0;
    if (!e || !e->run || !e->tests || e->ntests <= 0) return;
    uint32_t h = chash((const char *)g_bc, bl);                      /* mismo bytecode: mismo resultado (verify se llama varias veces por candidato) */
    if (g_tm_valid && g_tm_hash == h) { v->tpass = g_tm_pass; v->tfail = g_tm_fail; ccat(v->issue, sizeof v->issue, g_tm_issue); if (v->tfail) v->issues++; return; }
    for (int i = 0; i < e->ntests; i++) {
        const struct cc_test *t = &e->tests[i];
        g_tout[0] = 0;
        int rc = e->run(g_bc, bl, t->input ? t->input : "", g_tout, sizeof g_tout);
        if (rc == 0 && out_equal(g_tout, t->expect)) { v->tpass++; continue; }
        v->tfail++;
        if (v->tfail == 1) {
            char a[96], b[96], c[96];
            oneline(a, sizeof a, t->input ? t->input : "", 60); oneline(b, sizeof b, t->expect, 80); oneline(c, sizeof c, g_tout, 80);
            ccat(v->issue, sizeof v->issue, "La prueba '"); ccat(v->issue, sizeof v->issue, t->name ? t->name : "?"); ccat(v->issue, sizeof v->issue, "' FALLA");
            if (rc != 0) ccat(v->issue, sizeof v->issue, " (el programa no termina bien: error de ejecucion o bucle infinito)");
            ccat(v->issue, sizeof v->issue, ". Entrada: \""); ccat(v->issue, sizeof v->issue, a);
            ccat(v->issue, sizeof v->issue, "\". Salida esperada: \""); ccat(v->issue, sizeof v->issue, b);
            ccat(v->issue, sizeof v->issue, "\". Salida obtenida: \""); ccat(v->issue, sizeof v->issue, c); ccat(v->issue, sizeof v->issue, "\". ");
        }
    }
    if (v->tfail) { v->issues++; if (v->tfail > 1) { ccat(v->issue, sizeof v->issue, "Fallan "); cnum(v->issue, sizeof v->issue, (uint32_t)v->tfail); ccat(v->issue, sizeof v->issue, " pruebas en total. "); } }
    g_tm_valid = 1; g_tm_hash = h; g_tm_pass = v->tpass; g_tm_fail = v->tfail; g_tm_issue[0] = 0; ccat(g_tm_issue, sizeof g_tm_issue, v->issue);
}

static const char *g_mission = "";                                       /* SOMA_235: la tarea, para las comprobaciones */
/* SOMA_239_PYTHON_GRAFICO: verificacion de un programa Python: compilarlo de verdad (python -s en un proceso), comprobaciones de
 * la tarea y, en los de consola, las pruebas ejecutandolo (python -x con la entrada de cada caso). Resultados en cache por huella. */
static char g_pyout[2048];
static uint32_t g_pc_hash; static int g_pc_valid, g_pc_ok; static uint32_t g_pc_line; static char g_pc_msg[160];
static void run_tests_py(const char *src, uint32_t n, struct verdict *v)
{
    const struct cc_env *e = g_te;
    v->tpass = v->tfail = 0;
    if (!e || !e->run_src || !e->tests || e->ntests <= 0) return;
    uint32_t h = chash(src, n) ^ 0x5079u;
    if (g_tm_valid && g_tm_hash == h) { v->tpass = g_tm_pass; v->tfail = g_tm_fail; ccat(v->issue, sizeof v->issue, g_tm_issue); if (v->tfail) v->issues++; return; }
    for (int i = 0; i < e->ntests; i++) {
        const struct cc_test *t = &e->tests[i];
        g_tout[0] = 0;
        int rc = e->run_src(src, n, t->input ? t->input : "", g_tout, sizeof g_tout);
        if (rc == 0 && out_equal(g_tout, t->expect)) { v->tpass++; continue; }
        v->tfail++;
        if (v->tfail == 1) {
            char a[96], b[96], c[96];
            oneline(a, sizeof a, t->input ? t->input : "", 60); oneline(b, sizeof b, t->expect, 80); oneline(c, sizeof c, g_tout, 80);
            ccat(v->issue, sizeof v->issue, "La prueba '"); ccat(v->issue, sizeof v->issue, t->name ? t->name : "?"); ccat(v->issue, sizeof v->issue, "' FALLA");
            if (rc != 0) {
                uint32_t ln; char em[96];
                if (cc_py_error(g_tout, &ln, em, sizeof em) && em[0]) { ccat(v->issue, sizeof v->issue, " (error: "); ccat(v->issue, sizeof v->issue, em); ccat(v->issue, sizeof v->issue, ")"); }
                else ccat(v->issue, sizeof v->issue, " (el programa no termina bien: error o bucle infinito)");
            }
            ccat(v->issue, sizeof v->issue, ". Entrada: \""); ccat(v->issue, sizeof v->issue, a);
            ccat(v->issue, sizeof v->issue, "\". Salida esperada: \""); ccat(v->issue, sizeof v->issue, b);
            ccat(v->issue, sizeof v->issue, "\". Salida obtenida: \""); ccat(v->issue, sizeof v->issue, c); ccat(v->issue, sizeof v->issue, "\". ");
        }
    }
    if (v->tfail) { v->issues++; if (v->tfail > 1) { ccat(v->issue, sizeof v->issue, "Fallan "); cnum(v->issue, sizeof v->issue, (uint32_t)v->tfail); ccat(v->issue, sizeof v->issue, " pruebas en total. "); } }
    g_tm_valid = 1; g_tm_hash = h; g_tm_pass = v->tpass; g_tm_fail = v->tfail; g_tm_issue[0] = 0; ccat(g_tm_issue, sizeof g_tm_issue, v->issue);
}
static void verify_py(const char *src, uint32_t n, int gui, const char *orig, uint32_t orig_n, struct verdict *v)
{
    const struct cc_env *e = g_te;
    uint32_t h = chash(src, n);
    if (!(g_pc_valid && g_pc_hash == h)) {
        g_pc_valid = 1; g_pc_hash = h; g_pc_ok = 0; g_pc_line = 0; g_pc_msg[0] = 0;
        if (e && e->check_src) {
            g_pyout[0] = 0;
            int rc = e->check_src(src, n, g_pyout, sizeof g_pyout);
            if (rc == 0) g_pc_ok = 1;
            else if (!cc_py_error(g_pyout, &g_pc_line, g_pc_msg, sizeof g_pc_msg) || !g_pc_msg[0]) ccpy(g_pc_msg, sizeof g_pc_msg, rc == 2 ? "la comprobacion tardo demasiado" : "no se pudo compilar");
        } else g_pc_ok = 1;                                                 /* sin comprobador (no deberia pasar): se confia */
    }
    if (g_pc_ok) { v->compiles = 1; v->bc = n; }
    else {
        v->line = g_pc_line; ccpy(v->msg, sizeof v->msg, g_pc_msg);
        uint32_t ln = 1; const char *p = src, *end = src + n;
        while (p < end && ln < v->line) { if (*p == '\n') ln++; p++; }
        const char *q = p; while (q < end && *q != '\n' && (uint32_t)(q - p) < sizeof v->ltext - 1) q++;
        if (v->line) ccatn(v->ltext, sizeof v->ltext, p, (uint32_t)(q - p));
    }
    if (n < 8) { ccat(v->issue, sizeof v->issue, "No hay codigo. "); v->issues++; }
    else {                                                                  /* texto en vez de programa */
        int prose = 0, code = 0; const char *p = src, *end = src + n;
        while (p < end) {
            const char *ls = p; int words = 0, inw = 0, sym = 0;
            while (p < end && *p != '\n') { char c = *p; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { if (!inw) words++; inw = 1; } else { inw = 0; if (c == ':' || c == '(' || c == '=' || c == '[') sym = 1; } p++; }
            if (p < end) p++;
            while (ls < p && *ls == ' ') ls++;
            if (ls[0] == '#') continue;
            if (sym) code++; else if (words >= 4) prose++;
        }
        if (prose > code) { ccat(v->issue, sizeof v->issue, "La respuesta es TEXTO (razonamiento o explicacion), no un programa. "); v->issues += 3; }
    }
    static const char *const banned[] = { "import pygame", "import tkinter", "from tkinter", "import turtle", "import os", "from os ",
                                          "import threading", "import numpy", "import subprocess", "import curses", 0 };
    for (int i = 0; banned[i]; i++) if (cfind(src, banned[i])) {
        ccat(v->issue, sizeof v->issue, "Usa '"); ccat(v->issue, sizeof v->issue, banned[i]);
        ccat(v->issue, sizeof v->issue, "', que no existe en SOMA: para graficos usa from soma import win. "); v->issues++; break;
    }
    if (gui && !cfind(src, "win.create")) { ccat(v->issue, sizeof v->issue, "La tarea pide una ventana grafica y no hay win.create (from soma import win). "); v->issues++; }
    if (cfind(src, "win.create") && !cfind(src, "win.present")) { ccat(v->issue, sizeof v->issue, "Falta win.present() en el bucle de dibujo. "); v->issues++; }
    if (cfind(src, "win.create") && !cfind(src, "win.active")) { ccat(v->issue, sizeof v->issue, "Falta el bucle while win.active(): ... "); v->issues++; }
    if (cfind(src, "win.create") && cfind(src, "input(")) { ccat(v->issue, sizeof v->issue, "Un programa con ventana no puede usar input(): usa win.key() y win.mouse(). "); v->issues++; }
    if (ci_has(g_mission, "flecha") && cfind(src, "key()") && !cfind(src, "win.UP") && !cfind(src, "win.DOWN") && !cfind(src, "win.LEFT")
        && !cfind(src, "win.RIGHT") && !cfind(src, "== 16") && !cfind(src, "==16")) {
        ccat(v->issue, sizeof v->issue, "La tarea usa las FLECHAS: compara win.key() con win.UP, win.DOWN, win.LEFT y win.RIGHT. "); v->issues++;
    }
    if ((ci_has(g_mission, "hora") || ci_has(g_mission, "reloj")) && !ci_has(g_mission, "cronometro") && !ci_has(g_mission, "cronómetro")
        && !ci_has(g_mission, "temporizador") && !ci_has(g_mission, "milisegund") && !ci_has(g_mission, "desde") && !ci_has(g_mission, "cuenta atras")
        && !cfind(src, "localtime") && cfind(src, "ticks_ms")) {
        ccat(v->issue, sizeof v->issue, "time.ticks_ms() es el tiempo desde que arranco el equipo, no la hora: usa time.localtime(). "); v->issues++;
    }
    if (orig && orig_n && orig_n == n) { uint32_t i = 0; while (i < n && orig[i] == src[i]) i++; if (i == n) { v->unchanged = 1; ccat(v->issue, sizeof v->issue, "El codigo es IDENTICO al original: no se aplico el cambio pedido. "); v->issues++; } }
    if (v->compiles && !gui && !cfind(src, "win.create")) run_tests_py(src, n, v);
    v->score = (v->compiles ? 0 : 1000) + v->issues * 60 + v->tfail * 300;
}
static void verify(const char *src, uint32_t n, int gui, const char *orig, uint32_t orig_n, struct verdict *v)
{
    if (g_py) { for (uint32_t i = 0; i < sizeof *v; i++) ((char *)v)[i] = 0; verify_py(src, n, gui, orig, orig_n, v); return; }
    uint32_t bl = 0; int so = 0;
    for (uint32_t i = 0; i < sizeof *v; i++) ((char *)v)[i] = 0;
    extern int lang_diag_quiet;
    lang_diag_quiet = 1;
    g_rep.count = g_rep.hard = 0;
    lang_lint(src, n, &g_rep);
    v->hard = (int)g_rep.hard; v->warns = (int)(g_rep.count - g_rep.hard);
    if (lang_compile(src, n, g_bc, sizeof g_bc, &bl) == 0) { v->compiles = 1; v->bc = bl; }
    else {
        struct soma_compile_diag d; lang_get_diagnostic(&d); v->line = d.line; ccpy(v->msg, sizeof v->msg, d.msg); ccpy(v->token, sizeof v->token, d.token);
        uint32_t ln = 1; const char *p = src, *e = src + n;                     /* texto de la linea del error (para la pista) */
        while (p < e && ln < d.line) { if (*p == '\n') ln++; p++; }
        const char *q = p; while (q < e && *q != '\n' && (uint32_t)(q - p) < sizeof v->ltext - 1) q++;
        ccatn(v->ltext, sizeof v->ltext, p, (uint32_t)(q - p));
    }
    lang_diag_quiet = 0;
    int b = balance(src, n, &so);
    if (b > 0 || so) { v->truncated = 1; ccat(v->issue, sizeof v->issue, "El programa parece CORTADO (llaves o comillas sin cerrar). "); v->issues++; }
    if (n < 8) { ccat(v->issue, sizeof v->issue, "No hay codigo. "); v->issues++; }
    else {                                                                  /* PARCHE 097: texto/razonamiento en vez de programa */
        int prose = 0, code = 0; const char *p = src, *e = src + n;
        while (p < e) {
            const char *ls = p; int words = 0, inw = 0, sym = 0;
            while (p < e && *p != '\n') { char c = *p; if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { if (!inw) words++; inw = 1; } else { inw = 0; if (c == ';' || c == '{' || c == '}' || c == '(' || c == '=') sym = 1; } p++; }
            if (p < e) p++;
            while (ls < p && *ls == ' ') ls++;
            if (ls[0] == '/' && ls[1] == '/') continue;
            if (sym) code++; else if (words >= 3) prose++;
        }
        if (prose > code) { ccat(v->issue, sizeof v->issue, "La respuesta es TEXTO (razonamiento o explicacion), no un programa. "); v->issues += 3; }
    }
    if (gui && !cfind(src, "win.create")) { ccat(v->issue, sizeof v->issue, "La tarea pide una ventana grafica y no hay win.create. "); v->issues++; }
    if (cfind(src, "win.create") && !cfind(src, "win.present")) { ccat(v->issue, sizeof v->issue, "Falta win.present() en el bucle de dibujo. "); v->issues++; }
    if (cfind(src, "win.create") && !cfind(src, "win.active")) { ccat(v->issue, sizeof v->issue, "Falta el bucle while (win.active()) { ... }. "); v->issues++; }
    /* SOMA_235_PROGRAMADOR: errores que compilan pero no funcionan, detectados en pruebas reales con un modelo */
    if (ci_has(g_mission, "flecha") && cfind(src, "key()") && !cfind(src, "== 16") && !cfind(src, "==16") && !cfind(src, "== 14") && !cfind(src, "==14")) {
        ccat(v->issue, sizeof v->issue, "La tarea usa las FLECHAS y el programa no usa sus codigos en SOMA: arriba 16, abajo 14, izquierda 2, derecha 6 (no 37-40). "); v->issues++;
    }
    if ((ci_has(g_mission, "hora") || ci_has(g_mission, "reloj")) && !ci_has(g_mission, "cronometro") && !ci_has(g_mission, "cronómetro")
        && !ci_has(g_mission, "temporizador") && !ci_has(g_mission, "milisegund") && !ci_has(g_mission, "desde") && !ci_has(g_mission, "cuenta atras")
        && !cfind(src, "sys.hour") && (cfind(src, "sys.time") || cfind(src, "sys.millis"))) {
        ccat(v->issue, sizeof v->issue, "sys.millis()/sys.time() es el tiempo desde que arranco el equipo, no la hora: usa sys.hour(), sys.minute() y sys.second(). "); v->issues++;
    }
    if (orig && orig_n && orig_n == n) { uint32_t i = 0; while (i < n && orig[i] == src[i]) i++; if (i == n) { v->unchanged = 1; ccat(v->issue, sizeof v->issue, "El codigo es IDENTICO al original: no se aplico el cambio pedido. "); v->issues++; } }
    if (v->compiles && !gui && v->hard == 0 && !v->truncated) run_tests(v->bc, v);
    v->score = (v->compiles ? 0 : 1000) + v->hard * 100 + v->issues * 60 + v->warns * 3 + v->tfail * 300;
}
static int verdict_ok(const struct verdict *v) { return v->compiles && v->hard == 0 && v->issues == 0; }

/* SOMA_235: linea con forma de bloque de Python ("if n == 0:", "else:", "for i in range(10):") */
static int py_block_line(const char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    static const char *const kw[] = { "if ", "elif ", "else", "while ", "for ", "def ", "try", "except", 0 };
    int k = 0; for (int i = 0; kw[i] && !k; i++) { const char *a = t, *b = kw[i]; while (*b && *a == *b) { a++; b++; } if (!*b) k = 1; }
    if (!k) return 0;
    const char *e = t; while (*e) e++;
    while (e > t && (e[-1] == ' ' || e[-1] == '\r')) e--;
    return e > t && e[-1] == ':';
}
static const char *hint_for_py(const struct verdict *v)
{
    const char *m = v->msg;
    if (v->tfail) return "El programa funciona pero no produce la salida esperada en las pruebas: compara la salida obtenida con la esperada y corrige la logica o el formato (print separa con un espacio; usa sep='' o f-strings si hace falta).";
    if (cfind(m, "IndentationError")) return "Error de SANGRIA: usa 4 espacios por nivel, los mismos en todo el bloque, y nunca tabuladores mezclados.";
    if (cfind(m, "SyntaxError")) return "Error de sintaxis de Python: revisa los ':' al final de if/for/while/def, parentesis y comillas sin cerrar. No uses llaves ni ';' de C.";
    if (cfind(m, "ImportError") || cfind(m, "no module")) return "Ese modulo no existe en SOMA. Hay math, random, time, json, re, collections y soma (from soma import win). Nada de os, pygame ni tkinter.";
    if (cfind(m, "NameError")) return "Usas un nombre que no existe: define la variable o funcion antes de usarla, o importa el modulo (import time, from soma import win).";
    if (cfind(m, "AttributeError")) return "Atributo que no existe: win tiene create clear rect text line circle pixel present active mouse key size rgb close; el raton m.x m.y m.clicked m.button.";
    if (cfind(m, "TypeError")) return "Tipos o numero de argumentos incorrectos: win.rect(x, y, w, h, color), win.text(x, y, texto, color), win.circle(x, y, r, color). Convierte con int() o str().";
    if (cfind(m, "EOFError")) return "El programa pide mas datos con input() de los que da la prueba: lee exactamente las lineas de la entrada.";
    if (v->unchanged) return "Aplica el cambio pedido: el codigo devuelto es igual que el original.";
    return "Corrige solo lo necesario y conserva el resto.";
}
static const char *hint_for(const struct verdict *v)
{
    if (g_py) return hint_for_py(v);
    const char *m = v->msg;
    if (py_block_line(v->ltext)) return "Eso es un bloque de Python: en SOMA-LANG la condicion va entre parentesis y el bloque entre llaves, sin ':'. Ej.: if (n == 0) { print(\"cero\"); } else { ... }";
    if (v->tfail) return "El programa COMPILA pero no produce la salida esperada en las pruebas: compara la salida obtenida con la esperada, localiza el error de logica (limites de bucle, condiciones, orden, formato) y corrigelo sin cambiar lo demas.";
    for (const char *t = v->ltext; t[0] && t[1] && t[2]; t++)
        if (t[0] >= '0' && t[0] <= '9' && t[1] == '.' && t[2] >= '0' && t[2] <= '9') return "SOMA-LANG solo tiene ENTEROS: no escribas decimales (1.5). Usa enteros escalados (15 decimas) o divisiones enteras.";
    if (cfind(v->ltext, "def ") || cfind(v->ltext, "):")) return "Eso es Python: las funciones son fn nombre(a) { ... } y los bloques van entre llaves.";
    if (cfind(m, "Nombre de funcion esperado") || cfind(v->token, "spawn") == v->token || cfind(v->token, "agent") == v->token || cfind(v->token, "loop") == v->token || cfind(v->token, "proc") == v->token)
        return "Has usado una PALABRA RESERVADA como nombre (spawn, agent, proc, loop, import, ask, mem, fb...). Renombra esa variable o funcion (por ejemplo nueva_comida).";
    if (cfind(m, "Funcion no definida")) return "Declara cada funcion con fn nombre(a) { ... } ANTES de la primera llamada. Las del sistema: print input len push pop to_str to_int substr rand, sys.* y win.*.";
    if (cfind(m, "Metodo desconocido en win")) return "Metodos de win: create(t,x,y,w,h) clear(c) rect(x,y,w,h,c) text(x,y,t,c) line(x1,y1,x2,y2,c) circle(x,y,r,c) present() active() mouse() close(). Sin manejador de ventana.";
    if (cfind(m, "Token invalido")) return "No hay plantillas `${}`, ni comillas tipograficas, ni numeros decimales: concatena con + y to_str().";
    if (cfind(m, "Expresion esperada")) return "Revisa operadores: no existen ++, --, ?:, lambdas ni 'new'. Usa x = x + 1.";
    if (m[0] == ')' || cfind(m, "')'")) return "Numero de argumentos incorrecto o parentesis sin cerrar. En win.* no se pasa la ventana como primer argumento.";
    if (m[0] == ';' || cfind(m, "';'")) return "Sentencia mal formada: usa 'let' (no var/const), 'fn' (no function/def) y termina con ;.";
    if (m[0] == '{' || cfind(m, "'{'")) return "Los bloques llevan llaves: if (c) { ... }  while (c) { ... }  fn f() { ... }. Sin ':' de Python.";
    if (v->hard) return "El analisis estatico encontro construcciones prohibidas (Python o placeholders): reescribe esas lineas en SOMA-LANG.";
    if (v->truncated) return "Devuelve el programa ENTERO y cerrado; si es largo, simplificalo para que quepa.";
    return "Corrige solo lo necesario y conserva el resto.";
}

/* ------------------------------------------------------------------ prompts */
static char g_prompt[24576];
static char g_reply[CC_SRC_MAX + 8192];
static char g_cand[CC_SRC_MAX + 4096];
static char g_best[CC_SRC_MAX + 4096];
static char g_orig[CC_SRC_MAX];
static char g_lessons[2048];

static void add_lessons(void)
{
    if (!g_lessons[0]) return;
    ccat(g_prompt, sizeof g_prompt, "\nLECCIONES DE TRABAJOS ANTERIORES (errores que ya cometiste; no los repitas):\n");
    ccat(g_prompt, sizeof g_prompt, g_lessons);
}
static void add_numbered(const char *src, uint32_t n, uint32_t center, int radius)
{
    uint32_t line = 1; const char *p = src, *e = src + n;
    while (p < e) {
        const char *ls = p; while (p < e && *p != '\n') p++;
        if (!center || ((int)line >= (int)center - radius && (int)line <= (int)center + radius)) {
            ccat(g_prompt, sizeof g_prompt, line == center ? ">> " : "   "); cnum(g_prompt, sizeof g_prompt, line); ccat(g_prompt, sizeof g_prompt, ": ");
            ccatn(g_prompt, sizeof g_prompt, ls, (uint32_t)(p - ls)); ccat(g_prompt, sizeof g_prompt, "\n");
        }
        if (p < e) p++;
        line++;
    }
}
static void add_tests(void)       /* PARCHE 116: los casos de prueba forman parte del encargo */
{
    const struct cc_env *e = g_te;
    if (!e || !e->run || !e->tests || e->ntests <= 0) return;
    ccat(g_prompt, sizeof g_prompt, "\nPRUEBAS (el programa se ejecuta contra ellas; debe producir EXACTAMENTE esa salida):\n");
    for (int i = 0; i < e->ntests; i++) {
        char a[96], b[96]; oneline(a, sizeof a, e->tests[i].input ? e->tests[i].input : "", 60); oneline(b, sizeof b, e->tests[i].expect, 80);
        ccat(g_prompt, sizeof g_prompt, "- "); ccat(g_prompt, sizeof g_prompt, e->tests[i].name ? e->tests[i].name : "caso");
        ccat(g_prompt, sizeof g_prompt, ": entrada \""); ccat(g_prompt, sizeof g_prompt, a); ccat(g_prompt, sizeof g_prompt, "\" -> salida \""); ccat(g_prompt, sizeof g_prompt, b); ccat(g_prompt, sizeof g_prompt, "\"\n");
    }
}
static void build_gen(const char *mission, const char *plan, int gui, int edit, const char *retry_note)
{
    g_prompt[0] = 0;
    if (g_py) ccat(g_prompt, sizeof g_prompt, "Eres un programador experto en Python. Escribe para el Python de SOMA OS (MicroPython) codigo correcto a la primera.\n\n");
    else ccat(g_prompt, sizeof g_prompt, "Eres un programador experto en SOMA-LANG, el lenguaje de SOMA OS. Escribe codigo correcto a la primera.\n\n");
    ccat(g_prompt, sizeof g_prompt, ref_txt());
    ccat(g_prompt, sizeof g_prompt, "\nEJEMPLO VERIFICADO (funciona en "); ccat(g_prompt, sizeof g_prompt, lang_name()); ccat(g_prompt, sizeof g_prompt, "):\n```");
    ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, "\n");
    ccat(g_prompt, sizeof g_prompt, g_py ? (gui ? EX_GUI_PY : EX_CLI_PY) : (gui ? EX_GUI : EX_CLI));
    ccat(g_prompt, sizeof g_prompt, "```\n");
    add_lessons();
    if (edit) {
        ccat(g_prompt, sizeof g_prompt, "\nPROGRAMA ACTUAL (modificalo; conserva todo lo que la tarea no pide cambiar):\n```"); ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, "\n");
        ccat(g_prompt, sizeof g_prompt, g_orig);
        ccat(g_prompt, sizeof g_prompt, "\n```\n");
    }
    ccat(g_prompt, sizeof g_prompt, "\nTAREA: "); ccat(g_prompt, sizeof g_prompt, mission);
    add_tests();
    if (plan && plan[0]) { ccat(g_prompt, sizeof g_prompt, "\nPLAN: "); ccat(g_prompt, sizeof g_prompt, plan); }
    if (retry_note && retry_note[0]) { ccat(g_prompt, sizeof g_prompt, "\nNOTA: "); ccat(g_prompt, sizeof g_prompt, retry_note); }
    ccat(g_prompt, sizeof g_prompt, "\n\nResponde SOLO con el programa COMPLETO dentro de un bloque ```"); ccat(g_prompt, sizeof g_prompt, fence_tag());
    ccat(g_prompt, sizeof g_prompt, " ... ```. Sin explicaciones, sin '...', sin omitir partes.");
}
static void build_repair(const char *mission, const char *cand, uint32_t n, const struct verdict *v)
{
    g_prompt[0] = 0;
    ccat(g_prompt, sizeof g_prompt, "Corrige este programa "); ccat(g_prompt, sizeof g_prompt, lang_name()); ccat(g_prompt, sizeof g_prompt, " para que funcione y cumpla la tarea.\n\n");
    ccat(g_prompt, sizeof g_prompt, ref_txt());
    add_lessons();
    ccat(g_prompt, sizeof g_prompt, "\nTAREA: "); ccat(g_prompt, sizeof g_prompt, mission);
    add_tests();
    ccat(g_prompt, sizeof g_prompt, "\n\nPROBLEMAS DETECTADOS:\n");
    if (!v->compiles) {
        ccat(g_prompt, sizeof g_prompt, g_py ? "- Error de Python en la linea " : "- Error del compilador en la linea "); cnum(g_prompt, sizeof g_prompt, v->line);
        ccat(g_prompt, sizeof g_prompt, ": "); ccat(g_prompt, sizeof g_prompt, v->msg);
        if (v->token[0]) { ccat(g_prompt, sizeof g_prompt, " (cerca de '"); ccat(g_prompt, sizeof g_prompt, v->token); ccat(g_prompt, sizeof g_prompt, "')"); }
        ccat(g_prompt, sizeof g_prompt, "\n");
        if (v->line) { ccat(g_prompt, sizeof g_prompt, "  Contexto:\n"); add_numbered(cand, n, v->line, 3); }
    }
    if (!g_py) for (uint32_t i = 0; i < g_rep.count; i++) if (g_rep.msg[i][0] == 'E') { ccat(g_prompt, sizeof g_prompt, "- "); ccat(g_prompt, sizeof g_prompt, g_rep.msg[i]); ccat(g_prompt, sizeof g_prompt, "\n"); }
    if (v->issue[0]) { ccat(g_prompt, sizeof g_prompt, "- "); ccat(g_prompt, sizeof g_prompt, v->issue); ccat(g_prompt, sizeof g_prompt, "\n"); }
    ccat(g_prompt, sizeof g_prompt, "PISTA: "); ccat(g_prompt, sizeof g_prompt, hint_for(v));
    ccat(g_prompt, sizeof g_prompt, "\n\nPROGRAMA A CORREGIR:\n```"); ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, "\n"); ccatn(g_prompt, sizeof g_prompt, cand, n);
    ccat(g_prompt, sizeof g_prompt, "\n```\n\nResponde SOLO con el programa COMPLETO corregido dentro de un bloque ```"); ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, " ... ```.");
}

/* ------------------------------------------------------------------ lecciones */
static void lessons_load(const struct cc_env *e)
{
    g_lessons[0] = 0;
    if (!e->read) return;
    int r = e->read(g_py ? CC_LESSONS_PY : CC_LESSONS, g_lessons, sizeof g_lessons - 1);
    if (r < 0) r = 0;
    g_lessons[r] = 0;
}
static void lessons_add(const struct cc_env *e, const struct verdict *v)
{
    if (!e->write || (!v->msg[0] && !v->issue[0])) return;
    char line[256]; line[0] = 0;
    ccat(line, sizeof line, "- ");
    if (v->msg[0]) { ccat(line, sizeof line, "Error '"); ccat(line, sizeof line, v->msg); ccat(line, sizeof line, "': "); }
    else { ccatn(line, sizeof line, v->issue, 80); ccat(line, sizeof line, ": "); }
    ccat(line, sizeof line, hint_for(v)); ccat(line, sizeof line, "\n");
    if (cfind(g_lessons, line)) return;
    static char nb[2048]; nb[0] = 0;
    uint32_t total = cl(g_lessons) + cl(line);
    const char *p = g_lessons;
    while (total > 1600 && *p) { while (*p && *p != '\n') { p++; total--; } if (*p) { p++; total--; } }   /* se olvidan las mas viejas */
    ccat(nb, sizeof nb, p); ccat(nb, sizeof nb, line);
    e->write(g_py ? CC_LESSONS_PY : CC_LESSONS, nb, cl(nb));
    ccpy(g_lessons, sizeof g_lessons, nb);
}

/* ------------------------------------------------------------------ bucle principal */
static void say(const struct cc_env *e, const char *m) { if (e->log) e->log(m); }
static void st(const struct cc_env *e, int ph, const char *d, const char *t) { if (e->status) e->status(ph, d, t); }
static int stop(const struct cc_env *e) { return e->cancelled && e->cancelled(); }

static const char *g_kind = "";
static int ask(const struct cc_env *e, struct cc_result *r, uint32_t timeout)
{
    if (r->llm_calls >= (e->max_llm_calls ? e->max_llm_calls : 7)) return 0;
    r->llm_calls++;
    g_reply[0] = 0;
    ccat(g_prompt, sizeof g_prompt, "\n/no_think");                    /* modelos con razonamiento: respuesta directa */
    int ok = e->llm(g_prompt, g_reply, sizeof g_reply, timeout) && g_reply[0];
    if (e->trace) e->trace(g_kind, g_prompt, ok ? g_reply : "(sin respuesta del modelo)");
    return ok;
}
static char g_tmp[CC_SRC_MAX + 4096];
/* un ``` solo cuenta como valla si empieza linea (los ``` dentro de una frase son prosa) */
static const char *fence(const char *from, const char *start)
{
    for (const char *p = cfind(from, "```"); p; p = cfind(p + 3, "```")) {
        const char *b = p; while (b > start && b[-1] == ' ') b--;
        if (b == start || b[-1] == '\n') return p;
    }
    return 0;
}
/* PARCHE 097: la respuesta puede traer varios bloques (el razonamiento tambien escribe ``` ...): se prueban TODOS y el texto sin
 * bloques, y gana el que mejor verifica. Asi nunca se compila prosa como si fuera codigo. */
static uint32_t take(const struct cc_env *e, struct cc_result *r, int gui, int edit, struct verdict *v)
{
    struct verdict tv; uint32_t best_n = 0; int have = 0, best_fix = 0;
    const char *base = g_reply, *t = cfind(g_reply, "</think>");
    if (t) base = t + 8;
    for (int pass = 0; pass < 2; pass++) {
        const char *scan = pass == 0 ? base : g_reply;                    /* 2a pasada: tambien dentro del razonamiento */
        if (pass == 1 && (scan == base || have)) break;
        const char *p = fence(scan, g_reply);
        while (p) {
            const char *s = p + 3; while (*s && *s != '\n') s++; if (*s == '\n') s++;
            const char *q = fence(s, g_reply);
            uint32_t n = q ? (uint32_t)(q - s) : cl(s); if (n >= CC_SRC_MAX) n = CC_SRC_MAX - 1;
            cmove(g_tmp, s, n); g_tmp[n] = 0;
            int f = g_py ? cc_py_normalize(g_tmp, &n, CC_SRC_MAX) : cc_normalize(g_tmp, &n, CC_SRC_MAX);
            verify(g_tmp, n, gui, edit ? g_orig : 0, edit ? cl(g_orig) : 0, &tv);
            if (!have || tv.score < v->score) { *v = tv; cmove(g_cand, g_tmp, n + 1); best_n = n; best_fix = f; have = 1; }
            if (!q) break;
            p = fence(q + 3, g_reply);
        }
    }
    {   /* sin bloques (o como alternativa): prosa recortada */
        uint32_t n = cc_extract_code(base, g_tmp, CC_SRC_MAX);
        const char *f3 = cfind(g_tmp, "```"); if (f3) { n = (uint32_t)(f3 - g_tmp); g_tmp[n] = 0; }
        int f = g_py ? cc_py_normalize(g_tmp, &n, CC_SRC_MAX) : cc_normalize(g_tmp, &n, CC_SRC_MAX);
        verify(g_tmp, n, gui, edit ? g_orig : 0, edit ? cl(g_orig) : 0, &tv);
        if (!have || tv.score < v->score) { *v = tv; cmove(g_cand, g_tmp, n + 1); best_n = n; best_fix = f; have = 1; }
    }
    if (best_fix) { char m[96]; m[0] = 0; ccat(m, sizeof m, "Normalizador: "); cnum(m, sizeof m, (uint32_t)best_fix); ccat(m, sizeof m, " correcciones automaticas sin gastar llamadas."); say(e, m); r->auto_fixes += best_fix; }
    /* deja g_rep coherente con el candidato elegido (la reparacion lista sus errores) */
    verify(g_cand, best_n, gui, edit ? g_orig : 0, edit ? cl(g_orig) : 0, &tv);
    if (!v->compiles) { char m[220]; m[0] = 0; ccat(m, sizeof m, g_py ? "Candidato (Python): linea " : "Candidato: linea "); cnum(m, sizeof m, v->line); ccat(m, sizeof m, ": "); ccat(m, sizeof m, v->msg); if (v->ltext[0]) { ccat(m, sizeof m, "  |  "); ccatn(m, sizeof m, v->ltext, 70); } say(e, m); }
    else if (v->issue[0]) { char m[320]; m[0] = 0; ccat(m, sizeof m, "Candidato compila; aviso: "); ccatn(m, sizeof m, v->issue, 280); say(e, m); }
    else say(e, "Candidato correcto (compila y pasa las comprobaciones).");
    if (v->compiles && (g_py ? e->run_src != 0 : e->run != 0) && e->tests && e->ntests > 0) { char m[96]; m[0] = 0; ccat(m, sizeof m, "Pruebas de comportamiento: "); cnum(m, sizeof m, (uint32_t)v->tpass); ccat(m, sizeof m, "/"); cnum(m, sizeof m, (uint32_t)e->ntests); ccat(m, sizeof m, " superadas."); say(e, m); }
    return best_n;
}

static volatile int g_busy;
int cc_busy(void) { return g_busy; }
static int cc_run_inner(const struct cc_env *e, const char *target, const char *mission, struct cc_result *r);
int cc_run(const struct cc_env *e, const char *target, const char *mission, struct cc_result *r)
{
    if (g_busy) { for (uint32_t i = 0; i < sizeof *r; i++) ((char *)r)[i] = 0; ccpy(r->summary, sizeof r->summary, "El programador ya esta trabajando en otra tarea; espera a que termine."); return 0; }
    g_busy = 1;
    g_te = e; g_tm_valid = 0; g_pc_valid = 0;
    g_py = e->lang == CC_LANG_PYTHON;                                     /* SOMA_239 */
    int ok = cc_run_inner(e, target, mission, r);
    g_py = 0;
    g_te = 0;
    g_busy = 0;
    return ok;
}

static int cc_run_inner(const struct cc_env *e, const char *target, const char *mission, struct cc_result *r)
{
    struct verdict v, bv;
    uint32_t n = 0, bn = 0;
    int gui, edit, budget = e->max_llm_calls ? e->max_llm_calls : 7, no_improve = 0;
    uint32_t seen[8]; int nseen = 0;
    for (uint32_t i = 0; i < sizeof *r; i++) ((char *)r)[i] = 0;
    bv.score = 1 << 30; g_best[0] = 0;

    /* 1. analisis de la tarea y contexto */
    int ol = (e->read && !e->force_new) ? e->read(target, g_orig, CC_SRC_MAX - 1) : -1;
    if (ol < 0) ol = 0;
    g_orig[ol] = 0;
    edit = ol > 0;
    gui = cc_mission_is_gui(mission) || (edit && cfind(g_orig, "win.create") != 0);
    g_mission = mission ? mission : "";
    lessons_load(e);
    st(e, 1, "Fase 1/6: Analizando la tarea y preparando el contexto...", edit ? "Modificacion de un programa existente: se le pasa completo al modelo." : (gui ? "Programa nuevo con ventana grafica." : "Programa nuevo de consola."));
    say(e, edit ? "Tarea: MODIFICAR programa existente." : "Tarea: CREAR programa nuevo.");
    if (g_py) say(e, "Lenguaje: Python (MicroPython de SOMA; ventana con soma.win).");
    say(e, gui ? "Tipo: aplicacion grafica (ejemplo de ventana como referencia)." : "Tipo: programa de consola (ejemplo de consola como referencia).");
    if (g_lessons[0]) say(e, "Lecciones de trabajos anteriores incluidas en el contexto.");

    /* plan breve (solo si la tarea no es trivial y sobra presupuesto) */
    if (cl(mission) > 220 && budget >= 4) {   /* PARCHE 100: el plan aparte solo en encargos largos (una llamada lenta menos) */
        g_prompt[0] = 0;
        ccat(g_prompt, sizeof g_prompt, "Planifica en 3-5 pasos MUY breves (maximo 300 caracteres, sin codigo) un programa ");
        ccat(g_prompt, sizeof g_prompt, lang_name()); ccat(g_prompt, sizeof g_prompt, " para: ");
        ccat(g_prompt, sizeof g_prompt, mission);
        ccat(g_prompt, sizeof g_prompt, gui ? (g_py ? " (aplicacion con ventana: estado, bucle while win.active(), raton, dibujo)" : " (aplicacion con ventana: estado, bucle while (win.active()), raton, dibujo)") : " (programa de consola)");
        ccat(g_prompt, sizeof g_prompt, ". Incluye al final 'Criterio:' con lo que debe cumplir. Responde solo el plan.");
        g_kind = "PLAN";
        if (ask(e, r, 45000)) {
            const char *p = g_reply, *t = cfind(g_reply, "</think>"); if (t) p = t + 8;
            while (*p == ' ' || *p == '\n') p++;
            ccpy(r->plan, sizeof r->plan, p);
            for (char *q = r->plan; *q; q++) if (*q == '\n' || *q == '\r' || *q == '`') *q = ' ';
            if (cl(r->plan) > 420 || cfind(r->plan, "```")) r->plan[0] = 0;
            if (r->plan[0]) say(e, "Plan elaborado.");
        }
    }
    if (stop(e)) { r->cancelled = 1; ccpy(r->summary, sizeof r->summary, "Cancelado."); return 0; }

    /* 2. generacion */
    st(e, 2, "Fase 2/6: Generando el programa...", "Contexto: referencia del lenguaje, ejemplo verificado, lecciones y tarea.");
    build_gen(mission, r->plan, gui, edit, 0);
    g_kind = "GENERACION";
    if (!ask(e, r, 120000)) {
        ccpy(r->summary, sizeof r->summary, "El modelo de lenguaje no responde: no se ha tocado el archivo.");
        ccpy(r->last_error, sizeof r->last_error, "LLM no disponible");
        say(e, r->summary);
        return 0;
    }
    n = take(e, r, gui, edit, &v);
    if ((g_py ? e->run_src != 0 : e->run != 0) && e->tests && e->ntests > 0) { r->tests_total = e->ntests; r->tests_first_fail = v.compiles ? v.tfail : e->ntests; }
    seen[nseen++] = chash(g_cand, n);
    if (v.score < bv.score) { bv = v; bn = n; cmove(g_best, g_cand, n + 1); }

    /* 3-4. verificar y reparar */
    while (!verdict_ok(&bv) && !stop(e) && r->llm_calls < budget) {
        int regen = no_improve >= 2;
        char d[96]; d[0] = 0;
        ccat(d, sizeof d, regen ? "Fase 4/6: Regenerando con un enfoque mas simple (" : "Fase 4/6: Reparando (llamada ");
        cnum(d, sizeof d, (uint32_t)r->llm_calls + 1); ccat(d, sizeof d, "/"); cnum(d, sizeof d, (uint32_t)budget); ccat(d, sizeof d, ")...");
        char t[200]; t[0] = 0;
        if (!bv.compiles) { ccat(t, sizeof t, "Linea "); cnum(t, sizeof t, bv.line); ccat(t, sizeof t, ": "); ccat(t, sizeof t, bv.msg); }
        else ccatn(t, sizeof t, bv.issue[0] ? bv.issue : "lint con errores graves", 180);
        st(e, regen ? 4 : 4, d, t);
        say(e, t);
        if (regen) {
            char note[256]; note[0] = 0;
            ccat(note, sizeof note, "Un intento anterior fallo con: "); ccatn(note, sizeof note, t, 150);
            ccat(note, sizeof note, ". Escribe una version MAS SENCILLA y correcta.");
            build_gen(mission, r->plan, gui, edit, note);
            r->regenerations++; no_improve = 0;
        } else {
            build_repair(mission, g_best, bn, &bv);
            r->repairs++;
        }
        g_kind = regen ? "REGENERACION" : "REPARACION";
        if (!ask(e, r, 120000)) break;
        n = take(e, r, gui, edit, &v);
        uint32_t h = chash(g_cand, n); int dup = 0;
        for (int k = 0; k < nseen; k++) if (seen[k] == h) dup++;
        if (dup >= 2) { say(e, "El modelo repite la misma respuesta: se detiene para no gastar mas llamadas."); r->stuck = 1; break; }
        if (nseen < 8) seen[nseen++] = h;
        if (v.score < bv.score) {
            if (!verdict_ok(&bv) && verdict_ok(&v)) lessons_add(e, &bv);      /* reparacion lograda: se aprende */
            bv = v; bn = n; cmove(g_best, g_cand, n + 1); no_improve = 0;
        } else no_improve += dup ? 2 : 1;
    }
    if (stop(e)) { r->cancelled = 1; ccpy(r->summary, sizeof r->summary, "Cancelado: el archivo no se ha modificado."); return 0; }

    /* 5. revision critica (una ronda, solo si sobra presupuesto) */
    if (verdict_ok(&bv) && r->llm_calls < budget && cl(mission) > 30) {
        st(e, 5, "Fase 5/6: Revision critica del resultado...", "Comprobando que el programa cumple toda la tarea.");
        g_prompt[0] = 0;
        ccat(g_prompt, sizeof g_prompt, "Eres revisor de codigo "); ccat(g_prompt, sizeof g_prompt, lang_name()); ccat(g_prompt, sizeof g_prompt, ".\n"); ccat(g_prompt, sizeof g_prompt, ref_txt());
        ccat(g_prompt, sizeof g_prompt, "\nTAREA: "); ccat(g_prompt, sizeof g_prompt, mission);
        ccat(g_prompt, sizeof g_prompt, "\nPROGRAMA (ya compila):\n```"); ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, "\n"); ccatn(g_prompt, sizeof g_prompt, g_best, bn);
        ccat(g_prompt, sizeof g_prompt, "\n```\nSi cumple TODA la tarea responde exactamente: OK\nSi falta algo, responde SOLO con el programa completo corregido en un bloque ```");
        ccat(g_prompt, sizeof g_prompt, fence_tag()); ccat(g_prompt, sizeof g_prompt, " ... ```.");
        g_kind = "REVISION";
        if (ask(e, r, 120000)) {
            r->reviewed = 1;
            if (cfind(g_reply, "```")) {
                n = take(e, r, gui, edit, &v);
                if (verdict_ok(&v) && v.score <= bv.score + 3) { bv = v; bn = n; cmove(g_best, g_cand, n + 1); say(e, "Revision: el revisor mejoro el programa."); }
                else say(e, "Revision: la propuesta del revisor no compilaba; se conserva la version verificada.");
            } else say(e, "Revision: el programa cumple la tarea.");
        }
    }

    /* 6. entrega: se escribe una sola vez y solo si es correcto */
    r->score = bv.score;
    if (r->tests_total) r->tests_pass = bv.compiles ? bv.tpass : 0;
    if (bv.tfail == 0 && (verdict_ok(&bv) || (bv.compiles && bv.hard == 0 && !bv.truncated && !bv.unchanged && bv.issues <= 1 && bv.score < 200))) {
        if (e->write && e->write(target, g_best, bn) >= 0) {
            r->ok = 1; r->wrote = 1; r->bc_len = bv.bc;
            ccpy(r->summary, sizeof r->summary, verdict_ok(&bv) ? "Programa verificado y guardado." : "Programa guardado (compila; con un aviso).");
            if (!verdict_ok(&bv)) ccpy(r->last_error, sizeof r->last_error, bv.issue);
        } else ccpy(r->summary, sizeof r->summary, "No se pudo escribir el archivo.");
    } else {
        if (!bv.compiles) { ccpy(r->last_error, sizeof r->last_error, "Linea "); cnum(r->last_error, sizeof r->last_error, bv.line); ccat(r->last_error, sizeof r->last_error, ": "); ccat(r->last_error, sizeof r->last_error, bv.msg); }
        else ccpy(r->last_error, sizeof r->last_error, bv.issue[0] ? bv.issue : "errores graves de lint");
        ccpy(r->summary, sizeof r->summary, "No se logro un programa correcto; el archivo queda como estaba.");
    }
    say(e, r->summary);
    return r->ok;
}
