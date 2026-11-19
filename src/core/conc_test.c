/* PARCHE 111 (Fase 1, concurrencia): prueba de estres dentro del propio SOMA. Seis hilos del nucleo compiten a la vez por un mutex, por el
 * heap y por el VFS, CEDIENDO la CPU en mitad de cada operacion (el planificador es cooperativo: ese es el unico punto donde pueden
 * entrelazarse). Detecta exclusion mutua rota, corrupcion de bloques del heap, mezcla de contenidos del VFS y mutex huerfanos.
 * Se lanza con el comando `conctest` (no es CMD_F_SAFE: el agente no puede ejecutarlo). */
#include <stdint.h>
#include "conc_test.h"
#include "thread.h"
#include "mutex.h"
#include "mem.h"
#include "fs.h"
#include "console.h"
#include "idt.h"

#define CT_THREADS 6
#define CT_ITER_MUTEX 80
#define CT_ROUNDS_HEAP 30
#define CT_ROUNDS_VFS 12
#define CT_BLOCKS 10
#define CT_FILES 4
#define CT_FILE_LEN 300

static struct kmutex ct_lock, ct_orphan;
static volatile uint64_t ct_counter;
static volatile int ct_done, ct_errors, ct_started;
static volatile int ct_err_mutex, ct_err_heap, ct_err_vfs;

static uint32_t ct_rng(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void ct_name(char *out, uint32_t tid, int k)
{
    const char *p = "/tmp/ct_"; int n = 0;
    while (*p) out[n++] = *p++;
    char d[12]; int dl = 0; uint32_t v = tid; do { d[dl++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (dl) out[n++] = d[--dl];
    out[n++] = '_'; out[n++] = (char)('a' + k); out[n] = '\0';
}

static void ct_worker(void *arg)
{
    uint32_t tid = thread_current()->tid, seed = 0x9E3779B9u ^ (tid * 2654435761u);
    (void)arg;

    /* 1) exclusion mutua: leer-ceder-escribir dentro de la seccion critica. Si dos hilos entraran a la vez se perderian incrementos. */
    for (int i = 0; i < CT_ITER_MUTEX; ++i) {
        kmutex_lock(&ct_lock);
        uint64_t v = ct_counter;
        thread_yield();
        ct_counter = v + 1;
        kmutex_unlock(&ct_lock);
        if (ct_rng(&seed) & 1) thread_yield();
    }

    /* 2) heap: varios bloques vivos a la vez, con un patron propio; se cede y se comprueba que nadie los ha pisado. */
    for (int r = 0; r < CT_ROUNDS_HEAP; ++r) {
        uint8_t *b[CT_BLOCKS]; uint32_t sz[CT_BLOCKS];
        for (int i = 0; i < CT_BLOCKS; ++i) {
            sz[i] = 16 + ct_rng(&seed) % 3000;
            b[i] = (uint8_t *)kmalloc(sz[i]);
            if (!b[i]) { ct_err_heap++; sz[i] = 0; continue; }
            for (uint32_t k = 0; k < sz[i]; ++k) b[i][k] = (uint8_t)(tid * 31u + (uint32_t)i * 7u + k);
            if (i & 1) thread_yield();
        }
        thread_yield();
        for (int i = 0; i < CT_BLOCKS; ++i) {
            if (!b[i]) continue;
            for (uint32_t k = 0; k < sz[i]; ++k)
                if (b[i][k] != (uint8_t)(tid * 31u + (uint32_t)i * 7u + k)) { ct_err_heap++; break; }
        }
        for (int i = CT_BLOCKS - 1; i >= 0; --i) { if (b[i]) kfree(b[i]); if (i % 3 == 0) thread_yield(); }
    }

    /* 3) VFS: cada hilo escribe sus archivos, cede, los relee comparando byte a byte y los borra. */
    for (int r = 0; r < CT_ROUNDS_VFS; ++r) {
        char nm[32], buf[CT_FILE_LEN + 8], rd[CT_FILE_LEN + 8];
        for (int f = 0; f < CT_FILES; ++f) {
            ct_name(nm, tid, f);
            for (int k = 0; k < CT_FILE_LEN; ++k) buf[k] = (char)('A' + (int)((tid + (uint32_t)f + (uint32_t)r + (uint32_t)k) % 26u));
            if (vfs_write(nm, buf, CT_FILE_LEN) < 0) ct_err_vfs++;
            thread_yield();
        }
        for (int f = 0; f < CT_FILES; ++f) {
            ct_name(nm, tid, f);
            for (int k = 0; k < CT_FILE_LEN; ++k) buf[k] = (char)('A' + (int)((tid + (uint32_t)f + (uint32_t)r + (uint32_t)k) % 26u));
            int n = vfs_read(nm, rd, CT_FILE_LEN + 1);
            if (n != CT_FILE_LEN) ct_err_vfs++;
            else for (int k = 0; k < CT_FILE_LEN; ++k) if (rd[k] != buf[k]) { ct_err_vfs++; break; }
            if (vfs_delete(nm) < 0) ct_err_vfs++;
            thread_yield();
        }
    }
    ct_done++;
}

/* Un hilo que muere reteniendo un mutex: kmutex_release_all_for_thread debe liberarlo. */
static void ct_orphan_worker(void *arg)
{
    (void)arg;
    kmutex_lock(&ct_orphan);
    ct_started = 1;                     /* sale sin soltarlo */
}

static void ct_say(const char *s, int bad)
{
    kprint("  "); kprint(s); kprint(bad ? ": FALLO\n" : ": OK\n");
}

int conc_selftest(void)
{
    int fails = 0;
    struct kheap_counters h0, h1;
    kprint("\n[CONCTEST] "); kprint_dec(CT_THREADS); kprint(" hilos compitiendo por mutex, heap y VFS (con cesiones de CPU a mitad de operacion)...\n");

    /* SOMA_199: antes de contar bloques se recogen los hilos que ya habian muerto (sus pilas se liberan en diferido): si se
     * liberaban durante la prueba, la cuenta bajaba y la prueba daba un fallo falso la primera vez tras usar la red o el navegador. */
    for (int i = 0; i < 4; ++i) { thread_reap_dead(); thread_sleep(5); }
    kheap_get_counters(&h0);
    kmutex_init(&ct_lock, "conc_lock"); kmutex_init(&ct_orphan, "conc_orphan");
    ct_counter = 0; ct_done = 0; ct_started = 0; ct_err_mutex = ct_err_heap = ct_err_vfs = ct_errors = 0;
    uint32_t misuse0 = kmutex_misuse_reports(), dead0 = kmutex_deadlocks_found();

    for (int i = 0; i < CT_THREADS; ++i) {
        char n[8] = { 'c', 't', (char)('0' + i), 0 };
        if (!thread_create(n, ct_worker, 0)) { kprint("  no se pudo crear el hilo\n"); fails++; }
    }
    uint64_t t0 = timer_get_uptime_ms();
    while (ct_done < CT_THREADS && timer_get_uptime_ms() - t0 < 60000u) thread_sleep(5);
    int timeout = ct_done < CT_THREADS;
    if (timeout) { kprint("  TIEMPO AGOTADO: los hilos no terminaron en 60 s\n"); fails++; }
    uint64_t ms = timer_get_uptime_ms() - t0;
    thread_reap_dead();

    uint64_t esperado = (uint64_t)CT_THREADS * CT_ITER_MUTEX;
    ct_err_mutex = (ct_counter != esperado);
    ct_say("exclusion mutua (contador)", ct_err_mutex);
    if (ct_err_mutex) { kprint("    contador="); kprint_dec((uint32_t)ct_counter); kprint(" esperado="); kprint_dec((uint32_t)esperado); kprint("\n"); }
    ct_say("heap: bloques intactos y sin fallos de memoria", ct_err_heap != 0);
    ct_say("VFS: contenidos intactos entre hilos", ct_err_vfs != 0);
    fails += ct_err_mutex + (ct_err_heap != 0) + (ct_err_vfs != 0);

    int hc = kheap_check();
    ct_say("heap: estructura consistente (kheap_check)", hc != 0);
    kheap_get_counters(&h1);
    for (int i = 0; i < 20 && h1.live_blocks > h0.live_blocks; ++i) { thread_sleep(50); thread_reap_dead(); kheap_get_counters(&h1); }   /* SOMA_199: lo que otros hilos tengan a medias */
    int fuga = h1.live_blocks > h0.live_blocks;        /* SOMA_199: una fuga es que queden MAS bloques vivos (otros hilos pueden liberar los suyos mientras tanto) */
    ct_say("heap: sin bloques perdidos", fuga);
    if (fuga) { kprint("    bloques vivos antes "); kprint_dec((uint32_t)h0.live_blocks); kprint(", despues "); kprint_dec((uint32_t)h1.live_blocks); kprint(" (si al repetir la prueba sale bien, era una reserva unica de otro hilo, no una fuga)\n"); }
    ct_say("heap: sin llamadas reentrantes", h1.reentries != h0.reentries);
    int leak_files = 0; for (int t = 0; t < 400; ++t) for (int f = 0; f < CT_FILES; ++f) { char nm[32]; ct_name(nm, (uint32_t)t, f); if (vfs_size(nm) >= 0) leak_files++; }
    ct_say("VFS: sin archivos de prueba huerfanos", leak_files != 0);
    ct_say("mutex: sin usos incorrectos ni interbloqueos", kmutex_misuse_reports() != misuse0 || kmutex_deadlocks_found() != dead0);
    fails += (hc != 0) + fuga + (h1.reentries != h0.reentries) + (leak_files != 0) + (kmutex_misuse_reports() != misuse0 || kmutex_deadlocks_found() != dead0);

    /* hilo que muere con el mutex tomado */
    thread_create("ct_orphan", ct_orphan_worker, 0);
    for (int i = 0; i < 200 && !ct_started; ++i) thread_sleep(5);
    for (int i = 0; i < 40; ++i) { thread_sleep(5); thread_reap_dead(); }
    int huer = !kmutex_lock_timeout(&ct_orphan, 500);
    if (!huer) kmutex_unlock(&ct_orphan);
    ct_say("mutex huerfano recuperado tras la muerte del hilo", huer);
    fails += huer;

    kprint("  tiempo: "); kprint_dec((uint32_t)ms); kprint(" ms | contador="); kprint_dec((uint32_t)ct_counter);
    kprint(fails ? "\n[CONCTEST] FALLOS: " : "\n[CONCTEST] TODO OK ("); kprint_dec((uint32_t)fails); kprint(fails ? "\n" : " fallos)\n");
    return fails == 0;
}
