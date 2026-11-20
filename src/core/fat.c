/* PARCHE 050: ver fat.h. Toda longitud y cluster que sale del disco se valida; ningun bucle depende de datos del disco sin limite. */
#include "fat.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

const char *fat_err_str(int e)
{
    switch (e) {
    case 0: return "OK";
    case FAT_E_IO: return "error de lectura del dispositivo";
    case FAT_E_NOFAT: return "no hay un sistema de archivos FAT reconocible";
    case FAT_E_UNSUP: return "FAT no soportado (sector distinto de 512 bytes o tabla GPT)";
    case FAT_E_NOENT: return "no existe";
    case FAT_E_NOTDIR: return "no es un directorio";
    case FAT_E_ISDIR: return "es un directorio";
    case FAT_E_CORRUPT: return "estructura FAT danada";
    case FAT_E_ARG: return "argumentos invalidos";
    default: return "error";
    }
}

static int rd(struct fat_vol *v, uint64_t lba, uint32_t n, void *buf)
{
    if (lba >= v->io.sectors || n > v->io.sectors - lba) return FAT_E_IO;
    return v->io.read(v->io.ctx, lba, n, buf) == 0 ? 0 : FAT_E_IO;
}

/* ---- montaje ------------------------------------------------------------------------------------------------------------------ */
static int pow2(uint32_t x) { return x && !(x & (x - 1)); }

/* Intenta interpretar el sector s (ya leido) como sector de arranque FAT de una particion que empieza en lba y tiene psec sectores. */
static int try_bpb(struct fat_vol *v, const uint8_t *s, uint64_t lba, uint64_t psec)
{
    if (s[510] != 0x55 || s[511] != 0xAA) return FAT_E_NOFAT;
    uint32_t bps = le16(s + 11), spc = s[13], rsv = le16(s + 14), nf = s[16], rootent = le16(s + 17);
    uint32_t tot16 = le16(s + 19), fsz16 = le16(s + 22), tot32 = le32(s + 32), fsz32 = le32(s + 36);
    if (bps != 512) return (bps == 1024 || bps == 2048 || bps == 4096) && pow2(spc) ? FAT_E_UNSUP : FAT_E_NOFAT;
    if (!pow2(spc) || spc > 128 || rsv == 0 || nf < 1 || nf > 2) return FAT_E_NOFAT;
    uint32_t fsz = fsz16 ? fsz16 : fsz32, tot = tot16 ? tot16 : tot32;
    if (fsz == 0 || tot == 0 || (rootent * 32u) % 512 != 0) return FAT_E_NOFAT;
    if (psec == 0) psec = tot;
    if (tot > psec || tot > v->io.sectors - lba) return FAT_E_NOFAT;
    uint32_t root_secs = rootent * 32u / 512u;
    uint64_t meta = (uint64_t)rsv + (uint64_t)nf * fsz + root_secs;
    if (meta >= tot) return FAT_E_NOFAT;
    uint32_t clusters = (uint32_t)(((uint64_t)tot - meta) / spc);
    int is32 = (fsz16 == 0 && rootent == 0);
    int type = is32 ? 32 : (clusters < 4085 ? 12 : 16);
    if (is32 && clusters < 65525) return FAT_E_NOFAT;                        /* una FAT32 con tan pocos clusters no es valida */
    if (!is32 && clusters >= 65525) return FAT_E_NOFAT;
    if (clusters < 1 || clusters > 0x0FFFFFF0u) return FAT_E_NOFAT;
    uint32_t need;
    if (type == 12) need = ((clusters + 2) * 3 / 2 + 1 + 511) / 512;
    else if (type == 16) need = ((clusters + 2) * 2 + 511) / 512;
    else need = ((clusters + 2) * 4 + 511) / 512;
    if (fsz < need) return FAT_E_NOFAT;                                      /* la FAT no cubre todos los clusters */
    v->type = type; v->part_lba = lba; v->part_sectors = psec;
    v->fat_lba = (uint32_t)(lba + rsv); v->fat_sectors = fsz; v->nfats = nf;
    v->root_entries = rootent; v->root_lba = v->fat_lba + nf * fsz;
    v->data_lba = v->root_lba + root_secs; v->spc = spc; v->clusters = clusters; v->cluster_bytes = spc * 512u;
    v->root_cluster = 0;
    if (type == 32) {
        v->root_cluster = le32(s + 44);
        if (v->root_cluster < 2 || v->root_cluster >= clusters + 2) return FAT_E_NOFAT;
    }
    if ((uint64_t)v->data_lba + (uint64_t)clusters * spc > v->io.sectors) return FAT_E_NOFAT;
    {   /* etiqueta del sector de arranque (extendido si la firma 0x29 esta) */
        uint32_t lo = is32 ? 66 : 38;
        for (int i = 0; i < 12; i++) v->label[i] = 0;
        if (s[lo] == 0x29) { for (int i = 0; i < 11; i++) { uint8_t c = s[lo + 5 + i]; v->label[i] = (c >= 32 && c < 127) ? (char)c : ' '; } for (int i = 10; i >= 0 && v->label[i] == ' '; i--) v->label[i] = 0; }
    }
    v->fcs = 0xFFFFFFFFu;
    return 0;
}

int fat_mount(struct fat_vol *v, const struct fat_io *io)
{
    if (!v || !io || !io->read || io->sectors < 2) return FAT_E_ARG;
    v->io = *io; v->part_index = 0;
    static uint8_t s0[512];
    int r = rd(v, 0, 1, s0);
    if (r != 0) return r;
    int e = try_bpb(v, s0, 0, 0);                                             /* disco sin particiones */
    if (e == 0) { v->part_index = 0; goto label; }
    if (e == FAT_E_UNSUP) return e;
    if (s0[510] != 0x55 || s0[511] != 0xAA) return FAT_E_NOFAT;
    int unsup = 0;
    for (int i = 0; i < 4; i++) {                                             /* MBR: primera particion FAT que monte */
        const uint8_t *pe = s0 + 446 + 16 * i;
        uint8_t t = pe[4]; uint32_t st = le32(pe + 8), sz = le32(pe + 12);
        if (t == 0xEE) { unsup = 1; continue; }                               /* GPT protectora */
        if (!(t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E)) continue;
        if (st == 0 || sz == 0 || st >= io->sectors) continue;
        static uint8_t sb[512];
        if (rd(v, st, 1, sb) != 0) continue;
        int e2 = try_bpb(v, sb, st, sz);
        if (e2 == 0) { v->part_index = i + 1; goto label; }
        if (e2 == FAT_E_UNSUP) unsup = 1;
    }
    return unsup ? FAT_E_UNSUP : FAT_E_NOFAT;
label:
    return 0;
}

/* ---- FAT -------------------------------------------------------------------------------------------------------------------- */
/* Devuelve el siguiente cluster (>= 2), 0 = fin de cadena, o <0 si hay error. */
static int64_t fat_next(struct fat_vol *v, uint32_t cl)
{
    if (cl < 2 || cl >= v->clusters + 2) return FAT_E_CORRUPT;
    uint64_t off = v->type == 12 ? (uint64_t)cl * 3 / 2 : v->type == 16 ? (uint64_t)cl * 2 : (uint64_t)cl * 4;
    uint32_t sec = (uint32_t)(off / 512), need = v->type == 12 ? 2u : v->type == 16 ? 2u : 4u;
    uint32_t last = (uint32_t)((off + need - 1) / 512);
    if (v->fcs == 0xFFFFFFFFu || sec < v->fcs || last > v->fcs + 1) {
        uint32_t n = (sec + 1 < v->fat_sectors) ? 2u : 1u;
        if (sec >= v->fat_sectors) return FAT_E_CORRUPT;
        if (rd(v, (uint64_t)v->fat_lba + sec, n, v->fbuf) != 0) { v->fcs = 0xFFFFFFFFu; return FAT_E_IO; }
        v->fcs = sec;
    }
    const uint8_t *p = v->fbuf + (off - (uint64_t)v->fcs * 512);
    uint32_t val;
    if (v->type == 12) { val = le16(p); val = (cl & 1) ? (val >> 4) : (val & 0xFFF); if (val >= 0xFF8) return 0; if (val == 0xFF7) return FAT_E_CORRUPT; }
    else if (v->type == 16) { val = le16(p); if (val >= 0xFFF8) return 0; if (val == 0xFFF7) return FAT_E_CORRUPT; }
    else { val = le32(p) & 0x0FFFFFFFu; if (val >= 0x0FFFFFF8u) return 0; if (val == 0x0FFFFFF7u) return FAT_E_CORRUPT; }
    if (val < 2) return 0;                                                    /* libre / reservado: cadena cortada */
    if (val >= v->clusters + 2) return FAT_E_CORRUPT;
    return (int64_t)val;
}

static uint64_t cluster_lba(const struct fat_vol *v, uint32_t cl) { return (uint64_t)v->data_lba + (uint64_t)(cl - 2) * v->spc; }

/* ---- directorios ------------------------------------------------------------------------------------------------------------ */
static uint8_t lfn_sum(const uint8_t *s11) { uint8_t s = 0; for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) << 7) + (s >> 1) + s11[i]); return s; }

static int put_utf8(char *d, int n, int max, uint32_t cp)
{
    if (cp < 0x80) { if (n + 1 >= max) return -1; d[n++] = (char)cp; }
    else if (cp < 0x800) { if (n + 2 >= max) return -1; d[n++] = (char)(0xC0 | cp >> 6); d[n++] = (char)(0x80 | (cp & 0x3F)); }
    else { if (n + 3 >= max) return -1; d[n++] = (char)(0xE0 | cp >> 12); d[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[n++] = (char)(0x80 | (cp & 0x3F)); }
    return n;
}

/* Recorre un directorio. cb(ctx, ent, attr_raw) devuelve !=0 para parar. start = 0 -> raiz fija FAT12/16. */
typedef int (*dir_cb)(void *ctx, const struct fat_dirent *e);

static int dir_walk(struct fat_vol *v, uint32_t start, dir_cb cb, void *ctx)
{
    uint16_t lfn[20 * 13]; int lfn_n = 0, lfn_next = 0; uint8_t lfn_cs = 0, have_lfn = 0;
    uint32_t cl = start; uint32_t guard = v->clusters + 8;
    uint32_t sec_in_dir = 0, total_secs = (start == 0 && v->type != 32) ? v->root_entries * 32u / 512u : 0;
    for (;;) {
        uint64_t lba; 
        if (start == 0 && v->type != 32) { if (sec_in_dir >= total_secs) return 0; lba = (uint64_t)v->root_lba + sec_in_dir; }
        else {
            if (cl < 2) return 0;
            uint32_t s = sec_in_dir % v->spc;
            lba = cluster_lba(v, cl) + s;
        }
        if (rd(v, lba, 1, v->dbuf) != 0) return FAT_E_IO;
        for (int i = 0; i < 16; i++) {
            const uint8_t *e = v->dbuf + 32 * i;
            if (e[0] == 0x00) return 0;                                       /* fin del directorio */
            if (e[0] == 0xE5) { have_lfn = 0; lfn_next = 0; continue; }
            uint8_t attr = e[11];
            if ((attr & 0x3F) == 0x0F) {                                      /* entrada de nombre largo */
                uint8_t ord = e[0];
                if (ord & 0x40) { lfn_next = ord & 0x3F; if (lfn_next < 1 || lfn_next > 20) { lfn_next = 0; have_lfn = 0; continue; } lfn_n = lfn_next; lfn_cs = e[13]; have_lfn = 1; }
                else if (!have_lfn || (ord & 0x3F) != lfn_next || e[13] != lfn_cs) { have_lfn = 0; lfn_next = 0; continue; }
                int seq = lfn_next; if (seq < 1) { have_lfn = 0; continue; }
                static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int k = 0; k < 13; k++) lfn[(seq - 1) * 13 + k] = le16(e + pos[k]);
                lfn_next--;
                continue;
            }
            if (attr & 0x08) {                                                /* etiqueta de volumen */
                have_lfn = 0; lfn_next = 0;
                for (int k = 0; k < 11; k++) v->label[k] = (char)e[k];
                v->label[11] = 0; for (int k = 10; k >= 0 && v->label[k] == ' '; k--) v->label[k] = 0;
                continue;
            }
            struct fat_dirent d;
            int n = 0;
            if (have_lfn && lfn_next == 0 && lfn_sum(e) == lfn_cs) {
                int total = lfn_n * 13;
                for (int k = 0; k < total; k++) {
                    uint32_t cp = lfn[k];
                    if (cp == 0 || cp == 0xFFFF) break;
                    if (cp >= 0xD800 && cp < 0xE000) cp = '?';                 /* pares subrogados: no soportados */
                    int r = put_utf8(d.name, n, FAT_NAME_MAX, cp);
                    if (r < 0) break;
                    n = r;
                }
            } else {
                uint8_t nt = e[12];
                for (int k = 0; k < 8 && e[k] != ' '; k++) { char c = (char)e[k]; if (k == 0 && (uint8_t)c == 0x05) c = (char)0xE5; if ((nt & 0x08) && c >= 'A' && c <= 'Z') c = (char)(c + 32); d.name[n++] = c; }
                if (e[8] != ' ') {
                    d.name[n++] = '.';
                    for (int k = 8; k < 11 && e[k] != ' '; k++) { char c = (char)e[k]; if ((nt & 0x10) && c >= 'A' && c <= 'Z') c = (char)(c + 32); d.name[n++] = c; }
                }
            }
            d.name[n] = 0;
            have_lfn = 0; lfn_next = 0;
            d.attr = attr; d.is_dir = (attr & 0x10) ? 1 : 0; d.size = le32(e + 28);
            d.cluster = (uint32_t)le16(e + 26) | (v->type == 32 ? (uint32_t)le16(e + 20) << 16 : 0u);
            d.time = le16(e + 22); d.date = le16(e + 24);
            if (d.is_dir) d.size = 0;
            if (cb(ctx, &d)) return 1;
        }
        sec_in_dir++;
        if (!(start == 0 && v->type != 32) && sec_in_dir % v->spc == 0) {
            int64_t nx = fat_next(v, cl);
            if (nx < 0) return (int)nx;
            if (nx == 0) return 0;
            cl = (uint32_t)nx;
            if (!guard--) return FAT_E_CORRUPT;                               /* bucle en la cadena */
        }
    }
}

static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

struct find_ctx { const char *name; struct fat_dirent *out; int found; };
static int find_cb(void *c, const struct fat_dirent *e)
{
    struct find_ctx *f = c;
    if (!ieq(e->name, f->name)) return 0;
    *f->out = *e; f->found = 1; return 1;
}

struct list_ctx { struct fat_dirent *out; int max, n, total; };
static int list_cb(void *c, const struct fat_dirent *e)
{
    struct list_ctx *l = c;
    if (e->name[0] == '.' && (e->name[1] == 0 || (e->name[1] == '.' && e->name[2] == 0))) return 0;
    l->total++;
    if (l->n < l->max) l->out[l->n++] = *e;
    return 0;
}

/* Resuelve una ruta (separada por '/'); path vacio o "/" = raiz. */
static int resolve(struct fat_vol *v, const char *path, struct fat_dirent *out)
{
    uint32_t cur = v->type == 32 ? v->root_cluster : 0;
    int cur_is_root = 1;
    struct fat_dirent last;
    for (int i = 0; i < FAT_NAME_MAX; i++) last.name[i] = 0;
    last.is_dir = 1; last.size = 0; last.cluster = cur; last.attr = 0x10; last.date = last.time = 0;
    const char *p = path ? path : "";
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[FAT_NAME_MAX]; int n = 0;
        while (*p && *p != '/') { if (n >= FAT_NAME_MAX - 1) return FAT_E_ARG; comp[n++] = *p++; }
        comp[n] = 0;
        if (!last.is_dir) return FAT_E_NOTDIR;
        struct find_ctx f = { comp, &last, 0 };
        int r = dir_walk(v, cur, find_cb, &f);
        if (r < 0) return r;
        if (!f.found) return FAT_E_NOENT;
        cur = last.cluster; cur_is_root = 0;
        if (last.is_dir && last.cluster == 0 && v->type != 32) cur = 0;       /* '..' hacia la raiz fija */
        if (last.is_dir && last.cluster == 0 && v->type == 32) cur = v->root_cluster;
    }
    (void)cur_is_root;
    *out = last;
    return 0;
}

int fat_stat(struct fat_vol *v, const char *path, struct fat_dirent *out) { return resolve(v, path, out); }

int fat_readdir(struct fat_vol *v, const char *path, struct fat_dirent *out, int max, int *total)
{
    static struct fat_dirent d;
    int r = resolve(v, path, &d);
    if (r != 0) return r;
    if (!d.is_dir) return FAT_E_NOTDIR;
    struct list_ctx l = { out, max, 0, 0 };
    uint32_t start = d.cluster ? d.cluster : (v->type == 32 ? v->root_cluster : 0);
    r = dir_walk(v, start, list_cb, &l);
    if (r < 0) return r;
    if (total) *total = l.total;
    return l.n;
}

int fat_read(struct fat_vol *v, const char *path, uint32_t offset, void *buf, uint32_t len)
{
    static struct fat_dirent d;
    int r = resolve(v, path, &d);
    if (r != 0) return r;
    if (d.is_dir) return FAT_E_ISDIR;
    if (offset >= d.size || len == 0) return 0;
    if (len > d.size - offset) len = d.size - offset;
    if (d.cluster < 2) return FAT_E_CORRUPT;
    uint32_t cl = d.cluster, cb = v->cluster_bytes, skip = offset / cb, inpos = offset % cb, guard = v->clusters + 8;
    for (uint32_t i = 0; i < skip; i++) {
        int64_t nx = fat_next(v, cl);
        if (nx < 0) return (int)nx;
        if (nx == 0 || !guard--) return FAT_E_CORRUPT;
        cl = (uint32_t)nx;
    }
    uint8_t *dst = buf; uint32_t remaining = len;
    while (remaining) {
        uint32_t chunk = cb - inpos < remaining ? cb - inpos : remaining;
        uint64_t lba = cluster_lba(v, cl) + inpos / 512;
        if (inpos % 512 == 0 && chunk % 512 == 0) {
            if (rd(v, lba, chunk / 512, dst) != 0) return FAT_E_IO;
        } else {
            uint32_t done = 0, pos = inpos;
            while (done < chunk) {
                if (rd(v, cluster_lba(v, cl) + pos / 512, 1, v->dbuf) != 0) return FAT_E_IO;
                uint32_t o = pos % 512, c = 512 - o < chunk - done ? 512 - o : chunk - done;
                for (uint32_t k = 0; k < c; k++) dst[done + k] = v->dbuf[o + k];
                done += c; pos += c;
            }
        }
        dst += chunk; remaining -= chunk; inpos = 0;
        if (remaining) {
            int64_t nx = fat_next(v, cl);
            if (nx < 0) return (int)nx;
            if (nx == 0 || !guard--) return FAT_E_CORRUPT;
            cl = (uint32_t)nx;
        }
    }
    return (int)len;
}

/* SOMA_228_ARRANQUE: sobrescribe EN SU SITIO el contenido de un fichero que ya existe (len <= su tamano; el resto se rellena con pad).
 * No reserva clusters, no cambia el tamano ni toca la FAT ni los directorios: solo los sectores de datos de la cadena del propio
 * fichero, comprobados uno a uno contra la particion. Asi SOMA puede dejar un texto en la particion de arranque del pendrive
 * (INFORME.TXT, que crea vacio construyeimagen.py) sin riesgo para el resto del sistema de archivos. Devuelve 0 o <0. */
int fat_overwrite(struct fat_vol *v, const char *path, const void *data, uint32_t len, uint8_t pad,
                  int (*wr)(void *ctx, uint64_t lba, uint32_t n, const void *buf))
{
    static struct fat_dirent d;
    if (!v || !wr) return FAT_E_ARG;
    int r = resolve(v, path, &d);
    if (r != 0) return r;
    if (d.is_dir) return FAT_E_ISDIR;
    if (len > d.size) return FAT_E_ARG;
    if (d.size == 0) return 0;
    if (d.cluster < 2) return FAT_E_CORRUPT;
    const uint8_t *src = (const uint8_t *)data;
    uint32_t cl = d.cluster, done = 0, guard = v->clusters + 8;
    uint64_t pend = v->part_index ? v->part_lba + v->part_sectors : v->io.sectors;
    for (;;) {
        if (cl < 2 || cl >= v->clusters + 2) return FAT_E_CORRUPT;
        uint64_t lba = cluster_lba(v, cl);
        for (uint32_t s = 0; s < v->spc && done < d.size; s++, done += 512) {
            uint64_t a = lba + s;
            if (a < v->data_lba || a >= pend || a >= v->io.sectors) return FAT_E_CORRUPT;
            if (d.size - done < 512 && rd(v, a, 1, v->dbuf) != 0) return FAT_E_IO;   /* ultimo sector: se conserva lo de detras del final */
            uint32_t m = d.size - done < 512 ? d.size - done : 512;
            for (uint32_t k = 0; k < m; k++) v->dbuf[k] = done + k < len ? src[done + k] : pad;
            if (wr(v->io.ctx, a, 1, v->dbuf) != 0) return FAT_E_IO;
        }
        if (done >= d.size) break;
        int64_t nx = fat_next(v, cl);
        if (nx < 0) return (int)nx;
        if (nx == 0 || !guard--) return FAT_E_CORRUPT;                             /* la cadena es mas corta que el fichero */
        cl = (uint32_t)nx;
    }
    return 0;
}
