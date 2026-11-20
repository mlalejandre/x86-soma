/* SOMA_237_PYTHON: interprete de Python para SOMA. Es MicroPython (third_party/micropython, licencia MIT) compilado como un
 * programa normal de ring 3: un error en un script nunca tumba el sistema, igual que el resto de programas de usuario.
 *
 *   python                     modo interactivo (>>>); se sale con exit() o con una linea vacia tras Ctrl+D / fin de entrada
 *   python script.py [args]    ejecuta el script (rutas relativas: /workspace/script.py); sys.argv = [script, args...]
 *   python -c "codigo"         ejecuta una linea de codigo
 *
 * Lo que ve un script: print(), input(), import de otros .py de /workspace, enteros grandes, float de doble precision, math,
 * str/bytes/list/dict/set, clases, excepciones, generadores... (la configuracion esta en src/user/python/mpconfigport.h).
 * Lo que necesita del nucleo son dos llamadas al sistema que solo tiene este proceso (las da la orden "python" en su filtro):
 *   30 SYS_SOMA_CON: 0 = leer una linea de la consola, 1 = reloj del equipo
 *   31 SYS_SOMA_VFS: 0 = leer un archivo, 1 = escribirlo (solo /workspace), 2 = tamano. Nunca rutas privadas (/etc, /agent...).
 * Modulo propio `soma`: leer(ruta), escribir(ruta, texto), existe(ruta), reloj() -> (anio, mes, dia, h, m, s), ms(), dormir(ms).
 * Ademas: open() (texto, en /workspace para escribir), time (time, sleep, sleep_ms, ticks_ms, ticks_diff, monotonic, localtime),
 * random, json, re, heapq, binascii, math, cmath, struct, collections, array, gc, errno. */
#include <stdint.h>
#include <string.h>
#include "ulib.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/repl.h"
#include "py/lexer.h"
#include "py/builtin.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "shared/runtime/gchelper.h"

#define SYS_SOMA_CON 30
#define SYS_SOMA_VFS 31
struct soma_vfs_req { const char *path; u64 plen; void *buf; u64 len; };

static char g_heap[6u << 20];                     /* memoria de Python (recolector de basura): 6 MiB */
static char g_args[128];
static char g_path[128];

/* ------------------------------------------------------------------ entrada y salida */
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) { size_t t = len; while (len) { i64 n = u_write(str, len); if (n <= 0) break; str += n; len -= (size_t)n; } return t - len; }
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) { mp_hal_stdout_tx_strn(str, len); }
void mp_hal_stdout_tx_str(const char *str) { mp_hal_stdout_tx_strn(str, strlen(str)); }
int mp_hal_stdin_rx_chr(void) { return -1; }

static i64 read_line(char *buf, u64 max)              /* <0: no hay mas entrada */
{
    i64 n = u_sys3(SYS_SOMA_CON, 0, (i64)buf, (i64)(max - 1));
    if (n < 0) { buf[0] = 0; return n; }
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return n;
}

static mp_obj_t soma_input(size_t n_args, const mp_obj_t *args)
{
    if (n_args == 1) mp_obj_print(args[0], PRINT_STR);
    static char line[512];
    if (read_line(line, sizeof line) < 0) mp_raise_type(&mp_type_EOFError);
    return mp_obj_new_str(line, strlen(line));
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(soma_input_obj, 0, 1, soma_input);
static mp_obj_t soma_exit(size_t n_args, const mp_obj_t *args)   /* exit() / quit(), como en CPython */
{
    mp_raise_type_arg(&mp_type_SystemExit, n_args ? args[0] : mp_const_none);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(soma_exit_obj, 0, 1, soma_exit);

/* ------------------------------------------------------------------ archivos de SOMA (VFS) */
static const char *full_path(const char *p)
{
    if (p[0] == '/') return p;
    size_t k = 0; const char *w = "/workspace/";
    while (*w && k < sizeof g_path - 1) g_path[k++] = *w++;
    while (*p && k < sizeof g_path - 1) g_path[k++] = *p++;
    g_path[k] = 0;
    return g_path;
}
static i64 vfs_op(int op, const char *path, void *buf, u64 len)
{
    struct soma_vfs_req r = { path, strlen(path), buf, len };
    return u_sys3(SYS_SOMA_VFS, op, (i64)&r, 0);
}
static void raise_vfs(i64 e) { mp_raise_OSError(e == -13 ? MP_EACCES : e == -2 ? MP_ENOENT : e == -36 || e == -22 ? MP_EINVAL : MP_EIO); }

mp_import_stat_t mp_import_stat(const char *path)
{
    return vfs_op(2, full_path(path), 0, 0) >= 0 ? MP_IMPORT_STAT_FILE : MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
    const char *p = full_path(qstr_str(filename));
    i64 sz = vfs_op(2, p, 0, 0);
    if (sz < 0) raise_vfs(sz);
    char *buf = m_new(char, (size_t)sz + 1);
    i64 n = vfs_op(0, p, buf, (u64)sz);
    if (n < 0) { m_del(char, buf, (size_t)sz + 1); raise_vfs(n); }
    buf[n] = 0;
    return mp_lexer_new_from_str_len(filename, buf, (size_t)n, (size_t)sz + 1);
}

/* ------------------------------------------------------------------ modulo soma (archivos y reloj del sistema) */
static mp_obj_t soma_leer(mp_obj_t ruta)
{
    const char *p = full_path(mp_obj_str_get_str(ruta));
    i64 sz = vfs_op(2, p, 0, 0);
    if (sz < 0) raise_vfs(sz);
    char *buf = m_new(char, (size_t)sz + 1);
    i64 n = vfs_op(0, p, buf, (u64)sz);
    if (n < 0) { m_del(char, buf, (size_t)sz + 1); raise_vfs(n); }
    mp_obj_t r = mp_obj_new_str(buf, (size_t)n);
    m_del(char, buf, (size_t)sz + 1);
    return r;
}
static MP_DEFINE_CONST_FUN_OBJ_1(soma_leer_obj, soma_leer);

static mp_obj_t soma_escribir(mp_obj_t ruta, mp_obj_t texto)
{
    size_t len; const char *t = mp_obj_str_get_data(texto, &len);
    i64 r = vfs_op(1, full_path(mp_obj_str_get_str(ruta)), (void *)t, len);
    if (r < 0) raise_vfs(r);
    return mp_obj_new_int((mp_int_t)len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(soma_escribir_obj, soma_escribir);

static mp_obj_t soma_existe(mp_obj_t ruta) { return mp_obj_new_bool(vfs_op(2, full_path(mp_obj_str_get_str(ruta)), 0, 0) >= 0); }
static MP_DEFINE_CONST_FUN_OBJ_1(soma_existe_obj, soma_existe);

static mp_obj_t soma_reloj(void)                    /* (anio, mes, dia, hora, minuto, segundo) del reloj del equipo */
{
    i64 t[6] = { 0 };
    u_sys3(SYS_SOMA_CON, 1, (i64)t, 0);
    mp_obj_t v[6];
    for (int i = 0; i < 6; i++) v[i] = mp_obj_new_int((mp_int_t)t[i]);
    return mp_obj_new_tuple(6, v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(soma_reloj_obj, soma_reloj);

static mp_obj_t soma_ms(void) { return mp_obj_new_int_from_ull(u_uptime_ms()); }      /* milisegundos desde el arranque */
static MP_DEFINE_CONST_FUN_OBJ_0(soma_ms_obj, soma_ms);

static mp_obj_t soma_dormir(mp_obj_t ms) { mp_int_t v = mp_obj_get_int(ms); u_sleep(v < 0 ? 0 : (u64)v); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_1(soma_dormir_obj, soma_dormir);

/* modulo time (lo basico de CPython/MicroPython sobre el reloj de SOMA) */
static const unsigned char dias_mes[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int bisiesto(i64 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static mp_obj_t time_time(void)                     /* segundos desde 1970 (hora local del reloj del equipo) */
{
    i64 t[6] = { 0 };
    u_sys3(SYS_SOMA_CON, 1, (i64)t, 0);
    i64 d = 0;
    for (i64 y = 1970; y < t[0]; y++) d += bisiesto(y) ? 366 : 365;
    for (i64 m = 1; m < t[1] && m <= 12; m++) d += dias_mes[m - 1] + (m == 2 && bisiesto(t[0]));
    d += t[2] - 1;
    return mp_obj_new_float((mp_float_t)(d * 86400 + t[3] * 3600 + t[4] * 60 + t[5]) + (mp_float_t)(u_uptime_ms() % 1000) / 1000);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_time_obj, time_time);
static mp_obj_t time_sleep(mp_obj_t s) { mp_float_t v = mp_obj_get_float(s); if (v > 0) u_sleep((u64)(v * 1000)); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_obj, time_sleep);
static mp_obj_t time_monotonic(void) { return mp_obj_new_float((mp_float_t)u_uptime_ms() / 1000); }
static MP_DEFINE_CONST_FUN_OBJ_0(time_monotonic_obj, time_monotonic);
static mp_obj_t time_ticks_diff(mp_obj_t a, mp_obj_t b) { return mp_obj_new_int(mp_obj_get_int(a) - mp_obj_get_int(b)); }
static MP_DEFINE_CONST_FUN_OBJ_2(time_ticks_diff_obj, time_ticks_diff);
static mp_obj_t time_localtime(size_t n_args, const mp_obj_t *args)   /* (anio, mes, dia, h, m, s, dia_semana 0=lunes, dia_del_anio) */
{
    (void)n_args; (void)args;
    i64 t[6] = { 0 };
    u_sys3(SYS_SOMA_CON, 1, (i64)t, 0);
    i64 yd = t[2];
    for (i64 m = 1; m < t[1] && m <= 12; m++) yd += dias_mes[m - 1] + (m == 2 && bisiesto(t[0]));
    i64 d = 0;
    for (i64 y = 1970; y < t[0]; y++) d += bisiesto(y) ? 366 : 365;
    d += yd - 1;
    mp_obj_t v[8];
    for (int i = 0; i < 6; i++) v[i] = mp_obj_new_int((mp_int_t)t[i]);
    v[6] = mp_obj_new_int((mp_int_t)((d + 3) % 7));     /* 1/1/1970 fue jueves */
    v[7] = mp_obj_new_int((mp_int_t)yd);
    return mp_obj_new_tuple(8, v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(time_localtime_obj, 0, 1, time_localtime);
static const mp_rom_map_elem_t time_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_time) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&time_time_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&time_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&soma_dormir_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&soma_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_diff), MP_ROM_PTR(&time_ticks_diff_obj) },
    { MP_ROM_QSTR(MP_QSTR_monotonic), MP_ROM_PTR(&time_monotonic_obj) },
    { MP_ROM_QSTR(MP_QSTR_localtime), MP_ROM_PTR(&time_localtime_obj) },
};
static MP_DEFINE_CONST_DICT(time_module_globals, time_module_globals_table);
const mp_obj_module_t soma_time_module = { .base = { &mp_type_module }, .globals = (mp_obj_dict_t *)&time_module_globals };
MP_REGISTER_MODULE(MP_QSTR_time, soma_time_module);
unsigned int soma_random_seed(void) { return (unsigned int)(u_uptime_ms() * 2654435761u) ^ 0x5D0A5EEDu; }

/* open(): archivos de texto del VFS de SOMA, escrito en Python y cargado al arrancar el interprete. Se lee entero al abrir; cada
 * write() se guarda al momento (SOMA_239: antes solo al cerrar y open(...).write(...) sin close no guardaba nada). Modos r, w, a (+b). */
static const char PRELUDE[] =
    "def _soma_prelude():\n"
    " import soma\n"
    " class _Archivo:\n"
    "  def __init__(s, r, m):\n"
    "   s.name = r; s.mode = m; s._p = 0; s._r = r\n"
    "   if 'r' in m: s._d = soma.leer(r)\n"
    "   elif 'a' in m: s._d = soma.leer(r) if soma.existe(r) else ''\n"
    "   else: s._d = ''\n"
    "   if m[0] != 'r': soma.escribir(r, s._d)\n"
    "  def _o(s, t): return t.encode() if 'b' in s.mode else t\n"
    "  def read(s, n=-1):\n"
    "   e = len(s._d) if n is None or n < 0 else min(len(s._d), s._p + n)\n"
    "   t = s._d[s._p:e]; s._p = e; return s._o(t)\n"
    "  def readline(s):\n"
    "   i = s._d.find('\\n', s._p); e = len(s._d) if i < 0 else i + 1\n"
    "   t = s._d[s._p:e]; s._p = e; return s._o(t)\n"
    "  def readlines(s): return [l for l in s]\n"
    "  def __iter__(s): return s\n"
    "  def __next__(s):\n"
    "   t = s.readline()\n"
    "   if not t: raise StopIteration\n"
    "   return t\n"
    "  def write(s, t):\n"
    "   if s._r is None or s.mode[0] == 'r': raise OSError(9)\n"
    "   if isinstance(t, (bytes, bytearray)): t = bytes(t).decode()\n"
    "   s._d += t; soma.escribir(s._r, s._d); return len(t)\n"
    "  def flush(s):\n"
    "   if s._r is not None and s.mode[0] != 'r': soma.escribir(s._r, s._d)\n"
    "  def close(s): s.flush(); s._r = None\n"
    "  def __enter__(s): return s\n"
    "  def __exit__(s, *a): s.close()\n"
    " def _open(file, mode='r', *a, **k):\n"
    "  if not mode or mode[0] not in 'rwa': raise ValueError('modo no valido: ' + mode)\n"
    "  return _Archivo(file, mode)\n"
    " soma._fijar_open(_open)\n"
    "_soma_prelude()\n"
    "del _soma_prelude\n";

/* open() de verdad (el de builtins e io.open): llama a la implementacion en Python del preludio */
MP_REGISTER_ROOT_POINTER(mp_obj_t soma_open_fn);
static mp_obj_t soma_fijar_open(mp_obj_t f) { MP_STATE_VM(soma_open_fn) = f; return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_1(soma_fijar_open_obj, soma_fijar_open);
static mp_obj_t soma_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kw)
{
    if (MP_STATE_VM(soma_open_fn) == MP_OBJ_NULL) mp_raise_OSError(MP_EIO);
    mp_obj_t a[12]; size_t k = 0;
    if (n_args + 2 * kw->used > 12) mp_raise_TypeError(MP_ERROR_TEXT("open: demasiados argumentos"));
    for (size_t i = 0; i < n_args; i++) a[i] = args[i];
    for (size_t i = 0; i < kw->alloc; i++)
        if (mp_map_slot_is_filled(kw, i)) { a[n_args + 2 * k] = kw->table[i].key; a[n_args + 2 * k + 1] = kw->table[i].value; k++; }
    return mp_call_function_n_kw(MP_STATE_VM(soma_open_fn), n_args, k, a);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, soma_builtin_open);

/* SOMA_239_PYTHON_GRAFICO: soma.win, la ventana de las aplicaciones (la misma API que win.* de SOMA-LANG).
 *   from soma import win
 *   win.create("Titulo", 400, 300)      (o win.create("Titulo", x, y, ancho, alto), como en SOMA-LANG)
 *   while win.active():
 *       m = win.mouse(); k = win.key()  m.x m.y m.clicked m.button m.inside; k: 0, win.UP/DOWN/LEFT/RIGHT, 10 Intro, ord('a')...
 *       win.clear(c); win.rect(x, y, w, h, c); win.text(x, y, "hola", c); win.line(...); win.circle(x, y, r, c); win.pixel(x, y, c)
 *       win.present(); time.sleep_ms(16)
 * Las ordenes de dibujo se acumulan aqui y van al nucleo en lotes (una llamada al sistema por fotograma, no una por rectangulo). */
#define PYG_CLEAR 1
#define PYG_PIXEL 2
#define PYG_RECT  3
#define PYG_LINE  4
#define PYG_CIRC  5
#define PYG_TEXT  6
static int32_t g_gb[8192];
static uint32_t g_gn;
static void gb_flush(void)
{
    if (g_gn) u_sys3(SYS_SOMA_CON, 4, (i64)g_gb, (i64)(g_gn * 4));
    g_gn = 0;
}
static void gb_need(uint32_t words) { if (g_gn + words > sizeof g_gb / 4) gb_flush(); }
static int32_t ai(mp_obj_t o) { return (int32_t)mp_obj_get_int(o); }
static uint32_t acol(mp_obj_t o) { return (uint32_t)(mp_obj_get_int_truncated(o) & 0xFFFFFF); }

static mp_obj_t win_create(size_t n, const mp_obj_t *a)
{
    size_t tl; const char *t = n ? mp_obj_str_get_data(a[0], &tl) : "Python";
    if (!n) tl = 6;
    i64 w = 640, h = 420;
    if (n == 3) { w = ai(a[1]); h = ai(a[2]); }
    else if (n >= 5) { w = ai(a[3]); h = ai(a[4]); }
    else if (n != 1 && n != 0) mp_raise_TypeError(MP_ERROR_TEXT("win.create(titulo, ancho, alto) o win.create(titulo, x, y, ancho, alto)"));
    if (w < 120) w = 120;
    if (h < 80) h = 80;
    u64 q[4] = { (u64)t, (u64)tl, (u64)w, (u64)h };
    if (u_sys3(SYS_SOMA_CON, 2, (i64)q, 0) != 1) mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no hay escritorio: la ventana solo funciona en el escritorio de SOMA"));
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_create_obj, 0, 5, win_create);
static mp_obj_t win_close(void) { gb_flush(); u_sys3(SYS_SOMA_CON, 3, 0, 0); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_0(win_close_obj, win_close);
static void win_state(i64 st[8], int take_click) { for (int i = 0; i < 8; i++) st[i] = 0; u_sys3(SYS_SOMA_CON, 6, (i64)st, take_click); }
static mp_obj_t win_active(void) { i64 st[8]; win_state(st, 0); return mp_obj_new_bool(st[0] != 0); }
static MP_DEFINE_CONST_FUN_OBJ_0(win_active_obj, win_active);
static const qstr mouse_fields[] = { MP_QSTR_x, MP_QSTR_y, MP_QSTR_clicked, MP_QSTR_button, MP_QSTR_inside };
static mp_obj_t win_mouse(void)
{
    i64 st[8]; win_state(st, 1);
    mp_obj_t v[5] = { mp_obj_new_int((mp_int_t)st[3]), mp_obj_new_int((mp_int_t)st[4]), mp_obj_new_bool(st[7] != 0),
                      mp_obj_new_bool(st[5] != 0), mp_obj_new_bool(st[6] != 0) };
    return mp_obj_new_attrtuple(mouse_fields, 5, v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(win_mouse_obj, win_mouse);
static mp_obj_t win_size(void)
{
    i64 st[8]; win_state(st, 0);
    mp_obj_t v[2] = { mp_obj_new_int((mp_int_t)st[1]), mp_obj_new_int((mp_int_t)st[2]) };
    return mp_obj_new_tuple(2, v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(win_size_obj, win_size);
static mp_obj_t win_key(void) { return mp_obj_new_int((mp_int_t)u_sys3(SYS_SOMA_CON, 7, 0, 0)); }
static MP_DEFINE_CONST_FUN_OBJ_0(win_key_obj, win_key);
static mp_obj_t win_present(void) { gb_flush(); u_sys3(SYS_SOMA_CON, 5, 0, 0); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_0(win_present_obj, win_present);
static mp_obj_t win_clear(size_t n, const mp_obj_t *a)
{
    gb_need(2); g_gb[g_gn++] = PYG_CLEAR; g_gb[g_gn++] = (int32_t)(n ? acol(a[0]) : 0); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_clear_obj, 0, 1, win_clear);
static mp_obj_t win_pixel(mp_obj_t x, mp_obj_t y, mp_obj_t c)
{
    gb_need(4); g_gb[g_gn++] = PYG_PIXEL; g_gb[g_gn++] = ai(x); g_gb[g_gn++] = ai(y); g_gb[g_gn++] = (int32_t)acol(c); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(win_pixel_obj, win_pixel);
static mp_obj_t win_rect(size_t n, const mp_obj_t *a)
{
    (void)n; gb_need(6); g_gb[g_gn++] = PYG_RECT;
    for (int i = 0; i < 4; i++) g_gb[g_gn++] = ai(a[i]);
    g_gb[g_gn++] = (int32_t)acol(a[4]); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_rect_obj, 5, 5, win_rect);
static mp_obj_t win_line(size_t n, const mp_obj_t *a)
{
    (void)n; gb_need(6); g_gb[g_gn++] = PYG_LINE;
    for (int i = 0; i < 4; i++) g_gb[g_gn++] = ai(a[i]);
    g_gb[g_gn++] = (int32_t)acol(a[4]); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_line_obj, 5, 5, win_line);
static mp_obj_t win_circle(size_t n, const mp_obj_t *a)
{
    (void)n; gb_need(5); g_gb[g_gn++] = PYG_CIRC;
    for (int i = 0; i < 3; i++) g_gb[g_gn++] = ai(a[i]);
    g_gb[g_gn++] = (int32_t)acol(a[3]); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_circle_obj, 4, 4, win_circle);
static mp_obj_t win_text(size_t n, const mp_obj_t *a)
{
    (void)n;
    mp_obj_t so = mp_obj_is_str(a[2]) ? a[2] : mp_obj_str_make_new(&mp_type_str, 1, 0, &a[2]);   /* win.text(x, y, 42, c) tambien */
    size_t tl; const char *t = mp_obj_str_get_data(so, &tl);
    if (tl > 1024) tl = 1024;
    uint32_t words = (uint32_t)((tl + 3) / 4);
    gb_need(5 + words);
    g_gb[g_gn++] = PYG_TEXT; g_gb[g_gn++] = ai(a[0]); g_gb[g_gn++] = ai(a[1]); g_gb[g_gn++] = (int32_t)acol(a[3]); g_gb[g_gn++] = (int32_t)tl;
    char *d = (char *)(g_gb + g_gn);
    for (uint32_t i = 0; i < words * 4; i++) d[i] = i < tl ? t[i] : 0;
    g_gn += words;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(win_text_obj, 4, 4, win_text);
static mp_obj_t win_rgb(mp_obj_t r, mp_obj_t g, mp_obj_t b)
{
    return mp_obj_new_int(((ai(r) & 255) << 16) | ((ai(g) & 255) << 8) | (ai(b) & 255));
}
static MP_DEFINE_CONST_FUN_OBJ_3(win_rgb_obj, win_rgb);
static const mp_rom_map_elem_t win_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_win) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&win_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&win_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&win_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_mouse), MP_ROM_PTR(&win_mouse_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&win_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&win_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&win_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&win_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&win_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&win_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&win_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&win_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&win_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb), MP_ROM_PTR(&win_rgb_obj) },
    { MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_INT(14) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_ENTER), MP_ROM_INT(10) },
    { MP_ROM_QSTR(MP_QSTR_SPACE), MP_ROM_INT(32) },
    { MP_ROM_QSTR(MP_QSTR_BACKSPACE), MP_ROM_INT(8) },
};
static MP_DEFINE_CONST_DICT(win_module_globals, win_module_globals_table);
static const mp_obj_module_t soma_win_module = { .base = { &mp_type_module }, .globals = (mp_obj_dict_t *)&win_module_globals };

static const mp_rom_map_elem_t soma_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_soma) },
    { MP_ROM_QSTR(MP_QSTR_leer), MP_ROM_PTR(&soma_leer_obj) },
    { MP_ROM_QSTR(MP_QSTR_escribir), MP_ROM_PTR(&soma_escribir_obj) },
    { MP_ROM_QSTR(MP_QSTR_existe), MP_ROM_PTR(&soma_existe_obj) },
    { MP_ROM_QSTR(MP_QSTR_reloj), MP_ROM_PTR(&soma_reloj_obj) },
    { MP_ROM_QSTR(MP_QSTR_ms), MP_ROM_PTR(&soma_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_dormir), MP_ROM_PTR(&soma_dormir_obj) },
    { MP_ROM_QSTR(MP_QSTR__fijar_open), MP_ROM_PTR(&soma_fijar_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_win), MP_ROM_PTR(&soma_win_module) },
};
static MP_DEFINE_CONST_DICT(soma_module_globals, soma_module_globals_table);
const mp_obj_module_t soma_user_module = { .base = { &mp_type_module }, .globals = (mp_obj_dict_t *)&soma_module_globals };
MP_REGISTER_MODULE(MP_QSTR_soma, soma_user_module);

/* ------------------------------------------------------------------ ejecucion */
static int is_exit(mp_obj_t exc)
{
    return mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(mp_obj_get_type(exc)), MP_OBJ_FROM_PTR(&mp_type_SystemExit));
}
static int exit_code(mp_obj_t exc)
{
    mp_obj_t v = mp_obj_exception_get_value(exc);
    if (v == mp_const_none) return 0;
    if (mp_obj_is_int(v)) return (int)mp_obj_get_int(v);
    mp_obj_print(v, PRINT_STR); mp_hal_stdout_tx_str("\n");
    return 1;
}
/* 0 = bien; 1 = excepcion (ya impresa); 2 = SystemExit (codigo en *code) */
static int run_source(qstr name, const char *src, size_t len, mp_parse_input_kind_t kind, int *code)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(name, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t pt = mp_parse(lex, kind);
        mp_obj_t f = mp_compile(&pt, source_name, kind == MP_PARSE_SINGLE_INPUT);
        mp_call_function_0(f);
        nlr_pop();
        return 0;
    }
    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    if (is_exit(exc)) { *code = exit_code(exc); return 2; }
    mp_obj_print_exception(&mp_plat_print, exc);
    return 1;
}

static int repl(void)
{
    static char code[4096], line[512];
    mp_hal_stdout_tx_str("MicroPython " MICROPY_VERSION_STRING " en SOMA (x86_64, ring 3). exit() para salir.\n");
    for (;;) {
        mp_hal_stdout_tx_str(">>> ");
        i64 n = read_line(line, sizeof line);
        if (n < 0) return 0;
        if (n == 0) continue;
        if (!strcmp(line, "exit") || !strcmp(line, "salir") || !strcmp(line, "quit")) return 0;
        size_t k = 0;
        for (i64 i = 0; i < n && k < sizeof code - 2; i++) code[k++] = line[i];
        code[k] = 0;
        while (mp_repl_continue_with_input(code)) {
            mp_hal_stdout_tx_str("... ");
            n = read_line(line, sizeof line);
            if (n < 0) break;
            code[k++] = '\n';
            for (i64 i = 0; i < n && k < sizeof code - 2; i++) code[k++] = line[i];
            code[k] = 0;
        }
        int ec = 0;
        if (run_source(MP_QSTR__lt_stdin_gt_, code, k, MP_PARSE_SINGLE_INPUT, &ec) == 2) return ec;
    }
}

static void set_argv(char **argv, int argc)
{
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);
    for (int i = 0; i < argc; i++) mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
}

int py_main(void);
__asm__(".text\n.global _start\n_start:\n  andq $-16, %rsp\n  call py_main\n  movq %rax, %rdi\n  xorq %rax, %rax\n  syscall\n  jmp .\n");

int py_main(void)
{
    int stack_dummy;
    mp_stack_set_top(&stack_dummy);
    mp_stack_set_limit(60 * 1024 - 4096);                  /* la pila de usuario es de 64 KiB */
    gc_init(g_heap, g_heap + sizeof g_heap);
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    { int ec0 = 0; (void)run_source(MP_QSTR__lt_stdin_gt_, PRELUDE, sizeof PRELUDE - 1, MP_PARSE_FILE_INPUT, &ec0); }   /* open() */

    i64 al = u_getargs(g_args, sizeof g_args);
    if (al < 0) al = 0;
    char *argv[16]; int argc = 0;
    for (char *p = g_args; *p && argc < 16; ) {             /* separar por espacios (las comillas las quita la orden python) */
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    int rc = 0, ec = 0;
    if (argc >= 1 && (!strcmp(argv[0], "-s") || !strcmp(argv[0], "-x"))) {
        /* SOMA_239: el programador de SOMA comprueba (-s: solo compilar) o prueba (-x: ejecutar) un programa que aun no ha escrito
         * en disco: el nucleo le pasa el codigo (SYS_SOMA_CON op 8). Nombre del archivo en los errores: programa.py */
        i64 sz = u_sys3(SYS_SOMA_CON, 8, 0, 0);
        if (sz < 0) { mp_hal_stdout_tx_str("python: no hay codigo que comprobar\n"); rc = 2; }
        else {
            char *src = m_new(char, (size_t)sz + 1);
            i64 n = u_sys3(SYS_SOMA_CON, 8, (i64)src, sz);
            if (n < 0) n = 0;
            src[n] = 0;
            set_argv(argv, 0);
            mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(MP_QSTR_programa_dot_py));
            if (argv[0][1] == 's') {
                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR_programa_dot_py, src, (size_t)n, 0);
                    mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
                    (void)mp_compile(&pt, MP_QSTR_programa_dot_py, false);
                    nlr_pop();
                    mp_hal_stdout_tx_str("OK\n");
                } else { mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val)); rc = 1; }
            } else {
                int r = run_source(MP_QSTR_programa_dot_py, src, (size_t)n, MP_PARSE_FILE_INPUT, &ec);
                rc = r == 2 ? ec : r;
            }
        }
    } else if (argc >= 2 && !strcmp(argv[0], "-c")) {
        char *code = g_args + (argv[1] - g_args);
        for (char *q = code; q < g_args + al; q++) if (!*q) *q = ' ';  /* "-c" lleva el resto de la linea tal cual */
        set_argv(argv, 1);
        int r = run_source(MP_QSTR__lt_stdin_gt_, code, strlen(code), MP_PARSE_FILE_INPUT, &ec);
        rc = r == 2 ? ec : r;
    } else if (argc >= 1) {
        const char *path = full_path(argv[0]);
        i64 sz = vfs_op(2, path, 0, 0);
        if (sz < 0) { mp_hal_stdout_tx_str(sz == -13 ? "python: sin permiso para leer " : "python: no existe "); mp_hal_stdout_tx_str(path); mp_hal_stdout_tx_str("\n"); rc = 2; }
        else {
            set_argv(argv, argc);
            char *src = m_new(char, (size_t)sz + 1);
            i64 n = vfs_op(0, path, src, (u64)sz);
            if (n < 0) { mp_hal_stdout_tx_str("python: no se pudo leer el script\n"); rc = 2; }
            else {
                src[n] = 0;
                int r = run_source(qstr_from_str(path), src, (size_t)n, MP_PARSE_FILE_INPUT, &ec);
                rc = r == 2 ? ec : r;
            }
        }
    } else {
        set_argv(argv, 0);
        rc = repl();
    }
    gc_sweep_all();
    mp_deinit();
    return rc;
}

/* ------------------------------------------------------------------ lo que pide el nucleo de MicroPython */
void gc_collect(void)
{
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
void nlr_jump_fail(void *val)
{
    (void)val;
    mp_hal_stdout_tx_str("python: error interno (excepcion sin capturar)\n");
    u_exit(70);
    __builtin_unreachable();
}
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)file; (void)line; (void)func; (void)expr;
    mp_hal_stdout_tx_str("python: fallo interno (assert)\n");
    u_exit(71);
}
double fabs(double x) { return __builtin_fabs(x); }      /* lo que libm_dbl no trae */
double nan(const char *s) { (void)s; return __builtin_nan(""); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }
double log(double);
double log2(double x) { return log(x) * 1.44269504088896340736; }      /* 1/ln(2) */
void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func) { __assert_func(file, (int)line, func, expr); }
