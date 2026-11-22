/* SOMA_162_T480S: el disco del sistema en el pendrive.
 * En el portatil no hay disco virtio: sin esto, SOMAFS se formatea en RAM en cada arranque y nada se guarda. Aqui se busca en el
 * disco USB (el pendrive del que arranca SOMA) una particion MBR preparada por ejecutar.py:
 *   - con nuestra marca en su primer sector ("SOMADATA-DISCO-1" + identificadores), o
 *   - recien creada: un FAT32 con la etiqueta de volumen "SOMADATA" (diskutil/mkfs.vfat). Se reclama: se escribe la marca y se
 *     ponen a cero los primeros 8 MiB, que es donde SOMA guarda su buzon, VFS, diario, checkpoints y fuentes.
 * Solo se escribe DENTRO de esa particion: cada peticion se comprueba contra sus limites aqui, antes de llegar al USB. El disco
 * de SOMA empieza 8 sectores despues de la marca (4 KiB: alineado para la memoria flash) y se limita a 8 GiB.
 * Si el controlador USB se reinicia (enchufar o quitar algo), antes de volver a escribir se relee la marca: si el disco ya no es
 * el mismo, el disco del sistema pasa a solo lectura (nunca se escribe en otro pendrive). Interruptor: soma.usbdisk=off. */
#include <stdint.h>
#include "xhci.h"
#include "sysdisk.h"
#include "console.h"
#include "mutex.h"
#include "thread.h"
#include "bootfb.h"
#include "usbdisk.h"
#include "usbdisk_core.h"
#include "fat.h"
#include "idt.h"           /* SOMA_238: timer_get_uptime_ms */
#include "unidades.h"      /* SOMA_267_DISCOS: MODELOS y USUARIO */
#include "exfat.h"
#include "mem.h"
#include "rtc.h"


static struct kmutex g_lock __attribute__((aligned(16)));
static struct usbdisk_info g;
static uint32_t g_gen;
static uint8_t g_sec[512] __attribute__((aligned(16)));
static uint8_t g_zero[64 * 1024] __attribute__((aligned(16)));
static uint64_t g_reads, g_writes, g_errors, g_retries;

/* Lectura/escritura en bruto del USB con reintentos (ocupado o reexplorando). */
static int raw(int wr, uint64_t lba, uint32_t n, void *buf)
{
    for (int t = 0; t < 400; t++) {
        int r = wr ? xhci_msc_write_nosync(lba, n, buf) : xhci_msc_read(lba, n, buf);
        if (r == 0) return 0;
        g_retries++;
        if (!xhci_rescanning() && t >= 3) return r;
        thread_sleep(25);
    }
    return -1;
}

static void mark_build(uint8_t *s) { ud_mark_build(s, g.part_lba, g.part_sectors, g.disk_sig); }
static int mark_ok(const uint8_t *s) { return ud_mark_ok(s, g.part_lba, g.part_sectors, g.disk_sig); }

/* Tras un reinicio del controlador: sigue siendo el mismo pendrive? (llamar con g_lock) */
static int still_ours(void)
{
    if (!g.active) return 0;
    if (xhci_msc_generation() == g_gen) return !g.lost;
    for (int t = 0; t < 400 && xhci_rescanning(); t++) thread_sleep(25);          /* sin pendrive: no esperar (cada E/S tardaria) */
    if (!xhci_msc_ready() || raw(0, g.part_lba, 1, g_sec) != 0 || !mark_ok(g_sec)) {
        if (!g.lost) kprint("DISCO USB: el pendrive del sistema ha cambiado o no responde: no se escribe nada hasta que vuelva\n");
        g.readonly = 1; g.lost = 1;
        if (xhci_msc_ready()) g_gen = xhci_msc_generation();      /* otro pendrive: no se vuelve a mirar hasta otro cambio */
        return 0;
    }
    g_gen = xhci_msc_generation();
    if (g.lost) { g.lost = 0; g.readonly = 0; kprint("DISCO USB: el pendrive del sistema ha vuelto: lectura y escritura de nuevo\n"); }
    return 1;
}

static int d_io(int wr, uint64_t lba, void *buf, uint32_t n)
{
    if (!n) return 0;
    if (lba >= g.sectors || n > g.sectors - lba) { g_errors++; return -1; }      /* fuera del disco de SOMA: nunca llega al USB */
    kmutex_lock(&g_lock);
    int r = -1;
    if (still_ours() && !(wr && g.readonly)) {
        uint8_t *p = (uint8_t *)buf; r = 0;
        while (n && r == 0) {
            uint32_t k = n > 128 ? 128 : n;
            uint64_t abs;
            if (ud_map(g.part_lba, g.part_sectors, g.sectors, lba, k, &abs) != 0) { r = -1; break; }   /* segunda comprobacion */
            r = raw(wr, abs, k, p);
            lba += k; p += (uint64_t)k * 512; n -= k;
        }
    }
    if (r) g_errors++; else if (wr) g_writes++; else g_reads++;
    if (wr) g.dirty = 1;
    kmutex_unlock(&g_lock);
    return r ? -1 : 0;
}
static int d_read(uint64_t lba, void *buf, uint32_t n) { return d_io(0, lba, buf, n); }
static int d_write(uint64_t lba, const void *buf, uint32_t n) { return d_io(1, lba, (void *)buf, n); }
static int d_flush(void)
{
    kmutex_lock(&g_lock);
    int r = 0;
    if (g.dirty && still_ours()) {
        for (int t = 0; t < 400; t++) { r = xhci_msc_sync(); if (r == 0 || !xhci_rescanning()) break; thread_sleep(25); }
        if (r == 0) { g.dirty = 0; g.syncs++; }
    }
    kmutex_unlock(&g_lock);
    return r ? -1 : 0;
}
static struct sysdisk_ops g_ops = { d_read, d_write, d_flush, 0, "pendrive USB (SOMADATA)" };

/* Hilo: vacia la cache del pendrive cada 5 s si hubo escrituras (por si el sistema de archivos no pidio barrera). */
static void syncer(void *a)
{
    (void)a;
    int n = 0;
    for (;;) {
        thread_sleep(5000);
        if (g.dirty && !g.readonly) d_flush();
        /* el pendrive del sistema falta: reexplorar cada 10 s (la exploracion automatica pudo llegar con el pendrive a medio
         * conectar y no verlo). Si vuelve, la siguiente E/S relee la marca y recupera la escritura. */
        if (g.lost && !xhci_msc_ready() && (++n & 1) == 0 && !xhci_rescanning()) xhci_rescan();
        if (g.lost && xhci_msc_ready()) { kmutex_lock(&g_lock); (void)still_ours(); kmutex_unlock(&g_lock); }
    }
}

/* SOMA_228_ARRANQUE: la imagen de GitHub trae SOMADATA de 4 GiB para caber en un pendrive de 8 GB. En el primer arranque (particion
 * aun sin reclamar, nada que perder) se estira hasta el final del pendrive si es la ultima particion (usbdisk_core.h: ud_plan_grow).
 * Se escribe el MBR, se vacia la cache y se relee: si la relectura no cuadra, se sigue con el tamano viejo. soma.ampliar=off lo evita. */
static void grow_fresh(int idx, uint64_t disk_sectors)
{
    static uint8_t m[512] __attribute__((aligned(16)));
    if (bootinfo_disabled("ampliar")) { g.grow_why = "desactivado (soma.ampliar=off)"; return; }
    if (raw(0, 0, 1, m) != 0) { g.grow_why = "no se pudo releer el MBR"; return; }
    uint32_t old_n = ud_le32(m + 0x1BE + 16 * idx + 12);
    if (ud_le32(m + 0x1BE + 16 * idx + 8) != g.part_lba || old_n != g.part_sectors) { g.grow_why = "el MBR cambio"; return; }
    uint32_t n = ud_plan_grow(m, idx, disk_sectors);
    if (!n) { g.grow_why = "ya ocupa el pendrive (o no es la ultima particion)"; return; }
    ud_apply_grow(m, idx, n);
    kprint("DISCO USB: ampliando SOMADATA de "); kprint_dec(old_n / 2048); kprint(" a "); kprint_dec(n / 2048); kprint(" MiB...\n");
    if (raw(1, 0, 1, m) != 0 || xhci_msc_sync() != 0) kprint("DISCO USB: error al escribir el MBR (se comprueba)\n");
    struct ud_part pt[4]; uint32_t sig = 0;
    int np = (raw(0, 0, 1, m) == 0) ? ud_parse_mbr(m, disk_sectors, pt, &sig) : -1;
    for (int i = 0; i < np; i++)
        if (pt[i].index == idx && pt[i].lba == g.part_lba) {
            if (pt[i].n == n && sig == g.disk_sig) { g.part_sectors = n; g.grown_from_mb = old_n / 2048; g.grown_to_mb = n / 2048; return; }
            g.grow_why = "el MBR no se leyo como se escribio"; g.part_sectors = pt[i].n; return;
        }
    g.grow_why = "no se pudo comprobar el MBR";
}

/* SOMA_238_GUARDADO: sin particion SOMADATA, el disco puede ser un fichero SOMADATA.IMG (contiguo, >= 64 MiB) en la particion de
 * arranque FAT. Asi un pendrive preparado sin SOMADATA (o con una version antigua de ejecutar.py) guarda sin borrarlo entero.
 * Devuelve 0 y deja part_lba/part_sectors con su sitio en el pendrive, o -1 con g.why. */
static int bf_read(void *c, uint64_t lba, uint32_t n, void *buf);
static void usbdisk_unidades(void);
static int find_file(void)
{
    static struct fat_vol v;
    struct fat_io io = { bf_read, 0, xhci_msc_get()->sectors };
    if (fat_mount(&v, &io) != 0) return -1;
    uint64_t lba = 0, n = 0;
    int e = fat_contiguous(&v, UD_FILE_NAME, &lba, &n);
    if (e == FAT_E_NOENT) return -1;
    if (e == FAT_E_UNSUP) { g.why = "SOMADATA.IMG esta fragmentado (borralo y crealo de nuevo con el pendrive vacio)"; return -2; }
    if (e != 0) { g.why = "SOMADATA.IMG no se pudo leer"; return -2; }
    if (n < UD_FILE_MIN) { g.why = "SOMADATA.IMG es demasiado pequeno (minimo 64 MB)"; return -2; }
    g.part_lba = lba; g.part_sectors = n; g.in_file = 1; g.part_index = v.part_index;
    return 0;
}

int usbdisk_attach(void)
{
    static int inited;
    if (!inited) { kmutex_init(&g_lock, "usbdisk"); inited = 1; }
    if (g.active) return 0;
    g.why = "";
    if (bootinfo_disabled("usbdisk")) { g.why = "desactivado (soma.usbdisk=off o modo seguro)"; return -1; }
    if (sysdisk_capacity()) { g.why = "ya hay disco virtio"; return -1; }
    xhci_autostart_early();                                               /* teclado, raton y disco USB, ahora y no despues */
    /* SOMA_238_GUARDADO: algunos pendrives tardan en responder tras el reinicio del controlador. Antes, si no estaban listos en ese
     * instante, SOMA seguia en memoria todo el arranque. Se espera hasta 8 s, reexplorando el USB cada 2 s. */
    {
        uint64_t t0 = timer_get_uptime_ms(), last = t0;
        while (!xhci_msc_ready() && timer_get_uptime_ms() - t0 < 8000) {
            if (!xhci_rescanning() && timer_get_uptime_ms() - last >= 2000) { last = timer_get_uptime_ms(); (void)xhci_rescan(); }
            thread_sleep(50);
        }
        g.wait_ms = (uint32_t)(timer_get_uptime_ms() - t0);
        if (g.wait_ms >= 100) { kprint("DISCO USB: el pendrive tardo "); kprint_dec(g.wait_ms); kprint(" ms en responder\n"); }
    }
    if (!xhci_msc_ready()) { g.why = "no hay disco USB (almacenamiento masivo)"; return -1; }
    const struct xhci_msc_info *mi = xhci_msc_get();
    g_gen = xhci_msc_generation();
    if (raw(0, 0, 1, g_sec) != 0) { g.why = "no se pudo leer el sector 0 del pendrive"; return -1; }
    struct ud_part pt[4];
    int np = ud_parse_mbr(g_sec, mi->sectors, pt, &g.disk_sig);
    if (np < 0) { g.why = np == -2 ? "el pendrive usa GPT (ejecutar.py lo prepara con MBR)" : "el pendrive no tiene tabla MBR"; return -1; }
    int found = -1, fresh = 0, loose = -1;
    for (int i = 0; i < np && found < 0; i++) {
        if (raw(0, pt[i].lba, 1, g_sec) != 0) continue;
        g.part_lba = pt[i].lba; g.part_sectors = pt[i].n;
        if (mark_ok(g_sec)) { found = pt[i].index; break; }
        if (ud_is_fat_somadata(g_sec)) { found = pt[i].index; fresh = 1; break; }
        if (loose < 0 && ud_mark_loose(g_sec, pt[i].lba)) loose = i;
    }
    if (found < 0 && loose >= 0) {                                         /* SOMA_238: nuestra, aunque la marca no cuadre */
        g.part_lba = pt[loose].lba; g.part_sectors = pt[loose].n; found = pt[loose].index; g.remarked = 1;
        kprint("DISCO USB: la marca de SOMADATA no coincidia con la tabla de particiones (tamano o firma): se actualiza\n");
    }
    if (found < 0) {                                                       /* SOMA_238: el fichero SOMADATA.IMG */
        g.part_lba = g.part_sectors = 0;
        int fr = find_file();
        if (fr == -1) {
            g.why = "el pendrive no tiene la particion SOMADATA ni el fichero SOMADATA.IMG (ejecutar.py -t t480s-usb lo crea)";
            return -1;
        }
        if (fr < 0) { g.part_lba = g.part_sectors = 0; g.in_file = 0; return -1; }
        if (raw(0, g.part_lba, 1, g_sec) != 0) { g.why = "no se pudo leer SOMADATA.IMG"; return -1; }
        if (!mark_ok(g_sec)) {
            if (ud_mark_loose(g_sec, ud_le32(g_sec + 16))) g.remarked = 1;   /* nuestro aunque se haya movido (copiado) */
            else fresh = 1;                                                /* fichero nuevo: se reclama (su contenido no sirve) */
        }
    } else g.part_index = found + 1;
    g.grow_why = "";
    if (fresh && !g.in_file) grow_fresh(found, mi->sectors);              /* SOMA_228_ARRANQUE: SOMADATA hasta el final del pendrive */
    g.sectors = ud_data_sectors(g.part_sectors);
    if (fresh) {
        kprint(g.in_file ? "DISCO USB: fichero SOMADATA.IMG nuevo: preparandolo (8 MiB)...\n" : "DISCO USB: particion SOMADATA nueva: preparandola (8 MiB)...\n");
        for (uint32_t s = 0; s < UD_ZERO_SECT; s += 128) {
            uint32_t k = UD_ZERO_SECT - s < 128 ? UD_ZERO_SECT - s : 128;
            if (raw(1, g.part_lba + UD_DATA_OFF + s, k, g_zero) != 0) { g.why = "error al preparar la particion"; return -1; }
        }
        g.claimed_now = 1;
    }
    if (fresh || g.remarked) {
        mark_build(g_sec);
        if (raw(1, g.part_lba, 1, g_sec) != 0 || xhci_msc_sync() != 0) { g.why = "error al escribir la marca"; return -1; }
        if (raw(0, g.part_lba, 1, g_sec) != 0 || !mark_ok(g_sec)) { g.why = "la marca no se leyo bien despues de escribirla"; return -1; }
    }
    g_ops.sectors = g.sectors;
    if (sysdisk_set_device(&g_ops) != 0) { g.why = "no se pudo poner como disco del sistema"; return -1; }
    g.active = 1; g.why = "activo";
    thread_create("usbsync", syncer, 0);
    usbdisk_unidades();                                                    /* SOMA_267_DISCOS */
    kprint("DISCO USB: disco del sistema en ");
    if (g.in_file) kprint("el fichero SOMADATA.IMG");
    else { kprint("la particion "); kprint_dec((uint32_t)g.part_index); }
    kprint(" del pendrive ("); kprint(mi->vendor);
    kprint(" "); kprint(mi->product); kprint("): "); kprint_dec((uint32_t)(g.sectors / 2048)); kprint(" MiB");
    kprint(g.claimed_now ? " (recien preparado)\n" : "\n");
    return 0;
}

const struct usbdisk_info *usbdisk_info(void) { return &g; }
void usbdisk_stats(uint64_t *r, uint64_t *w, uint64_t *e, uint64_t *rt) { *r = g_reads; *w = g_writes; *e = g_errors; *rt = g_retries; }
int usbdisk_flush(void) { return g.active ? d_flush() : -1; }

/* ------------------------------------------------------------------ SOMA_228_ARRANQUE: fichero en la particion de arranque */
static int bf_read(void *c, uint64_t lba, uint32_t n, void *buf) { (void)c; return raw(0, lba, n, buf); }
static int bf_write(void *c, uint64_t lba, uint32_t n, const void *buf)
{
    (void)c;
    if (g.part_sectors && lba + n > g.part_lba && lba < g.part_lba + g.part_sectors) return -1;   /* nunca dentro de SOMADATA */
    return raw(1, lba, n, (void *)buf);
}
int usbdisk_bootfile_write(const char *name, const char *data, uint32_t len, const char **why)
{
    static struct fat_vol v;
    static const char *dummy; if (!why) why = &dummy;
    if (!g.active && !xhci_msc_ready()) { *why = "SOMA no arranco desde un pendrive"; return -1; }   /* SOMA_238: tambien sin SOMADATA */
    kmutex_lock(&g_lock);
    int r = -1;
    if (g.active && !still_ours()) *why = "el pendrive del sistema no responde";
    else {
        struct fat_io io = { bf_read, 0, xhci_msc_get()->sectors };
        int e = fat_mount(&v, &io);
        if (e != 0) *why = "no se encontro la particion de arranque FAT";
        else if (!g.in_file && g.active && v.part_index == g.part_index) *why = "la primera particion FAT es SOMADATA";
        else {
            struct fat_dirent d;
            e = fat_stat(&v, name, &d);
            if (e == FAT_E_NOENT) *why = "la imagen del pendrive no trae ese archivo (graba de nuevo la imagen actual del repositorio)";
            else if (e != 0) *why = fat_err_str(e);
            else if (d.size < 4096) *why = "el archivo es demasiado pequeno";
            else {
                if (len > d.size) len = d.size;
                e = fat_overwrite(&v, name, data, len, '\n', bf_write);
                if (e == 0 && xhci_msc_sync() == 0) { r = 0; *why = ""; } else *why = e ? fat_err_str(e) : "no se pudo vaciar la cache del pendrive";
            }
        }
    }
    kmutex_unlock(&g_lock);
    return r;
}

/* SOMA_241_VOZ: lee un trozo de un fichero de la particion FAT de arranque del pendrive (la "SOMA"), por ejemplo el modelo de voz
 * MODELOS/ggml-base-q5_1.bin que el usuario copia desde su ordenador. Con buf=0 solo da el tamano. Devuelve los bytes leidos o <0. */
/* SOMA_267_DISCOS: "MODELOS/x" -> /modelos/voz/x de la unidad MODELOS (pendrive de cuatro particiones); 1 si la ruta es de ahi */
static int modelos_unidad(const char *name, char *ruta, uint32_t cap)
{
    if (!unid_activa(UNID_MODELOS)) return 0;
    static const char *const pre = "MODELOS";
    for (int i = 0; i < 7; i++) { char c = name[i]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); if (c != pre[i]) return 0; }
    if (name[7] != '/' && name[7] != 0) return 0;
    uint32_t q = 0; const char *b = "/modelos/voz";
    while (*b && q + 1 < cap) ruta[q++] = *b++;
    for (const char *c = name + 7; *c && q + 1 < cap; c++) ruta[q++] = *c;
    ruta[q] = 0;
    return 1;
}
int usbdisk_bootfile_read(const char *name, uint32_t off, void *buf, uint32_t len, uint32_t *size_out, const char **why)
{
    static struct fat_vol v;
    static const char *dummy; if (!why) why = &dummy;
    {   char ru[320];                                                      /* SOMA_267_DISCOS */
        if (modelos_unidad(name, ru, sizeof ru)) {
            uint64_t t = 0; int dir = 0, e = unid_tam(ru, &t, &dir);
            if (e) { *why = e == EXFAT_E_NOENT ? "no esta en la unidad MODELOS (carpeta voz)" : exfat_err_str(e); return -1; }
            if (dir) { *why = "es una carpeta"; return -1; }
            if (size_out) *size_out = t > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)t;
            if (!buf) { *why = ""; return 0; }
            int r = unid_leer(ru, off, buf, len);
            *why = r < 0 ? exfat_err_str(r) : "";
            return r;
        } }
    if (!g.active && !xhci_msc_ready()) { *why = "SOMA no arranco desde un pendrive"; return -1; }
    kmutex_lock(&g_lock);
    int r = -1;
    if (g.active && !still_ours()) *why = "el pendrive del sistema no responde";
    else {
        struct fat_io io = { bf_read, 0, xhci_msc_get()->sectors };
        int e = fat_mount(&v, &io);
        if (e != 0) *why = "no se encontro la particion de arranque FAT";
        else {
            struct fat_dirent d;
            e = fat_stat(&v, name, &d);
            if (e != 0) *why = e == FAT_E_NOENT ? "no esta en el pendrive" : fat_err_str(e);
            else if (d.is_dir) *why = "es una carpeta";
            else {
                if (size_out) *size_out = d.size;
                if (!buf) r = 0;
                else { r = fat_read(&v, name, off, buf, len); if (r < 0) *why = fat_err_str(r); else *why = ""; }
            }
        }
    }
    kmutex_unlock(&g_lock);
    return r;
}
/* SOMA_241_VOZ: lista una carpeta de la particion de arranque (para buscar el modelo). Devuelve las entradas escritas o <0. */
int usbdisk_bootdir(const char *path, struct fat_dirent *out, int max, const char **why)
{
    static struct fat_vol v;
    static const char *dummy; if (!why) why = &dummy;
    {   char ru[320];                                                      /* SOMA_267_DISCOS: la carpeta voz de MODELOS */
        if (modelos_unidad(path, ru, sizeof ru)) {
            struct exfat_dirent *d = (struct exfat_dirent *)kmalloc(48 * sizeof *d);
            if (!d) { *why = "sin memoria"; return -1; }
            int total = 0, n = unid_listar(ru, d, 48, &total), k = 0;
            if (n < 0) { kfree(d); *why = n == EXFAT_E_NOENT ? "la unidad MODELOS no tiene la carpeta voz" : exfat_err_str(n); return -1; }
            for (int i = 0; i < n && k < max; i++) {
                uint32_t L = 0; while (d[i].name[L]) L++;
                if (L >= FAT_NAME_MAX || d[i].size > 0xFFFFFFFFu) continue;
                for (uint32_t c = 0; c <= L; c++) out[k].name[c] = d[i].name[c];
                out[k].size = (uint32_t)d[i].size; out[k].cluster = d[i].first_cluster; out[k].date = (uint16_t)(d[i].mtime >> 16);
                out[k].time = (uint16_t)d[i].mtime; out[k].is_dir = d[i].is_dir; out[k].attr = (uint8_t)d[i].attr;
                k++;
            }
            kfree(d);
            *why = "";
            return k;
        } }
    if (!g.active && !xhci_msc_ready()) { *why = "SOMA no arranco desde un pendrive"; return -1; }
    kmutex_lock(&g_lock);
    int r = -1, total = 0;
    if (g.active && !still_ours()) *why = "el pendrive del sistema no responde";
    else {
        struct fat_io io = { bf_read, 0, xhci_msc_get()->sectors };
        int e = fat_mount(&v, &io);
        if (e != 0) *why = "no se encontro la particion de arranque FAT";
        else { r = fat_readdir(&v, path, out, max, &total); if (r < 0) *why = r == FAT_E_NOENT ? "no existe la carpeta" : fat_err_str(r); }
    }
    kmutex_unlock(&g_lock);
    return r;
}

/* ------------------------------------------------------------------ SOMA_267_DISCOS: las unidades MODELOS y USUARIO (exFAT)
 * El pendrive tiene cuatro particiones (tools/soma_disco.py): SOMA (arranque), DATOS (el disco de SOMA, arriba), MODELOS y USUARIO.
 * Aqui, tras poner DATOS como disco del sistema:
 *   1. Primer arranque de la imagen del repositorio (5 GiB) en un pendrive mayor: MODELOS y USUARIO, si estan vacias, se agrandan
 *      hasta el final (unid_agrandar; a prueba de cortes). Solo se puede escribir el MBR y desde MODELOS en adelante: nunca en el
 *      arranque ni en DATOS. soma.agrandar=off lo evita.
 *   2. Se montan: /usuario (lectura y escritura) y /modelos (solo lectura desde el VFS). Cada unidad solo lee y escribe dentro de
 *      SU particion (se comprueba aqui, antes de llegar al USB) y, como DATOS, deja de escribir si el pendrive cambia. */
static struct kmutex g_ulock __attribute__((aligned(16)));
static void u_lock(void) { kmutex_lock(&g_ulock); }
static void u_unlock(void) { kmutex_unlock(&g_ulock); }
struct upart { uint64_t lba, n; };
static struct upart g_up[UNID_N];
static uint64_t g_p3_lba;                                       /* lo prohibido en el agrandado: [1, g_p3_lba) */
static uint32_t g_ugrow_mod, g_ugrow_usu;
static const char *g_ugrow_why = "", *g_uwhy[UNID_N] = { "no hay particion USUARIO", "no hay particion MODELOS" };

static int u_raw(int wr, uint64_t abs, uint32_t n, void *buf)
{
    kmutex_lock(&g_lock);
    int r = -1;
    if (still_ours() && !(wr && g.readonly)) {
        r = 0;
        uint8_t *p = (uint8_t *)buf;
        while (n && r == 0) { uint32_t k = n > 128 ? 128 : n; r = raw(wr, abs, k, p); abs += k; p += (uint64_t)k * 512; n -= k; }
        if (wr) g.dirty = 1;
    }
    if (r) g_errors++;
    kmutex_unlock(&g_lock);
    return r ? -1 : 0;
}
static int up_rd(void *c, uint64_t lba, uint32_t n, void *b)
{
    const struct upart *p = (const struct upart *)c;
    if (!n || lba >= p->n || n > p->n - lba) return -1;           /* fuera de SU particion: nunca llega al USB */
    return u_raw(0, p->lba + lba, n, b);
}
static int up_wr(void *c, uint64_t lba, uint32_t n, const void *b)
{
    const struct upart *p = (const struct upart *)c;
    if (!n || lba >= p->n || n > p->n - lba) return -1;
    return u_raw(1, p->lba + lba, n, (void *)b);
}
static void *u_alloc(uint32_t n) { return kmalloc(n); }
static void u_free(void *p) { kfree(p); }
static uint32_t u_now(void)
{
    struct rtc_time t; rtc_get_datetime(&t);
    uint32_t y = t.year >= 1980 ? (uint32_t)t.year - 1980 : 0;
    return (y << 25) | ((uint32_t)t.month << 21) | ((uint32_t)t.day << 16) | ((uint32_t)t.hour << 11) | ((uint32_t)t.min << 5) | ((uint32_t)t.sec / 2);
}
/* agrandado: disco entero, pero solo el MBR y desde MODELOS */
static int ag_rd(void *c, uint64_t lba, uint32_t n, void *b) { (void)c; return u_raw(0, lba, n, b); }
static int ag_wr(void *c, uint64_t lba, uint32_t n, const void *b)
{
    (void)c;
    if (lba == 0 ? n != 1 : lba < g_p3_lba) return -1;
    if (g.part_sectors && lba < g.part_lba + g.part_sectors && lba + n > g.part_lba) return -1;    /* nunca en DATOS */
    return u_raw(1, lba, n, (void *)b);
}
static int ag_sync(void *c) { (void)c; kmutex_lock(&g_lock); int r = xhci_msc_sync(); kmutex_unlock(&g_lock); return r; }

static void usbdisk_unidades(void)
{
    static int inited;
    if (!inited) { kmutex_init(&g_ulock, "unidades"); unid_set_lock(u_lock, u_unlock); inited = 1; }
    const struct xhci_msc_info *mi = xhci_msc_get();
    static uint8_t m[512] __attribute__((aligned(16)));
    if (g.in_file || u_raw(0, 0, 1, m) != 0 || m[510] != 0x55 || m[511] != 0xAA) return;   /* pendrive antiguo (SOMADATA.IMG) o sin MBR */
    g_p3_lba = ud_le32(m + 0x1BE + 16 * 2 + 8);
    if (g_p3_lba && !bootinfo_disabled("agrandar")) {
        struct unid_disco d = { ag_rd, ag_wr, ag_sync, 0, mi->sectors, u_alloc, u_free, u_now };
        int r = unid_agrandar(&d, &g_ugrow_why, &g_ugrow_mod, &g_ugrow_usu);
        if (r == 1) { kprint("DISCO USB: MODELOS y USUARIO agrandadas hasta el final del pendrive: "); kprint_dec(g_ugrow_mod); kprint(" y "); kprint_dec(g_ugrow_usu); kprint(" MiB\n"); }
        else if (r < 0) { kprint("DISCO USB: no se pudieron agrandar MODELOS y USUARIO ("); kprint(g_ugrow_why); kprint("); se reintenta en el proximo arranque\n"); }
        if (u_raw(0, 0, 1, m) != 0) return;
    } else if (bootinfo_disabled("agrandar")) g_ugrow_why = "desactivado (soma.agrandar=off)";
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = m + 0x1BE + 16 * i;
        uint64_t lba = ud_le32(e + 8), n = ud_le32(e + 12);
        if (!e[4] || !lba || n < 8192 || lba + n > mi->sectors) continue;
        if (lba < g.part_lba + g.part_sectors && lba + n > g.part_lba) continue;                  /* DATOS */
        static uint8_t b[512] __attribute__((aligned(16)));
        if (u_raw(0, lba, 1, b) != 0 || !exfat_is_boot(b)) continue;
        int u = -1;
        struct upart prueba = { lba, n };
        static struct exfat_vol v;
        struct exfat_io io = { up_rd, 0, &prueba, u_alloc, u_free, u_now };
        if (exfat_mount(&v, &io, 0, n) != 0) continue;
        const char *l = v.label;
        if (l[0] == 'U' && l[1] == 'S' && l[2] == 'U' && l[3] == 'A' && l[4] == 'R' && l[5] == 'I' && l[6] == 'O' && !l[7]) u = UNID_USUARIO;
        if (l[0] == 'M' && l[1] == 'O' && l[2] == 'D' && l[3] == 'E' && l[4] == 'L' && l[5] == 'O' && l[6] == 'S' && !l[7]) u = UNID_MODELOS;
        exfat_umount(&v);
        if (u < 0 || unid_activa(u)) continue;
        g_up[u].lba = lba; g_up[u].n = n;
        struct exfat_io io2 = { up_rd, up_wr, &g_up[u], u_alloc, u_free, u_now };
        int r = unid_montar(u, &io2, n);
        g_uwhy[u] = r ? exfat_err_str(r) : "montada";
        kprint("DISCO USB: unidad /"); kprint(unid_nombre(u)); kprint(r ? " no se pudo montar: " : " en la particion ");
        if (r) kprint(exfat_err_str(r)); else { kprint_dec((uint32_t)(i + 1)); kprint(" ("); kprint_dec((uint32_t)(n / 2048)); kprint(" MiB)"); }
        kprint("\n");
    }
}
/* para la orden "disco" y el informe */
void usbdisk_unidades_info(void)
{
    for (int u = 0; u < UNID_N; u++) {
        kprint("  /"); kprint(unid_nombre(u)); kprint(": ");
        uint64_t li, to; char et[16];
        if (unid_info(u, &li, &to, et, sizeof et) == 0) {
            kprint("exFAT '"); kprint(et); kprint("', particion de "); kprint_dec((uint32_t)(g_up[u].n / 2048)); kprint(" MiB, libres ");
            kprint_dec((uint32_t)(li >> 20)); kprint(" MiB"); kprint(u == UNID_MODELOS ? " (solo lectura desde SOMA)\n" : "\n");
        } else { kprint(g_uwhy[u]); kprint("\n"); }
    }
    if (g_ugrow_mod) { kprint("  agrandadas en este arranque: MODELOS "); kprint_dec(g_ugrow_mod); kprint(" MiB, USUARIO "); kprint_dec(g_ugrow_usu); kprint(" MiB\n"); }
    else if (g_ugrow_why && g_ugrow_why[0]) { kprint("  sin agrandar: "); kprint(g_ugrow_why); kprint("\n"); }
}
