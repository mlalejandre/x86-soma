/* SOMA_228_ARRANQUE: diagnostico del arranque e informe de hardware. Ver bootdiag.h. */
#include <stdint.h>
#include "bootdiag.h"
#include "console.h"
#include "idt.h"
#include "thread.h"
#include "mem.h"
#include "sysdisk.h"
#include "usbdisk.h"
#include "hwscan.h"
#include "pci.h"
#include "bootfb.h"
#include "rtc.h"
#include "hora.h"     /* SOMA_332_HORA */
#include "fs.h"
#include "e1000e.h"
#include "soma_str.h"

int dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out);

/* ------------------------------------------------------------------ etapas */
#define BD_MAX 24
static const char *g_name[BD_MAX], *g_opt[BD_MAX];
static uint32_t g_t[BD_MAX];
static int g_n, g_done, g_disk;
static uint32_t g_boot_ms;
static char g_prev[96];                                 /* etapa del arranque anterior que no termino ("" = termino bien) */
static uint32_t g_boots;

/* registro de 512 bytes en el sector BOOTDIAG_LBA */
struct bd_rec {
    char magic[8];                                      /* "SOMABOOT" */
    uint32_t version, state, stage, total, boots, ms;   /* state: 1 arrancando, 2 completo */
    char name[48], opt[48];
    uint8_t pad[512 - 8 - 6 * 4 - 96];
} __attribute__((packed));
_Static_assert(sizeof(struct bd_rec) == 512, "registro de arranque de 512 bytes");
static struct bd_rec g_rec __attribute__((aligned(16)));

static void scpy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }

static void persist(uint32_t state)
{
    if (!g_disk) return;
    for (int i = 0; i < 512; i++) ((uint8_t *)&g_rec)[i] = 0;
    for (int i = 0; i < 8; i++) g_rec.magic[i] = "SOMABOOT"[i];
    g_rec.version = 1; g_rec.state = state; g_rec.stage = (uint32_t)g_n; g_rec.total = 13; g_rec.boots = g_boots;
    g_rec.ms = (uint32_t)timer_get_uptime_ms();
    scpy(g_rec.name, g_n ? g_name[g_n - 1] : "", sizeof g_rec.name);
    scpy(g_rec.opt, g_n && g_opt[g_n - 1] ? g_opt[g_n - 1] : "", sizeof g_rec.opt);
    if (sysdisk_write(BOOTDIAG_LBA, &g_rec) == 0) (void)sysdisk_flush();
}

void bootdiag_stage(const char *name, const char *grub_opt)
{
    if (g_done || g_n >= BD_MAX) return;
    g_name[g_n] = name; g_opt[g_n] = grub_opt; g_t[g_n] = (uint32_t)timer_get_uptime_ms(); g_n++;
    kprint("[ARRANQUE "); kprint_dec((uint32_t)g_n); kprint("/13] "); kprint(name); kprint("\n");
    persist(1);
}

void bootdiag_disk_ready(void)
{
    if (g_disk || !sysdisk_capacity() || sysdisk_capacity() <= BOOTDIAG_LBA) return;
    if (sysdisk_read(BOOTDIAG_LBA, &g_rec) != 0) return;
    g_prev[0] = 0;
    {
        int ok = 1; for (int i = 0; i < 8; i++) if (g_rec.magic[i] != "SOMABOOT"[i]) ok = 0;
        if (ok && g_rec.version == 1) {
            g_boots = g_rec.boots;
            if (g_rec.state == 1) {
                g_rec.name[47] = 0; g_rec.opt[47] = 0;
                char *p = g_prev; uint32_t cap = sizeof g_prev, k = 0;
                const char *a = "etapa "; while (*a && k + 1 < cap) p[k++] = *a++;
                char d[12]; int dn = 0; uint32_t v = g_rec.stage; do { d[dn++] = (char)('0' + v % 10); v /= 10; } while (v);
                while (dn && k + 1 < cap) p[k++] = d[--dn];
                a = "/13: "; while (*a && k + 1 < cap) p[k++] = *a++;
                a = g_rec.name; while (*a && k + 1 < cap) p[k++] = *a++;
                if (g_rec.opt[0]) { a = " (prueba "; while (*a && k + 1 < cap) p[k++] = *a++; a = g_rec.opt; while (*a && k + 1 < cap) p[k++] = *a++; if (k + 1 < cap) p[k++] = ')'; }
                p[k] = 0;
            }
        }
    }
    g_boots++;
    g_disk = 1;
    if (g_prev[0]) { kprint("\033[1;33m[ARRANQUE] el arranque anterior no llego al escritorio: se quedo en la "); kprint(g_prev); kprint("\033[0m\n"); }
    persist(1);
}

void bootdiag_complete(void)
{
    if (g_done) return;
    g_done = 1; g_boot_ms = (uint32_t)timer_get_uptime_ms();
    kprint("[ARRANQUE] completo en "); kprint_dec(g_boot_ms); kprint(" ms\n");
    persist(2);
}
int bootdiag_in_boot(void) { return !g_done; }
const char *bootdiag_prev_failure(void) { return g_prev; }
uint32_t bootdiag_boot_ms(void) { return g_boot_ms; }

void bootdiag_panic_hint(void)
{
    if (g_done || !g_n) return;
    kprint("\nEL ARRANQUE SE DETUVO EN LA ETAPA "); kprint_dec((uint32_t)g_n); kprint("/13: "); kprint(g_name[g_n - 1]); kprint("\n");
    if (g_opt[g_n - 1]) {
        kprint("Prueba a saltarla: en el menu de GRUB pulsa 'e', anade  "); kprint(g_opt[g_n - 1]);
        kprint("  al final de la linea 'multiboot2' y pulsa F10.\n");
    }
    kprint("O elige 'modo seguro' en el menu de GRUB. Haz una foto de esta pantalla y compartela en GitHub o en YouTube:\n");
    kprint("asi se puede arreglar para tu equipo. El proximo arranque guardara el informe en INFORME.TXT del pendrive.\n");
}

/* ------------------------------------------------------------------ informe */
static void od2(soma_sbuf_t *o, uint32_t v) { if (v < 10) soma_sbuf_append_str(o, "0"); soma_sbuf_append_u64(o, v); }

/* que hace SOMA con cada dispositivo PCI */
static const char *coverage(uint16_t ven, uint16_t dev, uint32_t cc)
{
    uint32_t cs = cc >> 8;
    if (cc == 0x0C0330) return "SI: xHCI (teclado, raton, pendrive y camara USB)";
    if (cs == 0x0C03) return "no: USB 1.1/2.0 sin xHCI (usa un puerto USB 3)";
    if (cc == 0x010601) return "SI: AHCI (discos SATA)";
    if (cc == 0x010802) return "SI: NVMe";
    if (cs == 0x0106 || cs == 0x0101 || cs == 0x0104 || cs == 0x0107) return "no: este tipo de disco no";
    if (cs == 0x0403) return "SI: audio HD (Intel HDA)";
    if (cs == 0x0401 || cs == 0x0480) return "no: audio";
    if (ven == 0x1AF4) return cs == 0x0200 ? "SI: virtio-net" : cs == 0x0100 ? "SI: virtio-blk" : "maquina virtual";
    if (cs == 0x0200) return ven == 0x8086 && e1000e_pci_known(dev) ? "SI: Ethernet Intel (e1000e)" : ven == 0x10EC ? "no: Ethernet Realtek (aun no)" : "no: este Ethernet aun no";
    if (cs == 0x0280) {
        if (ven == 0x8086 && (dev == 0x24FD || dev == 0x24F3 || dev == 0x24F4 || dev == 0x24F5 || dev == 0x24F6)) return "SI: WiFi Intel 8260/8265";
        return ven == 0x8086 ? "no: este WiFi Intel aun no (solo 8260/8265)" : "no: WiFi de otra marca (aun no)";
    }
    if (cs == 0x0300 || cs == 0x0380) return "basico: pantalla del firmware (UEFI GOP), sin aceleracion";
    if (cs == 0x0C80) return "no: I2C (los trackpads I2C-HID aun no; PS/2 y USB si)";
    if (cs == 0x0C05) return "no hace falta (SMBus)";
    if (cs == 0x0805) return "no: lector de tarjetas SD";
    if (cs == 0x0C0A || cc == 0x0C0340 || (cs == 0x0880 && ven == 0x8086 && (dev == 0x15D9 || dev == 0x15D1 || dev == 0x15BF))) return "no: Thunderbolt/USB4";
    if ((cc >> 16) == 0x06) return "no hace falta (puente)";
    if (cs == 0x0780 || cs == 0x0880 || cs == 0x1180) return "no hace falta";
    return "sin driver";
}

static void strip_ansi(char *s, uint32_t *n)
{
    uint32_t w = 0;
    for (uint32_t r = 0; r < *n; r++) {
        if (s[r] == 0x1B && r + 1 < *n && s[r + 1] == '[') { r += 2; while (r < *n && !((s[r] >= 'A' && s[r] <= 'Z') || (s[r] >= 'a' && s[r] <= 'z'))) r++; continue; }
        if (s[r] == '\r') continue;
        s[w++] = s[r];
    }
    s[w] = 0; *n = w;
}

/* privacidad: ninguna linea que hable de claves, contrasenas o tokens sale en el informe (por si algun driver las registrara) */
static int has_ci(const char *l, uint32_t n, const char *w)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = 0;
        while (w[k] && i + k < n) { char c = l[i + k]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != w[k]) break; k++; }
        if (!w[k]) return 1;
    }
    return 0;
}
static void redact(char *s, uint32_t *n, uint32_t from)
{
    static const char *const bad[] = { "clave", "api_key", "apikey", "api key", "key=", "key:", "token", "psk", "passw", "contrasen", "secret", "bearer", "authoriz" };
    static const char repl[] = "[linea omitida por privacidad]";
    uint32_t w = from, r = from;
    while (r < *n) {
        uint32_t e = r; while (e < *n && s[e] != '\n') e++;
        int hide = 0; for (unsigned b = 0; b < sizeof bad / sizeof bad[0] && !hide; b++) hide = has_ci(s + r, e - r, bad[b]);
        if (hide) { for (uint32_t k = 0; k + 1 < sizeof repl && w < e; k++) s[w++] = repl[k]; }   /* nunca mas largo que la linea */
        else { for (uint32_t k = r; k < e; k++) s[w++] = s[k]; }
        if (e < *n) s[w++] = '\n';
        r = e + 1;
    }
    s[w] = 0; *n = w;
}

static void section(soma_sbuf_t *o, const char *t) { soma_sbuf_append_str(o, "\n== "); soma_sbuf_append_str(o, t); soma_sbuf_append_str(o, " ==\n"); }

static void capture(soma_sbuf_t *o, const char *cmd)
{
    section(o, cmd);
    if (console_capture_active()) { soma_sbuf_append_str(o, "(la consola estaba ocupada)\n"); return; }
    uint32_t room = o->cap - o->len;
    if (room < 2048) return;
    static char res[4096];
    res[0] = 0;
    console_capture_begin(o->buf + o->len, room - 1024, "");
    dispatch_command(cmd, res, sizeof res);
    uint32_t got = console_capture_end();
    o->len += got; o->buf[o->len] = 0;
    soma_sbuf_append_str(o, res);
}

uint32_t bootdiag_report(char *out, uint32_t max)
{
    soma_sbuf_t o = { out, max, 0 };
    if (!out || max < 4096) return 0;
    out[0] = 0;
    /* el registro del nucleo, antes de que las ordenes de abajo escriban en el */
    uint32_t kcap = 96u * 1024u, kn = 0;
    char *klog = (char *)kmalloc(kcap + 1);
    if (klog) kn = klog_snapshot(klog, kcap + 1);

    struct rtc_time t; hora_local(&t);           /* SOMA_332_HORA */
    soma_sbuf_append_str(&o, "SOMA 0.3 pre-beta - informe de arranque y hardware\n");
    soma_sbuf_append_str(&o, "Compartelo en https://github.com/mlalejandre/x86-soma/issues o en los comentarios del canal.\n");
    soma_sbuf_append_str(&o, "No contiene contrasenas ni claves: solo el equipo, los dispositivos y el registro del nucleo.\n");
    soma_sbuf_append_str(&o, "Generado: "); soma_sbuf_append_u64(&o, t.year); soma_sbuf_append_str(&o, "-"); od2(&o, t.month); soma_sbuf_append_str(&o, "-"); od2(&o, t.day); soma_sbuf_append_str(&o, " ");
    od2(&o, t.hour); soma_sbuf_append_str(&o, ":"); od2(&o, t.min); soma_sbuf_append_str(&o, " (hora local), "); soma_sbuf_append_u64(&o, timer_get_uptime_ms() / 1000); soma_sbuf_append_str(&o, " s tras encender\n");
    soma_sbuf_append_str(&o, "Compilado: " __DATE__ "\nOpciones de arranque: "); soma_sbuf_append_str(&o, bootinfo_cmdline()[0] ? bootinfo_cmdline() : "(ninguna)"); soma_sbuf_append_str(&o, "\n");

    uint32_t body = o.len;                                             /* desde aqui se filtran las lineas con claves */
    section(&o, "Arranque");
    if (g_prev[0]) { soma_sbuf_append_str(&o, "El arranque ANTERIOR no llego al escritorio: se quedo en la "); soma_sbuf_append_str(&o, g_prev); soma_sbuf_append_str(&o, "\n"); }
    else soma_sbuf_append_str(&o, "El arranque anterior termino bien (o es el primero).\n");
    for (int i = 0; i < g_n; i++) {
        soma_sbuf_append_str(&o, "  "); soma_sbuf_append_u64(&o, (uint64_t)i + 1); soma_sbuf_append_str(&o, ". "); soma_sbuf_append_str(&o, g_name[i]); soma_sbuf_append_str(&o, "  ");
        soma_sbuf_append_u64(&o, g_t[i]); soma_sbuf_append_str(&o, " ms"); if (g_opt[i]) { soma_sbuf_append_str(&o, "  [se salta con "); soma_sbuf_append_str(&o, g_opt[i]); soma_sbuf_append_str(&o, "]"); } soma_sbuf_append_str(&o, "\n");
    }
    if (g_done) { soma_sbuf_append_str(&o, "Completo en "); soma_sbuf_append_u64(&o, g_boot_ms); soma_sbuf_append_str(&o, " ms. Arranques registrados en este pendrive: "); soma_sbuf_append_u64(&o, g_boots); soma_sbuf_append_str(&o, "\n"); }
    else soma_sbuf_append_str(&o, "El arranque aun no ha terminado.\n");

    section(&o, "Equipo");
    { uint32_t cap = o.cap - o.len; if (cap > 4096) { hw_section_text("resumen", o.buf + o.len, cap); while (o.buf[o.len]) o.len++; } }

    section(&o, "Dispositivos PCI y driver de SOMA");
    {
        int si = 0, no = 0, nd = 0;
        for (int bus = 0; bus < 256 && nd < 96; bus++) for (int sl = 0; sl < 32 && nd < 96; sl++) for (int fn = 0; fn < 8; fn++) {
            uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)sl, (uint8_t)fn);
            if (v == 0xFFFF || v == 0) { if (fn == 0) break; continue; }
            uint16_t d = pci_device_id((uint8_t)bus, (uint8_t)sl, (uint8_t)fn);
            uint32_t cc = pci_config_read32((uint8_t)bus, (uint8_t)sl, (uint8_t)fn, 8) >> 8;
            uint32_t sub = pci_config_read32((uint8_t)bus, (uint8_t)sl, (uint8_t)fn, 0x2C);
            const char *c = coverage(v, d, cc), *nm = hw_pci_name(v, d), *vn = hw_vendor_name(v);
            soma_sbuf_append_str(&o, "  "); soma_sbuf_append_hex(&o, (uint32_t)bus, 2, 1); soma_sbuf_append_str(&o, ":"); soma_sbuf_append_hex(&o, (uint32_t)sl, 2, 1); soma_sbuf_append_str(&o, "."); soma_sbuf_append_u64(&o, (uint64_t)fn);
            soma_sbuf_append_str(&o, "  "); soma_sbuf_append_hex(&o, v, 4, 1); soma_sbuf_append_str(&o, ":"); soma_sbuf_append_hex(&o, d, 4, 1); soma_sbuf_append_str(&o, " ("); soma_sbuf_append_hex(&o, sub & 0xFFFF, 4, 1); soma_sbuf_append_str(&o, ":"); soma_sbuf_append_hex(&o, sub >> 16, 4, 1);
            soma_sbuf_append_str(&o, ")  clase "); soma_sbuf_append_hex(&o, cc, 6, 1); soma_sbuf_append_str(&o, "  "); if (vn) { soma_sbuf_append_str(&o, vn); soma_sbuf_append_str(&o, " "); } soma_sbuf_append_str(&o, nm ? nm : ""); soma_sbuf_append_str(&o, "\n      -> "); soma_sbuf_append_str(&o, c); soma_sbuf_append_str(&o, "\n");
            if (c[0] == 'S') si++; else if (c[0] == 'n' && c[1] == 'o' && c[2] == ':') no++;
            nd++;
            if (fn == 0 && !((pci_config_read32((uint8_t)bus, (uint8_t)sl, 0, 12) >> 16) & 0x80)) break;
        }
        soma_sbuf_append_str(&o, "Con driver: "); soma_sbuf_append_u64(&o, (uint64_t)si); soma_sbuf_append_str(&o, "   Sin driver (y harian falta): "); soma_sbuf_append_u64(&o, (uint64_t)no); soma_sbuf_append_str(&o, "\n");
    }
    {   /* disco del sistema */
        const struct usbdisk_info *u = usbdisk_info();
        section(&o, "SOMADATA");
        soma_sbuf_append_str(&o, sysdisk_describe()); soma_sbuf_append_str(&o, "\n");
        if (u->grown_to_mb) { soma_sbuf_append_str(&o, "Ampliada en el primer arranque de "); soma_sbuf_append_u64(&o, u->grown_from_mb); soma_sbuf_append_str(&o, " a "); soma_sbuf_append_u64(&o, u->grown_to_mb); soma_sbuf_append_str(&o, " MiB\n"); }
        else if (u->grow_why && u->grow_why[0]) { soma_sbuf_append_str(&o, "Sin ampliar: "); soma_sbuf_append_str(&o, u->grow_why); soma_sbuf_append_str(&o, "\n"); }
    }
    static const char *const cmds[] = { "pantalla", "teclado", "disco", "wifi", "bateria", "energia" };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) capture(&o, cmds[i]);

    section(&o, "Registro del nucleo (dmesg, lo mas reciente)");
    if (klog && kn) {
        uint32_t room = o.cap - o.len > 64 ? o.cap - o.len - 64 : 0, from = kn > room ? kn - room : 0;
        while (from < kn && from && klog[from - 1] != '\n') from++;          /* empezar en una linea entera */
        for (uint32_t i = from; i < kn && o.len + 1 < o.cap; i++) o.buf[o.len++] = klog[i];
        o.buf[o.len] = 0;
    }
    if (klog) kfree(klog);
    soma_sbuf_append_str(&o, "\n-- fin del informe --\n");
    strip_ansi(o.buf, &o.len);
    redact(o.buf, &o.len, body < o.len ? body : o.len);
    return o.len;
}

/* ------------------------------------------------------------------ guardado en segundo plano */
static volatile int g_busy;
static char g_status[160] = "aun no se ha generado";
static void st(const char *a, const char *b) { g_status[0] = 0; scpy(g_status, a, sizeof g_status); uint32_t n = 0; while (g_status[n]) n++; scpy(g_status + n, b ? b : "", sizeof g_status - n); }

static void saver(void *a)
{
    (void)a;
    const uint32_t cap = 240u * 1024u;
    char *buf = (char *)kmalloc(cap);
    if (!buf) { st("sin memoria para el informe", 0); g_busy = 0; return; }
    uint32_t n = bootdiag_report(buf, cap);
    const char *why = "";
    int usb = usbdisk_bootfile_write("INFORME.TXT", buf, n, &why);
    int v = vfs_write("/informe.txt", buf, n);
    if (v < 0) { vfs_create("/informe.txt", ""); v = vfs_write("/informe.txt", buf, n); }
    if (usb == 0) st(v >= 0 ? "en INFORME.TXT del pendrive y /informe.txt" : "en INFORME.TXT del pendrive", 0);
    else if (v >= 0) st("solo en /informe.txt (no en el pendrive)", 0);
    else st("no se pudo guardar", 0);
    if (usb != 0) { kprint("[INFORME] pendrive: "); kprint(why); kprint("\n"); }
    kprint("[INFORME] "); kprint(g_status); kprint("\n");
    kfree(buf);
    g_busy = 0;
}
void bootdiag_report_save_async(void)
{
    if (g_busy) return;
    g_busy = 1; st("generando...", 0);
    if (!thread_create("informe", saver, 0)) { g_busy = 0; st("no se pudo crear el hilo", 0); }
}
const char *bootdiag_report_status(void) { return g_status; }
