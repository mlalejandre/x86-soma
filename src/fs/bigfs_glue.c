/* SOMA_PARCHE_018_BIGFS: pegamento del BIGFS con el kernel. Ver bigfs.h / bigfs_glue.h */
#include <stdint.h>
#include <stddef.h>
#include "bigfs_glue.h"
#include "disk_layout.h"
#include "virtio_blk.h"
#include "console.h"
#include "mem.h"
#include "mutex.h"
#include "idt.h"
#include "rtc.h"
#include "fs.h"
#include "spillio.h"

static struct bigfs *g_fs;
static struct kmutex g_lock;
static int g_lock_ready;
static int g_spill_state;           /* PARCHE 087: almacen de RamFS: 0 sin probar, 1 listo, -1 no disponible */

static uint64_t region_sectors(void)
{
    uint64_t cap = virtio_blk_capacity();
    if (cap <= SOMA_LBA_BIGFS_START + 64) return 0;
    return cap - SOMA_LBA_BIGFS_START;
}

static int d_read(void *c, uint64_t lba, void *b, uint32_t n) { (void)c; return virtio_blk_read_n(SOMA_LBA_BIGFS_START + lba, b, n); }
static int d_write(void *c, uint64_t lba, const void *b, uint32_t n) { (void)c; return virtio_blk_write_n(SOMA_LBA_BIGFS_START + lba, b, n); }
static int d_flush(void *c) { (void)c; return virtio_blk_flush(); }
static void *d_alloc(size_t n) { return kmalloc(n); }
static void d_free(void *p) { kfree(p); }
static void d_lock(void *c) { (void)c; if (!g_lock_ready) { kmutex_init(&g_lock, "bigfs_lock"); g_lock_ready = 1; } kmutex_lock(&g_lock); }
static void d_unlock(void *c) { (void)c; kmutex_unlock(&g_lock); }
static uint64_t d_now(void *c)
{
    (void)c;
    struct rtc_time t; rtc_get_datetime(&t);        /* AAAAMMDDHHMMSS como numero: legible y creciente */
    return (((((uint64_t)t.year * 100 + t.month) * 100 + t.day) * 100 + t.hour) * 100 + t.min) * 100 + t.sec;
}

static int make_dev(struct bigfs_dev *d)
{
    uint64_t n = region_sectors();
    if (!n) return 0;
    d->ctx = 0; d->read = d_read; d->write = d_write; d->total_sectors = n;
    d->alloc = d_alloc; d->free = d_free; d->now = d_now; d->lock = d_lock; d->unlock = d_unlock; d->flush = d_flush;
    return 1;
}

void bigfs_sys_probe(void)
{
    uint64_t n = region_sectors();
    if (!n) { kprint("BIGFS: sin region (el disco no llega a LBA 16384; usa un hdd.img mayor)\n"); return; }
    uint8_t sec[512];
    kprint("BIGFS: region de "); kprint_dec((uint32_t)(n / 2048)); kprint(" MiB (LBA 16384..): ");
    if (virtio_blk_read_n(SOMA_LBA_BIGFS_START, sec, 1) != 0) { kprint("error de lectura\n"); return; }
    kprint(sec[0] == 'S' && sec[1] == 'O' && sec[2] == 'M' && sec[3] == 'A' && sec[4] == 'B' && sec[5] == 'I' && sec[6] == 'G'
           ? "formateada (SOMABIG1)\n" : "sin formatear (usa 'big format yes')\n");
}

static int g_repair_tried;     /* PARCHE 129 */
struct bigfs *bigfs_sys(int *err)
{
    int dummy; if (!err) err = &dummy;
    if (g_fs) { *err = 0; return g_fs; }
    struct bigfs_dev d;
    if (!make_dev(&d)) { *err = BIGFS_E_NOSPC; return 0; }
    g_fs = bigfs_mount(&d, err);
    /* PARCHE 129: un superbloque danado o borrado NO es motivo para perder los archivos: se reconstruye (una sola vez por arranque) */
    if (!g_fs && (*err == BIGFS_E_CORRUPT || *err == BIGFS_E_NOTFMT) && !g_repair_tried) {
        g_repair_tried = 1;
        int rr = bigfs_repair_super(&d);
        if (rr > 0) {
            kprint(rr == 1 ? "BIGFS: superbloque danado: restaurado desde la copia de reserva\n" : "BIGFS: superbloque danado: reconstruido por la geometria automatica\n");
            g_fs = bigfs_mount(&d, err);
        }
    }
    return g_fs;
}

int bigfs_sys_format(void)
{
    struct bigfs_dev d;
    if (!make_dev(&d)) return BIGFS_E_NOSPC;
    if (g_fs) { bigfs_unmount(g_fs); g_fs = 0; }
    g_spill_state = 0;                                   /* PARCHE 087 */
    int r = bigfs_format(&d, 0);
    if (r) return r;
    int e; g_fs = bigfs_mount(&d, &e);
    return g_fs ? 0 : e;
}

/* ================================================================== PARCHE 087: almacen de RamFS para archivos grandes
 * Cada archivo que no cabe en una ranura de SOMAFS02 (16 KiB) o que no tiene ranura (mas de 64 archivos) se guarda en BIGFS como
 * /.ramfs/NNN = cabecera de 64 bytes ("RFS1", nombre, tamano) + contenido. Si la region BIGFS esta en blanco (todo ceros) se formatea
 * sola; si tiene otra cosa no se toca. */
#define RFS_DIR "/.ramfs"
static void spill_path(int slot, char *p)
{
    const char *d = RFS_DIR "/"; int k = 0; while (*d) p[k++] = *d++;
    p[k++] = (char)('0' + (slot / 100) % 10); p[k++] = (char)('0' + (slot / 10) % 10); p[k++] = (char)('0' + slot % 10); p[k] = 0;
}
static void spill_tmp_path(int slot, char *p)      /* "/.ramfs/tNNN": mismo directorio, nunca choca con un numero de ranura */
{
    const char *d = RFS_DIR "/t"; int k = 0; while (*d) p[k++] = *d++;
    p[k++] = (char)('0' + (slot / 100) % 10); p[k++] = (char)('0' + (slot / 10) % 10); p[k++] = (char)('0' + slot % 10); p[k] = 0;
}
int fs_spill_ready(void)
{
    if (g_spill_state) return g_spill_state > 0;
    if (!region_sectors()) { g_spill_state = -1; return 0; }
    int e = 0;
    struct bigfs *fs = bigfs_sys(&e);
    if (!fs && e == BIGFS_E_NOTFMT) {
        uint8_t sec[512]; int blank = virtio_blk_read_n(SOMA_LBA_BIGFS_START, sec, 1) == 0;
        for (int i = 0; blank && i < 512; i++) if (sec[i]) blank = 0;
        if (blank && bigfs_sys_format() == 0) { kprint("BIGFS: region en blanco formateada para los archivos grandes de RamFS\n"); fs = bigfs_sys(&e); }
    }
    if (!fs) { g_spill_state = -1; return 0; }
    uint32_t ino; if (bigfs_lookup(fs, RFS_DIR, &ino) < 0 && bigfs_mkdir(fs, RFS_DIR) != 0) { g_spill_state = -1; return 0; }
    g_spill_state = 1;
    {   /* PARCHE 113: terminar o descartar actualizaciones interrumpidas por un corte */
        int prom = 0, desc = 0;
        for (int sl = 0; sl < FS_MAX_FILES; sl++) {
            char p[32], t[32]; spill_path(sl, p); spill_tmp_path(sl, t);
            int r = spill_recover(fs, p, t);
            if (r == 1) prom++; else if (r == 2) desc++;
        }
        if (prom || desc) { kprint("BIGFS: actualizaciones interrumpidas: "); kprint_dec((uint32_t)prom); kprint(" completadas, "); kprint_dec((uint32_t)desc); kprint(" descartadas\n"); }
    }
    return 1;
}
/* PARCHE 113: actualizacion atomica (ver spillio.h): el contenido anterior no se destruye hasta tener el nuevo completo. */
int fs_spill_put(int slot, const char *name, const char *data, uint32_t len)
{
    struct bigfs *fs = g_spill_state > 0 ? g_fs : 0; if (!fs) return -1;
    char p[32], t[32]; spill_path(slot, p); spill_tmp_path(slot, t);
    return spill_put_atomic(fs, p, t, name, FS_NAME_MAX, data, len);
}
int fs_spill_stat(int slot, char *name_out, uint32_t *len_out)
{
    struct bigfs *fs = g_spill_state > 0 ? g_fs : 0; if (!fs) return -1;
    char p[32]; spill_path(slot, p); uint32_t ino;
    if (bigfs_lookup(fs, p, &ino) < 0) return -1;
    uint8_t h[64];
    if (bigfs_read(fs, ino, 0, h, 64) != 64 || memcmp(h, "RFS1", 4) != 0) return -1;
    if (name_out) { memcpy(name_out, h + 4, FS_NAME_MAX); name_out[FS_NAME_MAX - 1] = 0; }
    if (len_out) *len_out = (uint32_t)h[52] | ((uint32_t)h[53] << 8) | ((uint32_t)h[54] << 16) | ((uint32_t)h[55] << 24);
    return 0;
}
int fs_spill_get(int slot, char *buf, uint32_t len)
{
    struct bigfs *fs = g_spill_state > 0 ? g_fs : 0; if (!fs) return -1;
    char p[32]; spill_path(slot, p); uint32_t ino;
    if (bigfs_lookup(fs, p, &ino) < 0) return -1;
    return bigfs_read(fs, ino, 64, buf, len) == (int64_t)len ? 0 : -1;
}
void fs_spill_del(int slot)
{
    struct bigfs *fs = g_spill_state > 0 ? g_fs : 0; if (!fs) return;
    char p[32], t[32]; spill_path(slot, p); spill_tmp_path(slot, t); (void)bigfs_unlink(fs, p); (void)bigfs_unlink(fs, t);
}

int bigfs_sys_autoexec(void (*run)(const char *line))
{
    uint8_t sec[512];
    if (virtio_blk_read_n(SOMA_LBA_BIGFS_START, sec, 1) != 0 || sec[0] != 'S' || sec[1] != 'O' || sec[2] != 'M' || sec[3] != 'A') return 0;
    int e; struct bigfs *fs = bigfs_sys(&e);
    uint32_t ino; struct bigfs_stat st;
    if (!fs || bigfs_lookup(fs, "/autoexec.cmd", &ino) != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0) return 0;
    uint32_t n = st.size > 2047 ? 2047 : (uint32_t)st.size;
    char *b = (char *)kmalloc(n + 1);
    if (!b) return 0;
    int64_t g = bigfs_read(fs, ino, 0, b, n);
    int ran = 0;
    if (g > 0) {
        b[g] = 0;
        char *p = b;
        while (*p) {
            char *eol = p; while (*eol && *eol != '\n') eol++;
            char save = *eol; *eol = 0;
            char *s = p; while (*s == ' ' || *s == '\t') s++;
            uint32_t l = 0; while (s[l]) l++;
            while (l && (s[l - 1] == '\r' || s[l - 1] == ' ')) s[--l] = 0;
            if (l && s[0] != '#') { run(s); ran++; }
            if (!save) break;
            p = eol + 1;
        }
    }
    kfree(b);
    return ran;
}

/* ---------------------------------------------------------------- comando 'big' */
struct ob { char *p; uint32_t max, pos; };
static void ob_s(struct ob *o, const char *s) { while (*s && o->pos + 1 < o->max) o->p[o->pos++] = *s++; o->p[o->pos < o->max ? o->pos : o->max - 1] = 0; }
static void ob_d(struct ob *o, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ob_s(o, b + i); }
static void ob_err(struct ob *o, const char *what, int e) { ob_s(o, what); ob_s(o, ": "); ob_s(o, bigfs_strerror(e)); }

static const char *next_tok(const char **p, char *buf, uint32_t max)
{
    while (**p == ' ') (*p)++;
    uint32_t n = 0;
    while (**p && **p != ' ' && n + 1 < max) buf[n++] = *(*p)++;
    buf[n] = 0;
    return n ? buf : 0;
}
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static int do_bench(struct ob *o, uint32_t mib)
{
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { ob_err(o, "big bench", e); return 1; }
    if (mib < 1) mib = 4;
    if (mib > 64) mib = 64;
    const uint32_t CH = 65536;
    uint8_t *buf = (uint8_t *)kmalloc(CH);
    if (!buf) { ob_s(o, "big bench: sin memoria"); return 1; }
    uint32_t id; int r = bigfs_create(fs, "/.bench", &id);
    if (r == BIGFS_E_EXIST) { bigfs_unlink(fs, "/.bench"); r = bigfs_create(fs, "/.bench", &id); }
    if (r) { kfree(buf); ob_err(o, "big bench", r); return 1; }
    uint64_t total = (uint64_t)mib * 1024 * 1024, off = 0;
    uint64_t t0 = timer_get_uptime_ms();
    int bad = 0;
    while (off < total && !bad) {
        for (uint32_t i = 0; i < CH; ++i) buf[i] = (uint8_t)((off + i) * 2654435761u >> 13);
        int64_t w = bigfs_write(fs, id, off, buf, CH);
        if (w != (int64_t)CH) { ob_err(o, "big bench: escritura", (int)w); bad = 1; break; }
        off += CH;
    }
    uint64_t t1 = timer_get_uptime_ms();
    uint64_t mism = 0; off = 0;
    while (off < total && !bad) {
        int64_t g = bigfs_read(fs, id, off, buf, CH);
        if (g != (int64_t)CH) { ob_err(o, "big bench: lectura", (int)g); bad = 1; break; }
        for (uint32_t i = 0; i < CH; ++i) if (buf[i] != (uint8_t)((off + i) * 2654435761u >> 13)) mism++;
        off += CH;
    }
    uint64_t t2 = timer_get_uptime_ms();
    bigfs_unlink(fs, "/.bench");
    kfree(buf);
    if (bad) return 1;
    uint64_t wms = t1 - t0 ? t1 - t0 : 1, rms = t2 - t1 ? t2 - t1 : 1;
    ob_s(o, "big bench "); ob_d(o, mib); ob_s(o, " MiB: escritura "); ob_d(o, total * 1000 / wms / 1048576);
    ob_s(o, " MiB/s ("); ob_d(o, wms); ob_s(o, " ms), lectura+verificacion "); ob_d(o, total * 1000 / rms / 1048576);
    ob_s(o, " MiB/s ("); ob_d(o, rms); ob_s(o, " ms), bytes distintos: "); ob_d(o, mism);
    return 1;
}

int bigfs_sys_command(const char *args, char *out, uint32_t max)
{
    struct ob o = { out, max, 0 }; if (max) out[0] = 0;
    char sub[16], a1[BIGFS_PATH_MAX + 1], a2[BIGFS_PATH_MAX + 1];
    const char *p = args ? args : "";
    if (!next_tok(&p, sub, sizeof sub)) {
        ob_s(&o, "uso: big info | format yes | ls [ruta] | mkdir r | rm r | mv a b | stat r | cat r [n] | put <vfs> <ruta> | get <ruta> <vfs> | write <ruta> <texto> | truncate r n | fsck | bench [MiB]");
        return 1;
    }
    if (str_eq(sub, "format")) {
        if (!next_tok(&p, a1, sizeof a1) || !str_eq(a1, "yes")) { ob_s(&o, "big format BORRA todos los archivos grandes. Confirma con: big format yes"); return 1; }
        int r = bigfs_sys_format();
        if (r) ob_err(&o, "big format", r); else ob_s(&o, "BIGFS formateado");
        kprint(out); kprint("\n");
        return 1;
    }
    if (str_eq(sub, "bench")) {
        uint32_t mib = 0; if (next_tok(&p, a1, sizeof a1)) for (const char *q = a1; *q >= '0' && *q <= '9'; ++q) mib = mib * 10 + (uint32_t)(*q - '0');
        do_bench(&o, mib); kprint(out); kprint("\n"); return 1;
    }
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) { ob_err(&o, "big", e); kprint(out); kprint("\n"); return 1; }

    if (str_eq(sub, "info")) {
        struct bigfs_info i; bigfs_info(fs, &i);
        ob_s(&o, "BIGFS: "); ob_d(&o, i.total_clusters * i.cluster_bytes / 1048576); ob_s(&o, " MiB totales, ");
        ob_d(&o, i.free_clusters * i.cluster_bytes / 1048576); ob_s(&o, " MiB libres, inodos ");
        ob_d(&o, i.used_inodes); ob_s(&o, "/"); ob_d(&o, i.total_inodes);
        if (i.repaired) { ob_s(&o, ", DESCARTADOS al montar: "); ob_d(&o, i.repaired); ob_s(&o, " (ejecuta 'big fsck')"); }
        ob_s(&o, ", cache de lectura: "); ob_d(&o, (uint32_t)i.cache_hits); ob_s(&o, " aciertos / "); ob_d(&o, (uint32_t)i.cache_misses); ob_s(&o, " fallos");
    } else if (str_eq(sub, "fsck")) {
        struct bigfs_dev d; struct bigfs_fsck_report rep;
        if (!make_dev(&d)) { ob_s(&o, "sin region"); } else {
            int r = bigfs_fsck(&d, &rep);
            ob_s(&o, "fsck: "); ob_d(&o, rep.problems); ob_s(&o, " problemas (crc "); ob_d(&o, rep.bad_crc); ob_s(&o, ", extents "); ob_d(&o, rep.bad_extent);
            ob_s(&o, ", solapes "); ob_d(&o, rep.overlap); ob_s(&o, ", huerfanos "); ob_d(&o, rep.orphan); ob_s(&o, ", dir "); ob_d(&o, rep.bad_dir);
            ob_s(&o, ", nombres "); ob_d(&o, rep.bad_name + rep.dup_name); ob_s(&o, ")"); if (r && !rep.problems) ob_err(&o, " ", r);
        }
    } else if (str_eq(sub, "ls")) {
        if (!next_tok(&p, a1, sizeof a1)) { a1[0] = '/'; a1[1] = 0; }
        uint32_t dir; int t = bigfs_lookup(fs, a1, &dir);
        if (t < 0) ob_err(&o, a1, t);
        else if (t != (int)BIGFS_T_DIR) ob_s(&o, "no es un directorio");
        else {
            uint32_t cur = 0; struct bigfs_stat st; int n = 0;
            while (bigfs_readdir(fs, dir, &cur, &st) == 1) {
                char line[120]; struct ob l = { line, sizeof line, 0 };
                ob_s(&l, st.type == BIGFS_T_DIR ? "d " : "- "); ob_d(&l, st.size); ob_s(&l, " "); ob_s(&l, st.name); ob_s(&l, st.type == BIGFS_T_DIR ? "/\n" : "\n");
                kprint(line); n++;                       /* el listado va completo por serie; en el terminal, el resumen */
            }
            ob_d(&o, (uint64_t)n); ob_s(&o, " entradas (listado por la consola serie)");
        }
    } else if (str_eq(sub, "mkdir")) {
        if (!next_tok(&p, a1, sizeof a1)) { ob_s(&o, "uso: big mkdir <ruta>"); } else { int r = bigfs_mkdir(fs, a1); if (r) ob_err(&o, a1, r); else ob_s(&o, "ok"); }
    } else if (str_eq(sub, "rm")) {
        if (!next_tok(&p, a1, sizeof a1)) { ob_s(&o, "uso: big rm <ruta>"); } else {
            int r = bigfs_unlink(fs, a1); if (r == BIGFS_E_ISDIR) r = bigfs_rmdir(fs, a1);
            if (r) ob_err(&o, a1, r); else ob_s(&o, "borrado");
        }
    } else if (str_eq(sub, "mv")) {
        if (!next_tok(&p, a1, sizeof a1) || !next_tok(&p, a2, sizeof a2)) ob_s(&o, "uso: big mv <origen> <destino>");
        else { int r = bigfs_rename(fs, a1, a2); if (r) ob_err(&o, "mv", r); else ob_s(&o, "ok"); }
    } else if (str_eq(sub, "stat")) {
        uint32_t id; struct bigfs_stat st;
        if (!next_tok(&p, a1, sizeof a1)) ob_s(&o, "uso: big stat <ruta>");
        else if (bigfs_lookup(fs, a1, &id) < 0 || bigfs_stat(fs, id, &st) < 0) ob_s(&o, "no existe");
        else { ob_s(&o, st.type == BIGFS_T_DIR ? "directorio " : "archivo "); ob_s(&o, a1); ob_s(&o, ": "); ob_d(&o, st.size); ob_s(&o, " bytes, "); ob_d(&o, st.nextents); ob_s(&o, " extent(s), modificado "); ob_d(&o, st.mtime); }
    } else if (str_eq(sub, "cat")) {
        uint32_t id; uint32_t n = 1024;
        if (!next_tok(&p, a1, sizeof a1)) ob_s(&o, "uso: big cat <ruta> [bytes]");
        else {
            if (next_tok(&p, a2, sizeof a2)) { n = 0; for (const char *q = a2; *q >= '0' && *q <= '9'; ++q) n = n * 10 + (uint32_t)(*q - '0'); }
            if (n > 4096) n = 4096;
            int t = bigfs_lookup(fs, a1, &id);
            if (t < 0) ob_err(&o, a1, t);
            else {
                uint8_t *b = (uint8_t *)kmalloc(n + 1);
                if (b) {
                    int64_t g = bigfs_read(fs, id, 0, b, n);
                    if (g < 0) ob_err(&o, a1, (int)g);
                    else { for (int64_t i = 0; i < g; ++i) { char c[2] = { (b[i] >= 32 && b[i] < 127) || b[i] == '\n' ? (char)b[i] : '.', 0 }; ob_s(&o, c); } }
                    kfree(b);
                }
            }
        }
    } else if (str_eq(sub, "put")) {          /* copia un archivo del VFS pequeno al almacen grande */
        if (!next_tok(&p, a1, sizeof a1) || !next_tok(&p, a2, sizeof a2)) ob_s(&o, "uso: big put <archivo_vfs> <ruta_big>");
        else {
            uint32_t ul = 0; char *b = vfs_read_alloc(a1, &ul);   /* PARCHE 087 */
            if (!b) ob_s(&o, "no se puede leer el origen (no existe o sin memoria)");
            else {
                int n = (int)ul;
                if (n < 0) ob_s(&o, "no se puede leer el origen");
                else {
                    uint32_t id; int r = bigfs_create(fs, a2, &id);
                    if (r == BIGFS_E_EXIST) { r = bigfs_lookup(fs, a2, &id); r = r == (int)BIGFS_T_FILE ? bigfs_truncate(fs, id, 0) : BIGFS_E_ISDIR; }
                    if (r >= 0) { int64_t w = bigfs_write(fs, id, 0, b, (uint64_t)n); r = w < 0 ? (int)w : 0; }
                    if (r) ob_err(&o, "put", r); else { ob_s(&o, "copiado: "); ob_d(&o, (uint64_t)n); ob_s(&o, " bytes"); }
                }
                kfree(b);
            }
        }
    } else if (str_eq(sub, "get")) {          /* copia del almacen grande al VFS (hasta 16 KiB) */
        if (!next_tok(&p, a1, sizeof a1) || !next_tok(&p, a2, sizeof a2)) ob_s(&o, "uso: big get <ruta_big> <archivo_vfs>");
        else {
            uint32_t id; struct bigfs_stat st; int t = bigfs_lookup(fs, a1, &id);
            if (t < 0) ob_err(&o, a1, t);
            else if (t != (int)BIGFS_T_FILE) ob_s(&o, "no es un archivo");
            else { bigfs_stat(fs, id, &st);
                if (st.size > FS_FILE_MAX) ob_s(&o, "el archivo no cabe en el VFS (max 32 MiB)");
                else { char *b = (char *)kmalloc((size_t)st.size + 1);
                    if (!b) ob_s(&o, "sin memoria");
                    else { int64_t g = bigfs_read(fs, id, 0, b, st.size); if (g < 0) ob_err(&o, a1, (int)g); else if (vfs_write(a2, b, (uint32_t)g) < 0) ob_s(&o, "no se pudo escribir en el VFS"); else ob_s(&o, "copiado"); kfree(b); } } }
        }
    } else if (str_eq(sub, "write")) {        /* big write <ruta> <texto...> : crea o sustituye */
        if (!next_tok(&p, a1, sizeof a1)) ob_s(&o, "uso: big write <ruta> <texto>");
        else {
            while (*p == ' ') p++;
            uint32_t id, n = 0; while (p[n]) n++;
            int r = bigfs_create(fs, a1, &id);
            if (r == BIGFS_E_EXIST) { r = bigfs_lookup(fs, a1, &id); r = r == (int)BIGFS_T_FILE ? bigfs_truncate(fs, id, 0) : BIGFS_E_ISDIR; }
            if (r >= 0) { int64_t w = bigfs_write(fs, id, 0, p, n); r = w < 0 ? (int)w : 0; }
            if (r) ob_err(&o, "write", r); else ob_s(&o, "ok");
        }
    } else if (str_eq(sub, "truncate")) {
        uint32_t id; uint64_t n = 0;
        if (!next_tok(&p, a1, sizeof a1) || !next_tok(&p, a2, sizeof a2)) ob_s(&o, "uso: big truncate <ruta> <bytes>");
        else { for (const char *q = a2; *q >= '0' && *q <= '9'; ++q) n = n * 10 + (uint64_t)(*q - '0');
            int t = bigfs_lookup(fs, a1, &id); int r = t < 0 ? t : bigfs_truncate(fs, id, n); if (r) ob_err(&o, a1, r); else ob_s(&o, "ok"); }
    } else {
        ob_s(&o, "subcomando desconocido: "); ob_s(&o, sub);
    }
    kprint(out); kprint("\n");
    return 1;
}
