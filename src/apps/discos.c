/* SOMA_331_DISCOS: gestor de discos (orden "discos" y aplicacion Discos). Lo que viene del disco lo interpreta discos_core.c
 * (tablas de particiones y sistemas de archivos, probado con datos hostiles); aqui va lo que toca el hardware:
 *   - la lista de discos de la capa de bloque (blk), con su tabla y el sistema de archivos de cada particion;
 *   - las operaciones que ESCRIBEN (nueva tabla, crear, borrar y formatear particiones, grabar una imagen), en un hilo de trabajo
 *     con progreso. Antes de escribir en un disco hay que desbloquearlo escribiendo su nombre: la capa de bloque solo escribe en
 *     discos de prueba de QEMU o en los desbloqueados asi. El disco del sistema (el pendrive de SOMA o el disco virtio) nunca. */
#include <stdint.h>
#include "discos.h"
#include "discos_core.h"
#include "blk.h"
#include "exfat.h"
#include "mem.h"
#include "thread.h"
#include "console.h"
#include "idt.h"
#include "rng.h"
#include "bigfs.h"
#include "bigfs_glue.h"
#include "unidades.h"
#if defined(__x86_64__)
#include "usbdisk.h"
#endif

static struct disco_info g_d[BLK_MAX_DEV];
static int g_n;
static uint8_t *g_buf;                                   /* 34 sectores de trabajo para las tablas */
static volatile int g_estado;                            /* DISCOS_LIBRE / TRABAJANDO / HECHO / ERROR */
static volatile int g_pct;
static char g_msg[200];
static volatile uint32_t g_ver;

static void sput(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void sdec(char *o, uint32_t cap, uint32_t *p, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } sput(o, cap, p, b + i); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

void discos_tam(char *o, uint32_t cap, uint64_t sectores)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    uint64_t b = sectores * 512;
    static const char *const u[] = { "B", "KB", "MB", "GB", "TB" };
    int k = 0; uint64_t ent = b, dec = 0;
    while (ent >= 1000 && k < 4) { dec = (ent % 1000) / 100; ent /= 1000; k++; }
    sdec(o, cap, &p, ent);
    if (k && ent < 100) { sput(o, cap, &p, ","); sdec(o, cap, &p, dec); }
    sput(o, cap, &p, " "); sput(o, cap, &p, u[k]);
}

static int rd(void *ctx, uint64_t lba, uint32_t n, void *buf) { return blk_read((int)(intptr_t)ctx, lba, n, buf) == 0 ? 0 : -1; }
static int wr(void *ctx, uint64_t lba, uint32_t n, const void *buf) { return blk_write((int)(intptr_t)ctx, lba, n, buf) == 0 ? 0 : -1; }

int discos_es_sistema(int dev)
{
    const struct blkdev *b = blk_get(dev);
    if (!b) return 1;
    if (!b->write) return 1;                             /* el disco virtio del sistema: la capa de bloque ni sabe escribir en el */
#if defined(__x86_64__)
    const struct usbdisk_info *u = usbdisk_info();
    if (u && u->active && b->name[0] == 'u' && b->name[1] == 's' && b->name[2] == 'b') return 1;   /* SOMA arranco de este pendrive */
#endif
    return 0;
}

static void lee_disco(int i)
{
    struct disco_info *d = &g_d[i];
    const struct blkdev *b = blk_get(i);
    d->dev = i; d->sectores = b->sectors; d->sistema = discos_es_sistema(i); d->escribible = b->write && b->test_disk && !d->sistema;
    uint32_t k = 0; while (b->name[k] && k + 1 < sizeof d->nombre) { d->nombre[k] = b->name[k]; k++; } d->nombre[k] = 0;
    d->driver = b->driver;
    if (dc_lee(rd, (void *)(intptr_t)i, b->sectors, &d->tabla, g_buf) != 0) { d->tabla.esquema = DC_NADA; d->tabla.n = 0; d->error_lectura = 1; }
    else d->error_lectura = 0;
    for (int p = 0; p < d->tabla.n; p++) d->fs[p] = dc_fs(rd, (void *)(intptr_t)i, d->tabla.p[p].lba, d->tabla.p[p].n, d->etiqueta[p], sizeof d->etiqueta[p], g_buf);
    d->fs_disco = d->tabla.esquema == DC_NADA ? dc_fs(rd, (void *)(intptr_t)i, 0, b->sectors, d->etiqueta_disco, sizeof d->etiqueta_disco, g_buf) : DC_FS_DESCONOCIDO;
}

static int g_escaneado;
int discos_lista(void)
{
    if (!g_buf) g_buf = (uint8_t *)kmalloc(512 * 34);
    if (!g_buf) return 0;
    if (g_estado == DISCOS_TRABAJANDO) return g_n;       /* mientras se escribe no se relee */
    if (!g_escaneado) { (void)blk_scan(); g_escaneado = 1; }   /* la primera vez se buscan los discos */
    g_n = blk_count();
    if (g_n > BLK_MAX_DEV) g_n = BLK_MAX_DEV;
    for (int i = 0; i < g_n; i++) lee_disco(i);
    g_ver++;
    return g_n;
}
/* busca otra vez los discos (uno enchufado despues): los desbloqueos se pierden */
int discos_reescanea(void)
{
    if (g_estado == DISCOS_TRABAJANDO) return g_n;
    g_escaneado = 0;
    return discos_lista();
}
const struct disco_info *discos_info(int i) { return i >= 0 && i < g_n ? &g_d[i] : 0; }
uint32_t discos_version(void) { return g_ver; }

int discos_desbloquea(int i, const char *confirmacion, const char **why)
{
    static const char *d; if (!why) why = &d;
    *why = "";
    if (i < 0 || i >= g_n) { *why = "no existe ese disco"; return -1; }
    if (g_d[i].sistema) { *why = "es el disco del sistema: SOMA no lo modifica desde aqui"; return -1; }
    if (!confirmacion || !seq(confirmacion, g_d[i].nombre)) { *why = "para confirmarlo hay que escribir el nombre del disco"; return -1; }
    if (blk_desbloquea(i) != 0) { *why = "este disco no admite escritura"; return -1; }
    g_d[i].escribible = 1; g_ver++;
    return 0;
}

/* ---------------------------------------------------------------- operaciones (hilo de trabajo) */
struct op { int tipo, dev, part, esquema, fs; uint64_t mib; char etiqueta[24]; char ruta[160]; };
static struct op g_op;

static void fin(int ok, const char *m1, const char *m2)
{
    uint32_t p = 0; g_msg[0] = 0; sput(g_msg, sizeof g_msg, &p, m1); sput(g_msg, sizeof g_msg, &p, m2);
    g_estado = ok ? DISCOS_HECHO : DISCOS_ERROR; g_pct = ok ? 100 : g_pct; g_ver++;
}
static void *x_alloc(uint32_t n) { return kmalloc(n); }
static void x_free(void *p) { kfree(p); }
static uint32_t x_now(void) { return 0; }

/* el tamano vale para ese sistema de archivos? Se mira ANTES de escribir la tabla: asi un tamano que no sirve no deja
 * una particion a medias. NULL = vale; si no, la razon */
static const char *fs_no_cabe(int fs, uint64_t n)
{
    if (fs == DC_FS_FAT32 && (n < DC_FAT32_MIN || n > 0xFFFFFFFFull)) return n < DC_FAT32_MIN ? "demasiado pequena para FAT32 (minimo unos 33 MiB): usa exFAT" : "demasiado grande para FAT32 (maximo 2 TiB): usa exFAT";
    if (fs == DC_FS_EXFAT && n < 8192) return "demasiado pequena para exFAT (minimo 4 MiB)";
    return 0;
}

/* formatea la particion p del disco (ya escrita en su tabla). 0 = bien */
static int formatea(int dev, struct dc_tabla *t, int p, int fs, const char *etiqueta, const char **why)
{
    uint64_t lba = t->p[p].lba, n = t->p[p].n;
    uint32_t serie = rng_u32();
    if (fs == DC_FS_FAT32) {
        uint8_t *b = (uint8_t *)kmalloc(65536);
        if (!b) { *why = "sin memoria"; return -1; }
        int r = dc_fat32(wr, (void *)(intptr_t)dev, lba, n, etiqueta, serie, b);
        kfree(b);
        if (r == -2) { *why = fs_no_cabe(fs, n) ? fs_no_cabe(fs, n) : "ese tamano no sirve para FAT32: usa exFAT"; return -1; }
        if (r) { *why = "error de escritura en el disco"; return -1; }
        if (t->esquema == DC_MBR && t->p[p].mbr_tipo != 0x0C && t->p[p].mbr_tipo != 0xEF) { t->p[p].mbr_tipo = 0x0C; if (dc_escribe(wr, (void *)(intptr_t)dev, t, g_buf)) { *why = "no se pudo actualizar la tabla"; return -1; } }
        return 0;
    }
    if (fs == DC_FS_EXFAT) {
        struct exfat_io io = { rd, wr, (void *)(intptr_t)dev, x_alloc, x_free, x_now };
        if (exfat_format(&io, lba, n, etiqueta, serie) != 0) { *why = "no se pudo dar formato exFAT"; return -1; }
        if (t->esquema == DC_MBR && t->p[p].mbr_tipo != 0x07) { t->p[p].mbr_tipo = 0x07; if (dc_escribe(wr, (void *)(intptr_t)dev, t, g_buf)) { *why = "no se pudo actualizar la tabla"; return -1; } }
        return 0;
    }
    *why = "sistema de archivos no soportado (exFAT o FAT32)";
    return -1;
}

/* abre la imagen a grabar: un archivo de /usuario (unidad del pendrive) o del almacen BIGFS. Devuelve su tamano o <0 */
struct origen { int unid; struct unid_fd fd; struct bigfs *fs; uint32_t ino; };
static int64_t origen_abre(const char *ruta, struct origen *o)
{
    const char *resto;
    if (unid_ruta(ruta, &resto) >= 0) {
        o->unid = 1;
        if (unid_abrir(ruta, &o->fd) != 0) return -1;
        return unid_fd_tam(&o->fd);
    }
    o->unid = 0;
    int e = 0; o->fs = bigfs_sys(&e);
    if (!o->fs) return -1;
    if (bigfs_lookup(o->fs, ruta, &o->ino) != (int)BIGFS_T_FILE) return -1;
    struct bigfs_stat st; if (bigfs_stat(o->fs, o->ino, &st) != 0) return -1;
    return (int64_t)st.size;
}
static int origen_lee(struct origen *o, uint64_t off, void *buf, uint32_t n)
{
    if (o->unid) return unid_pread(&o->fd, off, buf, n) == (int)n ? 0 : -1;
    return bigfs_read(o->fs, o->ino, off, buf, n) == (int64_t)n ? 0 : -1;
}

static void trabajo(void *arg)
{
    (void)arg;
    struct op o = g_op;
    const char *why = "";
    struct disco_info *d = &g_d[o.dev];
    struct dc_tabla t = d->tabla;
    void *ctx = (void *)(intptr_t)o.dev;
    switch (o.tipo) {
    case DISCOS_OP_DISCO: {                              /* todo el disco: tabla nueva con una sola particion formateada */
        dc_nueva(&t, o.esquema, d->sectores, rng_u32());
        g_pct = 5; g_ver++;
        if (dc_anade(&t, 0, DC_TIPO_DATOS, o.etiqueta, rng_u32()) < 0) { fin(0, "no cabe una particion en este disco", ""); break; }
        if ((why = fs_no_cabe(o.fs, t.p[0].n)) != 0) { fin(0, "el disco es ", why); break; }
        /* se borra el principio del disco (restos de otras tablas y sistemas de archivos) */
        uint8_t *z = (uint8_t *)kmalloc(65536);
        if (!z) { fin(0, "sin memoria", ""); break; }
        for (uint32_t i = 0; i < 65536; i++) z[i] = 0;
        int ok = blk_write(o.dev, 0, 128, z) == 0 && blk_write(o.dev, t.p[0].lba, 128, z) == 0 && blk_write(o.dev, d->sectores - 128, 128, z) == 0;
        kfree(z);
        if (!ok || dc_escribe(wr, ctx, &t, g_buf) != 0) { fin(0, "error de escritura en el disco", ""); break; }
        g_pct = 30; g_ver++;
        if (formatea(o.dev, &t, 0, o.fs, o.etiqueta, &why) != 0) { fin(0, why, ""); break; }
        fin(1, "disco listo: una particion ", dc_fs_nombre(o.fs));
        break;
    }
    case DISCOS_OP_NUEVA: {
        if (t.esquema == DC_NADA) dc_nueva(&t, d->sectores > 0xFFFFFFFFull ? DC_GPT : DC_MBR, d->sectores, rng_u32());
        int p = dc_anade(&t, o.mib * 2048, DC_TIPO_DATOS, o.etiqueta, rng_u32());
        if (p < 0) { fin(0, p == -1 ? "no caben mas particiones en esta tabla" : "no hay espacio libre suficiente", ""); break; }
        if ((why = fs_no_cabe(o.fs, t.p[p].n)) != 0) { fin(0, "la particion seria ", why); break; }
        if (dc_escribe(wr, ctx, &t, g_buf) != 0) { fin(0, "error de escritura en el disco", ""); break; }
        g_pct = 30; g_ver++;
        if (formatea(o.dev, &t, p, o.fs, o.etiqueta, &why) != 0) { fin(0, why, ""); break; }
        fin(1, "particion creada y formateada en ", dc_fs_nombre(o.fs));
        break;
    }
    case DISCOS_OP_BORRA:
        if (o.part < 0 || o.part >= t.n || t.p[o.part].logica) { fin(0, "esa particion no se puede borrar desde aqui", ""); break; }
        dc_quita(&t, o.part);
        if (dc_escribe(wr, ctx, &t, g_buf) != 0) { fin(0, "error de escritura en el disco", ""); break; }
        fin(1, "particion borrada", "");
        break;
    case DISCOS_OP_FORMATEA:
        if (o.part < 0 || o.part >= t.n) { fin(0, "no existe esa particion", ""); break; }
        g_pct = 10; g_ver++;
        if (formatea(o.dev, &t, o.part, o.fs, o.etiqueta, &why) != 0) { fin(0, why, ""); break; }
        fin(1, "particion formateada en ", dc_fs_nombre(o.fs));
        break;
    case DISCOS_OP_GRABA: {                              /* la imagen, tal cual, desde el sector 0, y se comprueba al final */
        struct origen org; int64_t tam = origen_abre(o.ruta, &org);
        if (tam <= 0) { fin(0, "no se puede leer la imagen ", o.ruta); break; }
        uint64_t sec = ((uint64_t)tam + 511) / 512;
        if (sec > d->sectores) { fin(0, "la imagen no cabe en el disco", ""); break; }
        const uint32_t CH = 1u << 20;
        uint8_t *a = (uint8_t *)kmalloc(CH), *b = (uint8_t *)kmalloc(CH);
        if (!a || !b) { if (a) kfree(a); if (b) kfree(b); fin(0, "sin memoria", ""); break; }
        int malo = 0;
        for (int pasada = 0; pasada < 2 && !malo; pasada++) {          /* 0 = grabar, 1 = comprobar */
            for (uint64_t off = 0; off < (uint64_t)tam && !malo; off += CH) {
                uint32_t n = (uint64_t)tam - off < CH ? (uint32_t)((uint64_t)tam - off) : CH;
                uint32_t ns = (n + 511) / 512;
                for (uint32_t k = n; k < ns * 512; k++) a[k] = 0;
                if (origen_lee(&org, off, a, n) != 0) { malo = 1; fin(0, "error leyendo la imagen", ""); break; }
                if (pasada == 0) { if (blk_write(o.dev, off / 512, ns, a) != 0) { malo = 1; fin(0, "error de escritura en el disco", ""); break; } }
                else {
                    if (blk_read(o.dev, off / 512, ns, b) != 0) { malo = 1; fin(0, "error leyendo el disco para comprobarlo", ""); break; }
                    for (uint32_t k = 0; k < n; k++) if (a[k] != b[k]) { malo = 1; fin(0, "la comprobacion ha fallado: el disco no guarda bien lo escrito", ""); break; }
                }
                g_pct = (int)(pasada * 50 + (off + n) * 50 / (uint64_t)tam); g_ver++;
                thread_yield();
            }
        }
        kfree(a); kfree(b);
        if (!malo) { static char m[64]; discos_tam(m, sizeof m, sec); fin(1, "imagen grabada y comprobada: ", m); }
        break;
    }
    default: fin(0, "operacion desconocida", "");
    }
    lee_disco(o.dev);
    g_ver++;
}

int discos_op(int tipo, int dev, int part, int esquema, int fs, uint64_t mib, const char *etiqueta, const char *ruta, const char **why)
{
    static const char *d; if (!why) why = &d;
    *why = "";
    if (g_estado == DISCOS_TRABAJANDO) { *why = "ya hay una operacion en marcha"; return -1; }
    if (dev < 0 || dev >= g_n) { *why = "no existe ese disco"; return -1; }
    if (g_d[dev].sistema) { *why = "es el disco del sistema: SOMA no lo modifica desde aqui"; return -1; }
    if (!g_d[dev].escribible) { *why = "el disco esta protegido: desbloquealo antes (escribiendo su nombre)"; return -1; }
    struct op o; uint8_t *z = (uint8_t *)&o; for (uint32_t i = 0; i < sizeof o; i++) z[i] = 0;
    o.tipo = tipo; o.dev = dev; o.part = part; o.esquema = esquema == DC_MBR ? DC_MBR : DC_GPT; o.fs = fs == DC_FS_FAT32 ? DC_FS_FAT32 : DC_FS_EXFAT; o.mib = mib;
    for (uint32_t i = 0; etiqueta && etiqueta[i] && i + 1 < sizeof o.etiqueta; i++) o.etiqueta[i] = etiqueta[i];
    for (uint32_t i = 0; ruta && ruta[i] && i + 1 < sizeof o.ruta; i++) o.ruta[i] = ruta[i];
    if (!o.etiqueta[0]) { const char *e = "SOMA"; for (int i = 0; e[i]; i++) o.etiqueta[i] = e[i]; }
    g_op = o;
    g_estado = DISCOS_TRABAJANDO; g_pct = 0; g_msg[0] = 0; g_ver++;
    if (!thread_create("discos", trabajo, 0)) { g_estado = DISCOS_ERROR; *why = "no se pudo crear el hilo de trabajo"; return -1; }
    return 0;
}
int discos_estado(char *msg, uint32_t cap, int *pct)
{
    if (msg && cap) { uint32_t p = 0; msg[0] = 0; sput(msg, cap, &p, g_msg); }
    if (pct) *pct = g_pct;
    return g_estado;
}
void discos_acepta(void) { if (g_estado == DISCOS_HECHO || g_estado == DISCOS_ERROR) { g_estado = DISCOS_LIBRE; g_ver++; } }

/* ---------------------------------------------------------------- orden "discos" */
static const char *palabra(const char *s, char *w, uint32_t cap)
{
    while (*s == ' ') s++;
    uint32_t k = 0; while (*s && *s != ' ' && k + 1 < cap) w[k++] = *s++;
    w[k] = 0;
    while (*s == ' ') s++;
    return s;
}
static int disco_por_nombre(const char *n) { for (int i = 0; i < g_n; i++) if (seq(n, g_d[i].nombre)) return i; return -1; }
static uint32_t num(const char *s) { uint32_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0'); return v; }
static void espera(char *out, uint32_t cap, uint32_t *p)
{
    while (g_estado == DISCOS_TRABAJANDO) thread_sleep(20);
    sput(out, cap, p, g_estado == DISCOS_HECHO ? "hecho: " : "error: "); sput(out, cap, p, g_msg); sput(out, cap, p, "\n");
    discos_acepta();
}
int discos_cmd(const char *args, char *out, uint32_t cap)
{
    uint32_t p = 0; char w[24], dn[16], a3[40];
    if (cap) out[0] = 0;
    const char *r = palabra(args ? args : "", w, sizeof w);
    discos_lista();
    if (!w[0] || seq(w, "lista")) {
        if (!g_n) { sput(out, cap, &p, "no hay discos\n"); return 1; }
        for (int i = 0; i < g_n; i++) {
            const struct disco_info *d = &g_d[i]; char t[24]; discos_tam(t, sizeof t, d->sectores);
            sput(out, cap, &p, d->nombre); sput(out, cap, &p, "  "); sput(out, cap, &p, t); sput(out, cap, &p, "  ("); sput(out, cap, &p, d->driver);
            sput(out, cap, &p, d->tabla.esquema == DC_GPT ? ", GPT" : d->tabla.esquema == DC_MBR ? ", MBR" : ", sin tabla");
            sput(out, cap, &p, d->sistema ? ", disco del sistema" : d->escribible ? ", desbloqueado" : ", protegido"); sput(out, cap, &p, ")\n");
            if (d->tabla.aviso[0]) { sput(out, cap, &p, "   aviso: "); sput(out, cap, &p, d->tabla.aviso); sput(out, cap, &p, "\n"); }
            if (d->tabla.esquema == DC_NADA && d->fs_disco > DC_FS_VACIO) { sput(out, cap, &p, "   todo el disco: "); sput(out, cap, &p, dc_fs_nombre(d->fs_disco)); sput(out, cap, &p, "\n"); }
            for (int k = 0; k < d->tabla.n; k++) {
                const struct dc_part *q = &d->tabla.p[k]; discos_tam(t, sizeof t, q->n);
                sput(out, cap, &p, "   "); sdec(out, cap, &p, (uint64_t)k + 1); sput(out, cap, &p, ". "); sput(out, cap, &p, t); sput(out, cap, &p, "  ");
                sput(out, cap, &p, dc_tipo_nombre(&d->tabla, q)); sput(out, cap, &p, ", "); sput(out, cap, &p, dc_fs_nombre(d->fs[k]));
                if (d->etiqueta[k][0]) { sput(out, cap, &p, " \""); sput(out, cap, &p, d->etiqueta[k]); sput(out, cap, &p, "\""); }
                if (q->nombre[0]) { sput(out, cap, &p, " ["); sput(out, cap, &p, q->nombre); sput(out, cap, &p, "]"); }
                sput(out, cap, &p, "\n");
            }
        }
        return 1;
    }
    r = palabra(r, dn, sizeof dn);
    int i = disco_por_nombre(dn);
    if (i < 0) {
        if (dn[0]) { sput(out, cap, &p, "discos: no hay ningun disco llamado "); sput(out, cap, &p, dn); sput(out, cap, &p, " (discos lista)\n"); }
        sput(out, cap, &p, "uso: discos [lista] | desbloquear <disco> <disco> | formatear <disco> [gpt|mbr] [exfat|fat32] [etiqueta]\n"
                           "      nueva <disco> <MiB|0=todo> [exfat|fat32] [etiqueta] | borrar <disco> <n> | formatear-part <disco> <n> [exfat|fat32] [etiqueta]\n"
                           "      grabar <disco> <imagen (.img/.iso en /usuario o en el almacen)>\n");
        return 1;
    }
    const char *why;
    if (seq(w, "desbloquear")) {
        r = palabra(r, a3, sizeof a3);
        if (discos_desbloquea(i, a3, &why) != 0) { sput(out, cap, &p, "discos: "); sput(out, cap, &p, why); sput(out, cap, &p, "\n"); }
        else { sput(out, cap, &p, "disco "); sput(out, cap, &p, dn); sput(out, cap, &p, " desbloqueado hasta reiniciar: las siguientes ordenes lo pueden BORRAR\n"); }
        return 1;
    }
    int rr = -1;
    if (seq(w, "formatear")) {
        char e[8], f[8]; r = palabra(r, e, sizeof e); r = palabra(r, f, sizeof f);
        rr = discos_op(DISCOS_OP_DISCO, i, -1, seq(e, "mbr") ? DC_MBR : DC_GPT, seq(f, "fat32") ? DC_FS_FAT32 : DC_FS_EXFAT, 0, *r ? r : 0, 0, &why);
    } else if (seq(w, "nueva")) {
        char m[16], f[8]; r = palabra(r, m, sizeof m); r = palabra(r, f, sizeof f);
        rr = discos_op(DISCOS_OP_NUEVA, i, -1, 0, seq(f, "fat32") ? DC_FS_FAT32 : DC_FS_EXFAT, num(m), *r ? r : 0, 0, &why);
    } else if (seq(w, "borrar")) {
        char m[8]; r = palabra(r, m, sizeof m);
        rr = discos_op(DISCOS_OP_BORRA, i, (int)num(m) - 1, 0, 0, 0, 0, 0, &why);
    } else if (seq(w, "formatear-part")) {
        char m[8], f[8]; r = palabra(r, m, sizeof m); r = palabra(r, f, sizeof f);
        rr = discos_op(DISCOS_OP_FORMATEA, i, (int)num(m) - 1, 0, seq(f, "fat32") ? DC_FS_FAT32 : DC_FS_EXFAT, 0, *r ? r : 0, 0, &why);
    } else if (seq(w, "grabar")) {
        rr = discos_op(DISCOS_OP_GRABA, i, -1, 0, 0, 0, 0, r, &why);
    } else { sput(out, cap, &p, "discos: orden desconocida\n"); return 1; }
    if (rr != 0) { sput(out, cap, &p, "discos: "); sput(out, cap, &p, why); sput(out, cap, &p, "\n"); return 1; }
    espera(out, cap, &p);
    return 1;
}
