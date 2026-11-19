/* Pruebas en host del BIGFS real (src/fs/bigfs.c) sobre un disco en RAM con inyeccion de fallos:
 * basicas, archivos grandes, fragmentacion, ENOSPC, errores de E/S, CORTE DE ENERGIA en cada escritura de sector,
 * corrupcion forjada (CRC, extents solapados, ciclos) y fuzzing contra un modelo. ASan/UBSan.
 * Con argumentos (--verify IMG MANIFEST) comprueba una imagen escrita por tools/bigfs.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "bigfs.h"

/* ---------------------------------------------------------------- disco en RAM con fallos */
struct ram {
    uint8_t *d; uint64_t sectors;
    long writes, crash_after;     /* a partir de crash_after escrituras, el resto se pierde (corte de energia) */
    long fail_at;                 /* la escritura numero fail_at devuelve error (una vez) */
    uint64_t clock;
};
static long g_dev_reads, g_dev_read_sectors;
static int r_read(void *c, uint64_t lba, void *b, uint32_t n) { struct ram *r = c; g_dev_reads++; g_dev_read_sectors += n; if (lba + n > r->sectors) return -1; memcpy(b, r->d + lba * 512, (size_t)n * 512); return 0; }
static int r_write(void *c, uint64_t lba, const void *b, uint32_t n)
{
    struct ram *r = c;
    if (lba + n > r->sectors) return -1;
    for (uint32_t i = 0; i < n; ++i) {            /* cada sector es atomico; cada uno cuenta como una escritura */
        long idx = r->writes++;
        if (r->fail_at >= 0 && idx == r->fail_at) { r->fail_at = -1; return -1; }
        if (r->crash_after >= 0 && idx >= r->crash_after) continue;           /* se pierde */
        memcpy(r->d + (lba + i) * 512, (const uint8_t *)b + (size_t)i * 512, 512);
    }
    return 0;
}
static uint64_t r_now(void *c) { return ++((struct ram *)c)->clock; }
static struct bigfs_dev mkdev(struct ram *r) {
    struct bigfs_dev d; memset(&d, 0, sizeof(d));
    d.ctx = r; d.read = r_read; d.write = r_write; d.total_sectors = r->sectors;
    d.alloc = malloc; d.free = free; d.now = r_now; return d;
}
static void ram_init(struct ram *r, uint64_t sectors) { memset(r, 0, sizeof(*r)); r->d = calloc(sectors, 512); r->sectors = sectors; r->crash_after = -1; r->fail_at = -1; }
static void ram_free(struct ram *r) { free(r->d); }

static uint64_t rng_s = 88172645463325252ull;
static uint64_t rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return rng_s; }
static uint64_t fnv(const void *p, size_t n, uint64_t h) { const uint8_t *b = p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static void fillpat(uint8_t *b, size_t n, uint64_t seed) { for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)((i * 2654435761u + seed * 40503u) >> 13); }

static struct bigfs *mount_ok(struct bigfs_dev *d) { int e; struct bigfs *fs = bigfs_mount(d, &e); CHECK(fs != 0); if (!fs) printf("  mount: %s\n", bigfs_strerror(e)); return fs; }

/* ---------------------------------------------------------------- basicas */
static void test_basico(void)
{
    struct ram r; ram_init(&r, 8192); struct bigfs_dev d = mkdev(&r);
    int e; CHECK(bigfs_mount(&d, &e) == 0); CHECK_EQ_INT(e, BIGFS_E_NOTFMT);     /* sin formatear */
    CHECK_EQ_INT(bigfs_format(&d, 0), 0);
    struct bigfs *fs = mount_ok(&d);
    uint32_t a, b, f, g; struct bigfs_stat st;
    CHECK_EQ_INT(bigfs_mkdir(fs, "/a"), 0);
    CHECK_EQ_INT(bigfs_mkdir(fs, "/a/b"), 0);
    CHECK_EQ_INT(bigfs_mkdir(fs, "/a"), BIGFS_E_EXIST);
    CHECK_EQ_INT(bigfs_mkdir(fs, "/x/y"), BIGFS_E_NOENT);
    CHECK_EQ_INT(bigfs_create(fs, "/a/b/f.txt", &f), 0);
    CHECK_EQ_INT(bigfs_create(fs, "/a/b/f.txt", &g), BIGFS_E_EXIST);
    CHECK_EQ_INT(bigfs_create(fs, "/a/b/f.txt/z", &g), BIGFS_E_NOTDIR);
    CHECK_EQ_INT(bigfs_write(fs, f, 0, "hola mundo", 10), 10);
    char buf[64] = {0};
    CHECK_EQ_INT(bigfs_read(fs, f, 0, buf, 63), 10); CHECK_STR(buf, "hola mundo");
    CHECK_EQ_INT(bigfs_read(fs, f, 5, buf, 63), 5);
    CHECK_EQ_INT(bigfs_read(fs, f, 10, buf, 5), 0);                  /* EOF */
    CHECK_EQ_INT(bigfs_read(fs, f, 99, buf, 5), 0);
    CHECK_EQ_INT(bigfs_lookup(fs, "/a/b/f.txt", &g), BIGFS_T_FILE); CHECK_EQ_INT(g, f);
    CHECK_EQ_INT(bigfs_lookup(fs, "a//b/./f.txt", &g), BIGFS_T_FILE);   /* normalizacion */
    CHECK_EQ_INT(bigfs_lookup(fs, "/a/b/../b/f.txt", &g), BIGFS_T_FILE);
    CHECK_EQ_INT(bigfs_lookup(fs, "/", &g), BIGFS_T_DIR); CHECK_EQ_INT(g, 0);
    CHECK_EQ_INT(bigfs_lookup(fs, "/../..", &g), BIGFS_T_DIR);       /* .. de la raiz es la raiz */
    CHECK_EQ_INT(bigfs_lookup(fs, "/nada", &g), BIGFS_E_NOENT);
    CHECK_EQ_INT(bigfs_stat(fs, f, &st), 0); CHECK_EQ_INT(st.size, 10); CHECK_STR(st.name, "f.txt"); CHECK_EQ_INT(st.type, BIGFS_T_FILE);
    CHECK_EQ_INT(bigfs_write(fs, 0, 0, "x", 1), BIGFS_E_INVAL);      /* la raiz no es un archivo */
    bigfs_lookup(fs, "/a", &a); bigfs_lookup(fs, "/a/b", &b);
    CHECK_EQ_INT(bigfs_write(fs, a, 0, "x", 1), BIGFS_E_ISDIR);
    CHECK_EQ_INT(bigfs_read(fs, a, 0, buf, 1), BIGFS_E_ISDIR);
    CHECK_EQ_INT(bigfs_unlink(fs, "/a"), BIGFS_E_ISDIR);
    CHECK_EQ_INT(bigfs_rmdir(fs, "/a/b/f.txt"), BIGFS_E_NOTDIR);
    CHECK_EQ_INT(bigfs_rmdir(fs, "/a"), BIGFS_E_NOTEMPTY);
    CHECK_EQ_INT(bigfs_rmdir(fs, "/"), BIGFS_E_INVAL);
    /* nombres */
    char longname[100]; memset(longname, 'n', 99); longname[99] = 0; char p[130]; snprintf(p, sizeof p, "/%s", longname);
    CHECK_EQ_INT(bigfs_create(fs, p, &g), BIGFS_E_NAME);
    memset(longname, 'n', 63); longname[63] = 0; snprintf(p, sizeof p, "/%s", longname);
    CHECK_EQ_INT(bigfs_create(fs, p, &g), 0);                        /* 63 caracteres: justo el maximo */
    CHECK_EQ_INT(bigfs_create(fs, "/con\x01" "ctl", &g), BIGFS_E_NAME);
    CHECK_EQ_INT(bigfs_create(fs, "/", &g), BIGFS_E_EXIST);
    /* readdir */
    bigfs_create(fs, "/a/c.txt", &g); bigfs_mkdir(fs, "/a/d");
    uint32_t cur = 0; int n = 0; while (bigfs_readdir(fs, a, &cur, &st) == 1) n++;
    CHECK_EQ_INT(n, 3);                                              /* b, c.txt, d */
    cur = 0; CHECK_EQ_INT(bigfs_readdir(fs, f, &cur, &st), BIGFS_E_NOTDIR);
    /* rename */
    CHECK_EQ_INT(bigfs_rename(fs, "/a/b/f.txt", "/a/d/g.txt"), 0);
    CHECK_EQ_INT(bigfs_lookup(fs, "/a/b/f.txt", &g), BIGFS_E_NOENT);
    CHECK_EQ_INT(bigfs_lookup(fs, "/a/d/g.txt", &g), BIGFS_T_FILE); CHECK_EQ_INT(g, f);
    CHECK_EQ_INT(bigfs_read(fs, f, 0, buf, 10), 10);
    CHECK_EQ_INT(bigfs_rename(fs, "/a/d/g.txt", "/a/c.txt"), BIGFS_E_EXIST);
    CHECK_EQ_INT(bigfs_rename(fs, "/a", "/a/b/zz"), BIGFS_E_INVAL);          /* dentro de si mismo */
    CHECK_EQ_INT(bigfs_rename(fs, "/a", "/a"), BIGFS_E_EXIST);
    CHECK_EQ_INT(bigfs_rename(fs, "/a/b", "/moved"), 0);
    CHECK_EQ_INT(bigfs_lookup(fs, "/moved", &g), BIGFS_T_DIR);
    CHECK_EQ_INT(bigfs_rename(fs, "/nada", "/x"), BIGFS_E_NOENT);
    /* persistencia */
    bigfs_unmount(fs); fs = mount_ok(&d);
    CHECK_EQ_INT(bigfs_lookup(fs, "/a/d/g.txt", &g), BIGFS_T_FILE);
    memset(buf, 0, sizeof buf); CHECK_EQ_INT(bigfs_read(fs, g, 0, buf, 63), 10); CHECK_STR(buf, "hola mundo");
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0); CHECK_EQ_INT(rep.problems, 0);
    bigfs_unmount(fs); ram_free(&r);
}

/* ---------------------------------------------------------------- archivos grandes */
static void test_grande(void)
{
    struct ram r; ram_init(&r, 16384);                       /* 8 MiB */
    struct bigfs_dev d = mkdev(&r); CHECK_EQ_INT(bigfs_format(&d, 0), 0);
    struct bigfs *fs = mount_ok(&d); struct bigfs_info i0, i1; bigfs_info(fs, &i0);
    const size_t N = 3 * 1024 * 1024 + 777;                  /* 3 MiB y pico */
    uint8_t *ref = malloc(N), *got = malloc(N); fillpat(ref, N, 1);
    uint32_t f; CHECK_EQ_INT(bigfs_create(fs, "/big.bin", &f), 0);
    size_t off = 0;
    while (off < N) { size_t n = 1 + (size_t)(rnd() % 70000); if (off + n > N) n = N - off; if (bigfs_write(fs, f, off, ref + off, n) != (int64_t)n) { CHECK(0); break; } off += n; }
    struct bigfs_stat st; bigfs_stat(fs, f, &st); CHECK_EQ_INT(st.size, N);
    CHECK(st.nextents >= 1 && st.nextents <= 2);              /* un archivo secuencial en disco vacio: contiguo */
    CHECK_EQ_INT(bigfs_read(fs, f, 0, got, N), (int64_t)N); CHECK(memcmp(ref, got, N) == 0);
    /* lecturas a desplazamientos y longitudes impares */
    for (int it = 0; it < 300; ++it) {
        size_t o = (size_t)(rnd() % N), n = 1 + (size_t)(rnd() % 20000); if (o + n > N) n = N - o;
        memset(got, 0xEE, n);
        CHECK_EQ_INT(bigfs_read(fs, f, o, got, n), (int64_t)n); CHECK(memcmp(ref + o, got, n) == 0);
    }
    /* sobrescritura en medio (sin cambiar el tamano) */
    uint8_t patch[5000]; fillpat(patch, sizeof patch, 99);
    CHECK_EQ_INT(bigfs_write(fs, f, 100001, patch, sizeof patch), (int64_t)sizeof patch); memcpy(ref + 100001, patch, sizeof patch);
    CHECK_EQ_INT(bigfs_read(fs, f, 0, got, N), (int64_t)N); CHECK(memcmp(ref, got, N) == 0);
    /* hueco: escribir mas alla del final -> ceros en medio */
    uint8_t *ref2 = calloc(N + 100000 + 10, 1); memcpy(ref2, ref, N);
    CHECK_EQ_INT(bigfs_write(fs, f, N + 100000, "FIN", 3), 3); memcpy(ref2 + N + 100000, "FIN", 3);
    uint8_t *got2 = malloc(N + 100003);
    CHECK_EQ_INT(bigfs_read(fs, f, 0, got2, N + 100003), (int64_t)(N + 100003)); CHECK(memcmp(ref2, got2, N + 100003) == 0);
    /* truncar y volver a crecer: lo que queda entre medias son ceros, no datos viejos */
    CHECK_EQ_INT(bigfs_truncate(fs, f, 1000), 0);
    CHECK_EQ_INT(bigfs_truncate(fs, f, 20000), 0);
    CHECK_EQ_INT(bigfs_read(fs, f, 0, got, 20000), 20000);
    CHECK(memcmp(got, ref, 1000) == 0);
    int ceros = 1; for (int k = 1000; k < 20000; ++k) if (got[k] != 0) ceros = 0; CHECK(ceros);
    /* persistencia tras remontar */
    bigfs_unmount(fs); fs = mount_ok(&d); uint32_t g; CHECK_EQ_INT(bigfs_lookup(fs, "/big.bin", &g), BIGFS_T_FILE);
    CHECK_EQ_INT(bigfs_read(fs, g, 0, got, 20000), 20000); CHECK(memcmp(got, ref, 1000) == 0);
    /* borrar devuelve todos los clusteres */
    CHECK_EQ_INT(bigfs_unlink(fs, "/big.bin"), 0); bigfs_info(fs, &i1);
    CHECK_EQ_INT(i1.free_clusters, i0.free_clusters);
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    free(ref); free(got); free(ref2); free(got2); bigfs_unmount(fs); ram_free(&r);
}

/* ---------------------------------------------------------------- espacio y fragmentacion */
static void test_enospc_y_fragmentacion(void)
{
    struct ram r; ram_init(&r, 72 + 64 * 8 + 4);              /* 128 inodos (72 sectores) y ~64 clusteres de datos: el limite es el espacio */
    struct bigfs_dev d = mkdev(&r); CHECK_EQ_INT(bigfs_format(&d, 128), 0);
    struct bigfs *fs = mount_ok(&d); struct bigfs_info i0; bigfs_info(fs, &i0);
    uint8_t blk[4096]; fillpat(blk, sizeof blk, 5);
    char nm[32]; uint32_t id; int created = 0;
    for (int i = 0; i < 200; ++i) {                           /* llenar con archivos de 1 cluster */
        snprintf(nm, sizeof nm, "/f%03d", i);
        int rc = bigfs_create(fs, nm, &id);
        if (rc) { CHECK_EQ_INT(rc, BIGFS_E_NOINODE); break; }
        int64_t w = bigfs_write(fs, id, 0, blk, sizeof blk);
        if (w < 0) { CHECK_EQ_INT(w, BIGFS_E_NOSPC); bigfs_unlink(fs, nm); break; }
        created++;
    }
    CHECK(created > 20);
    struct bigfs_info i1; bigfs_info(fs, &i1);
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    /* un archivo que no cabe: error, nada cambia */
    uint32_t big; CHECK_EQ_INT(bigfs_create(fs, "/zz", &big), 0);
    uint8_t *huge = malloc(2 * 1024 * 1024); fillpat(huge, 2 * 1024 * 1024, 1);
    struct bigfs_info before, after; bigfs_info(fs, &before);
    int64_t w = bigfs_write(fs, big, 0, huge, 2 * 1024 * 1024); CHECK_EQ_INT(w, BIGFS_E_NOSPC);
    bigfs_info(fs, &after); CHECK_EQ_INT(after.free_clusters, before.free_clusters);
    struct bigfs_stat st; bigfs_stat(fs, big, &st); CHECK_EQ_INT(st.size, 0);
    bigfs_unlink(fs, "/zz");
    /* borrar uno de cada dos: quedan huecos de 1 cluster */
    for (int i = 0; i < created; i += 2) { snprintf(nm, sizeof nm, "/f%03d", i); CHECK_EQ_INT(bigfs_unlink(fs, nm), 0); }
    bigfs_info(fs, &after); CHECK(after.free_clusters >= (uint64_t)(created / 2));
    CHECK_EQ_INT(bigfs_create(fs, "/frag5", &id), 0);         /* 5 clusteres en 5 huecos: cabe (<= 20 extents) */
    CHECK_EQ_INT(bigfs_write(fs, id, 0, huge, 5 * 4096), 5 * 4096);
    bigfs_stat(fs, id, &st); CHECK(st.nextents > 1);
    uint8_t *got = malloc(5 * 4096); CHECK_EQ_INT(bigfs_read(fs, id, 0, got, 5 * 4096), 5 * 4096); CHECK(memcmp(got, huge, 5 * 4096) == 0);
    /* 27 clusteres libres en huecos aislados: necesita > 20 extents -> FBIG y se deshace por completo */
    bigfs_info(fs, &before);
    uint32_t id2; CHECK_EQ_INT(bigfs_create(fs, "/frag_mucho", &id2), 0);
    printf("  fragmentacion: %d archivos creados, %llu clusteres libres en huecos antes del intento grande\n", created, (unsigned long long)before.free_clusters);
    CHECK(before.free_clusters > 22);                         /* si no, la prueba no ejercita el limite de extents */
    if (before.free_clusters > 22) {
        int64_t w2 = bigfs_write(fs, id2, 0, huge, (size_t)before.free_clusters * 4096);
        CHECK_EQ_INT(w2, BIGFS_E_FBIG);                        /* hay espacio de sobra, pero en >20 trozos */
        bigfs_info(fs, &after); CHECK_EQ_INT(after.free_clusters, before.free_clusters);
        bigfs_stat(fs, id2, &st); CHECK_EQ_INT(st.size, 0); CHECK_EQ_INT(st.nextents, 0);
    }
    CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    /* borrar todo devuelve todo */
    uint32_t cur = 0; struct bigfs_stat e; char names[256][80]; int nn = 0;
    while (bigfs_readdir(fs, 0, &cur, &e) == 1 && nn < 256) { snprintf(names[nn], 80, "/%s", e.name); nn++; }
    for (int i = 0; i < nn; ++i) CHECK_EQ_INT(bigfs_unlink(fs, names[i]), 0);
    bigfs_info(fs, &after); CHECK_EQ_INT(after.free_clusters, i0.free_clusters);
    (void)i1; free(huge); free(got); bigfs_unmount(fs); ram_free(&r);
}

/* ---------------------------------------------------------------- errores de E/S */
static void workload_simple(struct bigfs *fs)
{
    uint32_t f; uint8_t b[9000]; fillpat(b, sizeof b, 3);
    bigfs_mkdir(fs, "/m");
    bigfs_create(fs, "/m/a", &f); bigfs_write(fs, f, 0, b, 9000); bigfs_write(fs, f, 9000, b, 5000);
    bigfs_create(fs, "/m/b", &f); bigfs_write(fs, f, 0, b, 100);
    bigfs_rename(fs, "/m/b", "/m/c"); bigfs_truncate(fs, f, 10);
    bigfs_unlink(fs, "/m/a");
}

static void test_errores_de_es(void)
{
    /* se hace fallar cada escritura posible, una a una: nunca debe romperse el disco ni el estado en RAM */
    long total;
    {   struct ram r; ram_init(&r, 4096); struct bigfs_dev d = mkdev(&r); bigfs_format(&d, 0);
        struct bigfs *fs = mount_ok(&d); r.writes = 0; workload_simple(fs); total = r.writes; bigfs_unmount(fs); ram_free(&r); }
    CHECK(total > 5);
    for (long k = 0; k < total; ++k) {
        struct ram r; ram_init(&r, 4096); struct bigfs_dev d = mkdev(&r); bigfs_format(&d, 0);
        struct bigfs *fs = mount_ok(&d); r.writes = 0; r.fail_at = k;
        workload_simple(fs);
        struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);          /* disco consistente */
        /* y el fs en RAM sigue sirviendo: se puede seguir operando */
        uint32_t f; int rc = bigfs_create(fs, "/despues", &f);
        CHECK(rc == 0 || rc == BIGFS_E_EXIST);
        if (rc == 0) CHECK_EQ_INT(bigfs_write(fs, f, 0, "ok", 2), 2);
        bigfs_unmount(fs);
        fs = mount_ok(&d); bigfs_unmount(fs);                                       /* y el disco monta */
        CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
        ram_free(&r);
    }
}

/* ---------------------------------------------------------------- corte de energia */
struct snap { uint64_t h; };
static uint64_t snap_dir(struct bigfs *fs, uint32_t dir, const char *prefix, uint64_t h)
{
    uint32_t cur = 0; struct bigfs_stat e; char path[300];
    /* orden canonico: por nombre (insercion sencilla) */
    struct bigfs_stat list[128]; int n = 0;
    while (bigfs_readdir(fs, dir, &cur, &e) == 1 && n < 128) {
        int j = n++; while (j > 0 && strcmp(list[j - 1].name, e.name) > 0) { list[j] = list[j - 1]; j--; } list[j] = e;
    }
    for (int i = 0; i < n; ++i) {
        snprintf(path, sizeof path, "%s/%s", prefix, list[i].name);
        h = fnv(path, strlen(path), h); h = fnv(&list[i].type, sizeof(uint32_t), h);
        if (list[i].type == BIGFS_T_FILE) {
            h = fnv(&list[i].size, sizeof(uint64_t), h);
            uint8_t *b = malloc(list[i].size ? list[i].size : 1);
            int64_t rr = bigfs_read(fs, list[i].ino, 0, b, list[i].size);
            if (rr != (int64_t)list[i].size) h ^= 0xDEADBEEF;
            h = fnv(b, list[i].size, h); free(b);
        } else h = snap_dir(fs, list[i].ino, path, h);
    }
    return h;
}
static uint64_t snapshot(struct bigfs *fs) { return snap_dir(fs, 0, "", 1469598103934665603ull); }

/* carga de trabajo del test de cortes: SIN sobrescrituras en el sitio (las escrituras de crecimiento y de
 * metadatos son atomicas; una sobrescritura en el sitio puede quedar a medias y no se promete lo contrario) */
#define NOPS 26
static void run_op(struct bigfs *fs, int op)       /* UNA llamada de escritura por operacion */
{
    static uint8_t b[40000]; uint32_t f; static int init; if (!init) { fillpat(b, sizeof b, 11); init = 1; }
    switch (op) {
    case 0: bigfs_mkdir(fs, "/docs"); break;
    case 1: bigfs_mkdir(fs, "/docs/sub"); break;
    case 2: bigfs_create(fs, "/docs/a.bin", &f); break;
    case 3: bigfs_lookup(fs, "/docs/a.bin", &f); bigfs_write(fs, f, 0, b, 5000); break;
    case 4: bigfs_lookup(fs, "/docs/a.bin", &f); bigfs_write(fs, f, 5000, b + 5000, 30000); break;      /* append */
    case 5: bigfs_create(fs, "/docs/sub/b.bin", &f); break;
    case 6: bigfs_lookup(fs, "/docs/sub/b.bin", &f); bigfs_write(fs, f, 0, b, 100); break;
    case 7: bigfs_lookup(fs, "/docs/sub/b.bin", &f); bigfs_write(fs, f, 100, b, 4000); break;           /* completa el cluster */
    case 8: bigfs_rename(fs, "/docs/sub/b.bin", "/docs/b2.bin"); break;
    case 9: bigfs_lookup(fs, "/docs/a.bin", &f); bigfs_truncate(fs, f, 8000); break;                    /* encoge */
    case 10: bigfs_lookup(fs, "/docs/a.bin", &f); bigfs_truncate(fs, f, 20000); break;                  /* crece con ceros */
    case 11: bigfs_create(fs, "/c.bin", &f); break;
    case 12: bigfs_lookup(fs, "/c.bin", &f); bigfs_write(fs, f, 0, b, 40000); break;
    case 13: bigfs_rename(fs, "/docs/sub", "/sub2"); break;
    case 14: bigfs_unlink(fs, "/docs/b2.bin"); break;
    case 15: bigfs_create(fs, "/sub2/z", &f); break;
    case 16: bigfs_lookup(fs, "/sub2/z", &f); bigfs_write(fs, f, 0, b, 12345); break;
    case 17: bigfs_lookup(fs, "/c.bin", &f); bigfs_truncate(fs, f, 0); break;
    case 18: bigfs_lookup(fs, "/c.bin", &f); bigfs_write(fs, f, 0, b, 33333); break;
    case 19: bigfs_rename(fs, "/sub2", "/docs/sub3"); break;
    case 20: bigfs_unlink(fs, "/docs/sub3/z"); break;
    case 21: bigfs_rmdir(fs, "/docs/sub3"); break;
    case 22: bigfs_unlink(fs, "/c.bin"); break;
    case 23: bigfs_create(fs, "/d.bin", &f); break;
    case 24: bigfs_lookup(fs, "/d.bin", &f); bigfs_write(fs, f, 100000, "X", 1); break;                 /* hueco */
    case 25: bigfs_unlink(fs, "/d.bin"); break;
    }
}

static void test_corte_de_energia(void)
{
    uint64_t after[NOPS + 1]; long ends[NOPS + 1];
    struct ram base; ram_init(&base, 4096);
    struct bigfs_dev d0 = mkdev(&base); CHECK_EQ_INT(bigfs_format(&d0, 128), 0);
    uint8_t *img = malloc(base.sectors * 512); memcpy(img, base.d, base.sectors * 512);
    /* ejecucion de referencia: hash del estado tras cada operacion y escrituras acumuladas */
    {
        struct ram r; ram_init(&r, 4096); memcpy(r.d, img, r.sectors * 512); struct bigfs_dev d = mkdev(&r);
        struct bigfs *fs = mount_ok(&d); r.writes = 0;
        after[0] = snapshot(fs); ends[0] = 0;
        for (int op = 0; op < NOPS; ++op) { run_op(fs, op); after[op + 1] = snapshot(fs); ends[op + 1] = r.writes; }
        bigfs_unmount(fs); ram_free(&r);
    }
    long total = ends[NOPS]; CHECK(total > 40);
    int ops_con_escritura = 0; for (int i = 1; i <= NOPS; ++i) if (ends[i] > ends[i - 1]) ops_con_escritura++;
    CHECK(ops_con_escritura >= 22);
    /* corte tras k escrituras, para TODO k */
    int fallos = 0;
    for (long k = 0; k <= total; ++k) {
        struct ram r; ram_init(&r, 4096); memcpy(r.d, img, r.sectors * 512); struct bigfs_dev d = mkdev(&r);
        struct bigfs *fs = mount_ok(&d); r.writes = 0; r.crash_after = k;
        for (int op = 0; op < NOPS; ++op) run_op(fs, op);
        bigfs_unmount(fs);
        r.crash_after = -1; r.fail_at = -1;
        struct bigfs_fsck_report rep; int fe = bigfs_fsck(&d, &rep);
        int e; struct bigfs *fs2 = bigfs_mount(&d, &e);
        if (fe != 0 || rep.problems != 0 || !fs2) { fallos++; CHECK(fe == 0); CHECK(fs2 != 0); if (fallos < 4) printf("  corte tras %ld/%ld escrituras: fsck=%d problemas=%u\n", k, total, fe, rep.problems); }
        if (fs2) {
            uint64_t h = snapshot(fs2);
            /* la operacion interrumpida es la primera cuyo final supera k */
            int j = 1; while (j <= NOPS && ends[j] <= k) j++;
            int ok = 0;
            for (int q = (j > 1 ? j - 1 : 0); q <= (j <= NOPS ? j : NOPS); ++q) if (after[q] == h) ok = 1;
            if (!ok) { fallos++; CHECK(ok); if (fallos < 4) printf("  corte tras %ld/%ld: el estado no coincide ni con antes ni con despues de la op %d\n", k, total, j - 1); }
            /* y se puede seguir trabajando */
            uint32_t f; int rc = bigfs_create(fs2, "/post", &f); if (rc == 0) CHECK_EQ_INT(bigfs_write(fs2, f, 0, "ok", 2), 2);
            bigfs_unmount(fs2);
        }
        ram_free(&r);
    }
    printf("  cortes de energia probados: %ld puntos (cada escritura de sector), %d fallos\n", total + 1, fallos);
    free(img); ram_free(&base);
}

/* ---------------------------------------------------------------- corrupcion forjada */
static uint32_t crc32_t(const void *data, size_t n)
{
    uint32_t c = 0xFFFFFFFFu; const uint8_t *p = data;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
    return c ^ 0xFFFFFFFFu;
}
static uint8_t *inode_ptr(struct ram *r, uint32_t id) { return r->d + (1 + id / 2) * 512 + (id % 2) * 256; }
static void fix_crc(uint8_t *in) { uint32_t c = crc32_t(in + 8, 248); memcpy(in + 4, &c, 4); }

static void test_corrupcion(void)
{
    struct ram r; ram_init(&r, 4096); struct bigfs_dev d = mkdev(&r); bigfs_format(&d, 128);
    struct bigfs *fs = mount_ok(&d); uint32_t fa, fb, da, db, ok; uint8_t buf[6000]; fillpat(buf, sizeof buf, 2);
    bigfs_mkdir(fs, "/da"); bigfs_mkdir(fs, "/da/db");
    bigfs_create(fs, "/fa", &fa); bigfs_write(fs, fa, 0, buf, 6000);
    bigfs_create(fs, "/fb", &fb); bigfs_write(fs, fb, 0, buf, 6000);
    bigfs_create(fs, "/ok", &ok); bigfs_write(fs, ok, 0, "intacto", 7);
    bigfs_lookup(fs, "/da", &da); bigfs_lookup(fs, "/da/db", &db);
    bigfs_unmount(fs);
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    uint8_t save[256 * 2];

    /* 1) un bit cambiado -> CRC incorrecto */
    memcpy(save, inode_ptr(&r, fa), 256); inode_ptr(&r, fa)[20] ^= 1;
    CHECK(bigfs_fsck(&d, &rep) != 0); CHECK_EQ_INT(rep.bad_crc, 1);
    int e; fs = bigfs_mount(&d, &e); CHECK(fs != 0);                          /* monta y descarta lo danado */
    struct bigfs_info inf; bigfs_info(fs, &inf); CHECK_EQ_INT(inf.repaired, 1);
    uint32_t t; CHECK_EQ_INT(bigfs_lookup(fs, "/fa", &t), BIGFS_E_NOENT);
    CHECK_EQ_INT(bigfs_lookup(fs, "/ok", &t), BIGFS_T_FILE);
    char b7[8] = {0}; CHECK_EQ_INT(bigfs_read(fs, t, 0, b7, 7), 7); CHECK_STR(b7, "intacto");
    bigfs_unmount(fs); memcpy(inode_ptr(&r, fa), save, 256);
    CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);

    /* 2) extents solapados con CRC valido: fb apunta a los clusteres de fa */
    memcpy(save, inode_ptr(&r, fb), 256);
    memcpy(inode_ptr(&r, fb) + 96, inode_ptr(&r, fa) + 96, 8); fix_crc(inode_ptr(&r, fb));
    CHECK(bigfs_fsck(&d, &rep) != 0); CHECK_EQ_INT(rep.overlap, 1);
    fs = bigfs_mount(&d, &e); CHECK(fs != 0); bigfs_unmount(fs); memcpy(inode_ptr(&r, fb), save, 256);

    /* 3) extent fuera del disco */
    memcpy(save, inode_ptr(&r, fa), 256);
    uint32_t big = 0x7FFFFFF0u; memcpy(inode_ptr(&r, fa) + 96, &big, 4); fix_crc(inode_ptr(&r, fa));
    CHECK(bigfs_fsck(&d, &rep) != 0); CHECK_EQ_INT(rep.bad_extent, 1);
    memcpy(inode_ptr(&r, fa), save, 256);

    /* 4) ciclo de directorios: da -> db -> da (ambos con CRC valido) */
    memcpy(save, inode_ptr(&r, da), 256); memcpy(save + 256, inode_ptr(&r, db), 256);
    memcpy(inode_ptr(&r, da) + 8, &db, 4); fix_crc(inode_ptr(&r, da));
    CHECK(bigfs_fsck(&d, &rep) != 0); CHECK(rep.orphan >= 2);
    fs = bigfs_mount(&d, &e); CHECK(fs != 0); CHECK_EQ_INT(bigfs_lookup(fs, "/ok", &t), BIGFS_T_FILE); bigfs_unmount(fs);
    memcpy(inode_ptr(&r, da), save, 256); memcpy(inode_ptr(&r, db), save + 256, 256);
    CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);

    /* 5) nombre duplicado */
    memcpy(save, inode_ptr(&r, fb), 256); memcpy(inode_ptr(&r, fb) + 32, "fa\0", 3); fix_crc(inode_ptr(&r, fb));
    CHECK(bigfs_fsck(&d, &rep) != 0); CHECK_EQ_INT(rep.dup_name, 1);
    memcpy(inode_ptr(&r, fb), save, 256);

    /* 6) superbloque danado / ausente */
    uint8_t sb[512]; memcpy(sb, r.d, 512); r.d[40] ^= 0x55;
    CHECK(bigfs_mount(&d, &e) == 0); CHECK_EQ_INT(e, BIGFS_E_CORRUPT); CHECK(bigfs_fsck(&d, &rep) != 0); CHECK_EQ_INT(rep.bad_super, 1);
    memset(r.d, 0, 512); CHECK(bigfs_mount(&d, &e) == 0); CHECK_EQ_INT(e, BIGFS_E_NOTFMT);
    memcpy(r.d, sb, 512); CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);

    /* 7) raiz danada: no se monta */
    memcpy(save, inode_ptr(&r, 0), 256); inode_ptr(&r, 0)[0] = 1;             /* la raiz como archivo */
    fix_crc(inode_ptr(&r, 0)); CHECK(bigfs_mount(&d, &e) == 0); CHECK_EQ_INT(e, BIGFS_E_CORRUPT);
    memcpy(inode_ptr(&r, 0), save, 256);
    ram_free(&r);
}

/* ---------------------------------------------------------------- PARCHE 129: reconstruccion del superbloque */
static void rs_populate(struct bigfs_dev *d, uint64_t seed)
{
    struct bigfs *fs = mount_ok(d); uint32_t f;
    CHECK_EQ_INT(bigfs_mkdir(fs, "/docs"), 0);
    CHECK_EQ_INT(bigfs_create(fs, "/docs/a.bin", &f), 0);
    uint8_t *b = malloc(50000); fillpat(b, 50000, seed);
    CHECK_EQ_INT((int)bigfs_write(fs, f, 0, b, 50000), 50000);
    free(b); bigfs_unmount(fs);
}
static void rs_verify(struct bigfs_dev *d, uint64_t seed)
{
    struct bigfs *fs = mount_ok(d); uint32_t f;
    if (!fs) return;
    CHECK_EQ_INT(bigfs_lookup(fs, "/docs/a.bin", &f), BIGFS_T_FILE);
    uint8_t *b = malloc(50000), *w = malloc(50000); fillpat(w, 50000, seed);
    CHECK_EQ_INT((int)bigfs_read(fs, f, 0, b, 50000), 50000);
    CHECK(memcmp(b, w, 50000) == 0);
    free(b); free(w); bigfs_unmount(fs);
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(d, &rep), 0);
}
static void test_repair_super(void)
{
    struct ram r; ram_init(&r, 8192); struct bigfs_dev d = mkdev(&r); int e;
    CHECK_EQ_INT(bigfs_repair_super(&d), BIGFS_E_NOTFMT);                       /* disco en blanco: nada que reconstruir */
    { uint8_t z[512] = {0}; CHECK(memcmp(r.d, z, 512) == 0); }                  /* y no se ha escrito nada */
    CHECK_EQ_INT(bigfs_format(&d, 0), 0);
    rs_populate(&d, 7);
    CHECK_EQ_INT(bigfs_repair_super(&d), 0);                                    /* sano: no hace nada */
    uint8_t sb[512]; memcpy(sb, r.d, 512);
    CHECK(memcmp(r.d + 129 * 512, sb, 512) == 0);                               /* la copia de reserva existe */
    /* a) superbloque a ceros (en BLANCO: antes el sistema habria formateado y perdido todo) */
    memset(r.d, 0, 512); CHECK(bigfs_mount(&d, &e) == 0);
    CHECK_EQ_INT(bigfs_repair_super(&d), 1); CHECK(memcmp(r.d, sb, 512) == 0); rs_verify(&d, 7);
    /* b) un bit cambiado (CRC malo) */
    r.d[40] ^= 0x04; CHECK(bigfs_mount(&d, &e) == 0); CHECK_EQ_INT(e, BIGFS_E_CORRUPT);
    CHECK_EQ_INT(bigfs_repair_super(&d), 1); rs_verify(&d, 7);
    /* c) sin copia de reserva (disco anterior al parche): geometria automatica */
    memset(r.d, 0, 512); memset(r.d + 129 * 512, 0, 512);
    CHECK_EQ_INT(bigfs_repair_super(&d), 2); CHECK(memcmp(r.d, sb, 512) == 0); rs_verify(&d, 7);
    /* d) una copia de reserva con el CRC malo no se acepta */
    memset(r.d, 0, 512); r.d[129 * 512 + 30] ^= 1;
    CHECK_EQ_INT(bigfs_repair_super(&d), 2); memcpy(r.d + 129 * 512, sb, 512);
    /* d2) un superbloque valido de OTRA geometria en el relleno, en un sitio que no le corresponde (restos de un formateo anterior): se ignora */
    {   struct ram o; ram_init(&o, 8192); struct bigfs_dev od = mkdev(&o); CHECK_EQ_INT(bigfs_format(&od, 600), 0);
        memcpy(r.d + 131 * 512, o.d, 512); ram_free(&o);
        memset(r.d, 0, 512); memset(r.d + 129 * 512, 0, 512);
        CHECK_EQ_INT(bigfs_repair_super(&d), 2); CHECK(memcmp(r.d, sb, 512) == 0); rs_verify(&d, 7);   /* el senuelo no se acepta */
        memset(r.d + 131 * 512, 0, 512); }
    /* e) la raiz tambien destruida: no es un SOMABIG1 reconocible, no se inventa nada */
    memset(r.d, 0, 512); memset(r.d + 129 * 512, 0, 512); memset(r.d + 512, 0, 256);
    CHECK_EQ_INT(bigfs_repair_super(&d), BIGFS_E_NOTFMT);
    { uint8_t z[512] = {0}; CHECK(memcmp(r.d, z, 512) == 0); }
    ram_free(&r);
    /* f) geometria no automatica (300 inodos): con copia se recupera */
    ram_init(&r, 8192); d = mkdev(&r);
    CHECK_EQ_INT(bigfs_format(&d, 300), 0); rs_populate(&d, 9);
    memcpy(sb, r.d, 512); memset(r.d, 0, 512);
    CHECK_EQ_INT(bigfs_repair_super(&d), 1); CHECK(memcmp(r.d, sb, 512) == 0); rs_verify(&d, 9);
    ram_free(&r);
    /* g) error de E/S al escribir el superbloque: se propaga y no queda nada a medias */
    ram_init(&r, 8192); d = mkdev(&r); CHECK_EQ_INT(bigfs_format(&d, 0), 0); memset(r.d, 0, 512);
    r.fail_at = r.writes; CHECK_EQ_INT(bigfs_repair_super(&d), BIGFS_E_IO);
    { uint8_t z[512] = {0}; CHECK(memcmp(r.d, z, 512) == 0); }
    CHECK_EQ_INT(bigfs_repair_super(&d), 1);                                    /* y reintentar funciona */
    ram_free(&r);
}

/* ---------------------------------------------------------------- PARCHE 131: cache de lectura */
static void test_cache(void)
{
    struct ram r; ram_init(&r, 16384); struct bigfs_dev d = mkdev(&r);
    CHECK_EQ_INT(bigfs_format(&d, 0), 0);
    struct bigfs *fs = mount_ok(&d); uint32_t f, g;
    enum { SZ = 20000 };
    uint8_t *w = malloc(SZ), *b = malloc(SZ); fillpat(w, SZ, 5);
    CHECK_EQ_INT(bigfs_create(fs, "/c.bin", &f), 0); CHECK_EQ_INT((int)bigfs_write(fs, f, 0, w, SZ), SZ);
    bigfs_unmount(fs); fs = mount_ok(&d); CHECK_EQ_INT(bigfs_lookup(fs, "/c.bin", &f), BIGFS_T_FILE);   /* montaje nuevo = cache fria */
    long r0 = g_dev_reads;
    CHECK_EQ_INT((int)bigfs_read(fs, f, 0, b, SZ), SZ); CHECK(memcmp(b, w, SZ) == 0);
    long frias = g_dev_reads - r0; CHECK(frias > 0);
    /* una lectura de 16 KiB (32 sectores) se cachea; las de 64 KiB no */
    r0 = g_dev_reads; memset(b, 0, SZ);
    CHECK_EQ_INT((int)bigfs_read(fs, f, 0, b, 16384), 16384); CHECK(memcmp(b, w, 16384) == 0);
    long tras1 = g_dev_reads - r0;
    r0 = g_dev_reads; memset(b, 0, SZ);
    CHECK_EQ_INT((int)bigfs_read(fs, f, 0, b, 16384), 16384); CHECK(memcmp(b, w, 16384) == 0);
    CHECK_EQ_INT((int)(g_dev_reads - r0), 0);                                  /* repetida: cero lecturas del disco */
    CHECK(tras1 >= 0);
    /* lecturas pequenas sueltas dentro de lo ya cacheado */
    r0 = g_dev_reads; for (int i = 0; i < 20; ++i) { CHECK_EQ_INT((int)bigfs_read(fs, f, (uint64_t)i * 700, b, 100), 100); CHECK(memcmp(b, w + i * 700, 100) == 0); }
    CHECK_EQ_INT((int)(g_dev_reads - r0), 0);
    /* escribir invalida: se lee lo nuevo */
    uint8_t nuevo[300]; fillpat(nuevo, 300, 77); memcpy(w + 5000, nuevo, 300);
    CHECK_EQ_INT((int)bigfs_write(fs, f, 5000, nuevo, 300), 300);
    CHECK_EQ_INT((int)bigfs_read(fs, f, 0, b, 16384), 16384); CHECK(memcmp(b, w, 16384) == 0);
    /* reutilizacion de clusteres: borrar y crear otro archivo en el mismo sitio */
    CHECK_EQ_INT(bigfs_unlink(fs, "/c.bin"), 0);
    uint8_t *w2 = malloc(SZ); fillpat(w2, SZ, 99);
    CHECK_EQ_INT(bigfs_create(fs, "/d.bin", &g), 0); CHECK_EQ_INT((int)bigfs_write(fs, g, 0, w2, SZ), SZ);
    CHECK_EQ_INT((int)bigfs_read(fs, g, 0, b, 16384), 16384); CHECK(memcmp(b, w2, 16384) == 0);
    CHECK_EQ_INT((int)bigfs_read(fs, g, 0, b, SZ), SZ); CHECK(memcmp(b, w2, SZ) == 0);
    /* truncar y volver a crecer: lo nuevo son ceros, no restos de la cache */
    CHECK_EQ_INT(bigfs_truncate(fs, g, 100), 0); CHECK_EQ_INT(bigfs_truncate(fs, g, 9000), 0);
    CHECK_EQ_INT((int)bigfs_read(fs, g, 0, b, 9000), 9000); CHECK(memcmp(b, w2, 100) == 0);
    int ceros = 1; for (int i = 100; i < 9000; ++i) if (b[i]) { ceros = 0; break; } CHECK(ceros);
    struct bigfs_info inf; bigfs_info(fs, &inf); CHECK(inf.cache_hits > 0 && inf.cache_misses > 0);
    free(w); free(w2); free(b); bigfs_unmount(fs); ram_free(&r);
}

/* ---------------------------------------------------------------- fuzzing contra un modelo */
#define MMAX 96
struct mnode { char path[96]; int type; uint8_t *data; size_t size; int used; };
static struct mnode M[MMAX];
static int m_find(const char *p) { for (int i = 0; i < MMAX; ++i) if (M[i].used && strcmp(M[i].path, p) == 0) return i; return -1; }
static int m_has_children(const char *p) { size_t l = strlen(p); for (int i = 0; i < MMAX; ++i) if (M[i].used && strncmp(M[i].path, p, l) == 0 && M[i].path[l] == '/') return 1; return 0; }
static void parent_of(const char *p, char *out) { strcpy(out, p); char *s = strrchr(out, '/'); if (s == out) out[1] = 0; else *s = 0; }
static int m_add(const char *p, int type) { for (int i = 0; i < MMAX; ++i) if (!M[i].used) { memset(&M[i], 0, sizeof M[i]); strcpy(M[i].path, p); M[i].type = type; M[i].used = 1; return i; } return -1; }
static void m_del(int i) { free(M[i].data); memset(&M[i], 0, sizeof M[i]); }
static void m_resize(int i, size_t n) { M[i].data = realloc(M[i].data, n ? n : 1); if (n > M[i].size) memset(M[i].data + M[i].size, 0, n - M[i].size); M[i].size = n; }

static const char *DIRS[] = { "/", "/d1", "/d1/s1", "/d2", "/d2/s2", "/d1/s1/t1" };
static const char *FILES[] = { "f1", "f2", "f3", "f4" };
static void pick_path(char *out, int want_dir)
{
    const char *d = DIRS[rnd() % 6];
    if (want_dir) { strcpy(out, DIRS[1 + rnd() % 5]); return; }
    snprintf(out, 96, "%s%s%s", d, strcmp(d, "/") ? "/" : "", FILES[rnd() % 4]);
}

static int cmp_tree(struct bigfs *fs)
{
    for (int i = 0; i < MMAX; ++i) {
        if (!M[i].used) continue;
        uint32_t id; int t = bigfs_lookup(fs, M[i].path, &id);
        if (t != M[i].type) { printf("  modelo: %s tipo %d != %d\n", M[i].path, t, M[i].type); return 0; }
        if (t == BIGFS_T_FILE) {
            struct bigfs_stat st; bigfs_stat(fs, id, &st);
            if (st.size != M[i].size) { printf("  modelo: %s tamano %llu != %zu\n", M[i].path, (unsigned long long)st.size, M[i].size); return 0; }
            uint8_t *b = malloc(M[i].size ? M[i].size : 1);
            int64_t rr = bigfs_read(fs, id, 0, b, M[i].size);
            int same = rr == (int64_t)M[i].size && (M[i].size == 0 || memcmp(b, M[i].data, M[i].size) == 0); free(b);
            if (!same) { printf("  modelo: %s contenido distinto\n", M[i].path); return 0; }
        }
    }
    /* y no hay nada de mas */
    int count = 0; for (int i = 0; i < MMAX; ++i) if (M[i].used) count++;
    int n = 0; struct bigfs_info inf; bigfs_info(fs, &inf); n = (int)inf.used_inodes - 1;
    if (n != count) { printf("  modelo: %d inodos usados, %d en el modelo\n", n, count); return 0; }
    return 1;
}

static void test_fuzz_modelo(void)
{
    memset(M, 0, sizeof M);
    struct ram r; ram_init(&r, 8192); struct bigfs_dev d = mkdev(&r); bigfs_format(&d, 128);
    struct bigfs *fs = mount_ok(&d);
    uint8_t *wb = malloc(60000); int steps = 6000, mismatch = 0;
    for (int s = 0; s < steps && !mismatch; ++s) {
        int op = (int)(rnd() % 100); char p[96], q[96], par[96]; uint32_t id; int rc;
        if (op < 12) {                                              /* mkdir */
            pick_path(p, 1); int exists = m_find(p) >= 0; parent_of(p, par);
            int par_ok = strcmp(par, "/") == 0 || (m_find(par) >= 0 && M[m_find(par)].type == BIGFS_T_DIR);
            rc = bigfs_mkdir(fs, p);
            if (exists) CHECK_EQ_INT(rc, BIGFS_E_EXIST); else if (!par_ok) CHECK(rc == BIGFS_E_NOENT || rc == BIGFS_E_NOTDIR);
            else { CHECK_EQ_INT(rc, 0); if (rc == 0) m_add(p, BIGFS_T_DIR); }
        } else if (op < 24) {                                       /* create */
            pick_path(p, 0); int exists = m_find(p) >= 0; parent_of(p, par);
            int par_ok = strcmp(par, "/") == 0 || (m_find(par) >= 0 && M[m_find(par)].type == BIGFS_T_DIR);
            rc = bigfs_create(fs, p, &id);
            if (exists) CHECK_EQ_INT(rc, BIGFS_E_EXIST); else if (!par_ok) CHECK(rc == BIGFS_E_NOENT);
            else { CHECK_EQ_INT(rc, 0); if (rc == 0) m_add(p, BIGFS_T_FILE); }
        } else if (op < 52) {                                       /* write */
            pick_path(p, 0); int mi = m_find(p); if (mi < 0 || M[mi].type != BIGFS_T_FILE) continue;
            size_t off = (size_t)(rnd() % (M[mi].size + 3000)), n = (size_t)(rnd() % 30000);
            if (rnd() % 8 == 0) off = M[mi].size;                    /* append */
            for (size_t i = 0; i < n; ++i) wb[i] = (uint8_t)rnd();
            bigfs_lookup(fs, p, &id); int64_t w = bigfs_write(fs, id, off, wb, n);
            if (w == BIGFS_E_NOSPC || w == BIGFS_E_FBIG) continue;
            CHECK_EQ_INT(w, (int64_t)n);
            if (off + n > M[mi].size) m_resize(mi, off + n);
            if (n) memcpy(M[mi].data + off, wb, n);
        } else if (op < 62) {                                       /* read aleatorio */
            pick_path(p, 0); int mi = m_find(p); if (mi < 0 || M[mi].type != BIGFS_T_FILE) continue;
            bigfs_lookup(fs, p, &id); size_t off = (size_t)(rnd() % (M[mi].size + 100)), n = (size_t)(rnd() % 20000);
            uint8_t *b = malloc(n ? n : 1); int64_t g = bigfs_read(fs, id, off, b, n);
            size_t want = off >= M[mi].size ? 0 : (n < M[mi].size - off ? n : M[mi].size - off);
            CHECK_EQ_INT(g, (int64_t)want); if (want) CHECK(memcmp(b, M[mi].data + off, want) == 0); free(b);
        } else if (op < 70) {                                       /* truncate */
            pick_path(p, 0); int mi = m_find(p); if (mi < 0 || M[mi].type != BIGFS_T_FILE) continue;
            size_t ns = (size_t)(rnd() % 50000); bigfs_lookup(fs, p, &id); rc = bigfs_truncate(fs, id, ns);
            if (rc == BIGFS_E_NOSPC || rc == BIGFS_E_FBIG) continue;
            CHECK_EQ_INT(rc, 0); m_resize(mi, ns);
        } else if (op < 80) {                                       /* unlink */
            pick_path(p, 0); int mi = m_find(p); rc = bigfs_unlink(fs, p);
            if (mi < 0) CHECK_EQ_INT(rc, BIGFS_E_NOENT); else { CHECK_EQ_INT(rc, 0); m_del(mi); }
        } else if (op < 86) {                                       /* rmdir */
            pick_path(p, 1); int mi = m_find(p); rc = bigfs_rmdir(fs, p);
            if (mi < 0) CHECK_EQ_INT(rc, BIGFS_E_NOENT);
            else if (m_has_children(p)) CHECK_EQ_INT(rc, BIGFS_E_NOTEMPTY);
            else { CHECK_EQ_INT(rc, 0); m_del(mi); }
        } else if (op < 96) {                                       /* rename */
            if (rnd() % 2) { pick_path(p, 0); pick_path(q, 0); } else { pick_path(p, 1); pick_path(q, 1); }
            int src = m_find(p); int dst = m_find(q); parent_of(q, par);
            int par_ok = strcmp(par, "/") == 0 || (m_find(par) >= 0 && M[m_find(par)].type == BIGFS_T_DIR);
            size_t pl = strlen(p); int inside = strncmp(q, p, pl) == 0 && (q[pl] == '/' || q[pl] == 0);
            rc = bigfs_rename(fs, p, q);
            if (src < 0) CHECK_EQ_INT(rc, BIGFS_E_NOENT);
            else if (strcmp(p, q) == 0) CHECK_EQ_INT(rc, BIGFS_E_EXIST);
            else if (!par_ok) CHECK(rc == BIGFS_E_NOENT || rc == BIGFS_E_NOTDIR);
            else if (dst >= 0) CHECK_EQ_INT(rc, BIGFS_E_EXIST);
            else if (inside) CHECK_EQ_INT(rc, BIGFS_E_INVAL);
            else {
                CHECK_EQ_INT(rc, 0);
                if (rc == 0) {                                      /* reescribe el prefijo en el modelo */
                    for (int i = 0; i < MMAX; ++i) if (M[i].used && strncmp(M[i].path, p, pl) == 0 && (M[i].path[pl] == '/' || M[i].path[pl] == 0)) {
                        char np[96]; snprintf(np, sizeof np, "%s%s", q, M[i].path + pl); strcpy(M[i].path, np); }
                }
            }
        } else {                                                    /* remontar */
            bigfs_unmount(fs); fs = mount_ok(&d);
        }
        if (s % 250 == 0 && !cmp_tree(fs)) { mismatch = 1; CHECK(0); }
        if (s % 1000 == 999) { struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0); }
    }
    CHECK(cmp_tree(fs));
    bigfs_unmount(fs); fs = mount_ok(&d); CHECK(cmp_tree(fs));
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    for (int i = 0; i < MMAX; ++i) if (M[i].used) m_del(i);
    free(wb); bigfs_unmount(fs); ram_free(&r);
}

/* ---------------------------------------------------------------- imagen escrita por el host (tools/bigfs.py) */
static void test_verify(const char *img, const char *manifest)
{
    FILE *f = fopen(img, "rb"); if (!f) { printf("  no se puede abrir %s\n", img); CHECK(0); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    struct ram r; ram_init(&r, (uint64_t)n / 512); CHECK(fread(r.d, 1, (size_t)n, f) == (size_t)n); fclose(f);
    struct bigfs_dev d = mkdev(&r);
    struct bigfs_fsck_report rep; CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0);
    struct bigfs *fs = mount_ok(&d);
    FILE *m = fopen(manifest, "r"); CHECK(m != 0);
    char line[512]; int files = 0, dirs = 0;
    while (m && fgets(line, sizeof line, m)) {
        char kind[8], path[300]; unsigned long long size, h;
        if (sscanf(line, "%7s %299s %llu %llx", kind, path, &size, &h) < 3) continue;
        uint32_t id; int t = bigfs_lookup(fs, path, &id);
        if (kind[0] == 'D') { CHECK_EQ_INT(t, BIGFS_T_DIR); dirs++; continue; }
        CHECK_EQ_INT(t, BIGFS_T_FILE); if (t != BIGFS_T_FILE) continue;
        struct bigfs_stat st; bigfs_stat(fs, id, &st); CHECK_EQ_INT(st.size, size);
        uint8_t *b = malloc(size ? size : 1); int64_t rr = bigfs_read(fs, id, 0, b, size);
        CHECK_EQ_INT(rr, (int64_t)size);
        CHECK(fnv(b, size, 1469598103934665603ull) == h); free(b); files++;
    }
    if (m) fclose(m);
    printf("  imagen del host: %d archivos y %d directorios verificados byte a byte\n", files, dirs);
    CHECK(files > 0);
    bigfs_unmount(fs); ram_free(&r);
}

/* Imagen escrita por ESTE codigo, para que tools/bigfs.py la verifique (prueba cruzada en sentido inverso). */
static void emit_image(const char *img, const char *manifest)
{
    struct ram r; ram_init(&r, 16384); struct bigfs_dev d = mkdev(&r); bigfs_format(&d, 0);
    struct bigfs *fs = mount_ok(&d); FILE *m = fopen(manifest, "w");
    bigfs_mkdir(fs, "/musica"); bigfs_mkdir(fs, "/musica/rock"); bigfs_mkdir(fs, "/prog");
    fprintf(m, "D /musica 0 0\nD /musica/rock 0 0\nD /prog 0 0\n");
    const struct { const char *p; size_t n; } F[] = { {"/leeme.txt", 11}, {"/musica/a.wav", 700000}, {"/musica/rock/b.wav", 1500001}, {"/prog/hola.elf", 8920}, {"/vacio", 0}, {"/prog/grande.bin", 3000000} };
    for (size_t i = 0; i < sizeof F / sizeof F[0]; ++i) {
        uint8_t *b = malloc(F[i].n ? F[i].n : 1); fillpat(b, F[i].n, i + 1); uint32_t id;
        CHECK_EQ_INT(bigfs_create(fs, F[i].p, &id), 0);
        size_t off = 0; while (off < F[i].n) { size_t n = 1 + rnd() % 100000; if (off + n > F[i].n) n = F[i].n - off; CHECK_EQ_INT(bigfs_write(fs, id, off, b + off, n), (int64_t)n); off += n; }
        fprintf(m, "F %s %zu %llx\n", F[i].p, F[i].n, (unsigned long long)fnv(b, F[i].n, 1469598103934665603ull)); free(b);
    }
    bigfs_unmount(fs); fclose(m);
    FILE *f = fopen(img, "wb"); fwrite(r.d, 512, r.sectors, f); fclose(f); ram_free(&r);
}

/* fsck del codigo C sobre una imagen cualquiera (la usa la prueba cruzada tras editarla con Python) */
static void test_fsck_image(const char *img)
{
    FILE *f = fopen(img, "rb"); if (!f) { CHECK(0); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    struct ram r; ram_init(&r, (uint64_t)n / 512); CHECK(fread(r.d, 1, (size_t)n, f) == (size_t)n); fclose(f);
    struct bigfs_dev d = mkdev(&r); struct bigfs_fsck_report rep;
    CHECK_EQ_INT(bigfs_fsck(&d, &rep), 0); CHECK_EQ_INT(rep.problems, 0);
    struct bigfs *fs = mount_ok(&d); bigfs_unmount(fs); ram_free(&r);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--fsck") == 0) { test_fsck_image(argv[2]); TEST_MAIN_END("bigfs --fsck"); }
    if (argc == 4 && strcmp(argv[1], "--verify") == 0) { test_verify(argv[2], argv[3]); TEST_MAIN_END("bigfs (imagen del host)"); }
    if (argc == 4 && strcmp(argv[1], "--emit") == 0) { emit_image(argv[2], argv[3]); TEST_MAIN_END("bigfs --emit"); }
    test_basico();
    test_grande();
    test_enospc_y_fragmentacion();
    test_errores_de_es();
    test_corte_de_energia();
    test_corrupcion();
    test_repair_super();
    test_cache();
    test_fuzz_modelo();
    TEST_MAIN_END("bigfs");
}
