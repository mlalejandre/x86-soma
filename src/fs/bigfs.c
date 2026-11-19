/* SOMA_PARCHE_018_BIGFS: ver bigfs.h */
#include "bigfs.h"
#include "blkcache.h"

#define MAGIC "SOMABIG1"
#define VERSION 1u
#define COPY(d, s, n) do { for (size_t _i = 0; _i < (n); ++_i) ((uint8_t *)(d))[_i] = ((const uint8_t *)(s))[_i]; } while (0)
#define ZERO(d, n)    do { for (size_t _i = 0; _i < (n); ++_i) ((uint8_t *)(d))[_i] = 0; } while (0)

struct extent { uint32_t start, count; };

struct inode {                      /* 256 bytes, un sector contiene 2 */
    uint32_t state;
    uint32_t crc;                   /* CRC32 de los bytes [8, 256) */
    uint32_t parent;
    uint32_t nextents;
    uint64_t size;
    uint64_t mtime;
    char     name[64];
    struct extent ext[BIGFS_MAX_EXTENTS];
};
_Static_assert(sizeof(struct inode) == BIGFS_INODE_SIZE, "inodo de 256 bytes");

struct super {
    char     magic[8];
    uint32_t version, cluster_sectors;
    uint64_t total_sectors;
    uint32_t inode_count, inode_start, data_start, data_clusters;
    uint32_t crc;                   /* CRC32 de todo lo anterior */
};

struct bigfs {
    struct bigfs_dev dev;
    struct super sb;
    struct inode *ino;              /* tabla en RAM (la verdad; se escribe sector a sector) */
    uint8_t *bitmap;                /* 1 bit por cluster */
    uint8_t *scratch;               /* 64 KiB */
    uint64_t free_clusters;
    uint32_t used_inodes, repaired;
    uint32_t hint;                  /* siguiente cluster a probar */
    struct blkcache bc;             /* PARCHE 131: cache de lectura (bc.nlines == 0: desactivada) */
    void *bc_mem;                   /* un solo bloque: etiquetas + datos + zona de lectura contigua */
};
#define SCRATCH 65536u

/* ---------------------------------------------------------------- utilidades */
static uint32_t crc_table[256];
static int crc_ready;

static uint32_t crc32(const void *data, size_t n)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
        crc_ready = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; ++i) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t inode_crc(const struct inode *in) { return crc32((const uint8_t *)in + 8, BIGFS_INODE_SIZE - 8); }
static uint32_t super_crc(const struct super *sb) { return crc32(sb, __builtin_offsetof(struct super, crc)); }   /* hasta el campo crc */

static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static size_t str_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static inline void lock(struct bigfs *fs) { if (fs->dev.lock) fs->dev.lock(fs->dev.ctx); }
static inline void unlock(struct bigfs *fs) { if (fs->dev.unlock) fs->dev.unlock(fs->dev.ctx); }
static uint64_t now(struct bigfs *fs) { return fs->dev.now ? fs->dev.now(fs->dev.ctx) : 0; }

const char *bigfs_strerror(int c)
{
    switch (c) {
    case BIGFS_OK: return "ok";
    case BIGFS_E_NOENT: return "no existe";
    case BIGFS_E_EXIST: return "ya existe";
    case BIGFS_E_NOTDIR: return "no es un directorio";
    case BIGFS_E_ISDIR: return "es un directorio";
    case BIGFS_E_NOTEMPTY: return "el directorio no esta vacio";
    case BIGFS_E_NAME: return "nombre invalido";
    case BIGFS_E_NOSPC: return "sin espacio";
    case BIGFS_E_IO: return "error de E/S del disco";
    case BIGFS_E_CORRUPT: return "sistema de archivos danado";
    case BIGFS_E_INVAL: return "argumento invalido";
    case BIGFS_E_NOINODE: return "sin inodos libres";
    case BIGFS_E_FBIG: return "archivo demasiado fragmentado";
    case BIGFS_E_NOTMNT: return "no montado";
    case BIGFS_E_NOTFMT: return "la region no tiene un SOMABIG1 (usa 'big format')";
    default: return "error desconocido";
    }
}

/* bitmap */
static inline int bit_get(const uint8_t *bm, uint64_t i) { return (bm[i >> 3] >> (i & 7)) & 1; }
static inline void bit_set(uint8_t *bm, uint64_t i) { bm[i >> 3] |= (uint8_t)(1u << (i & 7)); }
static inline void bit_clr(uint8_t *bm, uint64_t i) { bm[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

/* ---------------------------------------------------------------- E/S de dispositivo */
static int dev_read(struct bigfs *fs, uint64_t lba, void *buf, uint32_t n)
{
    if (n == 0) return 0;
    if (lba + n > fs->dev.total_sectors) return BIGFS_E_IO;
    struct blkcache *c = &fs->bc;
    if (c->nlines == 0 || n > BLKC_MAX_SECT) return fs->dev.read(fs->dev.ctx, lba, buf, n) == 0 ? 0 : BIGFS_E_IO;
    /* PARCHE 131: lectura con cache. Se sirve de las lineas si estan todas; si no, se lee de una vez el tramo alineado y se rellenan. */
    uint64_t l0 = lba / BLKC_LINE_SECT, l1 = (lba + n - 1) / BLKC_LINE_SECT; uint32_t nl = (uint32_t)(l1 - l0 + 1);
    int all = 1;
    for (uint32_t i = 0; i < nl && all; ++i) if (!blkc_line(c, l0 + i)) all = 0;
    if (!all) {
        if ((l1 + 1) * BLKC_LINE_SECT > fs->dev.total_sectors) return fs->dev.read(fs->dev.ctx, lba, buf, n) == 0 ? 0 : BIGFS_E_IO;   /* ultimo tramo parcial: no se cachea */
        if (fs->dev.read(fs->dev.ctx, l0 * BLKC_LINE_SECT, c->span, nl * BLKC_LINE_SECT) != 0) return BIGFS_E_IO;
        for (uint32_t i = 0; i < nl; ++i) blkc_put(c, l0 + i, c->span + (size_t)i * BLKC_LINE_BYTES);
        c->misses++;
        COPY(buf, c->span + (size_t)(lba - l0 * BLKC_LINE_SECT) * BIGFS_SECTOR, (size_t)n * BIGFS_SECTOR);
        return 0;
    }
    c->hits++;
    for (uint32_t k = 0; k < n; ++k) {
        uint64_t sec = lba + k;
        COPY((uint8_t *)buf + (size_t)k * BIGFS_SECTOR, blkc_line(c, sec / BLKC_LINE_SECT) + (size_t)(sec % BLKC_LINE_SECT) * BIGFS_SECTOR, BIGFS_SECTOR);
    }
    return 0;
}
static int dev_write(struct bigfs *fs, uint64_t lba, const void *buf, uint32_t n)
{
    if (n == 0) return 0;
    if (lba + n > fs->dev.total_sectors) return BIGFS_E_IO;
    if (fs->bc.nlines) blkc_invalidate(&fs->bc, lba, n);      /* PARCHE 131: ANTES de escribir (un fallo a medias tampoco deja datos viejos) */
    int r = fs->dev.write(fs->dev.ctx, lba, buf, n) == 0 ? 0 : BIGFS_E_IO;
    if (fs->bc.nlines) blkc_invalidate(&fs->bc, lba, n);      /* y despues: una lectura intercalada no puede haber cacheado el estado intermedio */
    return r;
}

/* ---------------------------------------------------------------- formato y montaje */
static void layout(uint64_t total_sectors, uint32_t inode_count, struct super *sb, int *ok)
{
    ZERO(sb, sizeof(*sb));
    COPY(sb->magic, MAGIC, 8);
    sb->version = VERSION; sb->cluster_sectors = BIGFS_CLUSTER_SECT;
    sb->total_sectors = total_sectors;
    sb->inode_count = inode_count;
    sb->inode_start = 1;
    uint32_t isect = (inode_count + 1) / 2;
    uint32_t ds = 1 + isect;
    ds = (ds + BIGFS_CLUSTER_SECT - 1) / BIGFS_CLUSTER_SECT * BIGFS_CLUSTER_SECT;   /* alineado a cluster */
    sb->data_start = ds;
    *ok = total_sectors > ds + BIGFS_CLUSTER_SECT * 2;
    if (*ok) {
        uint64_t clusters = (total_sectors - ds) / BIGFS_CLUSTER_SECT;
        if (clusters > 0xFFFFFFF0ull) clusters = 0xFFFFFFF0ull;
        sb->data_clusters = (uint32_t)clusters;
    }
    sb->crc = super_crc(sb);
}

int bigfs_format(const struct bigfs_dev *dev, uint32_t inode_count)
{
    if (!dev || !dev->read || !dev->write || !dev->alloc || !dev->free) return BIGFS_E_INVAL;
    if (inode_count == 0) {
        uint64_t c = dev->total_sectors / BIGFS_CLUSTER_SECT / 8;     /* ~1 inodo por 32 KiB */
        inode_count = c < 256 ? 256 : c > 4096 ? 4096 : (uint32_t)c;
    }
    if (inode_count < 2 || inode_count > BIGFS_MAX_INODES) return BIGFS_E_INVAL;
    inode_count = (inode_count + 1) & ~1u;
    struct super sb; int ok;
    layout(dev->total_sectors, inode_count, &sb, &ok);
    if (!ok) return BIGFS_E_NOSPC;

    struct bigfs tmp; ZERO(&tmp, sizeof(tmp)); tmp.dev = *dev;
    uint8_t *z = (uint8_t *)dev->alloc(SCRATCH);
    if (!z) return BIGFS_E_NOSPC;
    ZERO(z, SCRATCH);
    int r = 0;
    /* se invalida el superbloque primero: un formateo interrumpido no deja un fs a medias montable */
    uint8_t sec[BIGFS_SECTOR]; ZERO(sec, sizeof(sec));
    r = dev_write(&tmp, 0, sec, 1);
    uint32_t isect = sb.data_start - 1;
    for (uint32_t s = 0; r == 0 && s < isect; ) {
        uint32_t n = isect - s < SCRATCH / BIGFS_SECTOR ? isect - s : SCRATCH / BIGFS_SECTOR;
        if (s == 0) {                                  /* primer bloque: lleva el inodo raiz */
            struct inode root; ZERO(&root, sizeof(root));
            root.state = BIGFS_T_DIR; root.parent = 0;
            root.mtime = dev->now ? dev->now(dev->ctx) : 0;
            root.crc = inode_crc(&root);
            COPY(z, &root, sizeof(root));
        }
        r = dev_write(&tmp, 1 + s, z, n);
        if (s == 0) ZERO(z, sizeof(struct inode));
        s += n;
    }
    dev->free(z);
    if (r) return r;
    ZERO(sec, sizeof(sec)); COPY(sec, &sb, sizeof(sb));
    /* PARCHE 129: copia de reserva del superbloque en el primer sector de relleno tras la tabla de inodos (si el
     * alineado a cluster deja alguno). No cambia la geometria: los discos anteriores simplemente no la tienen. */
    if (sb.data_start > 1 + inode_count / 2) {
        r = dev_write(&tmp, 1 + inode_count / 2, sec, 1);
        if (r) return r;
    }
    return dev_write(&tmp, 0, sec, 1);                 /* el superbloque se escribe el ULTIMO */
}

/* Analiza una tabla de inodos cruda. verdict[i]: 0 libre, 1 ok, 2 descartado. Rellena rep (si no es NULL) y,
 * si bm no es NULL, marca en el los clusteres de los inodos aceptados. */
static void analyze(const struct super *sb, struct inode *tab, uint8_t *verdict, uint8_t *bm,
                    struct bigfs_fsck_report *rep, uint32_t *repaired, uint64_t *used_clusters)
{
    uint32_t n = sb->inode_count;
    uint64_t used = 0;
    struct bigfs_fsck_report r; ZERO(&r, sizeof(r));
    for (uint32_t i = 0; i < n; ++i) verdict[i] = 0;

    /* 1) CRC, tipo, nombre, extents propios */
    for (uint32_t i = 0; i < n; ++i) {
        struct inode *in = &tab[i];
        if (in->state == BIGFS_T_FREE) {
            continue;
        }
        verdict[i] = 1;
        if ((in->state != BIGFS_T_FILE && in->state != BIGFS_T_DIR) || in->crc != inode_crc(in)) { r.bad_crc++; verdict[i] = 2; continue; }
        int nm = 0; for (; nm < 64 && in->name[nm]; ++nm) { if (in->name[nm] == '/' || (uint8_t)in->name[nm] < 32) { nm = -1; break; } }
        if (i == 0) { if (in->state != BIGFS_T_DIR || in->name[0] != 0 || in->parent != 0) { r.bad_dir++; verdict[i] = 2; } continue; }
        if (nm < 1 || nm >= 64) { r.bad_name++; verdict[i] = 2; continue; }
        if (in->nextents > BIGFS_MAX_EXTENTS) { r.bad_extent++; verdict[i] = 2; continue; }
        if (in->state == BIGFS_T_DIR && (in->nextents != 0 || in->size != 0)) { r.bad_dir++; verdict[i] = 2; continue; }
        uint64_t cl = 0;
        for (uint32_t k = 0; k < in->nextents; ++k) {
            uint64_t s = in->ext[k].start, c = in->ext[k].count;
            if (c == 0 || s + c > sb->data_clusters) { r.bad_extent++; verdict[i] = 2; break; }
            cl += c;
        }
        if (verdict[i] == 2) continue;
        if (in->state == BIGFS_T_FILE && in->size > cl * BIGFS_CLUSTER) { r.bad_extent++; verdict[i] = 2; continue; }
    }
    if (tab[0].state != BIGFS_T_DIR) { r.bad_dir++; verdict[0] = 2; }

    /* 2) padres: deben ser directorios validos y llevar a la raiz sin ciclos */
    for (uint32_t i = 1; i < n; ++i) {
        if (verdict[i] != 1) continue;
        uint32_t cur = i, steps = 0; int good = 1;
        while (cur != 0) {
            uint32_t p = tab[cur].parent;
            if (p >= n || verdict[p] != 1 || tab[p].state != BIGFS_T_DIR) { good = 0; break; }
            cur = p;
            if (++steps > n) { good = 0; break; }
        }
        if (!good) { r.orphan++; verdict[i] = 2; }
    }
    /* 3) nombres duplicados en un mismo directorio (el primero gana) */
    for (uint32_t i = 1; i < n; ++i) {
        if (verdict[i] != 1) continue;
        for (uint32_t j = i + 1; j < n; ++j) {
            if (verdict[j] != 1 || tab[j].parent != tab[i].parent) continue;
            if (str_eq(tab[j].name, tab[i].name)) { r.dup_name++; verdict[j] = 2; }
        }
    }
    /* 4) solapamiento de extents entre inodos (el primero gana) */
    if (bm) {
        for (uint32_t i = 0; i < n; ++i) {
            if (verdict[i] != 1) continue;
            int clash = 0;
            for (uint32_t k = 0; k < tab[i].nextents && !clash; ++k)
                for (uint64_t c = 0; c < tab[i].ext[k].count; ++c)
                    if (bit_get(bm, tab[i].ext[k].start + c)) { clash = 1; break; }
            if (clash) { r.overlap++; verdict[i] = 2; continue; }
            for (uint32_t k = 0; k < tab[i].nextents; ++k)
                for (uint64_t c = 0; c < tab[i].ext[k].count; ++c) { bit_set(bm, tab[i].ext[k].start + c); used++; }
        }
    }
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; ++i) if (verdict[i] == 2) bad++;
    r.problems = r.bad_crc + r.bad_extent + r.overlap + r.orphan + r.bad_dir + r.bad_name + r.dup_name;
    if (rep) *rep = r;
    if (repaired) *repaired = bad;
    if (used_clusters) *used_clusters = used;
}

/* PARCHE 129: un superbloque es valido si su CRC cuadra y su geometria coincide con la que calcula `layout` */
static int super_valid(const struct super *sb, uint64_t dev_sectors)
{
    for (int i = 0; i < 8; ++i) if (sb->magic[i] != MAGIC[i]) return 0;
    if (sb->crc != super_crc(sb) || sb->version != VERSION || sb->cluster_sectors != BIGFS_CLUSTER_SECT) return 0;
    if (sb->total_sectors > dev_sectors) return 0;
    if (sb->inode_count < 2 || sb->inode_count > BIGFS_MAX_INODES || (sb->inode_count & 1)) return 0;
    struct super chk; int ok;
    layout(sb->total_sectors, sb->inode_count, &chk, &ok);
    return ok && chk.data_start == sb->data_start && chk.data_clusters == sb->data_clusters && sb->inode_start == 1;
}

static int read_super(const struct bigfs_dev *dev, struct super *sb)
{
    uint8_t sec[BIGFS_SECTOR];
    if (dev->total_sectors < 16) return BIGFS_E_NOTFMT;
    if (dev->read(dev->ctx, 0, sec, 1) != 0) return BIGFS_E_IO;
    COPY(sb, sec, sizeof(*sb));
    for (int i = 0; i < 8; ++i) if (sb->magic[i] != MAGIC[i]) return BIGFS_E_NOTFMT;
    return super_valid(sb, dev->total_sectors) ? 0 : BIGFS_E_CORRUPT;
}

/* PARCHE 129: reconstruye el superbloque SIN tocar inodos ni datos. 0 = ya estaba bien; 1 = restaurado desde la copia
 * de reserva; 2 = reconstruido por la geometria automatica (discos sin copia, formateados con inode_count = 0: lo que usa
 * el sistema); <0 = no hay nada que reconstruir (no hay un SOMABIG1 reconocible: el llamador decide si formatea). */
int bigfs_repair_super(const struct bigfs_dev *dev)
{
    if (!dev || !dev->read || !dev->write || !dev->alloc || !dev->free) return BIGFS_E_INVAL;
    struct super sb;
    int r = read_super(dev, &sb);
    if (r == 0) return 0;
    if (r == BIGFS_E_IO) return r;
    struct bigfs tmp; ZERO(&tmp, sizeof(tmp)); tmp.dev = *dev;
    uint8_t *buf = (uint8_t *)dev->alloc(SCRATCH);
    if (!buf) return BIGFS_E_NOSPC;
    int how = 0;
    uint64_t last = dev->total_sectors < 1 + BIGFS_MAX_INODES / 2 + 1 ? dev->total_sectors : 1 + BIGFS_MAX_INODES / 2 + 1;
    /* 1) buscar la copia: esta en el sector 1 + inode_count/2, asi que su posicion confirma la geometria */
    for (uint64_t s0 = 2; s0 < last && !how; ) {
        uint32_t n = (uint32_t)(last - s0 < SCRATCH / BIGFS_SECTOR ? last - s0 : SCRATCH / BIGFS_SECTOR);
        if (dev->read(dev->ctx, s0, buf, n) != 0) { dev->free(buf); return BIGFS_E_IO; }
        for (uint32_t i = 0; i < n && !how; ++i) {
            struct super c; COPY(&c, buf + (size_t)i * BIGFS_SECTOR, sizeof(c));
            if (super_valid(&c, dev->total_sectors) && s0 + i == 1u + c.inode_count / 2) { sb = c; how = 1; }
        }
        s0 += n;
    }
    /* 2) sin copia: geometria automatica; solo si el inodo raiz (sector 1) es un directorio con CRC valido */
    if (!how) {
        uint32_t cnt;
        { uint64_t c = dev->total_sectors / BIGFS_CLUSTER_SECT / 8; cnt = c < 256 ? 256 : c > 4096 ? 4096 : (uint32_t)c; }
        cnt = (cnt + 1) & ~1u;
        int ok; struct super c; layout(dev->total_sectors, cnt, &c, &ok);
        struct inode root;
        if (ok && dev->read(dev->ctx, 1, buf, 1) == 0) {
            COPY(&root, buf, sizeof(root));
            if (root.state == BIGFS_T_DIR && root.parent == 0 && root.name[0] == 0 && root.crc == inode_crc(&root)) { sb = c; how = 2; }
        }
    }
    dev->free(buf);
    if (!how) return BIGFS_E_NOTFMT;
    uint8_t sec[BIGFS_SECTOR]; ZERO(sec, sizeof(sec)); COPY(sec, &sb, sizeof(sb));
    r = dev_write(&tmp, 0, sec, 1);
    if (r) return r;
    if (dev->flush && dev->flush(dev->ctx) != 0) return BIGFS_E_IO;
    return how;
}

static int load_table(const struct bigfs_dev *dev, const struct super *sb, struct inode *tab, uint8_t *scratch)
{
    uint32_t isect = (sb->inode_count + 1) / 2;
    for (uint32_t s = 0; s < isect; ) {
        uint32_t n = isect - s < SCRATCH / BIGFS_SECTOR ? isect - s : SCRATCH / BIGFS_SECTOR;
        if (dev->read(dev->ctx, sb->inode_start + s, scratch, n) != 0) return BIGFS_E_IO;
        COPY((uint8_t *)tab + (size_t)s * BIGFS_SECTOR, scratch, (size_t)n * BIGFS_SECTOR);
        s += n;
    }
    return 0;
}

struct bigfs *bigfs_mount(const struct bigfs_dev *dev, int *err)
{
    int dummy; if (!err) err = &dummy;
    *err = BIGFS_E_INVAL;
    if (!dev || !dev->read || !dev->write || !dev->alloc || !dev->free) return 0;
    struct super sb;
    int r = read_super(dev, &sb);
    if (r) { *err = r; return 0; }

    struct bigfs *fs = (struct bigfs *)dev->alloc(sizeof(*fs));
    if (!fs) { *err = BIGFS_E_NOSPC; return 0; }
    ZERO(fs, sizeof(*fs));
    fs->dev = *dev; fs->sb = sb;
    size_t tsz = (size_t)sb.inode_count * BIGFS_INODE_SIZE;
    size_t bsz = ((size_t)sb.data_clusters + 7) / 8;
    fs->ino = (struct inode *)dev->alloc(tsz);
    fs->bitmap = (uint8_t *)dev->alloc(bsz ? bsz : 1);
    fs->scratch = (uint8_t *)dev->alloc(SCRATCH);
    uint8_t *verdict = (uint8_t *)dev->alloc(sb.inode_count);
    if (!fs->ino || !fs->bitmap || !fs->scratch || !verdict) {
        if (fs->ino) dev->free(fs->ino);
        if (fs->bitmap) dev->free(fs->bitmap);
        if (fs->scratch) dev->free(fs->scratch);
        if (verdict) dev->free(verdict);
        dev->free(fs); *err = BIGFS_E_NOSPC; return 0;
    }
    ZERO(fs->bitmap, bsz);
    {   /* PARCHE 131: cache de lectura (si no hay memoria, el sistema de archivos funciona igual sin ella) */
        enum { NL = 64 };
        size_t tag_b = NL * sizeof(uint64_t), tot = tag_b + (size_t)NL * BLKC_LINE_BYTES + (size_t)BLKC_SPAN_LINES * BLKC_LINE_BYTES;
        uint8_t *m = (uint8_t *)dev->alloc(tot);
        if (m) { fs->bc_mem = m; blkc_init(&fs->bc, NL, (uint64_t *)(void *)m, m + tag_b, m + tag_b + (size_t)NL * BLKC_LINE_BYTES); }
    }
    r = load_table(dev, &sb, fs->ino, fs->scratch);
    if (r == 0) {
        analyze(&sb, fs->ino, verdict, fs->bitmap, 0, &fs->repaired, 0);
        if (verdict[0] != 1) r = BIGFS_E_CORRUPT;
    }
    if (r) {
        dev->free(verdict); dev->free(fs->ino); dev->free(fs->bitmap); dev->free(fs->scratch); if (fs->bc_mem) dev->free(fs->bc_mem); dev->free(fs);
        *err = r; return 0;
    }
    /* los descartados se tratan como libres en RAM (en disco no se tocan hasta que se reutilicen) y sus
     * clusteres quedan libres: el bitmap solo contiene los de inodos aceptados */
    for (uint32_t i = 0; i < sb.inode_count; ++i) {
        if (verdict[i] == 2) ZERO(&fs->ino[i], sizeof(struct inode));
        if (verdict[i] == 1 && i != 0) fs->used_inodes++;
    }
    uint64_t used = 0;
    for (uint64_t c = 0; c < sb.data_clusters; ++c) if (bit_get(fs->bitmap, c)) used++;
    fs->free_clusters = sb.data_clusters - used;
    dev->free(verdict);
    *err = 0;
    return fs;
}

void bigfs_unmount(struct bigfs *fs)
{
    if (!fs) return;
    struct bigfs_dev d = fs->dev;
    d.free(fs->ino); d.free(fs->bitmap); d.free(fs->scratch); if (fs->bc_mem) d.free(fs->bc_mem); d.free(fs);
}

int bigfs_fsck(const struct bigfs_dev *dev, struct bigfs_fsck_report *rep)
{
    struct bigfs_fsck_report r; ZERO(&r, sizeof(r));
    if (rep) *rep = r;
    struct super sb;
    int e = read_super(dev, &sb);
    if (e) { r.bad_super = 1; r.problems = 1; if (rep) *rep = r; return e; }
    struct inode *tab = (struct inode *)dev->alloc((size_t)sb.inode_count * BIGFS_INODE_SIZE);
    uint8_t *verdict = (uint8_t *)dev->alloc(sb.inode_count);
    uint8_t *bm = (uint8_t *)dev->alloc(((size_t)sb.data_clusters + 7) / 8 + 1);
    uint8_t *scr = (uint8_t *)dev->alloc(SCRATCH);
    if (!tab || !verdict || !bm || !scr) {
        if (tab) { dev->free(tab); }
        if (verdict) { dev->free(verdict); }
        if (bm) { dev->free(bm); }
        if (scr) { dev->free(scr); }
        return BIGFS_E_NOSPC;
    }
    ZERO(bm, ((size_t)sb.data_clusters + 7) / 8 + 1);
    e = load_table(dev, &sb, tab, scr);
    if (e == 0) analyze(&sb, tab, verdict, bm, &r, 0, 0);
    dev->free(tab); dev->free(verdict); dev->free(bm); dev->free(scr);
    if (rep) *rep = r;
    return e ? e : (r.problems ? BIGFS_E_CORRUPT : 0);
}

void bigfs_info(struct bigfs *fs, struct bigfs_info *o)
{
    lock(fs);
    o->total_clusters = fs->sb.data_clusters; o->free_clusters = fs->free_clusters;
    o->total_inodes = fs->sb.inode_count; o->used_inodes = fs->used_inodes + 1;
    o->cluster_bytes = BIGFS_CLUSTER; o->repaired = fs->repaired;
    o->cache_hits = fs->bc.hits; o->cache_misses = fs->bc.misses;
    unlock(fs);
}

/* ---------------------------------------------------------------- inodos */
static int commit_inode(struct bigfs *fs, uint32_t id)
{
    struct inode *in = &fs->ino[id];
    in->mtime = now(fs);
    in->crc = inode_crc(in);
    uint32_t sec = id / 2;
    /* PARCHE 115: barrera: todo lo escrito antes (datos, inodos previos) debe ser durable ANTES de confirmar este inodo. */
    if (fs->dev.flush && fs->dev.flush(fs->dev.ctx) != 0) return BIGFS_E_IO;
    return dev_write(fs, fs->sb.inode_start + sec, &fs->ino[sec * 2], 1);   /* UN sector: atomico */
}

static int find_child(struct bigfs *fs, uint32_t dir, const char *name, size_t nlen)
{
    for (uint32_t i = 1; i < fs->sb.inode_count; ++i) {
        struct inode *in = &fs->ino[i];
        if (in->state == BIGFS_T_FREE || in->parent != dir) continue;
        size_t k = 0; while (k < nlen && in->name[k] == name[k]) k++;
        if (k == nlen && in->name[k] == 0) return (int)i;
    }
    return -1;
}

static int valid_name(const char *s, size_t n)
{
    if (n == 0 || n > BIGFS_NAME_MAX) return 0;
    for (size_t i = 0; i < n; ++i) if (s[i] == '/' || (uint8_t)s[i] < 32 || s[i] == 127) return 0;
    if (n == 1 && s[0] == '.') return 0;
    if (n == 2 && s[0] == '.' && s[1] == '.') return 0;
    return 1;
}

/* Resuelve `path` salvo el ultimo componente. Devuelve el inodo del directorio padre y el ultimo nombre.
 * Si path es la raiz, *last_len = 0 y devuelve 0. */
static int resolve_parent(struct bigfs *fs, const char *path, const char **last, size_t *last_len)
{
    if (!path) return BIGFS_E_INVAL;
    size_t pl = str_len(path);
    if (pl > BIGFS_PATH_MAX) return BIGFS_E_NAME;
    uint32_t cur = 0;
    const char *p = path;
    *last = p; *last_len = 0;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) { *last_len = 0; return (int)cur; }
        const char *s = p; while (*p && *p != '/') p++;
        size_t n = (size_t)(p - s);
        const char *q = p; while (*q == '/') q++;
        int is_last = (*q == 0);
        if (n == 1 && s[0] == '.') { if (is_last) { *last = s; *last_len = 0; return (int)cur; } continue; }
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            cur = fs->ino[cur].parent;
            if (is_last) { *last = s; *last_len = 0; return (int)cur; }
            continue;
        }
        if (n > BIGFS_NAME_MAX) return BIGFS_E_NAME;
        if (is_last) { *last = s; *last_len = n; return (int)cur; }
        int c = find_child(fs, cur, s, n);
        if (c < 0) return BIGFS_E_NOENT;
        if (fs->ino[c].state != BIGFS_T_DIR) return BIGFS_E_NOTDIR;
        cur = (uint32_t)c;
    }
}

static int lookup_locked(struct bigfs *fs, const char *path, uint32_t *ino_out)
{
    const char *last; size_t ll;
    int par = resolve_parent(fs, path, &last, &ll);
    if (par < 0) return par;
    if (ll == 0) { *ino_out = (uint32_t)par; return (int)fs->ino[par].state; }
    int c = find_child(fs, (uint32_t)par, last, ll);
    if (c < 0) return BIGFS_E_NOENT;
    *ino_out = (uint32_t)c;
    return (int)fs->ino[c].state;
}

int bigfs_lookup(struct bigfs *fs, const char *path, uint32_t *ino_out)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int r = lookup_locked(fs, path, ino_out); unlock(fs); return r;
}

static void fill_stat(struct bigfs *fs, uint32_t id, struct bigfs_stat *st)
{
    struct inode *in = &fs->ino[id];
    st->type = in->state; st->ino = id; st->parent = in->parent; st->nextents = in->nextents;
    st->size = in->size; st->mtime = in->mtime;
    for (int i = 0; i < 64; ++i) st->name[i] = in->name[i];
    st->name[BIGFS_NAME_MAX] = 0;
}

int bigfs_stat(struct bigfs *fs, uint32_t ino, struct bigfs_stat *st)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs);
    int r = (ino >= fs->sb.inode_count || fs->ino[ino].state == BIGFS_T_FREE) ? BIGFS_E_NOENT : 0;
    if (r == 0) fill_stat(fs, ino, st);
    unlock(fs);
    return r;
}

int bigfs_readdir(struct bigfs *fs, uint32_t dir, uint32_t *cursor, struct bigfs_stat *out)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs);
    int r;
    if (dir >= fs->sb.inode_count || fs->ino[dir].state != BIGFS_T_DIR) r = BIGFS_E_NOTDIR;
    else {
        r = 0;
        uint32_t i = *cursor < 1 ? 1 : *cursor;
        for (; i < fs->sb.inode_count; ++i) {
            if (fs->ino[i].state != BIGFS_T_FREE && fs->ino[i].parent == dir) { fill_stat(fs, i, out); r = 1; i++; break; }
        }
        *cursor = i;
    }
    unlock(fs);
    return r;
}

static int alloc_inode(struct bigfs *fs)
{
    for (uint32_t i = 1; i < fs->sb.inode_count; ++i) if (fs->ino[i].state == BIGFS_T_FREE) return (int)i;
    return BIGFS_E_NOINODE;
}

static int create_node(struct bigfs *fs, const char *path, uint32_t type, uint32_t *ino_out)
{
    const char *last; size_t ll;
    int par = resolve_parent(fs, path, &last, &ll);
    if (par < 0) return par;
    if (ll == 0) return BIGFS_E_EXIST;                      /* la raiz / "." / ".." */
    if (!valid_name(last, ll)) return BIGFS_E_NAME;
    if (fs->ino[par].state != BIGFS_T_DIR) return BIGFS_E_NOTDIR;
    if (find_child(fs, (uint32_t)par, last, ll) >= 0) return BIGFS_E_EXIST;
    int id = alloc_inode(fs);
    if (id < 0) return id;
    struct inode *in = &fs->ino[id];
    ZERO(in, sizeof(*in));
    in->state = type; in->parent = (uint32_t)par;
    COPY(in->name, last, ll);
    int r = commit_inode(fs, (uint32_t)id);
    if (r) { ZERO(in, sizeof(*in)); return r; }
    fs->used_inodes++;
    if (ino_out) *ino_out = (uint32_t)id;
    return 0;
}

int bigfs_create(struct bigfs *fs, const char *path, uint32_t *ino_out)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int r = create_node(fs, path, BIGFS_T_FILE, ino_out); unlock(fs); return r;
}
int bigfs_mkdir(struct bigfs *fs, const char *path)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int r = create_node(fs, path, BIGFS_T_DIR, 0); unlock(fs); return r;
}

/* libera los clusteres de un inodo en el bitmap de RAM */
static void free_extents(struct bigfs *fs, const struct inode *in)
{
    for (uint32_t k = 0; k < in->nextents; ++k)
        for (uint32_t c = 0; c < in->ext[k].count; ++c) {
            uint64_t cl = (uint64_t)in->ext[k].start + c;
            if (bit_get(fs->bitmap, cl)) { bit_clr(fs->bitmap, cl); fs->free_clusters++; }
        }
}

static int remove_node(struct bigfs *fs, const char *path, uint32_t want_type)
{
    uint32_t id; int t = lookup_locked(fs, path, &id);
    if (t < 0) return t;
    if (id == 0) return BIGFS_E_INVAL;
    if ((uint32_t)t != want_type) return want_type == BIGFS_T_FILE ? BIGFS_E_ISDIR : BIGFS_E_NOTDIR;
    if (want_type == BIGFS_T_DIR) {
        for (uint32_t i = 1; i < fs->sb.inode_count; ++i)
            if (fs->ino[i].state != BIGFS_T_FREE && fs->ino[i].parent == id) return BIGFS_E_NOTEMPTY;
    }
    struct inode saved = fs->ino[id];
    ZERO(&fs->ino[id], sizeof(struct inode));                 /* estado libre: crc 0 no se valida */
    uint32_t sec = id / 2;
    int r = dev_write(fs, fs->sb.inode_start + sec, &fs->ino[sec * 2], 1);
    if (r) { fs->ino[id] = saved; return r; }
    free_extents(fs, &saved);
    fs->used_inodes--;
    return 0;
}

int bigfs_unlink(struct bigfs *fs, const char *path)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int r = remove_node(fs, path, BIGFS_T_FILE); unlock(fs); return r;
}
int bigfs_rmdir(struct bigfs *fs, const char *path)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int r = remove_node(fs, path, BIGFS_T_DIR); unlock(fs); return r;
}

int bigfs_rename(struct bigfs *fs, const char *from, const char *to)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs);
    int r;
    uint32_t id; int t = lookup_locked(fs, from, &id);
    const char *last; size_t ll;
    if (t < 0) r = t;
    else if (id == 0) r = BIGFS_E_INVAL;
    else {
        int par = resolve_parent(fs, to, &last, &ll);
        if (par < 0) r = par;
        else if (ll == 0) r = BIGFS_E_EXIST;
        else if (!valid_name(last, ll)) r = BIGFS_E_NAME;
        else if (fs->ino[par].state != BIGFS_T_DIR) r = BIGFS_E_NOTDIR;
        else if (find_child(fs, (uint32_t)par, last, ll) >= 0) r = BIGFS_E_EXIST;
        else {
            r = 0;
            if (t == (int)BIGFS_T_DIR) {                         /* no se puede mover un directorio dentro de si mismo */
                uint32_t cur = (uint32_t)par, steps = 0;
                for (;;) {
                    if (cur == id) { r = BIGFS_E_INVAL; break; }
                    if (cur == 0 || ++steps > fs->sb.inode_count) break;
                    cur = fs->ino[cur].parent;
                }
            }
            if (r == 0) {
                struct inode saved = fs->ino[id];
                fs->ino[id].parent = (uint32_t)par;
                ZERO(fs->ino[id].name, 64); COPY(fs->ino[id].name, last, ll);
                r = commit_inode(fs, id);                         /* una sola escritura de sector */
                if (r) fs->ino[id] = saved;
            }
        }
    }
    unlock(fs);
    return r;
}

/* ---------------------------------------------------------------- datos */
static uint64_t file_clusters(const struct inode *in)
{
    uint64_t n = 0; for (uint32_t k = 0; k < in->nextents; ++k) n += in->ext[k].count; return n;
}

/* Ejecuta E/S sobre [off, off+len) de un archivo cuyos extents ya cubren el rango. */
static int file_io(struct bigfs *fs, struct inode *in, uint64_t off, uint8_t *buf, uint64_t len, int is_write)
{
    while (len > 0) {
        uint64_t cidx = off / BIGFS_CLUSTER, within = off % BIGFS_CLUSTER;
        uint64_t cum = 0; uint32_t k = 0;
        for (; k < in->nextents; ++k) { if (cidx < cum + in->ext[k].count) break; cum += in->ext[k].count; }
        if (k >= in->nextents) return BIGFS_E_CORRUPT;
        uint64_t ext_clusters_left = in->ext[k].count - (cidx - cum);
        uint64_t run_bytes = ext_clusters_left * BIGFS_CLUSTER - within;        /* hasta el final del extent */
        uint64_t n = len < run_bytes ? len : run_bytes;
        uint64_t lba = fs->sb.data_start + ((uint64_t)in->ext[k].start + (cidx - cum)) * BIGFS_CLUSTER_SECT + within / BIGFS_SECTOR;
        uint32_t soff = (uint32_t)(within % BIGFS_SECTOR);

        /* cabeza no alineada */
        if (soff) {
            uint8_t sec[BIGFS_SECTOR];
            uint32_t take = BIGFS_SECTOR - soff; if (take > n) take = (uint32_t)n;
            int r = dev_read(fs, lba, sec, 1);
            if (r) return r;
            if (is_write) { COPY(sec + soff, buf, take); r = dev_write(fs, lba, sec, 1); if (r) return r; }
            else COPY(buf, sec + soff, take);
            buf += take; off += take; len -= take; n -= take; lba++;
            if (n == 0) continue;
        }
        /* cuerpo alineado, en bloques */
        while (n >= BIGFS_SECTOR) {
            uint32_t ns = (uint32_t)(n / BIGFS_SECTOR);
            if (ns > SCRATCH / BIGFS_SECTOR) ns = SCRATCH / BIGFS_SECTOR;
            int r = is_write ? dev_write(fs, lba, buf, ns) : dev_read(fs, lba, buf, ns);
            if (r) return r;
            uint64_t b = (uint64_t)ns * BIGFS_SECTOR;
            buf += b; off += b; len -= b; n -= b; lba += ns;
        }
        /* cola no alineada */
        if (n > 0) {
            uint8_t sec[BIGFS_SECTOR];
            int r = dev_read(fs, lba, sec, 1);
            if (r) return r;
            if (is_write) { COPY(sec, buf, n); r = dev_write(fs, lba, sec, 1); if (r) return r; }
            else COPY(buf, sec, n);
            buf += n; off += n; len -= n;
        }
    }
    return 0;
}

/* Reserva `need` clusteres para `in` (en RAM: bitmap y extents). Si falla no deja nada reservado. */
static int grow_clusters(struct bigfs *fs, struct inode *in, uint64_t need)
{
    if (need == 0) return 0;
    if (need > fs->free_clusters) return BIGFS_E_NOSPC;
    struct inode backup = *in;
    uint64_t remaining = need;
    uint64_t total = fs->sb.data_clusters;

    /* 1) extender el ultimo extent si lo que hay justo detras esta libre */
    if (in->nextents > 0) {
        struct extent *e = &in->ext[in->nextents - 1];
        uint64_t pos = (uint64_t)e->start + e->count;
        while (remaining > 0 && pos < total && !bit_get(fs->bitmap, pos) && e->count < 0xFFFFFFF0u) {
            bit_set(fs->bitmap, pos); fs->free_clusters--; e->count++; pos++; remaining--;
        }
    }
    /* 2) nuevos extents: primer hueco que quepa entero; si no, el hueco mas grande */
    while (remaining > 0) {
        uint64_t best_s = 0, best_n = 0, s = fs->hint < total ? fs->hint : 0;
        int found = 0;
        for (int pass = 0; pass < 2 && !found; ++pass) {
            uint64_t from = pass == 0 ? s : 0, to = pass == 0 ? total : s;
            uint64_t c = from;
            while (c < to) {
                if (bit_get(fs->bitmap, c)) { c++; continue; }
                uint64_t run = c; while (run < total && !bit_get(fs->bitmap, run) && run - c < remaining) run++;
                uint64_t n = run - c;
                if (n >= remaining) { best_s = c; best_n = n; found = 1; break; }
                if (n > best_n) { best_s = c; best_n = n; }
                c = run;
            }
        }
        if (best_n == 0) break;
        if (in->nextents >= BIGFS_MAX_EXTENTS) break;
        uint64_t take = best_n < remaining ? best_n : remaining;
        for (uint64_t i = 0; i < take; ++i) bit_set(fs->bitmap, best_s + i);
        fs->free_clusters -= take;
        in->ext[in->nextents].start = (uint32_t)best_s; in->ext[in->nextents].count = (uint32_t)take;
        in->nextents++;
        fs->hint = (uint32_t)(best_s + take);
        remaining -= take;
    }
    if (remaining > 0) {                                       /* deshacer */
        int fbig = in->nextents >= BIGFS_MAX_EXTENTS;           /* hay hueco, pero en demasiados trozos */
        for (uint32_t k = 0; k < in->nextents; ++k)
            for (uint32_t c = 0; c < in->ext[k].count; ++c) {
                uint64_t cl = (uint64_t)in->ext[k].start + c;
                int was_old = 0;
                for (uint32_t j = 0; j < backup.nextents && !was_old; ++j)
                    if (cl >= backup.ext[j].start && cl < (uint64_t)backup.ext[j].start + backup.ext[j].count) was_old = 1;
                if (!was_old && bit_get(fs->bitmap, cl)) { bit_clr(fs->bitmap, cl); fs->free_clusters++; }
            }
        *in = backup;
        return fbig ? BIGFS_E_FBIG : BIGFS_E_NOSPC;
    }
    return 0;
}

static void rollback_clusters(struct bigfs *fs, struct inode *in, const struct inode *backup)
{
    for (uint32_t k = 0; k < in->nextents; ++k)
        for (uint32_t c = 0; c < in->ext[k].count; ++c) {
            uint64_t cl = (uint64_t)in->ext[k].start + c;
            int was_old = 0;
            for (uint32_t j = 0; j < backup->nextents && !was_old; ++j)
                if (cl >= backup->ext[j].start && cl < (uint64_t)backup->ext[j].start + backup->ext[j].count) was_old = 1;
            if (!was_old && bit_get(fs->bitmap, cl)) { bit_clr(fs->bitmap, cl); fs->free_clusters++; }
        }
    *in = *backup;
}

static int zero_range(struct bigfs *fs, struct inode *in, uint64_t from, uint64_t to)
{
    while (from < to) {
        uint64_t n = to - from; if (n > SCRATCH) n = SCRATCH;
        ZERO(fs->scratch, n);
        int r = file_io(fs, in, from, fs->scratch, n, 1);
        if (r) return r;
        from += n;
    }
    return 0;
}

static int64_t write_locked(struct bigfs *fs, uint32_t id, uint64_t off, const void *data, uint64_t len)
{
    if (id == 0 || id >= fs->sb.inode_count) return BIGFS_E_INVAL;
    struct inode *in = &fs->ino[id];
    if (in->state == BIGFS_T_DIR) return BIGFS_E_ISDIR;
    if (in->state != BIGFS_T_FILE) return BIGFS_E_NOENT;
    if (off + len < off || off + len > 0x7FFFFFFFFFFFFFFFull) return BIGFS_E_INVAL;
    uint64_t end = off + len, old_size = in->size;
    struct inode backup = *in;

    uint64_t have = file_clusters(in) * BIGFS_CLUSTER;
    if (end > have) {
        int r = grow_clusters(fs, in, (end - have + BIGFS_CLUSTER - 1) / BIGFS_CLUSTER);
        if (r) return r;
    }
    int r = 0;
    if (off > old_size) r = zero_range(fs, in, old_size, off);       /* el hueco se lee como ceros */
    if (r == 0 && len > 0) {
        const uint8_t *src = (const uint8_t *)data;
        /* file_io necesita un buffer escribible; las escrituras no lo modifican */
        r = file_io(fs, in, off, (uint8_t *)(uintptr_t)src, len, 1);
    }
    if (r == 0 && (end > old_size || in->nextents != backup.nextents || file_clusters(in) != file_clusters(&backup))) {
        if (end > old_size) in->size = end;
        r = commit_inode(fs, id);                                    /* confirmacion atomica: datos ya escritos */
    }
    if (r) { rollback_clusters(fs, in, &backup); return r; }
    return (int64_t)len;
}

int64_t bigfs_write(struct bigfs *fs, uint32_t ino, uint64_t off, const void *buf, uint64_t len)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs); int64_t r = write_locked(fs, ino, off, buf, len); unlock(fs); return r;
}

int64_t bigfs_read(struct bigfs *fs, uint32_t ino, uint64_t off, void *buf, uint64_t len)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs);
    int64_t r;
    if (ino == 0 || ino >= fs->sb.inode_count) r = BIGFS_E_INVAL;
    else if (fs->ino[ino].state == BIGFS_T_DIR) r = BIGFS_E_ISDIR;
    else if (fs->ino[ino].state != BIGFS_T_FILE) r = BIGFS_E_NOENT;
    else {
        struct inode *in = &fs->ino[ino];
        if (off >= in->size) r = 0;
        else {
            if (len > in->size - off) len = in->size - off;
            int e = file_io(fs, in, off, (uint8_t *)buf, len, 0);
            r = e ? e : (int64_t)len;
        }
    }
    unlock(fs);
    return r;
}

int bigfs_truncate(struct bigfs *fs, uint32_t ino, uint64_t size)
{
    if (!fs) return BIGFS_E_NOTMNT;
    lock(fs);
    int r = 0;
    if (ino == 0 || ino >= fs->sb.inode_count) r = BIGFS_E_INVAL;
    else if (fs->ino[ino].state == BIGFS_T_DIR) r = BIGFS_E_ISDIR;
    else if (fs->ino[ino].state != BIGFS_T_FILE) r = BIGFS_E_NOENT;
    else if (size > 0x7FFFFFFFFFFFFFFFull) r = BIGFS_E_INVAL;
    else {
        struct inode *in = &fs->ino[ino];
        if (size > in->size) {
            r = (int)write_locked(fs, ino, size, 0, 0);             /* crece: hueco de ceros */
            if (r >= 0) r = 0;
        } else if (size < in->size) {
            struct inode backup = *in;
            uint64_t keep = (size + BIGFS_CLUSTER - 1) / BIGFS_CLUSTER, acc = 0;
            struct inode freed; ZERO(&freed, sizeof(freed));
            uint32_t nk = 0;
            for (uint32_t k = 0; k < in->nextents; ++k) {
                uint64_t c = in->ext[k].count;
                if (acc + c <= keep) { acc += c; nk = k + 1; continue; }
                uint64_t keepc = keep > acc ? keep - acc : 0;
                if (keepc > 0) { freed.ext[freed.nextents].start = in->ext[k].start + (uint32_t)keepc; freed.ext[freed.nextents].count = (uint32_t)(c - keepc); freed.nextents++; in->ext[k].count = (uint32_t)keepc; nk = k + 1; }
                else { freed.ext[freed.nextents] = in->ext[k]; freed.nextents++; }
                acc += c;
            }
            in->nextents = nk; in->size = size;
            r = commit_inode(fs, ino);
            if (r) *in = backup;
            else free_extents(fs, &freed);
        }
    }
    unlock(fs);
    return r;
}
