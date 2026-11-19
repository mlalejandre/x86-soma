#ifndef SOMA_WEB_JS_I_H
#define SOMA_WEB_JS_I_H
#include "web_js.h"
#include "web_f64.h"
#include "web_util.h"

typedef struct jstr { uint32_t len, hash; uint32_t u16; /* SOMA_185: unidades UTF-16 + 1 (0 = sin calcular) */ uint8_t interned; char d[1]; } jstr;
typedef struct jobj jobj;
typedef struct jv { uint8_t t; union { f64 n; int b; jstr *s; jobj *o; } u; } jv;
enum { JT_UNDEF = 0, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ };

enum { JC_OBJECT = 0, JC_ARRAY, JC_FUNC, JC_NATIVE, JC_ERROR, JC_DATE, JC_REGEXP, JC_PROMISE, JC_MAP, JC_SET, JC_HOST, JC_BOOL, JC_NUMBER, JC_STRING, JC_ARGS, JC_BOUND, JC_GEN, JC_ABUF, JC_TYPED, JC_DVIEW, JC_PROXY, JC_BIGINT };
enum { JP_ENUM = 1, JP_ACC = 2, JP_RO = 4, JP_NOCONF = 8 };
struct jprop { jstr *key; jv val; jv set; uint8_t flags; };

struct jrt;
typedef jv (*jnative)(struct jrt *rt, jv thisv, jv *args, int argc, int magic);

struct jdyn {
    jv (*get)(struct jrt *rt, jobj *o, jstr *key, int *found);
    void (*put)(struct jrt *rt, jobj *o, jstr *key, jv val);
    int (*del)(struct jrt *rt, jobj *o, jstr *key);
    int (*has)(struct jrt *rt, jobj *o, jstr *key);
    jobj *(*keys)(struct jrt *rt, jobj *o);
    jv (*named)(struct jrt *rt, jobj *o, jstr *key, int *found);   /* SOMA_187A: document.<nombre> (formularios, imagenes...) cuando la propiedad no existe */
};
struct scope;
struct jn;
struct jobj {
    uint8_t cls, frozen, extensible, flag; int16_t magic;
    jobj *proto; struct jprop *props; uint32_t np, cap; uint32_t *idx; uint32_t icap;
    jv *el; uint32_t alen, acap;                         /* array / map entries / args */
    union {
        struct { struct jn *node; struct scope *env; jobj *home; jv bthis; struct jn *cls; } fn;
        struct { jnative f; } nat;
        struct { void *p; int kind; } host;
        struct { f64 t; } date;
        struct { uint32_t *h; uint32_t hcap, hcount; } map;   /* SOMA 189: indice de claves de Map/Set (hcount = entradas indexadas; ~0 = rehacer) */
        struct { uint64_t lo, hi; } big;                  /* SOMA 188B: BigInt (128 bits con signo) */
        struct { void *prog; jstr *src; jstr *flags; } re;
        struct { int state; jv value; void *react; int handled; } prom;
    } u;
};

/* ---------- AST ---------- */
enum {
    N_NUM = 1, N_STR, N_TPL, N_REGEX, N_IDENT, N_THIS, N_NULL, N_TRUE, N_FALSE, N_UNDEF, N_ARRAY, N_OBJECT, N_PROP, N_FUNC, N_CLASS,
    N_UNARY, N_UPDATE, N_BINARY, N_LOGICAL, N_ASSIGN, N_COND, N_CALL, N_NEW, N_MEMBER, N_SEQ, N_SPREAD, N_AWAIT, N_SUPER, N_HOLE, N_TAGGED, N_NEWTARGET,
    N_VAR, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_FOROF, N_WHILE, N_DO, N_RETURN, N_BREAK, N_CONTINUE, N_THROW, N_TRY, N_SWITCH, N_CASE,
    N_LABEL, N_EMPTY, N_FUNCDECL, N_CLASSDECL, N_PROGRAM, N_DECL, N_YIELD, N_WITH
};
enum { NF_ARROW = 1, NF_ASYNC = 2, NF_EXPRBODY = 4, NF_METHOD = 8, NF_GETTER = 16, NF_SETTER = 32, NF_STATIC = 64, NF_CTOR = 128, NF_DERIVED = 256,
       NF_COMPUTED = 512, NF_SHORT = 1024, NF_OPT = 2048, NF_PAREN = 4096, NF_FIELD = 8192, NF_LEX = 16384, NF_GEN = 32768 };
struct jfi { jstr **vn; int nvn; int ncap; int has_closure; int uses_args; jstr *name; const char *src; uint32_t srclen; int nparams; int has_await; int strict; /* SOMA 188B: codigo estricto ('use strict', clases) */ };
struct jn {
    uint8_t t, op; uint16_t fl; int line;
#ifdef WJS_DEBUG_COL
    int col;                       /* solo en pruebas del anfitrion: columna para depurar codigo minificado */
#endif
    struct jn *a, *b, *c, *d, *next;
    jstr *str; f64 num; void *p; int ival;
};

enum { B_ADD = 1, B_SUB, B_MUL, B_DIV, B_MOD, B_EXP, B_EQ, B_NE, B_SEQ, B_SNE, B_LT, B_GT, B_LE, B_GE, B_SHL, B_SHR, B_USHR, B_AND, B_OR, B_XOR, B_INSTANCEOF, B_IN, B_LAND, B_LOR, B_NULLISH };

/* ---------- ambito ---------- */
struct jvar { jstr *name; jv v; uint8_t kind; };       /* 0 var, 1 let, 2 const */
struct scope {
    struct scope *parent; struct jvar *vars; uint32_t n, cap; uint8_t is_fn, arrow, heap;   /* SOMA 189: 32 bits (el modulo principal de YouTube declara mas de 65535 nombres en una funcion) */
    uint32_t *hidx; uint32_t hcap;   /* SOMA 189: indice por nombre para ambitos grandes (la busqueda lineal era el 23 % del tiempo en Google) */
    jv this_v; jobj *callee; jv *argv; int argc; jobj *newtarget; jobj *home;
    jobj *with_obj; uint8_t has_with;   /* SOMA_188: sentencia with (el bit se hereda: sin with en la cadena no cuesta nada) */
};

struct jtimer { struct jtimer *next; int id; uint64_t due; uint32_t interval; int repeat; jv fn; jv *args; int nargs; };
struct jmicro { struct jmicro *next; jv fn; jv a, b; int kind; jobj *prom; };

/* interrupcion estructurada */
struct jtry { struct jtry *up; void *jb[5]; jv exc; int depth; char *sstop; struct scope *scope; };

struct jrt {
    struct wjs *js;
    struct wa_arena ar; uint32_t heap_cap;
    char *sstack, *stop, *send;
    struct jtry *try_top; int depth; uint32_t steps; uint32_t step_limit; int halted; char errbuf[256];
    jobj *global; struct scope *gscope;
    jobj *p_object, *p_function, *p_array, *p_string, *p_number, *p_boolean, *p_error, *p_date, *p_regexp, *p_promise, *p_map, *p_set, *p_iter, *p_symbol, *p_bigint;
    jobj *p_errs[8];
    jstr **itab; uint32_t itab_cap, itab_n;
    jstr *s_length, *s_prototype, *s_constructor, *s_proto, *s_name, *s_message, *s_empty, *s_undefined, *s_null, *s_true, *s_false, *s_object, *s_function,
         *s_number, *s_string, *s_boolean, *s_nan, *s_stack, *s_then, *s_value, *s_done, *s_next, *s_index, *s_input, *s_get, *s_set;
    jstr *s_chars[128];
    struct jtimer *timers, *timers_tail; int next_timer; uint64_t now;
    struct jmicro *mhead, *mtail;
    const struct wjs_host *host; struct wdoc *doc;
    uint32_t rng[4];
    int in_event; jobj *cur_event; int async_depth;
    jv last_exc; int exc_pending;
    int cur_line; const char *cur_src;
    jstr *fstk[40]; int fline[40];   /* SOMA_188: nombres de las funciones activas y linea de la llamada (Error.stack) */
    jobj *eval_fn; jstr *s_eval188;   /* SOMA_188: eval nativo (para reconocer el eval directo) */
    char srcname188[96];              /* SOMA_188: copia del nombre del script en curso (cur_src apuntaba a la pila de quien llamaba) */
#ifdef WJS_DEBUG_COL
    int cur_col;
#endif
    jv retval; jstr *brk_label; int sc_flag; jobj *new_target; char *stack_lo; uint64_t task_start; uint32_t big_stack_size; char *big_stack; int in_task;
    jobj *g_console; struct jv *tmp_roots; jobj *cur_native; jobj *unhandled; int timer_depth;
    void *dom;                    /* estado del DOM (web_js_dom.c) */
    void *recache; uint32_t recache_pos;   /* cache de regex compiladas */
    const struct jdyn *dyn;       /* ganchos de propiedades dinamicas para objetos host (style, dataset, storage) */
    int timed_out;                /* halted por tiempo (se puede reanudar con la siguiente tarea) */
    int heap_over;                /* SOMA 188B: ya se supero una vez el tope de memoria (queda la reserva para los manejadores) */
    /* SOMA 189: recolector de basura (web_js_gc.c) */
    void *gc; const char *gc_root_lo, *gc_root_hi; char *gc_main_hi, *gc_outer_sp; uint32_t gc_sstack_size;
    struct jtry *base_try;        /* marco de excepcion mas externo (para abortar scripts detenidos) */
    /* SOMA_183: corrutinas (generadores, async/await real) */
    struct jco *cur_co, *co_live; int co_npool; uint32_t co_bytes; char *co_pool[16];
    int gen_unwind;               /* return() de un generador desenrollando try/finally (los catch no lo atrapan) */
    int co_sync;                  /* >0: funcion async ejecutada en sincrono (sin pila de corrutina disponible) */
    struct { jstr *s; uint32_t *tab; } u16t[4]; uint32_t u16t_next;   /* SOMA 189: tablas de puntos de control indice UTF-16 -> byte de las ultimas cadenas largas */
    jstr *u16c_s; uint32_t u16c_idx, u16c_off;   /* SOMA_185: cursor de la ultima conversion indice UTF-16 <-> byte (recorridos secuenciales en O(1)) */
    int co_body;                  /* la siguiente call_closure es el cuerpo de una corrutina: ejecutar como funcion normal */
    jobj *p_generator, *p_asyncgen;
    jobj *p_abuf, *p_sabuf, *p_typed, *p_dview, *p_ta[9];   /* SOMA_186: ArrayBuffer, SharedArrayBuffer, %TypedArray%, DataView y los 9 arrays tipados */
};

/* SOMA_186: datos binarios reales (web_js_bin.c). Un ArrayBuffer (JC_ABUF, u.host.p -> jbuf) es un bloque de bytes del arena; un array tipado
 * (JC_TYPED) o un DataView (JC_DVIEW) es una vista (jta) con desplazamiento y longitud sobre el. Con soma.jsbin=off no se crean (prelude antiguo). */
enum { TA_I8 = 0, TA_U8, TA_U8C, TA_I16, TA_U16, TA_I32, TA_U32, TA_F32, TA_F64, TA_N };
struct jbuf { uint8_t *d; uint32_t len, maxlen; uint8_t shared, detached, resizable; };
struct jta { jobj *bufobj; struct jbuf *buf; uint32_t off, len; uint8_t kind, track; };

/* ---------- utilidades de valores ---------- */
#define JUNDEF ((jv){ JT_UNDEF, { 0 } })
static inline jv jnull(void) { jv v; v.t = JT_NULL; v.u.n = 0; return v; }
static inline jv jundef(void) { jv v; v.t = JT_UNDEF; v.u.n = 0; return v; }
/* SOMA 188B: hueco de array (Array(3), [,,], delete a[i]): un undefined marcado. Se lee como undefined; forEach/map/filter/reduce/in/keys lo saltan */
#define JS_HOLE_TAG 0x484F4C45
static inline jv jhole(void) { jv v; v.t = JT_UNDEF; v.u.n = 0; v.u.b = JS_HOLE_TAG; return v; }
#define IS_HOLE(v) ((v).t == JT_UNDEF && (v).u.b == JS_HOLE_TAG)
void js_arr_push_raw(struct jrt *rt, jobj *a, jv v);
static inline jv jbool(int b) { jv v; v.t = JT_BOOL; v.u.n = 0; v.u.b = b != 0; return v; }
static inline jv jnum(f64 n) { jv v; v.t = JT_NUM; v.u.n = n; return v; }
static inline jv jint(int64_t n) { jv v; v.t = JT_NUM; v.u.n = f64_from_i64(n); return v; }
static inline jv jstrv(jstr *s) { jv v; v.t = JT_STR; v.u.n = 0; v.u.s = s; return v; }
static inline jv jobjv(jobj *o) { jv v; v.t = JT_OBJ; v.u.n = 0; v.u.o = o; return v; }
#define IS_OBJ(v) ((v).t == JT_OBJ)
#define IS_STR(v) ((v).t == JT_STR)
#define IS_NUM(v) ((v).t == JT_NUM)
#define IS_UNDEF(v) ((v).t == JT_UNDEF)
#define IS_NULLISH(v) ((v).t <= JT_NULL)

/* memoria */
void *js_alloc(struct jrt *rt, uint32_t n);
/* cadenas */
jstr *js_str_new(struct jrt *rt, const char *s, uint32_t n);
/* SOMA_186 */
/* SOMA_188: Proxy (web_js_proxy.c) y modulos ES (web_js_module.c) */
void     js_module_init(struct jrt *rt);
int      js_module_transform(const char *src, uint32_t len, struct wb *out, int async_wrap);
jobj    *js_own_keys(struct jrt *rt, jv v, int all);
int      js_proxy_on(void);
void     js_proxy_init(struct jrt *rt);
/* SOMA 188B: BigInt (web_js_big.c) */
void     js_big_init(struct jrt *rt);
/* SOMA 189: recolector de basura */
void    *js_gc_alloc(struct jrt *rt, uint32_t n, int atomic);
void     js_gc_collect(struct jrt *rt);
void     js_gc_free_all(struct jrt *rt);
void     js_gc_scan_stack(struct jrt *rt, const char *lo, const char *hi);
uint32_t js_heap_total(struct jrt *rt);
uint32_t js_gc_count(struct jrt *rt);
uint32_t js_gc_live(struct jrt *rt);
void    *js_alloc_atomic(struct jrt *rt, uint32_t n);
int      js_is_big(jv v);
jv       js_big_binop(struct jrt *rt, int op, jv a, jv b);
int      js_big_lt(struct jrt *rt, jv a, jv b, int *undef);
int      js_big_eq(struct jrt *rt, jv a, jv b);
int      js_big_same(jv a, jv b);
int      js_big_is_zero(jv v);
jv       js_big_neg(struct jrt *rt, jv v);
jv       js_big_not(struct jrt *rt, jv v);
jv       js_big_inc(struct jrt *rt, jv v, int d);
jv       js_big_literal(struct jrt *rt, jstr *digits);
jstr    *js_big_tostr(struct jrt *rt, jv v, int radix);
f64      js_big_tonum(jv v);
jv       js_big_from_i64(struct jrt *rt, int64_t v);
jv       js_big_from_u64(struct jrt *rt, uint64_t v);
uint64_t js_big_low64(jv v);
jv       js_proxy_get(struct jrt *rt, jobj *p, jstr *key, jv receiver);
int      js_proxy_set(struct jrt *rt, jobj *p, jstr *key, jv val, jv receiver);
int      js_proxy_has(struct jrt *rt, jobj *p, jstr *key);
int      js_proxy_delete(struct jrt *rt, jobj *p, jstr *key);
jobj    *js_proxy_own_keys(struct jrt *rt, jobj *p, int all);
jv       js_proxy_call(struct jrt *rt, jobj *p, jv thisv, jv *args, int argc);
jv       js_proxy_construct(struct jrt *rt, jobj *p, jv *args, int argc, jobj *newtarget);
jobj    *js_proxy_proto(struct jrt *rt, jobj *p);
int      js_proxy_is_array(jobj *p);
int      js_proxy_callable(jobj *p);
int      js_bin_on(void);
void     js_bin_init(struct jrt *rt);
uint32_t js_ta_length(jobj *o);
jv       js_ta_get(jobj *o, uint32_t i);
void     js_ta_set(struct jrt *rt, jobj *o, uint32_t i, jv v);
jobj    *js_bin_buffer_new(struct jrt *rt, uint32_t len);
jobj    *js_bin_u8_new(struct jrt *rt, const void *src, uint32_t n);
int      js_bin_bytes(jv v, const uint8_t **p, uint32_t *n);
jv       js_bin_clone(struct jrt *rt, jv v);
f64      js_f32_round(f64 a);
/* SOMA_185: semantica UTF-16 sobre cadenas UTF-8 (WTF-8: las mitades sueltas de un par sustituto se guardan como 3 bytes ED Ax/Bx xx).
 * length, indices y posiciones visibles desde JS cuentan unidades de 16 bits; el almacenamiento sigue siendo UTF-8. Con soma.jsutf16=off
 * todo cuenta bytes como antes del parche. */
int      js_u16_on(void);
uint32_t js_u16len_n(const char *d, uint32_t n);                           /* idem para un bloque de bytes (sin cache) */
uint32_t js_u16len(jstr *s);                                                  /* longitud en unidades UTF-16 */
uint32_t js_u16_to_byte(struct jrt *rt, jstr *s, uint32_t idx, int *mid);     /* byte de la unidad idx; *mid=1 si cae entre las dos mitades de un par */
uint32_t js_byte_to_u16(struct jrt *rt, jstr *s, uint32_t off);               /* unidad que corresponde a un byte */
int32_t  js_u16_unit(struct jrt *rt, jstr *s, uint32_t idx);                  /* valor de la unidad idx, -1 fuera de rango */
jstr    *js_u16_slice(struct jrt *rt, jstr *s, uint32_t a, uint32_t b);       /* unidades [a, b) */
jstr    *js_u16_char(struct jrt *rt, jstr *s, uint32_t idx);                  /* cadena de una unidad, 0 fuera de rango */
void     js_wb_unit(struct wb *b, uint32_t unit);                             /* anade una unidad UTF-16 (une pares sustitutos) */
jstr *js_str_cstr(struct jrt *rt, const char *s);
jstr *js_intern(struct jrt *rt, jstr *s);
jstr *js_intern_c(struct jrt *rt, const char *s);
jstr *js_intern_n(struct jrt *rt, const char *s, uint32_t n);
jstr *js_str_concat(struct jrt *rt, jstr *a, jstr *b);
int   js_str_eq(const jstr *a, const jstr *b);
/* objetos */
jobj *js_obj_new(struct jrt *rt, jobj *proto);
jobj *js_arr_new(struct jrt *rt, uint32_t cap);
void  js_arr_push(struct jrt *rt, jobj *a, jv v);
struct jprop *js_find_own(jobj *o, jstr *key);
jv    js_get(struct jrt *rt, jv obj, jstr *key);
jv    js_getv(struct jrt *rt, jv obj, jv key);
jv    js_get_c(struct jrt *rt, jv obj, const char *key);
void  js_put(struct jrt *rt, jv obj, jstr *key, jv val);
void  js_putv(struct jrt *rt, jv obj, jv key, jv val);
void  js_set_c(struct jrt *rt, jobj *o, const char *key, jv val);
void  js_def(struct jrt *rt, jobj *o, jstr *key, jv val, int flags);
void  js_def_acc(struct jrt *rt, jobj *o, jstr *key, jv getter, jv setter, int flags);
int   js_has(struct jrt *rt, jobj *o, jstr *key);
int   js_delete(struct jrt *rt, jobj *o, jstr *key);
jobj *js_native(struct jrt *rt, const char *name, jnative f, int nargs, int magic);
void  js_method(struct jrt *rt, jobj *o, const char *name, jnative f, int nargs, int magic);
void  js_getter(struct jrt *rt, jobj *o, const char *name, jnative get, jnative set, int magic);
/* conversion */
jstr *js_tostr(struct jrt *rt, jv v);
f64   js_tonum(struct jrt *rt, jv v);
int   js_truthy(jv v);
jv    js_toprim(struct jrt *rt, jv v, int hint_str);
jobj *js_toobj(struct jrt *rt, jv v);
const char *js_typeof(struct jrt *rt, jv v);
int   js_strict_eq(jv a, jv b);
int   js_loose_eq(struct jrt *rt, jv a, jv b);
int   js_same_zero(jv a, jv b);
int   js_to_index(struct jrt *rt, jv v, int64_t *out);       /* 1 si es indice entero >= 0 */
/* llamadas y errores */
jv    js_call(struct jrt *rt, jv fn, jv thisv, jv *args, int argc);
jv    js_construct(struct jrt *rt, jv fn, jv *args, int argc, jobj *newtarget);
int   js_is_callable(jv v);
void  js_throw(struct jrt *rt, jv exc) __attribute__((noreturn));
void  js_throw_err(struct jrt *rt, int kind, const char *fmt, ...) __attribute__((noreturn));
enum { E_ERROR = 0, E_TYPE, E_REFERENCE, E_RANGE, E_SYNTAX, E_EVAL, E_URI };
void js_stack_trace(struct jrt *rt, const char *head, struct wb *b);   /* SOMA_188 */
jobj *js_make_error(struct jrt *rt, int kind, const char *msg);
int   js_instanceof(struct jrt *rt, jv v, jv ctor);
/* iteracion */
int   js_iterate(struct jrt *rt, jv iterable, int (*cb)(struct jrt *rt, jv item, void *ud), void *ud);
jv    js_make_iter_result(struct jrt *rt, jv value, int done);
jv    js_sym_description(struct jrt *rt, jstr *s);                               /* SOMA_187 */
jstr *js_sym_tostring(struct jrt *rt, jstr *s);
jv    js_make_iterator(struct jrt *rt, jv src, int kind);                       /* SOMA_186: 0 valores, 1 claves, 2 pares */
jobj *js_array_from(struct jrt *rt, jv *items, uint32_t n);
/* microtareas y promesas */
void  js_micro_enqueue(struct jrt *rt, jv fn, jv a, jv b, int kind, jobj *prom);
void  js_run_micro(struct jrt *rt);
jobj *js_promise_new(struct jrt *rt);
void  js_promise_resolve(struct jrt *rt, jobj *p, jv v);
void  js_promise_reject(struct jrt *rt, jobj *p, jv v);
jv    js_promise_then(struct jrt *rt, jobj *p, jv onf, jv onr);
jv    js_await(struct jrt *rt, jv v);
/* corrutinas (web_js_co.c) */
struct jco;
jv    js_call_body(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc);
jv    js_gen_create(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc);
int   js_async_try(struct jrt *rt, jobj *fo, jv thisv, jv *args, int argc, jv *out);
int   js_co_await(struct jrt *rt, jobj *p, jv *out);
jv    js_co_yield(struct jrt *rt, jv v);
jv    js_co_yield_star(struct jrt *rt, jv iterable);
void  js_co_init(struct jrt *rt);
void  js_co_free_all(struct jrt *rt);
void  js_co_reset(struct jrt *rt);
int   js_co_available(void);
/* analisis */
struct jn *js_parse(struct jrt *rt, const char *src, uint32_t len, int *err_line, char *errmsg, uint32_t errmax);
jv    js_run_program(struct jrt *rt, struct jn *prog);
jv    js_eval_string(struct jrt *rt, const char *src, uint32_t len, int *ok);
/* biblioteca */
void  js_lib_init(struct jrt *rt);
void  js_dom_init(struct jrt *rt);
void  js_regex_init(struct jrt *rt);
jv    js_json_parse(struct jrt *rt, const char *s, uint32_t n);
jstr *js_json_stringify(struct jrt *rt, jv v, jv replacer, jv indent);
jstr *js_num_to_str(struct jrt *rt, f64 n);
jstr *js_inspect(struct jrt *rt, jv v, int depth);
void  js_log(struct jrt *rt, int level, const char *msg);
/* scope helpers (run) */
struct jvar *js_scope_lookup(struct scope *s, jstr *name);
void  js_step(struct jrt *rt);
void  js_check_stack(struct jrt *rt);
void  js_run_big(struct jrt *rt, void (*fn)(void *), void *arg);
/* puente DOM (web_js_dom.c) */
int   js_dom_dispatch(struct jrt *rt, struct wnode *target, const char *type, int x, int y, int key, const char *keystr, int mods);
void  js_dom_fire_load(struct jrt *rt);
void  js_dom_set_script(struct jrt *rt, struct wnode *n);
void  js_dom_set_url(struct jrt *rt, const char *url);
int   js_pump_timers(struct jrt *rt, int *ran);
jobj *js_regex_new(struct jrt *rt, jstr *src, jstr *flags);
uint64_t web_epoch_ms(void);
/* codigos de operacion de String (lib2 y regex) */
enum { S_CHARAT, S_CHARCODEAT, S_CODEPOINTAT, S_AT, S_INDEXOF, S_LASTINDEXOF, S_INCLUDES, S_STARTSWITH, S_ENDSWITH, S_SLICE, S_SUBSTRING, S_SUBSTR, S_UPPER, S_LOWER, S_TRIM, S_TRIMSTART, S_TRIMEND,
       S_SPLIT, S_REPLACE, S_REPLACEALL, S_MATCH, S_MATCHALL, S_SEARCH, S_PADSTART, S_PADEND, S_REPEAT, S_CONCAT, S_LOCALECMP, S_NORMALIZE, S_TOSTRING, S_ITER, S_ANCHOR };
#define JS_STEP(rt) do { if (++(rt)->steps > (rt)->step_limit) js_step(rt); } while (0)
extern uint64_t web_now_ms(void);
#endif
