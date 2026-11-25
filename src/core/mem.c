#include <stddef.h>
#include <stdint.h>

#include "mem.h"
#include "vmm.h"
#include "pmm.h"
#include "console.h"

#ifndef SOMA_HOST_TEST /* en pruebas en host se usa la libc */
#if defined(__x86_64__)
#include "memfn.h"            /* SOMA_199: memcpy, memmove, memset y memcmp de x86_64 */
#else
#include "memfn_gen.h"        /* SOMA_308_EFICIENCIA: aarch64 de 8 en 8 bytes (antes, byte a byte) y solo accesos alineados */
#endif /* x86_64 */
#endif /* SOMA_HOST_TEST */

/*
 * Heap Dinamico de MYOS respaldado por el VMM y PMM
 * Base Virtual: KHEAP_START (x86_64: 256 GiB desde el parche 157; desacoplado de direcciones fisicas fijas)
 * Proteccion de Hardware: Paginas de 4 KiB con bits PRESENT | WRITABLE | NX
 *
 * SOMA_PARCHE_005_HEAP: endurecimiento del asignador
 *   - magic por estado en cada cabecera (detecta cabeceras pisadas y double free)
 *   - cola de control tras los datos pedidos (detecta desbordamientos de buffer)
 *   - kheap_check(): el heap debe cubrir toda la region mapeada sin huecos
 *   - contadores (fugas, pico de uso, fallos) en kheap_get_counters()
 *   - tamanos imposibles se rechazan antes de operar con ellos (antes envolvian)
 */
#ifndef KHEAP_START
#ifdef __x86_64__
/* SOMA_157_T480S: el montón se muda de 0x20000000 (dentro del primer GiB proyectado en identidad, con sitio para 256 MiB) a una
 * ventana propia a 256 GiB, lejos de la identidad, de las tablas del firmware (1-4 GiB) y del framebuffer: puede crecer varios
 * GiB. Sigue dentro de PML4[0], que comparten todos los espacios de direcciones. Sus marcos salen de la reserva ALTA del PMM. */
#define KHEAP_START        0x0000004000000000ULL
#define KHEAP_FRAME()      pmm_alloc_frame_high()
#else
#include "platform.h"
#define KHEAP_START        SOMA_HEAP_START /* PARCHE 060: por plataforma (QEMU virt: 0x41000000; Pi 5: 0x18000000) */
#endif
#endif
#ifndef KHEAP_FRAME
#define KHEAP_FRAME()      pmm_alloc_frame()
#endif
#ifndef KHEAP_MAX_SIZE
#define KHEAP_MAX_SIZE     (64 * 1024 * 1024ULL) /* Limite de expansion: 64 MiB */
#endif
/* PARCHE 086: en x86_64 el limite crece con la RAM (la mitad, hasta 256 MiB) si la RAM fisica queda por debajo de la ventana virtual del
 * heap (0x20000000); asi una pagina web pesada no se queda sin memoria para sus imagenes. En aarch64 sigue el limite fijo del mapa. */
static size_t kheap_limit = KHEAP_MAX_SIZE;
size_t kheap_max_bytes(void) { return kheap_limit; }
#define KHEAP_INIT_BYTES   (16 * 1024ULL)        /* 16 KiB iniciales (4 paginas) */

#define KHEAP_MAGIC_ALLOC  0xA110CA7Eu
#define KHEAP_MAGIC_FREE   0xF4EEB10Cu
#define KHEAP_TAIL_BYTE    0xA5u
#define KHEAP_TAIL_MIN     8u     /* minimo de bytes de control tras los datos */
#define KHEAP_POISON_BYTE  0xDDu  /* solo con -DSOMA_KHEAP_POISON */

struct block_header {
    size_t size;                /* bytes del bloque tras la cabecera (multiplo de 16, incluye la cola) */
    uint32_t is_free;
    uint32_t magic;             /* KHEAP_MAGIC_ALLOC / KHEAP_MAGIC_FREE segun is_free */
    struct block_header *next;
    uint64_t req;               /* bytes pedidos por el llamador (0 si esta libre) */
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct block_header) == 32, "block_header debe ser exactamente de 32 bytes");

/* PARCHE 089: el byte alto de req guarda el "dueno" del bloque (aplicacion o subsistema) para el Monitor del sistema */
#define REQ_MASK  0x00FFFFFFFFFFFFFFULL
#define REQ(b)    ((b)->req & REQ_MASK)
volatile uint8_t kheap_owner;   /* dueno actual: lo cambia el planificador al conmutar y la GUI al dar paso a cada aplicacion */
static struct block_header *heap_head = 0;
static uint64_t heap_current_brk = 0;
static size_t   heap_mapped_bytes = 0;
static int      heap_initialized = 0;
static struct kheap_counters heap_ctr;
static size_t   heap_free_bytes;   /* PARCHE 128: suma de `size` de los bloques libres, al dia en bin_insert/bin_remove (kheap_stats ya no recorre el monton) */

/* PARCHE 030: indices de bloques libres. Antes kmalloc y kfree recorrian la lista entera (coste O(n) por llamada: con
 * 20.000 bloques vivos, ~0,5 ms por par kfree+kmalloc en el anfitrion). Ahora los libres cuelgan de listas por tamano
 * (exactas hasta 1 KiB y 4 por potencia de 2 por encima; mapa de bits para saltar a la primera no vacia), y cada
 * bloque libre lleva al final de su zona de datos un nodo {siguiente, anterior, direccion de su cabecera}. La direccion
 * del bloque anterior sale del "pie" que dejan los libres (se valida entera antes de fiarse). Cabecera sin cambios. */
#define KHEAP_MIN_PAYLOAD  32u
#define KHEAP_NBINS        136
struct fnode { struct block_header *fnext; struct block_header *fprev; uint64_t self; };
_Static_assert(sizeof(struct fnode) == 24, "fnode debe ocupar 24 bytes");
static struct block_header *heap_bins[KHEAP_NBINS];
static uint64_t heap_bin_mask[3];
static struct block_header *heap_tail = 0;

static void kheap_report(const char *what, const void *where)
{
    heap_ctr.corruptions++;
    kprint("KHEAP: CORRUPCION: ");
    kprint(what);
    kprint(" (bloque 0x");
    kprint_hex32((uint32_t)(uintptr_t)where);
    kprint(")\n");
}

static int hdr_in_heap(const struct block_header *b)
{
    uintptr_t a = (uintptr_t)b;
    return (a & 0x0FULL) == 0 &&
           a >= (uintptr_t)KHEAP_START &&
           a + sizeof(struct block_header) <= (uintptr_t)heap_current_brk;
}

/* Cabecera coherente: dentro del heap, tamano sano y magic acorde a su estado. */
static int block_valid(const struct block_header *b)
{
    if (!hdr_in_heap(b)) return 0;
    if (b->size == 0 || (b->size & 0x0FULL) != 0) return 0;
    if ((uintptr_t)(b + 1) + b->size > (uintptr_t)heap_current_brk) return 0;
    if (b->is_free == 1) return b->magic == KHEAP_MAGIC_FREE;
    if (b->is_free == 0) return b->magic == KHEAP_MAGIC_ALLOC &&
                                REQ(b) > 0 && REQ(b) + KHEAP_TAIL_MIN <= b->size;
    return 0;
}

static int tail_intact(const struct block_header *b)
{
    const uint8_t *p = (const uint8_t *)(b + 1);
    for (size_t i = (size_t)REQ(b); i < b->size; ++i) {
        if (p[i] != KHEAP_TAIL_BYTE) return 0;
    }
    return 1;
}

static void heap_account_alloc(const struct block_header *b)
{
    heap_ctr.live_blocks++;
    heap_ctr.used_bytes += b->size + sizeof(struct block_header);
    if (heap_ctr.used_bytes > heap_ctr.peak_used_bytes) {
        heap_ctr.peak_used_bytes = heap_ctr.used_bytes;
    }
}

static inline struct fnode *FN(const struct block_header *b)
{
    return (struct fnode *)((uint8_t *)(b + 1) + b->size - sizeof(struct fnode));
}

static unsigned bin_of(size_t size)
{
    unsigned k;
    if (size <= 1024) return (unsigned)(size >> 4);
    unsigned l = 63u - (unsigned)__builtin_clzll((unsigned long long)size);
    k = 65u + (l - 10u) * 4u + (unsigned)((size >> (l - 2u)) & 3u);
    return k >= KHEAP_NBINS ? KHEAP_NBINS - 1 : k;
}

/* Bloque libre coherente y presente en su lista. */
static int fnode_ok(const struct block_header *b)
{
    return hdr_in_heap(b) && block_valid(b) && b->is_free == 1 && b->size >= KHEAP_MIN_PAYLOAD &&
           FN(b)->self == (uint64_t)(uintptr_t)b;
}

static void bin_insert(struct block_header *b)
{
    b->is_free = 1; b->magic = KHEAP_MAGIC_FREE; b->req = 0;
    unsigned k = bin_of(b->size);
    struct fnode *f = FN(b);
    f->self = (uint64_t)(uintptr_t)b;
    f->fprev = 0;
    f->fnext = heap_bins[k];
    if (f->fnext) FN(f->fnext)->fprev = b;
    heap_bins[k] = b;
    heap_bin_mask[k >> 6] |= 1ULL << (k & 63);
    heap_free_bytes += b->size;
}

static int bin_remove(struct block_header *b)
{
    struct fnode *f = FN(b);
    struct block_header *p = f->fprev, *n = f->fnext;
    unsigned k = bin_of(b->size);
    if ((p && !fnode_ok(p)) || (n && !fnode_ok(n)) || (!p && heap_bins[k] != b)) {
        kheap_report("lista de libres danada", b);
        return 0;
    }
    if (p) FN(p)->fnext = n; else heap_bins[k] = n;
    if (n) FN(n)->fprev = p;
    if (!heap_bins[k]) heap_bin_mask[k >> 6] &= ~(1ULL << (k & 63));
    heap_free_bytes -= b->size;
    return 1;
}

/* Primer bloque libre que cabe (ajuste casi exacto en pequenos; en grandes, primero de su rango y si no el siguiente rango). */
static struct block_header *bin_find(size_t size)
{
    unsigned k = bin_of(size);
    if (k > 64) {
        for (struct block_header *b = heap_bins[k]; b; b = FN(b)->fnext) {
            if (!fnode_ok(b)) { kheap_report("lista de libres danada al buscar", b); return 0; }
            if (b->size >= size) return b;
        }
        k++;
    }
    for (unsigned w = k >> 6; w < 3; ++w) {
        uint64_t m = heap_bin_mask[w];
        if (w == (k >> 6)) m &= ~0ULL << (k & 63);
        if (m) return heap_bins[w * 64 + (unsigned)__builtin_ctzll(m)];
    }
    return 0;
}

static int kheap_expand(size_t min_bytes)
{
    size_t pages_needed = (min_bytes + VMM_PAGE_SIZE - 1) / VMM_PAGE_SIZE;
    if (pages_needed == 0) pages_needed = 1;

    if (min_bytes > kheap_limit ||
        heap_mapped_bytes + (pages_needed * VMM_PAGE_SIZE) > kheap_limit) {
        return 0;
    }

    uint64_t new_pages_start = heap_current_brk;

    for (size_t i = 0; i < pages_needed; ++i) {
        uintptr_t frame = KHEAP_FRAME();
        if (!frame) {
            for (size_t j = 0; j < i; ++j) {
                uint64_t va = new_pages_start + j * VMM_PAGE_SIZE;
                uint64_t pa = vmm_virt_to_phys(va);
                vmm_unmap_page(va);
                if (pa) pmm_free_frame(pa);
            }
            return 0;
        }

        if (vmm_map_page(new_pages_start + i * VMM_PAGE_SIZE,
                         (uint64_t)frame,
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX) != 0) {
            pmm_free_frame(frame);
            for (size_t j = 0; j < i; ++j) {
                uint64_t va = new_pages_start + j * VMM_PAGE_SIZE;
                uint64_t pa = vmm_virt_to_phys(va);
                vmm_unmap_page(va);
                if (pa) pmm_free_frame(pa);
            }
            return 0;
        }
    }

    struct block_header *new_block = (struct block_header *)(uintptr_t)new_pages_start;
    new_block->size = (pages_needed * VMM_PAGE_SIZE) - sizeof(struct block_header);
    new_block->next = 0;

    /* La region mapeada crece antes de validar cabeceras que apunten a ella. */
    heap_current_brk += pages_needed * VMM_PAGE_SIZE;
    heap_mapped_bytes += pages_needed * VMM_PAGE_SIZE;

    if (!heap_head) {
        heap_head = heap_tail = new_block;
        bin_insert(new_block);
    } else {
        struct block_header *curr = heap_tail;
        /* Si el ultimo bloque era contiguo y libre, fusionar */
        if (curr->is_free && ((uint8_t *)(curr + 1) + curr->size == (uint8_t *)new_block) && bin_remove(curr)) {
            curr->size += pages_needed * VMM_PAGE_SIZE;
            new_block->magic = 0;
            bin_insert(curr);
        } else {
            curr->next = new_block;
            heap_tail = new_block;
            bin_insert(new_block);
        }
    }
    return 1;
}

void kheap_init(void)
{
    if (heap_initialized) return;

#if defined(__x86_64__) && !defined(SOMA_HOST_TEST)
    {   /* PARCHE 086 / SOMA_157_T480S: limite segun la RAM: la mitad, hasta 8 GiB (el T480s de 16 GiB: 7,8 GiB; antes 256 MiB).
         * Con RAM de 512 MiB o menos queda como antes (la mitad, como mucho 256 MiB). */
        size_t fr = 0, us = 0, tot = 0; pmm_get_stats(&fr, &us, &tot);
        uint64_t ram = (uint64_t)tot * 4096u;
        if (ram) {
            uint64_t lim = ram / 2;
            if (ram <= 0x20000000ULL && lim > (256ULL << 20)) lim = 256ULL << 20;
            if (lim > (8ULL << 30)) lim = 8ULL << 30;
            if (lim > kheap_limit) kheap_limit = (size_t)lim;
        }
    }
#endif
    heap_current_brk  = KHEAP_START;
    heap_mapped_bytes = 0;
    heap_head         = 0;
    heap_tail         = 0;
    memset(heap_bins, 0, sizeof(heap_bins));
    memset(heap_bin_mask, 0, sizeof(heap_bin_mask));
    memset(&heap_ctr, 0, sizeof(heap_ctr));

    if (!kheap_expand(KHEAP_INIT_BYTES)) {
        kprint("KHEAP ERROR: Fallo al asignar paginas iniciales del Heap\n");
        return;
    }

    heap_initialized = 1;
    kprint("KHEAP: Inicializado en 0x");
    if ((uint64_t)KHEAP_START >> 32) kprint_hex32((uint32_t)((uint64_t)KHEAP_START >> 32));   /* SOMA_157: base de 64 bits */
    kprint_hex32((uint32_t)KHEAP_START);
    kprint(" (NX activo, ");
    kprint_dec((uint32_t)(heap_mapped_bytes / 1024));
    kprint(" KiB mapeados, hasta ");
    kprint_dec((uint32_t)(kheap_limit >> 20));
    kprint(" MiB)\n");
}

static void *kmalloc_impl(size_t size)
{
    if (!heap_initialized) {
        kheap_init();
    }

    if (size == 0) return 0;

    heap_ctr.alloc_calls++;

    /* Antes de redondear: un tamano enorme envolvia a 0 y devolvia un bloque minimo. */
    if (size > kheap_limit) {
        heap_ctr.failed_allocs++;
        return 0;
    }

    size_t req = size;
    /* Datos + cola de control, alineado a 16 bytes estricta (minimo 32: cabe el nodo de libres) */
    size = (size + KHEAP_TAIL_MIN + 15) & ~15ULL;
    if (size < KHEAP_MIN_PAYLOAD) size = KHEAP_MIN_PAYLOAD;

    for (int attempt = 0; attempt < 2; ++attempt) {
        struct block_header *curr = bin_find(size);
        if (curr) {
            if (!fnode_ok(curr) || curr->size < size || !bin_remove(curr)) {
                kheap_report("bloque libre invalido en kmalloc", curr);
                heap_ctr.failed_allocs++;
                return 0;
            }
            if (curr->size >= size + sizeof(struct block_header) + 32) {
                struct block_header *new_block = (struct block_header *)((uint8_t *)(curr + 1) + size);
                new_block->size = curr->size - size - sizeof(struct block_header);
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
                if (heap_tail == curr) heap_tail = new_block;
                bin_insert(new_block);
            }

            curr->is_free = 0;
            curr->magic = KHEAP_MAGIC_ALLOC;
            curr->req = req | ((uint64_t)kheap_owner << 56);
            memset((uint8_t *)(curr + 1) + req, (int)KHEAP_TAIL_BYTE, curr->size - req);
            heap_account_alloc(curr);
            return (void *)(curr + 1);
        }

        /* Si no cabe, expandir el heap dinamicamente pidiendo paginas al VMM */
        if (attempt == 0) {
            if (!kheap_expand(size + sizeof(struct block_header))) {
                heap_ctr.failed_allocs++;
                return 0; /* OOM */
            }
        }
    }

    heap_ctr.failed_allocs++;
    return 0;
}

static void kfree_impl(void *ptr)
{
    if (!ptr || !heap_initialized) return;

    uintptr_t raw = (uintptr_t)ptr;
    heap_ctr.free_calls++;

    /* Nunca interpretar como cabecera un puntero arbitrario. */
    if ((raw & 0x0FULL) != 0 ||
        raw < KHEAP_START + sizeof(struct block_header) ||
        raw >= heap_current_brk) {
        heap_ctr.rejected_frees++;
        kprint("KHEAP: kfree rechazado: puntero fuera del heap.\n");
        return;
    }

    /* PARCHE 030: la cabecera esta justo antes del puntero (antes se buscaba recorriendo toda la lista). */
    struct block_header *block = (struct block_header *)(raw - sizeof(struct block_header));

    if (block->magic == KHEAP_MAGIC_FREE && block->is_free == 1) {
        heap_ctr.rejected_frees++;
        kprint("KHEAP: kfree rechazado: double free.\n");
        return;
    }
    if (block->magic != KHEAP_MAGIC_ALLOC || block->is_free != 0) {
        heap_ctr.rejected_frees++;
        kprint("KHEAP: kfree rechazado: sin cabecera valida (cabecera danada o puntero ajeno).\n");
        return;
    }
    {   /* el sucesor fisico debe encajar: descarta cabeceras falsas dentro de datos de usuario */
        uint8_t *end = (uint8_t *)(block + 1) + block->size;
        int ok = block_valid(block) &&
                 (block->next ? ((uint8_t *)block->next == end && hdr_in_heap(block->next))
                              : ((uintptr_t)end == heap_current_brk));
        if (!ok) {
            heap_ctr.rejected_frees++;
            kheap_report("cabecera danada en kfree (no se libera)", block);
            return;
        }
    }

    if (!tail_intact(block)) {
        /* El llamador escribio mas alla de lo pedido; se libera igualmente. */
        kheap_report("desbordamiento de buffer detectado en kfree", block);
    }

    heap_ctr.live_blocks--;
    heap_ctr.used_bytes -= block->size + sizeof(struct block_header);

#ifdef SOMA_KHEAP_POISON
    memset(block + 1, (int)KHEAP_POISON_BYTE, block->size);
#endif

    /* Fusion con el vecino de delante (si esta libre) */
    struct block_header *nx = block->next;
    if (nx) {
        if (!block_valid(nx)) {
            kheap_report("cabecera invalida al fusionar bloques libres", nx);
        } else if (nx->is_free && fnode_ok(nx) && bin_remove(nx)) {
            block->size += sizeof(struct block_header) + nx->size;
            block->next = nx->next;
            nx->magic = 0;
            if (heap_tail == nx) heap_tail = block;
        }
    }

    /* ... y con el de detras: su direccion sale del pie que dejan los bloques libres, y se valida entera */
    if ((uintptr_t)block > (uintptr_t)KHEAP_START) {
        uint64_t self = *(uint64_t *)(void *)((uint8_t *)block - sizeof(uint64_t));
        struct block_header *pv = (struct block_header *)(uintptr_t)self;
        if ((uintptr_t)pv < (uintptr_t)block && fnode_ok(pv) && pv->next == block &&
            (uint8_t *)(pv + 1) + pv->size == (uint8_t *)block && bin_remove(pv)) {
            pv->size += sizeof(struct block_header) + block->size;
            pv->next = block->next;
            block->magic = 0;
            if (heap_tail == block) heap_tail = pv;
            block = pv;
        }
    }
    bin_insert(block);
}

/* PARCHE 103 (Fase 1): guarda de reentrada. El heap no tiene cerrojo porque el nucleo es cooperativo a proposito (ver ESTABILIDAD,
 * parche 072): ninguna IRQ ni punto de desalojo debe caer dentro de kmalloc/kfree. Si algun dia ocurre (una IRQ que reserva memoria, un
 * hilo desalojado a mitad), en vez de corromper las listas en silencio se rechaza la llamada reentrante y se cuenta en reentries. */
static volatile int heap_busy;
int kheap_busy(void) { return heap_busy; }      /* SOMA_198 */
void *kmalloc(size_t size)
{
    if (heap_busy) { heap_ctr.reentries++; return 0; }
    heap_busy = 1;
    void *r = kmalloc_impl(size);
    heap_busy = 0;
    return r;
}
void kfree(void *ptr)
{
    if (heap_busy) { heap_ctr.reentries++; return; }
    heap_busy = 1;
    kfree_impl(ptr);
    heap_busy = 0;
}
/* Solo para pruebas: simula que otra ruta ya esta dentro del asignador. Devuelve 1 si la reentrada se rechaza bien. */
int kheap_test_reentry(void)
{
    uint64_t antes = heap_ctr.reentries;
    void *ok = kmalloc(32);
    if (!ok) return 0;
    heap_busy = 1;
    void *no = kmalloc(32);
    kfree(ok);
    heap_busy = 0;
    int bien = (no == 0) && heap_ctr.reentries == antes + 2;
    kfree(ok);                                   /* ahora si se libera; el bloque sigue vivo */
    return bien;
}

/* PARCHE 119 (solo pruebas): tamano del mayor bloque libre; sirve para agotar el hueco y forzar que la siguiente peticion haga crecer el monton */
size_t kheap_largest_free(void)
{
    size_t m = 0;
    for (struct block_header *c = heap_head; c; c = c->next) if (c->is_free && c->size > m) m = c->size;
    return m;
}

void kheap_stats(size_t *used, size_t *free_bytes)
{
    if (!heap_initialized) kheap_init();

    /* PARCHE 128: O(1). kheap_check() comprueba que los contadores coinciden con un recorrido completo. */
    if (used) *used = (size_t)heap_ctr.used_bytes;
    if (free_bytes) *free_bytes = heap_free_bytes;
}

/* PARCHE 089: bytes vivos (con cabecera) por dueno */
void kheap_owner_usage(uint64_t out[256], uint32_t blocks[256])
{
    for (int i = 0; i < 256; i++) { out[i] = 0; if (blocks) blocks[i] = 0; }
    if (!heap_initialized) return;
    for (struct block_header *b = heap_head; b; b = b->next) {
        if (!hdr_in_heap(b)) break;
        if (b->is_free) continue;
        uint8_t o = (uint8_t)(b->req >> 56);
        out[o] += b->size + sizeof(struct block_header);
        if (blocks) blocks[o]++;
    }
}
void kheap_get_counters(struct kheap_counters *out)
{
    if (!out) return;
    if (!heap_initialized) kheap_init();
    *out = heap_ctr;
    out->mapped_bytes = heap_mapped_bytes;
    out->reentries = heap_ctr.reentries;
}

/*
 * Verifica el heap completo. Devuelve el numero de problemas (0 = sano).
 * Invariante: los bloques recorren la region mapeada sin huecos ni solapes,
 * desde KHEAP_START hasta el limite actual del heap.
 */
int kheap_check(void)
{
    if (!heap_initialized) return 0;

    int problems = 0;
    uintptr_t expect = (uintptr_t)KHEAP_START;

    for (struct block_header *curr = heap_head; curr; curr = curr->next) {
        if ((uintptr_t)curr != expect) {
            kheap_report("los bloques no son contiguos", curr);
            return problems + 1;
        }
        if (!block_valid(curr)) {
            kheap_report("cabecera invalida", curr);
            return problems + 1;
        }
        if (!curr->is_free && !tail_intact(curr)) {
            kheap_report("desbordamiento de buffer en bloque asignado", curr);
            problems++;
        }
        expect = (uintptr_t)(curr + 1) + curr->size;
    }

    if (expect != (uintptr_t)heap_current_brk) {
        kheap_report("la lista no cubre toda la region mapeada", (const void *)expect);
        problems++;
    }

    /* PARCHE 030: las listas de libres deben contener exactamente los bloques libres de la lista fisica */
    size_t libres = 0, en_listas = 0;
    struct block_header *ultimo = 0;
    for (struct block_header *curr = heap_head; curr; curr = curr->next) { if (curr->is_free) libres++; ultimo = curr; }
    if (ultimo != heap_tail) { kheap_report("heap_tail no apunta al ultimo bloque", heap_tail); problems++; }
    for (unsigned k = 0; k < KHEAP_NBINS; ++k) {
        int marcado = (heap_bin_mask[k >> 6] >> (k & 63)) & 1;
        if ((heap_bins[k] != 0) != marcado) { kheap_report("mapa de bits de libres incoherente", heap_bins[k]); problems++; }
        for (struct block_header *b = heap_bins[k]; b; b = FN(b)->fnext) {
            if (!fnode_ok(b) || bin_of(b->size) != k) { kheap_report("bloque mal colocado en las listas de libres", b); return problems + 1; }
            if (++en_listas > libres) break;
        }
    }
    if (en_listas != libres) { kheap_report("las listas de libres no coinciden con la lista fisica", 0); problems++; }
    {   /* PARCHE 128: los contadores incrementales deben coincidir con lo que dice el recorrido */
        uint64_t u = 0, f = 0, vivos = 0;
        for (struct block_header *c = heap_head; c; c = c->next) {
            if (c->is_free) f += c->size; else { u += c->size + sizeof(struct block_header); vivos++; }
        }
        if (u != heap_ctr.used_bytes || vivos != heap_ctr.live_blocks) { kheap_report("contador de bytes usados desviado", 0); problems++; }
        if (f != heap_free_bytes) { kheap_report("contador de bytes libres desviado", 0); problems++; }
    }
    return problems;
}

void kheap_dump_stats(void)
{
    size_t used = 0, free_b = 0;
    kheap_stats(&used, &free_b);

    kprint("\nESTADO DEL HEAP (kmalloc Dinamico VMM):\n");
    kprint("----------------------------------------\n");
    kprint("Base Virtual:       0x"); if ((uint64_t)KHEAP_START >> 32) kprint_hex32((uint32_t)((uint64_t)KHEAP_START >> 32)); kprint_hex32((uint32_t)KHEAP_START); kprint("\n");
    kprint("Alineacion:         16 bytes estricta\n");
    kprint("Proteccion MMU:     Read-Write + No-Execute (NX ACTIVO)\n");
    kprint("Paginas Mapeadas:   "); kprint_dec((uint32_t)(heap_mapped_bytes / 1024)); kprint(" KiB (");
    kprint_dec((uint32_t)(heap_mapped_bytes / VMM_PAGE_SIZE)); kprint(" paginas 4 KiB)\n");
    kprint("Memoria Usada:      "); kprint_dec((uint32_t)used); kprint(" bytes\n");
    kprint("Memoria Libre:      "); kprint_dec((uint32_t)(free_b / 1024)); kprint(" KiB (");
    kprint_dec((uint32_t)free_b); kprint(" bytes)\n");
    kprint("Bloques vivos:      "); kprint_dec((uint32_t)heap_ctr.live_blocks);
    kprint(" | Pico de uso: "); kprint_dec((uint32_t)(heap_ctr.peak_used_bytes / 1024)); kprint(" KiB\n");
    kprint("Asignaciones:       "); kprint_dec((uint32_t)heap_ctr.alloc_calls);
    kprint(" (fallidas: "); kprint_dec((uint32_t)heap_ctr.failed_allocs);
    kprint(") | Liberaciones: "); kprint_dec((uint32_t)heap_ctr.free_calls);
    kprint(" (rechazadas: "); kprint_dec((uint32_t)heap_ctr.rejected_frees); kprint(")\n");
    kprint("Integridad:         ");
    if (heap_ctr.reentries) { kprint("  llamadas reentrantes rechazadas: "); kprint_dec(heap_ctr.reentries); kprint("\n"); }
    if (heap_ctr.corruptions == 0 && kheap_check() == 0) kprint("OK\n\n");
    else kprint("CORRUPCION DETECTADA\n\n");
}

int kheap_test_self(void)
{
    kprint("\n[HEAP AUTO-TEST] Verificando asignador dinamico respaldado por VMM...\n");
    size_t used_before = 0, free_before = 0;
    kheap_stats(&used_before, &free_before);
    size_t mapped_before = heap_mapped_bytes;

    /* 1. Asignaciones regulares */
    void *p1 = kmalloc(32);
    void *p2 = kmalloc(1024);

    if (!p1 || !p2) {
        kprint("  FALLO: kmalloc inicial retorno NULL\n");
        return 0;
    }

    if (((uintptr_t)p1 & 0x0F) != 0 || ((uintptr_t)p2 & 0x0F) != 0) {
        kprint("  FALLO: punteros no alineados a 16 bytes\n");
        return 0;
    }

    if ((uintptr_t)p1 < KHEAP_START || (uintptr_t)p2 < KHEAP_START) {
        kprint("  FALLO: punteros fuera del rango virtual del heap\n");
        return 0;
    }

   /*
     * 2. Expansion dinamica real.
     *
     * free_before representa la capacidad libre observada antes de
     * las asignaciones de esta prueba. Pedimos mas que esa capacidad,
     * de modo que kmalloc() no pueda satisfacer la peticion usando
     * ningun bloque libre existente y tenga que llamar a kheap_expand().
     */
    size_t big_size = free_before + 4096;
    /* SOMA_157: con un montón grande puede haber mucha memoria ya proyectada y libre (p. ej. tras "ramtest"); pedir mas que
     * todo eso superaria el limite y la prueba fallaria sin que el montón tenga ningun problema. Entonces se omite. */
    if (heap_mapped_bytes + big_size + (64u << 10) > kheap_limit) {
        kprint("  (expansion omitida: lo ya proyectado y libre ocupa casi todo el limite del montón)\n");
        kfree(p1); kfree(p2);
        kprint("[HEAP AUTO-TEST] SUPERADO (sin margen para forzar la expansion).\n\n");
        return 1;
    }
    void *p_big = kmalloc(big_size);
    if (!p_big) {
        kprint("  FALLO: kmalloc expansion dinamica no pudo crecer\n");
        return 0;
    }

    if (heap_mapped_bytes <= mapped_before) {
        kprint("  FALLO: el heap no incremento sus paginas mapeadas tras expansion forzada\n");
        return 0;
    }

    kprint("  Expansion dinamica forzada superada (Mapeados: ");
    kprint_dec((uint32_t)(mapped_before / 1024));
    kprint(" KiB -> ");
    kprint_dec((uint32_t)(heap_mapped_bytes / 1024));
    kprint(" KiB) OK\n");

    /* 3. Canarios */
    memset(p1, 0xAA, 32);
    memset(p2, 0x55, 1024);
    memset(p_big, 0x33, big_size);

    uint8_t *b1 = (uint8_t *)p1;
    uint8_t *b_big = (uint8_t *)p_big;
    if (b1[0] != 0xAA || b1[31] != 0xAA ||
        b_big[0] != 0x33 || b_big[big_size - 1] != 0x33) {
        kprint("  FALLO: corrupcion de memoria en heap dinamico\n");
        return 0;
    }
    
    /* 4. Robustez de kfree: puntero interior + double free */
    kfree((void *)((uintptr_t)p1 + 8));

    if (b1[0] != 0xAA || b1[31] != 0xAA) {
        kprint("  FALLO: invalid free corrompio el bloque valido\n");
        return 0;
    }

    kfree(p2);
    kfree(p2);

    /* 5. Liberar bloques validos */
    kfree(p1);
    kfree(p_big);

    size_t used_after = 0, free_after = 0;
    kheap_stats(&used_after, &free_after);

    if (used_after != used_before) {
        kprint("  FALLO: fuga de memoria detectada tras kfree\n");
        return 0;
    }

    if (kheap_check() != 0) {
        kprint("  FALLO: el heap no es consistente tras la prueba\n");
        return 0;
    }

    kprint("  Coalescencia y liberacion verificada OK (0 fugas)\n");
    kprint("[HEAP AUTO-TEST] SUPERADO CON EXITO.\n\n");
    return 1;
}
