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
#include "virtio_blk.h"
#include "console.h"
#include "mutex.h"
#include "thread.h"
#include "bootfb.h"
#include "usbdisk.h"
#include "usbdisk_core.h"


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

int usbdisk_attach(void)
{
    static int inited;
    if (!inited) { kmutex_init(&g_lock, "usbdisk"); inited = 1; }
    if (g.active) return 0;
    g.why = "";
    if (bootinfo_disabled("usbdisk")) { g.why = "desactivado (soma.usbdisk=off o modo seguro)"; return -1; }
    if (virtio_blk_capacity()) { g.why = "ya hay disco virtio"; return -1; }
    xhci_autostart_early();                                               /* teclado, raton y disco USB, ahora y no despues */
    if (!xhci_msc_ready()) { g.why = "no hay disco USB (almacenamiento masivo)"; return -1; }
    const struct xhci_msc_info *mi = xhci_msc_get();
    g_gen = xhci_msc_generation();
    if (raw(0, 0, 1, g_sec) != 0) { g.why = "no se pudo leer el sector 0 del pendrive"; return -1; }
    struct ud_part pt[4];
    int np = ud_parse_mbr(g_sec, mi->sectors, pt, &g.disk_sig);
    if (np < 0) { g.why = np == -2 ? "el pendrive usa GPT (ejecutar.py lo prepara con MBR)" : "el pendrive no tiene tabla MBR"; return -1; }
    int found = -1, fresh = 0;
    for (int i = 0; i < np && found < 0; i++) {
        if (raw(0, pt[i].lba, 1, g_sec) != 0) continue;
        g.part_lba = pt[i].lba; g.part_sectors = pt[i].n;
        if (mark_ok(g_sec)) { found = pt[i].index; break; }
        if (ud_is_fat_somadata(g_sec)) { found = pt[i].index; fresh = 1; break; }
    }
    if (found < 0) { g.part_lba = g.part_sectors = 0; g.why = "el pendrive no tiene la particion SOMADATA (ejecutar.py -t t480s-usb --formatear)"; return -1; }
    g.part_index = found + 1;
    g.sectors = ud_data_sectors(g.part_sectors);
    if (fresh) {
        kprint("DISCO USB: particion SOMADATA nueva: preparandola (8 MiB)...\n");
        for (uint32_t s = 0; s < UD_ZERO_SECT; s += 128) {
            uint32_t k = UD_ZERO_SECT - s < 128 ? UD_ZERO_SECT - s : 128;
            if (raw(1, g.part_lba + UD_DATA_OFF + s, k, g_zero) != 0) { g.why = "error al preparar la particion"; return -1; }
        }
        mark_build(g_sec);
        if (raw(1, g.part_lba, 1, g_sec) != 0 || xhci_msc_sync() != 0) { g.why = "error al escribir la marca"; return -1; }
        if (raw(0, g.part_lba, 1, g_sec) != 0 || !mark_ok(g_sec)) { g.why = "la marca no se leyo bien despues de escribirla"; return -1; }
        g.claimed_now = 1;
    }
    g_ops.sectors = g.sectors;
    if (virtio_blk_set_alt(&g_ops) != 0) { g.why = "no se pudo poner como disco del sistema"; return -1; }
    g.active = 1; g.why = "activo";
    thread_create("usbsync", syncer, 0);
    kprint("DISCO USB: disco del sistema en la particion "); kprint_dec((uint32_t)g.part_index); kprint(" del pendrive ("); kprint(mi->vendor);
    kprint(" "); kprint(mi->product); kprint("): "); kprint_dec((uint32_t)(g.sectors / 2048)); kprint(" MiB");
    kprint(g.claimed_now ? " (recien preparada)\n" : "\n");
    return 0;
}

const struct usbdisk_info *usbdisk_info(void) { return &g; }
void usbdisk_stats(uint64_t *r, uint64_t *w, uint64_t *e, uint64_t *rt) { *r = g_reads; *w = g_writes; *e = g_errors; *rt = g_retries; }
int usbdisk_flush(void) { return g.active ? d_flush() : -1; }
