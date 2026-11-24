/* SOMA 189: recolector de basura del interprete de JavaScript.
 *
 * Hasta ahora el monton JS era una arena que solo crecia: cada objeto, cadena o ambito temporal se quedaba hasta cerrar la
 * pagina. La portada de Google gastaba 260 MiB y YouTube pasaba de 3 GiB antes de pintar nada.
 *
 * Diseno: marcado y barrido conservador, sin mover objetos.
 *  - Los bloques pequenos (hasta 2048 bytes) salen de trozos de 64 KiB dedicados a un tamano; cada trozo lleva un mapa de bits de
 *    marcas. Los grandes se piden uno a uno al nucleo. No hay cabecera por bloque.
 *  - El marcado es conservador: cualquier palabra de 8 bytes de una raiz o de un bloque vivo que apunte dentro de un bloque (al
 *    principio o al interior) lo mantiene vivo. El interprete guarda punteros JS en variables locales de C por todas partes, asi
 *    que no hace falta registrar raices a mano: se recorren la pila de C en curso, las pilas de las corrutinas suspendidas, la
 *    pila de ambitos y la estructura del interprete. Los bloques "atomicos" (texto de cadenas) no se recorren.
 *  - El barrido reconstruye las listas de libres y devuelve al nucleo los trozos vacios.
 * Lo unico que guarda punteros JS fuera de este monton es wnode->js (el envoltorio de cada nodo del DOM): web_js_dom.c los
 * apunta en una lista que vive aqui dentro, de modo que un envoltorio dura lo que la pagina.
 * Se desactiva con soma.jsgc=off (el monton vuelve a crecer sin liberar). */
#include "web_js_i.h"

#define GC_CHUNK   (64u << 10)
#define GC_HDR     576u                       /* cabecera del trozo (mapa de 4096 bits) alineada a 64 */
#define GC_NCLS    28
#define GC_MAXSMALL 2048u
#define GC_KEEP_EMPTY 32                      /* trozos vacios que se conservan tras un barrido */

#if defined(WEB_HOST_TEST) && defined(__SANITIZE_ADDRESS__)
#define GC_QUAR 1                             /* pruebas con ASan: HL_GC_QUAR=1 no reutiliza lo liberado (cuarentena) para cazar el primer acceso */
#undef GC_HDR
#define GC_HDR 1088u
struct gchunk { uint32_t bsize, nblocks, nmarked; uint8_t atomic, cls; uint64_t mark[64]; uint64_t dead[64]; };
#else
struct gchunk { uint32_t bsize, nblocks, nmarked; uint8_t atomic, cls; uint64_t mark[64]; };
#endif
struct glarge { uint32_t size; uint8_t mark, atomic; uint16_t pad; uint64_t pad2; };   /* 16 bytes: los datos quedan alineados */
struct gment { char *p; uint32_t n; };
struct jgc {
    struct gchunk **chunks; uint32_t nchunks, capchunks;
    struct glarge **large; uint32_t nlarge, caplarge; int large_sorted;
    void *freel[2][GC_NCLS];
    uint64_t committed, large_bytes, free_bytes;
    uint64_t alloc_since, threshold, live_last;
    uintptr_t lo, hi;
    struct gment *ms; uint32_t msp, mcap; int overflow;
    int running, disabled, quar; uint32_t stress, stress_n, collections;

};

static const uint16_t GC_BIG[12] = { 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048 };
static inline int gc_cls(uint32_t n) { if (n <= 16) return 0; if (n <= 256) return (int)((n + 15) / 16) - 1; for (int i = 0; i < 11; i++) if (n <= GC_BIG[i]) return 16 + i; return 27; }
static inline uint32_t gc_cls_size(int c) { return c < 16 ? (uint32_t)(c + 1) * 16u : GC_BIG[c - 16]; }
static inline char *gc_data(struct gchunk *c) { return (char *)c + GC_HDR; }

#if defined(WEB_HOST_TEST) && defined(__SANITIZE_ADDRESS__)
void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
int __asan_address_is_poisoned(void const volatile *addr);
void *__asan_get_current_fake_stack(void);
#define GC_POISON(p, n) __asan_poison_memory_region((p), (n))
#define GC_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#define GC_NOSAN __attribute__((no_sanitize_address))
#else
#define GC_POISON(p, n) ((void)0)
#define GC_UNPOISON(p, n) ((void)0)
#define GC_NOSAN
#endif

static int gc_enabled_cfg(void)
{
#if defined(WEB_HOST_TEST)
    extern char *getenv(const char *); extern char *strstr(const char *, const char *); /* SOMA_299_CI */ const char *o = getenv("SOMA_OFF"); return !(o && strstr(o, "jsgc"));
#elif defined(__x86_64__)
    extern int bootinfo_disabled(const char *name);
    static int v = -1; if (v < 0) v = bootinfo_disabled("jsgc") ? 0 : 1;
    return v;
#else
    return 1;
#endif
}
static struct jgc *gc_get(struct jrt *rt)
{
    if (rt->gc) return (struct jgc *)rt->gc;
    struct jgc *g = (struct jgc *)kmalloc(sizeof *g); if (!g) return 0;
    memset(g, 0, sizeof *g); g->threshold = 8u << 20; g->disabled = !gc_enabled_cfg(); g->lo = ~(uintptr_t)0;
#if defined(WEB_HOST_TEST)
    { extern char *getenv(const char *); const char *s = getenv("HL_GC_STRESS"); if (s) g->stress = (uint32_t)wu_atoi(s); g->quar = getenv("HL_GC_QUAR") != 0; }
#endif
    rt->gc = g; return g;
}
uint32_t js_heap_total(struct jrt *rt)
{
    struct jgc *g = (struct jgc *)rt->gc; uint64_t t = rt->ar.total; if (g) t += g->committed + g->large_bytes;
    return t > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)t;
}

/* ------------------------------ busqueda de bloque ------------------------------ */
static struct gchunk *find_chunk(struct jgc *g, uintptr_t w)
{
    uint32_t lo = 0, hi = g->nchunks;
    while (lo < hi) { uint32_t m = (lo + hi) >> 1; uintptr_t b = (uintptr_t)g->chunks[m]; if (w < b) hi = m; else if (w >= b + GC_CHUNK) lo = m + 1; else return g->chunks[m]; }
    return 0;
}
static struct glarge *find_large(struct jgc *g, uintptr_t w, int edge)
{
    uint32_t lo = 0, hi = g->nlarge;
    while (lo < hi) {
        uint32_t m = (lo + hi) >> 1; uintptr_t b = (uintptr_t)(g->large[m] + 1), e = b + g->large[m]->size;
        if (w < b) hi = m; else if (w > e || (w == e && !edge)) lo = m + 1; else return g->large[m];
    }
    return 0;
}
static void sort_large(struct jgc *g)
{
    if (g->large_sorted) return;
    struct glarge **a = g->large; uint32_t n = g->nlarge;      /* ordenacion Shell: casi ordenado en la practica */
    for (uint32_t gap = n / 2; gap > 0; gap /= 2)
        for (uint32_t i = gap; i < n; i++) { struct glarge *t = a[i]; uint32_t j = i; while (j >= gap && (uintptr_t)a[j - gap] > (uintptr_t)t) { a[j] = a[j - gap]; j -= gap; } a[j] = t; }
    g->large_sorted = 1;
}

/* ------------------------------ marcado ------------------------------ */
static void ms_push(struct jgc *g, char *p, uint32_t n)
{
    if (g->msp == g->mcap) {
        uint32_t nc = g->mcap ? g->mcap * 2 : 16384; struct gment *nm = (struct gment *)kmalloc((size_t)nc * sizeof *nm);
        if (!nm) { g->overflow = 1; return; }                 /* sin memoria para la pila de marcado: se repasa despues */
        if (g->msp) memcpy(nm, g->ms, (size_t)g->msp * sizeof *nm);
        if (g->ms) kfree(g->ms);
        g->ms = nm; g->mcap = nc;
    }
    g->ms[g->msp].p = p; g->ms[g->msp].n = n; g->msp++;
}
static inline void mark_word(struct jgc *g, uintptr_t w, int edge)
{
    if (w < g->lo || w > g->hi) return;
    struct gchunk *c = find_chunk(g, w);
    if (c) {
        char *d = gc_data(c); if ((char *)w < d) return;
        uint32_t idx = (uint32_t)(((char *)w - d) / c->bsize);
        if (edge && idx > 0 && (char *)w == d + (size_t)idx * c->bsize && idx - 1 < c->nblocks) {   /* puntero "uno despues del final" en la pila */
            uint32_t pi = idx - 1; if (!(c->mark[pi >> 6] & (1ull << (pi & 63)))) { c->mark[pi >> 6] |= 1ull << (pi & 63); c->nmarked++; if (!c->atomic) ms_push(g, d + (size_t)pi * c->bsize, c->bsize); }
        }
        if (idx >= c->nblocks) return;
        if (c->mark[idx >> 6] & (1ull << (idx & 63))) return;
        c->mark[idx >> 6] |= 1ull << (idx & 63); c->nmarked++;
        if (!c->atomic) ms_push(g, d + (size_t)idx * c->bsize, c->bsize);
        return;
    }
    struct glarge *L = find_large(g, w, edge);
    if (L && !L->mark) { L->mark = 1; if (!L->atomic) ms_push(g, (char *)(L + 1), L->size); }
}
GC_NOSAN static void scan_range(struct jgc *g, const char *lo, const char *hi, int edge)
{
    uintptr_t a = ((uintptr_t)lo + 7) & ~(uintptr_t)7, e = (uintptr_t)hi & ~(uintptr_t)7;
    for (; a < e; a += 8) {
        uintptr_t w = *(const uintptr_t *)a;
        if (w < g->lo || w > g->hi) {
            continue;
        }
        mark_word(g, w, edge);
    }
}
static void drain(struct jgc *g)
{
    for (;;) {
        uint32_t cnt = 0;
        while (g->msp) { struct gment m = g->ms[--g->msp]; scan_range(g, m.p, m.p + m.n, 0); if ((++cnt & 1023) == 0) wu_tick(); }   /* cede la CPU: marcar cientos de MiB tarda */
        if (!g->overflow) break;
        g->overflow = 0;                                       /* repaso: vuelve a recorrer todo lo marcado */
        for (uint32_t i = 0; i < g->nchunks; i++) {
            struct gchunk *c = g->chunks[i]; if (c->atomic || !c->nmarked) continue;
            for (uint32_t k = 0; k < c->nblocks; k++) if (c->mark[k >> 6] & (1ull << (k & 63))) { scan_range(g, gc_data(c) + (size_t)k * c->bsize, gc_data(c) + (size_t)(k + 1) * c->bsize, 0); while (g->msp) { struct gment m = g->ms[--g->msp]; scan_range(g, m.p, m.p + m.n, 0); } }
        }
        for (uint32_t i = 0; i < g->nlarge; i++) if (g->large[i]->mark && !g->large[i]->atomic) { scan_range(g, (char *)(g->large[i] + 1), (char *)(g->large[i] + 1) + g->large[i]->size, 0); while (g->msp) { struct gment m = g->ms[--g->msp]; scan_range(g, m.p, m.p + m.n, 0); } }
    }
}
/* la usa web_js_co.c para entregar las pilas de las corrutinas */
void js_gc_scan_stack(struct jrt *rt, const char *lo, const char *hi) { struct jgc *g = (struct jgc *)rt->gc; if (g && lo && hi > lo) scan_range(g, lo, hi, 1); }

/* ------------------------------ barrido ------------------------------ */
static void sweep(struct jgc *g)
{
    for (int a = 0; a < 2; a++) for (int c = 0; c < GC_NCLS; c++) g->freel[a][c] = 0;
    g->free_bytes = 0; uint32_t w = 0, empty = 0;
    for (uint32_t i = 0; i < g->nchunks; i++) {
        struct gchunk *c = g->chunks[i];
        if ((i & 63) == 63) wu_tick();
        if (!c->nmarked && ++empty > GC_KEEP_EMPTY) { GC_UNPOISON(c, GC_CHUNK); g->committed -= GC_CHUNK; kfree(c); continue; }
        char *d = gc_data(c); void **head = &g->freel[c->atomic][c->cls]; void *h = *head;
        for (uint32_t k = c->nblocks; k-- > 0; ) {
            if (c->mark[k >> 6] & (1ull << (k & 63))) continue;
            char *b = d + (size_t)k * c->bsize;
#ifdef GC_QUAR
            if (c->dead[k >> 6] & (1ull << (k & 63))) continue;
            if (g->quar && !__asan_address_is_poisoned(b + sizeof(void *))) { c->dead[k >> 6] |= 1ull << (k & 63); GC_POISON(b, c->bsize); continue; }
#endif
            GC_UNPOISON(b, c->bsize);
            *(void **)b = h; h = b; g->free_bytes += c->bsize;
            GC_POISON(b + sizeof(void *), c->bsize - sizeof(void *));
        }
        *head = h; memset(c->mark, 0, sizeof c->mark); c->nmarked = 0;
        g->chunks[w++] = c;
    }
    g->nchunks = w;
    uint32_t lw = 0;
    for (uint32_t i = 0; i < g->nlarge; i++) {
        struct glarge *L = g->large[i];
        if (!L->mark) { g->large_bytes -= L->size + sizeof *L; kfree(L); continue; }
        L->mark = 0; g->large[lw++] = L;
    }
    g->nlarge = lw;
}

int js_co_gc_roots(struct jrt *rt, const char *cur_sp);      /* web_js_co.c */
#if !defined(WEB_HOST_TEST) && (defined(__x86_64__) || defined(__aarch64__))
char *web_stack_hi(const char *sp);                          /* browser.c: tope de la pila conocida que contiene sp (0 si no) */
#endif

__attribute__((noinline)) GC_NOSAN static void gc_mark_roots(struct jrt *rt, struct jgc *g)
{
    volatile uintptr_t anchor = 0; const char *sp = (const char *)&anchor;
    __builtin_unwind_init();                                   /* vuelca a la pila los registros que conserva la llamada */
    if (rt->gc_root_lo) scan_range(g, rt->gc_root_lo, rt->gc_root_hi, 0);
    if (rt->sstack) scan_range(g, rt->sstack, rt->sstack + rt->gc_sstack_size, 0);   /* pila de ambitos */
    if (!js_co_gc_roots(rt, sp)) g->overflow = 2;              /* pila en curso y pilas suspendidas; 2 = no se reconoce la pila: no barrer */
    if (rt->gc_outer_sp) {                                     /* marcos de quien entro en el interprete (otra pila en el nucleo) */
#if !defined(WEB_HOST_TEST) && (defined(__x86_64__) || defined(__aarch64__))
        char *top = web_stack_hi(rt->gc_outer_sp); if (!top || top - rt->gc_outer_sp > (64 << 10)) top = (char *)(((uintptr_t)rt->gc_outer_sp + 2048) & ~(uintptr_t)4095);
        scan_range(g, rt->gc_outer_sp, top, 1);
#endif
    }
}

void js_gc_collect(struct jrt *rt)
{
    struct jgc *g = (struct jgc *)rt->gc;
    if (!g || g->running || g->disabled || !rt->in_task || !rt->gc_main_hi) return;
#if defined(WEB_HOST_TEST) && defined(__SANITIZE_ADDRESS__)
    /* Con detect_stack_use_after_return=1 ASan saca las variables locales de la pila real y un recolector conservador no puede
     * encontrarlas todas: en ese modo de prueba no se recoge. Las pruebas del recolector usan detect_stack_use_after_return=0. */
    if (__asan_get_current_fake_stack()) { g->alloc_since = 0; return; }
#endif
    g->running = 1; g->msp = 0; g->overflow = 0;
    sort_large(g);
    gc_mark_roots(rt, g);
    if (g->overflow == 2) {                                    /* raices incompletas: se deshace el marcado y no se libera nada */
        for (uint32_t i = 0; i < g->nchunks; i++) { memset(g->chunks[i]->mark, 0, sizeof g->chunks[i]->mark); g->chunks[i]->nmarked = 0; }
        for (uint32_t i = 0; i < g->nlarge; i++) g->large[i]->mark = 0;
        g->msp = 0; g->overflow = 0; g->alloc_since = 0; g->running = 0; return;
    }
    drain(g);
    sweep(g);
    g->collections++; g->alloc_since = 0;
    g->live_last = g->committed - g->free_bytes + g->large_bytes;
    g->threshold = g->live_last; if (g->threshold < (8u << 20)) g->threshold = 8u << 20;   /* la siguiente recogida, cuando se haya reservado otro tanto de lo vivo */
    if (g->mcap > (1u << 20)) { kfree(g->ms); g->ms = 0; g->mcap = 0; }
    g->running = 0;
}
static int gc_should(struct jrt *rt, struct jgc *g, uint32_t want)
{
    if (g->running || g->disabled || !rt->in_task || !rt->gc_main_hi) return 0;
    if (g->alloc_since >= g->threshold) return 1;
    uint64_t tot = g->committed + g->large_bytes + rt->ar.total + want;      /* cerca del tope: recoge antes de pedir mas al nucleo */
    uint64_t min = g->live_last / 8; if (min < (2u << 20)) min = 2u << 20;
    return tot + (tot >> 3) > rt->heap_cap && g->alloc_since >= min;
}

static int add_chunk(struct jgc *g, int cls, int atomic)
{
    if (g->nchunks == g->capchunks) {
        uint32_t nc = g->capchunks ? g->capchunks * 2 : 256; struct gchunk **na = (struct gchunk **)kmalloc((size_t)nc * sizeof *na); if (!na) return 0;
        if (g->nchunks) memcpy(na, g->chunks, (size_t)g->nchunks * sizeof *na);
        if (g->chunks) kfree(g->chunks);
        g->chunks = na; g->capchunks = nc;
    }
    struct gchunk *c = (struct gchunk *)kmalloc(GC_CHUNK); if (!c) return 0;
    memset(c, 0, GC_HDR); c->bsize = gc_cls_size(cls); c->cls = (uint8_t)cls; c->atomic = (uint8_t)atomic; c->nblocks = (GC_CHUNK - GC_HDR) / c->bsize;
    uint32_t i = g->nchunks; while (i > 0 && (uintptr_t)g->chunks[i - 1] > (uintptr_t)c) { g->chunks[i] = g->chunks[i - 1]; i--; }
    g->chunks[i] = c; g->nchunks++; g->committed += GC_CHUNK;
    if ((uintptr_t)c < g->lo) g->lo = (uintptr_t)c;
    if ((uintptr_t)c + GC_CHUNK > g->hi) g->hi = (uintptr_t)c + GC_CHUNK;
    char *d = gc_data(c); void *h = g->freel[atomic][cls];
    for (uint32_t k = c->nblocks; k-- > 0; ) { char *b = d + (size_t)k * c->bsize; *(void **)b = h; h = b; GC_POISON(b + sizeof(void *), c->bsize - sizeof(void *)); }
    g->freel[atomic][cls] = h; g->free_bytes += (uint64_t)c->nblocks * c->bsize;
    return 1;
}

/* Reserva n bytes puestos a cero (0 = sin memoria en el nucleo). atomic: el bloque no contiene punteros. */
void *js_gc_alloc(struct jrt *rt, uint32_t n, int atomic)
{
    struct jgc *g = gc_get(rt); if (!g) return 0;
    atomic = atomic ? 1 : 0;
    if (g->stress && rt->in_task && !g->running && ++g->stress_n >= g->stress) { g->stress_n = 0; js_gc_collect(rt); }
    if (!n) n = 1;
    if (n > GC_MAXSMALL) {
        if (gc_should(rt, g, n)) js_gc_collect(rt);
        if (g->nlarge == g->caplarge) {
            uint32_t nc = g->caplarge ? g->caplarge * 2 : 256; struct glarge **na = (struct glarge **)kmalloc((size_t)nc * sizeof *na); if (!na) return 0;
            if (g->nlarge) memcpy(na, g->large, (size_t)g->nlarge * sizeof *na);
            if (g->large) kfree(g->large);
            g->large = na; g->caplarge = nc;
        }
        uint32_t sz = (n + 15u) & ~15u;
        struct glarge *L = (struct glarge *)kmalloc(sizeof *L + sz);
        if (!L) { js_gc_collect(rt); L = (struct glarge *)kmalloc(sizeof *L + sz); if (!L) return 0; }
        L->size = sz; L->mark = 0; L->atomic = (uint8_t)atomic; L->pad = 0; L->pad2 = 0;
        memset(L + 1, 0, sz);
        if (g->nlarge && (uintptr_t)g->large[g->nlarge - 1] > (uintptr_t)L) g->large_sorted = 0;
        g->large[g->nlarge++] = L; g->large_bytes += sz + sizeof *L; g->alloc_since += sz;
        if ((uintptr_t)L < g->lo) g->lo = (uintptr_t)L;
        if ((uintptr_t)(L + 1) + sz > g->hi) g->hi = (uintptr_t)(L + 1) + sz;
        return L + 1;
    }
    int cls = gc_cls(n); void **head = &g->freel[atomic][cls];
    if (!*head) {
        if (gc_should(rt, g, GC_CHUNK)) js_gc_collect(rt);
        if (!*head && !add_chunk(g, cls, atomic)) { js_gc_collect(rt); if (!*head && !add_chunk(g, cls, atomic)) return 0; }
    }
    char *b = (char *)*head; uint32_t bs = gc_cls_size(cls);
    GC_UNPOISON(b, bs);
    *head = *(void **)b; memset(b, 0, bs);
    g->free_bytes -= bs; g->alloc_since += bs;
    return b;
}
void js_gc_free_all(struct jrt *rt)
{
    struct jgc *g = (struct jgc *)rt->gc; if (!g) return;
    for (uint32_t i = 0; i < g->nchunks; i++) { GC_UNPOISON(g->chunks[i], GC_CHUNK); kfree(g->chunks[i]); }
    for (uint32_t i = 0; i < g->nlarge; i++) kfree(g->large[i]);
    if (g->chunks) kfree(g->chunks);
    if (g->large) kfree(g->large);
    if (g->ms) kfree(g->ms);
    kfree(g); rt->gc = 0;
}
uint32_t js_gc_count(struct jrt *rt) { struct jgc *g = (struct jgc *)rt->gc; return g ? g->collections : 0; }
uint32_t js_gc_live(struct jrt *rt) { struct jgc *g = (struct jgc *)rt->gc; if (!g) return 0; uint64_t l = g->committed - g->free_bytes + g->large_bytes; return l > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)l; }
