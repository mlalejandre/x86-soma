/* PARCHE 125 (calidad): autopruebas del modo usuario separadas del nucleo de `usermode.c` (que queda en entrada/salida de ring 3, ranuras y syscalls).
 * Aqui viven `usfuzz` (parche 117, con los senuelos del 121) y `oomtest` (118, 119): solo usan la API publica (usermode_exec/wait, pmm, kheap, bigfs). */
#include <stdint.h>
#include <stddef.h>
#include "usermode.h"
#include "vmm.h"
#include "pmm.h"
#include "idt.h"
#include "console.h"
#include "thread.h"
#include "fs.h"
#include "bigfs_glue.h"
#include "mem.h"

/* ================================================================== PARCHE 117: fuzzing de syscalls desde ring 3
 * Lanza `nproc` procesos hostiles (/bin/sysfuzz.elf) de 3 en 3 con semillas distintas. Cada uno es su propio oraculo (ver src/user/sysfuzz.c).
 * Despues comprueba que el kernel sigue integro: sin fugas de marcos, monton consistente, region de usuario vacia en la PML4 del kernel. */
static void fz_u32(char *d, uint32_t *n, uint32_t v) { char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) d[(*n)++] = t[--k]; }
/* PARCHE 121: archivos "senuelo" en rutas privadas/internas del almacen BIGFS, para que el fuzzer demuestre que SYS_OPEN los rechaza aunque EXISTAN (sin ellos, un
 * kernel sin la proteccion tambien devolveria error: "no existe"). Solo se borra lo que esta funcion creo. */
static const char *const fz_canary_files[] = { "/.ramfs/0", "/.ramfs/t000", "/.ramfs/3", "/mem/x", "/agent/state.txt", "/etc/init.sh", "/.ckpt/B", 0 };
static const char *const fz_canary_dirs[] = { "/.ramfs", "/mem", "/agent", "/etc", "/.ckpt", 0 };
static uint8_t fz_made_file[8], fz_made_dir[5];
static void fz_canaries(int create)
{
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return;
    if (create) {
        for (int i = 0; fz_canary_dirs[i]; ++i) fz_made_dir[i] = bigfs_mkdir(fs, fz_canary_dirs[i]) == 0;
        for (int i = 0; fz_canary_files[i]; ++i) {
            uint32_t ino; fz_made_file[i] = bigfs_create(fs, fz_canary_files[i], &ino) == 0;
            if (fz_made_file[i]) bigfs_write(fs, ino, 0, "SECRETO", 7);
        }
    } else {
        for (int i = 0; fz_canary_files[i]; ++i) if (fz_made_file[i]) { bigfs_unlink(fs, fz_canary_files[i]); fz_made_file[i] = 0; }
        for (int i = 0; fz_canary_dirs[i]; ++i) if (fz_made_dir[i]) { bigfs_rmdir(fs, fz_canary_dirs[i]); fz_made_dir[i] = 0; }
    }
}
int usermode_fuzz(int nproc, uint32_t iters, uint32_t seed)
{
    if (nproc < 1) nproc = 1;
    if (nproc > 500) nproc = 500;
    if (iters < 10) iters = 10;
    if (iters > 1000000) iters = 1000000;
    uint32_t ilen = 0; char *img = vfs_read_alloc("/bin/sysfuzz.elf", &ilen);
    if (!img || ilen == 0) { kprint("[USFUZZ] no encuentro /bin/sysfuzz.elf\n"); if (img) kfree(img); return -1; }
    /* La pasada principal (nivel 0) hace crecer cosas legitimas de un solo uso (cache del backbuffer, tablas de paginas del monton): su recuento de marcos es solo
     * informativo. La medida de fugas se hace en una segunda pasada corta (nivel 1) ya en estado estable: si cada proceso perdiera un marco, aqui se veria. */
    static int nivel;
    int nv = nivel;
    size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0;
    struct kheap_counters h0, h1;
    kheap_get_counters(&h0);
    pmm_get_stats(&f0, &u0, &t0);
    if (!nv) fz_canaries(1);
    kprint("[USFUZZ] "); kprint_dec((uint32_t)nproc); kprint(" procesos hostiles x "); kprint_dec(iters); kprint(" syscalls maliciosas (semilla "); kprint_dec(seed); kprint(")\n");
    int clean = 0, breach = 0, faulted = 0, timeout = 0, other = 0, norun = 0; uint64_t t_start = timer_get_uptime_ms();
    int idx[3];
    for (int base = 0; base < nproc; base += 3) {
        int w = nproc - base < 3 ? nproc - base : 3;
        for (int k = 0; k < w; ++k) {
            char args[40]; uint32_t n = 0; fz_u32(args, &n, seed + (uint32_t)(base + k) * 7919u); args[n++] = ' '; fz_u32(args, &n, iters); args[n] = 0;
            int err = 0; idx[k] = usermode_exec("sysfuzz", img, ilen, 20000, args, &err);
            if (idx[k] < 0) norun++;
        }
        for (int k = 0; k < w; ++k) {
            if (idx[k] < 0) continue;
            int64_t code = usermode_wait(idx[k]);
            if (code == 0) clean++;
            else if (code > 0 && code <= 100) { breach++; kprint("[USFUZZ] VIOLACION: el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" detecto "); kprint_dec((uint32_t)code); kprint(" llamadas aceptadas indebidamente\n"); }
            else if (code <= USER_EXIT_FAULT_BASE && code > USER_EXIT_FAULT_BASE - 32) { faulted++; kprint("[USFUZZ] el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" murio por excepcion (vector "); kprint_dec((uint32_t)(USER_EXIT_FAULT_BASE - code)); kprint(")\n"); }
            else if (code == USER_EXIT_TIMEOUT) { timeout++; kprint("[USFUZZ] el proceso "); kprint_dec((uint32_t)(base + k)); kprint(" no termino en su plazo\n"); }
            else { other++; kprint("[USFUZZ] codigo de salida inesperado "); kprint(code < 0 ? "-" : ""); kprint_dec((uint32_t)(code < 0 ? -code : code)); kprint(" en el proceso "); kprint_dec((uint32_t)(base + k)); kprint("\n"); }
        }
    }
    kfree(img);
    thread_sleep(20);
    if (!nv) fz_canaries(0);
    pmm_get_stats(&f1, &u1, &t1);
    kheap_get_counters(&h1);
    /* fuga de MARCOS de procesos = marcos perdidos que NO se explican por el crecimiento del monton (el backbuffer de pantalla que pide SYS_FB_OPEN se conserva
     * como cache hasta 8 MiB y es crecimiento legitimo; 1 bloque vivo como mucho) */
    long perdidos = (long)f0 - (long)f1, crec = (long)((h1.mapped_bytes > h0.mapped_bytes ? h1.mapped_bytes - h0.mapped_bytes : 0) / 4096);
    int leak = nv ? ((perdidos > 2) || (h1.live_blocks > h0.live_blocks + 1)) : 0;
    (void)crec;
    int heap = kheap_check();
    int region = vmm_kernel_user_region_empty() ? 1 : 0;
    kprint("[USFUZZ] limpios: "); kprint_dec((uint32_t)clean); kprint(" | violaciones: "); kprint_dec((uint32_t)breach); kprint(" | excepciones: "); kprint_dec((uint32_t)faulted);
    kprint(" | plazo: "); kprint_dec((uint32_t)timeout); kprint(" | otros: "); kprint_dec((uint32_t)(other + norun)); kprint(" | "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t_start)); kprint(" ms\n");
    kprint("[USFUZZ] marcos libres antes/despues: "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1); kprint(" (monton crecio "); kprint_dec((uint32_t)crec);
    kprint(" marcos, bloques vivos "); kprint_dec((uint32_t)h0.live_blocks); kprint("->"); kprint_dec((uint32_t)h1.live_blocks); kprint(")"); kprint(nv ? (leak ? "  FUGA\n" : "  sin fugas\n") : "  (informativo)\n");
    kprint("[USFUZZ] monton: "); kprint(heap == 0 ? "consistente" : "DANADO"); kprint(" | PML4 del kernel sin entradas de usuario: "); kprint(region ? "si" : "NO"); kprint("\n");
    int ok = clean == nproc && !leak && heap == 0 && region;
    if (!nv) { nivel = 1; int ok2 = usermode_fuzz(3, 300, seed ^ 0x5a5a5u); nivel = 0; ok = ok && ok2; }
    kprint(ok ? "[USFUZZ] TODO OK: el kernel resistio sin ninguna violacion\n" : "[USFUZZ] HAY FALLOS\n");
    return ok;
}

/* ================================================================== PARCHE 118: memoria agotada (OOM) en el cargador de procesos
 * Para n = 0, 1, 2... hace que la asignacion de marcos fisicos n+1 (y todas las siguientes) falle MIENTRAS se crea un proceso de usuario. Cada fallo cae en un punto
 * distinto de usermode_exec / vmm / heap. Obligaciones: nunca cuelgue ni panico; un fallo devuelve error (negativo) y deja el sistema EXACTAMENTE como estaba (mismos
 * marcos libres, mismos bloques vivos del monton, tabla de procesos sin ranuras ocupadas); en cuanto n basta, el proceso arranca y termina bien. */
int usermode_oomtest(void)
{
    uint32_t ilen = 0; char *img = vfs_read_alloc("/bin/hello.elf", &ilen);
    if (!img || ilen == 0) { kprint("[OOM] no encuentro /bin/hello.elf\n"); if (img) kfree(img); return -1; }
    size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0;
    int fail_at_first_ok = -1, bad = 0, errs = 0, exits_ok = 0;
    long hits_total = 0;
    /* pasada de calentamiento sin inyeccion (cosas de un solo uso) */
    { int e = 0; int ix = usermode_exec("oom", img, ilen, 5000, "", &e); if (ix >= 0) (void)usermode_wait(ix); thread_sleep(20); }
    struct kheap_counters h0, h1;
    kheap_get_counters(&h0); pmm_get_stats(&f0, &u0, &t0);
    kprint("[OOM] inyectando memoria agotada en cada punto de la carga de un proceso...\n");
    for (int n = 0; n < 200; ++n) {
        pmm_inject_oom_after(n);
        int e = 0; int ix = usermode_exec("oom", img, ilen, 5000, "", &e);
        unsigned long hits = pmm_inject_oom_hits();
        pmm_inject_oom_after(-1);
        if (ix < 0) {
            errs++; hits_total += (long)hits;
            if (hits == 0) { bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": fallo sin que se negara ningun marco\n"); }
        } else {
            int64_t code = usermode_wait(ix);
            if (code == 0) exits_ok++; else { bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": el proceso arranco pero salio con codigo distinto de 0\n"); }
            if (fail_at_first_ok < 0) fail_at_first_ok = n;
            if (hits == 0) break;                         /* ya no se denego ningun marco: hemos barrido todos los puntos */
        }
        thread_sleep(5);
        pmm_get_stats(&f1, &u1, &t1); kheap_get_counters(&h1);
        if (f1 != f0 || h1.live_blocks != h0.live_blocks) {
            bad++; kprint("[OOM] n="); kprint_dec((uint32_t)n); kprint(": el sistema no quedo como estaba (marcos libres "); kprint_dec((uint32_t)f0); kprint("->"); kprint_dec((uint32_t)f1);
            kprint(", bloques "); kprint_dec((uint32_t)h0.live_blocks); kprint("->"); kprint_dec((uint32_t)h1.live_blocks); kprint(")\n");
            f0 = f1; h0 = h1;                              /* re-base para no repetir el mismo aviso en todas las vueltas */
        }
    }
    kfree(img);
    int heap = kheap_check();
    kprint("[OOM] cargas fallidas sin dano: "); kprint_dec((uint32_t)errs); kprint(" | denegaciones de marco: "); kprint_dec((uint32_t)hits_total);
    kprint(" | primera carga completa con n="); kprint_dec((uint32_t)(fail_at_first_ok < 0 ? 0 : fail_at_first_ok)); kprint(" | defectos: "); kprint_dec((uint32_t)bad);
    kprint(" | monton: "); kprint(heap == 0 ? "consistente\n" : "DANADO\n");
    int ok = bad == 0 && heap == 0 && errs >= 3 && exits_ok >= 1;
    kprint(ok ? "[OOM] TODO OK\n" : "[OOM] HAY FALLOS\n");
    return ok;
}

/* ================================================================== PARCHE 119: memoria agotada en el monton y en el sistema de archivos
 * Cada ronda pide MAS de lo que cabe en el mayor hueco libre (obliga al monton a crecer) y se repite con el PMM fallando en el marco 0, k, 2k... hasta cubrir toda la
 * peticion; la ultima repeticion va sin fallos.  Obligaciones:
 *  (1) kmalloc: devuelve NULL (nunca basura) y el monton queda consistente y SIN marcos perdidos tras cada fallo.
 *  (2) vfs_write sobre un archivo existente: si falla, el contenido ANTERIOR sigue intacto; si funciona, el archivo tiene el tamano pedido.
 *  (3) vfs_create: o existe entero o no existe.
 * El monton no se encoge, asi que cada ronda parte de un hueco mayor: pocas rondas y de tamano creciente. */
static int oom_same(const char *a, uint32_t al, const char *b, uint32_t bl) { if (al != bl) return 0; for (uint32_t i = 0; i < al; ++i) if (a[i] != b[i]) return 0; return 1; }
int usermode_oomtest_mem(void)
{
    int bad = 0, denied = 0, served = 0, fw_ok = 0, fw_fail = 0, fc_ok = 0, fc_fail = 0, cc_ok = 0, cc_fail = 0, cc_notap = 0, rondas_hechas = 0;
    long pt_lost = 0;       /* tablas de paginas que un crecimiento fallido deja creadas (se reutilizan despues; acotadas por el tamano del monton) */
    { void *w = kmalloc(4096); if (w) kfree(w); }
    for (int ronda = 0; ronda < 4; ++ronda) {
        size_t want = kheap_largest_free() + 64 * 1024;
        { struct kheap_counters hm; kheap_get_counters(&hm);                       /* PARCHE 128: sin margen bajo el limite del monton no hay nada que probar */
          if (hm.mapped_bytes + want + 16 * 1024 * 1024 > kheap_max_bytes()) { kprint("[OOM] monton: limite alcanzado, se paran las rondas en la "); kprint_dec((uint32_t)ronda); kprint("\n"); break; } }
        rondas_hechas++;
        size_t frames = want / 4096 + 2;
        long step = (long)(frames / 24) + 1;
        for (long n = 0; n <= (long)frames + step; n += step) {
            int last = n > (long)frames;                          /* ultima vuelta: sin fallos */
            size_t f0 = 0, u0 = 0, t0 = 0, f1 = 0, u1 = 0, t1 = 0; struct kheap_counters h0, h1;
            pmm_get_stats(&f0, &u0, &t0); kheap_get_counters(&h0);
            pmm_inject_oom_after(last ? -1 : n);
            uint8_t *p = (uint8_t *)kmalloc(want);
            unsigned long hits = pmm_inject_oom_hits();
            pmm_inject_oom_after(-1);
            if (!p) {
                denied++;
                if (last) { bad++; kprint("[OOM] monton: la peticion sin fallos tampoco se sirvio\n"); }
                pmm_get_stats(&f1, &u1, &t1); kheap_get_counters(&h1);
                if (f0 >= f1 && f0 - f1 <= (want >> 21) + 2 && h1.mapped_bytes == h0.mapped_bytes && h1.live_blocks == h0.live_blocks) pt_lost += (long)(f0 - f1);
                else { bad++; kprint("[OOM] monton: un fallo dejo marcos/bloques perdidos (n="); kprint_dec((uint32_t)n); kprint(")\n"); }
            } else {
                served++; if (hits) bad++;                      /* servida pese a haber denegado marcos */
                p[0] = 1; p[want - 1] = 2; if (p[0] != 1 || p[want - 1] != 2) bad++;
                kfree(p);
            }
            if (kheap_check() != 0) { bad++; kprint("[OOM] monton danado, ronda "); kprint_dec((uint32_t)ronda); kprint("\n"); return 0; }
        }
        /* --- sistema de archivos: mismo barrido con un archivo cuyo tamano exige crecer el monton (ruta /tmp/: efimera, sin disco) --- */
        size_t fwant = kheap_largest_free() + 64 * 1024; if (fwant > 16u * 1024 * 1024) fwant = 16u * 1024 * 1024;
        size_t ffr = fwant / 4096 + 2; long fstep = (long)(ffr / 24) + 1;
        const char *orig = "CONTENIDO ORIGINAL"; uint32_t ol = 18;
        for (long n = 0; n <= (long)ffr + fstep; n += fstep) {
            int last = n > (long)ffr;
            vfs_delete("/tmp/oomA"); vfs_delete("/tmp/oomB");
            vfs_create("/tmp/oomA", orig);
            pmm_inject_oom_after(last ? -1 : n);
            int r = vfs_write("/tmp/oomA", 0, (uint32_t)fwant);
            int c = vfs_create("/tmp/oomB", "NUEVO");
            pmm_inject_oom_after(-1);
            uint32_t l = 0; char *back = vfs_read_alloc("/tmp/oomA", &l);
            if (!back) { bad++; kprint("[OOM] vfs: el archivo desaparecio\n"); }
            else {
                if (r >= 0) { fw_ok++; if (l != (uint32_t)fwant) { bad++; kprint("[OOM] vfs: escritura aceptada con tamano incorrecto\n"); } }
                else { fw_fail++; if (last) { bad++; kprint("[OOM] vfs: la escritura sin fallos fallo\n"); } if (!oom_same(back, l, orig, ol)) { bad++; kprint("[OOM] vfs: la escritura fallo y ESTROPEO el contenido anterior\n"); } }
                kfree(back);
            }
            uint32_t l2 = 0; char *b2 = vfs_read_alloc("/tmp/oomB", &l2);
            if (c >= 0) { fc_ok++; if (!b2 || !oom_same(b2, l2, "NUEVO", 5)) { bad++; kprint("[OOM] vfs: creacion aceptada pero archivo incompleto\n"); } }
            else { fc_fail++; if (b2) { bad++; kprint("[OOM] vfs: creacion fallida dejo un archivo\n"); } }
            if (b2) kfree(b2);
            if (kheap_check() != 0) { bad++; kprint("[OOM] monton danado tras vfs\n"); return 0; }
        }
        vfs_delete("/tmp/oomA"); vfs_delete("/tmp/oomB");
        /* --- PARCHE 128: creacion de un archivo grande. Se tapan todos los huecos >= cs del monton ("tapones") para que vfs_create
         * TENGA que crecerlo; asi la denegacion de marcos cae de verdad dentro de la creacion y debe deshacerse sin dejar nada. --- */
        {
            const size_t cs = 512 * 1024; const long cfr = (long)(cs / 4096) + 2;
            char *big = (char *)kmalloc(cs + 1);
            if (!big) { bad++; kprint("[OOM] vfs: sin memoria para preparar la creacion grande\n"); }
            else {
                for (size_t i = 0; i < cs; ++i) big[i] = (char)('a' + (i % 23));
                big[cs] = 0;
                for (long n = 0; n <= cfr + 6; n += 3) {
                    int last = n > cfr;
                    size_t u0, uf0, u1, uf1; kheap_stats(&u0, &uf0); (void)uf0;
                    void *tap[48]; int nt = 0;
                    while (nt < 48 && kheap_largest_free() >= cs + 64) { void *t = kmalloc(kheap_largest_free() - 48); if (!t) break; tap[nt++] = t; }
                    int tapado = kheap_largest_free() < cs;
                    vfs_delete("/tmp/oomC");
                    pmm_inject_oom_after(last ? -1 : n);
                    int c = vfs_create("/tmp/oomC", big);
                    pmm_inject_oom_after(-1);
                    uint32_t l3 = 0; char *b3 = vfs_read_alloc("/tmp/oomC", &l3);
                    if (c >= 0) { cc_ok++; if (!b3 || l3 != (uint32_t)cs || !oom_same(b3, l3, big, (uint32_t)cs)) { bad++; kprint("[OOM] vfs: creacion grande aceptada pero incompleta\n"); } }
                    else { cc_fail++; if (b3) { bad++; kprint("[OOM] vfs: creacion grande fallida dejo un archivo\n"); } if (last) { bad++; kprint("[OOM] vfs: la creacion grande sin fallos fallo\n"); } }
                    if (b3) kfree(b3);
                    if (!tapado && !last) cc_notap++;
                    for (int i = 0; i < nt; ++i) kfree(tap[i]);
                    vfs_delete("/tmp/oomC");
                    kheap_stats(&u1, &uf1); (void)uf1;
                    if (u1 != u0) { bad++; kprint("[OOM] vfs: la creacion grande dejo bytes del monton sin liberar\n"); }
                    if (kheap_check() != 0) { bad++; kprint("[OOM] monton danado tras la creacion grande\n"); return 0; }
                }
                kfree(big);
            }
        }
    }
    { struct kheap_counters hz; kheap_get_counters(&hz);
      long cota = (long)(hz.mapped_bytes / (2u * 1024 * 1024)) + 4;
      if (pt_lost > cota) { bad++; kprint("[OOM] monton: demasiadas tablas de paginas sin reutilizar: "); kprint_dec((uint32_t)pt_lost); kprint(" > "); kprint_dec((uint32_t)cota); kprint("\n"); } }
    kprint("[OOM] monton: "); kprint_dec((uint32_t)denied); kprint(" peticiones denegadas, "); kprint_dec((uint32_t)served); kprint(" servidas, tablas de paginas retenidas "); kprint_dec((uint32_t)pt_lost); kprint("\n");
    kprint("[OOM] vfs: escrituras ok/rechazadas "); kprint_dec((uint32_t)fw_ok); kprint("/"); kprint_dec((uint32_t)fw_fail);
    kprint(" | creaciones ok/rechazadas "); kprint_dec((uint32_t)fc_ok); kprint("/"); kprint_dec((uint32_t)fc_fail); kprint(" | defectos: "); kprint_dec((uint32_t)bad); kprint("\n");
    kprint("[OOM] vfs: creaciones grandes ok/rechazadas "); kprint_dec((uint32_t)cc_ok); kprint("/"); kprint_dec((uint32_t)cc_fail); kprint(" (sin tapar el hueco: "); kprint_dec((uint32_t)cc_notap); kprint(")\n");
    int ok = bad == 0 && rondas_hechas >= 2 && denied > 20 && served >= rondas_hechas && fw_fail > 20 && fw_ok >= 4 && cc_fail >= 20 && cc_ok >= 4 && cc_notap == 0;
    kprint(ok ? "[OOM] MEM TODO OK\n" : "[OOM] MEM HAY FALLOS\n");
    return ok;
}
