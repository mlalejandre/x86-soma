/* SOMA_363_ACCESO: ver medios.h. */
#include <stdint.h>
#include <stddef.h>
#include "medios.h"
#include "unidades.h"
#include "exfat.h"
#include "fat.h"
#include "discos_core.h"
#include "mem.h"
#include "mutex.h"
#include "thread.h"
#include "console.h"
#include "rtc.h"
#include "hora.h"
#include "fs.h"
#if defined(__x86_64__)
#include "xhci.h"
#include "usbdisk.h"
#endif

struct medio {
    int on, tipo, dev, unid;                     /* unid: unidad exFAT (UNID_EXT0..) o -1 */
    uint64_t lba, n;
    char nombre[40], fs[16], disp[40];
    struct fat_vol *fat;
};
static struct medio g_m[MEDIOS_MAX];
static int g_n;
static struct kmutex g_mx; static int g_mx_ok;
static volatile uint32_t g_ver = 1;
static volatile int g_pide;

static void lk(void) { if (!g_mx_ok) { kmutex_init(&g_mx, "medios"); g_mx_ok = 1; } kmutex_lock(&g_mx); }
static void ulk(void) { kmutex_unlock(&g_mx); }
static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; ++i; } if (cap) d[i] = 0; }
static int pref(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }

uint32_t medios_version(void) { return g_ver; }
int medios_n(void) { return g_n; }

/* ---------------------------------------------------------------- rutas */
int medios_es_raiz(const char *r) { return r && (r[0] == '/' && r[1] == 'u' && r[2] == 's' && r[3] == 'b' && (r[4] == 0 || (r[4] == '/' && r[5] == 0))); }
/* el medio de una ruta /usb/NOMBRE/... y el resto ("/..." o "/") */
static int de_ruta(const char *r, const char **resto)
{
    if (!r || !pref(r, "/usb/")) return -1;
    for (int i = 0; i < g_n; ++i) {
        if (!g_m[i].on) continue;
        uint32_t n = sl(g_m[i].nombre);
        if (pref(r + 5, g_m[i].nombre) && (r[5 + n] == 0 || r[5 + n] == '/')) { if (resto) *resto = r[5 + n] ? r + 5 + n : "/"; return i; }
    }
    return -1;
}
int medios_lista_raiz(char *out, uint32_t max)
{
    uint32_t p = 0; if (!max) return 0;
    out[0] = 0;
    const char *t = "Archivos: "; while (*t && p + 1 < max) out[p++] = *t++;
    lk();
    int k = 0;
    for (int i = 0; i < g_n; ++i) {
        if (!g_m[i].on || g_m[i].tipo == MEDIO_OTRO) continue;
        if (k++) { if (p + 2 < max) { out[p++] = ','; out[p++] = ' '; } }
        const char *a = "/usb/"; while (*a && p + 1 < max) out[p++] = *a++;
        for (const char *b = g_m[i].nombre; *b && p + 1 < max; ) out[p++] = *b++;
        if (p + 1 < max) out[p++] = '/';
    }
    ulk();
    out[p] = 0;
    return (int)p;
}
int medios_info(int i, struct medio_info *o)
{
    if (i < 0 || i >= g_n) return -1;
    lk();
    struct medio *m = &g_m[i];
    cpy(o->ruta, sizeof o->ruta, "/usb/"); { uint32_t k = sl(o->ruta); cpy(o->ruta + k, sizeof o->ruta - k, m->nombre); }
    cpy(o->etiqueta, sizeof o->etiqueta, m->nombre); cpy(o->fs, sizeof o->fs, m->fs); cpy(o->dispositivo, sizeof o->dispositivo, m->disp);
    o->tipo = m->tipo; o->solo_lectura = m->tipo != MEDIO_EXFAT;
    o->total = m->n * 512u; o->libre = 0;
    ulk();
    if (m->tipo == MEDIO_EXFAT && m->unid >= 0) { uint64_t li = 0, to = 0; if (unid_info(m->unid, &li, &to, 0, 0) == 0) { o->libre = li; o->total = to; } }
    return 0;
}

/* ---------------------------------------------------------------- FAT (solo lectura) */
int medios_fat_es(const char *r) { lk(); const char *x; int i = de_ruta(r, &x); int s = i >= 0 && g_m[i].tipo == MEDIO_FAT; ulk(); return s; }
int medios_fat_tam(const char *r, uint64_t *tam, int *dir)
{
    lk(); const char *x; int i = de_ruta(r, &x);
    if (i < 0 || g_m[i].tipo != MEDIO_FAT || !g_m[i].fat) { ulk(); return -1; }
    int e;
    if (x[0] == '/' && !x[1]) { if (tam) *tam = 0; if (dir) *dir = 1; e = 0; }
    else { struct fat_dirent d; e = fat_stat(g_m[i].fat, x, &d); if (!e) { if (tam) *tam = d.size; if (dir) *dir = d.is_dir; } }
    ulk(); return e ? -1 : 0;
}
int medios_fat_leer(const char *r, uint64_t off, void *buf, uint32_t n)
{
    lk(); const char *x; int i = de_ruta(r, &x);
    if (i < 0 || g_m[i].tipo != MEDIO_FAT || !g_m[i].fat || off > 0xFFFFFFFFu) { ulk(); return -1; }
    uint32_t hecho = 0; int e = 0;
    while (hecho < n) { e = fat_read(g_m[i].fat, x, (uint32_t)off + hecho, (uint8_t *)buf + hecho, n - hecho); if (e <= 0) break; hecho += (uint32_t)e; }
    ulk();
    return e < 0 && !hecho ? -1 : (int)hecho;
}
int medios_fat_listar(const char *r, struct fat_dirent *out, int max, int *total)
{
    lk(); const char *x; int i = de_ruta(r, &x);
    if (i < 0 || g_m[i].tipo != MEDIO_FAT || !g_m[i].fat) { ulk(); return -1; }
    int e = fat_readdir(g_m[i].fat, x, out, max, total);
    ulk(); return e;
}

/* ---------------------------------------------------------------- el VFS (fs.h) */
static int op_es(const char *r) { return medios_es_raiz(r) || medios_fat_es(r); }
static int op_tam(const char *r, uint64_t *t, int *d) { if (medios_es_raiz(r)) { if (t) *t = 0; if (d) *d = 1; return 0; } return medios_fat_tam(r, t, d); }
static int op_leer(const char *r, uint64_t off, void *b, uint32_t n) { return medios_fat_leer(r, off, b, n); }
static void lp(char *o, uint32_t max, uint32_t *p, const char *s) { while (o && s && *s && *p + 1 < max) o[(*p)++] = *s++; if (o && max) o[*p] = 0; }
static int op_lista(int todo, const char *dir, char *out, uint32_t max, int consola)
{
    uint32_t p = 0;
    if (medios_es_raiz(dir)) {
        if (out && max) medios_lista_raiz(out, max), p = sl(out);
        if (consola) {
            kprint("\nMEDIOS USB EXTERNOS (/usb):\n--------------------------------------------------\n");
            int k = 0;
            for (int i = 0; i < medios_n(); ++i) {
                struct medio_info mi; if (medios_info(i, &mi)) continue;
                kprint("  "); kprint(mi.ruta); kprint("/  "); kprint(mi.fs); kprint(mi.tipo == MEDIO_EXFAT ? "  lectura y escritura" : mi.tipo == MEDIO_FAT ? "  solo lectura" : "  no se puede abrir");
                if (mi.dispositivo[0]) { kprint("  ("); kprint(mi.dispositivo); kprint(")"); }
                kprint("\n"); ++k;
            }
            if (!k) kprint("  (ninguno: enchufa un pendrive o un disco USB)\n");
            kprint("--------------------------------------------------\n\n");
        }
        return out ? (int)p : 0;
    }
    struct fat_dirent *d = (struct fat_dirent *)kmalloc(64 * sizeof *d);
    int total = 0, n = d ? medios_fat_listar(dir, d, 64, &total) : -1;
    if (out && max) { out[0] = 0; lp(out, max, &p, "Archivos: "); }
    if (consola) { kprint("\nARCHIVOS EN "); kprint(dir); kprint(" (solo lectura):\n--------------------------------------------------\n"); }
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (d[i].name[0] == '.' && !todo) continue;
        if (out && max) {
            if (k) lp(out, max, &p, ", ");
            lp(out, max, &p, dir); { uint32_t dl = sl(dir); if (dl && dir[dl - 1] != '/') lp(out, max, &p, "/"); }
            lp(out, max, &p, d[i].name); if (d[i].is_dir) lp(out, max, &p, "/");
        }
        if (consola) { kprint("  "); kprint(d[i].name); kprint(d[i].is_dir ? "/" : ""); if (!d[i].is_dir) { kprint("  "); kprint_dec(d[i].size); kprint(" bytes"); } kprint("\n"); }
        ++k;
    }
    if (consola) { if (n < 0) kprint("  no se puede leer\n"); kprint("--------------------------------------------------\n"); kprint_dec((uint32_t)k); kprint(" elementos\n\n"); }
    if (d) kfree(d);
    return out ? (int)p : k;
}
static const struct vfs_medios_ops g_ops = { op_es, op_tam, op_leer, op_lista };

#if defined(__x86_64__)
struct mpart { int dev; uint64_t lba, n; };
static struct mpart g_mp[MEDIOS_MAX];
/* ---------------------------------------------------------------- E/S de los discos externos */
static int m_raw(int dev, int wr, uint64_t lba, uint32_t n, void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    while (n) {
        uint32_t k = n > 128 ? 128 : n; int r = -1;
        for (int t = 0; t < 200; t++) {
            r = wr ? xhci_msc_write_n(dev, lba, k, p) : xhci_msc_read_n(dev, lba, k, p);
            if (r == 0) break;
            if (!xhci_rescanning() && t >= 3) return -1;
            thread_sleep(10);
        }
        if (r) return -1;
        lba += k; p += (uint64_t)k * 512u; n -= k;
    }
    if (wr) { for (int t = 0; t < 50; t++) { if (xhci_msc_sync_n(dev) == 0 || !xhci_rescanning()) break; thread_sleep(10); } }   /* un medio que se puede quitar: a disco ya */
    return 0;
}
static int dev_rd(void *c, uint64_t lba, uint32_t n, void *b) { return m_raw((int)(intptr_t)c, 0, lba, n, b); }
static int mp_rd(void *c, uint64_t lba, uint32_t n, void *b) { const struct mpart *p = (const struct mpart *)c; if (!n || lba >= p->n || n > p->n - lba) return -1; return m_raw(p->dev, 0, p->lba + lba, n, b); }
static int mp_wr(void *c, uint64_t lba, uint32_t n, const void *b) { const struct mpart *p = (const struct mpart *)c; if (!n || lba >= p->n || n > p->n - lba) return -1; return m_raw(p->dev, 1, p->lba + lba, n, (void *)b); }
static void *m_alloc(uint32_t n) { return kmalloc(n); }
static void m_free(void *p) { kfree(p); }
static uint32_t m_now(void)
{
    struct rtc_time t; hora_local(&t);
    uint32_t y = t.year >= 1980 ? (uint32_t)t.year - 1980 : 0;
    return (y << 25) | ((uint32_t)t.month << 21) | ((uint32_t)t.day << 16) | ((uint32_t)t.hour << 11) | ((uint32_t)t.min << 5) | ((uint32_t)t.sec / 2);
}

static void desmonta_todo(void)
{
    for (int i = 0; i < g_n; ++i) {
        if (g_m[i].unid >= 0) unid_desmontar(g_m[i].unid);
        if (g_m[i].fat) { kfree(g_m[i].fat); g_m[i].fat = 0; }
        g_m[i].on = 0;
    }
    g_n = 0;
}
static void nombre_unico(char *n, uint32_t cap, const char *base, int disco, int part)
{
    char b[40]; uint32_t k = 0;
    for (const char *s = base; s && *s && k + 1 < sizeof b; ++s) { char c = *s; if (c == '/' || c == '\\' || (uint8_t)c < 32 || c == ':') c = '_'; b[k++] = c; }
    while (k && b[k - 1] == ' ') --k;
    b[k] = 0;
    if (!b[0]) { cpy(b, sizeof b, "USB"); k = 3; b[k++] = (char)('1' + disco % 9); if (part > 0) { b[k++] = '-'; b[k++] = (char)('0' + part % 10); } b[k] = 0; }
    cpy(n, cap, b);
    for (int v = 2; v < 10; ++v) {
        int rep = 0; for (int i = 0; i < g_n; ++i) { const char *a = g_m[i].nombre, *c = n; while (*a && *a == *c) { ++a; ++c; } if (!*a && !*c) rep = 1; }
        if (!rep) return;
        cpy(n, cap, b); uint32_t q = sl(n); if (q + 3 < cap) { n[q++] = '-'; n[q++] = (char)('0' + v); n[q] = 0; }
    }
}
static void monta_particion(int dev, int disco, int part, uint64_t lba, uint64_t n, int fs, const char *etiqueta, const char *disp)
{
    if (g_n >= MEDIOS_MAX) return;
    struct medio *m = &g_m[g_n]; m->on = 0; m->unid = -1; m->fat = 0; m->dev = dev; m->lba = lba; m->n = n;
    cpy(m->disp, sizeof m->disp, disp);
    cpy(m->fs, sizeof m->fs, dc_fs_nombre(fs));
    nombre_unico(m->nombre, sizeof m->nombre, etiqueta, disco, part);
    g_mp[g_n].dev = dev; g_mp[g_n].lba = lba; g_mp[g_n].n = n;
    if (fs == DC_FS_EXFAT) {
        int u = -1; for (int k = UNID_EXT0; k < UNID_N; ++k) if (!unid_activa(k)) { int usada = 0; for (int j = 0; j < g_n; ++j) if (g_m[j].unid == k) usada = 1; if (!usada) { u = k; break; } }
        if (u < 0) return;
        struct exfat_io io = { mp_rd, mp_wr, &g_mp[g_n], m_alloc, m_free, m_now };
        if (unid_montar_ext(u, &io, n, m->nombre) != 0) return;
        m->tipo = MEDIO_EXFAT; m->unid = u; m->on = 1;
    } else if (fs == DC_FS_FAT12 || fs == DC_FS_FAT16 || fs == DC_FS_FAT32) {
        m->fat = (struct fat_vol *)kmalloc(sizeof *m->fat); if (!m->fat) return;
        struct fat_io io = { mp_rd, &g_mp[g_n], n };
        if (fat_mount(m->fat, &io) != 0) { kfree(m->fat); m->fat = 0; return; }
        m->tipo = MEDIO_FAT; m->on = 1;
    } else { m->tipo = MEDIO_OTRO; m->on = 1; }                        /* se ve en la lista (NTFS, ext...) pero no se abre */
    ++g_n;
    kprint("MEDIOS: /usb/"); kprint(m->nombre); kprint(" ("); kprint(m->fs); kprint(m->tipo == MEDIO_EXFAT ? ", lectura y escritura" : m->tipo == MEDIO_FAT ? ", solo lectura" : ", no se puede abrir"); kprint(")\n");
}
static void monta_todo(void)
{
    static uint8_t buf[512 * 34] __attribute__((aligned(16)));
    static struct dc_tabla t;
    int nd = xhci_msc_count(), prim = usbdisk_disco_sistema(), disco = 0;
    const struct usbdisk_info *ui = usbdisk_info();
    uint32_t firma = ui && ui->active ? ui->disk_sig : 0;
    for (int d = 0; d < nd; ++d) {
        if (d == prim) continue;                                            /* el pendrive del sistema: nunca */
        const struct xhci_msc_info *mi = xhci_msc_get_n(d); if (!mi || !mi->sectors) continue;
        if (firma && dev_rd((void *)(intptr_t)d, 0, 1, buf) == 0 && buf[510] == 0x55 && buf[511] == 0xAA &&
            ((uint32_t)buf[440] | (uint32_t)buf[441] << 8 | (uint32_t)buf[442] << 16 | (uint32_t)buf[443] << 24) == firma) continue;   /* ni aunque haya cambiado de numero */
        char disp[40]; cpy(disp, sizeof disp, mi->vendor); { uint32_t k = sl(disp); if (k + 2 < sizeof disp) { disp[k++] = ' '; disp[k] = 0; } cpy(disp + k, sizeof disp - k, mi->product); }
        ++disco;
        if (dc_lee(dev_rd, (void *)(intptr_t)d, mi->sectors, &t, buf) != 0) continue;
        char et[40];
        if (t.esquema == DC_NADA) {
            int fs = dc_fs(dev_rd, (void *)(intptr_t)d, 0, mi->sectors, et, sizeof et, buf);
            if (fs != DC_FS_VACIO && fs != DC_FS_DESCONOCIDO) monta_particion(d, disco, 0, 0, mi->sectors, fs, et, disp);
            continue;
        }
        for (int p = 0; p < t.n; ++p) {
            int fs = dc_fs(dev_rd, (void *)(intptr_t)d, t.p[p].lba, t.p[p].n, et, sizeof et, buf);
            if (fs == DC_FS_VACIO || fs == DC_FS_DESCONOCIDO || fs == DC_FS_BIGFS) continue;
            if (!et[0] && t.p[p].nombre[0]) cpy(et, sizeof et, t.p[p].nombre);
            monta_particion(d, disco, t.n > 1 ? p + 1 : 0, t.p[p].lba, t.p[p].n, fs, et, disp);
        }
    }
}
static uint32_t g_gen_visto = 0xFFFFFFFFu;
static int g_vivo;
static void hilo(void *a)
{
    (void)a;
    for (;;) {
        uint32_t gen = xhci_msc_generation();
        int cuantos = xhci_msc_count();
        if ((gen != g_gen_visto || g_pide) && !xhci_rescanning()) {
            g_pide = 0;
            thread_sleep(300);                                              /* que usbdisk elija antes el disco del sistema */
            if (xhci_rescanning()) continue;
            g_gen_visto = xhci_msc_generation();
            lk(); desmonta_todo(); if (cuantos > 0) monta_todo(); ulk();
            ++g_ver;
        }
        thread_sleep(1000);
    }
}
void medios_inicia(void)
{
    vfs_medios = &g_ops;
    if (g_vivo) return;
    g_vivo = 1;
    if (!thread_create("medios", hilo, 0)) g_vivo = 0;
}
void medios_refresca(void) { g_pide = 1; }
#else
void medios_inicia(void) { vfs_medios = &g_ops; }
void medios_refresca(void) { }
#endif
