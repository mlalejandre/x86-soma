/* PARCHE 138: servicios aislados en ring 3. Ver sandbox.h y src/core/sbx_abi.h. */
#include "sandbox.h"
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "mem.h"
#include "thread.h"
#include "console.h"

uint64_t timer_get_uptime_ms(void);

static struct sbx_stats st;
uint32_t sbx_reserve_override;   /* solo pruebas: si no es 0, sustituye a SBX_RESERVE_FRAMES */
static int validate_reply(struct sbx_call *c, struct uregion *out, struct uregion *heap);
void sbx_get_stats(struct sbx_stats *s) { *s = st; }

static void region_free(struct uregion *r)
{
    if (r->frames) {
        for (uint32_t k = 0; k < r->npages; ++k) if (r->frames[k]) pmm_free_frame((uintptr_t)r->frames[k]);
        kfree(r->frames);
    }
    r->frames = 0; r->npages = 0; r->used = 0;
}

/* Reserva el array de marcos y, si eager, los marcos (a cero). */
static int region_init(struct uregion *r, uint64_t va, uint32_t npages, uint64_t flags, int demand)
{
    r->va = va; r->npages = npages; r->used = 0; r->flags = flags; r->demand = demand;
    r->frames = (uint64_t *)kmalloc((size_t)npages * sizeof(uint64_t));
    if (!r->frames) { r->npages = 0; return -1; }
    for (uint32_t k = 0; k < npages; ++k) r->frames[k] = 0;
    if (demand) return 0;
    for (uint32_t k = 0; k < npages; ++k) {
        uintptr_t f = pmm_alloc_frame();
        if (!f) return -1;                                  /* el llamante libera con sbx_release */
        uint8_t *p = (uint8_t *)f; for (int b = 0; b < 4096; ++b) p[b] = 0;
        r->frames[k] = f; r->used++;
    }
    return 0;
}

/* Copia a/desde una region por paginas (los marcos estan mapeados 1:1 en el kernel). off+n ya validados. */
static void region_write(struct uregion *r, uint64_t off, const void *src, uint64_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        uint8_t *d = (uint8_t *)(uintptr_t)r->frames[k] + o;
        for (uint64_t i = 0; i < c; ++i) d[i] = s[i];
        s += c; off += c; n -= c;
    }
}
static int region_read(const struct uregion *r, uint64_t off, void *dst, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        uint64_t k = off >> 12, o = off & 0xFFF, c = 4096 - o; if (c > n) c = n;
        if (k >= r->npages || !r->frames[k]) return -1;
        const uint8_t *s = (const uint8_t *)(uintptr_t)r->frames[k] + o;
        for (uint64_t i = 0; i < c; ++i) d[i] = s[i];
        d += c; off += c; n -= c;
    }
    return 0;
}

void sbx_prepare(struct sbx_call *c, uint32_t op, const void *data, uint32_t len, uint32_t heap_bytes)
{
    uint8_t *z = (uint8_t *)c; for (size_t i = 0; i < sizeof *c; ++i) z[i] = 0;
    c->in.magic = SBX_IN_MAGIC; c->in.op = op; c->in.len = len;
    c->in.heap_bytes = (heap_bytes + 4095u) & ~4095u;
    c->data = data; c->len = len;
}

static void svc_done(struct sbx_svc *s, int ok);
void sbx_release(struct sbx_call *c)
{
    if (c->svc) { struct sbx_svc *s = c->svc; c->svc = 0; c->heap = 0; svc_done(s, c->exit_code == 0 && c->out.magic == SBX_OUT_MAGIC); return; }
    region_free(&c->r_in); region_free(&c->r_out); region_free(&c->r_heap);
    c->heap = 0;
}

/* validacion de la respuesta: nada de lo que escribio el proceso se cree sin comprobar */
static int validate_reply(struct sbx_call *c, struct uregion *out, struct uregion *heap)
{
    (void)region_read(out, 0, &c->out, sizeof c->out);
    c->heap = heap;
    if (c->out.magic != SBX_OUT_MAGIC || c->out.inline_len > SBX_OUT_INLINE) { st.bad_reply++; return SBX_E_REPLY; }
    if (c->out.data_len) {
        uint64_t a = c->out.data_va, n = c->out.data_len, lo = heap->va, hi = lo + (uint64_t)heap->npages * 4096;
        if (a < lo || a >= hi || n > hi - a) { st.bad_reply++; return SBX_E_REPLY; }
        for (uint64_t k = (a - lo) >> 12; k <= (a - lo + n - 1) >> 12; ++k)
            if (!heap->frames[k]) { st.bad_reply++; return SBX_E_REPLY; }   /* paginas nunca escritas: respuesta falsa */
    } else if (c->out.data_va) { st.bad_reply++; return SBX_E_REPLY; }
    st.ok++;
    return 0;
}

int sbx_run(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms) { return sbx_run_caps(c, elf, elf_len, max_ms, 0); }
int sbx_run_caps(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms, uint32_t extra_sysmask)
{
    return sbx_run_caps_cancel(c, elf, elf_len, max_ms, extra_sysmask, 0);
}
/* SOMA_317_DESCARGAS_AISLADAS: lo mismo, y si *cancel se pone a 1 el proceso se para (la llamada devuelve un error) */
int sbx_run_caps_cancel(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms, uint32_t extra_sysmask, const volatile int *cancel)
{
    st.runs++;
    if (!c || !elf || (c->len && !c->data) || c->len > SBX_IN_MAX || c->in.heap_bytes == 0 || c->in.heap_bytes > SBX_HEAP_MAX) return SBX_E_ARGS;
    const uint64_t NXR = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_NX, NXW = NXR | VMM_FLAG_WRITABLE;
    uint32_t in_pages = (uint32_t)((64u + c->len + 4095u) / 4096u);
    if (region_init(&c->r_in, SBX_IN_VA, in_pages, NXR, 0) || region_init(&c->r_out, SBX_OUT_VA, 1, NXW, 0) ||
        region_init(&c->r_heap, SBX_HEAP_VA, c->in.heap_bytes / 4096u, NXW, 1)) { sbx_release(c); return SBX_E_NOMEM; }
    region_write(&c->r_in, 0, &c->in, sizeof c->in);
    if (c->len) region_write(&c->r_in, 64, c->data, c->len);

    struct uexec_spec sp = {0}; sp.may_thr = 0;   /* SOMA_329; SOMA_352: entera a cero (max_sockets quedaba con basura de la pila) */
    sp.sysmask = (1u << SYS_EXIT) | extra_sysmask;
    sp.regions[0] = &c->r_in; sp.regions[1] = &c->r_out; sp.regions[2] = &c->r_heap; sp.regions[3] = 0;
    sp.nregions = 3;
    sp.reserve_frames = sbx_reserve_override ? sbx_reserve_override : SBX_RESERVE_FRAMES;

    uint64_t t0 = timer_get_uptime_ms();
    int idx = -2, err = 0;
    for (int tries = 0; tries < 400; ++tries) {             /* ranuras ocupadas: esperar hasta ~2 s */
        idx = usermode_exec_ex("sbx", elf, elf_len, max_ms, &sp, &err);
        if (idx != -2) break;
        thread_sleep(5);
    }
    if (idx < 0) { st.spawn_fail++; return SBX_E_SPAWN; }
    c->exit_code = usermode_wait_cancel(idx, cancel);
    uint64_t dt = timer_get_uptime_ms() - t0;
    st.last_ms = dt; st.total_ms += dt; if (dt > st.max_ms) st.max_ms = dt;

    if (c->exit_code == USER_EXIT_NOMEM) { st.oom++; return SBX_E_HEAPOOM; }
    if (c->exit_code != 0) { st.died++; return SBX_E_DIED; }
    return validate_reply(c, &c->r_out, &c->r_heap);
}

/* SOMA_326_TUNEL_TLS: lanza el proceso SIN ESPERAR, con una region compartida mas (sh_pages paginas a cero en sh_va, lectura y escritura
 * para el proceso; el nucleo la ve 1:1 en sh->frames). Devuelve el indice del proceso o < 0. Se termina con sbx_termina. */
int sbx_lanza(struct sbx_call *c, const void *elf, size_t elf_len, uint32_t max_ms, uint32_t extra_sysmask, struct uregion *sh, uint64_t sh_va, uint32_t sh_pages)
{
    st.runs++;
    sh->frames = 0; sh->npages = 0;
    if (!c || !elf || !sh || (c->len && !c->data) || c->len > SBX_IN_MAX || c->in.heap_bytes == 0 || c->in.heap_bytes > SBX_HEAP_MAX) return SBX_E_ARGS;
    const uint64_t NXR = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_NX, NXW = NXR | VMM_FLAG_WRITABLE;
    uint32_t in_pages = (uint32_t)((64u + c->len + 4095u) / 4096u);
    if (region_init(&c->r_in, SBX_IN_VA, in_pages, NXR, 0) || region_init(&c->r_out, SBX_OUT_VA, 1, NXW, 0) ||
        region_init(&c->r_heap, SBX_HEAP_VA, c->in.heap_bytes / 4096u, NXW, 1) || region_init(sh, sh_va, sh_pages, NXW, 0)) {
        sbx_release(c); region_free(sh); return SBX_E_NOMEM;
    }
    region_write(&c->r_in, 0, &c->in, sizeof c->in);
    if (c->len) region_write(&c->r_in, 64, c->data, c->len);
    struct uexec_spec sp = {0}; sp.may_thr = 0;   /* SOMA_329; SOMA_352: entera a cero (max_sockets quedaba con basura de la pila) */
    sp.sysmask = (1u << SYS_EXIT) | extra_sysmask;
    sp.regions[0] = &c->r_in; sp.regions[1] = &c->r_out; sp.regions[2] = &c->r_heap; sp.regions[3] = sh;
    sp.nregions = 4;
    sp.reserve_frames = sbx_reserve_override ? sbx_reserve_override : SBX_RESERVE_FRAMES;
    int idx = -2, err = 0;
    for (int tries = 0; tries < 400; ++tries) {
        idx = usermode_exec_ex("tlstun", elf, elf_len, max_ms, &sp, &err);
        if (idx != -2) break;
        thread_sleep(5);
    }
    if (idx < 0) { st.spawn_fail++; sbx_release(c); region_free(sh); return SBX_E_SPAWN; }
    return idx;
}
/* para el proceso (si sigue vivo), lo recoge y libera sus regiones */
void sbx_termina(struct sbx_call *c, int idx, struct uregion *sh)
{
    if (idx >= 0) { if (usermode_proc_alive(idx)) usermode_stop(idx); c->exit_code = usermode_wait(idx); }
    sbx_release(c);
    region_free(sh);
}

int sbx_copy_data(const struct sbx_call *c, void *dst, uint64_t n)
{
    const struct uregion *h = c->heap ? c->heap : &c->r_heap;
    if (n > c->out.data_len || !h->frames) return -1;
    return region_read(h, c->out.data_va - h->va, dst, n);
}

/* ======================================================================== PARCHE 139: servicios persistentes */
struct sbx_svc {
    int      id, idx;                   /* idx de proceso (-1 = no hay) */
    volatile int ready, posted, inflight, replied, stop, busy;
    uint32_t caller_tid;                /* hilo del kernel que espera la respuesta (se le despierta al responder) */
    int64_t  result;
    uint32_t served, call_ms;
    struct uregion r_in, r_out, r_heap;
};
static struct sbx_svc svcs[SBX_SVC_N][SBX_SVC_POOL];
static int svcs_init;

static void svc_elf(int id, const uint8_t **e, size_t *n)
{
    if (id == SBX_SVC_IMG) { *e = _binary_imgdec_elf_start; *n = (size_t)(_binary_imgdec_elf_end - _binary_imgdec_elf_start); }
    else if (id == SBX_SVC_ZLIB) { *e = _binary_zdec_elf_start; *n = (size_t)(_binary_zdec_elf_end - _binary_zdec_elf_start); }
    else if (id == SBX_SVC_PDF) { *e = _binary_pdfsvc_elf_start; *n = (size_t)(_binary_pdfsvc_elf_end - _binary_pdfsvc_elf_start); }
    else if (id == SBX_SVC_NET) { *e = _binary_netsvc_elf_start; *n = (size_t)(_binary_netsvc_elf_end - _binary_netsvc_elf_start); }
    else if (id == SBX_SVC_MIME) { *e = _binary_mimesvc_elf_start; *n = (size_t)(_binary_mimesvc_elf_end - _binary_mimesvc_elf_start); }   /* SOMA_311 */
    else if (id == SBX_SVC_LEE) { *e = _binary_leesvc_elf_start; *n = (size_t)(_binary_leesvc_elf_end - _binary_leesvc_elf_start); }      /* SOMA_312 */
    else { *e = _binary_sbxevil_elf_start; *n = (size_t)(_binary_sbxevil_elf_end - _binary_sbxevil_elf_start); }
}

static void svc_teardown(struct sbx_svc *s)      /* el proceso ya termino (o nunca arranco) */
{
    region_free(&s->r_in); region_free(&s->r_out); region_free(&s->r_heap);
    s->idx = -1; s->ready = s->posted = s->inflight = s->replied = s->stop = 0; s->served = 0;
}

/* Termina el proceso (si vive) y libera todo. */
static void svc_kill(struct sbx_svc *s)
{
    if (s->idx >= 0) {
        s->stop = 1;
        thread_wake(usermode_proc_tid(s->idx));
        (void)usermode_wait(s->idx);              /* sale de SYS_SVC_NEXT con 1 -> exit; o ya habia muerto */
    }
    svc_teardown(s);
}

static int svc_start(struct sbx_svc *s)
{
    const uint8_t *elf; size_t n; svc_elf(s->id, &elf, &n);
    const uint64_t NXR = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_NX, NXW = NXR | VMM_FLAG_WRITABLE;
    svc_teardown(s);
    /* entrada: el KERNEL reserva sus paginas al escribirla (demand=0: un acceso del proceso fuera de lo escrito es un fallo) */
    if (region_init(&s->r_in, SBX_IN_VA, SBX_IN_MAX / 4096u, NXR, 1) || region_init(&s->r_out, SBX_OUT_VA, 1, NXW, 0) ||
        region_init(&s->r_heap, SBX_HEAP_VA, SBX_HEAP_MAX / 4096u, NXW, 1)) { svc_teardown(s); return SBX_E_NOMEM; }
    s->r_in.demand = 0;
    {   /* la primera pagina de entrada existe desde el principio (a cero): el proceso lee ahi in->magic == 0 -> modo servicio */
        uintptr_t f = pmm_alloc_frame();
        if (!f) { svc_teardown(s); return SBX_E_NOMEM; }
        uint8_t *p = (uint8_t *)f; for (int b = 0; b < 4096; ++b) p[b] = 0;
        s->r_in.frames[0] = f; s->r_in.used = 1;
    }
    struct uexec_spec sp = {0}; sp.may_thr = 0;   /* SOMA_329; SOMA_352: entera a cero (max_sockets quedaba con basura de la pila) */
    sp.sysmask = (1u << SYS_EXIT) | (1u << SYS_SVC_NEXT);
    if (s->id == SBX_SVC_NET) sp.sysmask |= USER_SYS_NET_MASK | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS);   /* PARCHE 142: solo netsvc tiene red */
    sp.regions[0] = &s->r_in; sp.regions[1] = &s->r_out; sp.regions[2] = &s->r_heap; sp.regions[3] = 0;
    sp.nregions = 3;
    sp.reserve_frames = sbx_reserve_override ? sbx_reserve_override : SBX_RESERVE_FRAMES;
    int idx = -2, err = 0;
    for (int tries = 0; tries < 400; ++tries) {
        idx = usermode_exec_ex("svc", elf, n, 2000, &sp, &err);
        if (idx != -2) break;
        thread_sleep(5);
    }
    if (idx < 0) { st.spawn_fail++; svc_teardown(s); return SBX_E_SPAWN; }
    s->idx = idx;
    uint64_t t0 = timer_get_uptime_ms();
    s->caller_tid = thread_current() ? thread_current()->tid : 0;
    while (!s->ready && usermode_proc_alive(idx) && timer_get_uptime_ms() - t0 < 2000) thread_sleep(5);
    s->caller_tid = 0;
    if (!s->ready) { svc_kill(s); st.spawn_fail++; return SBX_E_SPAWN; }
    st.svc_starts++;
    return 0;
}

/* SYS_SVC_NEXT: entrega el resultado de la peticion en curso (si la habia) y espera la siguiente. */
int64_t sbx_svc_next(int proc_idx, int64_t result)
{
    struct sbx_svc *s = 0;
    for (int i = 0; i < SBX_SVC_N && !s; ++i) for (int j = 0; j < SBX_SVC_POOL; ++j) if (svcs_init && svcs[i][j].idx == proc_idx) { s = &svcs[i][j]; break; }
    if (!s) return 1;
    if (s->inflight) { s->result = result; s->inflight = 0; s->replied = 1; }
    else s->ready = 1;
    if (s->caller_tid) thread_wake(s->caller_tid);          /* respuesta lista: el llamante sigue ya */
    usermode_set_deadline_cur(~0ULL >> 2);                  /* esperando no tiene plazo */
    while (!s->posted && !s->stop) thread_sleep(100);       /* sin gastar CPU en reposo: svc_call / svc_kill lo despiertan */
    if (s->stop) return 1;
    s->posted = 0; s->inflight = 1;
    usermode_set_deadline_cur(timer_get_uptime_ms() + s->call_ms);
    return 0;
}

/* Escribe en la entrada del servicio reservando y mapeando las paginas que falten (solo lectura para el proceso). */
static int svc_write_in(struct sbx_svc *s, uint64_t off, const void *src, uint64_t n)
{
    uint64_t as = usermode_proc_as(s->idx);
    if (!as) return -1;
    for (uint64_t k = off >> 12; n && k <= (off + n - 1) >> 12; ++k) {
        if (k >= s->r_in.npages) return -1;
        if (s->r_in.frames[k]) continue;
        uintptr_t f = pmm_alloc_frame();
        if (!f) return -1;
        if (vmm_as_map(as, s->r_in.va + k * 4096, f, s->r_in.flags) != 0) { pmm_free_frame(f); return -1; }
        s->r_in.frames[k] = f; s->r_in.used++;
    }
    region_write(&s->r_in, off, src, n);
    return 0;
}

static void svc_trim(struct uregion *r, uint64_t as, uint32_t keep, int zero)
{
    for (uint32_t k = r->npages; k-- > 0 && r->used > 0;) {
        if (!r->frames[k]) continue;
        if (r->used > keep) {
            (void)vmm_as_unmap(as, r->va + (uint64_t)k * 4096);
            pmm_free_frame((uintptr_t)r->frames[k]); r->frames[k] = 0; r->used--;
        } else if (zero) {
            uint8_t *p = (uint8_t *)(uintptr_t)r->frames[k]; for (int b = 0; b < 4096; ++b) p[b] = 0;
        }
    }
}

/* Fin de una llamada: limpia el estado que vera la siguiente peticion o recicla el proceso. */
static void svc_done(struct sbx_svc *s, int ok)
{
    if (s->idx >= 0 && (!ok || ++s->served >= SBX_SVC_RECYCLE || !usermode_proc_alive(s->idx))) { svc_kill(s); st.svc_recycled++; }
    else if (s->idx >= 0) {
        uint64_t as = usermode_proc_as(s->idx);
        svc_trim(&s->r_heap, as, SBX_SVC_KEEP_HEAP_PAGES, 1);    /* lo que queda, a cero: la siguiente peticion no ve nada de esta */
        svc_trim(&s->r_in, as, 256, 1);
    }
    s->busy = 0;
}

int sbx_svc_call(int svc_id, struct sbx_call *c, uint32_t max_ms)
{
    st.runs++; st.svc_calls++;
    c->svc = 0; c->heap = 0; c->exit_code = 0;
    if (svc_id < 0 || svc_id >= SBX_SVC_N || (c->len && !c->data) || c->len > SBX_IN_MAX) return SBX_E_ARGS;
    if (!svcs_init) { for (int i = 0; i < SBX_SVC_N; ++i) for (int j = 0; j < SBX_SVC_POOL; ++j) { svcs[i][j].id = i; svcs[i][j].idx = -1; } svcs_init = 1; }
    struct sbx_svc *s = 0;
    uint64_t t0 = timer_get_uptime_ms();
    while (!s) {                                           /* kernel cooperativo: comprobar y marcar es atomico */
        for (int j = 0; j < SBX_SVC_POOL; ++j) if (!svcs[svc_id][j].busy) { s = &svcs[svc_id][j]; s->busy = 1; break; }
        if (!s) { if (timer_get_uptime_ms() - t0 > 10000) { st.spawn_fail++; return SBX_E_SPAWN; } thread_sleep(1); }
    }
    c->svc = s;
    if (s->idx < 0 || !usermode_proc_alive(s->idx)) {
        if (s->idx >= 0) svc_kill(s);
        int r = svc_start(s);
        if (r) { c->svc = 0; s->busy = 0; return r; }
    }
    c->in.magic = SBX_IN_MAGIC; c->in.len = c->len; c->in.heap_bytes = SBX_HEAP_MAX;
    if (svc_write_in(s, 0, &c->in, sizeof c->in) || (c->len && svc_write_in(s, 64, c->data, c->len))) { c->svc = 0; svc_kill(s); s->busy = 0; return SBX_E_NOMEM; }
    { uint8_t *o = (uint8_t *)(uintptr_t)s->r_out.frames[0]; for (int b = 0; b < 4096; ++b) o[b] = 0; }   /* sin respuesta vieja */
    s->call_ms = max_ms; s->replied = 0; s->posted = 1;
    s->caller_tid = thread_current() ? thread_current()->tid : 0;
    thread_wake(usermode_proc_tid(s->idx));
    uint64_t t1 = timer_get_uptime_ms();
    while (!s->replied && usermode_proc_alive(s->idx)) thread_sleep(5);   /* el servicio nos despierta al responder */
    s->caller_tid = 0;
    uint64_t dt = timer_get_uptime_ms() - t1;
    st.last_ms = dt; st.total_ms += dt; if (dt > st.max_ms) st.max_ms = dt;
    if (!s->replied) {                                     /* murio durante la peticion (fallo, plazo, syscall prohibida, memoria) */
        c->exit_code = usermode_wait(s->idx);
        s->idx = -1;
        svc_teardown(s);
        c->svc = 0; s->busy = 0; st.svc_recycled++;
        if (c->exit_code == USER_EXIT_NOMEM) { st.oom++; return SBX_E_HEAPOOM; }
        st.died++; return SBX_E_DIED;
    }
    c->exit_code = s->result;
    if (s->result != 0) { st.died++; return SBX_E_DIED; }      /* sbx_release lo recicla */
    return validate_reply(c, &s->r_out, &s->r_heap);
}

void sbx_svc_stop_all(void)
{
    if (!svcs_init) return;
    for (int i = 0; i < SBX_SVC_N; ++i) for (int j = 0; j < SBX_SVC_POOL; ++j) {
        struct sbx_svc *s = &svcs[i][j];
        while (s->busy) thread_sleep(1);
        s->busy = 1; svc_kill(s); s->busy = 0;
    }
}

int sbx_svc_running(int svc_id)
{
    int n = 0;
    if (!svcs_init || svc_id < 0 || svc_id >= SBX_SVC_N) return 0;
    for (int j = 0; j < SBX_SVC_POOL; ++j) if (svcs[svc_id][j].idx >= 0 && usermode_proc_alive(svcs[svc_id][j].idx)) n++;
    return n;
}
