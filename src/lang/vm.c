#include "vm.h"
#include "console.h"
#include "mem.h"
#include "fs.h"
#include "pmm.h"
#include "agent.h"
#include "arch.h"
#include "thread.h"
#include "sysinfo.h"
#include "fb.h"
#include "gui.h" /*PL12_GUI_INC*/
#include "rtc.h"

extern uint64_t timer_get_uptime_ms(void);

/* SOMA_264_LANG: vm_init deja la VM vacia sin mirar lo que hubiera en la memoria (kmalloc no la pone a cero: antes se leia un
 * table_pool basura). Si la VM ya se uso, hay que llamar antes a vm_release, que devuelve su memoria. */
void vm_init(struct soma_vm *vm)
{
    vm->sp = 0;
    vm->ip = 0;
    vm->halted = 0;
    vm->code = 0;
    vm->code_len = 0;
    vm->lp = 0;
    vm->fp = 0;
    vm->fsp = 0;
    vm->array_pool = 0; vm->array_pool_cap = 0; vm->array_pool_count = 0;
    vm->table_pool = 0; vm->table_pool_cap = 0; vm->table_pool_count = 0;
    vm->mem_used = 0; vm->mem_budget = VM_MEM_BUDGET; vm->mem_aviso = 0; vm->gc_tras = (size_t)-1;
    for (int si = 0; si < VM_STR_SLOTS; ++si) { vm->str_ptr[si] = 0; vm->str_used[si] = 0; vm->str_mark[si] = 0; vm->str_len[si] = 0; }
    for (int ri = 0; ri < 8; ++ri) { vm->tmp_roots[ri].type = VAL_NIL; vm->tmp_roots[ri].i = 0; vm->tmp_roots[ri].arr_id = 0; vm->tmp_roots[ri].tab_id = 0; vm->tmp_roots[ri].s[0] = '\0'; } /*PL4_INIT*/
    vm->win.active = 0;
    vm->win.x = 0; vm->win.y = 0; vm->win.w = 0; vm->win.h = 0; vm->win.title[0] = '\0'; vm->win.bg = 0x0A0D14;
    for (int i = 0; i < VM_STACK_CAPACITY; ++i) { vm->stack[i].type = VAL_NIL; vm->stack[i].i = 0; vm->stack[i].arr_id = 0; vm->stack[i].tab_id = 0; vm->stack[i].s[0] = '\0'; } /*PL1_INITZ*/
    for (int i = 0; i < VM_VARS_CAPACITY; ++i) { vm->vars[i].type = VAL_NIL; vm->vars[i].i = 0; vm->vars[i].arr_id = 0; vm->vars[i].tab_id = 0; vm->vars[i].s[0] = '\0'; }
    for (int i = 0; i < VM_LOCALS_CAPACITY; ++i) { vm->locals[i].type = VAL_NIL; vm->locals[i].i = 0; vm->locals[i].arr_id = 0; vm->locals[i].tab_id = 0; vm->locals[i].s[0] = '\0'; }
}

static int push_val(struct soma_vm *vm, struct vm_val val)
{
    if (vm->sp >= VM_STACK_CAPACITY) {
        kprint("[VM ERROR] Desbordamiento de pila\n");
        return 0;
    }
    vm->stack[vm->sp++] = val;
    return 1;
}

static struct vm_val pop_val(struct soma_vm *vm)
{
    if (vm->sp <= 0) {
                kprint("[VM ERROR] Pila vacia\n");
        struct vm_val nil = { VAL_NIL, 0, {0}, 0, 0 }; /*PL4_POPNIL*/
        return nil;
    }
    return vm->stack[--vm->sp];
}

static int64_t read_i64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint64_t)p[i]) << (i * 8);
    return (int64_t)v;
}

static int32_t read_i32(const uint8_t *p)
{
    uint32_t v = ((uint32_t)p[0]) | (((uint32_t)p[1]) << 8) |
                 (((uint32_t)p[2]) << 16) | (((uint32_t)p[3]) << 24);
    return (int32_t)v;
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int vm_truthy(const struct vm_val *v)
{
    if (v->type == VAL_NIL) return 0; /*PL1_TRUTHY*/
    if (v->type == VAL_STR) return (v->s[0] != '\0');
    if (v->type == VAL_ARRAY || v->type == VAL_TABLE) return 1; /*PL4_TRUTHY*/
    return (v->i != 0);
}

/*PL3_HELPERS*/
static struct soma_vm *g_vm_cur = 0;
static void vm_gc_collect(struct soma_vm *vm);

/* ---------------------------------------------------------------- SOMA_264_LANG: memoria de la VM con presupuesto */
static void *vm_mem_get(struct soma_vm *vm, size_t n)
{
    if (!n || vm->mem_used + n > vm->mem_budget) return 0;
    void *p = kmalloc(n);
    if (p) vm->mem_used += n;
    return p;
}
static void vm_mem_put(struct soma_vm *vm, void *p, size_t n)
{
    if (!p) return;
    kfree(p);
    vm->mem_used = vm->mem_used >= n ? vm->mem_used - n : 0;
}
static void vm_sin_sitio(struct soma_vm *vm, const char *que)
{
    if (vm->mem_aviso) return;
    vm->mem_aviso = 1;
    kprint("[VM] Sin sitio para "); kprint(que); kprint(" (memoria del programa: ");
    kprint_dec((uint32_t)(vm->mem_used >> 20)); kprint(" de "); kprint_dec((uint32_t)(vm->mem_budget >> 20)); kprint(" MB)\n");
}

static int vm_is_long(const struct soma_vm *vm, const struct vm_val *v)
{
    return vm && v->type == VAL_STR && v->i > 0 &&
           v->i <= VM_STR_SLOTS && vm->str_used[v->i - 1] && vm->str_ptr[v->i - 1];
}

static const char *vm_sdata(const struct soma_vm *vm, const struct vm_val *v)
{
    if (vm_is_long(vm, v)) return vm->str_ptr[v->i - 1];
    return v->s;
}

static uint32_t vm_slen(const struct soma_vm *vm, const struct vm_val *v)
{
    if (vm_is_long(vm, v)) return vm->str_len[v->i - 1];
    uint32_t n = 0;
    while (v->s[n]) n++;
    return n;
}

static void vm_gc_collect(struct soma_vm *vm);
/* recogida por falta de sitio: solo si se ha pedido memoria desde la ultima (si no, no liberaria nada y, repetida en cada
 * operacion de un programa que ya lleno el presupuesto, lo volveria cuadratico) */
static int vm_gc_por_sitio(struct soma_vm *vm)
{
    if (vm->gc_tras != (size_t)-1 && vm->mem_used <= vm->gc_tras) return 0;
    vm_gc_collect(vm);
    vm->gc_tras = vm->mem_used;
    return 1;
}

/* una ranura para una cadena larga de 'len' caracteres (la memoria la pone vm_str_make2) */
static int vm_str_slot_alloc(struct soma_vm *vm)
{
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < VM_STR_SLOTS; ++i) {
            if (!vm->str_used[i]) {
                vm->str_used[i] = 1;
                vm->str_mark[i] = 0;
                vm->str_len[i] = 0;
                vm->str_ptr[i] = 0;
                return i;
            }
        }
        if (pass == 0) vm_gc_collect(vm);
    }
    return -1;
}

/* Crea out = d1 + d2. Las fuentes d1/d2 pueden vivir en el pool: el llamador
 * debe tenerlas protegidas en vm->tmp_roots (o en la pila) durante la llamada. */
static void vm_str_make2(struct soma_vm *vm, struct vm_val *out,
                         const char *d1, uint32_t l1, const char *d2, uint32_t l2)
{
    uint32_t len = l1 + l2;
    uint32_t pre = 0;
    out->type = VAL_STR;
    out->i = 0;
    out->arr_id = 0;
    for (uint32_t k = 0; k < l1 && pre < sizeof(out->s) - 1; ++k) out->s[pre++] = d1[k];
    for (uint32_t k = 0; k < l2 && pre < sizeof(out->s) - 1; ++k) out->s[pre++] = d2[k];
    out->s[pre] = '\0';
    if (len < sizeof(out->s)) return;

    if (len > VM_STR_MAX - 1) {
        if (l1 >= VM_STR_MAX - 1) { l1 = VM_STR_MAX - 1; l2 = 0; }
        else l2 = VM_STR_MAX - 1 - l1;
        len = l1 + l2;
    }
    int slot = vm_str_slot_alloc(vm);
    if (slot < 0) { vm_sin_sitio(vm, "mas cadenas largas"); return; }   /* queda el prefijo de 127 caracteres */
    char *dst = (char *)vm_mem_get(vm, (size_t)len + 1);
    if (!dst && vm_gc_por_sitio(vm)) { vm->str_used[slot] = 1; dst = (char *)vm_mem_get(vm, (size_t)len + 1); }
    if (!dst) { vm->str_used[slot] = 0; vm_sin_sitio(vm, "una cadena"); return; }
    for (uint32_t k = 0; k < l1; ++k) dst[k] = d1[k];
    for (uint32_t k = 0; k < l2; ++k) dst[l1 + k] = d2[k];
    dst[len] = '\0';
    vm->str_ptr[slot] = dst;
    vm->str_len[slot] = len;
    out->i = (int64_t)slot + 1;
}

static int vm_values_equal(const struct vm_val *a, const struct vm_val *b)
{
    if (a->type == VAL_STR && b->type == VAL_STR) {
        const char *xa = vm_sdata(g_vm_cur, a); /*PL3_EQ*/
        const char *xb = vm_sdata(g_vm_cur, b);
        uint32_t i = 0;
        while (xa[i] && xa[i] == xb[i]) i++;
        return (xa[i] == xb[i]);
    }
        if (a->type == VAL_ARRAY && b->type == VAL_ARRAY) {
        return (a->arr_id == b->arr_id);
    }
    if (a->type == VAL_TABLE && b->type == VAL_TABLE) { /*PL4_EQTAB*/
        return (a->tab_id == b->tab_id);
    }
    /*PL11_NILEQ*/
    if (a->type == VAL_NIL && b->type == VAL_NIL) return 1;
    if ((a->type == VAL_NIL && b->type == VAL_INT && b->i == 0) ||
        (b->type == VAL_NIL && a->type == VAL_INT && a->i == 0)) return 1;
    if (a->type != b->type) return 0;
    return (a->i == b->i);
}

/*PL1_PROTPATH*/
static int vm_is_protected_path(const char *p)
{
    static const char *const prot[] = { "/etc/init.sh", "/etc/soma_manifest.txt", 0 };
    for (int i = 0; prot[i]; ++i) {
        const char *a = p;
        const char *b = prot[i];
        while (*a && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return 1;
    }
    return 0;
}

/*PL2_GC*/
static void vm_gc_mark_val(struct soma_vm *vm, const struct vm_val *v, int depth)
{
        if (v->type == VAL_STR) { /*PL3_MARKSTR*/
        if (v->i > 0 && v->i <= VM_STR_SLOTS) vm->str_mark[v->i - 1] = 1;
        return;
    }
    if (v->type == VAL_TABLE) { /*PL4_MARK_TABLE*/
        if (vm->table_pool && v->tab_id < vm->table_pool_cap) {
            struct vm_table_obj *t = &vm->table_pool[v->tab_id];
            if (!t->used || t->mark || depth > 40) return;
            t->mark = 1;
            for (uint32_t k = 0; k < t->len; ++k) {
                vm_gc_mark_val(vm, &t->entries[k].key, depth + 1);
                vm_gc_mark_val(vm, &t->entries[k].val, depth + 1);
            }
        }
        return;
    }
    if (v->type != VAL_ARRAY || !vm->array_pool || v->arr_id >= vm->array_pool_cap) return;
    struct vm_array_obj *a = &vm->array_pool[v->arr_id];
    if (!a->used || a->mark || depth > 40) return;
    a->mark = 1;
    for (uint32_t k = 0; k < a->len; ++k) vm_gc_mark_val(vm, &a->items[k], depth + 1);
}

static void vm_array_free_items(struct soma_vm *vm, struct vm_array_obj *a)
{
    vm_mem_put(vm, a->items, (size_t)a->cap * sizeof(struct vm_val));
    a->items = 0; a->cap = 0; a->len = 0;
}
static void vm_table_free_items(struct soma_vm *vm, struct vm_table_obj *t)
{
    vm_mem_put(vm, t->entries, (size_t)t->cap * sizeof(struct vm_table_entry));
    vm_mem_put(vm, t->hidx, (size_t)t->hcap * sizeof(uint32_t));
    t->entries = 0; t->cap = 0; t->len = 0; t->hidx = 0; t->hcap = 0;
}

static void vm_gc_collect(struct soma_vm *vm)
{
    for (int i = 0; i < vm->array_pool_cap; ++i) vm->array_pool[i].mark = 0;
    for (int i = 0; i < VM_STR_SLOTS; ++i) vm->str_mark[i] = 0; /*PL3_GCSTR*/
    for (int i = 0; i < vm->table_pool_cap; ++i) vm->table_pool[i].mark = 0; /*PL4_GCTAB*/
    for (int i = 0; i < 8; ++i) vm_gc_mark_val(vm, &vm->tmp_roots[i], 0);
    for (int i = 0; i < vm->sp; ++i)  vm_gc_mark_val(vm, &vm->stack[i], 0);
    for (int i = 0; i < VM_VARS_CAPACITY; ++i) vm_gc_mark_val(vm, &vm->vars[i], 0);
    int lim = vm->lp;
    if (lim > VM_LOCALS_CAPACITY) lim = VM_LOCALS_CAPACITY;
    for (int i = 0; i < lim; ++i) vm_gc_mark_val(vm, &vm->locals[i], 0);
    for (int i = 0; i < vm->array_pool_cap; ++i) {
        struct vm_array_obj *a = &vm->array_pool[i];
        if (a->used && !a->mark) { a->used = 0; vm_array_free_items(vm, a); }
    }
    for (int i = 0; i < VM_STR_SLOTS; ++i) { /*PL3_SWEEPSTR*/
        if (vm->str_used[i] && !vm->str_mark[i]) {
            vm->str_used[i] = 0;
            vm_mem_put(vm, vm->str_ptr[i], (size_t)vm->str_len[i] + 1);
            vm->str_ptr[i] = 0; vm->str_len[i] = 0;
        }
    }
    for (int i = 0; i < vm->table_pool_cap; ++i) { /*PL4_SWEEPTAB*/
        struct vm_table_obj *t = &vm->table_pool[i];
        if (t->used && !t->mark) { t->used = 0; vm_table_free_items(vm, t); }
    }
}

/* el pool de arrays (o de tablas) crece al doble hasta 'max'; los identificadores no cambian */
static int vm_pool_grow(struct soma_vm *vm, void **pool, int *cap, size_t obj, int max)
{
    (void)vm;   /* SOMA_299_CI */
    if (*cap >= max) return 0;
    int n = *cap ? *cap * 2 : 64;
    if (n > max) n = max;
    uint8_t *p = (uint8_t *)kmalloc((size_t)n * obj);          /* el pool no cuenta en el presupuesto: son descriptores */
    if (!p) return 0;
    for (size_t k = 0; k < (size_t)n * obj; ++k) p[k] = 0;
    if (*pool) {
        uint8_t *q = (uint8_t *)*pool;
        for (size_t k = 0; k < (size_t)*cap * obj; ++k) p[k] = q[k];
        kfree(*pool);
    }
    *pool = p; *cap = n;
    return 1;
}

static int vm_alloc_array(struct soma_vm *vm)
{
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < vm->array_pool_cap; ++i) {
            if (!vm->array_pool[i].used) {
                struct vm_array_obj *a = &vm->array_pool[i];
                a->used = 1; a->mark = 0; a->len = 0;
                if (i >= vm->array_pool_count) vm->array_pool_count = i + 1;
                return i;
            }
        }
        if (pass == 0 && vm->array_pool_cap >= 64) vm_gc_collect(vm);
        else if (!vm_pool_grow(vm, (void **)&vm->array_pool, &vm->array_pool_cap, sizeof(struct vm_array_obj), VM_ARRAYS_MAX)) {
            if (pass == 0) vm_gc_collect(vm); else break;
        }
    }
    return -1;
}

/* que quepan 'need' elementos en el array (crece al doble; 0 si no hay sitio) */
static int vm_array_reserve(struct soma_vm *vm, struct vm_array_obj *a, uint32_t need)
{
    if (need <= a->cap) return 1;
    if (need > VM_ARRAY_ITEMS_MAX) return 0;
    uint32_t n = a->cap ? a->cap : 8;
    while (n < need) n = (n > VM_ARRAY_ITEMS_MAX / 2) ? VM_ARRAY_ITEMS_MAX : n * 2;
    struct vm_val *it = (struct vm_val *)vm_mem_get(vm, (size_t)n * sizeof(struct vm_val));
    if (!it && n > need) { n = need; it = (struct vm_val *)vm_mem_get(vm, (size_t)n * sizeof(struct vm_val)); }
    if (!it) return 0;
    for (uint32_t k = 0; k < a->len; ++k) it[k] = a->items[k];
    vm_mem_put(vm, a->items, (size_t)a->cap * sizeof(struct vm_val));
    a->items = it; a->cap = n;
    return 1;
}
/* igual, pero si no cabe recoge basura (con el array y el valor protegidos) y lo reintenta */
static int vm_array_reserve_gc(struct soma_vm *vm, const struct vm_val *arr_v, const struct vm_val *extra, uint32_t need)
{
    struct vm_array_obj *a = &vm->array_pool[arr_v->arr_id];
    if (vm_array_reserve(vm, a, need)) return 1;
    vm->tmp_roots[4] = *arr_v;
    if (extra) vm->tmp_roots[5] = *extra;
    int otra = vm_gc_por_sitio(vm);
    vm->tmp_roots[4].type = VAL_NIL; vm->tmp_roots[5].type = VAL_NIL;
    a = &vm->array_pool[arr_v->arr_id];
    if (otra && vm_array_reserve(vm, a, need)) return 1;
    vm_sin_sitio(vm, "mas elementos en un array");
    return 0;
}

/*PL4_TAB_HELPERS*/
static int vm_alloc_table(struct soma_vm *vm)
{
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < vm->table_pool_cap; ++i) {
            if (!vm->table_pool[i].used) {
                struct vm_table_obj *t = &vm->table_pool[i];
                t->used = 1; t->mark = 0; t->len = 0;
                if (i >= vm->table_pool_count) vm->table_pool_count = i + 1;
                return i;
            }
        }
        if (pass == 0 && vm->table_pool_cap >= 64) vm_gc_collect(vm);
        else if (!vm_pool_grow(vm, (void **)&vm->table_pool, &vm->table_pool_cap, sizeof(struct vm_table_obj), VM_TABLES_MAX)) {
            if (pass == 0) vm_gc_collect(vm); else break;
        }
    }
    return -1;
}

/* SOMA_264_LANG: hash de una clave, coherente con vm_values_equal (nil == 0; arrays y tablas por identidad) */
static uint32_t vm_key_hash(const struct vm_val *k)
{
    uint64_t h;
    if (k->type == VAL_STR) {
        const char *s = vm_sdata(g_vm_cur, k);
        h = 1469598103934665603ULL;
        while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
    } else if (k->type == VAL_ARRAY) h = 0xA1A1A1A1ULL + k->arr_id;
    else if (k->type == VAL_TABLE) h = 0x7B7B7B7BULL + k->tab_id;
    else h = (k->type == VAL_NIL) ? 0 : (uint64_t)k->i;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
    return (uint32_t)h;
}
static void vm_table_reindex(struct soma_vm *vm, struct vm_table_obj *tab)
{
    if (tab->len < 32) return;                                   /* tablas pequenas: busqueda lineal, como antes */
    uint32_t need = 64; while (need < tab->len * 2) need *= 2;
    if (tab->hidx && tab->hcap >= need) return;
    uint32_t *h = (uint32_t *)vm_mem_get(vm, (size_t)need * sizeof(uint32_t));
    if (!h) return;                                               /* sin indice sigue funcionando (lineal) */
    for (uint32_t k = 0; k < need; ++k) h[k] = 0;
    for (uint32_t e = 0; e < tab->len; ++e) {
        uint32_t p = vm_key_hash(&tab->entries[e].key) & (need - 1);
        while (h[p]) p = (p + 1) & (need - 1);
        h[p] = e + 1;
    }
    vm_mem_put(vm, tab->hidx, (size_t)tab->hcap * sizeof(uint32_t));
    tab->hidx = h; tab->hcap = need;
}

static int vm_table_find_key(struct soma_vm *vm, const struct vm_table_obj *tab, const struct vm_val *key)
{
    (void)vm;
    if (tab->hidx && tab->hcap) {
        uint32_t p = vm_key_hash(key) & (tab->hcap - 1);
        while (tab->hidx[p]) {
            uint32_t e = tab->hidx[p] - 1;
            if (e < tab->len && vm_values_equal(&tab->entries[e].key, key)) return (int)e;
            p = (p + 1) & (tab->hcap - 1);
        }
        return -1;
    }
    for (uint32_t k = 0; k < tab->len; ++k) {
        if (vm_values_equal(&tab->entries[k].key, key)) return (int)k;
    }
    return -1;
}

static int vm_table_reserve(struct soma_vm *vm, struct vm_table_obj *tab, uint32_t need)
{
    if (need <= tab->cap) return 1;
    if (need > VM_TABLE_ITEMS_MAX) return 0;
    uint32_t n = tab->cap ? tab->cap * 2 : 8;
    while (n < need) n *= 2;
    if (n > VM_TABLE_ITEMS_MAX) n = VM_TABLE_ITEMS_MAX;
    struct vm_table_entry *e = (struct vm_table_entry *)vm_mem_get(vm, (size_t)n * sizeof(struct vm_table_entry));
    if (!e) return 0;
    for (uint32_t k = 0; k < tab->len; ++k) e[k] = tab->entries[k];
    vm_mem_put(vm, tab->entries, (size_t)tab->cap * sizeof(struct vm_table_entry));
    tab->entries = e; tab->cap = n;
    return 1;
}

/* anade al final sin buscar (la clave no esta); actualiza el indice */
static int vm_table_append(struct soma_vm *vm, struct vm_table_obj *tab, const struct vm_val *key, const struct vm_val *val)
{
    if (!vm_table_reserve(vm, tab, tab->len + 1)) return 0;
    tab->entries[tab->len].key = *key;
    tab->entries[tab->len].val = *val;
    tab->len++;
    if (tab->hidx && tab->len * 2 <= tab->hcap) {
        uint32_t p = vm_key_hash(key) & (tab->hcap - 1);
        while (tab->hidx[p]) p = (p + 1) & (tab->hcap - 1);
        tab->hidx[p] = tab->len;
    } else vm_table_reindex(vm, tab);
    return 1;
}

static int vm_table_set_val(struct soma_vm *vm, struct vm_table_obj *tab,
                            const struct vm_val *key, const struct vm_val *val)
{
    int idx = vm_table_find_key(vm, tab, key);
    if (idx >= 0) {
        tab->entries[idx].val = *val;
        return 1;
    }
    if (tab->len >= VM_TABLE_ITEMS_MAX) return 0;
    if (!vm_table_append(vm, tab, key, val)) { vm_sin_sitio(vm, "mas entradas en una tabla"); return 0; }
    return 1;
}

static uint32_t vm_rand31(void)
{
    static uint64_t rng = 0;
    if (rng == 0) rng = arch_cycle_counter();
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(rng >> 32) & 0x7FFFFFFF;
}

static void int_to_str(int64_t val, char *buf, uint32_t max_buf)
{
    char tmp[32];
    int tp = 0;
    int neg = (val < 0);
    uint64_t u = neg ? (0 - (uint64_t)val) : (uint64_t)val;
    if (u == 0) tmp[tp++] = '0';
    while (u > 0 && tp < 30) {
        tmp[tp++] = '0' + (u % 10);
        u /= 10;
    }
    if (neg && tp < 31) tmp[tp++] = '-';
    uint32_t pos = 0;
    while (tp > 0 && pos < max_buf - 1) {
        buf[pos++] = tmp[--tp];
    }
    buf[pos] = '\0';
}

static int val_to_str_depth = 0; /*PL1_VTS_DECL*/

static void val_to_str(struct soma_vm *vm, const struct vm_val *v, char *buf, uint32_t max_buf)
{
    if (v->type == VAL_INT) {
        int_to_str(v->i, buf, max_buf);
    } else if (v->type == VAL_STR) {
        uint32_t k = 0;
        const char *sd_ = vm_sdata(vm, v); /*PL3_VTS*/
        while (sd_[k] && k < max_buf - 1) { buf[k] = sd_[k]; k++; }
        buf[k] = '\0';
    } else if (v->type == VAL_ARRAY && val_to_str_depth >= 3) { /*PL1_VTS_ENTER*/
        uint32_t z = 0;
        const char *ell = "[...]";
        while (ell[z] && z + 1 < max_buf) { buf[z] = ell[z]; z++; }
        buf[z] = '\0';
    } else if (v->type == VAL_ARRAY) {
        uint32_t pos = 0;
        val_to_str_depth++;
        if (pos < max_buf - 1) buf[pos++] = '[';
        if (v->arr_id < vm->array_pool_count) {
            struct vm_array_obj *arr = &vm->array_pool[v->arr_id];
            for (uint32_t k = 0; k < arr->len && pos < max_buf - 8; ++k) {
                if (k > 0 && pos < max_buf - 3) { buf[pos++] = ','; buf[pos++] = ' '; }
                char ibuf[64]; /*PL1_IBUF*/
                val_to_str(vm, &arr->items[k], ibuf, sizeof(ibuf));
                for (uint32_t m = 0; ibuf[m] && pos < max_buf - 2; ++m) buf[pos++] = ibuf[m];
            }
        }
                if (pos < max_buf - 1) buf[pos++] = ']';
        buf[pos] = '\0';
        val_to_str_depth--; /*PL1_VTS_LEAVE*/
    } else if (v->type == VAL_TABLE) { /*PL4_VTS_TAB*/
        uint32_t pos = 0;
        val_to_str_depth++;
        if (pos < max_buf - 1) buf[pos++] = '{';
        if (vm->table_pool && v->tab_id < vm->table_pool_cap) {
            struct vm_table_obj *tab = &vm->table_pool[v->tab_id];
            for (uint32_t k = 0; k < tab->len && pos < max_buf - 12; ++k) {
                if (k > 0 && pos < max_buf - 3) { buf[pos++] = ','; buf[pos++] = ' '; }
                char kbuf[48];
                val_to_str(vm, &tab->entries[k].key, kbuf, sizeof(kbuf));
                for (uint32_t m = 0; kbuf[m] && pos < max_buf - 4; ++m) buf[pos++] = kbuf[m];
                if (pos < max_buf - 3) { buf[pos++] = ':'; buf[pos++] = ' '; }
                char vbuf[64];
                val_to_str(vm, &tab->entries[k].val, vbuf, sizeof(vbuf));
                for (uint32_t m = 0; vbuf[m] && pos < max_buf - 2; ++m) buf[pos++] = vbuf[m];
            }
        }
        if (pos < max_buf - 1) buf[pos++] = '}';
        buf[pos] = '\0';
        val_to_str_depth--;
    } else {
        buf[0] = '\0';
    }
}

/* ====================================================================
 * PRIMITIVAS GRAFICAS REALES CONECTADAS AL DRIVER FB
 * ==================================================================== */

static void native_fb_clear(uint32_t color)
{
    if (!fb_is_active()) return;
    fb_fill_rect(0, 0, 1280, 720, color);
}

static void native_fb_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!fb_is_active() || w <= 0 || h <= 0) return; /*PL1_RECT*/
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

static void native_fb_pixel(int x, int y, uint32_t color)
{
    if (!fb_is_active() || x < 0 || y < 0) return;
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, color);
}

static void native_fb_present(void)
{
    if (!fb_is_active()) return;
    fb_present();
}

static void native_fb_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!fb_is_active()) return;
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        native_fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void native_fb_circle(int cx, int cy, int r, uint32_t color)
{
    if (!fb_is_active() || r <= 0) return;
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    while (y >= x) {
        native_fb_rect(cx - x, cy + y, 2 * x + 1, 1, color); /*PL1_CIRCLE*/
        native_fb_rect(cx - x, cy - y, 2 * x + 1, 1, color);
        native_fb_rect(cx - y, cy + x, 2 * y + 1, 1, color);
        native_fb_rect(cx - y, cy - x, 2 * y + 1, 1, color);
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

/*PL1_FONT*/
struct fb_glyph_p { char ch; uint8_t rows[7]; };

static const uint8_t font5x7_digits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}
};

static const uint8_t font5x7_upper[26][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}  /* Z */
};

static const struct fb_glyph_p font5x7_punct[] = {
    { '.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C} },
    { ',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08} },
    { '-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00} },
    { '!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04} },
    { '?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04} },
    { ':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00} },
    { ';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08} },
    { '(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02} },
    { ')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08} },
    { '/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10} },
    { '+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00} },
    { '=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00} },
    { '_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F} },
    { '>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08} },
    { '<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02} },
    { '*', {0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00} },
    { '%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03} },
    { '#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A} },
    { '[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E} },
    { ']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E} },
    { '|', {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04} },
    { 39,  {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00} },
    { '"', {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00} }
};

static const uint8_t *native_glyph(char c)
{
    if (c >= '0' && c <= '9') return font5x7_digits[c - '0'];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return font5x7_upper[c - 'A'];
    for (unsigned i = 0; i < sizeof(font5x7_punct) / sizeof(font5x7_punct[0]); ++i) {
        if (font5x7_punct[i].ch == c) return font5x7_punct[i].rows;
    }
    return 0;
}

static void native_fb_char(int x, int y, char c, uint32_t color)
{
    if (!fb_is_active()) return;
    const uint8_t *g = native_glyph(c);
    if (!g) { native_fb_rect(x, y + 1, 5, 6, color); return; }
    for (int r = 0; r < 7; ++r) {
        uint8_t row = g[r];
        int b = 0;
        while (b < 5) {
            if (row & (1 << (4 - b))) {
                int s = b;
                while (b < 5 && (row & (1 << (4 - b)))) b++;
                native_fb_rect(x + s, y + r, b - s, 1, color);
            } else {
                b++;
            }
        }
    }
}

static void native_fb_text(int x, int y, const char *str, uint32_t color)
{
    if (!fb_is_active() || !str) return; /*PL1_TEXT*/
    int cur_x = x;
    while (*str) {
        unsigned char uc = (unsigned char)*str;
        if (uc == ' ') {
            cur_x += 6;
        } else if (uc == '\n') {
            cur_x = x;
            y += 9;
        } else if (uc == 0xC3 && str[1]) {
            unsigned char d = (unsigned char)str[1];
            char base = 0;
            if (d == 0xA1 || d == 0x81) base = 'a';
            else if (d == 0xA9 || d == 0x89) base = 'e';
            else if (d == 0xAD || d == 0x8D) base = 'i';
            else if (d == 0xB3 || d == 0x93) base = 'o';
            else if (d == 0xBA || d == 0x9A || d == 0xBC || d == 0x9C) base = 'u';
            else if (d == 0xB1 || d == 0x91) base = 'n';
            if (base) {
                native_fb_char(cur_x, y, base, color);
                cur_x += 7;
            }
            str += 2;
            continue;
        } else if (uc >= 0x80) {
            /* otros bytes UTF-8: se omiten */
        } else {
            native_fb_char(cur_x, y, (char)uc, color);
            cur_x += 7;
        }
        str++;
    }
}

/* ====================================================================
 * GESTOR DE VENTANAS NATIVO SOMA-LANG (Her-Aura Desktop Style)
 * ==================================================================== */
/*PL4_WIN_FUNCS*/
/*PL5_MOUSE_BIND*/
extern int mouse_x __attribute__((weak));
extern int mouse_y __attribute__((weak));
extern int mouse_buttons __attribute__((weak));
int __attribute__((weak)) mouse_get_x(void) { return 640; }
int __attribute__((weak)) mouse_get_y(void) { return 360; }
int __attribute__((weak)) mouse_get_buttons(void) { return 0; }

static void native_get_mouse_state(int *out_x, int *out_y, int *out_btn)
{
    int x = 640, y = 360, b = 0;
    if (&mouse_x && &mouse_y) {
        x = mouse_x;
        y = mouse_y;
        if (&mouse_buttons) b = mouse_buttons;
    } else {                                  /* las versiones debiles devuelven el centro y 0; mouse.c las sustituye al enlazar */
        x = mouse_get_x();
        y = mouse_get_y();
        b = mouse_get_buttons();
    }
    *out_x = x;
    *out_y = y;
    *out_btn = b;
}

#define WIN_TITLE_H 26

/*PL11_WINCLIP*/
/* Linea de Bresenham recortada al area cliente de la ventana (coordenadas absolutas). */
static void native_win_line_clipped(struct soma_vm *vm, int x0, int y0, int x1, int y1, uint32_t color)
{
    int cx0 = vm->win.x + 2;
    int cy0 = vm->win.y + WIN_TITLE_H + 1;
    int cx1 = vm->win.x + vm->win.w - 2;
    int cy1 = vm->win.y + vm->win.h - 2;
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    if (dx > 4096 || dy > 4096) return;
    while (1) {
        if (x0 >= cx0 && x0 < cx1 && y0 >= cy0 && y0 < cy1)
            native_fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void native_win_draw_frame(struct soma_vm *vm)
{
    if (!fb_is_active() || !vm->win.active) return;
    int x = vm->win.x;
    int y = vm->win.y;
    int w = vm->win.w;
    int h = vm->win.h;

    /* Sombra exterior y marco */
    native_fb_rect(x + 4, y + 4, w, h, 0x050811);
    native_fb_rect(x, y, w, h, 0x334155);
    native_fb_rect(x + 1, y + 1, w - 2, h - 2, 0x0F172A);

    /* Barra de titulo cosmica */
    native_fb_rect(x + 1, y + 1, w - 2, WIN_TITLE_H, 0x1E293B);
    native_fb_line(x + 1, y + WIN_TITLE_H, x + w - 2, y + WIN_TITLE_H, 0x38BDF8);

    /* Botones de control estilo ventana SOMA */
    native_fb_rect(x + w - 20, y + 5, 14, 14, 0xF43F5E); /* Cerrar [x] */
    native_fb_text(x + w - 16, y + 7, "x", 0xFFFFFF);
    native_fb_rect(x + w - 38, y + 5, 14, 14, 0xF59E0B); /* Minimizar */
    native_fb_rect(x + w - 56, y + 5, 14, 14, 0x10B981); /* Estado */

    /* Titulo en cyan brillante */
    if (vm->win.title[0]) {
        native_fb_text(x + 10, y + 7, vm->win.title, 0x38BDF8);
    }

    /* Fondo del area cliente */
    native_fb_rect(x + 2, y + WIN_TITLE_H + 1, w - 4, h - WIN_TITLE_H - 3, vm->win.bg);
}

static void native_win_create(struct soma_vm *vm, const char *title, int x, int y, int w, int h)
{
    if (w < 120) w = 120;
    if (h < 80) h = 80;
    if (w > 1240) w = 1240;
    if (h > 700) h = 700;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > 1280) x = 1280 - w;
    if (y + h > 720) y = 720 - h;

    vm->win.active = 1;
    vm->win.x = x;
    vm->win.y = y;
    vm->win.w = w;
    vm->win.h = h;
    vm->win.bg = 0x0A0D14;

    uint32_t tlen = 0;
    while (title[tlen] && tlen < sizeof(vm->win.title) - 1) {
        vm->win.title[tlen] = title[tlen];
        tlen++;
    }
    vm->win.title[tlen] = '\0';

    native_win_draw_frame(vm);
}

static void native_win_clear(struct soma_vm *vm, uint32_t color)
{
    if (!fb_is_active() || !vm->win.active) return;
    vm->win.bg = color;
    int x = vm->win.x + 2;
    int y = vm->win.y + WIN_TITLE_H + 1;
    int w = vm->win.w - 4;
    int h = vm->win.h - WIN_TITLE_H - 3;
    native_fb_rect(x, y, w, h, color);
}

static void native_win_rect(struct soma_vm *vm, int rx, int ry, int rw, int rh, uint32_t color)
{
    if (!fb_is_active() || !vm->win.active || rw <= 0 || rh <= 0) return;
    int cx = vm->win.x + 2;
    int cy = vm->win.y + WIN_TITLE_H + 1;
    int cw = vm->win.w - 4;
    int ch = vm->win.h - WIN_TITLE_H - 3;

    int ax = cx + rx;
    int ay = cy + ry;
    if (ax < cx) { rw -= (cx - ax); ax = cx; }
    if (ay < cy) { rh -= (cy - ay); ay = cy; }
    if (ax + rw > cx + cw) rw = (cx + cw) - ax;
    if (ay + rh > cy + ch) rh = (cy + ch) - ay;
    if (rw <= 0 || rh <= 0) return;

    native_fb_rect(ax, ay, rw, rh, color);
}

static void native_win_text(struct soma_vm *vm, int tx, int ty, const char *str, uint32_t color)
{
    if (!fb_is_active() || !vm->win.active || !str) return;
    int ax = vm->win.x + 2 + tx;
    int ay = vm->win.y + WIN_TITLE_H + 1 + ty;
    if (ax < vm->win.x + 2 || ay < vm->win.y + WIN_TITLE_H + 1) return;
    if (ax >= vm->win.x + vm->win.w - 4 || ay >= vm->win.y + vm->win.h - 4) return;
    { /*PL11_WINTEXT*/
        char clipped[128];
        int right  = vm->win.x + vm->win.w - 2;
        int bottom = vm->win.y + vm->win.h - 2;
        int maxc   = (right - ax) / 7;
        int n      = 0;
        if (ay + 7 > bottom || maxc <= 0) return;
        while (str[n] && n < maxc && n < (int)sizeof(clipped) - 1) { clipped[n] = str[n]; n++; }
        clipped[n] = '\0';
        native_fb_text(ax, ay, clipped, color);
    }
}

static void native_win_line(struct soma_vm *vm, int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!fb_is_active() || !vm->win.active) return;
    int ox = vm->win.x + 2;
    int oy = vm->win.y + WIN_TITLE_H + 1;
    native_win_line_clipped(vm, ox + x0, oy + y0, ox + x1, oy + y1, color); /*PL11_WINLINE*/
}

static void native_win_circle(struct soma_vm *vm, int cx, int cy, int r, uint32_t color)
{
    if (!fb_is_active() || !vm->win.active || r <= 0) return;
    int ox = vm->win.x + 2;
    int oy = vm->win.y + WIN_TITLE_H + 1;
    { /*PL11_WINCIRCLE*/
        int x = 0;
        int y = r;
        int d = 3 - 2 * r;
        (void)ox; (void)oy;
        if (r > 1000) return;
        while (y >= x) {
            native_win_rect(vm, cx - x, cy + y, 2 * x + 1, 1, color);
            native_win_rect(vm, cx - x, cy - y, 2 * x + 1, 1, color);
            native_win_rect(vm, cx - y, cy + x, 2 * y + 1, 1, color);
            native_win_rect(vm, cx - y, cy - x, 2 * y + 1, 1, color);
            x++;
            if (d > 0) {
                y--;
                d = d + 4 * (x - y) + 10;
            } else {
                d = d + 4 * x + 6;
            }
        }
    }
}

static void native_win_close(struct soma_vm *vm)
{
    if (!fb_is_active() || !vm->win.active) return;
    native_fb_rect(vm->win.x, vm->win.y, vm->win.w + 4, vm->win.h + 4, 0x0A0D14);
    vm->win.active = 0;
    native_fb_present();
}

static int vm_run_inner(struct soma_vm *vm, const uint8_t *bytecode, uint32_t len) /*PL3_RUNWRAP_A*/
{
    uint64_t vm_steps = 0; /*PL1_RUNINIT*/
    vm->fsp = 0;
    vm->lp = 0;
    vm->fp = 0;
    vm->code = bytecode;
    vm->code_len = len;
    vm->ip = 0;
    vm->halted = 0;

    while (vm->ip < vm->code_len && !vm->halted) {
        if (++vm_steps > vm_step_limit) { /*PL1_WATCHDOG*/
            kprint("[VM ERROR] Limite de instrucciones sin pausa excedido (bucle infinito?). Usa sys.sleep() en bucles largos.\n");
            return -1;
        }
        if ((vm_steps & 4095u) == 0) thread_preempt_point();   /* PARCHE 098: un programa sin pausas no congela el sistema */
        uint8_t op = vm->code[vm->ip++];

        switch (op) {
        case OP_HALT:
            vm->halted = 1;
            break;

        case OP_PUSH_INT: {
            if (vm->ip + 8 > vm->code_len) return -1;
            int64_t val = read_i64(&vm->code[vm->ip]);
            vm->ip += 8;
            struct vm_val v = { VAL_INT, val, {0}, 0, 0 };
            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_PUSH_STR: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t slen = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (vm->ip + slen > vm->code_len) return -1;
            struct vm_val v;
            v.type = VAL_STR;
            v.i = 0;
            v.arr_id = 0;
            uint32_t cpy = (slen < sizeof(v.s) - 1) ? slen : (sizeof(v.s) - 1);
            for (uint32_t k = 0; k < cpy; ++k) v.s[k] = (char)vm->code[vm->ip + k];
            v.s[cpy] = '\0';
            vm->ip += slen;
            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_POP:
            pop_val(vm);
            break;

        case OP_SWAP: {
            if (vm->sp < 2) return -1;
            struct vm_val tmp = vm->stack[vm->sp - 1];
            vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2];
            vm->stack[vm->sp - 2] = tmp;
            break;
        }

        case OP_DUP: {
            if (vm->sp <= 0) return -1;
            struct vm_val top = vm->stack[vm->sp - 1];
            if (!push_val(vm, top)) return -1;
            break;
        }

        case OP_ADD: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);

            if (a.type == VAL_STR || b.type == VAL_STR || a.type == VAL_ARRAY || b.type == VAL_ARRAY) {
                struct vm_val res;
                res.type = VAL_STR;
                res.i = 0;
                res.arr_id = 0;
                res.s[0] = '\0';

                { /*PL3_ADD*/
                    char a_buf[512], b_buf[512]; /*PL11_BUFAB*/
                    const char *ad;
                    const char *bd;
                    uint32_t la = 0, lb = 0;
                    if (a.type == VAL_STR) { ad = vm_sdata(vm, &a); la = vm_slen(vm, &a); }
                    else { val_to_str(vm, &a, a_buf, sizeof(a_buf)); ad = a_buf; while (a_buf[la]) la++; }
                    if (b.type == VAL_STR) { bd = vm_sdata(vm, &b); lb = vm_slen(vm, &b); }
                    else { val_to_str(vm, &b, b_buf, sizeof(b_buf)); bd = b_buf; while (b_buf[lb]) lb++; }
                    vm->tmp_roots[0] = a;
                    vm->tmp_roots[1] = b;
                    vm_str_make2(vm, &res, ad, la, bd, lb);
                    vm->tmp_roots[0].type = VAL_NIL;
                    vm->tmp_roots[1].type = VAL_NIL;
                }

                if (!push_val(vm, res)) return -1;
            } else {
                struct vm_val res = { VAL_INT, (int64_t)((uint64_t)a.i + (uint64_t)b.i), {0}, 0, 0 };
                if (!push_val(vm, res)) return -1;
            }
            break;
        }

        case OP_SUB: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (int64_t)((uint64_t)a.i - (uint64_t)b.i), {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_MUL: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (int64_t)((uint64_t)a.i * (uint64_t)b.i), {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_DIV: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            if (b.i == 0) {
                kprint("[VM ERROR] Division por cero en Ring 0\n");
                return -1;
            }
            struct vm_val res = { VAL_INT, (b.i == -1) ? (int64_t)(0 - (uint64_t)a.i) : (a.i / b.i), {0}, 0, 0 }; /*PL11_DIV*/
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_MOD: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            if (b.i == 0) {
                kprint("[VM ERROR] Modulo por cero en Ring 0\n");
                return -1;
            }
            struct vm_val res = { VAL_INT, (b.i == -1) ? 0 : (a.i % b.i), {0}, 0, 0 }; /*PL11_MOD*/
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_EQ: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, vm_values_equal(&a, &b) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_NEQ: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, !vm_values_equal(&a, &b) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_LT: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (a.i < b.i) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_GT: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (a.i > b.i) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_LE: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (a.i <= b.i) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_GE: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (a.i >= b.i) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_AND: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (vm_truthy(&a) && vm_truthy(&b)) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_OR: {
            struct vm_val b = pop_val(vm);
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (vm_truthy(&a) || vm_truthy(&b)) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_NOT: {
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, !vm_truthy(&a) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_JUMP: {
            if (vm->ip > vm->code_len || vm->code_len - vm->ip < 4) return -1;
            int32_t off = read_i32(&vm->code[vm->ip]);
            int64_t target = (int64_t)vm->ip + 4 + (int64_t)off;
            if (target < 0 || (uint64_t)target > vm->code_len) return -1;
            vm->ip = (uint32_t)target;
            break;
        }

        case OP_JUMP_IF_ZERO: {
            if (vm->ip > vm->code_len || vm->code_len - vm->ip < 4 || vm->sp <= 0) return -1;
            int32_t off = read_i32(&vm->code[vm->ip]);
            vm->ip += 4;
            int64_t target = (int64_t)vm->ip + (int64_t)off;
            if (target < 0 || (uint64_t)target > vm->code_len) return -1;
            struct vm_val cond = pop_val(vm);
            if (!vm_truthy(&cond)) vm->ip = (uint32_t)target;
            break;
        }

        case OP_JUMP_IF_NZ: {
            if (vm->ip > vm->code_len || vm->code_len - vm->ip < 4 || vm->sp <= 0) return -1;
            int32_t off = read_i32(&vm->code[vm->ip]);
            vm->ip += 4;
            int64_t target = (int64_t)vm->ip + (int64_t)off;
            if (target < 0 || (uint64_t)target > vm->code_len) return -1;
            struct vm_val cond = pop_val(vm);
            if (vm_truthy(&cond)) vm->ip = (uint32_t)target;
            break;
        }

        case OP_STORE_VAR: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t slot = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (slot < VM_VARS_CAPACITY) {
                vm->vars[slot] = pop_val(vm);
            }
            break;
        }

        case OP_LOAD_VAR: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t slot = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (slot < VM_VARS_CAPACITY) {
                if (!push_val(vm, vm->vars[slot])) return -1;
            }
            break;
        }

        case OP_CALL: {
            if (vm->ip + 4 > vm->code_len) return -1;
            int32_t target = read_i32(&vm->code[vm->ip]);
            vm->ip += 4;
            if (target < 0 || (uint32_t)target >= vm->code_len) return -1;
            if (vm->fsp >= VM_FRAMES_CAPACITY) {
                kprint("[VM ERROR] Desbordamiento de marcos de llamada\n");
                return -1;
            }
            vm->frames[vm->fsp].ret_ip = vm->ip;
            vm->frames[vm->fsp].fp = vm->fp;
            vm->fsp++;
            vm->ip = (uint32_t)target;
            break;
        }

        case OP_ENTER: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t n = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (vm->lp + n > VM_LOCALS_CAPACITY) {
                kprint("[VM ERROR] Desbordamiento de variables locales\n");
                return -1;
            }
            vm->fp = vm->lp;
            for (uint16_t k = 0; k < n; ++k) { /*PL1_ENTERZ*/
                vm->locals[vm->fp + k].type = VAL_NIL;
                vm->locals[vm->fp + k].i = 0;
                vm->locals[vm->fp + k].arr_id = 0;
                vm->locals[vm->fp + k].s[0] = '\0';
            }
            vm->lp += n;
            break;
        }

        case OP_LOAD_LOCAL: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t s = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (vm->fp + s < VM_LOCALS_CAPACITY) {
                if (!push_val(vm, vm->locals[vm->fp + s])) return -1;
            }
            break;
        }

        case OP_STORE_LOCAL: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t s = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            if (vm->fp + s < VM_LOCALS_CAPACITY) {
                vm->locals[vm->fp + s] = pop_val(vm);
            }
            break;
        }

        case OP_RET: {
            if (vm->fsp <= 0) {
                vm->halted = 1;
                break;
            }
            struct vm_frame f = vm->frames[--vm->fsp];
            vm->lp = vm->fp;
            vm->fp = f.fp;
            vm->ip = f.ret_ip;
            break;
        }

        case OP_NEW_ARRAY: {
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t count = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            int aid_i = vm_alloc_array(vm); /*PL2_NEWARR*/
            if (aid_i < 0) {
                kprint("[VM ERROR] Demasiados arrays vivos a la vez (8192)\n");
                return -1;
            }
            uint16_t aid = (uint16_t)aid_i;
            struct vm_array_obj *arr = &vm->array_pool[aid];
            arr->len = 0;
            if (count && !vm_array_reserve(vm, arr, count) && vm_gc_por_sitio(vm)) { arr = &vm->array_pool[aid]; arr->used = 1; (void)vm_array_reserve(vm, arr, count); }
            arr->len = count <= arr->cap ? count : arr->cap;   /* sin sitio: se quedan los que quepan (antes, 48) */
            for (int k = (int)count - 1; k >= 0; --k) { /*PL1_NEWARR*/
                struct vm_val item = pop_val(vm);
                if (k < (int)arr->len) arr->items[k] = item;
            }
            struct vm_val res = { VAL_ARRAY, 0, {0}, aid, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_ARRAY_GET: {
            struct vm_val idx = pop_val(vm);
            struct vm_val arr_v = pop_val(vm);

                        if (arr_v.type == VAL_STR) {
                uint32_t slen = 0; while (arr_v.s[slen]) slen++;
                struct vm_val res;
                res.type = VAL_STR;
                res.i = 0;
                res.arr_id = 0;
                res.tab_id = 0;
                res.s[0] = '\0';
                if (idx.i >= 0 && idx.i < (int64_t)(slen >= 127 ? vm_slen(vm, &arr_v) : slen)) { /*PL3_GET1*/
                    res.s[0] = vm_sdata(vm, &arr_v)[idx.i]; /*PL3_GET2*/
                    res.s[1] = '\0';
                }
                if (!push_val(vm, res)) return -1;
                break;
            }

            if (arr_v.type == VAL_TABLE) { /*PL4_TABGET*/
                struct vm_val res = { VAL_NIL, 0, {0}, 0, 0 };
                if (vm->table_pool && arr_v.tab_id < vm->table_pool_cap) {
                    struct vm_table_obj *tab = &vm->table_pool[arr_v.tab_id];
                    int ki = vm_table_find_key(vm, tab, &idx);
                    if (ki >= 0) res = tab->entries[ki].val;
                }
                if (!push_val(vm, res)) return -1;
                break;
            }

            if (arr_v.type != VAL_ARRAY || arr_v.arr_id >= vm->array_pool_count) {
                struct vm_val nil = { VAL_NIL, 0, {0}, 0, 0 };
                if (!push_val(vm, nil)) return -1;
                break;
            }
            struct vm_array_obj *arr = &vm->array_pool[arr_v.arr_id];
            if (idx.type == VAL_STR || idx.type == VAL_ARRAY || idx.type == VAL_TABLE ||
                idx.i < 0 || idx.i >= (int64_t)arr->len) { /*PL11_AGET*/
                struct vm_val nil = { VAL_NIL, 0, {0}, 0, 0 };
                if (!push_val(vm, nil)) return -1;
                break;
            }
            if (!push_val(vm, arr->items[idx.i])) return -1;
            break;
        }

                case OP_ARRAY_SET: {
            struct vm_val val = pop_val(vm);
            struct vm_val idx = pop_val(vm);
            struct vm_val arr_v = pop_val(vm);
            if (arr_v.type == VAL_ARRAY && arr_v.arr_id < vm->array_pool_count) {
                struct vm_array_obj *arr = &vm->array_pool[arr_v.arr_id];
                if (idx.type != VAL_STR && idx.type != VAL_ARRAY && idx.type != VAL_TABLE) { /*PL11_ASET*/
                    if (idx.i >= 0 && idx.i < (int64_t)arr->len) {
                        arr->items[idx.i] = val;
                    } else if (idx.i == (int64_t)arr->len && vm_array_reserve_gc(vm, &arr_v, &val, arr->len + 1)) {
                        arr = &vm->array_pool[arr_v.arr_id];
                        arr->items[arr->len++] = val;
                    }
                }
            } else if (arr_v.type == VAL_TABLE && vm->table_pool && arr_v.tab_id < vm->table_pool_cap) { /*PL4_TABSET*/
                vm_table_set_val(vm, &vm->table_pool[arr_v.tab_id], &idx, &val);
            }
            break;
        }

        case OP_ARRAY_LEN: {
            struct vm_val arr_v = pop_val(vm);
            int64_t l = 0;
            if (arr_v.type == VAL_ARRAY && arr_v.arr_id < vm->array_pool_count) {
                l = (int64_t)vm->array_pool[arr_v.arr_id].len;
                        } else if (arr_v.type == VAL_STR) {
                uint32_t slen = 0; while (arr_v.s[slen]) slen++;
                l = (int64_t)(slen >= 127 ? vm_slen(vm, &arr_v) : slen); /*PL3_LEN*/
            } else if (arr_v.type == VAL_TABLE && vm->table_pool && arr_v.tab_id < vm->table_pool_cap) { /*PL4_TABLEN*/
                l = (int64_t)vm->table_pool[arr_v.tab_id].len;
            }
            struct vm_val res = { VAL_INT, l, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_ARRAY_PUSH: {
            struct vm_val val = pop_val(vm);
            struct vm_val arr_v = pop_val(vm);
            if (arr_v.type == VAL_ARRAY && arr_v.arr_id < vm->array_pool_count) {
                struct vm_array_obj *arr = &vm->array_pool[arr_v.arr_id];
                if (vm_array_reserve_gc(vm, &arr_v, &val, arr->len + 1)) {
                    arr = &vm->array_pool[arr_v.arr_id];
                    arr->items[arr->len++] = val;
                }
            }
            break;
        }

        case OP_ARRAY_POP: {
            struct vm_val arr_v = pop_val(vm);
            struct vm_val res = { VAL_NIL, 0, {0}, 0, 0 };
            if (arr_v.type == VAL_ARRAY && arr_v.arr_id < vm->array_pool_count) {
                struct vm_array_obj *arr = &vm->array_pool[arr_v.arr_id];
                if (arr->len > 0) {
                    res = arr->items[--arr->len];
                }
            }
            if (!push_val(vm, res)) return -1;
            break;
        }

                case OP_NEW_TABLE: { /*PL4_NEW_TABLE_CASE*/
            if (vm->ip + 2 > vm->code_len) return -1;
            uint16_t count = read_u16(&vm->code[vm->ip]);
            vm->ip += 2;
            int tid_i = vm_alloc_table(vm);
            if (tid_i < 0) {
                kprint("[VM ERROR] Demasiadas tablas vivas a la vez (8192)\n");
                return -1;
            }
            uint16_t tid = (uint16_t)tid_i;
            struct vm_table_obj *tab = &vm->table_pool[tid];
            tab->len = 0;
            uint32_t tcap = 0;
            if (count && vm_table_reserve(vm, tab, count)) tcap = count;
            for (int k = (int)count - 1; k >= 0; --k) {
                struct vm_val v = pop_val(vm);
                struct vm_val ky = pop_val(vm);
                if ((uint32_t)k < tcap) {
                    tab->entries[k].key = ky;
                    tab->entries[k].val = v;
                    tab->len++;
                }
            }
            vm_table_reindex(vm, tab);
            struct vm_val res = { VAL_TABLE, 0, {0}, 0, tid };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_TABLE_HAS: {
            struct vm_val ky = pop_val(vm);
            struct vm_val tab_v = pop_val(vm);
            int has = 0;
            if (tab_v.type == VAL_TABLE && vm->table_pool && tab_v.tab_id < vm->table_pool_cap) {
                has = (vm_table_find_key(vm, &vm->table_pool[tab_v.tab_id], &ky) >= 0);
            }
            struct vm_val res = { VAL_INT, has ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_TABLE_KEYS: {
            struct vm_val tab_v = pop_val(vm);
            vm->tmp_roots[2] = tab_v; /*PL11_KEYSROOT*/
            int aid_i = vm_alloc_array(vm);
            vm->tmp_roots[2].type = VAL_NIL;
            if (aid_i < 0) return -1;
            uint16_t aid = (uint16_t)aid_i;
            struct vm_array_obj *arr = &vm->array_pool[aid];
            arr->len = 0;
            if (tab_v.type == VAL_TABLE && vm->table_pool && tab_v.tab_id < vm->table_pool_cap) {
                struct vm_table_obj *tab = &vm->table_pool[tab_v.tab_id];
                if (tab->len && vm_array_reserve(vm, arr, tab->len)) {
                    for (uint32_t k = 0; k < tab->len; ++k) {
                        arr->items[k] = tab->entries[k].key;
                        arr->len++;
                    }
                } else if (tab->len) vm_sin_sitio(vm, "las claves de una tabla");
            }
            struct vm_val res = { VAL_ARRAY, 0, {0}, aid, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

                /*PL12_WIN_OPS*/
        case OP_SYS_WIN_CREATE: {
            struct vm_val vh = pop_val(vm);
            struct vm_val vw = pop_val(vm);
            struct vm_val vy = pop_val(vm);
            struct vm_val vx = pop_val(vm);
            struct vm_val vt = pop_val(vm);
            char tbuf[64];
            val_to_str(vm, &vt, tbuf, sizeof(tbuf));
            if (fb_is_active()) {
                gui_soma_win_open(tbuf, (int)vw.i, (int)vh.i);
                vm->win.active = gui_soma_win_is_active();
            } else {
                native_win_create(vm, tbuf, (int)vx.i, (int)vy.i, (int)vw.i, (int)vh.i);
            }
            struct vm_val res = { VAL_INT, vm->win.active ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_CLEAR: {
            struct vm_val col = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_clear((uint32_t)col.i);
            } else {
                native_win_clear(vm, (uint32_t)col.i);
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_RECT: {
            struct vm_val col = pop_val(vm);
            struct vm_val h   = pop_val(vm);
            struct vm_val w   = pop_val(vm);
            struct vm_val y   = pop_val(vm);
            struct vm_val x   = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_rect((int)x.i, (int)y.i, (int)w.i, (int)h.i, (uint32_t)col.i);
            } else {
                native_win_rect(vm, (int)x.i, (int)y.i, (int)w.i, (int)h.i, (uint32_t)col.i);
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_TEXT: {
            struct vm_val col  = pop_val(vm);
            struct vm_val text = pop_val(vm);
            struct vm_val y    = pop_val(vm);
            struct vm_val x    = pop_val(vm);
            char tbuf[128];
            val_to_str(vm, &text, tbuf, sizeof(tbuf));
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_text((int)x.i, (int)y.i, tbuf, (uint32_t)col.i);
            } else {
                native_win_text(vm, (int)x.i, (int)y.i, tbuf, (uint32_t)col.i);
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_LINE: {
            struct vm_val col = pop_val(vm);
            struct vm_val y1  = pop_val(vm);
            struct vm_val x1  = pop_val(vm);
            struct vm_val y0  = pop_val(vm);
            struct vm_val x0  = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_line((int)x0.i, (int)y0.i, (int)x1.i, (int)y1.i, (uint32_t)col.i);
            } else {
                native_win_line(vm, (int)x0.i, (int)y0.i, (int)x1.i, (int)y1.i, (uint32_t)col.i);
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_CIRCLE: {
            struct vm_val col = pop_val(vm);
            struct vm_val r   = pop_val(vm);
            struct vm_val cy  = pop_val(vm);
            struct vm_val cx  = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_circle((int)cx.i, (int)cy.i, (int)r.i, (uint32_t)col.i);
            } else {
                native_win_circle(vm, (int)cx.i, (int)cy.i, (int)r.i, (uint32_t)col.i);
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_PRESENT: {
            vm_steps = 0;
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_present();
                if (!gui_soma_win_is_active()) vm->halted = 1;
            } else {
                native_fb_present();
                if (vm->win.active) {
                    int gx = 0, gy = 0, gb = 0;
                    native_get_mouse_state(&gx, &gy, &gb);
                    if (gb != 0) {
                        int cx = vm->win.x + vm->win.w - 20;
                        int cy = vm->win.y + 5;
                        if (gx >= cx && gx <= cx + 14 && gy >= cy && gy <= cy + 14) {
                            native_win_close(vm);
                            vm->halted = 1;
                        }
                    }
                }
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_CLOSE: {
            if (fb_is_active()) {
                gui_soma_win_close();
            } else {
                native_win_close(vm);
            }
            vm->win.active = 0;
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_ACTIVE: {
            int act = 0;
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) {
                    gui_soma_win_open("SOMA App", 640, 420);
                }
                act = gui_soma_win_is_active();
            } else {
                act = vm->win.active;
            }
            vm->win.active = act;
            struct vm_val res = { VAL_INT, act ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_WIN_MOUSE: {
            int rx = 0, ry = 0, btn = 0, ins = 0, clk = 0;
            int ww = 640, wh = 420;
            if (fb_is_active()) {
                gui_soma_win_get_mouse(&rx, &ry, &btn, &ins, &clk);
                gui_soma_win_get_size(&ww, &wh);
            } else {
                int gx = 0, gy = 0, gb = 0;
                native_get_mouse_state(&gx, &gy, &gb);
                rx = gx; ry = gy; btn = gb;
                if (vm->win.active) {
                    rx = gx - (vm->win.x + 2);
                    ry = gy - (vm->win.y + WIN_TITLE_H + 1);
                    ins = (rx >= 0 && ry >= 0 && rx < vm->win.w - 4 && ry < vm->win.h - WIN_TITLE_H - 3);
                    clk = (gb != 0 && ins);
                }
            }
            int tid_i = vm_alloc_table(vm);
            if (tid_i < 0) {
                struct vm_val nil = { VAL_NIL, 0, {0}, 0, 0 };
                if (!push_val(vm, nil)) return -1;
                break;
            }
            uint16_t tid = (uint16_t)tid_i;
            struct vm_table_obj *tab = &vm->table_pool[tid];
            tab->len = 0;

            struct vm_val k_x = { VAL_STR, 0, "x", 0, 0 };
            struct vm_val v_x = { VAL_INT, (int64_t)rx, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_x, &v_x);

            struct vm_val k_y = { VAL_STR, 0, "y", 0, 0 };
            struct vm_val v_y = { VAL_INT, (int64_t)ry, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_y, &v_y);

            struct vm_val k_w = { VAL_STR, 0, "w", 0, 0 };
            struct vm_val v_w = { VAL_INT, (int64_t)ww, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_w, &v_w);

            struct vm_val k_h = { VAL_STR, 0, "h", 0, 0 };
            struct vm_val v_h = { VAL_INT, (int64_t)wh, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_h, &v_h);

            struct vm_val k_btn = { VAL_STR, 0, "btn", 0, 0 };
            struct vm_val v_btn = { VAL_INT, (int64_t)btn, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_btn, &v_btn);

            struct vm_val k_ins = { VAL_STR, 0, "inside", 0, 0 };
            struct vm_val v_ins = { VAL_INT, ins ? 1 : 0, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_ins, &v_ins);

            struct vm_val k_clk = { VAL_STR, 0, "clicked", 0, 0 };
            struct vm_val v_clk = { VAL_INT, clk ? 1 : 0, {0}, 0, 0 };
            vm_table_set_val(vm, tab, &k_clk, &v_clk);

            struct vm_val res = { VAL_TABLE, 0, {0}, 0, tid };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_FB_IS_ACTIVE: {
            struct vm_val res = { VAL_INT, fb_is_active() ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

                /*PL12_FB_OPS*/
        case OP_SYS_FB_CLEAR: {
            struct vm_val col = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_clear((uint32_t)col.i);
            } else {
                native_fb_clear((uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_RECT: {
            struct vm_val col = pop_val(vm);
            struct vm_val h   = pop_val(vm);
            struct vm_val w   = pop_val(vm);
            struct vm_val y   = pop_val(vm);
            struct vm_val x   = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_rect((int)x.i, (int)y.i, (int)w.i, (int)h.i, (uint32_t)col.i);
            } else {
                native_fb_rect((int)x.i, (int)y.i, (int)w.i, (int)h.i, (uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_PIXEL: {
            struct vm_val col = pop_val(vm);
            struct vm_val y   = pop_val(vm);
            struct vm_val x   = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_pixel((int)x.i, (int)y.i, (uint32_t)col.i);
            } else {
                native_fb_pixel((int)x.i, (int)y.i, (uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_LINE: {
            struct vm_val col = pop_val(vm);
            struct vm_val y1  = pop_val(vm);
            struct vm_val x1  = pop_val(vm);
            struct vm_val y0  = pop_val(vm);
            struct vm_val x0  = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_line((int)x0.i, (int)y0.i, (int)x1.i, (int)y1.i, (uint32_t)col.i);
            } else {
                native_fb_line((int)x0.i, (int)y0.i, (int)x1.i, (int)y1.i, (uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_CIRCLE: {
            struct vm_val col = pop_val(vm);
            struct vm_val r   = pop_val(vm);
            struct vm_val cy  = pop_val(vm);
            struct vm_val cx  = pop_val(vm);
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_circle((int)cx.i, (int)cy.i, (int)r.i, (uint32_t)col.i);
            } else {
                native_fb_circle((int)cx.i, (int)cy.i, (int)r.i, (uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_TEXT: {
            struct vm_val col  = pop_val(vm);
            struct vm_val text = pop_val(vm);
            struct vm_val y    = pop_val(vm);
            struct vm_val x    = pop_val(vm);
            char tbuf[128];
            val_to_str(vm, &text, tbuf, sizeof(tbuf));
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) gui_soma_win_open("SOMA App", 640, 420);
                gui_soma_win_text((int)x.i, (int)y.i, tbuf, (uint32_t)col.i);
            } else {
                native_fb_text((int)x.i, (int)y.i, tbuf, (uint32_t)col.i);
            }
            break;
        }

        case OP_SYS_FB_PRESENT: { vm_steps = 0;
            if (fb_is_active()) {
                gui_soma_win_present();
                if (!gui_soma_win_is_active()) vm->halted = 1;
            } else {
                native_fb_present();
            }
            break;
        }

                        /*PL12_SLEEP_KEY*/
        case OP_SYS_SLEEP: { vm_steps = 0;
            struct vm_val ms = pop_val(vm);
            if (ms.i > 0) {
                thread_sleep((uint32_t)ms.i);
            }
            if (fb_is_active()) {
                if (!gui_soma_win_is_active()) vm->halted = 1;
            } else if (vm->win.active) {
                int gx = 0, gy = 0, gb = 0;
                native_get_mouse_state(&gx, &gy, &gb);
                if (gb != 0) {
                    int cx = vm->win.x + vm->win.w - 20;
                    int cy = vm->win.y + 5;
                    if (gx >= cx && gx <= cx + 14 && gy >= cy && gy <= cy + 14) {
                        native_win_close(vm);
                        vm->halted = 1;
                    }
                }
            }
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_KEY: { vm_steps = 0;
            int ch = 0;
            if (fb_is_active()) {
                if (gui_soma_win_is_active()) {
                    ch = gui_soma_win_get_key();
                } else {
                    vm->halted = 1;
                }
            } else {
                if (kgetc_ready()) ch = (unsigned char)kgetc();
                if (vm->win.active) {
                    int gx = 0, gy = 0, gb = 0;
                    native_get_mouse_state(&gx, &gy, &gb);
                    if (gb != 0) {
                        int cx = vm->win.x + vm->win.w - 20;
                        int cy = vm->win.y + 5;
                        if (gx >= cx && gx <= cx + 14 && gy >= cy && gy <= cy + 14) {
                            native_win_close(vm);
                            vm->halted = 1;
                        }
                    }
                }
            }
            struct vm_val res = { VAL_INT, ch, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_MILLIS: {
            uint64_t ms = timer_get_uptime_ms();
            struct vm_val res = { VAL_INT, (int64_t)ms, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_CLOCK: {                                           /* SOMA_235_PROGRAMADOR: sys.hour() ... sys.year() */
            if (vm->ip + 1 > vm->code_len) return -1;
            uint8_t f = vm->code[vm->ip++];
            struct rtc_time t; rtc_get_datetime(&t);
            int64_t v = f == 0 ? t.hour : f == 1 ? t.min : f == 2 ? t.sec : f == 3 ? t.day : f == 4 ? t.month : t.year;
            struct vm_val res = { VAL_INT, v, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_STR_TO_INT: {
            struct vm_val v = pop_val(vm);
            int64_t val = 0;
            if (v.type == VAL_INT) {
                val = v.i;
            } else if (v.type == VAL_STR) {
                const char *p = v.s;
                while (*p == ' ' || *p == '\t') p++;
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                else if (*p == '+') { p++; }
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                if (neg) val = -val;
            }
            struct vm_val res = { VAL_INT, val, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        /* PARCHE 101: funciones matematicas y de texto basicas */
        case OP_ABS: {
            struct vm_val a = pop_val(vm);
            struct vm_val res = { VAL_INT, (a.i < 0) ? -a.i : a.i, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_MIN: case OP_MAX: {
            struct vm_val b = pop_val(vm), a = pop_val(vm);
            int64_t r = (op == OP_MIN) ? (a.i < b.i ? a.i : b.i) : (a.i > b.i ? a.i : b.i);
            struct vm_val res = { VAL_INT, r, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_CLAMP: {
            struct vm_val hi = pop_val(vm), lo = pop_val(vm), x = pop_val(vm);
            int64_t r = x.i;
            if (r < lo.i) r = lo.i;
            if (r > hi.i) r = hi.i;
            struct vm_val res = { VAL_INT, r, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_SQRT: {
            struct vm_val a = pop_val(vm);
            int64_t n = a.i, r = 0;
            if (n > 0) { r = 0; while ((r + 1) * (r + 1) <= n && r < 3037000499LL) r++; }
            struct vm_val res = { VAL_INT, r, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_STR_FIND: {
            struct vm_val sub = pop_val(vm), str = pop_val(vm);
            int64_t pos = -1;
            if (str.type == VAL_STR && sub.type == VAL_STR) {
                const char *h = vm_sdata(vm, &str), *nd = vm_sdata(vm, &sub);
                uint32_t hl = vm_slen(vm, &str), nl = vm_slen(vm, &sub);
                if (nl == 0) pos = 0;
                else for (uint32_t i = 0; i + nl <= hl; i++) {
                    uint32_t k = 0; while (k < nl && h[i + k] == nd[k]) k++;
                    if (k == nl) { pos = (int64_t)i; break; }
                }
            }
            struct vm_val res = { VAL_INT, pos, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_CHR: {
            struct vm_val a = pop_val(vm);
            struct vm_val res;
            res.type = VAL_STR; res.i = 0; res.arr_id = 0; res.tab_id = 0; res.s[0] = '\0';
            char ch[2]; ch[0] = (char)(a.i & 0x7F); ch[1] = 0;
            if (ch[0]) vm_str_make2(vm, &res, ch, 1, "", 0);
            if (!push_val(vm, res)) return -1;
            break;
        }
        case OP_ORD: {
            struct vm_val idx = pop_val(vm), str = pop_val(vm);
            int64_t r = 0;
            if (str.type == VAL_STR && idx.i >= 0 && idx.i < (int64_t)vm_slen(vm, &str)) r = (uint8_t)vm_sdata(vm, &str)[idx.i];
            struct vm_val res = { VAL_INT, r, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_STR_SUB: {
            struct vm_val v_len = pop_val(vm);
            struct vm_val v_start = pop_val(vm);
            struct vm_val v_str = pop_val(vm);
            struct vm_val res;
            res.type = VAL_STR;
            res.i = 0;
            res.arr_id = 0;
            res.s[0] = '\0';
            if (v_str.type == VAL_STR) { /*PL3_SUB*/
                const char *sd = vm_sdata(vm, &v_str);
                uint32_t slen = vm_slen(vm, &v_str);
                int64_t st = v_start.i;
                int64_t ln = v_len.i;
                if (st < 0) st = 0;
                if (st < (int64_t)slen && ln > 0) {
                    uint32_t count = (ln > (int64_t)slen) ? slen : (uint32_t)ln;
                    if ((uint32_t)st + count > slen) count = slen - (uint32_t)st;
                    vm->tmp_roots[0] = v_str;
                    vm_str_make2(vm, &res, sd + st, count, "", 0);
                    vm->tmp_roots[0].type = VAL_NIL;
                }
            }
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_PRINT: {
            struct vm_val top = pop_val(vm);
            char pbuf[512]; /*PL11_BUFP*/
            if (top.type == VAL_STR) { /*PL3_PRINT*/
                kprint(vm_sdata(vm, &top));
            } else {
                val_to_str(vm, &top, pbuf, sizeof(pbuf));
                kprint(pbuf);
            }
            kprint("\n");
            break;
        }

        case OP_SYS_INPUT: { vm_steps = 0; /*PL1_STEP_INPUT*/
            struct vm_val prompt = pop_val(vm);
            if (prompt.type == VAL_STR && prompt.s[0] != '\0') {
                kprint(prompt.s);
            }

            struct tcb *cur_t = thread_current();
            int saved_agent = cur_t ? cur_t->agent_mode : 0;
            if (cur_t) cur_t->agent_mode = 0;

            char in_buf[128];
            int n = kgetline(in_buf, sizeof(in_buf) - 1);

            if (cur_t) cur_t->agent_mode = saved_agent;

            if (n < 0) n = 0;
            in_buf[n] = '\0';

            char *p = in_buf;
            while (*p == ' ') p++;
            uint32_t plen_in = 0; /*PL1_INPUTEND*/
            while (p[plen_in]) plen_in++;
            char *end = plen_in ? (p + plen_in - 1) : p;
            while (end >= p && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end = '\0';
                end--;
            }

            int is_num = 1;
            int is_neg = 0;
            char *scan = p;
            if (*scan == '-') {
                is_neg = 1;
                scan++;
            }
            if (*scan == '\0') is_num = 0;
            while (*scan) {
                if (*scan < '0' || *scan > '9') {
                    is_num = 0;
                    break;
                }
                scan++;
            }

            struct vm_val res;
            res.arr_id = 0;
            if (is_num) {
                int64_t val = 0;
                scan = is_neg ? p + 1 : p;
                while (*scan >= '0' && *scan <= '9') {
                    val = val * 10 + (*scan - '0');
                    scan++;
                }
                if (is_neg) val = -val;
                res.type = VAL_INT;
                res.i = val;
                res.s[0] = '\0';
            } else {
                res.type = VAL_STR;
                res.i = 0;
                uint32_t len_in = 0;
                while (p[len_in] && len_in < sizeof(res.s) - 1) {
                    res.s[len_in] = p[len_in];
                    len_in++;
                }
                res.s[len_in] = '\0';
            }

            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_RAM: {
            size_t f_free = 0, f_used = 0, f_total = 0;
            pmm_get_stats(&f_free, &f_used, &f_total);
            struct vm_val v = { VAL_INT, (int64_t)(f_free * 4096), {0}, 0, 0 };
            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_SYS_UPTIME: {
            uint64_t up_s = timer_get_uptime_ms() / 1000; /*PL11_UPTIME*/
            struct vm_val v = { VAL_INT, (int64_t)up_s, {0}, 0, 0 };
            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_SYS_RAND: {
            uint32_t r = vm_rand31();
            struct vm_val v = { VAL_INT, (int64_t)r, {0}, 0, 0 };
            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_SYS_RANDRANGE: {
            struct vm_val hi = pop_val(vm);
            struct vm_val lo = pop_val(vm);
            int64_t span = (int64_t)((uint64_t)hi.i - (uint64_t)lo.i + 1u);
            if (span <= 0) span = 1;
            int64_t r = (int64_t)((uint64_t)lo.i + (vm_rand31() % (uint64_t)span));
            struct vm_val res = { VAL_INT, r, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_CLEAR: {
            console_clear();
            struct vm_val res = { VAL_INT, 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_VFS_READ: { /*PL3_VFSREAD*/
            struct vm_val path = pop_val(vm);
            struct vm_val v;
            v.type = VAL_STR;
            v.i = 0;
            v.arr_id = 0;
            v.s[0] = '\0';

            int fsz = vfs_size(path.s); if (fsz < FS_DATA_MAX) fsz = FS_DATA_MAX;   /* PARCHE 087 */
            char *fbuf = (char *)kmalloc((size_t)fsz + 1);
            if (fbuf) {
                int r = vfs_read(path.s, fbuf, (uint32_t)fsz + 1);
                if (r <= 0) {
                    if (path.s[0] != '/') {
                        char alt[64];
                        alt[0] = '/';
                        uint32_t k = 0;
                        while (path.s[k] && k < sizeof(alt) - 2) {
                            alt[k + 1] = path.s[k];
                            k++;
                        }
                        alt[k + 1] = '\0';
                        r = vfs_read(alt, fbuf, FS_DATA_MAX);
                    } else {
                        r = vfs_read(path.s + 1, fbuf, FS_DATA_MAX);
                    }
                }
                if (r > 0) {
                    fbuf[r] = '\0';
                    vm_str_make2(vm, &v, fbuf, (uint32_t)r, "", 0);
                }
                kfree(fbuf);
            }

            if (!push_val(vm, v)) return -1;
            break;
        }

        case OP_SYS_VFS_WRITE: {
            struct vm_val data = pop_val(vm);
            struct vm_val path = pop_val(vm);

            char norm[64];
            if (path.s[0] != '/') {
                norm[0] = '/';
                uint32_t k = 0;
                while (path.s[k] && k < sizeof(norm) - 2) {
                    norm[k + 1] = path.s[k];
                    k++;
                }
                norm[k + 1] = '\0';
            } else {
                uint32_t k = 0;
                while (path.s[k] && k < sizeof(norm) - 1) {
                    norm[k] = path.s[k];
                    k++;
                }
                norm[k] = '\0';
            }

            char dbuf_small[128]; /*PL3_VFSWRITE*/
            const char *dbuf;
            uint32_t dlen = 0;
            if (data.type == VAL_STR) {
                dbuf = vm_sdata(vm, &data);
                dlen = vm_slen(vm, &data);
            } else {
                val_to_str(vm, &data, dbuf_small, sizeof(dbuf_small));
                dbuf = dbuf_small;
                while (dbuf_small[dlen]) dlen++;
            }

            int r;
            struct tcb *wt = thread_current();
            if (wt && wt->agent_mode && vm_is_protected_path(norm)) {
                kprint("[VM SEGURIDAD] Escritura denegada: el Agente no puede modificar scripts de arranque.\n");
                r = -1;
            } else {
                r = vfs_write(norm, dbuf, dlen);
                if (r < 0) {
                    r = vfs_create(norm, dbuf);
                }
            }
            struct vm_val res = { VAL_INT, (r >= 0) ? 1 : 0, {0}, 0, 0 };
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_VFS_EXISTS: {
            struct vm_val path = pop_val(vm);
            static char dummy[512];
            int r = vfs_read(path.s, dummy, sizeof(dummy) - 1);
            if (r < 0) { /*PL11_EXISTS1*/
                if (path.s[0] != '/') {
                    char alt[64];
                    alt[0] = '/';
                    uint32_t k = 0;
                    while (path.s[k] && k < sizeof(alt) - 2) {
                        alt[k + 1] = path.s[k];
                        k++;
                    }
                    alt[k + 1] = '\0';
                    r = vfs_read(alt, dummy, sizeof(dummy) - 1);
                } else {
                    r = vfs_read(path.s + 1, dummy, sizeof(dummy) - 1);
                }
            }
            struct vm_val res = { VAL_INT, (r >= 0) ? 1 : 0, {0}, 0, 0 }; /*PL11_EXISTS2*/
            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_SYS_AGENT_ASK: { vm_steps = 0; /*PL1_STEP_ASK*/
            struct vm_val prompt = pop_val(vm);
            struct vm_val ag_name = pop_val(vm);
            const char *name = (ag_name.s[0] != '\0') ? ag_name.s : "soma";

            kprint("[SOMA-LANG] Invocando agente '");
            kprint(name);
            kprint("'...\n");

            vm->tmp_roots[0] = prompt; /*PL3_ASKPROMPT*/
            agent_run(name, vm_sdata(vm, &prompt));
            vm->tmp_roots[0].type = VAL_NIL;

            struct vm_val resp;
            resp.type = VAL_STR;
            resp.i = 0;
            resp.arr_id = 0;
            resp.s[0] = '\0';

            if (agent_last_response[0] != '\0') {
                uint32_t rl_ = 0; /*PL3_ASKRESP*/
                while (agent_last_response[rl_]) rl_++;
                vm_str_make2(vm, &resp, agent_last_response, rl_, "", 0);
            } else {
                const char *def = "Agente ejecuto mision.";
                uint32_t len_resp = 0;
                while (def[len_resp] && len_resp < sizeof(resp.s) - 1) {
                    resp.s[len_resp] = def[len_resp];
                    len_resp++;
                }
                resp.s[len_resp] = '\0';
            }

            if (!push_val(vm, resp)) return -1;
            break;
        }

        case OP_SYS_MEM_QUERY: {
            struct vm_val query = pop_val(vm);
            struct vm_val res;
            res.type = VAL_STR;
            res.i = 0;
            res.arr_id = 0;
            res.s[0] = '\0';

            static char bm25_buf[2048]; /*PL3_BM25BUF*/
            int r = vfs_search_bm25(query.s, "/mem", bm25_buf, sizeof(bm25_buf) - 1);
            if (r > 0) {
                uint32_t bl_ = 0; /*PL3_BM25COPY*/
                while (bm25_buf[bl_]) bl_++;
                vm_str_make2(vm, &res, bm25_buf, bl_, "", 0);
            } else {
                const char *empty = "Sin coincidencias semanticas en /mem";
                uint32_t len_q = 0;
                while (empty[len_q] && len_q < sizeof(res.s) - 1) {
                    res.s[len_q] = empty[len_q];
                    len_q++;
                }
                res.s[len_q] = '\0';
            }

            if (!push_val(vm, res)) return -1;
            break;
        }

        case OP_DUP2: { /*PL1_DUP2CASE*/
            if (vm->sp < 2) return -1;
            struct vm_val d2a = vm->stack[vm->sp - 2];
            struct vm_val d2b = vm->stack[vm->sp - 1];
            if (!push_val(vm, d2a)) return -1;
            if (!push_val(vm, d2b)) return -1;
            break;
        }

        default:
            kprint("[VM ERROR] Opcode desconocido: 0x");
            kprint_hex8(op);
            kprint("\n");
            return -1;
        }
    }

    return 0;
}

/*PL3_RUNWRAP_B*/
uint64_t vm_step_limit = VM_MAX_STEPS;
int vm_run(struct soma_vm *vm, const uint8_t *bytecode, uint32_t len)
{
    struct soma_vm *prev = g_vm_cur;
    g_vm_cur = vm;
    int r = vm_run_inner(vm, bytecode, len);
    g_vm_cur = prev;
    return r;
}

void vm_release(struct soma_vm *vm)
{
    for (int i = 0; i < VM_STR_SLOTS; ++i) {
        if (vm->str_used[i] && vm->str_ptr[i]) kfree(vm->str_ptr[i]);
        vm->str_ptr[i] = 0; vm->str_used[i] = 0; vm->str_mark[i] = 0; vm->str_len[i] = 0;
    }
    if (vm->array_pool) {
        for (int i = 0; i < vm->array_pool_cap; ++i) if (vm->array_pool[i].items) kfree(vm->array_pool[i].items);
        kfree(vm->array_pool);
    }
    vm->array_pool = 0; vm->array_pool_cap = 0; vm->array_pool_count = 0;
    if (vm->table_pool) { /*PL4_RELTAB*/
        for (int i = 0; i < vm->table_pool_cap; ++i) {
            if (vm->table_pool[i].entries) kfree(vm->table_pool[i].entries);
            if (vm->table_pool[i].hidx) kfree(vm->table_pool[i].hidx);
        }
        kfree(vm->table_pool);
    }
    vm->table_pool = 0; vm->table_pool_cap = 0; vm->table_pool_count = 0;
    vm->mem_used = 0;
    vm->win.active = 0;
}

/* PARCHE 029: la VM de pruebas (~460 KB) ya no vive en .bss: se pide al montículo solo mientras dura la prueba. */
#define test_vm (*vmp)
static int vm_test_self_impl(struct soma_vm *vmp)
{
    kprint("\n[VM AUTO-TEST] Verificando Bytecode Engine nativo en Ring 0...\n");
    vm_init(&test_vm);

    uint8_t prog[] = {
        OP_PUSH_INT, 10, 0, 0, 0, 0, 0, 0, 0,
        OP_PUSH_INT, 25, 0, 0, 0, 0, 0, 0, 0,
        OP_ADD,
        OP_PUSH_INT, 2,  0, 0, 0, 0, 0, 0, 0,
        OP_MUL,
        OP_HALT
    };

    if (vm_run(&test_vm, prog, sizeof(prog)) != 0) { vm_release(&test_vm); return 0; }
    struct vm_val res = pop_val(&test_vm);
    vm_release(&test_vm);                                           /* SOMA_264_LANG */
    if (res.type != VAL_INT || res.i != 70) return 0;

    vm_init(&test_vm);
    uint8_t arr_prog[] = {
        OP_PUSH_INT, 11, 0, 0, 0, 0, 0, 0, 0,
        OP_PUSH_INT, 22, 0, 0, 0, 0, 0, 0, 0,
        OP_NEW_ARRAY, 2, 0,
        OP_PUSH_INT, 0,  0, 0, 0, 0, 0, 0, 0,
        OP_ARRAY_GET,
        OP_HALT
    };
    if (vm_run(&test_vm, arr_prog, sizeof(arr_prog)) != 0) { vm_release(&test_vm); return 0; }
    struct vm_val ares = pop_val(&test_vm);
    vm_release(&test_vm);
    if (ares.type != VAL_INT || ares.i != 11) return 0;

    kprint("  Operaciones en pila, logica booleana y arrays en Ring 0 OK\n");
    kprint("[VM AUTO-TEST] SUPERADO CON EXITO.\n\n");
    return 1;
}
#undef test_vm

int vm_test_self(void)
{
    struct soma_vm *v = (struct soma_vm *)kmalloc(sizeof(struct soma_vm));
    if (!v) { kprint("[VM AUTO-TEST] sin memoria para la VM de pruebas\n"); return 0; }
    int r = vm_test_self_impl(v);
    kfree(v);
    return r;
}
