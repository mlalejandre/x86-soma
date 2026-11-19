/* SOMA_186: datos binarios reales del motor JavaScript: ArrayBuffer, SharedArrayBuffer, arrays tipados, DataView, y las piezas nativas de
 * TextEncoder/TextDecoder/Blob/base64.
 *
 * Antes los "arrays tipados" eran arrays normales disfrazados (sin memoria compartida entre vistas, sin DataView, sin desbordamiento
 * correcto) y Blob/TextEncoder trabajaban con cadenas. Ahora:
 *   - ArrayBuffer: bloque de bytes real (struct jbuf) dentro del arena del script; redimensionable (resize) y transferible (transfer).
 *   - Arrays tipados: vistas (struct jta) sobre un ArrayBuffer con desplazamiento y longitud; lectura/escritura por indice directamente
 *     en memoria (js_get/js_put las llaman). Int8/Uint8/Uint8Clamped/Int16/Uint16/Int32/Uint32/Float32/Float64.
 *   - f64 del motor es el patron de bits IEEE, asi que Float64Array es un memcpy; Float32 usa redondeo al par mas cercano real
 *     (Math.fround tambien).
 *   - soma.jsbin=off (o el modo seguro) deja el prelude antiguo (arrays disfrazados), como antes del parche 186. */
#include "web_js_i.h"
#define ARG(i) ((i) < argc ? args[i] : jundef())

#if defined(WEB_HOST_TEST)
int g_jbin_off, g_jstream_off;
#endif
/* SOMA_187: soma.jsstream=off desactiva Streams, indexedDB y caches (el resto de 187 sigue). */
int js_stream_on(void)
{
#if defined(WEB_HOST_TEST)
    return !g_jstream_off;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsstream") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}
int js_bin_on(void)
{
#if defined(WEB_HOST_TEST)
    return !g_jbin_off;
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsbin") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}

#define MAXBUF (16u << 20)                 /* tope de un ArrayBuffer; ademas cuenta contra el limite de memoria del script */
static const uint8_t TA_SZ[TA_N + 1] = { 1, 1, 1, 2, 2, 4, 4, 4, 8, 1 };
static const char *const TA_NAME[TA_N] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array", "Uint32Array", "Float32Array", "Float64Array" };

/* ------------------------------ flotantes de 32 bits ------------------------------ */
static f64 f32_to_f64(uint32_t b)
{
    uint64_t s = (uint64_t)(b >> 31) << 63; uint32_t e = (b >> 23) & 0xFF, m = b & 0x7FFFFF;
    if (e == 0xFF) return s | 0x7FF0000000000000ULL | (m ? (0x8000000000000ULL | ((uint64_t)m << 29)) : 0);
    if (e == 0) {
        if (!m) return s;
        int sh = 0; while (!(m & 0x800000)) { m <<= 1; sh++; }
        m &= 0x7FFFFF; return s | ((uint64_t)(uint32_t)(1 - sh - 127 + 1023) << 52) | ((uint64_t)m << 29);
    }
    return s | ((uint64_t)(e - 127 + 1023) << 52) | ((uint64_t)m << 29);
}
static uint32_t f64_to_f32(f64 a)
{
    uint32_t s = (uint32_t)(a >> 63) << 31; int e = (int)((a >> 52) & 0x7FF); uint64_t m = a & 0xFFFFFFFFFFFFFULL;
    if (e == 0x7FF) return s | 0x7F800000u | (m ? 0x400000u : 0);
    if (e == 0) return s;
    int ue = e - 1023; uint64_t full = m | (1ULL << 52); int shift;
    if (ue > 127) return s | 0x7F800000u;
    if (ue >= -126) shift = 29; else { shift = 29 + (-126 - ue); if (shift > 63) return s; }
    uint64_t q = full >> shift, rem = full & ((1ULL << shift) - 1), half = 1ULL << (shift - 1);
    if (rem > half || (rem == half && (q & 1))) q++;
    if (ue >= -126) {
        uint32_t be = (uint32_t)(ue + 127);
        if (q >> 24) { q >>= 1; be++; }
        if (be >= 255) return s | 0x7F800000u;
        return s | (be << 23) | (uint32_t)(q & 0x7FFFFF);
    }
    return s | (uint32_t)q;
}
f64 js_f32_round(f64 a) { return f32_to_f64(f64_to_f32(a)); }

/* ------------------------------ acceso a elementos ------------------------------ */
static inline uint32_t view_len(const struct jta *t)                 /* elementos validos de la vista (0 si quedo fuera del buffer) */
{
    uint32_t bl = t->buf->len, sz = TA_SZ[t->kind];
    if (t->track) return bl > t->off ? (bl - t->off) / sz : 0;
    return ((uint64_t)t->off + (uint64_t)t->len * sz <= bl) ? t->len : 0;
}
uint32_t js_ta_length(jobj *o) { return view_len((const struct jta *)o->u.host.p); }
jv js_ta_get(jobj *o, uint32_t i)
{
    const struct jta *t = (const struct jta *)o->u.host.p;
    const uint8_t *p = t->buf->d + t->off + (uint64_t)i * TA_SZ[t->kind];
    switch (t->kind) {
    case TA_I8: return jint((int8_t)p[0]);
    case TA_U8: case TA_U8C: return jint(p[0]);
    case TA_I16: { int16_t v; memcpy(&v, p, 2); return jint(v); }
    case TA_U16: { uint16_t v; memcpy(&v, p, 2); return jint(v); }
    case TA_I32: { int32_t v; memcpy(&v, p, 4); return jint(v); }
    case TA_U32: { uint32_t v; memcpy(&v, p, 4); return jint(v); }
    case TA_F32: { uint32_t v; memcpy(&v, p, 4); return jnum(f32_to_f64(v)); }
    default: { f64 v; memcpy(&v, p, 8); return jnum(v); }
    }
}
static void ta_store(const struct jta *t, uint32_t i, f64 n)           /* escribe un numero ya convertido; i dentro de rango */
{
    uint8_t *p = t->buf->d + t->off + (uint64_t)i * TA_SZ[t->kind];
    switch (t->kind) {
    case TA_I8: case TA_U8: p[0] = (uint8_t)f64_to_i32(n); break;
    case TA_U8C: {
        int v;
        if (f64_is_nan(n) || !f64_lt(F64_ZERO, n)) v = 0;
        else if (!f64_lt(n, 0x406FE00000000000ULL)) v = 255;       /* >= 255 */
        else {
            f64 fl = f64_floor(n), d = f64_sub(n, fl); v = (int)f64_to_i64(fl);
            if (f64_lt(0x3FE0000000000000ULL, d) || (f64_eq(d, 0x3FE0000000000000ULL) && (v & 1))) v++;   /* redondeo al par */
        }
        p[0] = (uint8_t)v; break;
    }
    case TA_I16: case TA_U16: { uint16_t v = (uint16_t)f64_to_i32(n); memcpy(p, &v, 2); break; }
    case TA_I32: case TA_U32: { uint32_t v = (uint32_t)f64_to_i32(n); memcpy(p, &v, 4); break; }
    case TA_F32: { uint32_t v = f64_to_f32(n); memcpy(p, &v, 4); break; }
    default: memcpy(p, &n, 8); break;
    }
}
void js_ta_set(struct jrt *rt, jobj *o, uint32_t i, jv v)
{
    const struct jta *t = (const struct jta *)o->u.host.p;
    f64 n = js_tonum(rt, v);                                            /* se convierte antes de comprobar el indice (como la especificacion) */
    if (i < view_len(t)) ta_store(t, i, n);
}

/* ------------------------------ creacion de objetos ------------------------------ */
static void check_alloc(struct jrt *rt, uint64_t n)
{
    if (n > MAXBUF || (uint64_t)js_gc_live(rt) + n + (512u << 10) > rt->heap_cap) js_throw_err(rt, E_RANGE, "No se pudo reservar el ArrayBuffer (%u bytes)", (unsigned)(n > 0xFFFFFFFFu ? 0xFFFFFFFFu : n));
}
static jobj *buf_make(struct jrt *rt, jobj *proto, uint32_t len, uint32_t maxlen, int shared, int resizable)
{
    uint32_t cap = resizable ? maxlen : len;
    check_alloc(rt, cap);
    jobj *o = js_obj_new(rt, proto); o->cls = JC_ABUF;
    struct jbuf *b = (struct jbuf *)js_alloc(rt, sizeof *b); b->d = cap ? (uint8_t *)js_alloc(rt, cap) : 0; b->len = len; b->maxlen = cap; b->shared = (uint8_t)shared; b->resizable = (uint8_t)resizable;
    o->u.host.p = b; return o;
}
jobj *js_bin_buffer_new(struct jrt *rt, uint32_t len) { return buf_make(rt, rt->p_abuf, len, len, 0, 0); }
static jobj *view_make(struct jrt *rt, jobj *proto, int kind, jobj *bufobj, uint32_t off, uint32_t len, int track)
{
    jobj *o = js_obj_new(rt, proto); o->cls = kind == TA_N ? JC_DVIEW : JC_TYPED; o->u.host.kind = kind;
    struct jta *t = (struct jta *)js_alloc(rt, sizeof *t); t->bufobj = bufobj; t->buf = (struct jbuf *)bufobj->u.host.p; t->off = off; t->len = len; t->kind = (uint8_t)kind; t->track = (uint8_t)track;
    o->u.host.p = t; return o;
}
static jobj *ta_alloc(struct jrt *rt, int kind, uint32_t n)             /* array tipado nuevo con su propio buffer */
{
    if ((uint64_t)n * TA_SZ[kind] > MAXBUF) js_throw_err(rt, E_RANGE, "Longitud de array tipado no valida: %u", (unsigned)n);
    jobj *b = js_bin_buffer_new(rt, n * TA_SZ[kind]);
    return view_make(rt, rt->p_ta[kind], kind, b, 0, n, 0);
}
jobj *js_bin_u8_new(struct jrt *rt, const void *src, uint32_t n)
{
    jobj *o = ta_alloc(rt, TA_U8, n); if (src && n) memcpy(((struct jta *)o->u.host.p)->buf->d, src, n); return o;
}
/* bytes de un ArrayBuffer / array tipado / DataView */
int js_bin_bytes(jv v, const uint8_t **p, uint32_t *n)
{
    if (v.t != JT_OBJ) return 0;
    jobj *o = v.u.o;
    if (o->cls == JC_ABUF) { const struct jbuf *b = (const struct jbuf *)o->u.host.p; *p = b->d; *n = b->len; return 1; }
    if (o->cls == JC_TYPED || o->cls == JC_DVIEW) {
        const struct jta *t = (const struct jta *)o->u.host.p; uint32_t cnt = o->cls == JC_TYPED ? view_len(t) : (t->track ? (t->buf->len > t->off ? t->buf->len - t->off : 0) : ((uint64_t)t->off + t->len <= t->buf->len ? t->len : 0));
        *p = t->buf->d + t->off; *n = o->cls == JC_TYPED ? cnt * TA_SZ[t->kind] : cnt; return 1;
    }
    return 0;
}
static jobj *proto_from_newtarget(struct jrt *rt, jobj *def)
{
    jobj *proto = def;
    if (rt->new_target) { jv pr = js_get(rt, jobjv(rt->new_target), rt->s_prototype); if (pr.t == JT_OBJ) proto = pr.u.o; rt->new_target = 0; }
    return proto;
}
static uint32_t to_index(struct jrt *rt, jv v, const char *what)
{
    if (v.t == JT_UNDEF) return 0;
    f64 n = js_tonum(rt, v); if (f64_is_nan(n)) return 0;
    n = f64_trunc(n);
    if (f64_lt(n, F64_ZERO) || f64_lt(0x41EFFFFFFFE00000ULL, n)) js_throw_err(rt, E_RANGE, "%s no valido", what);
    return (uint32_t)f64_to_i64(n);
}
static int64_t rel_idx(struct jrt *rt, jv v, int64_t len, int64_t dflt)
{
    if (v.t == JT_UNDEF) return dflt;
    f64 n = js_tonum(rt, v); if (f64_is_nan(n)) return 0;
    int64_t i = f64_to_i64(n);
    if (i < 0) { i += len; if (i < 0) i = 0; } else if (i > len) i = len;
    return i;
}

/* ------------------------------ ArrayBuffer ------------------------------ */
enum { AB_BYTELENGTH, AB_MAXBYTELENGTH, AB_RESIZABLE, AB_DETACHED, AB_SLICE, AB_RESIZE, AB_TRANSFER, AB_TRANSFERFIXED, AB_ISVIEW };
static jv abuf_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)    /* magic 1 = SharedArrayBuffer */
{
    (void)thisv;
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor %s requiere 'new'", magic ? "SharedArrayBuffer" : "ArrayBuffer");
    jobj *proto = proto_from_newtarget(rt, magic ? rt->p_sabuf : rt->p_abuf);
    uint32_t len = to_index(rt, ARG(0), "Longitud de ArrayBuffer"), mx = len; int resizable = 0;
    if (ARG(1).t == JT_OBJ) { jv m = js_get_c(rt, args[1], "maxByteLength"); if (m.t != JT_UNDEF) { mx = to_index(rt, m, "maxByteLength"); if (len > mx) js_throw_err(rt, E_RANGE, "maxByteLength menor que byteLength"); resizable = 1; } }
    return jobjv(buf_make(rt, proto, len, mx, magic, resizable));
}
static struct jbuf *buf_this(struct jrt *rt, jv t)
{
    if (t.t != JT_OBJ || t.u.o->cls != JC_ABUF) js_throw_err(rt, E_TYPE, "Metodo de ArrayBuffer llamado sobre un objeto incompatible");
    return (struct jbuf *)t.u.o->u.host.p;
}
static jv abuf_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct jbuf *b = buf_this(rt, thisv);
    switch (magic) {
    case AB_BYTELENGTH: return jint(b->len);
    case AB_MAXBYTELENGTH: return jint(b->resizable ? b->maxlen : b->len);
    case AB_RESIZABLE: return jbool(b->resizable);
    case AB_DETACHED: return jbool(b->detached);
    case AB_SLICE: {
        int64_t a = rel_idx(rt, ARG(0), b->len, 0), e = rel_idx(rt, ARG(1), b->len, b->len); if (e < a) e = a;
        jobj *n = buf_make(rt, thisv.u.o->proto ? thisv.u.o->proto : rt->p_abuf, (uint32_t)(e - a), (uint32_t)(e - a), b->shared, 0);
        if (e > a) memcpy(((struct jbuf *)n->u.host.p)->d, b->d + a, (size_t)(e - a));
        return jobjv(n);
    }
    case AB_RESIZE: {
        if (!b->resizable) js_throw_err(rt, E_TYPE, "El ArrayBuffer no es redimensionable");
        uint32_t nl = to_index(rt, ARG(0), "Longitud"); if (nl > b->maxlen) js_throw_err(rt, E_RANGE, "Longitud superior a maxByteLength");
        if (nl < b->len) memset(b->d + nl, 0, b->len - nl);
        b->len = nl; return jundef();
    }
    case AB_TRANSFER: case AB_TRANSFERFIXED: {
        if (b->detached) js_throw_err(rt, E_TYPE, "El ArrayBuffer esta separado");
        uint32_t nl = ARG(0).t == JT_UNDEF ? b->len : to_index(rt, ARG(0), "Longitud");
        int keep = magic == AB_TRANSFER && b->resizable;
        jobj *n = buf_make(rt, rt->p_abuf, nl, keep ? b->maxlen : nl, 0, keep);
        struct jbuf *nb = (struct jbuf *)n->u.host.p; memcpy(nb->d, b->d, nl < b->len ? nl : b->len);
        b->d = 0; b->len = 0; b->maxlen = 0; b->detached = 1; return jobjv(n);
    }
    }
    return jundef();
}
static jv abuf_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    return jbool(ARG(0).t == JT_OBJ && (ARG(0).u.o->cls == JC_TYPED || ARG(0).u.o->cls == JC_DVIEW));
}

/* ------------------------------ arrays tipados ------------------------------ */
struct cb_collect { jobj *arr; };
static int collect_cb(struct jrt *rt, jv item, void *ud) { js_arr_push(rt, ((struct cb_collect *)ud)->arr, item); return 1; }
static struct jta *ta_this(struct jrt *rt, jv t)
{
    if (t.t != JT_OBJ || t.u.o->cls != JC_TYPED) js_throw_err(rt, E_TYPE, "this no es un array tipado");
    return (struct jta *)t.u.o->u.host.p;
}
static jv ta_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int kind)
{
    (void)thisv;
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor %s requiere 'new'", TA_NAME[kind]);
    jobj *proto = proto_from_newtarget(rt, rt->p_ta[kind]);
    jv a0 = ARG(0); uint32_t sz = TA_SZ[kind];
    if (a0.t != JT_OBJ) {
        uint32_t n = to_index(rt, a0, "Longitud de array tipado");
        if ((uint64_t)n * sz > MAXBUF) js_throw_err(rt, E_RANGE, "Longitud de array tipado no valida: %u", (unsigned)n);
        jobj *b = js_bin_buffer_new(rt, n * sz); return jobjv(view_make(rt, proto, kind, b, 0, n, 0));
    }
    jobj *src = a0.u.o;
    if (src->cls == JC_ABUF) {
        struct jbuf *bf = (struct jbuf *)src->u.host.p; uint32_t off = to_index(rt, ARG(1), "Desplazamiento"); int track = 0; uint32_t len;
        if (bf->detached) js_throw_err(rt, E_TYPE, "El ArrayBuffer esta separado");
        if (off % sz) js_throw_err(rt, E_RANGE, "El desplazamiento de %s debe ser multiplo de %u", TA_NAME[kind], (unsigned)sz);
        if (ARG(2).t == JT_UNDEF) {
            if (off > bf->len) js_throw_err(rt, E_RANGE, "El desplazamiento %u esta fuera de los limites", (unsigned)off);
            if (bf->resizable) { track = 1; len = 0; }
            else { if (bf->len % sz) js_throw_err(rt, E_RANGE, "La longitud en bytes de %s debe ser multiplo de %u", TA_NAME[kind], (unsigned)sz); len = (bf->len - off) / sz; }
        } else {
            len = to_index(rt, args[2], "Longitud de array tipado");
            if ((uint64_t)off + (uint64_t)len * sz > bf->len) js_throw_err(rt, E_RANGE, "Longitud de array tipado no valida: %u", (unsigned)len);
        }
        return jobjv(view_make(rt, proto, kind, src, off, len, track));
    }
    if (src->cls == JC_TYPED) {
        const struct jta *s = (const struct jta *)src->u.host.p; uint32_t n = view_len(s);
        jobj *o = ta_alloc(rt, kind, n); struct jta *d = (struct jta *)o->u.host.p; o->proto = proto;
        if (s->kind == kind) memcpy(d->buf->d, s->buf->d + s->off, (size_t)n * sz);
        else for (uint32_t i = 0; i < n; i++) ta_store(d, i, js_tonum(rt, js_ta_get(src, i)));
        return jobjv(o);
    }
    if (src->cls == JC_DVIEW) js_throw_err(rt, E_TYPE, "No se puede construir un array tipado a partir de un DataView");
    /* iterable o tipo array */
    jobj *items; uint32_t n;
    jv itf = js_get_c(rt, a0, "@@iterator");
    if (src->cls == JC_ARRAY) { items = src; }
    else if (js_is_callable(itf)) { struct cb_collect cc; cc.arr = js_arr_new(rt, 8); js_iterate(rt, a0, collect_cb, &cc); items = cc.arr; }
    else {
        int64_t ln = f64_to_i64(js_tonum(rt, js_get(rt, a0, rt->s_length))); if (ln < 0) ln = 0; if (ln * sz > MAXBUF) js_throw_err(rt, E_RANGE, "Longitud de array tipado no valida");
        items = js_arr_new(rt, (uint32_t)ln); for (int64_t i = 0; i < ln; i++) js_arr_push(rt, items, js_getv(rt, a0, jint(i)));
    }
    n = items->alen;
    jobj *o = ta_alloc(rt, kind, n); o->proto = proto; struct jta *d = (struct jta *)o->u.host.p;
    for (uint32_t i = 0; i < n; i++) ta_store(d, i, js_tonum(rt, items->el[i]));
    return jobjv(o);
}
static jobj *ta_like(struct jrt *rt, const struct jta *t, uint32_t n)    /* array tipado nuevo del mismo tipo */
{
    return ta_alloc(rt, t->kind, n);
}
enum { T_BUFFER, T_BYTELENGTH, T_BYTEOFFSET, T_LENGTH, T_TAG, T_SET, T_SUBARRAY, T_SLICE, T_FILL, T_COPYWITHIN, T_REVERSE, T_INDEXOF, T_LASTINDEXOF, T_INCLUDES,
       T_JOIN, T_AT, T_WITH, T_TOREVERSED, T_TOSORTED, T_SORT, T_FOREACH, T_MAP, T_FILTER, T_REDUCE, T_REDUCERIGHT, T_FIND, T_FINDINDEX, T_FINDLAST, T_FINDLASTINDEX,
       T_EVERY, T_SOME, T_VALUES, T_KEYS, T_ENTRIES, T_TOSTRING };
static int cmp_num(jv a, jv b)                                          /* orden numerico por defecto: -0 < +0, NaN al final */
{
    f64 x = a.u.n, y = b.u.n;
    if (f64_is_nan(x)) return f64_is_nan(y) ? 0 : 1;
    if (f64_is_nan(y)) return -1;
    if (f64_lt(x, y)) return -1;
    if (f64_lt(y, x)) return 1;
    if (x == F64_ZERO && y != F64_ZERO) return 1;                        /* +0 despues de -0 */
    if (y == F64_ZERO && x != F64_ZERO) return -1;
    return 0;
}
static void msort(struct jrt *rt, jv *v, jv *tmp, uint32_t n, jv cmpf)
{
    if (n < 2) return;
    uint32_t h = n / 2; msort(rt, v, tmp, h, cmpf); msort(rt, v + h, tmp, n - h, cmpf);
    uint32_t i = 0, j = h, k = 0;
    while (i < h && j < n) {
        int c;
        if (cmpf.t == JT_UNDEF) c = cmp_num(v[i], v[j]);
        else { jv ar[2] = { v[i], v[j] }; f64 r = js_tonum(rt, js_call(rt, cmpf, jundef(), ar, 2)); c = f64_is_nan(r) ? 0 : f64_lt(r, F64_ZERO) ? -1 : f64_lt(F64_ZERO, r) ? 1 : 0; }
        tmp[k++] = c <= 0 ? v[i++] : v[j++];
    }
    while (i < h) tmp[k++] = v[i++];
    while (j < n) tmp[k++] = v[j++];
    memcpy(v, tmp, (size_t)n * sizeof(jv));
}
static void ta_sort_into(struct jrt *rt, jobj *src, jobj *dst, jv cmpf)
{
    uint32_t n = view_len((const struct jta *)src->u.host.p); if (n < 2 && src == dst) return;
    jv *v = (jv *)js_alloc(rt, (n ? n : 1) * 2 * (uint32_t)sizeof(jv)); jv *tmp = v + n;
    for (uint32_t i = 0; i < n; i++) v[i] = js_ta_get(src, i);
    msort(rt, v, tmp, n, cmpf);
    const struct jta *d = (const struct jta *)dst->u.host.p;
    for (uint32_t i = 0; i < n && i < view_len(d); i++) ta_store(d, i, v[i].u.n);
}
static jv ta_proto(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct jta *t = ta_this(rt, thisv); jobj *self = thisv.u.o; uint32_t n = view_len(t), sz = TA_SZ[t->kind];
    switch (magic) {
    case T_BUFFER: return jobjv(t->bufobj);
    case T_BYTELENGTH: return jint((int64_t)n * sz);
    case T_BYTEOFFSET: return jint(n || t->buf->len >= t->off ? t->off : 0);
    case T_LENGTH: return jint(n);
    case T_TAG: return jstrv(js_intern_c(rt, TA_NAME[t->kind]));
    case T_SET: {
        jv src = ARG(0); f64 of = js_tonum(rt, ARG(1)); if (f64_is_nan(of)) of = F64_ZERO; if (f64_lt(of, F64_ZERO)) js_throw_err(rt, E_RANGE, "desplazamiento fuera de los limites");
        int64_t off = f64_to_i64(of);
        if (src.t == JT_OBJ && src.u.o->cls == JC_TYPED) {
            const struct jta *s = (const struct jta *)src.u.o->u.host.p; uint32_t sn = view_len(s);
            if ((uint64_t)off + sn > n) js_throw_err(rt, E_RANGE, "la longitud de origen mas el desplazamiento supera la del destino");
            if (s->kind == t->kind || (TA_SZ[s->kind] == 1 && sz == 1 && s->kind != TA_U8C && t->kind != TA_U8C && s->kind != TA_I8 && t->kind != TA_I8))
                { if (sn) memmove(t->buf->d + t->off + (size_t)off * sz, s->buf->d + s->off, (size_t)sn * sz); }   /* SOMA 188B: origen vacio (puntero nulo) */
            else {
                jv *tmpv = (jv *)js_alloc(rt, (sn ? sn : 1) * (uint32_t)sizeof(jv)); for (uint32_t i = 0; i < sn; i++) tmpv[i] = js_ta_get(src.u.o, i);
                for (uint32_t i = 0; i < sn; i++) ta_store(t, (uint32_t)off + i, tmpv[i].u.n);
            }
            return jundef();
        }
        if (src.t != JT_OBJ && src.t != JT_STR) js_throw_err(rt, E_TYPE, "origen no valido para set()");
        int64_t ln = f64_to_i64(js_tonum(rt, js_get(rt, src, rt->s_length))); if (ln < 0) ln = 0;
        if ((uint64_t)off + (uint64_t)ln > n) js_throw_err(rt, E_RANGE, "la longitud de origen mas el desplazamiento supera la del destino");
        for (int64_t i = 0; i < ln; i++) js_ta_set(rt, self, (uint32_t)(off + i), js_getv(rt, src, jint(i)));
        return jundef();
    }
    case T_SUBARRAY: {
        int64_t a = rel_idx(rt, ARG(0), n, 0), e = rel_idx(rt, ARG(1), n, n); if (e < a) e = a;
        return jobjv(view_make(rt, self->proto ? self->proto : rt->p_ta[t->kind], t->kind, t->bufobj, t->off + (uint32_t)a * sz, (uint32_t)(e - a), 0));
    }
    case T_SLICE: {
        int64_t a = rel_idx(rt, ARG(0), n, 0), e = rel_idx(rt, ARG(1), n, n); if (e < a) e = a;
        jobj *o = ta_like(rt, t, (uint32_t)(e - a)); if (e > a) memcpy(((struct jta *)o->u.host.p)->buf->d, t->buf->d + t->off + (size_t)a * sz, (size_t)(e - a) * sz); return jobjv(o);
    }
    case T_FILL: {
        f64 v = js_tonum(rt, ARG(0)); int64_t a = rel_idx(rt, ARG(1), n, 0), e = rel_idx(rt, ARG(2), n, n); n = view_len(t); if (e > n) e = n;
        for (int64_t i = a; i < e; i++) ta_store(t, (uint32_t)i, v);
        return thisv;
    }
    case T_COPYWITHIN: {
        int64_t to = rel_idx(rt, ARG(0), n, 0), from = rel_idx(rt, ARG(1), n, 0), fin = rel_idx(rt, ARG(2), n, n);
        int64_t cnt = fin - from; if (cnt > (int64_t)n - to) cnt = (int64_t)n - to;
        if (cnt > 0) memmove(t->buf->d + t->off + (size_t)to * sz, t->buf->d + t->off + (size_t)from * sz, (size_t)cnt * sz);
        return thisv;
    }
    case T_REVERSE: case T_TOREVERSED: {
        jobj *dst = magic == T_TOREVERSED ? ta_like(rt, t, n) : self; struct jta *d = (struct jta *)dst->u.host.p;
        uint8_t tmp[8];
        if (dst == self) { for (uint32_t i = 0; i < n / 2; i++) { uint8_t *p = t->buf->d + t->off + (size_t)i * sz, *q = t->buf->d + t->off + (size_t)(n - 1 - i) * sz; memcpy(tmp, p, sz); memcpy(p, q, sz); memcpy(q, tmp, sz); } }
        else for (uint32_t i = 0; i < n; i++) memcpy(d->buf->d + d->off + (size_t)i * sz, t->buf->d + t->off + (size_t)(n - 1 - i) * sz, sz);
        return jobjv(dst);
    }
    case T_INDEXOF: case T_LASTINDEXOF: case T_INCLUDES: {
        if (!n) return magic == T_INCLUDES ? jbool(0) : jint(-1);
        if (magic == T_LASTINDEXOF) {
            int64_t i = ARG(1).t == JT_UNDEF && argc < 2 ? (int64_t)n - 1 : f64_to_i64(js_tonum(rt, ARG(1))); if (i < 0) i += n; if (i >= (int64_t)n) i = n - 1;
            for (; i >= 0; i--) if (js_strict_eq(js_ta_get(self, (uint32_t)i), ARG(0))) return jint(i);
            return jint(-1);
        }
        int64_t i = rel_idx(rt, ARG(1), n, 0);
        for (; i < (int64_t)n; i++) {
            jv e = js_ta_get(self, (uint32_t)i);
            if (magic == T_INCLUDES ? js_same_zero(e, ARG(0)) : js_strict_eq(e, ARG(0))) return magic == T_INCLUDES ? jbool(1) : jint(i);
        }
        return magic == T_INCLUDES ? jbool(0) : jint(-1);
    }
    case T_JOIN: case T_TOSTRING: {
        jstr *sep = (magic == T_JOIN && ARG(0).t != JT_UNDEF) ? js_tostr(rt, args[0]) : js_intern_c(rt, ",");
        struct wb b = {0};
        for (uint32_t i = 0; i < n; i++) { if (i) wb_addn(&b, sep->d, sep->len); jstr *s = js_tostr(rt, js_ta_get(self, i)); wb_addn(&b, s->d, s->len); }
        jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
    }
    case T_AT: {
        int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); if (i < 0) i += n; if (i < 0 || i >= (int64_t)n) return jundef(); return js_ta_get(self, (uint32_t)i);
    }
    case T_WITH: {
        int64_t i = f64_to_i64(js_tonum(rt, ARG(0))); if (i < 0) i += n; f64 v = js_tonum(rt, ARG(1));
        if (i < 0 || i >= (int64_t)view_len(t)) js_throw_err(rt, E_RANGE, "Indice no valido");
        jobj *o = ta_like(rt, t, n); memcpy(((struct jta *)o->u.host.p)->buf->d, t->buf->d + t->off, (size_t)n * sz); ta_store((struct jta *)o->u.host.p, (uint32_t)i, v); return jobjv(o);
    }
    case T_SORT: case T_TOSORTED: {
        if (ARG(0).t != JT_UNDEF && !js_is_callable(args[0])) js_throw_err(rt, E_TYPE, "El comparador debe ser una funcion o undefined");
        jobj *dst = magic == T_TOSORTED ? ta_like(rt, t, n) : self;
        if (dst != self && n) memcpy(((struct jta *)dst->u.host.p)->buf->d, t->buf->d + t->off, (size_t)n * sz);
        ta_sort_into(rt, dst, dst, ARG(0)); return jobjv(dst);
    }
    case T_VALUES: case T_KEYS: case T_ENTRIES: return js_make_iterator(rt, thisv, magic == T_VALUES ? 0 : magic == T_KEYS ? 1 : 2);
    }
    /* metodos con callback */
    jv fn = ARG(0), th = ARG(1);
    if (!js_is_callable(fn)) js_throw_err(rt, E_TYPE, "%s no es una funcion", "El argumento");
    switch (magic) {
    case T_FOREACH: for (uint32_t i = 0; i < n; i++) { jv a[3] = { js_ta_get(self, i), jint(i), thisv }; js_call(rt, fn, th, a, 3); } return jundef();
    case T_MAP: { jobj *o = ta_like(rt, t, n); for (uint32_t i = 0; i < n; i++) { jv a[3] = { js_ta_get(self, i), jint(i), thisv }; js_ta_set(rt, o, i, js_call(rt, fn, th, a, 3)); } return jobjv(o); }
    case T_FILTER: {
        jobj *keep = js_arr_new(rt, 8);
        for (uint32_t i = 0; i < n; i++) { jv e = js_ta_get(self, i); jv a[3] = { e, jint(i), thisv }; if (js_truthy(js_call(rt, fn, th, a, 3))) js_arr_push(rt, keep, e); }
        jobj *o = ta_like(rt, t, keep->alen); for (uint32_t i = 0; i < keep->alen; i++) ta_store((struct jta *)o->u.host.p, i, keep->el[i].u.n); return jobjv(o);
    }
    case T_REDUCE: case T_REDUCERIGHT: {
        int64_t i = magic == T_REDUCE ? 0 : (int64_t)n - 1, st = magic == T_REDUCE ? 1 : -1; jv acc;
        if (argc >= 2) acc = args[1]; else { if (!n) js_throw_err(rt, E_TYPE, "Reduce de un array tipado vacio sin valor inicial"); acc = js_ta_get(self, (uint32_t)i); i += st; }
        for (; i >= 0 && i < (int64_t)n; i += st) { jv a[4] = { acc, js_ta_get(self, (uint32_t)i), jint(i), thisv }; acc = js_call(rt, fn, jundef(), a, 4); }
        return acc;
    }
    case T_FIND: case T_FINDINDEX: case T_FINDLAST: case T_FINDLASTINDEX: {
        int back = magic == T_FINDLAST || magic == T_FINDLASTINDEX;
        for (int64_t k = 0; k < (int64_t)n; k++) {
            uint32_t i = (uint32_t)(back ? (int64_t)n - 1 - k : k); jv e = js_ta_get(self, i); jv a[3] = { e, jint(i), thisv };
            if (js_truthy(js_call(rt, fn, th, a, 3))) return (magic == T_FIND || magic == T_FINDLAST) ? e : jint(i);
        }
        return (magic == T_FIND || magic == T_FINDLAST) ? jundef() : jint(-1);
    }
    case T_EVERY: for (uint32_t i = 0; i < n; i++) { jv a[3] = { js_ta_get(self, i), jint(i), thisv }; if (!js_truthy(js_call(rt, fn, th, a, 3))) return jbool(0); } return jbool(1);
    case T_SOME: for (uint32_t i = 0; i < n; i++) { jv a[3] = { js_ta_get(self, i), jint(i), thisv }; if (js_truthy(js_call(rt, fn, th, a, 3))) return jbool(1); } return jbool(0);
    }
    return jundef();
}
static jv ta_static(struct jrt *rt, jv thisv, jv *args, int argc, int magic)     /* magic = tipo*2 + (1 = of) */
{
    int kind = magic >> 1; jobj *o;
    if (magic & 1) { o = ta_alloc(rt, kind, (uint32_t)argc); for (int i = 0; i < argc; i++) js_ta_set(rt, o, (uint32_t)i, args[i]); return jobjv(o); }
    jv src = ARG(0), fn = ARG(1), th = ARG(2);
    if (fn.t != JT_UNDEF && !js_is_callable(fn)) js_throw_err(rt, E_TYPE, "La funcion de mapeo no es una funcion");
    jobj *items;
    if (src.t == JT_STR) { struct cb_collect cc; cc.arr = js_arr_new(rt, 8); js_iterate(rt, src, collect_cb, &cc); items = cc.arr; }
    else if (src.t == JT_OBJ && js_is_callable(js_get_c(rt, src, "@@iterator"))) { struct cb_collect cc; cc.arr = js_arr_new(rt, 8); js_iterate(rt, src, collect_cb, &cc); items = cc.arr; }
    else if (src.t == JT_OBJ) { int64_t ln = f64_to_i64(js_tonum(rt, js_get(rt, src, rt->s_length))); if (ln < 0) ln = 0; if (ln * TA_SZ[kind] > MAXBUF) js_throw_err(rt, E_RANGE, "Longitud no valida"); items = js_arr_new(rt, (uint32_t)ln); for (int64_t i = 0; i < ln; i++) js_arr_push(rt, items, js_getv(rt, src, jint(i))); }
    else js_throw_err(rt, E_TYPE, "el valor no es iterable");
    (void)thisv;
    o = ta_alloc(rt, kind, items->alen);
    for (uint32_t i = 0; i < items->alen; i++) { jv v = items->el[i]; if (fn.t != JT_UNDEF) { jv a[2] = { v, jint(i) }; v = js_call(rt, fn, th, a, 2); } js_ta_set(rt, o, i, v); }
    return jobjv(o);
}

/* ------------------------------ DataView ------------------------------ */
enum { DV_BUFFER, DV_BYTELENGTH, DV_BYTEOFFSET, DV_TAG };
static jv dv_ctor(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic;
    if (!rt->new_target) js_throw_err(rt, E_TYPE, "El constructor DataView requiere 'new'");
    jobj *proto = proto_from_newtarget(rt, rt->p_dview);
    if (ARG(0).t != JT_OBJ || args[0].u.o->cls != JC_ABUF) js_throw_err(rt, E_TYPE, "El primer argumento de DataView debe ser un ArrayBuffer");
    struct jbuf *bf = (struct jbuf *)args[0].u.o->u.host.p; uint32_t off = to_index(rt, ARG(1), "Desplazamiento"), len; int track = 0;
    if (bf->detached) js_throw_err(rt, E_TYPE, "El ArrayBuffer esta separado");
    if (off > bf->len) js_throw_err(rt, E_RANGE, "Desplazamiento de DataView fuera de los limites");
    if (ARG(2).t == JT_UNDEF) { if (bf->resizable) { track = 1; len = 0; } else len = bf->len - off; }
    else { len = to_index(rt, args[2], "Longitud"); if ((uint64_t)off + len > bf->len) js_throw_err(rt, E_RANGE, "Longitud de DataView fuera de los limites"); }
    return jobjv(view_make(rt, proto, TA_N, args[0].u.o, off, len, track));
}
static struct jta *dv_this(struct jrt *rt, jv t)
{
    if (t.t != JT_OBJ || t.u.o->cls != JC_DVIEW) js_throw_err(rt, E_TYPE, "Metodo de DataView llamado sobre un objeto incompatible");
    return (struct jta *)t.u.o->u.host.p;
}
static uint32_t dv_size(const struct jta *t)
{
    if (t->track) return t->buf->len > t->off ? t->buf->len - t->off : 0;
    return (uint64_t)t->off + t->len <= t->buf->len ? t->len : 0;
}
static jv dv_prop(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)args; (void)argc; struct jta *t = dv_this(rt, thisv);
    switch (magic) {
    case DV_BUFFER: return jobjv(t->bufobj);
    case DV_BYTELENGTH: return jint(dv_size(t));
    case DV_BYTEOFFSET: return jint(t->off);
    default: return jstrv(js_intern_c(rt, "DataView"));
    }
}
static jv dv_get(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct jta *t = dv_this(rt, thisv); uint32_t sz = TA_SZ[magic], off = to_index(rt, ARG(0), "Desplazamiento"); int little = js_truthy(ARG(1));
    if ((uint64_t)off + sz > dv_size(t)) js_throw_err(rt, E_RANGE, "El desplazamiento esta fuera de los limites del DataView");
    uint8_t b[8]; memcpy(b, t->buf->d + t->off + off, sz);
    if (!little) for (uint32_t i = 0; i < sz / 2; i++) { uint8_t x = b[i]; b[i] = b[sz - 1 - i]; b[sz - 1 - i] = x; }
    switch (magic) {
    case TA_I8: return jint((int8_t)b[0]);
    case TA_U8: case TA_U8C: return jint(b[0]);
    case TA_I16: { int16_t v; memcpy(&v, b, 2); return jint(v); }
    case TA_U16: { uint16_t v; memcpy(&v, b, 2); return jint(v); }
    case TA_I32: { int32_t v; memcpy(&v, b, 4); return jint(v); }
    case TA_U32: { uint32_t v; memcpy(&v, b, 4); return jint(v); }
    case TA_F32: { uint32_t v; memcpy(&v, b, 4); return jnum(f32_to_f64(v)); }
    default: { f64 v; memcpy(&v, b, 8); return jnum(v); }
    }
}
static jv dv_set(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    struct jta *t = dv_this(rt, thisv); uint32_t sz = TA_SZ[magic], off = to_index(rt, ARG(0), "Desplazamiento"); f64 n = js_tonum(rt, ARG(1)); int little = js_truthy(ARG(2));
    if ((uint64_t)off + sz > dv_size(t)) js_throw_err(rt, E_RANGE, "El desplazamiento esta fuera de los limites del DataView");
    uint8_t b[8];
    switch (magic) {
    case TA_I8: case TA_U8: case TA_U8C: b[0] = (uint8_t)f64_to_i32(n); break;
    case TA_I16: case TA_U16: { uint16_t v = (uint16_t)f64_to_i32(n); memcpy(b, &v, 2); break; }
    case TA_I32: case TA_U32: { uint32_t v = (uint32_t)f64_to_i32(n); memcpy(b, &v, 4); break; }
    case TA_F32: { uint32_t v = f64_to_f32(n); memcpy(b, &v, 4); break; }
    default: memcpy(b, &n, 8); break;
    }
    if (!little) for (uint32_t i = 0; i < sz / 2; i++) { uint8_t x = b[i]; b[i] = b[sz - 1 - i]; b[sz - 1 - i] = x; }
    memcpy(t->buf->d + t->off + off, b, sz); return jundef();
}

/* ------------------------------ texto, base64 y bytes crudos ------------------------------ */
static jv bytes_arg(struct jrt *rt, jv v, const uint8_t **p, uint32_t *n)
{
    if (!js_bin_bytes(v, p, n)) js_throw_err(rt, E_TYPE, "Se esperaba un ArrayBuffer o una vista");
    return v;
}
/* UTF-8 -> Uint8Array: las mitades sustitutas sueltas (WTF-8) pasan a U+FFFD, como TextEncoder */
static jv n_utf8enc(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic; jstr *s = js_tostr(rt, ARG(0)); jobj *o = ta_alloc(rt, TA_U8, s->len); uint8_t *d = ((struct jta *)o->u.host.p)->buf->d; uint32_t j = 0;
    for (uint32_t i = 0; i < s->len;) {
        uint8_t c = (uint8_t)s->d[i];
        if (c == 0xED && i + 2 < s->len && ((uint8_t)s->d[i + 1] & 0xE0) == 0xA0) { d[j++] = 0xEF; d[j++] = 0xBF; d[j++] = 0xBD; i += 3; continue; }
        d[j++] = c; i++;
    }
    ((struct jta *)o->u.host.p)->len = j;                                        /* puede encogerse (3 bytes -> 3 bytes) */
    return jobjv(o);
}
/* encodeInto(cadena, u8) -> [leidas (unidades UTF-16), escritas (bytes)] */
static jv n_utf8into(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic; jstr *s = js_tostr(rt, ARG(0)); const uint8_t *p; uint32_t cap; bytes_arg(rt, ARG(1), &p, &cap); uint8_t *d = (uint8_t *)p; uint32_t j = 0, read = 0;
    for (uint32_t i = 0; i < s->len;) {
        uint8_t c = (uint8_t)s->d[i]; uint32_t l = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; if (i + l > s->len) l = s->len - i;
        int lone = c == 0xED && l == 3 && ((uint8_t)s->d[i + 1] & 0xE0) == 0xA0; uint32_t ol = lone ? 3 : l;
        if (j + ol > cap) break;
        if (lone) { d[j++] = 0xEF; d[j++] = 0xBF; d[j++] = 0xBD; } else { memcpy(d + j, s->d + i, l); j += l; }
        read += l == 4 ? 2 : 1; i += l;
    }
    jv r[2] = { jint(read), jint(j) }; return jobjv(js_array_from(rt, r, 2));
}
static const uint16_t CP1252[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                     0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
static void put_cp(struct wb *b, uint32_t cp) { char t[4]; int n = wu_utf8_put(cp, t); wb_addn(b, t, (uint32_t)n); }
/* decode(vista, fatal, ignoreBOM, codificacion: 0 utf-8, 1 utf-16le, 2 utf-16be, 3 windows-1252, 4 latin1) */
static jv n_utf8dec(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic; const uint8_t *p; uint32_t n; bytes_arg(rt, ARG(0), &p, &n);
    int fatal = js_truthy(ARG(1)), ignbom = js_truthy(ARG(2)), enc = (int)f64_to_i64(js_tonum(rt, ARG(3))), bad = 0; struct wb b = {0}; uint32_t i = 0;
    if (enc == 0) {
        if (!ignbom && n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) i = 3;
        while (i < n) {
            uint8_t c = p[i];
            if (c < 0x80) { wb_addc(&b, (char)c); i++; continue; }
            uint32_t need; uint8_t lo = 0x80, hi = 0xBF;
            if (c >= 0xC2 && c <= 0xDF) need = 1;
            else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
            else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
            else { bad = 1; wb_addn(&b, "\xEF\xBF\xBD", 3); i++; continue; }
            uint32_t j = i + 1, got = 0;
            while (got < need && j < n && p[j] >= lo && p[j] <= hi) { j++; got++; lo = 0x80; hi = 0xBF; }
            if (got == need) wb_addn(&b, (const char *)p + i, j - i); else { bad = 1; wb_addn(&b, "\xEF\xBF\xBD", 3); }
            i = j;
        }
    } else if (enc == 1 || enc == 2) {
        int be = enc == 2;
        if (!ignbom && n >= 2 && ((!be && p[0] == 0xFF && p[1] == 0xFE) || (be && p[0] == 0xFE && p[1] == 0xFF))) i = 2;
        for (; i + 1 < n; i += 2) {
            uint32_t u = be ? (uint32_t)(p[i] << 8 | p[i + 1]) : (uint32_t)(p[i + 1] << 8 | p[i]);
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n) {
                uint32_t u2 = be ? (uint32_t)(p[i + 2] << 8 | p[i + 3]) : (uint32_t)(p[i + 3] << 8 | p[i + 2]);
                if (u2 >= 0xDC00 && u2 <= 0xDFFF) { put_cp(&b, 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00)); i += 2; continue; }
            }
            if (u >= 0xD800 && u <= 0xDFFF) { bad = 1; put_cp(&b, 0xFFFD); } else put_cp(&b, u);
        }
        if (i < n) { bad = 1; put_cp(&b, 0xFFFD); }
    } else {
        for (; i < n; i++) { uint32_t cp = p[i]; if (enc == 3 && cp >= 0x80 && cp < 0xA0) cp = CP1252[cp - 0x80]; put_cp(&b, cp); }
    }
    if (bad && fatal) { wb_free(&b); js_throw_err(rt, E_TYPE, "Los datos codificados no son validos para la codificacion indicada"); }
    jstr *r = js_str_new(rt, b.p ? b.p : "", b.len); wb_free(&b); return jstrv(r);
}
static jv n_rawbytes(struct jrt *rt, jv thisv, jv *args, int argc, int magic)    /* bytes tal cual de una cadena (cuerpos de red) */
{
    (void)thisv; (void)magic; jstr *s = js_tostr(rt, ARG(0)); return jobjv(js_bin_u8_new(rt, s->d, s->len));
}
static jv n_rawstr(struct jrt *rt, jv thisv, jv *args, int argc, int magic)       /* cadena con los bytes tal cual (cuerpos de peticion binarios) */
{
    (void)thisv; (void)magic; const uint8_t *p; uint32_t n; bytes_arg(rt, ARG(0), &p, &n); return jstrv(js_str_new(rt, (const char *)p, n));
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static jv n_b64enc(struct jrt *rt, jv thisv, jv *args, int argc, int magic)
{
    (void)thisv; (void)magic; const uint8_t *p; uint32_t n; bytes_arg(rt, ARG(0), &p, &n);
    uint32_t ol = (n + 2) / 3 * 4; jstr *r = (jstr *)js_alloc(rt, (uint32_t)sizeof(jstr) + ol + 1); r->len = ol; uint32_t j = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0u);
        r->d[j++] = B64[v >> 18 & 63]; r->d[j++] = B64[v >> 12 & 63]; r->d[j++] = i + 1 < n ? B64[v >> 6 & 63] : '='; r->d[j++] = i + 2 < n ? B64[v & 63] : '=';
    }
    r->d[ol] = 0; r->hash = wu_hash(r->d, ol); return jstrv(r);
}
static jv n_b64dec(struct jrt *rt, jv thisv, jv *args, int argc, int magic)     /* null si la cadena no es base64 valido (como atob) */
{
    (void)thisv; (void)magic; jstr *s = js_tostr(rt, ARG(0)); uint8_t *tmp = (uint8_t *)js_alloc(rt, s->len / 4 * 3 + 4); uint32_t acc = 0, bits = 0, j = 0, cnt = 0, pad = 0;
    for (uint32_t i = 0; i < s->len; i++) {
        uint8_t c = (uint8_t)s->d[i]; int v;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') continue;
        if (c == '=') { pad++; continue; }
        if (pad) return jnull();
        v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0) return jnull();
        acc = acc << 6 | (uint32_t)v; bits += 6; cnt++;
        if (bits >= 8) { bits -= 8; tmp[j++] = (uint8_t)(acc >> bits); acc &= (1u << bits) - 1; }
    }
    if (cnt % 4 == 1 || (pad && (cnt + pad) % 4)) return jnull();
    return jobjv(js_bin_u8_new(rt, tmp, j));
}
/* structuredClone de datos binarios (nivel superior): copia el contenido */
jv js_bin_clone(struct jrt *rt, jv v)
{
    if (v.t != JT_OBJ) return v;
    jobj *o = v.u.o;
    if (o->cls == JC_ABUF) { struct jbuf *b = (struct jbuf *)o->u.host.p; if (b->detached) js_throw_err(rt, E_TYPE, "El ArrayBuffer esta separado"); jobj *n = buf_make(rt, o->proto, b->len, b->maxlen, b->shared, b->resizable); if (b->len) memcpy(((struct jbuf *)n->u.host.p)->d, b->d, b->len); return jobjv(n); }
    if (o->cls == JC_TYPED || o->cls == JC_DVIEW) {
        const struct jta *t = (const struct jta *)o->u.host.p; jv nb = js_bin_clone(rt, jobjv(t->bufobj));
        return jobjv(view_make(rt, o->proto, t->kind, nb.u.o, t->off, t->len, t->track));
    }
    return v;
}

/* ------------------------------ registro ------------------------------ */
static jobj *bin_ctor(struct jrt *rt, const char *name, jnative f, int nargs, int magic, jobj *proto)
{
    jobj *c = js_native(rt, name, f, nargs, magic);
    js_def(rt, c, rt->s_prototype, jobjv(proto), JP_NOCONF | JP_RO); js_def(rt, proto, rt->s_constructor, jobjv(c), 0);
    js_def(rt, rt->global, js_intern_c(rt, name), jobjv(c), 0); return c;
}
void js_bin_init(struct jrt *rt)
{
    if (!js_stream_on()) js_def(rt, rt->global, js_intern_c(rt, "__noStream"), jbool(1), 0);   /* SOMA_187 */
    if (!js_bin_on()) return;
    /* ArrayBuffer / SharedArrayBuffer */
    rt->p_abuf = js_obj_new(rt, rt->p_object); rt->p_sabuf = js_obj_new(rt, rt->p_object);
    jobj *ac = bin_ctor(rt, "ArrayBuffer", abuf_ctor, 1, 0, rt->p_abuf); js_method(rt, ac, "isView", abuf_static, 1, AB_ISVIEW);
    bin_ctor(rt, "SharedArrayBuffer", abuf_ctor, 1, 1, rt->p_sabuf);
    js_getter(rt, rt->p_abuf, "byteLength", abuf_proto, 0, AB_BYTELENGTH); js_getter(rt, rt->p_abuf, "maxByteLength", abuf_proto, 0, AB_MAXBYTELENGTH);
    js_getter(rt, rt->p_abuf, "resizable", abuf_proto, 0, AB_RESIZABLE); js_getter(rt, rt->p_abuf, "detached", abuf_proto, 0, AB_DETACHED);
    js_method(rt, rt->p_abuf, "slice", abuf_proto, 2, AB_SLICE); js_method(rt, rt->p_abuf, "resize", abuf_proto, 1, AB_RESIZE);
    js_method(rt, rt->p_abuf, "transfer", abuf_proto, 0, AB_TRANSFER); js_method(rt, rt->p_abuf, "transferToFixedLength", abuf_proto, 0, AB_TRANSFERFIXED);
    js_def(rt, rt->p_abuf, js_intern_c(rt, "@@toStringTag"), jstrv(js_intern_c(rt, "ArrayBuffer")), 0);
    js_getter(rt, rt->p_sabuf, "byteLength", abuf_proto, 0, AB_BYTELENGTH); js_method(rt, rt->p_sabuf, "slice", abuf_proto, 2, AB_SLICE);
    js_def(rt, rt->p_sabuf, js_intern_c(rt, "@@toStringTag"), jstrv(js_intern_c(rt, "SharedArrayBuffer")), 0);
    /* %TypedArray%.prototype */
    jobj *tp = js_obj_new(rt, rt->p_object); rt->p_typed = tp;
    static const struct { const char *n; int m; int a; } TM[] = {
        { "set", T_SET, 1 }, { "subarray", T_SUBARRAY, 2 }, { "slice", T_SLICE, 2 }, { "fill", T_FILL, 1 }, { "copyWithin", T_COPYWITHIN, 2 }, { "reverse", T_REVERSE, 0 },
        { "indexOf", T_INDEXOF, 1 }, { "lastIndexOf", T_LASTINDEXOF, 1 }, { "includes", T_INCLUDES, 1 }, { "join", T_JOIN, 1 }, { "at", T_AT, 1 }, { "with", T_WITH, 2 },
        { "toReversed", T_TOREVERSED, 0 }, { "toSorted", T_TOSORTED, 1 }, { "sort", T_SORT, 1 }, { "forEach", T_FOREACH, 1 }, { "map", T_MAP, 1 }, { "filter", T_FILTER, 1 },
        { "reduce", T_REDUCE, 1 }, { "reduceRight", T_REDUCERIGHT, 1 }, { "find", T_FIND, 1 }, { "findIndex", T_FINDINDEX, 1 }, { "findLast", T_FINDLAST, 1 },
        { "findLastIndex", T_FINDLASTINDEX, 1 }, { "every", T_EVERY, 1 }, { "some", T_SOME, 1 }, { "values", T_VALUES, 0 }, { "keys", T_KEYS, 0 }, { "entries", T_ENTRIES, 0 },
        { "toString", T_TOSTRING, 0 }, { "toLocaleString", T_TOSTRING, 0 }, { 0, 0, 0 } };
    for (int i = 0; TM[i].n; i++) js_method(rt, tp, TM[i].n, ta_proto, TM[i].a, TM[i].m);
    js_getter(rt, tp, "buffer", ta_proto, 0, T_BUFFER); js_getter(rt, tp, "byteLength", ta_proto, 0, T_BYTELENGTH); js_getter(rt, tp, "byteOffset", ta_proto, 0, T_BYTEOFFSET);
    js_getter(rt, tp, "length", ta_proto, 0, T_LENGTH); js_getter(rt, tp, "@@toStringTag", ta_proto, 0, T_TAG);
    js_def(rt, tp, js_intern_c(rt, "@@iterator"), js_get_c(rt, jobjv(tp), "values"), 0);
    for (int k = 0; k < TA_N; k++) {
        jobj *pr = js_obj_new(rt, tp); rt->p_ta[k] = pr;
        jobj *c = bin_ctor(rt, TA_NAME[k], ta_ctor, 3, k, pr);
        js_def(rt, c, js_intern_c(rt, "BYTES_PER_ELEMENT"), jint(TA_SZ[k]), JP_RO | JP_NOCONF); js_def(rt, pr, js_intern_c(rt, "BYTES_PER_ELEMENT"), jint(TA_SZ[k]), JP_RO | JP_NOCONF);
        js_method(rt, c, "from", ta_static, 1, k * 2); js_method(rt, c, "of", ta_static, 0, k * 2 + 1);
    }
    /* DataView */
    jobj *dp = js_obj_new(rt, rt->p_object); rt->p_dview = dp; bin_ctor(rt, "DataView", dv_ctor, 1, 0, dp);
    js_getter(rt, dp, "buffer", dv_prop, 0, DV_BUFFER); js_getter(rt, dp, "byteLength", dv_prop, 0, DV_BYTELENGTH); js_getter(rt, dp, "byteOffset", dv_prop, 0, DV_BYTEOFFSET); js_getter(rt, dp, "@@toStringTag", dv_prop, 0, DV_TAG);
    static const struct { const char *g, *s; int k; } DV[] = { { "getInt8", "setInt8", TA_I8 }, { "getUint8", "setUint8", TA_U8 }, { "getInt16", "setInt16", TA_I16 }, { "getUint16", "setUint16", TA_U16 },
        { "getInt32", "setInt32", TA_I32 }, { "getUint32", "setUint32", TA_U32 }, { "getFloat32", "setFloat32", TA_F32 }, { "getFloat64", "setFloat64", TA_F64 }, { 0, 0, 0 } };
    for (int i = 0; DV[i].g; i++) { js_method(rt, dp, DV[i].g, dv_get, 1, DV[i].k); js_method(rt, dp, DV[i].s, dv_set, 2, DV[i].k); }
    /* piezas nativas del prelude (TextEncoder/TextDecoder/Blob/base64/cuerpos de red) */
    static const struct { const char *n; jnative f; int a; } NT[] = { { "__utf8enc", n_utf8enc, 1 }, { "__utf8into", n_utf8into, 2 }, { "__utf8dec", n_utf8dec, 4 }, { "__rawbytes", n_rawbytes, 1 }, { "__rawstr", n_rawstr, 1 },
        { "__b64enc", n_b64enc, 1 }, { "__b64dec", n_b64dec, 1 }, { 0, 0, 0 } };
    for (int i = 0; NT[i].n; i++) js_def(rt, rt->global, js_intern_c(rt, NT[i].n), jobjv(js_native(rt, NT[i].n, NT[i].f, NT[i].a, 0)), 0);
    js_def(rt, rt->global, js_intern_c(rt, "__nativeBin"), jbool(1), 0);
}
