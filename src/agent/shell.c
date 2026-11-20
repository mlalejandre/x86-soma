#if defined(__x86_64__)
#include "bootfb.h"   /* SOMA_154_T480S */
#include "i8042.h"    /* SOMA_155_T480S */
#include "hwscan.h"   /* SOMA_156_T480S */
#endif
#include "image.h"
#include "fb.h"
#include "usermode.h"
#include "elf.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#include "micrec.h"
#include "camera.h"
#include "scrcap.h"                                  /* SOMA_179_T480S */                                   /* SOMA_178_T480S */
#include "media.h"
#include "gui_font.h"
#include "mouse.h"
#include "httpd.h"
#include "rng.h"
#include "agent_net.h"
#include "tls_trust.h"
#include "llm_proto.h"
#include "web_fault.h"
#if defined(__x86_64__)
#include "web_r3.h"
int browser_r3_wanted(void); int browser_r3_active(void);
#endif
#ifdef __x86_64__
#include "memprot.h"
#endif
#include "shell.h"
#include "lang/lang.h"
#include "console.h"
#include "fs.h"
#include "coder_app.h"
#include "coder_demo.h"
#include "web_net.h"   /* PARCHE 087 */
#include "mem.h"
#include "thread.h"
#include "panic.h"
#include "agent_policy.h"                              /* PARCHE 110 */
#include "conc_test.h"                                  /* PARCHE 111 */
#include "mutex.h"
#include "mutex.h"
#include "pmm.h"
#include "vmm.h"
#include "sysinfo.h"
#include "pci.h"
#include "hwinfo.h"
#include "blk.h"
#include "fat.h"
#if defined(__x86_64__)
#include "apic.h"
#include "ahci.h"
#include "nvme.h"
#include "xhci.h"
#endif
#if defined(__aarch64__)
#include "platform.h"
#include "xhci.h"                                      /* PARCHE 062: mismo controlador USB en la Pi 5 (via PCIe) */
#include "pcie.h"
#include "rp1.h"
#include "gem.h"
#ifdef __aarch64__
#include "rpi_fb.h"
#include "wifi.h"                                     /* PARCHE 079 */
#endif
#include "rp1_gpio.h"
#endif
#include "net.h"
#include "netif.h"                                    /* PARCHE 080 */
#include "crypto.h"
#include "wpa.h"
#include "dhcp.h"
#include "smp.h"
#include "ip.h"
#include "dns.h"
#include "http.h"
#include "virtio_blk.h"
#ifdef __x86_64__
#include "usbdisk.h"
#include "bootdiag.h"         /* SOMA_228_ARRANQUE */
#include "acpi_sys.h"
#include "cpupower.h"
#endif
#include "srcfs.h"
#include "boot_gate.h"
#include "rtc.h"
#include "nano.h"
#include "agent.h"
#include "llm.h"
#include "io.h"
#include "arch.h"
#include "idt.h"
#include "disk_layout.h"
#include "ramstat.h"

#ifndef SECTOR_USER_MIN
#define SECTOR_USER_MIN 3600
#endif

/* ====================================================================
 * UTILIDADES DE CADENA Y COMPARACIÓN
 * ==================================================================== */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static char to_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static const char *find_substr_ci(const char *haystack, const char *needle, int ignore_case)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            char ch = ignore_case ? to_lower_char(*h) : *h;
            char cn = ignore_case ? to_lower_char(*n) : *n;
            if (ch != cn) break;
            h++; n++;
        }
        if (!*n) return haystack;
    }
    return 0;
}

/* ====================================================================
 * SUBSISTEMA DE VARIABLES DE ENTORNO (ENV BARE-METAL)
 * ==================================================================== */

#define MAX_ENV_VARS 32
#define ENV_KEY_LEN  32
#define ENV_VAL_LEN  96

struct env_var {
    char key[ENV_KEY_LEN];
    char val[ENV_VAL_LEN];
    int  used;
    int  pad;
} __attribute__((aligned(8)));

static struct env_var env_table[MAX_ENV_VARS];
static int env_initialized = 0;

static const char *pipe_stdin_data = 0;
static uint32_t    pipe_stdin_len  = 0;

static void env_init_defaults(void)
{
    if (env_initialized) return;
    for (int i = 0; i < MAX_ENV_VARS; ++i) env_table[i].used = 0;

    int idx = 0;
    #define SET_DEF(k, v) do { \
        uint32_t p=0; while((k)[p] && p < ENV_KEY_LEN-1){ env_table[idx].key[p]=(k)[p]; p++; } env_table[idx].key[p]='\0'; \
        p=0; while((v)[p] && p < ENV_VAL_LEN-1){ env_table[idx].val[p]=(v)[p]; p++; } env_table[idx].val[p]='\0'; \
        env_table[idx].used = 1; idx++; \
    } while(0)

    SET_DEF("USER", "marcos");
    SET_DEF("HOST", "soma-node1");
    SET_DEF("ARCH", MYOS_ARCH_NAME);
    SET_DEF("OS", "SOMA-0.2");
    SET_DEF("SHELL", "/bin/soma");
    SET_DEF("PWD", "/");
    #undef SET_DEF

    env_initialized = 1;
}

static const char *env_get(const char *key)
{
    if (!env_initialized) env_init_defaults();
    if (!key || key[0] == '\0') return "";

    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            return env_table[i].val;
        }
    }
    return "";
}

static int env_set(const char *key, const char *val)
{
    if (!env_initialized) env_init_defaults();
    if (!key || key[0] == '\0') return -1;

    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            uint32_t p = 0;
            while (val && val[p] && p < ENV_VAL_LEN - 1) { env_table[i].val[p] = val[p]; p++; }
            env_table[i].val[p] = '\0';
            return 0;
        }
    }
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (!env_table[i].used) {
            uint32_t p = 0;
            while (key[p] && p < ENV_KEY_LEN - 1) { env_table[i].key[p] = key[p]; p++; }
            env_table[i].key[p] = '\0';
            p = 0;
            while (val && val[p] && p < ENV_VAL_LEN - 1) { env_table[i].val[p] = val[p]; p++; }
            env_table[i].val[p] = '\0';
            env_table[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int env_unset(const char *key)
{
    if (!env_initialized) env_init_defaults();
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used && str_eq(env_table[i].key, key)) {
            env_table[i].used = 0;
            return 0;
        }
    }
    return -1;
}

static void env_expand(const char *src, char *dst, uint32_t max_dst)
{
    if (!env_initialized) env_init_defaults();
    if (!src || !dst || max_dst == 0) return;

    uint32_t dp = 0;
    while (*src && dp < max_dst - 1) {
        if (*src == '$' && *(src + 1) != '\0' && *(src + 1) != ' ' && *(src + 1) != '"' && *(src + 1) != '\'') {
            src++;
            char k_buf[ENV_KEY_LEN];
            uint32_t kp = 0;
            while ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') || (*src >= '0' && *src <= '9') || *src == '_') {
                if (kp < sizeof(k_buf) - 1) k_buf[kp++] = *src;
                src++;
            }
            k_buf[kp] = '\0';
            const char *val = env_get(k_buf);
            while (*val && dp < max_dst - 1) dst[dp++] = *val++;
        } else {
            dst[dp++] = *src++;
        }
    }
    dst[dp] = '\0';
}

/* ====================================================================
 * FORMATEO DE BUFFER Y HARDWARE
 * ==================================================================== */

struct bg_job {
    char cmd[256];
    uint32_t tid;
    char log_file[64];
    uint8_t agent_origin;
};

/* AGENT_POLICY_V1: frontera de privilegios del Agente ReAct. */
/* PARCHE 046: el modo Agente es POR HILO (tcb.agent_mode). Con un flag global,
 * un job del agente (spawn) que dormia heredaba privilegios completos cuando
 * otro hilo restauraba el flag a 0. */
static int agent_mode_fallback = 0;
static int *agent_mode_ptr(void)
{
    struct tcb *t = thread_current();
    return t ? &t->agent_mode : &agent_mode_fallback;
}
#define agent_dispatch_mode (*agent_mode_ptr())

int dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out);

/* PARCHE 110: politica de escritura de los agentes (ver agent_policy.h). Devuelve 1 (y avisa) si el agente intenta modificar una ruta protegida. */
static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s);
volatile uint32_t agent_denied_writes;
static int agent_write_denied(const char *path, const char *cmd, char *out_buf, uint32_t max_out)
{
    /* SOMA_193: tampoco /www ni /public: lo que hay ahi lo sirve httpd a toda la red (seria publicar datos del usuario) */
    int published = agent_dispatch_mode && path && (agent_path_has_prefix(path, "/www") || agent_path_has_prefix(path, "/public"));
    if (!agent_dispatch_mode || (!agent_path_protected(path) && !published)) return 0;
    agent_denied_writes++;
    kprint("[SEGURIDAD AGENTE] "); kprint(cmd); kprint(published ? ": denegado, los agentes no pueden publicar en /www ni /public (lo sirve el servidor web).\n" : ": denegado, los agentes no pueden modificar /etc, /sys, /bin ni /boot.\n");
    if (out_buf && max_out) { uint32_t pp = 0; fb_puts(out_buf, max_out, &pp, "Error: Operacion denegada por politica de seguridad."); }
    return 1;
}

/* ---------------------------------------------------------------- SOMA_193: a donde puede conectarse un agente (agent_net.h) */
static int shell_starts_with(const char *s, const char *prefix);
static int agent_net_cfg_loaded;
static void agent_net_cfg_load(void)
{
    if (agent_net_cfg_loaded) return;
    agent_net_cfg_loaded = 1;
    static char b[2048];
    int n = vfs_read("/etc/agente_red.conf", b, sizeof b - 1);
    if (n <= 0) return;
    b[n] = 0;
    for (char *p = b; *p; ) {
        char *l = p; while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        while (*l == ' ') l++;
        if (shell_starts_with(l, "modo=")) {
            const char *v = l + 5;
            agent_net_set_mode(v[0] == 'a' ? AGENT_NET_ABIERTO : v[0] == 'c' ? AGENT_NET_CERRADO : AGENT_NET_PROCEDENCIA);
        } else if (shell_starts_with(l, "permitir=")) agent_net_allow_add(l + 9);
    }
}
static int agent_net_cfg_save(void)
{
    static char b[2048]; uint32_t p = 0;
    int m = agent_net_mode();
    fb_puts(b, sizeof b, &p, "# Destinos de red de los agentes (parche 193). Lo cambia la orden agente_red.\nmodo=");
    fb_puts(b, sizeof b, &p, m == AGENT_NET_ABIERTO ? "abierto" : m == AGENT_NET_CERRADO ? "cerrado" : "procedencia");
    fb_puts(b, sizeof b, &p, "\n");
    for (int i = 0; agent_net_allow_get(i); ++i) { fb_puts(b, sizeof b, &p, "permitir="); fb_puts(b, sizeof b, &p, agent_net_allow_get(i)); fb_puts(b, sizeof b, &p, "\n"); }
    return vfs_write("/etc/agente_red.conf", b, p) < 0 ? -1 : 0;
}
/* 1 (y avisa) si un agente pide un destino de red que no viene de la mision, de un resultado ni de la lista de confianza. */
volatile uint32_t agent_denied_net;
static int agent_net_denied(const char *cmd, const char *args, char *out_buf, uint32_t max_out)
{
    if (!agent_dispatch_mode) return 0;
    agent_net_cfg_load();
    if (agent_net_args_ok(args)) return 0;
    agent_denied_net++;
    kprint("[SEGURIDAD AGENTE] "); kprint(cmd); kprint(": destino de red no permitido (no viene de la mision, de un resultado de busqueda ni de la lista de confianza).\n");
    if (out_buf && max_out) {
        uint32_t pp = 0;
        fb_puts(out_buf, max_out, &pp, "AGENT_DENIED: destino de red no permitido. Solo puedes pedir una URL que aparezca TAL CUAL en la mision del usuario, en un resultado de search o en una pagina ya descargada. No compongas URL ni les anadas datos.");
    }
    return 1;
}
/* Solo se registran las URL de texto que viene de FUERA (search, curl, descargar) y sin encadenar ordenes: si valiera cualquier salida,
 * el modelo podria "blanquear" una URL con datos pegados haciendo `echo http://...` y pidiendola despues. */
static int agent_cmd_is_external(const char *c)
{
    while (*c == ' ') c++;
    if (!(shell_starts_with(c, "search ") || shell_starts_with(c, "ddg ") || shell_starts_with(c, "curl ") || shell_starts_with(c, "descargar "))) return 0;
    for (const char *p = c; *p; ++p) if (*p == ';' || *p == '|' || *p == '&' || *p == '>' || *p == '<' || *p == '`' || *p == '$') return 0;
    return 1;
}
int shell_agent_mode(void) { return agent_dispatch_mode; }

int dispatch_agent_command(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    int previous = agent_dispatch_mode;
    agent_dispatch_mode = 1;

    int result = dispatch_command(cmd_line, out_buf, max_out);
    if (out_buf && max_out && agent_cmd_is_external(cmd_line)) agent_net_observe(out_buf);   /* SOMA_193 */

    agent_dispatch_mode = previous;
    return result;
}

static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    if (!out || max == 0 || !pos || *pos >= max - 1) return;
    while (*s && *pos + 1 < max) {
        out[(*pos)++] = *s++;
    }
    out[*pos] = '\0';
}

static void fb_putc(char *out, uint32_t max, uint32_t *pos, char c)
{
    if (!out || max == 0) return;
    if (*pos + 1 < max) {
        out[(*pos)++] = c;
        out[*pos] = '\0';
    }
}

static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) { fb_putc(out, max, pos, '0'); return; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) fb_putc(out, max, pos, tmp[--n]);
}

static void fb_put_hex16(char *out, uint32_t max, uint32_t *pos, uint16_t v)
{
    const char hex[] = "0123456789ABCDEF";
    fb_putc(out, max, pos, hex[(v >> 12) & 0xF]);
    fb_putc(out, max, pos, hex[(v >> 8) & 0xF]);
    fb_putc(out, max, pos, hex[(v >> 4) & 0xF]);
    fb_putc(out, max, pos, hex[v & 0xF]);
}

static void fb_put_hex32(char *out, uint32_t max, uint32_t *pos, uint32_t v)
{
    fb_put_hex16(out, max, pos, (uint16_t)(v >> 16));
    fb_put_hex16(out, max, pos, (uint16_t)v);
}

static void parse_ip(const char *s, uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        uint32_t val = 0;
        while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
        ip[i] = (uint8_t)val;
        if (*s == '.') s++;
    }
}

static uint64_t parse_num(const char **str)
{
    uint64_t val = 0;
    while (**str == ' ') (*str)++;
    while (**str >= '0' && **str <= '9') { val = val * 10 + (**str - '0'); (*str)++; }
    return val;
}

/* SOMA_200_MENU_ENERGIA: reinicio/apagado real, compartido con la GUI. */
#ifdef __aarch64__
/* SMP usa esta misma distincion de firmware para CPU_ON: SMC para EL2 / Pi5,
 * HVC cuando QEMU virt arranca directamente en EL1. */
extern uint64_t soma_boot_el;
static void soma_psci_system_action(uint64_t function_id)
{
    register uint64_t x0 __asm__("x0") = function_id;
#if defined(PLATFORM_rpi5)
    __asm__ volatile ("smc #0" : "+r"(x0) : : "memory", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
#else
    if (soma_boot_el >= 2u)
        __asm__ volatile ("smc #0" : "+r"(x0) : : "memory", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    else
        __asm__ volatile ("hvc #0" : "+r"(x0) : : "memory", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
#endif
    (void)x0;
}
#endif

static void system_reboot(void)
{
    kprint("\n[REBOOT] Sincronizando discos...\n");
    vfs_sync();
#ifdef __x86_64__
    usbdisk_flush();                                                 /* SOMA_164_T480S */
    (void)acpi_reboot();                                             /* FADT RESET; 8042 como respaldo */
    for (int i = 0; i < 10000; ++i) if ((inb(0x64) & 0x02) == 0) break;
    outb(0x64, 0xFE);
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idtr = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idtr));
#elif defined(__aarch64__)
    soma_psci_system_action(0x84000009ULL);                          /* PSCI SYSTEM_RESET */
#endif
    kprint("[REBOOT] Firmware no ha reiniciado: equipo detenido.\n");
    for (;;) { arch_interrupts_disable(); arch_halt(); }
}

static void system_poweroff(void)
{
    kprint("\n[POWEROFF] Sincronizando discos...\n");
    vfs_sync();
#ifdef __x86_64__
    usbdisk_flush();                                                 /* SOMA_164_T480S */
    {   int r = acpi_poweroff();
        if (r != -1) { kprint("[POWEROFF] ACPI S5 fallo ("); kprint_dec((uint32_t)-r); kprint("); probando las salidas de emulador\n"); } }
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outb(0xF4, 0x00);
#elif defined(__aarch64__)
    soma_psci_system_action(0x84000008ULL);                          /* PSCI SYSTEM_OFF */
#endif
    kprint("[POWEROFF] Firmware no ha apagado: equipo detenido.\n");
    for (;;) { arch_interrupts_disable(); arch_halt(); }
}

/* API publica: evita pasar por el parser de comandos desde un clic de la GUI. */
void shell_system_reboot(void) { system_reboot(); }
void shell_system_poweroff(void) { system_poweroff(); }

#ifdef __x86_64__
void shell_poweroff_from_button(void) { system_poweroff(); }        /* SOMA_165_T480S: boton de encendido */
#endif

static const char *pci_device_name(uint16_t vendor, uint16_t device)
{
    if (vendor == 0x8086 && device == 0x1237) return "Intel 440FX Host Bridge";
    if (vendor == 0x8086 && device == 0x7000) return "Intel PIIX3 ISA Bridge";
    if (vendor == 0x8086 && device == 0x7010) return "Intel PIIX3 IDE";
    if (vendor == 0x8086 && device == 0x7113) return "Intel PIIX4 ACPI";
    if (vendor == 0x1234 && device == 0x1111) return "QEMU Standard VGA";
    if (vendor == 0x1AF4 && (device == 0x1000 || device == 0x1041)) return "VirtIO-NET Adapter";
    if (vendor == 0x1AF4 && (device == 0x1001 || device == 0x1042)) return "VirtIO-BLK Storage";
    return "Dispositivo PCI generico";
}

void pci_scan(void)
{
    kprint("\nPCI SCAN\n--------\n");
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFF) continue;
                uint16_t dev = pci_device_id((uint8_t)bus, slot, func);
                kprint("PCI "); kprint_hex16(bus); kputc(':'); kprint_dec(slot); kputc('.'); kprint_dec(func);
                kprint(" vendor=0x"); kprint_hex16(vendor); kprint(" device=0x"); kprint_hex16(dev);
                if (vendor == 0x1AF4 && (dev == 0x1000 || dev == 0x1041)) kprint("  <-- VIRTIO-NET");
                if (vendor == 0x1AF4 && (dev == 0x1001 || dev == 0x1042)) kprint("  <-- VIRTIO-BLK");
                kprint("\n");
            }
        }
    }
}

static int pci_format_scan(char *out, uint32_t max)
{
    uint32_t pos = 0;
    fb_puts(out, max, &pos, "Dispositivos PCI detectados:\n");
    uint32_t count = 0;

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFF) continue;
                uint16_t dev = pci_device_id((uint8_t)bus, slot, func);
                count++;

                fb_puts(out, max, &pos, "  PCI ");
                fb_put_hex16(out, max, &pos, bus);
                fb_putc(out, max, &pos, ':');
                fb_put_dec(out, max, &pos, slot);
                fb_putc(out, max, &pos, '.');
                fb_put_dec(out, max, &pos, func);
                fb_puts(out, max, &pos, " [0x");
                fb_put_hex16(out, max, &pos, vendor);
                fb_puts(out, max, &pos, ":0x");
                fb_put_hex16(out, max, &pos, dev);
                fb_puts(out, max, &pos, "] ");
                fb_puts(out, max, &pos, pci_device_name(vendor, dev));

                if (vendor == 0x1AF4) {
                    uint32_t bar0 = pci_bar0((uint8_t)bus, slot, func);
                    fb_puts(out, max, &pos, " (BAR0=0x");
                    fb_put_hex32(out, max, &pos, bar0 & 0xFFFFFFFC);
                    fb_putc(out, max, &pos, ')');
                }
                fb_putc(out, max, &pos, '\n');
            }
        }
    }
    return (int)pos;
}

static void do_hexdump(const char *filename, uint32_t max_bytes, char *out_buf, uint32_t max_out)
{
    static char hbuf[4096];
    uint32_t pos = 0;
    int r = vfs_read(filename, hbuf, sizeof(hbuf));
    if (r < 0) {
        kprint("hexdump: archivo no encontrado: '"); kprint(filename); kprint("'\n");
        if (out_buf) fb_puts(out_buf, max_out, &pos, "hexdump: archivo no encontrado");
        return;
    }
    uint32_t total = (uint32_t)r;
    uint32_t limit = (max_bytes > 0 && max_bytes < total) ? max_bytes : total;
    if (limit > 128) limit = 128;

    kprint("\nHEXDUMP de '"); kprint(filename); kprint("' ("); kprint_dec(total); kprint(" bytes):\n");
    const char hexchars[] = "0123456789ABCDEF";
    for (uint32_t off = 0; off < limit; off += 16) {
        for (int sh = 28; sh >= 0; sh -= 4) kputc(hexchars[(off >> sh) & 0x0F]);
        kprint(": ");
        for (uint32_t i = 0; i < 16; ++i) {
            if (off + i < limit) {
                uint8_t b = (uint8_t)hbuf[off + i];
                kputc(hexchars[(b >> 4) & 0x0F]);
                kputc(hexchars[b & 0x0F]);
                kputc(' ');
            } else {
                kprint("   ");
            }
            if (i == 7) kputc(' ');
        }
        kprint(" |");
        for (uint32_t i = 0; i < 16 && off + i < limit; ++i) {
            uint8_t c = (uint8_t)hbuf[off + i];
            kputc((c >= 32 && c < 127) ? (char)c : '.');
        }
        kprint("|\n");
    }
    if (out_buf) fb_puts(out_buf, max_out, &pos, "[HEXDUMP OK]");
}

void show_somafetch(char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;

    size_t f_free = 0, f_used = 0, f_total = 0;
    pmm_get_stats(&f_free, &f_used, &f_total);
    uint32_t ram_tot = (uint32_t)((f_total * 4096) / (1024 * 1024));
    uint32_t ram_usd = (uint32_t)((f_used * 4096) / (1024 * 1024));
    uint32_t ram_pct = f_total ? (uint32_t)((f_used * 100) / f_total) : 0;

    size_t h_used = 0, h_free = 0;
    kheap_stats(&h_used, &h_free);

    kprint("\n");
    kprint("\033[1;36m  ____  ____  __  __    _      \033[1;33mOS:     \033[1;37mSOMA v0.2 \033[0;36m(Multi-Agente)\033[0m\n");
    kprint("\033[1;36m / ___|/ __ \\|  \\/  |  / \\     \033[1;33mKernel: \033[1;37m" MYOS_ARCH_NAME " Bare-Metal \033[1;32m(HAL v1)\033[0m\n");
    kprint("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37m");
    if (h > 0) { kprint_dec(h); kprint("h "); }
    if (m > 0 || h > 0) { kprint_dec(m); kprint("m "); }
    kprint_dec(s);
#ifdef __aarch64__
    kprint("s \033[0;32m(timer 1 kHz)\033[0m\n");
#else
    kprint("s \033[0;32m(PIT 1 kHz)\033[0m\n");
#endif
#ifdef __aarch64__
    kprint("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mEL1 \033[1;35m(MMU + caches, " SOMA_PLATFORM_NAME ")\033[0m\n");
#else
    kprint("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mRing 0 \033[1;35m(CR0:WP, NXE)\033[0m\n");
#endif
    kprint("\033[1;36m |____/\\____/|_|  |_/_/   \\_\\  \033[1;33mRAM:    \033[1;37m");
    kprint_dec(ram_usd); kprint("/"); kprint_dec(ram_tot); kprint(" MiB \033[1;32m("); kprint_dec(ram_pct); kprint("% PMM)\033[0m\n");
    kprint("                               \033[1;33mHeap:   \033[1;37m");
    kprint_dec((uint32_t)(h_free / 1024)); kprint(" KiB libres\033[0m\n");
#if defined(__aarch64__) && !SOMA_HAVE_VIRTIO
    kprint("                               \033[1;33mDisco:  \033[1;37mRamFS + SD / USB (comando 'blk')\033[0m\n");
#if defined(PLATFORM_rpi5)
    kprint(gem_present() ? "                               \033[1;33mRed:    \033[1;37mEthernet del RP1 (GEM)\033[0m\n"
                         : "                               \033[1;33mRed:    \033[1;37msin interfaz (la Ethernet esta en el RP1)\033[0m\n");
#else
    kprint("                               \033[1;33mRed:    \033[1;37msin interfaz (la Ethernet esta en el RP1)\033[0m\n");
#endif
#else
    kprint("                               \033[1;33mDisco:  \033[1;37m"); kprint(sysdisk_describe()); kprint("\033[0m\n");   /* SOMA_162 */
    kprint("                               \033[1;33mRed:    \033[1;37m"); kprint(net_describe()); kprint("\033[0m\n");
#endif
    kprint("                               \033[1;33mAgente: \033[1;37mnail-35b (ReAct LAN)\033[0m\n\n");

    fb_puts(out_buf, max_out, &pos, "SOMA v0.2 Operational");
}

static void spawn_job_worker(void *arg)
{
    struct bg_job *job = (struct bg_job *)arg;
    if (!job) return;

    char *out_buf = (char *)kmalloc(2048);
    if (out_buf) {
        out_buf[0] = '\0';
        if (job->agent_origin) {
            dispatch_agent_command(job->cmd, out_buf, 2048);
        } else {
            dispatch_command(job->cmd, out_buf, 2048);
        }
        if (out_buf[0] != '\0') {
            uint32_t len = 0; while (out_buf[len]) len++;
            vfs_write(job->log_file, out_buf, len);
        }
        kfree(out_buf);
    }

    kprint("\n[JOB TID "); kprint_dec(job->tid); kprint(" FINALIZADO -> "); kprint(job->log_file); kprint("]\nsoma> ");
    kfree(job);
}

/* ====================================================================
 * MANEJADORES DE COMANDO DECLARATIVOS (cmd_handler_t)
 * ==================================================================== */

static int cmd_help(const char *args, char *out_buf, uint32_t max_out);

static int cmd_somafetch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    show_somafetch(out_buf, max_out);
    return 1;
}

static int cmd_creador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("SOMA (Sistema Operativo Multi-Agente) - Creado por Marcos\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "SOMA - Creado por Marcos");
    return 1;
}

/* PARCHE_FEEDBACK_V1: status con datos al agente */
static int cmd_status(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("ARCH: "); kprint(MYOS_ARCH_NAME);
    kprint(" (HAL v1) | IP: "); kprint_ip(net_ip);
    kprint(" | GW: "); kprint_ip(net_gateway);
    kprint(" | MAC: "); kprint_mac(net_mac);
    kprint("\n");
    uint32_t p = 0;
    fb_puts(out_buf, max_out, &p, "ARCH: ");
    fb_puts(out_buf, max_out, &p, MYOS_ARCH_NAME);
    fb_puts(out_buf, max_out, &p, " | Red: "); fb_puts(out_buf, max_out, &p, net_describe()); fb_puts(out_buf, max_out, &p, " | Disco: "); fb_puts(out_buf, max_out, &p, sysdisk_describe());
    return 1;
}

/* PARCHE_FEEDBACK_V1: uptime real al agente */
/* PARCHE 032: "hw" describe el hardware (ACPI en x86, PCI en ambos). Solo lectura. */
/* SOMA_156_T480S: el acceso a memoria fisica (antes hw_map_identity, solo el primer GiB) esta en hwscan.c: hw_map_phys */

__attribute__((unused)) static const char *hw_pci_class(uint8_t c, uint8_t sc)
{
    switch (c) {
    case 0x01: return sc == 0x06 ? "SATA" : sc == 0x08 ? "NVMe" : sc == 0x00 ? "SCSI/virtio-blk" : "almacenamiento";
    case 0x02: return "red";
    case 0x03: return "video";
    case 0x04: return sc == 0x03 ? "audio HDA" : "multimedia";
    case 0x06: return sc == 0x00 ? "host bridge" : sc == 0x01 ? "ISA bridge" : "bridge";
    case 0x0C: return sc == 0x03 ? "USB" : "bus serie";
    default:   return "otro";
    }
}

#if defined(__aarch64__)
extern uint64_t soma_dtb_addr;                 /* boot.s: x0 al arrancar */
static int hw_dtb_ok(const uint8_t *c)
{
    if (!(c[0] == 0xD0 && c[1] == 0x0D && c[2] == 0xFE && c[3] == 0xED)) return 0;
    uint32_t ts = (uint32_t)c[4] << 24 | (uint32_t)c[5] << 16 | (uint32_t)c[6] << 8 | c[7];
    uint32_t ver = (uint32_t)c[20] << 24 | (uint32_t)c[21] << 16 | (uint32_t)c[22] << 8 | c[23];
    return ts >= 64 && ts <= 0x100000 && ver >= 16 && ver <= 17;
}

/* x0 del cargador; si QEMU no lo entrega al arrancar un ELF, el DTB queda en la RAM (normalmente en su base):
 * se busca a 4 KiB de paso en los primeros 192 MiB (RAM de QEMU virt, accesible con la MMU apagada). */
static const uint8_t *hw_dtb(void)
{
    const uint8_t *c = (const uint8_t *)(uintptr_t)soma_dtb_addr;
    if (soma_dtb_addr >= 0x40000000ULL && soma_dtb_addr < 0x40000000ULL + (192ULL << 20) && hw_dtb_ok(c)) return c;
    for (uint64_t a = 0x40000000ULL; a < 0x40000000ULL + (192ULL << 20); a += 4096) {
        c = (const uint8_t *)(uintptr_t)a;
        if (hw_dtb_ok(c)) return c;
    }
    return 0;
}
#endif

/* Capacidades PCI: PM, MSI, MSI-X, PCIe (lista enlazada en el espacio de configuracion; recorrido acotado). */
__attribute__((unused)) static void hw_pci_caps(char *b, uint32_t max, uint32_t *p, uint8_t bus, uint8_t slot, uint8_t fn)
{
    if (!(pci_config_read32(bus, slot, fn, 0x04) & 0x00100000u)) return;      /* estado bit 4: hay lista de capacidades */
    uint8_t ptr = (uint8_t)(pci_config_read32(bus, slot, fn, 0x34) & 0xFC);
    fb_puts(b, max, p, "       caps:");
    for (int i = 0; i < 24 && ptr >= 0x40; i++) {
        uint32_t h = pci_config_read32(bus, slot, fn, ptr);
        uint8_t id = (uint8_t)h;
        const char *n = id == 0x01 ? "PM" : id == 0x05 ? "MSI" : id == 0x09 ? "vendor" : id == 0x10 ? "PCIe" : id == 0x11 ? "MSI-X" : 0;
        fb_putc(b, max, p, ' ');
        if (n) fb_puts(b, max, p, n); else { fb_puts(b, max, p, "0x"); fb_put_hex16(b, max, p, id); }
        ptr = (uint8_t)((h >> 8) & 0xFC);
    }
    fb_putc(b, max, p, '\n');
    if (pci_vendor_id(bus, slot, fn) == 0x1AF4) {                           /* PARCHE 034: estructuras virtio-pci */
        uint8_t cfg[256];
        for (int i = 0; i < 64; i++) { uint32_t w = pci_config_read32(bus, slot, fn, (uint8_t)(i * 4)); cfg[i * 4] = (uint8_t)w; cfg[i * 4 + 1] = (uint8_t)(w >> 8); cfg[i * 4 + 2] = (uint8_t)(w >> 16); cfg[i * 4 + 3] = (uint8_t)(w >> 24); }
        struct hw_virtio_cap vc[8]; int nv = hw_virtio_caps(cfg, vc, 8);
        static const char *const vn[] = { "?", "common", "notify", "isr", "device", "pcicfg" };
        for (int i = 0; i < nv; i++) {
            fb_puts(b, max, p, "       virtio "); fb_puts(b, max, p, vn[vc[i].type]); fb_puts(b, max, p, ": BAR"); fb_put_dec(b, max, p, vc[i].bar);
            fb_puts(b, max, p, " +0x"); fb_put_hex32(b, max, p, vc[i].offset); fb_puts(b, max, p, " len "); fb_put_dec(b, max, p, vc[i].length);
            if (vc[i].type == HW_VIRTIO_NOTIFY) { fb_puts(b, max, p, " mult "); fb_put_dec(b, max, p, vc[i].notify_mult); }
            fb_putc(b, max, p, '\n');
        }
    }
}

static int cmd_hw(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    int verbose = args && args[0] == '-' && args[1] == 'v';
    (void)verbose;
    static char b[6144];
    uint32_t p = 0;
#if defined(__x86_64__)
    /* SOMA_156_T480S: secciones para fotografiar en el portatil: hw cpu | mem [-v] | smbios | acpi | pci [-v] | todo.
     * Sin argumentos: resumen (equipo, CPU, RAM, pantalla) + ACPI + PCI, como antes. */
    int only_acpi = args && args[0] == 'a' && args[1] == 'c';
    int only_pci = args && args[0] == 'p' && args[1] == 'c';
    if (only_pci && args[2] == 'i' && args[3] == ' ' && args[4] == '-' && args[5] == 'v') verbose = 1;
    if (args && *args && !only_acpi && !only_pci && !verbose) {
        static char sb[8192];
        int todo = args[0] == 't' && args[1] == 'o' && args[2] == 'd';
        if (todo) {
            uint32_t q = 0; const char *secs[] = { "smbios", "cpu", "mem" };
            for (int i = 0; i < 3; i++) { hw_section_text(secs[i], sb + q, sizeof sb - q); while (sb[q]) q++; }
        } else if (!hw_section_text(args, sb, sizeof sb)) {
            kprint("Uso: hw [cpu | mem [-v] | smbios | acpi | pci [-v] | todo]\n");
            return 1;
        }
        kprint(sb);
        if (!todo) { if (out_buf && max_out) out_buf[0] = 0; return 1; }
        only_acpi = 0; only_pci = 0;
    } else if (!only_acpi && !only_pci) {
        static char sb[2048];
        hw_section_text("resumen", sb, sizeof sb);
        fb_puts(b, sizeof b, &p, sb);
    }
    uint64_t rsdp = hw_find_rsdp();    /* SOMA_156_T480S: tablas por encima de 1 GiB accesibles (UEFI) */
    static struct hw_acpi a;
    if (only_pci) { /* nada */ } else
    if (rsdp && hw_acpi_parse(rsdp, hw_map_phys, 0, &a) == 0) {
        fb_puts(b, sizeof b, &p, "ACPI: rev "); fb_put_dec(b, sizeof b, &p, a.revision);
        fb_puts(b, sizeof b, &p, ", OEM "); fb_puts(b, sizeof b, &p, a.oem);
        fb_puts(b, sizeof b, &p, ", tablas:");
        for (int i = 0; i < a.ntables && i < 32; i++) { fb_putc(b, sizeof b, &p, ' '); fb_puts(b, sizeof b, &p, a.sig[i]); }
        fb_puts(b, sizeof b, &p, "\nCPU: "); fb_put_dec(b, sizeof b, &p, (uint64_t)a.ncpu);
        fb_puts(b, sizeof b, &p, " | LAPIC 0x"); fb_put_hex32(b, sizeof b, &p, a.lapic_addr);
        for (int i = 0; i < a.nioapic; i++) { fb_puts(b, sizeof b, &p, " | IOAPIC 0x"); fb_put_hex32(b, sizeof b, &p, a.ioapic_addr[i]); }
        for (int i = 0; i < a.nmcfg; i++) {
            fb_puts(b, sizeof b, &p, "\nPCIe ECAM 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(a.mcfg[i].base >> 32));
            fb_put_hex32(b, sizeof b, &p, (uint32_t)a.mcfg[i].base);
            fb_puts(b, sizeof b, &p, " buses "); fb_put_dec(b, sizeof b, &p, a.mcfg[i].bus0); fb_putc(b, sizeof b, &p, '-'); fb_put_dec(b, sizeof b, &p, a.mcfg[i].bus1);
        }
        for (int i = 0; i < a.nirq_ovr; i++) {
            fb_puts(b, sizeof b, &p, "\nIRQ "); fb_put_dec(b, sizeof b, &p, a.irq_ovr[i].src); fb_puts(b, sizeof b, &p, " -> GSI "); fb_put_dec(b, sizeof b, &p, a.irq_ovr[i].gsi);
        }
        fb_puts(b, sizeof b, &p, a.has_hpet ? "\nHPET: si\n" : "\nHPET: no\n");
    } else {
        fb_puts(b, sizeof b, &p, "ACPI: no disponible (arranque sin tablas ACPI)\n");
    }
    if (only_acpi) goto hw_done;
#else
    {
        const uint8_t *dtb = hw_dtb();
        static struct hw_fdt f;
        if (dtb && hw_fdt_parse(dtb, 0x100000, &f) == 0) {
            fb_puts(b, sizeof b, &p, "FDT: "); fb_puts(b, sizeof b, &p, f.model[0] ? f.model : "(sin modelo)");
            fb_puts(b, sizeof b, &p, " [");  fb_puts(b, sizeof b, &p, f.compat); fb_puts(b, sizeof b, &p, "]\nCPU: ");
            fb_put_dec(b, sizeof b, &p, (uint64_t)f.ncpu); fb_puts(b, sizeof b, &p, " | nodos: "); fb_put_dec(b, sizeof b, &p, (uint64_t)f.nnodes);
            for (int i = 0; i < f.nmem; i++) {
                fb_puts(b, sizeof b, &p, "\nRAM 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(f.mem_base[i] >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)f.mem_base[i]);
                fb_puts(b, sizeof b, &p, " + "); fb_put_dec(b, sizeof b, &p, f.mem_size[i] >> 20); fb_puts(b, sizeof b, &p, " MiB");
            }
            static const char *const dev[][2] = { { "arm,pl011", "UART PL011" }, { "arm,pl031", "RTC PL031" }, { "arm,cortex-a15-gic", "GICv2" }, { "arm,gic-400", "GICv2" }, { "arm,gic-v3", "GICv3" }, { "virtio,mmio", "virtio-mmio" }, { "pci-host-ecam-generic", "PCIe ECAM" } };
            for (unsigned i = 0; i < sizeof dev / sizeof dev[0]; i++) {
                uint64_t base, size;
                if (hw_fdt_find_reg(dtb, 0x100000, dev[i][0], &base, &size) == 0) {
                    fb_putc(b, sizeof b, &p, '\n'); fb_puts(b, sizeof b, &p, dev[i][1]); fb_puts(b, sizeof b, &p, " 0x");
                    fb_put_hex32(b, sizeof b, &p, (uint32_t)(base >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)base);
                }
            }
            fb_putc(b, sizeof b, &p, '\n');
        } else {
            fb_puts(b, sizeof b, &p, "FDT: no se encontro un arbol de dispositivos valido (x0 = 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(soma_dtb_addr >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)soma_dtb_addr); fb_puts(b, sizeof b, &p, ")\n");
        }
    }
#endif
#if defined(__x86_64__)
    fb_puts(b, sizeof b, &p, "PCI:\n");
    int n = 0;
    for (int bus = 0; bus < 256 && n < 40; bus++)
        for (int slot = 0; slot < 32 && n < 40; slot++) {
            uint16_t v0 = pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0);
            if (v0 == 0xFFFF) continue;
            int multi = (int)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0x80);
            for (int fn = 0; fn < (multi ? 8 : 1) && n < 40; fn++) {
                uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
                if (v == 0xFFFF) continue;
                uint32_t cc = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x08);
                fb_puts(b, sizeof b, &p, "  "); fb_put_dec(b, sizeof b, &p, (uint64_t)bus); fb_putc(b, sizeof b, &p, ':');
                fb_put_dec(b, sizeof b, &p, (uint64_t)slot); fb_putc(b, sizeof b, &p, '.'); fb_put_dec(b, sizeof b, &p, (uint64_t)fn);
                fb_putc(b, sizeof b, &p, ' '); fb_put_hex16(b, sizeof b, &p, v); fb_putc(b, sizeof b, &p, ':');
                fb_put_hex16(b, sizeof b, &p, pci_device_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn));
                fb_putc(b, sizeof b, &p, ' '); fb_puts(b, sizeof b, &p, hw_pci_class((uint8_t)(cc >> 24), (uint8_t)(cc >> 16)));
                {   /* SOMA_156_T480S: fabricante y nombre de lo que lleva el portatil */
                    uint16_t dv = pci_device_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
                    const char *vn = hw_vendor_name(v), *dn = hw_pci_name(v, dv);
                    if (vn || dn) { fb_puts(b, sizeof b, &p, " - "); if (vn) fb_puts(b, sizeof b, &p, vn); if (dn) { if (vn) fb_putc(b, sizeof b, &p, ' '); fb_puts(b, sizeof b, &p, dn); } }
                }
                fb_putc(b, sizeof b, &p, '\n'); n++;
                if (verbose) hw_pci_caps(b, sizeof b, &p, (uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
            }
        }
    if (n == 0) fb_puts(b, sizeof b, &p, "  (ningun dispositivo)\n");
hw_done:
#else
    fb_puts(b, sizeof b, &p, "PCI: no usado en aarch64 (los dispositivos son virtio-mmio)\n");     /* su ECAM no esta mapeada en QEMU virt */
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 037: "apic" muestra el estado del LAPIC/IOAPIC (solo lectura; no habilita ni enruta nada). */
static int cmd_apic(const char *args, char *out_buf, uint32_t max_out)
{
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    int do_route = 0, route_test = 0, route_off = 0;                  /* PARCHE 039 */
    (void)do_test; (void)do_route; (void)route_test; (void)route_off;      /* en aarch64 no hay APIC */
    if (args && args[0] == 'r' && args[1] == 'o' && args[2] == 'u' && args[3] == 't' && args[4] == 'e') {
        const char *w = args + 5; while (*w == ' ') w++;
        do_route = 1; route_test = (w[0] == 't'); route_off = (w[0] == 'o' && w[1] == 'f');
    }
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__)
    uint64_t rsdp = hw_find_rsdp();    /* SOMA_156_T480S (antes solo el primer GiB) */
    static struct hw_acpi a; static struct apic_info ai;
    if (!rsdp || hw_acpi_parse(rsdp, hw_map_phys, 0, &a) != 0 || !a.has_madt) {
        fb_puts(b, sizeof b, &p, "APIC: sin tabla MADT\n");
    } else if (apic_probe(a.lapic_addr, a.nioapic ? a.ioapic_addr[0] : 0, &ai) != 0) {
        fb_puts(b, sizeof b, &p, "APIC: no se pudo leer el LAPIC\n");
    } else {
        fb_puts(b, sizeof b, &p, "IA32_APIC_BASE 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(ai.msr_base >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)ai.msr_base);
        fb_puts(b, sizeof b, &p, (ai.msr_base & 0x800) ? " (habilitado)" : " (DESHABILITADO)"); fb_puts(b, sizeof b, &p, (ai.msr_base & 0x100) ? " BSP\n" : "\n");
        fb_puts(b, sizeof b, &p, "LAPIC id "); fb_put_dec(b, sizeof b, &p, ai.lapic_id); fb_puts(b, sizeof b, &p, " ver 0x"); fb_put_hex16(b, sizeof b, &p, (uint16_t)ai.lapic_ver);
        fb_puts(b, sizeof b, &p, " SVR 0x"); fb_put_hex16(b, sizeof b, &p, (uint16_t)ai.lapic_svr); fb_puts(b, sizeof b, &p, (ai.lapic_svr & 0x100) ? " (software: ON)" : " (software: OFF)");
        fb_puts(b, sizeof b, &p, " TPR "); fb_put_dec(b, sizeof b, &p, ai.lapic_tpr & 0xFF); fb_putc(b, sizeof b, &p, '\n');
        if (ai.ioapic_ok) {
            fb_puts(b, sizeof b, &p, "IOAPIC id "); fb_put_dec(b, sizeof b, &p, ai.io_id); fb_puts(b, sizeof b, &p, " ver 0x"); fb_put_hex16(b, sizeof b, &p, (uint16_t)ai.io_ver);
            fb_puts(b, sizeof b, &p, ", "); fb_put_dec(b, sizeof b, &p, (uint64_t)ai.nredir); fb_puts(b, sizeof b, &p, " entradas\n");
            for (int i = 0; i < ai.nredir; i++) {
                uint32_t lo = (uint32_t)ai.redir[i];
                fb_puts(b, sizeof b, &p, "  GSI "); fb_put_dec(b, sizeof b, &p, (uint64_t)i); fb_puts(b, sizeof b, &p, ": vec 0x"); fb_put_hex16(b, sizeof b, &p, (uint16_t)(lo & 0xFF));
                fb_puts(b, sizeof b, &p, (lo & 0x10000) ? " enmascarada\n" : " ACTIVA\n");
            }
        } else fb_puts(b, sizeof b, &p, "IOAPIC: no leido\n");
        if (do_test) {
            static struct apic_test at;
            int r = apic_timer_selftest(&at);
            fb_puts(b, sizeof b, &p, "Autotest temporizador LAPIC: ");
            fb_puts(b, sizeof b, &p, r == 0 ? "OK" : r == -2 ? "FALLO (interrupciones desactivadas)" : r == -3 ? "FALLO (el PIT no avanza)" : r == -4 ? "FALLO (calibracion inverosimil)" : r == -5 ? "FALLO (la interrupcion no llego)" : "FALLO");
            fb_puts(b, sizeof b, &p, " | ticks/ms (div 16): "); fb_put_dec(b, sizeof b, &p, at.ticks_per_ms);
            fb_puts(b, sizeof b, &p, " | espera: "); fb_put_dec(b, sizeof b, &p, at.irq_wait_ms); fb_puts(b, sizeof b, &p, " ms | IRQ: "); fb_put_dec(b, sizeof b, &p, at.irqs); fb_putc(b, sizeof b, &p, '\n');
        } else if (do_route) {
            static struct apic_route_cfg rc;
            rc.lapic_id = (int)ai.lapic_id;
            for (int i = 0; i < 2; i++) {
                rc.gsi[i] = i; rc.low[i] = 0; rc.level[i] = 0;                 /* ISA por defecto: GSI = IRQ, flanco, activo alto */
                for (int k = 0; k < a.nirq_ovr; k++)
                    if (a.irq_ovr[k].src == i) { rc.gsi[i] = (int)a.irq_ovr[k].gsi; rc.low[i] = (a.irq_ovr[k].flags & 3) == 3; rc.level[i] = ((a.irq_ovr[k].flags >> 2) & 3) == 3; }
                if (rc.gsi[i] >= ai.nredir) rc.gsi[i] = -1;
            }
            if (rc.gsi[0] < 0 || rc.gsi[1] < 0) fb_puts(b, sizeof b, &p, "route: GSI fuera de la tabla del IOAPIC\n");
            else if (route_off) { apic_route_off(); fb_puts(b, sizeof b, &p, "route: desactivado (IRQ0/IRQ1 vuelven al PIC)\n"); }
            else {
                int r = apic_route_on(&rc);
                if (r == -2) fb_puts(b, sizeof b, &p, "route: ya estaba activo (usa 'apic route off')\n");
                else if (r != 0) fb_puts(b, sizeof b, &p, "route: fallo al activar\n");
                else if (route_test) {
                    uint64_t t0 = timer_get_uptime_ms(); unsigned long guard = 400000000UL;
                    while (timer_get_uptime_ms() - t0 < 50 && --guard) __asm__ volatile ("pause");
                    uint32_t eoi = apic_eoi_count(); uint64_t adv = timer_get_uptime_ms() - t0;
                    apic_route_off();
                    fb_puts(b, sizeof b, &p, "Autotest enrutado IOAPIC (PIT por GSI "); fb_put_dec(b, sizeof b, &p, (uint64_t)rc.gsi[0]); fb_puts(b, sizeof b, &p, "): ");
                    fb_puts(b, sizeof b, &p, (eoi >= 30 && adv >= 30) ? "OK" : "FALLO"); fb_puts(b, sizeof b, &p, " | ticks en 50 ms de espera: "); fb_put_dec(b, sizeof b, &p, adv);
                    fb_puts(b, sizeof b, &p, " | IRQ entregadas por el LAPIC: "); fb_put_dec(b, sizeof b, &p, eoi); fb_puts(b, sizeof b, &p, " | revertido al PIC\n");
                } else fb_puts(b, sizeof b, &p, "route: ACTIVO (PIT y teclado por IOAPIC). 'apic route off' lo revierte\n");
            }
        } else fb_puts(b, sizeof b, &p, "(usa 'apic test' | 'apic route test|on|off')\n");
    }
#else
    fb_puts(b, sizeof b, &p, "apic: solo disponible en x86_64 (aarch64 usa GIC)\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 040: "ahci" detecta el controlador SATA, identifica el primer disco y lee su sector 0 (solo lectura, opt-in). */
static int cmd_ahci(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    (void)do_test;
    int do_read = args && args[0] == 'r' && args[1] == 'e' && args[2] == 'a' && args[3] == 'd';
    uint64_t rd_lba = 0;
    if (do_read) { const char *w = args + 4; while (*w == ' ') w++; while (*w >= '0' && *w <= '9') rd_lba = rd_lba * 10 + (uint64_t)(*w++ - '0'); }
    uint32_t p = 0;
#if defined(__x86_64__)
    static struct ahci_info ai;
    int r = ahci_probe(&ai);
    if (ai.found) {
        fb_puts(b, sizeof b, &p, "AHCI en PCI "); fb_put_dec(b, sizeof b, &p, ai.bus); fb_putc(b, sizeof b, &p, ':'); fb_put_dec(b, sizeof b, &p, ai.slot); fb_putc(b, sizeof b, &p, '.'); fb_put_dec(b, sizeof b, &p, ai.fn);
        fb_puts(b, sizeof b, &p, " ABAR 0x"); fb_put_hex32(b, sizeof b, &p, ai.abar); fb_puts(b, sizeof b, &p, " version 0x"); fb_put_hex32(b, sizeof b, &p, ai.vs);
        fb_puts(b, sizeof b, &p, " puertos implementados: "); fb_put_dec(b, sizeof b, &p, (uint64_t)ai.nports_impl); fb_puts(b, sizeof b, &p, " | CAP: ranuras "); fb_put_dec(b, sizeof b, &p, ((ai.cap >> 8) & 0x1F) + 1);
        fb_puts(b, sizeof b, &p, (ai.cap >> 31) ? ", 64 bits\n" : ", 32 bits\n");
        for (int n = 0; n < 32; n++)
            if (ai.pi & (1u << n)) { fb_puts(b, sizeof b, &p, "  puerto "); fb_put_dec(b, sizeof b, &p, (uint64_t)n); fb_puts(b, sizeof b, &p, ": "); fb_puts(b, sizeof b, &p, ai.port_state[n] == 1 ? "disco ATA" : ai.port_state[n] == 2 ? "ATAPI (optico)" : ai.port_state[n] == 3 ? "otro" : "sin dispositivo"); fb_putc(b, sizeof b, &p, '\n'); }
    }
    if (r == 0) {
        fb_puts(b, sizeof b, &p, "Disco (puerto "); fb_put_dec(b, sizeof b, &p, (uint64_t)ai.port); fb_puts(b, sizeof b, &p, "): "); fb_puts(b, sizeof b, &p, ai.model);
        fb_puts(b, sizeof b, &p, " | "); fb_put_dec(b, sizeof b, &p, ai.sectors / 2048); fb_puts(b, sizeof b, &p, " MiB"); fb_puts(b, sizeof b, &p, ai.lba48 ? " | LBA48" : " | LBA28");
        fb_puts(b, sizeof b, &p, "\nSector 0 leido por DMA: ");
        for (int i = 0; i < 16; i++) fb_putc(b, sizeof b, &p, (ai.sector0[i] >= 32 && ai.sector0[i] < 127) ? (char)ai.sector0[i] : '.');
        fb_puts(b, sizeof b, &p, "\nAHCI: OK\n");
        if (do_test) {
            static struct ahci_selftest st;
            int tr = ahci_selftest(&st);
            fb_puts(b, sizeof b, &p, "Autotest E/S: ");
            if (!st.allowed) fb_puts(b, sizeof b, &p, "OMITIDO (solo se escribe en el disco de prueba QEMU con firma SOMASATA)\n");
            else {
                fb_puts(b, sizeof b, &p, tr == 0 ? "OK" : "FALLO");
                fb_puts(b, sizeof b, &p, " | escritura "); fb_puts(b, sizeof b, &p, st.wrote_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", verificacion "); fb_puts(b, sizeof b, &p, st.verify_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", restauracion "); fb_puts(b, sizeof b, &p, st.restore_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", sector 0 intacto "); fb_puts(b, sizeof b, &p, st.sector0_ok ? "si" : "NO");
                fb_puts(b, sizeof b, &p, "\nLectura secuencial: "); fb_put_dec(b, sizeof b, &p, st.read_kib); fb_puts(b, sizeof b, &p, " KiB en "); fb_put_dec(b, sizeof b, &p, st.read_ms); fb_puts(b, sizeof b, &p, " ms");
                if (tr != 0) { fb_puts(b, sizeof b, &p, " | "); fb_puts(b, sizeof b, &p, ahci_err_str(st.err)); }
                fb_putc(b, sizeof b, &p, '\n');
            }
        } else if (do_read) {
            static uint8_t sec[512];
            int rr = ahci_read(rd_lba, 1, sec);
            if (rr != 0) { fb_puts(b, sizeof b, &p, "read: "); fb_puts(b, sizeof b, &p, ahci_err_str(rr)); fb_putc(b, sizeof b, &p, '\n'); }
            else {
                const char hx[] = "0123456789ABCDEF";
                for (int row = 0; row < 4; row++) {
                    fb_puts(b, sizeof b, &p, "  LBA "); fb_put_dec(b, sizeof b, &p, rd_lba); fb_putc(b, sizeof b, &p, '+'); fb_put_dec(b, sizeof b, &p, (uint64_t)row * 16); fb_puts(b, sizeof b, &p, ": ");
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, hx[v >> 4]); fb_putc(b, sizeof b, &p, hx[v & 15]); fb_putc(b, sizeof b, &p, ' '); }
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                    fb_putc(b, sizeof b, &p, '\n');
                }
            }
        } else fb_puts(b, sizeof b, &p, "(usa 'ahci test' | 'ahci read <lba>')\n");
    } else {
        fb_puts(b, sizeof b, &p, "AHCI: FALLO - "); fb_puts(b, sizeof b, &p, ahci_err_str(r)); fb_putc(b, sizeof b, &p, '\n');
    }
#else
    fb_puts(b, sizeof b, &p, "ahci: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 043: "nvme" inicializa el controlador NVMe, identifica el disco y lee su sector 0; nvme test | nvme read <lba>. */
static int cmd_nvme(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__)
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    int do_read = args && args[0] == 'r' && args[1] == 'e' && args[2] == 'a' && args[3] == 'd';
    uint64_t rd_lba = 0;
    if (do_read) { const char *w = args + 4; while (*w == ' ') w++; while (*w >= '0' && *w <= '9') rd_lba = rd_lba * 10 + (uint64_t)(*w++ - '0'); }
    static struct nvme_info ni;
    int r = nvme_probe(&ni);
    if (ni.found) {
        fb_puts(b, sizeof b, &p, "NVMe en PCI "); fb_put_dec(b, sizeof b, &p, ni.bus); fb_putc(b, sizeof b, &p, ':'); fb_put_dec(b, sizeof b, &p, ni.slot); fb_putc(b, sizeof b, &p, '.'); fb_put_dec(b, sizeof b, &p, ni.fn);
        fb_puts(b, sizeof b, &p, " BAR0 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(ni.bar >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)ni.bar);
        fb_puts(b, sizeof b, &p, " version 0x"); fb_put_hex32(b, sizeof b, &p, ni.vs); fb_puts(b, sizeof b, &p, " | colas hasta "); fb_put_dec(b, sizeof b, &p, ni.mqes); fb_putc(b, sizeof b, &p, '\n');
    }
    if (r == 0) {
        fb_puts(b, sizeof b, &p, "Disco: "); fb_puts(b, sizeof b, &p, ni.model); fb_puts(b, sizeof b, &p, " (serie "); fb_puts(b, sizeof b, &p, ni.serial); fb_puts(b, sizeof b, &p, ") | ");
        fb_put_dec(b, sizeof b, &p, ni.sectors / 2048); fb_puts(b, sizeof b, &p, " MiB | sector de "); fb_put_dec(b, sizeof b, &p, ni.lba_size);
        fb_puts(b, sizeof b, &p, "\nSector 0 leido por DMA: ");
        for (int i = 0; i < 16; i++) fb_putc(b, sizeof b, &p, (ni.sector0[i] >= 32 && ni.sector0[i] < 127) ? (char)ni.sector0[i] : '.');
        fb_puts(b, sizeof b, &p, "\nNVMe: OK\n");
        if (do_test) {
            static struct nvme_selftest st;
            int tr = nvme_selftest(&st);
            fb_puts(b, sizeof b, &p, "Autotest E/S: ");
            if (!st.allowed) fb_puts(b, sizeof b, &p, "OMITIDO (solo se escribe en el disco de prueba QEMU con firma SOMANVME)\n");
            else {
                fb_puts(b, sizeof b, &p, tr == 0 ? "OK" : "FALLO");
                fb_puts(b, sizeof b, &p, " | escritura "); fb_puts(b, sizeof b, &p, st.wrote_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", verificacion "); fb_puts(b, sizeof b, &p, st.verify_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", restauracion "); fb_puts(b, sizeof b, &p, st.restore_ok ? "ok" : "NO");
                fb_puts(b, sizeof b, &p, ", sector 0 intacto "); fb_puts(b, sizeof b, &p, st.sector0_ok ? "si" : "NO");
                fb_puts(b, sizeof b, &p, "\nLectura secuencial: "); fb_put_dec(b, sizeof b, &p, st.read_kib); fb_puts(b, sizeof b, &p, " KiB en "); fb_put_dec(b, sizeof b, &p, st.read_ms); fb_puts(b, sizeof b, &p, " ms");
                if (tr != 0) { fb_puts(b, sizeof b, &p, " | "); fb_puts(b, sizeof b, &p, nvme_err_str(st.err)); }
                fb_putc(b, sizeof b, &p, '\n');
            }
        } else if (do_read) {
            static uint8_t sec[512];
            int rr = nvme_read(rd_lba, 1, sec);
            if (rr != 0) { fb_puts(b, sizeof b, &p, "read: "); fb_puts(b, sizeof b, &p, nvme_err_str(rr)); fb_putc(b, sizeof b, &p, '\n'); }
            else {
                const char hx[] = "0123456789ABCDEF";
                for (int row = 0; row < 4; row++) {
                    fb_puts(b, sizeof b, &p, "  LBA "); fb_put_dec(b, sizeof b, &p, rd_lba); fb_putc(b, sizeof b, &p, '+'); fb_put_dec(b, sizeof b, &p, (uint64_t)row * 16); fb_puts(b, sizeof b, &p, ": ");
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, hx[v >> 4]); fb_putc(b, sizeof b, &p, hx[v & 15]); fb_putc(b, sizeof b, &p, ' '); }
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                    fb_putc(b, sizeof b, &p, '\n');
                }
            }
        } else fb_puts(b, sizeof b, &p, "(usa 'nvme test' | 'nvme read <lba>')\n");
    } else {
        fb_puts(b, sizeof b, &p, "NVMe: FALLO - "); fb_puts(b, sizeof b, &p, nvme_err_str(r)); fb_putc(b, sizeof b, &p, '\n');
    }
#else
    (void)args;
    fb_puts(b, sizeof b, &p, "nvme: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 046: "usb" inicializa el controlador xHCI (USB 3), reinicia los puertos con dispositivo y lee sus descriptores. */
static void usb_hex(char *b, uint32_t m, uint32_t *p, uint32_t v, int digits)
{
    const char hx[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) fb_putc(b, m, p, hx[(v >> (4 * i)) & 15]);
}

#if defined(__x86_64__) || defined(__aarch64__)
static void usb_hid_print(char *b, uint32_t m, uint32_t *p, const struct xhci_hid_status *h)
{
    fb_puts(b, m, p, "USB HID: "); fb_puts(b, m, p, h->active ? "ACTIVO" : "inactivo");
    fb_puts(b, m, p, " | teclados "); fb_put_dec(b, m, p, h->nkbd); fb_puts(b, m, p, " | ratones "); fb_put_dec(b, m, p, h->nmouse);
    fb_puts(b, m, p, " | informes "); fb_put_dec(b, m, p, h->reports); fb_puts(b, m, p, " | scancodes "); fb_put_dec(b, m, p, h->keys);
    fb_puts(b, m, p, " | movimientos de raton "); fb_put_dec(b, m, p, h->mouse_ev); fb_puts(b, m, p, " | errores "); fb_put_dec(b, m, p, h->errors); fb_puts(b, m, p, " | recuperados "); fb_put_dec(b, m, p, h->recovered);
    fb_putc(b, m, p, '\n');
}
#endif

/* PARCHE 071: "gpio" - pines del RP1 (Pi 5) */
static int cmd_gpio(const char *args, char *out_buf, uint32_t max_out)
{
#if defined(PLATFORM_rpi5)
    (void)rp1_gpio_run(args, out_buf, max_out);
    kprint(out_buf);
#else
    (void)args; (void)max_out;
    if (out_buf) { out_buf[0] = 0; }
    kprint("gpio: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

/* PARCHE 079: "wifi" (Wi-Fi de la Pi 5, fase 1: bus SDIO y firmware) y "placa" */
static int cmd_wifi(const char *args, char *out_buf, uint32_t max_out)
{
    if (out_buf) out_buf[0] = 0;
    if (agent_dispatch_mode) {                    /* SOMA_193: un agente consulta la WiFi, pero no cambia de red (podria llevar el equipo al punto de acceso de un atacante) */
        const char *a = args ? args : ""; while (*a == ' ') a++;
        if (*a && !shell_starts_with(a, "buscar") && !shell_starts_with(a, "guardadas") && !shell_starts_with(a, "registro") && !shell_starts_with(a, "log") && !shell_starts_with(a, "firmware")) {
            kprint("[SEGURIDAD AGENTE] wifi: los agentes solo pueden consultar (wifi, wifi buscar, wifi guardadas, wifi registro).\n");
            if (out_buf && max_out) { uint32_t pp = 0; fb_puts(out_buf, max_out, &pp, "AGENT_DENIED: los agentes no pueden cambiar la conexion WiFi"); }
            return 1;
        }
    }
#ifdef __aarch64__
    wifi_command(args ? args : "");
#else
    { extern void iwl_command(const char *args); iwl_command(args ? args : ""); }   /* SOMA_175A_T480S: Intel 8265 */
#endif
    return 1;
}
/* SOMA_176A: "tls <url>": prueba el HTTPS directo (TLS 1.3 propio) sin navegador */
static int cmd_tls(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("tls", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (!args || !*args || shell_starts_with(args, "estado")) {           /* SOMA_194 */
        struct tls_trust_info ti; tls_trust_get_info(&ti);
        kprint("TLS 1.3 del nucleo: verificacion de certificados "); kprint(ti.verify ? "ACTIVA" : "DESACTIVADA (soma.tlsverificar=off): cifrado sin comprobar con quien se habla");
        kprint("\n  autoridades de confianza: "); kprint_dec(ti.roots); kprint(" de fabrica (Mozilla) + "); kprint_dec((uint32_t)ti.user); kprint(" del usuario (/etc/tls_confianza.pem)");
        kprint("\n  reloj: "); if (ti.now) { kprint("correcto (se comprueban las fechas de los certificados)"); } else kprint("SIN PONER: no se comprueban las fechas de los certificados");
        kprint("\nuso: tls <url>   (ej. tls https://example.com/)   HTTPS directo; dice por que falla un certificado\n");
        return 1;
    }
    static struct wn_req rq; static struct wn_resp rs;
    memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    rq.url = args; rq.timeout_ms = 20000; rq.max_body = 300000;
    kprint("tls: pidiendo "); kprint(args); kprint(wn_https_unverified() ? " (HTTPS directo, certificado SIN verificar: soma.tlsverificar=off)\n" : " (HTTPS directo con certificado verificado, o por la pasarela del anfitrion en QEMU)\n");
    int r = wn_fetch(&rq, &rs);
    if (r != 0) { kprint("tls: fallo: "); kprint(rs.err); const char *e = wn_tls_last_error(); if (e[0]) { kprint(" | "); kprint(e); } kprint("\n"); wn_free(&rs); return 1; }
    kprint("tls: HTTP "); kprint_dec((uint32_t)rs.status); kprint(" | "); kprint(rs.ctype); kprint(" | "); kprint_dec(rs.len); kprint(" bytes | redirecciones "); kprint_dec((uint32_t)rs.redirects); kprint(rs.via_gateway ? " | por la pasarela\n" : " | directo\n");
    char t[161]; uint32_t k = 0; for (uint32_t i = 0; i < rs.len && k < 160; i++) { char c = rs.body[i]; t[k++] = (c >= 32 && c < 127) ? c : ' '; } t[k] = 0;
    kprint("tls: "); kprint(t); kprint("\n");
    wn_free(&rs);
    return 1;
}
static int cmd_placa(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
#ifdef __aarch64__
    board_print();
#else
    kprint("placa: PC x86_64 (sin firmware de Raspberry Pi)\n");
#endif
    return 1;
}

/* PARCHE 082: "cpus" - nucleos del procesador y prueba de calculo en paralelo */
#define CPUS_BUF (256 * 1024)
static uint8_t *cpus_buf;            /* SOMA_175C: en el montón al usarse (256 KiB menos en la imagen del kernel, que no puede pasar de 14 MiB) */
struct cpus_job { uint32_t rounds; uint8_t out[16][20]; };   /* SOMA_159_T480S: 16 (antes 4, el maximo de la Pi 5: con 8 nucleos se desbordaba) */
static void cpus_part(int idx, int n, void *arg)
{
    struct cpus_job *j = (struct cpus_job *)arg;
    /* trabajo total fijo (rounds pasadas de SHA-1 sobre el buffer), repartido por trozos del buffer */
    uint32_t a = (uint32_t)((uint64_t)CPUS_BUF * (uint32_t)idx / (uint32_t)n), b = (uint32_t)((uint64_t)CPUS_BUF * (uint32_t)(idx + 1) / (uint32_t)n);
    uint8_t h[20] = { 0 };
    for (uint32_t r = 0; r < j->rounds; r++) { struct sha1_ctx c; sha1_init(&c); sha1_update(&c, h, 20); sha1_update(&c, cpus_buf + a, b - a); sha1_final(&c, h); }
    for (int i = 0; i < 20; i++) j->out[idx][i] = h[i];
}
extern uint64_t fb_parallel_rects(void);
static int cmd_cpus(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == '1' && !args[1]) { smp_set_enabled(0); kprint("cpus: solo el nucleo 0 (vuelve con 'cpus todos')\n"); return 1; }
    if (args && args[0] == 't') { smp_set_enabled(1); kprint("cpus: todos los nucleos disponibles\n"); }
    if (args && args[0] == 'p') {
        if (!cpus_buf) cpus_buf = (uint8_t *)kmalloc(CPUS_BUF);
        if (!cpus_buf) { kprint("cpus: sin memoria para la prueba\n"); return 1; }
        for (uint32_t i = 0; i < CPUS_BUF; i++) cpus_buf[i] = (uint8_t)(i * 2654435761u >> 24);
        struct cpus_job j; j.rounds = 24;
        uint32_t t1 = 0, tn = 0;
        for (int pass = 0; pass < 2; pass++) {
            int n = pass ? smp_cpus() : 1;
            if (pass && n == 1) break;
            uint64_t t0 = timer_ticks;
            int used = smp_parallel_n(cpus_part, &j, n);
            uint32_t t = (uint32_t)(timer_ticks - t0);
            if (pass) tn = t; else t1 = t;
            kprint("  "); kprint_dec((uint32_t)used); kprint(used == 1 ? " nucleo:  " : " nucleos: "); kprint_dec(t); kprint(" ms  (SHA-1 de ");
            kprint_dec(CPUS_BUF / 1024 * j.rounds / 1024); kprint(" MiB)\n");
        }
        if (smp_cpus() > 1 && tn) { uint32_t x10 = t1 * 10 / tn; kprint("  aceleracion: x"); kprint_dec(x10 / 10); kprint(","); kprint_dec(x10 % 10); kprint("\n"); }
        else if (smp_cpus() == 1) kprint("  (solo hay un nucleo disponible)\n");
        return 1;
    }
    kprint("Nucleos: "); kprint_dec((uint32_t)smp_cpus()); kprint(" en uso | "); kprint(smp_method()); kprint(smp_enabled() ? "\n" : " | reparto apagado ('cpus todos')\n");
#ifdef __x86_64__
    /* SOMA_159_T480S: hasta 16 nucleos; el identificador es el del LAPIC (en el T480s: 0..7, dos hilos por nucleo fisico) */
    kprint("  el firmware declara "); kprint_dec((uint32_t)smp_found()); kprint(" procesadores");
    if (smp_cpus() > 1) { kprint(" | trampolin 0x"); kprint_hex32(smp_tramp_addr()); }
    else { kprint(" | "); kprint(smp_why()); }
    kprint("\n");
    for (int i = 0; i < 16; i++) {
        struct smp_core_info c;
        if (smp_core_info(i, &c) != 0) break;
        if (i > 0 && !c.online) continue;
        kprint("  nucleo "); if (i < 10) kprint(" "); kprint_dec((uint32_t)i); kprint(i == 0 ? "  sistema + calculo" : "  calculo          ");
        kprint("  APIC "); kprint_dec(c.mpidr); kprint("  trabajos "); kprint_dec((uint32_t)c.jobs); kprint("  ocupado "); kprint_dec((uint32_t)c.busy_ticks); kprint(" ms");
        if (i > 0) { kprint("  despertares "); kprint_dec((uint32_t)smp_core_wakes(i)); kprint("  arranque "); kprint_dec(smp_core_boot_ms(i)); kprint(" ms"); }
        kprint("\n");
    }
#else
    for (int i = 0; i < 4; i++) {
        struct smp_core_info c;
        if (smp_core_info(i, &c) != 0) break;
        if (i > 0 && !c.online) continue;
        kprint("  nucleo "); kprint_dec((uint32_t)i); kprint(i == 0 ? "  sistema + calculo" : "  calculo          ");
        kprint("  MPIDR 0x"); kprint_hex32(c.mpidr); kprint("  trabajos "); kprint_dec((uint32_t)c.jobs); kprint("  ocupado "); kprint_dec((uint32_t)c.busy_ticks); kprint(" ms\n");
    }
#endif
    kprint("  volcados de pantalla repartidos: "); kprint_dec((uint32_t)fb_parallel_rects()); kprint("\n");
    kprint("uso: cpus | cpus prueba | cpus 1 | cpus todos\n");
    return 1;
}

/* PARCHE 081: "dhcp" - pide direccion al router (Wi-Fi o Ethernet de una red real) */
static int cmd_dhcp(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    if (!net_init()) { kprint("dhcp: sin red\n"); return 1; }
    dhcp_run(1);
    return 1;
}

/* SOMA_170_T480S: "red": estado de la conexion (cable, movil por USB), direccion y tarjetas */
#ifdef __x86_64__
#include "e1000e.h"
#include "netmgr.h"
#endif
static int cmd_red(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    const struct netif_ops *act = netif_active();
#ifdef __x86_64__
    kprint("Red: "); kprint(netmgr_state());
#else
    kprint("Red:");
#endif
    kprint(" | interfaz "); kprint(act ? act->name : "ninguna");
    kprint(" | IP "); kprint_ip(net_ip); kprint(" puerta "); kprint_ip(net_gateway); kprint(" DNS "); kprint_ip(net_dns); kprint("\n");
#ifdef __x86_64__
    { uint32_t sw, ok, fl; netmgr_counts(&sw, &ok, &fl); kprint("  cambios de interfaz "); kprint_dec(sw); kprint(", DHCP bien "); kprint_dec(ok); kprint(", fallidos "); kprint_dec(fl); kprint("\n"); }
    e1000e_info();
    { extern void usbnet_info(void) __attribute__((weak)); if (usbnet_info) usbnet_info(); }
#endif
    kprint("uso: red | netif (interfaces) | netif usar <nombre> | dhcp | ping <ip>\n");
    return 1;
}

/* PARCHE 080: "netif" (interfaces de red) y "wpa" (criptografia de WPA2) */
static int cmd_netif(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'u' && args[1] == 's' && args[2] == 'a' && args[3] == 'r') {
        const char *n = args + 4; while (*n == ' ') n++;
        if (netif_use(n)) { memcpy(net_mac, netif_mac(), 6); kprint("netif: ahora se usa "); kprint(n); kprint(" (la IP no cambia; 'dhcp' si hace falta)\n"); }
        else { kprint("netif: "); kprint(n); kprint(" no existe o no arranca\n"); }
        return 1;
    }
    const struct netif_ops *act = netif_active();
    for (int i = 0; i < netif_count(); i++) {
        const struct netif_ops *o = netif_get(i);
        kprint(o == act ? " * " : "   "); kprint(o->name); kprint("  "); kprint(o->desc);
        kprint(netif_is_up(i) ? "  [operativa" : "  [sin arrancar");
        if (netif_is_up(i) && o->link) kprint(o->link() ? ", enlace arriba]" : ", sin enlace]"); else kprint("]");
        if (netif_is_up(i) && o->mac) { kprint("  MAC "); kprint_mac(o->mac()); }
        kprint("\n");
    }
    if (!netif_count()) kprint("netif: ninguna interfaz registrada (la red no se ha iniciado)\n");
    kprint("uso: netif | netif usar <nombre>\n");
    return 1;
}

/* SOMA_195: "llm-config" - con que modelo de lenguaje habla SOMA (solo el usuario: no es una orden de agente) */
static char llmcfg_models[4096]; static int llmcfg_nmodels;
static void llmcfg_show(const struct llm_conf *c)
{
    kprint("Modelo de lenguaje de SOMA\n");
    for (int i = 0; i < LLM_NPROV; i++) {
        uint32_t kl = 0; while (c->key[i][kl]) kl++;
        kprint(i == c->prov ? "  * " : "    "); kprint(llm_prov_name(i)); kprint(i == LLM_LOCAL ? "   " : "  ");
        kprint(llm_prov_label(i)); kprint(": "); kprint(c->base[i]);
        kprint(" | modelo: "); kprint(c->model[i][0] ? c->model[i] : "(sin elegir)");
        kprint(" | clave: "); if (kl) { kprint("puesta (termina en "); kprint(c->key[i] + (kl > 4 ? kl - 4 : 0)); kprint(")"); } else kprint(i == LLM_LOCAL ? "no hace falta" : "FALTA");
        kprint("\n");
    }
    kprint("uso: llm-config proveedor <local|openai|claude|gemini> | servidor <url o ip:puerto> | modelo <nombre o numero de la lista>\n"
           "     llm-config clave <clave de la API> | archivo-clave <ruta> | modelos (pide la lista al proveedor) | probar\n"
           "     llm-config lanzador (arrancar llama-server en el servidor de la red local) | lanzar <modelo> | parar\n");
}
static int cmd_llm_config(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    static struct llm_conf c; c = *llm_conf_get();
    while (*args == ' ') args++;
    const char *field = shell_starts_with(args, "servidor") ? "servidor" : shell_starts_with(args, "modelos") ? 0 : shell_starts_with(args, "modelo") ? "modelo" : shell_starts_with(args, "clave") ? "clave" : 0;
    if (!*args) { llmcfg_show(&c); return 1; }
    if (shell_starts_with(args, "lanzador")) {                                   /* SOMA_196: lanzador del servidor local */
        const char *a = args + 8; while (*a == ' ') a++;
        const char *lf = shell_starts_with(a, "clave") ? "clave-lanzador" : shell_starts_with(a, "servidor") ? "lanzador" : 0;
        if (lf) {
            const char *v = a; while (*v && *v != ' ') v++;
            while (*v == ' ') v++;
            if (llm_conf_set(&c, 0, lf, v) != 0) { kprint("llm-config: valor no valido\n"); return 1; }
            if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
            kprint("llm-config: guardado\n");
            return 1;
        }
        char lb[LLM_BASE_MAX]; llm_launcher_base(&c, lb, sizeof lb);
        if (shell_starts_with(a, "modelos")) {
            llmcfg_nmodels = 0;
            int n = llm_launcher_models(llmcfg_models, sizeof llmcfg_models);
            if (n < 0) { kprint(llm_last_error); kprint("\n"); return 1; }
            llmcfg_nmodels = n;
            const char *m = llmcfg_models;
            for (int i = 1; i <= n; i++) {
                kprint(i < 10 ? "   " : "  "); kprint_dec((uint32_t)i); kprint("  ");
                char one[LLM_MODEL_MAX + 1]; uint32_t k = 0; while (*m && *m != '\n' && k < LLM_MODEL_MAX) one[k++] = *m++;
                one[k] = 0; if (*m) m++;
                kprint(one); kprint("\n");
            }
            kprint(n ? "arranca uno con:  llm-config lanzar <numero o nombre>\n" : "el lanzador no encuentra modelos .gguf en el servidor\n");
            return 1;
        }
        kprint("Lanzador del servidor local: "); kprint(lb); kprint(c.launcher[0] ? "" : " (por defecto)"); kprint(" | clave: "); kprint(c.launcher_key[0] ? "puesta" : "FALTA"); kprint("\n");
        if (c.launcher_key[0]) {
            char mo[LLM_MODEL_MAX]; int st = llm_launcher_status(mo, sizeof mo);
            kprint("estado de llama-server: "); kprint(st == 1 ? "LISTO" : st == 2 ? "cargando un modelo" : st == 0 ? "parado" : "ERROR");
            if (st > 0 && mo[0]) { kprint(", modelo "); kprint(mo); }
            kprint("\n"); if (st < 0) { kprint(llm_last_error); kprint("\n"); }
        }
        kprint("uso: llm-config lanzador clave <clave que muestra soma_lanzador.py> | lanzador servidor <ip:puerto> | lanzador modelos\n"
               "     llm-config lanzar <numero o nombre> (arranca llama-server con ese modelo) | llm-config parar\n");
        return 1;
    }
    if (shell_starts_with(args, "lanzar")) {
        const char *v = args + 6; while (*v == ' ') v++;
        static char pick[LLM_MODEL_MAX];
        if (*v >= '0' && *v <= '9' && llmcfg_nmodels) {
            int idx = 0; const char *d = v; while (*d >= '0' && *d <= '9') idx = idx * 10 + (*d++ - '0');
            if (!*d && idx >= 1 && idx <= llmcfg_nmodels) {
                const char *m = llmcfg_models; uint32_t k = 0;
                for (int i = 1; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
                while (m[k] && m[k] != '\n' && k < sizeof pick - 1) { pick[k] = m[k]; k++; }
                pick[k] = 0; v = pick;
            }
        }
        if (!*v) { kprint("uso: llm-config lanzar <numero de `llm-config lanzador modelos` o nombre>\n"); return 1; }
        kprint("llm-config: arrancando llama-server con "); kprint(v); kprint(" (cargar el modelo puede tardar un par de minutos) ...\n");
        if (llm_launcher_start(v, 240000, 0)) { llmcfg_nmodels = 0; kprint("llm-config: servidor LISTO con "); kprint(v); kprint(". Queda elegido como modelo local.\n"); }
        else { kprint("llm-config: no arranco. "); kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    if (shell_starts_with(args, "parar")) {
        if (llm_launcher_stop() == 0) kprint("llm-config: llama-server parado (VRAM libre)\n"); else { kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    if (shell_starts_with(args, "proveedor")) {
        int p = llm_prov_by_name(args + 9);
        if (p < 0) { kprint("llm-config: proveedores: local, openai, claude, gemini\n"); return 1; }
        c.prov = p; llmcfg_nmodels = 0;
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (field) {
        const char *v = args; while (*v && *v != ' ') v++;
        while (*v == ' ') v++;
        if (field[0] == 'm' && *v >= '0' && *v <= '9' && llmcfg_nmodels) {            /* numero de la ultima lista */
            int idx = 0; const char *d = v; while (*d >= '0' && *d <= '9') idx = idx * 10 + (*d++ - '0');
            if (!*d && idx >= 1 && idx <= llmcfg_nmodels) {
                static char pick[LLM_MODEL_MAX]; const char *m = llmcfg_models; uint32_t k = 0;
                for (int i = 1; i < idx; i++) { while (*m && *m != '\n') m++; if (*m) m++; }
                while (m[k] && m[k] != '\n' && k < sizeof pick - 1) { pick[k] = m[k]; k++; }
                pick[k] = 0; v = pick;
            }
        }
        if (llm_conf_set(&c, c.prov, field, v) != 0) { kprint("llm-config: valor no valido\n"); return 1; }
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (shell_starts_with(args, "archivo-clave")) {                              /* la clave desde un archivo (p. ej. de un USB): no hay que teclearla */
        const char *p = args + 13; while (*p == ' ') p++;
        static char kb[LLM_KEY_MAX + 8]; int n = *p ? vfs_read(p, kb, sizeof kb - 1) : -1;
        if (n <= 0) { kprint("uso: llm-config archivo-clave <ruta de un archivo de texto que solo contiene la clave>\n"); return 1; }
        kb[n] = 0; while (n > 0 && (kb[n - 1] == '\n' || kb[n - 1] == '\r' || kb[n - 1] == ' ' || kb[n - 1] == '\t')) kb[--n] = 0;
        const char *v = kb; while (*v == ' ' || *v == '\t') v++;
        int r = llm_conf_set(&c, c.prov, "clave", v);
        memset(kb, 0, sizeof kb);
        if (r != 0) { kprint("llm-config: ese archivo no contiene una clave valida (una sola linea)\n"); return 1; }
        if (llm_conf_save(&c) != 0) kprint("llm-config: no se pudo guardar /etc/llm.conf (el cambio vale hasta reiniciar)\n");
        llmcfg_show(&c);
        return 1;
    }
    if (shell_starts_with(args, "modelos")) {
        kprint("llm-config: pidiendo la lista de modelos a "); kprint(c.base[c.prov]); kprint(" ...\n");
        int n = llm_list_models(llmcfg_models, sizeof llmcfg_models);
        if (n < 0) { llmcfg_nmodels = 0; kprint(llm_last_error); kprint("\n"); return 1; }
        llmcfg_nmodels = n;
        const char *m = llmcfg_models;
        for (int i = 1; i <= n; i++) {
            kprint(i < 10 ? "   " : i < 100 ? "  " : " "); kprint_dec((uint32_t)i); kprint("  ");
            char one[LLM_MODEL_MAX + 1]; uint32_t k = 0; while (*m && *m != '\n' && k < LLM_MODEL_MAX) one[k++] = *m++;
            one[k] = 0; if (*m) m++;
            kprint(one); kprint("\n");
        }
        kprint(n ? "elige uno con:  llm-config modelo <numero o nombre>\n" : "el proveedor no devolvio ningun modelo\n");
        return 1;
    }
    if (shell_starts_with(args, "probar")) {
        static char reply[512];
        kprint("llm-config: probando "); kprint(llm_prov_label(c.prov)); kprint(", modelo "); kprint(c.model[c.prov][0] ? c.model[c.prov] : "(sin elegir)"); kprint(" ...\n");
        uint64_t t0 = timer_get_uptime_ms();
        if (llm_chat("Responde unicamente con estas tres palabras: SOMA TEST OK", reply, sizeof reply, 60000)) {
            kprint("llm-config: FUNCIONA ("); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms). Respuesta: "); kprint(reply); kprint("\n");
        } else { kprint("llm-config: NO funciona. "); kprint(llm_last_error); kprint("\n"); }
        return 1;
    }
    llmcfg_show(&c);
    return 1;
}

#if defined(__x86_64__)
/* SOMA_202: "navegador-aislado" - el motor del navegador en un proceso de ring 3 (solo el usuario) */
static int cmd_nav_aislado(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (*args == ' ') args++;
    int on = shell_starts_with(args, "on") || shell_starts_with(args, "si"), off = shell_starts_with(args, "off") || shell_starts_with(args, "no");
    if (on || off) {
        const char *t = on ? "# Navegador de SOMA (parche 202): navegador-aislado on | off\naislado=si\n" : "# Navegador de SOMA (parche 202): navegador-aislado on | off\naislado=no\n";
        uint32_t n = 0; while (t[n]) n++;
        if (vfs_write("/etc/navegador.conf", t, n) < 0) { kprint("navegador-aislado: no se pudo guardar /etc/navegador.conf\n"); return 1; }
        kprint(on ? "navegador-aislado: ACTIVADO. " : "navegador-aislado: DESACTIVADO (el motor correra dentro del nucleo, como antes del parche 202). ");
        kprint(browser_r3_active() == on ? "\n" : "Vale desde el proximo arranque (el navegador de esta sesion ya esta en marcha).\n");
        return 1;
    }
    uint32_t st = 0, de = 0, hp = 0, sp = 0; int64_t le = 0; web_r3_stats(&st, &de, &le, &hp, &sp);
    kprint("Navegador aislado (motor web en un proceso de ring 3)\n  configurado: "); kprint(browser_r3_wanted() ? "si" : "no");
    kprint("\n  esta sesion: "); kprint(browser_r3_active() ? "AISLADO" : "el navegador aun no se ha abierto, o corre dentro del nucleo");
    kprint("\n  proceso:     "); kprint(web_r3_alive() ? "en marcha" : "parado"); kprint(", lanzado "); kprint_dec(st); kprint(" veces, caidas "); kprint_dec(de);
    kprint("\n  memoria:     monton "); kprint_dec(hp * 4u); kprint(" KiB, superficie "); kprint_dec(sp * 4u); kprint(" KiB\n");
    { uint32_t fr = 0, pl = 0, pa = 0, fh = 0, sv = 0, sl = 0; web_r3_stats2(&fr, &pl, &pa, &fh, &sv, &sl);      /* SOMA_203 */
      kprint("  fotogramas:  "); kprint_dec(fr); kprint(" pintados, ultimo "); kprint_dec(pl); kprint(" ms, media "); kprint_dec(pa); kprint(" ms, alto "); kprint_dec(fh); kprint(" px\n");
      kprint("  a salvo:     cookies y almacenamiento, "); kprint_dec(sv); kprint(" copias, la ultima de "); kprint_dec(sl); kprint(" bytes\n"); }
    kprint("uso: navegador-aislado on | off     (prueba sin interfaz: webr3 <url>)\n");
    return 1;
}
/* SOMA_202: "webr3 <url>" - carga una pagina con el motor del navegador en ring 3, sin interfaz, y dice que salio */
static int cmd_webr3(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (*args == ' ') args++;
    if (agent_net_denied("webr3", args, out_buf, max_out)) return 1;
    if (!*args) { kprint("uso: webr3 <url>   (prueba del navegador aislado: carga la pagina en el proceso de ring 3 y muestra titulo, fotogramas y memoria)\n"); return 1; }
    (void)web_r3_selftest(args);
    return 1;
}
#endif

/* SOMA_199: "membench" - coste de memcpy/memset/memmove/memcmp por tamano (ciclos por llamada, de la mejor de varias tandas) */
static int cmd_membench(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out; if (out_buf) out_buf[0] = 0;
    static const uint32_t sz[] = { 4, 8, 16, 24, 48, 64, 128, 256, 1024, 4096, 65536 };
    static uint8_t *a, *b;
    if (!a) { a = (uint8_t *)kmalloc(70000); b = (uint8_t *)kmalloc(70000); }
    if (!a || !b) { kprint("membench: sin memoria\n"); return 1; }
    for (uint32_t i = 0; i < 70000; i++) { a[i] = (uint8_t)(i * 7); b[i] = (uint8_t)(i * 7); }
    kprint("membench: ciclos por llamada (menos es mejor)\n  bytes    memcpy    memset   memmove<   memmove>    memcmp\n");
    void *(*volatile cpy)(void *, const void *, size_t) = memcpy; void *(*volatile set)(void *, int, size_t) = memset;
    void *(*volatile mov)(void *, const void *, size_t) = memmove; int (*volatile cmp)(const void *, const void *, size_t) = memcmp;
    volatile int sink = 0;
    for (uint32_t k = 0; k < sizeof sz / sizeof sz[0]; k++) {
        uint32_t n = sz[k], reps = n <= 256 ? 2000 : n <= 4096 ? 400 : 40;
        uint64_t best[5];
        for (int op = 0; op < 5; op++) {
            best[op] = ~0ull;
            for (int round = 0; round < 5; round++) {
                uint64_t t0 = arch_cycle_counter();
                for (uint32_t r = 0; r < reps; r++) {
                    uint32_t o = (r * 13) & 63;
                    if (op == 0) cpy(b + 300 + o, a + ((r * 5) & 63), n);
                    else if (op == 1) set(b + 300 + o, (int)r, n);
                    else if (op == 2) mov(a + 100 + o, a + 100 + o + 3, n);
                    else if (op == 3) mov(a + 100 + o + 3, a + 100 + o, n);
                    else sink += cmp(a + 2000, b + 68000 - n - 1 - (n & 1), n) != 0;
                }
                uint64_t dt = (arch_cycle_counter() - t0) / reps;
                if (dt < best[op]) best[op] = dt;
            }
        }
        char line[96]; uint32_t p = 0;
        for (int c = -1; c < 5; c++) {
            uint64_t v = c < 0 ? n : best[c]; char d[20]; int nd = 0;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 19);
            for (int sp = nd; sp < (c < 0 ? 7 : 10); sp++) line[p++] = ' ';
            while (nd) line[p++] = d[--nd];
        }
        line[p++] = '\n'; line[p] = 0; kprint(line);
    }
    (void)sink;
    return 1;
}

/* SOMA_193: "agente_red" - politica de destinos de red de los agentes (solo el usuario: no es una orden de agente) */
static int cmd_agente_red(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    agent_net_cfg_load();
    while (*args == ' ') args++;
    int changed = 0;
    if (shell_starts_with(args, "abierto")) { agent_net_set_mode(AGENT_NET_ABIERTO); changed = 1; }
    else if (shell_starts_with(args, "procedencia")) { agent_net_set_mode(AGENT_NET_PROCEDENCIA); changed = 1; }
    else if (shell_starts_with(args, "cerrado")) { agent_net_set_mode(AGENT_NET_CERRADO); changed = 1; }
    else if (shell_starts_with(args, "permitir ")) {
        if (agent_net_allow_add(args + 9) != 0) { kprint("agente_red: dominio no valido o lista llena (ej.: agente_red permitir wikipedia.org)\n"); return 1; }
        changed = 1;
    } else if (shell_starts_with(args, "quitar ")) {
        if (agent_net_allow_del(args + 7) != 0) { kprint("agente_red: ese dominio no esta en la lista\n"); return 1; }
        changed = 1;
    } else if (*args) { kprint("uso: agente_red | agente_red abierto|procedencia|cerrado | agente_red permitir <dominio> | agente_red quitar <dominio>\n"); return 1; }
    if (changed && agent_net_cfg_save() != 0) kprint("agente_red: no se pudo guardar /etc/agente_red.conf (el cambio vale hasta reiniciar)\n");
    int m = agent_net_mode(); uint32_t seen = 0, den = 0; agent_net_stats(&seen, &den);
    kprint("Destinos de red de los agentes: modo ");
    kprint(m == AGENT_NET_ABIERTO ? "ABIERTO (cualquier destino; sin proteccion)\n" : m == AGENT_NET_CERRADO ? "CERRADO (solo los dominios de confianza)\n"
           : "PROCEDENCIA (URL de la mision, de resultados de busqueda o de paginas ya descargadas, y dominios de confianza)\n");
    kprint("  dominios de confianza:");
    if (!agent_net_allow_get(0)) kprint(" (ninguno)");
    for (int i = 0; agent_net_allow_get(i); ++i) { kprint(" "); kprint(agent_net_allow_get(i)); }
    kprint("\n  URL registradas: "); kprint_dec(seen); kprint(" | destinos denegados: "); kprint_dec(den); kprint("\n");
    return 1;
}

/* SOMA_192: "rng" - estado del generador aleatorio del nucleo */
static int cmd_rng(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    struct rng_info ri; rng_get_info(&ri);
    int st = rng_selftest();
    uint8_t s[16]; rng_bytes(s, sizeof s);
    kprint("Generador aleatorio del nucleo (ChaCha20, clave nueva tras cada peticion)\n  fuentes de hardware: RDSEED ");
    kprint(ri.rdseed ? "si" : "no"); kprint(", RDRAND "); kprint(ri.rdrand ? "si" : "no");
    kprint((ri.rdseed || ri.rdrand) ? "\n" : "  -> solo ruido de tiempos y de red (en QEMU: -cpu max; la Pi 5 no tiene)\n");
    kprint("  siembras: "); kprint_dec(ri.reseeds); kprint(" | sucesos mezclados: "); kprint_dec(ri.events);
    kprint(" | bytes entregados: "); kprint_dec((uint32_t)ri.bytes_out);
    kprint("\n  autoprueba (vector del RFC 8439 y salidas distintas): "); kprint(st == 0 ? "correcta" : "FALLO");
    kprint("\n  muestra: "); for (int i = 0; i < 16; i++) kprint_hex8(s[i]);
    kprint("\n");
    return 1;
}

#ifdef __x86_64__
/* SOMA_192: "memprot" - repite la pasada de proteccion de memoria y muestra el resultado */
static int cmd_memprot(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    memprot_apply();
    struct memprot_info mi; memprot_get_info(&mi);
    if (mi.mode == 0) { kprint("Proteccion de memoria del nucleo: DESACTIVADA (soma.memprot=off o modo seguro)\n"); return 1; }
    kprint("Proteccion de memoria del nucleo (W^X)\n  ejecutable: solo .text, "); kprint_dec((uint32_t)mi.text_kib); kprint(" KiB");
    kprint(mi.wx_text ? "  (AVISO: parte de .text es escribible)\n" : " (ninguno escribible)\n");
    kprint("  retirado el permiso de ejecucion a "); kprint_dec((uint32_t)(mi.exec_fixed / 1024)); kprint(" MiB en "); kprint_dec(mi.sweeps); kprint(" pasadas");
    kprint("\n  ejecutable fuera de .text ahora: "); kprint_dec((uint32_t)mi.exec_left); kprint(mi.exec_left ? " KiB  <-- AVISO\n" : " KiB\n");
    kprint("  pagina 0 (punteros nulos): "); kprint(mi.mode == 2 ? "sin mapear (soma.pagina0=estricta): leer o escribir detiene el sistema\n" : "solo lectura: escribir detiene el sistema (soma.pagina0=estricta para que tambien fallen las lecturas)\n");
    return 1;
}
#endif

static void hexline(const uint8_t *b, int n) { static const char *h = "0123456789abcdef"; char t[3] = { 0, 0, 0 }; for (int i = 0; i < n; i++) { t[0] = h[b[i] >> 4]; t[1] = h[b[i] & 15]; kprint(t); } kprint("\n"); }
static int cmd_wpa(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'p' && args[1] == 's' && args[2] == 'k' && args[3] == ' ') {
        /* wpa psk <ssid> <contrasena>: la PMK, como wpa_passphrase (el SSID sin espacios en esta orden) */
        const char *p = args + 4; while (*p == ' ') p++;
        char ssid[33]; int n = 0; while (*p && *p != ' ' && n < 32) ssid[n++] = *p++; ssid[n] = 0;
        while (*p == ' ') p++;
        uint8_t pmk[32]; uint64_t t0 = timer_ticks;
        if (!n || wpa_psk_to_pmk(p, (const uint8_t *)ssid, (size_t)n, pmk)) { kprint("uso: wpa psk <ssid> <contrasena de 8 a 63 caracteres>\n"); return 1; }
        kprint("PMK de '"); kprint(ssid); kprint("' ("); kprint_dec((uint32_t)(timer_ticks - t0)); kprint(" ms): "); hexline(pmk, 32);
        return 1;
    }
    /* wpa / wpa prueba: vectores oficiales ejecutados en esta maquina */
    int ok = 1; uint8_t d[32], e[32];
    sha1("abc", 3, d);
    { static const uint8_t r[20] = { 0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d }; ok &= !memcmp(d, r, 20); }
    kprint(ok ? "SHA-1 ............ bien\n" : "SHA-1 ............ MAL\n");
    { uint8_t k[16], p[16], c[16]; for (int i = 0; i < 16; i++) { k[i] = (uint8_t)i; p[i] = (uint8_t)(i * 0x11); }
      static const uint8_t r[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
      struct aes128 a; aes128_init(&a, k); aes128_encrypt(&a, p, c); aes128_decrypt(&a, c, e);
      int g = !memcmp(c, r, 16) && !memcmp(e, p, 16); ok &= g; kprint(g ? "AES-128 .......... bien\n" : "AES-128 .......... MAL\n"); }
    { uint64_t t0 = timer_ticks; wpa_psk_to_pmk("password", (const uint8_t *)"IEEE", 4, d); uint32_t ms = (uint32_t)(timer_ticks - t0);
      static const uint8_t r[8] = { 0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef };
      int g = !memcmp(d, r, 8); ok &= g; kprint(g ? "PBKDF2 (PMK) ..... bien, " : "PBKDF2 (PMK) ..... MAL, "); kprint_dec(ms); kprint(" ms\n"); }
    { uint8_t kek[16], kd[16], w[24], u[16]; for (int i = 0; i < 16; i++) { kek[i] = (uint8_t)i; kd[i] = (uint8_t)(i * 0x11); }
      aes_key_wrap(kek, kd, 16, w);
      int g = w[0] == 0x1f && w[1] == 0xa6 && w[23] == 0xe5 && aes_key_unwrap(kek, w, 16, u) == 0 && !memcmp(u, kd, 16); ok &= g;
      kprint(g ? "AES Key Wrap ..... bien\n" : "AES Key Wrap ..... MAL\n"); }
    kprint(ok ? "wpa: criptografia de WPA2 correcta en esta maquina ('wpa psk <ssid> <clave>' calcula la PMK)\n" : "wpa: HAY FALLOS\n");
    return 1;
}

/* PARCHE 077: "fan" - ventilador de la Pi 5 */
static int cmd_fan(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
#if defined(PLATFORM_rpi5)
    while (*args == ' ') args++;
    int r = 0;
    if (args[0] == 'o' && args[1] == 'n') r = rp1_fan_set(1);
    else if (args[0] == 'o' && args[1] == 'f') r = rp1_fan_set(0);
    else if (args[0] == 'i') rp1_fan_invert();
    else if (args[0]) { kprint("uso: fan | fan on | fan off | fan inv (invierte el nivel si gira al reves)\n"); return 1; }
    if (r != 0) { kprint("fan: no hay RP1\n"); return 1; }
    kprint("fan: "); kprint(rp1_fan_state() < 0 ? "sin tocar" : rp1_fan_state() ? "en marcha" : "parado"); kprint(rp1_fan_inverted() ? " (nivel invertido)\n" : "\n");
#else
    (void)args;
    kprint("fan: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

/* PARCHE 076: "hdmi" - modo del framebuffer que dio el firmware (diagnostico en placa) */
static int cmd_hdmi(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
#ifdef __aarch64__
    if (args && args[0] == 's' && args[1] == 'w') rpi_fb_toggle_swap();   /* 'hdmi swap': rojo <-> azul */
    rpi_fb_info();
#else
    kprint("hdmi: solo en aarch64\n");
#endif
    return 1;
}

/* PARCHE 073: "eth" - estado de la Ethernet del RP1 (enlace negociado) */
static int cmd_eth(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
#if defined(PLATFORM_rpi5)
    if (!gem_present()) { kprint("eth: no hay Ethernet del RP1\n"); return 1; }
    kprint("eth: enlace ");
    if (!gem_link_up()) { kprint("abajo (sin cable o negociando)\n"); return 1; }
    kprint_dec(gem_speed_mbps()); kprint(gem_full_duplex() ? " Mb/s duplex completo" : " Mb/s semiduplex");
    { uint32_t t, r; uint64_t tb, rb; gem_stats(&t, &r, &tb, &rb);
      kprint(" | tramas tx "); kprint_dec(t); kprint(" rx "); kprint_dec(r); kprint("\n"); }
#else
    kprint("eth: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

static int cmd_usb(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[4096];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    static struct xhci_info xi;
    static struct xhci_hid_status hs;
    int hid_on = 0, hid_off = 0, hid_st = 0;
    if (args) {                                                                  /* PARCHE 063: un subcomando desconocido ya no repite la exploracion */
        const char *w0 = args; while (*w0 == ' ') w0++;
        if (*w0 && !((w0[0] == 'h' && w0[1] == 'i' && w0[2] == 'd') || (w0[0] == 'r' && w0[1] == 'e' && w0[2] == 's') || (w0[0] == 'm' && w0[1] == 's' && w0[2] == 'c'))) {
            kprint("usb: subcomando desconocido. Uso: usb | usb rescan | usb hid [on|off|status] | usb msc\n");
            if (out_buf && max_out) { uint32_t q0 = 0; out_buf[0] = 0; fb_puts(out_buf, max_out, &q0, "usb: subcomando desconocido"); }
            return 1;
        }
    }
    if (args && args[0] == 'h' && args[1] == 'i' && args[2] == 'd') {            /* usb hid [on | off | status] */
        const char *w = args + 3; while (*w == ' ') w++;
        if (w[0] == 'o' && w[1] == 'f') hid_off = 1; else if (w[0] == 's') hid_st = 1; else hid_on = 1;
    }
    if (args && args[0] == 'r' && args[1] == 'e' && args[2] == 's') {            /* usb rescan: vuelve a explorar y reactiva HID y disco */
        int rr = xhci_rescan();
        fb_puts(b, sizeof b, &p, rr == 0 ? "USB: reexploracion terminada (usa 'usb' para ver los dispositivos)\n" : rr == -1 ? "USB: ya hay una reexploracion en curso\n" : "USB: la reexploracion ha fallado\n");
        goto usb_done;
    }
    if (hid_off || hid_st) {
        if (hid_off) xhci_hid_stop();
        xhci_hid_status(&hs);
        usb_hid_print(b, sizeof b, &p, &hs);
        goto usb_done;
    }
    if (args && args[0] == 'm' && args[1] == 's' && args[2] == 'c') {            /* usb msc: disco USB (almacenamiento masivo) */
        static struct xhci_msc_info mi;
        if (!xhci_msc_ready()) {
            int pr = xhci_probe(&xi);
            int mr = pr == 0 ? xhci_msc_start(&xi, &mi) : pr;
            if (mr != 0) { fb_puts(b, sizeof b, &p, "USB MSC: FALLO - "); fb_puts(b, sizeof b, &p, mr == -1 ? "no hay disco USB (almacenamiento masivo) conectado" : xhci_err_str(mr)); fb_putc(b, sizeof b, &p, '\n'); }
        }
        if (xhci_msc_ready()) {
            const struct xhci_msc_info *ui = xhci_msc_get();
            fb_puts(b, sizeof b, &p, "USB MSC: "); fb_puts(b, sizeof b, &p, ui->vendor); fb_putc(b, sizeof b, &p, ' '); fb_puts(b, sizeof b, &p, ui->product);
            fb_puts(b, sizeof b, &p, " | "); fb_put_dec(b, sizeof b, &p, ui->sectors / 2048); fb_puts(b, sizeof b, &p, " MiB | sector 0: ");
            for (int i = 0; i < 16; i++) fb_putc(b, sizeof b, &p, (ui->sector0[i] >= 32 && ui->sector0[i] < 127) ? (char)ui->sector0[i] : '.');
            fb_puts(b, sizeof b, &p, "\nUsa 'blk' para ver usb0, 'blk read usb0 <lba>' y 'blk test'.\n");
        }
        goto usb_done;
    }
    int r;
    if (xhci_hid_active() && xhci_last() && xhci_last()->ready) {               /* con el teclado en marcha no se reinicia el controlador: se muestra la ultima exploracion */
        xi = *xhci_last(); r = 0;
        fb_puts(b, sizeof b, &p, "(ultima exploracion; 'usb rescan' para repetirla)\n");
    } else r = xhci_probe(&xi);
    if (xi.found) {
        if (xi.bus == 0xFF) fb_puts(b, sizeof b, &p, "xHCI del RP1 (fuera de PCI)");         /* PARCHE 067 */
        else { fb_puts(b, sizeof b, &p, "xHCI en PCI "); fb_put_dec(b, sizeof b, &p, xi.bus); fb_putc(b, sizeof b, &p, ':'); fb_put_dec(b, sizeof b, &p, xi.slot); fb_putc(b, sizeof b, &p, '.'); fb_put_dec(b, sizeof b, &p, xi.fn); }
        fb_puts(b, sizeof b, &p, " BAR0 0x"); fb_put_hex32(b, sizeof b, &p, (uint32_t)(xi.bar >> 32)); fb_put_hex32(b, sizeof b, &p, (uint32_t)xi.bar);
        fb_puts(b, sizeof b, &p, " | version "); fb_put_dec(b, sizeof b, &p, xi.hciversion >> 8); fb_putc(b, sizeof b, &p, '.'); usb_hex(b, sizeof b, &p, xi.hciversion & 0xFF, 2);
        fb_puts(b, sizeof b, &p, "\n  ranuras "); fb_put_dec(b, sizeof b, &p, xi.max_slots); fb_puts(b, sizeof b, &p, ", puertos "); fb_put_dec(b, sizeof b, &p, xi.max_ports);
        fb_puts(b, sizeof b, &p, ", contextos de "); fb_put_dec(b, sizeof b, &p, xi.csz64 ? 64 : 32); fb_puts(b, sizeof b, &p, " bytes, scratchpad "); fb_put_dec(b, sizeof b, &p, xi.scratch);
        if (xi.bios_owned) fb_puts(b, sizeof b, &p, ", el BIOS lo tenia y lo ha cedido");
        fb_putc(b, sizeof b, &p, '\n');
    }
    if (r == 0) {
        fb_puts(b, sizeof b, &p, "Puertos con dispositivo: "); fb_put_dec(b, sizeof b, &p, xi.n_conn); fb_puts(b, sizeof b, &p, " de "); fb_put_dec(b, sizeof b, &p, xi.n_ports);
        fb_putc(b, sizeof b, &p, '\n');
        for (int i = 0; i < xi.n_ports && i < XHCI_MAX_PORTS_INFO; i++) {
            if (!xi.port[i].ccs) continue;
            fb_puts(b, sizeof b, &p, "  puerto "); fb_put_dec(b, sizeof b, &p, xi.port[i].num); fb_puts(b, sizeof b, &p, xi.port[i].major == 3 ? " (USB3)" : " (USB2)");
            fb_puts(b, sizeof b, &p, " PORTSC 0x"); fb_put_hex32(b, sizeof b, &p, xi.port[i].portsc); fb_putc(b, sizeof b, &p, '\n');
        }
        for (int i = 0; i < xi.ndev; i++) {
            const struct usb_dev_info *d = &xi.dev[i];
            fb_puts(b, sizeof b, &p, "Puerto "); fb_put_dec(b, sizeof b, &p, d->root_port ? d->root_port : d->port);
            for (int lv = 0; lv < 5; lv++) { unsigned nb = (d->route >> (4 * lv)) & 0xF; if (!nb) break; fb_putc(b, sizeof b, &p, '.'); fb_put_dec(b, sizeof b, &p, nb); }
            if (d->err != 0) { fb_puts(b, sizeof b, &p, ": FALLO - "); fb_puts(b, sizeof b, &p, xhci_err_str(d->err)); fb_putc(b, sizeof b, &p, '\n'); if (!d->vid) continue; }
            else fb_puts(b, sizeof b, &p, ": ");
            fb_puts(b, sizeof b, &p, xhci_speed_str(d->speed)); fb_puts(b, sizeof b, &p, " | ranura "); fb_put_dec(b, sizeof b, &p, d->slot);
            fb_puts(b, sizeof b, &p, " | ID "); usb_hex(b, sizeof b, &p, d->vid, 4); fb_putc(b, sizeof b, &p, ':'); usb_hex(b, sizeof b, &p, d->pid, 4);
            fb_puts(b, sizeof b, &p, " | USB "); fb_put_dec(b, sizeof b, &p, d->bcd_usb >> 8); fb_putc(b, sizeof b, &p, '.'); usb_hex(b, sizeof b, &p, d->bcd_usb & 0xFF, 2);
            fb_puts(b, sizeof b, &p, " | EP0 "); fb_put_dec(b, sizeof b, &p, d->speed == 4 ? 512u : d->mps0); fb_puts(b, sizeof b, &p, " B");
            if (d->is_hub) {
                fb_puts(b, sizeof b, &p, " | HUB ");
                if (d->hub_ports) { fb_put_dec(b, sizeof b, &p, d->hub_ports); fb_puts(b, sizeof b, &p, " puertos"); }
                else fb_puts(b, sizeof b, &p, d->hub_note == 1 ? "no soportado (SuperSpeed o demasiado profundo)" : "no se pudo configurar");
            }
            fb_putc(b, sizeof b, &p, '\n');
            for (int k = 0; k < d->n_if && k < 4; k++) {
                const struct usb_if_info *f = &d->ifs[k];
                fb_puts(b, sizeof b, &p, "    interfaz "); fb_put_dec(b, sizeof b, &p, f->num); fb_puts(b, sizeof b, &p, ": "); fb_puts(b, sizeof b, &p, usb_class_str(f->cls, f->sub, f->proto));
                fb_puts(b, sizeof b, &p, " (clase "); usb_hex(b, sizeof b, &p, f->cls, 2); fb_putc(b, sizeof b, &p, '/'); usb_hex(b, sizeof b, &p, f->sub, 2); fb_putc(b, sizeof b, &p, '/'); usb_hex(b, sizeof b, &p, f->proto, 2); fb_putc(b, sizeof b, &p, ')');
                for (int e = 0; e < f->n_ep; e++) {
                    fb_puts(b, sizeof b, &p, " | EP 0x"); usb_hex(b, sizeof b, &p, f->ep[e].addr, 2);
                    fb_puts(b, sizeof b, &p, (f->ep[e].attr & 3) == 3 ? " interrupt" : (f->ep[e].attr & 3) == 2 ? " bulk" : (f->ep[e].attr & 3) == 1 ? " isoc" : " control");
                    fb_putc(b, sizeof b, &p, ' '); fb_put_dec(b, sizeof b, &p, f->ep[e].mps); fb_puts(b, sizeof b, &p, " B");
                }
                fb_putc(b, sizeof b, &p, '\n');
            }
        }
        fb_puts(b, sizeof b, &p, "xHCI: OK\n");
        if (hid_on) {
            int hr = xhci_hid_start(&xi, &hs);
            if (hr != 0) { fb_puts(b, sizeof b, &p, "USB HID: FALLO - "); fb_puts(b, sizeof b, &p, hr == -1 ? "no hay teclado ni raton USB HID conectado" : xhci_err_str(hr)); fb_putc(b, sizeof b, &p, '\n'); }
            else usb_hid_print(b, sizeof b, &p, &hs);
        } else if (!xhci_hid_active()) fb_puts(b, sizeof b, &p, "(usa 'usb hid' para activar el teclado/raton USB)\n");
    } else {
        fb_puts(b, sizeof b, &p, "xHCI: FALLO - "); fb_puts(b, sizeof b, &p, xhci_err_str(r)); fb_putc(b, sizeof b, &p, '\n');
    }
usb_done:;
#else
    (void)args;
    fb_puts(b, sizeof b, &p, "usb: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 045: "blk" lista los discos (virtio, AHCI, NVMe) en una capa comun; blk read <dev> <lba>; blk copy <src> <lba> <dst> <lba> <n>; blk test. */
static const char *blk_word(const char **pp, char *w, int max)
{
    const char *s = *pp; while (*s == ' ') s++;
    int n = 0; while (*s && *s != ' ' && n < max - 1) w[n++] = *s++;
    w[n] = 0; *pp = s; return n ? w : 0;
}
static uint64_t blk_num(const char **pp)
{
    const char *s = *pp; while (*s == ' ') s++;
    uint64_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint64_t)(*s++ - '0');
    *pp = s; return v;
}

static int cmd_blk(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    const char *a = args ? args : "";
    char w[16], d1[16], d2[16];
    const char *sub = blk_word(&a, w, sizeof w);
    int n = blk_scan();
    int is_read = sub && w[0] == 'r' && w[1] == 'e' && w[2] == 'a' && w[3] == 'd';
    int is_copy = sub && w[0] == 'c' && w[1] == 'o' && w[2] == 'p' && w[3] == 'y';
    int is_test = sub && w[0] == 't' && w[1] == 'e' && w[2] == 's' && w[3] == 't';
    if (n == 0) fb_puts(b, sizeof b, &p, "blk: ningun dispositivo\n");
    for (int i = 0; i < n && !is_read && !is_copy && !is_test; i++) {
        const struct blkdev *d = blk_get(i);
        fb_puts(b, sizeof b, &p, "  "); fb_puts(b, sizeof b, &p, d->name); fb_puts(b, sizeof b, &p, "  ["); fb_puts(b, sizeof b, &p, d->driver); fb_puts(b, sizeof b, &p, "]  ");
        fb_put_dec(b, sizeof b, &p, d->sectors / 2048); fb_puts(b, sizeof b, &p, " MiB  "); fb_puts(b, sizeof b, &p, d->test_disk ? "disco de PRUEBA (escritura permitida)\n" : "protegido (solo lectura)\n");
    }
    if (is_read) {
        const char *dn = blk_word(&a, d1, sizeof d1); uint64_t lba = blk_num(&a);
        static uint8_t sec[512];
        int dev = dn ? blk_find(dn) : -1;
        int r = dev >= 0 ? blk_read(dev, lba, 1, sec) : BLK_E_ARG;
        if (r != 0) { fb_puts(b, sizeof b, &p, "blk read <dev> <lba>: "); fb_puts(b, sizeof b, &p, blk_err_str(r)); fb_putc(b, sizeof b, &p, '\n'); }
        else {
            const char hx[] = "0123456789ABCDEF";
            for (int row = 0; row < 4; row++) {
                fb_puts(b, sizeof b, &p, "  "); fb_put_dec(b, sizeof b, &p, lba); fb_putc(b, sizeof b, &p, '+'); fb_put_dec(b, sizeof b, &p, (uint64_t)row * 16); fb_puts(b, sizeof b, &p, ": ");
                for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, hx[v >> 4]); fb_putc(b, sizeof b, &p, hx[v & 15]); fb_putc(b, sizeof b, &p, ' '); }
                for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; fb_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                fb_putc(b, sizeof b, &p, '\n');
            }
        }
    } else if (is_copy) {
        const char *sn = blk_word(&a, d1, sizeof d1); uint64_t sl = blk_num(&a);
        const char *dn = blk_word(&a, d2, sizeof d2); uint64_t dl = blk_num(&a); uint64_t cnt = blk_num(&a);
        int s = sn ? blk_find(sn) : -1, d = dn ? blk_find(dn) : -1;
        int r = (s >= 0 && d >= 0) ? blk_copy(s, sl, d, dl, cnt, 1) : BLK_E_ARG;
        fb_puts(b, sizeof b, &p, "blk copy <origen> <lba> <destino> <lba> <n>: "); fb_puts(b, sizeof b, &p, r == 0 ? "OK (copiado y verificado)" : blk_err_str(r)); fb_putc(b, sizeof b, &p, '\n');
    } else if (is_test) {
        static uint8_t orig[64 * 512], now[64 * 512], src[64 * 512];
        int dst = -1, srcd = blk_find("vda");
        for (int i = 0; i < n; i++) if (blk_get(i)->test_disk && blk_get(i)->sectors >= 4096) { dst = i; break; }
        if (dst < 0) fb_puts(b, sizeof b, &p, "blk test: hace falta un disco de prueba (SOMA_SATA=1, SOMA_NVME=1, SOMA_USB=1 o la tarjeta SD de pi5.py)\n");
        else {
            int r = 0;
            if (srcd >= 0) r = blk_read(srcd, 0, 64, src);
            else for (int i = 0; i < 64 * 512; i++) src[i] = (uint8_t)(i * 131 + (i >> 9) * 7 + 5);   /* sin vda: patron determinista */
            if (r == 0) r = blk_read(dst, 2048, 64, orig);
            int c = r;
            if (r == 0) c = srcd >= 0 ? blk_copy(srcd, 0, dst, 2048, 64, 1) : blk_write(dst, 2048, 64, src);
            if (c == 0 && srcd < 0) { int rr = blk_read(dst, 2048, 64, now); for (int i = 0; rr == 0 && i < 64 * 512; i++) if (now[i] != src[i]) rr = BLK_E_VERIFY; c = rr; }
            int ok = (c == 0);
            if (r == 0) { int rr = blk_write(dst, 2048, 64, orig); if (rr == 0) rr = blk_read(dst, 2048, 64, now); int same = (rr == 0); for (int i = 0; same && i < 64 * 512; i++) if (now[i] != orig[i]) same = 0; if (!same) ok = 0; }
            fb_puts(b, sizeof b, &p, srcd >= 0 ? "blk test (vda -> " : "blk test (patron -> "); fb_puts(b, sizeof b, &p, blk_get(dst)->name); fb_puts(b, sizeof b, &p, ", 32 KiB, verificado y restaurado): ");
            fb_puts(b, sizeof b, &p, ok ? "OK\n" : "FALLO\n");
        }
    }
#else
    (void)args;
    fb_puts(b, sizeof b, &p, "blk: no disponible en esta arquitectura\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 050: "fat" lee sistemas de archivos FAT12/16/32 (solo lectura) de los discos de blk: fat | fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino VFS> */
#if defined(__x86_64__) || defined(__aarch64__)
static int fat_blk_read(void *ctx, uint64_t lba, uint32_t n, void *buf) { return blk_read((int)(intptr_t)ctx, lba, n, buf); }

static int fat_open(struct fat_vol *v, const char *devname, char *b, uint32_t m, uint32_t *p)
{
    int dev = devname ? blk_find(devname) : -1;
    if (dev < 0) { fb_puts(b, m, p, "fat: dispositivo desconocido (mira 'blk')\n"); return 0; }
    struct fat_io io = { fat_blk_read, (void *)(intptr_t)dev, blk_get(dev)->sectors };
    int r = fat_mount(v, &io);
    if (r != 0) { fb_puts(b, m, p, devname); fb_puts(b, m, p, ": "); fb_puts(b, m, p, fat_err_str(r)); fb_putc(b, m, p, '\n'); return 0; }
    return 1;
}

static void fat_put_date(char *b, uint32_t m, uint32_t *p, uint16_t d, uint16_t t)
{
    uint32_t y = 1980u + (d >> 9), mo = (d >> 5) & 15u, da = d & 31u, h = t >> 11, mi = (t >> 5) & 63u;
    fb_put_dec(b, m, p, y); fb_putc(b, m, p, '-'); if (mo < 10) fb_putc(b, m, p, '0'); fb_put_dec(b, m, p, mo); fb_putc(b, m, p, '-'); if (da < 10) fb_putc(b, m, p, '0'); fb_put_dec(b, m, p, da);
    fb_putc(b, m, p, ' '); if (h < 10) fb_putc(b, m, p, '0'); fb_put_dec(b, m, p, h); fb_putc(b, m, p, ':'); if (mi < 10) fb_putc(b, m, p, '0'); fb_put_dec(b, m, p, mi);
}
#endif

static int cmd_fat(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[6144];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    const char *a = args ? args : "";
    char w[16], dn[16], path[160], dest[64];
    const char *sub = blk_word(&a, w, sizeof w);
    static struct fat_vol v;
    int is_ls = sub && w[0] == 'l' && w[1] == 's' && !w[2];
    int is_cat = sub && w[0] == 'c' && w[1] == 'a' && w[2] == 't' && !w[3];
    int is_get = sub && w[0] == 'g' && w[1] == 'e' && w[2] == 't' && !w[3];
    int n = blk_scan();
    if (!sub) {
        for (int i = 0; i < n; i++) {
            const struct blkdev *d = blk_get(i);
            fb_puts(b, sizeof b, &p, "  "); fb_puts(b, sizeof b, &p, d->name); fb_puts(b, sizeof b, &p, ": ");
            struct fat_io io = { fat_blk_read, (void *)(intptr_t)i, d->sectors };
            int r = fat_mount(&v, &io);
            if (r == 0) {
                fb_puts(b, sizeof b, &p, "FAT"); fb_put_dec(b, sizeof b, &p, (uint32_t)v.type);
                if (v.part_index) { fb_puts(b, sizeof b, &p, " (particion "); fb_put_dec(b, sizeof b, &p, (uint32_t)v.part_index); fb_putc(b, sizeof b, &p, ')'); }
                fb_puts(b, sizeof b, &p, " | "); fb_put_dec(b, sizeof b, &p, v.part_sectors / 2048); fb_puts(b, sizeof b, &p, " MiB | etiqueta '"); fb_puts(b, sizeof b, &p, v.label); fb_puts(b, sizeof b, &p, "'\n");
            } else { fb_puts(b, sizeof b, &p, fat_err_str(r)); fb_putc(b, sizeof b, &p, '\n'); }
        }
        fb_puts(b, sizeof b, &p, "Usa: fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>\n");
    } else if (is_ls || is_cat || is_get) {
        const char *d = blk_word(&a, dn, sizeof dn);
        const char *pa = blk_word(&a, path, sizeof path);
        if (!pa && is_ls) { path[0] = 0; pa = path; }
        if (!d || !pa) fb_puts(b, sizeof b, &p, "fat: faltan argumentos. Uso: fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>   (dev: ver fat)\n");
        else if (fat_open(&v, d, b, sizeof b, &p)) {
            if (is_ls) {
                static struct fat_dirent ents[48]; int total = 0;
                int got = fat_readdir(&v, pa, ents, 48, &total);
                if (got < 0) { fb_puts(b, sizeof b, &p, "fat ls: "); fb_puts(b, sizeof b, &p, fat_err_str(got)); fb_putc(b, sizeof b, &p, '\n'); }
                else {
                    for (int i = 0; i < got; i++) {
                        fb_puts(b, sizeof b, &p, "  "); fat_put_date(b, sizeof b, &p, ents[i].date, ents[i].time); fb_puts(b, sizeof b, &p, ents[i].is_dir ? "  <DIR>     " : "  ");
                        if (!ents[i].is_dir) { uint32_t pp0 = p; fb_put_dec(b, sizeof b, &p, ents[i].size); fb_puts(b, sizeof b, &p, " B"); while (p - pp0 < 12 && p + 2 < sizeof b) fb_putc(b, sizeof b, &p, ' '); }
                        fb_puts(b, sizeof b, &p, ents[i].name); fb_puts(b, sizeof b, &p, ents[i].is_dir ? "/\n" : "\n");
                    }
                    fb_put_dec(b, sizeof b, &p, (uint32_t)total); fb_puts(b, sizeof b, &p, " entradas"); if (total > got) fb_puts(b, sizeof b, &p, " (se muestran las primeras 48)"); fb_putc(b, sizeof b, &p, '\n');
                }
            } else if (is_cat) {
                static uint8_t buf[4096];
                int got = fat_read(&v, pa, 0, buf, sizeof buf);
                if (got < 0) { fb_puts(b, sizeof b, &p, "fat cat: "); fb_puts(b, sizeof b, &p, fat_err_str(got)); fb_putc(b, sizeof b, &p, '\n'); }
                else {
                    for (int i = 0; i < got; i++) { uint8_t c = buf[i]; if (c == '\n' || c == '\t' || c >= 0x80 || (c >= 32 && c < 127)) fb_putc(b, sizeof b, &p, (char)c); else fb_putc(b, sizeof b, &p, '.'); }
                    if (got == (int)sizeof buf) fb_puts(b, sizeof b, &p, "\n[... solo los primeros 4096 bytes]");
                    fb_putc(b, sizeof b, &p, '\n');
                }
            } else {
                const char *dst = blk_word(&a, dest, sizeof dest);
                struct fat_dirent st;
                char *fbuf = 0;
                int r = dst ? fat_stat(&v, pa, &st) : FAT_E_ARG;
                if (!dst) fb_puts(b, sizeof b, &p, "fat get: falta el destino\n");
                else if (agent_write_denied(dst, "fat get", 0, 0)) fb_puts(b, sizeof b, &p, "fat get: destino protegido (los agentes no pueden modificar /etc, /sys, /bin ni /boot)\n");
                else if (r != 0) { fb_puts(b, sizeof b, &p, "fat get: "); fb_puts(b, sizeof b, &p, fat_err_str(r)); fb_putc(b, sizeof b, &p, '\n'); }
                else if (st.is_dir) fb_puts(b, sizeof b, &p, "fat get: es un directorio\n");
                else if (st.size > FS_FILE_MAX) { fb_puts(b, sizeof b, &p, "fat get: "); fb_put_dec(b, sizeof b, &p, st.size); fb_puts(b, sizeof b, &p, " bytes no caben en el VFS (maximo 32 MiB)\n"); }
                else if (!(fbuf = (char *)kmalloc((size_t)st.size + 1))) fb_puts(b, sizeof b, &p, "fat get: sin memoria\n");
                else {
                    int got = fat_read(&v, pa, 0, fbuf, st.size);
                    if (got < 0) { fb_puts(b, sizeof b, &p, "fat get: "); fb_puts(b, sizeof b, &p, fat_err_str(got)); fb_putc(b, sizeof b, &p, '\n'); }
                    else if (vfs_write(dst, fbuf, (uint32_t)got) < 0) fb_puts(b, sizeof b, &p, "fat get: no se pudo escribir en el VFS\n");
                    else { fb_puts(b, sizeof b, &p, "copiados "); fb_put_dec(b, sizeof b, &p, (uint32_t)got); fb_puts(b, sizeof b, &p, " bytes a "); fb_puts(b, sizeof b, &p, dst); fb_putc(b, sizeof b, &p, '\n'); }
                }
                if (fbuf) kfree(fbuf);
            }
        }
    } else fb_puts(b, sizeof b, &p, "uso: fat | fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>\n");
#else
    (void)args;
    fb_puts(b, sizeof b, &p, "fat: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; fb_puts(out_buf, max_out, &q, b); }
    return 1;
}

static int cmd_uptime(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t sec = (uint32_t)(ms / 1000);
    kprint("Uptime: "); kprint_dec(sec); kprint("s\n");
    uint32_t p = 0;
    fb_puts(out_buf, max_out, &p, "Uptime: ");
    fb_put_dec(out_buf, max_out, &p, sec);
    fb_puts(out_buf, max_out, &p, " segundos");
    return 1;
}

/* ramstat v2: comando 'ram' - consumo real de RAM con desglose y delta */
static void ram_put_mib(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    fb_puts(out, max, p, label);
    fb_put_dec(out, max, p, mib);
    fb_puts(out, max, p, ".");
    if (frac < 10) fb_puts(out, max, p, "0");
    fb_put_dec(out, max, p, frac);
    fb_puts(out, max, p, " MiB\n");
}

static void ram_put_delta(char *out, uint32_t max, uint32_t *p, const char *label, uint64_t now, uint64_t base)
{
    fb_puts(out, max, p, label);
    if (now >= base) { fb_puts(out, max, p, "+"); fb_put_dec(out, max, p, (uint32_t)((now - base) >> 10)); }
    else             { fb_puts(out, max, p, "-"); fb_put_dec(out, max, p, (uint32_t)((base - now) >> 10)); }
    fb_puts(out, max, p, " KiB\n");
}

static int ram_arg_is(const char *a, const char *word)
{
    if (!a) return 0;
    while (*a == ' ') a++;
    while (*word) {
        if (*a != *word) return 0;
        a++; word++;
    }
    return (*a == 0 || *a == ' ');
}

/* PARCHE_TERMINAL_V2: 'ram' unifica a 'free' (alias) con un informe completo. */
static void shell_emit(const char *text, char *out_buf, uint32_t max_out)
{
    kprint(text);
    if (out_buf && max_out) {
        uint32_t p = 0;
        out_buf[0] = '\0';
        fb_puts(out_buf, max_out, &p, text);
    }
}

static void ram_put_mib_raw(char *out, uint32_t max, uint32_t *p, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);
    fb_put_dec(out, max, p, mib);
    fb_putc(out, max, p, '.');
    if (frac < 10) fb_putc(out, max, p, '0');
    fb_put_dec(out, max, p, frac);
    fb_puts(out, max, p, " MiB");
}

static void ram_put_pct10(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    fb_put_dec(out, max, p, pct10 / 10);
    fb_putc(out, max, p, '.');
    fb_put_dec(out, max, p, pct10 % 10);
    fb_puts(out, max, p, " %");
}

static void ram_put_bar(char *out, uint32_t max, uint32_t *p, uint32_t pct10)
{
    const uint32_t width = 40;
    uint32_t fill = (pct10 * width + 500) / 1000;
    if (fill > width) fill = width;
    fb_putc(out, max, p, '[');
    for (uint32_t i = 0; i < width; ++i) fb_putc(out, max, p, i < fill ? '#' : '.');
    fb_putc(out, max, p, ']');
}

static int cmd_ram(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1792];
    uint32_t p = 0;
    struct ramstat_info ri, b;
    int brief = 0, verbose = 0;

    if (!args) args = "";
    while (*args == ' ') args++;

    if (ram_arg_is(args, "base")) {
        ramstat_set_baseline();
        shell_emit("RAM: linea base guardada. Abre una aplicacion y ejecuta 'ram -v' para ver el delta.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "reset")) {
        ramstat_clear_baseline();
        shell_emit("RAM: linea base borrada.\n", out_buf, max_out);
        return 1;
    }
    if (ram_arg_is(args, "-s") || ram_arg_is(args, "short") || ram_arg_is(args, "resumen")) {
        brief = 1;
    } else if (ram_arg_is(args, "-v") || ram_arg_is(args, "full") || ram_arg_is(args, "detalle")) {
        verbose = 1;
    } else if (*args) {
        shell_emit("Uso: ram [-s | -v | base | reset]   (alias: free)\n"
                   "  -s resumen en una linea | -v desglose completo y delta\n"
                   "  base / reset: fija o borra la linea base para medir el coste de una app\n",
                   out_buf, max_out);
        return 1;
    }

    ramstat_get(&ri);
    uint32_t free10 = ri.total ? (uint32_t)((ri.free_b * 1000ULL) / ri.total) : 0;
    rep[0] = '\0';

    if (brief) {
        fb_puts(rep, sizeof(rep), &p, "RAM: ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
        fb_puts(rep, sizeof(rep), &p, " / ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
        fb_puts(rep, sizeof(rep), &p, " (");
        ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
        fb_puts(rep, sizeof(rep), &p, ") | libre ");
        ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
        fb_puts(rep, sizeof(rep), &p, " | heap libre ");
        fb_put_dec(rep, sizeof(rep), &p, ri.heap_free >> 10);
        fb_puts(rep, sizeof(rep), &p, " KiB\n");
        shell_emit(rep, out_buf, max_out);
        return 1;
    }

    fb_puts(rep, sizeof(rep), &p, "=== MEMORIA RAM ===\n");
    fb_puts(rep, sizeof(rep), &p, "Total : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    fb_puts(rep, sizeof(rep), &p, "\nUsada : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, ")\nLibre : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, free10);
    fb_puts(rep, sizeof(rep), &p, ")\n");
    ram_put_bar(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, "\nHeap del kernel: ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_used);
    fb_puts(rep, sizeof(rep), &p, " en uso | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_free);
    fb_puts(rep, sizeof(rep), &p, " libre | ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.heap_mapped);
    fb_puts(rep, sizeof(rep), &p, " mapeado\n");
    if (ri.pct10 >= 900) fb_puts(rep, sizeof(rep), &p, "AVISO: memoria casi agotada.\n");

    if (verbose) {
        fb_puts(rep, sizeof(rep), &p, "--- Desglose de la memoria comprometida ---\n");
        ram_put_mib(rep, sizeof(rep), &p, "Zona baja reservada : ", ri.lower);
        ram_put_mib(rep, sizeof(rep), &p, "  Imagen del kernel  : ", ri.lower_image);
        ram_put_mib(rep, sizeof(rep), &p, "  Reserva sin medir  : ", ri.lower_rest);
        ram_put_mib(rep, sizeof(rep), &p, "Heap mapeado        : ", ri.heap_mapped);
        ram_put_mib(rep, sizeof(rep), &p, "  En uso (kmalloc)   : ", ri.heap_used);
        ram_put_mib(rep, sizeof(rep), &p, "  Libre              : ", ri.heap_free);
        ram_put_mib(rep, sizeof(rep), &p, "Pilas/tablas/otros  : ", ri.other);
        if (ramstat_get_baseline(&b)) {
            fb_puts(rep, sizeof(rep), &p, "--- Delta desde 'ram base' ---\n");
            ram_put_delta(rep, sizeof(rep), &p, "Huella            : ", ri.footprint, b.footprint);
            ram_put_delta(rep, sizeof(rep), &p, "Heap en uso       : ", ri.heap_used, b.heap_used);
            ram_put_delta(rep, sizeof(rep), &p, "Pilas/tablas/otros: ", ri.other, b.other);
        } else {
            fb_puts(rep, sizeof(rep), &p, "(usa 'ram base' para medir el coste de una aplicacion)\n");
        }
    } else {
        fb_puts(rep, sizeof(rep), &p, "(ram -v: desglose | ram -s: resumen | ram base: medir una app)\n");
    }
    shell_emit(rep, out_buf, max_out);
    return 1;
}


/* PARCHE_HORA_LS_V1: date en CEST (UTC+2) */
static int cmd_date(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    struct rtc_time t; rtc_get_datetime(&t);

    /* Convertir UTC → CEST (UTC+2). Ajuste simple de hora/día. */
    uint32_t y = t.year, mo = t.month, d = t.day;
    uint32_t h = t.hour + 2, mi = t.min, s = t.sec;
    if (h >= 24) {
        h -= 24;
        d += 1;
        uint32_t dim = 31;
        if (mo == 4 || mo == 6 || mo == 9 || mo == 11) dim = 30;
        else if (mo == 2) dim = 28;
        if (d > dim) { d = 1; mo += 1; }
        if (mo > 12) { mo = 1; y += 1; }
    }

    char date_str[80]; uint32_t dp = 0;
    fb_puts(date_str, sizeof(date_str), &dp, "CEST: ");
    fb_put_dec(date_str, sizeof(date_str), &dp, y); fb_putc(date_str, sizeof(date_str), &dp, '-');
    if (mo < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, mo); fb_putc(date_str, sizeof(date_str), &dp, '-');
    if (d < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, d); fb_putc(date_str, sizeof(date_str), &dp, ' ');
    if (h < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, h); fb_putc(date_str, sizeof(date_str), &dp, ':');
    if (mi < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, mi); fb_putc(date_str, sizeof(date_str), &dp, ':');
    if (s < 10) fb_putc(date_str, sizeof(date_str), &dp, '0');
    fb_put_dec(date_str, sizeof(date_str), &dp, s);
    fb_puts(date_str, sizeof(date_str), &dp, " CEST");

    kprint(date_str); kprint("\n");
    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, date_str);
    return 1;
}

static int cmd_clear(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    console_clear();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Pantalla borrada.");
    return 1;
}

static int cmd_test_suite(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    boot_gate_run_test_suite(out_buf, max_out);
    return 1;
}

static int cmd_mem(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_mem();
    sysinfo_format_mem(out_buf, max_out);
    return 1;
}

/* PARCHE_FEEDBACK_V1: df reporta capacidad real al agente */
static int cmd_df(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0;
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);

    kprint("\nALMACENAMIENTO (df):\n");
    kprint("  Disco: "); kprint(sysdisk_describe()); kprint("\n");   /* SOMA_162 */
    kprint("  RamFS: "); kprint_dec(files_u); kprint("/64 inodos | ");
    kprint_dec(bytes_u / 1024); kprint(" KiB usados (max 16 KiB/archivo) | dirty=");
    kprint_dec(dirty_c); kprint("\n\n");

    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, "Disco: "); fb_puts(out_buf, max_out, &pos, sysdisk_describe()); fb_puts(out_buf, max_out, &pos, " | RamFS V2: ");
    fb_put_dec(out_buf, max_out, &pos, files_u);
    fb_puts(out_buf, max_out, &pos, "/64 inodos | ");
    fb_put_dec(out_buf, max_out, &pos, bytes_u / 1024);
    fb_puts(out_buf, max_out, &pos, " KiB usados (16 KiB/archivo) | dirty=");
    fb_put_dec(out_buf, max_out, &pos, dirty_c);
    return 1;
}

static int cmd_stats(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    sysinfo_print_stats();
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "stats ejecutado");
    return 1;
}

static int cmd_heap(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kheap_dump_stats();
    kheap_test_self();
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Heap inspeccionado");
    return 1;
}

/* SOMA_PARCHE_008_PANIC: diagnostico de fallos. 'crash' y 'hang' NO son CMD_F_SAFE:
 * el agente no puede ejecutarlos (dispatch bloquea lo no SAFE en modo agente). */
static void __attribute__((noinline)) crash_level3(volatile uint64_t *p) { *p = 0x42; }
static void __attribute__((noinline)) crash_level2(volatile uint64_t *p) { crash_level3(p); kprint(""); }
static void __attribute__((noinline)) crash_level1(volatile uint64_t *p) { crash_level2(p); kprint(""); }

static int cmd_crash(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    kprint("crash: provocando un fallo de pagina a proposito...\n");
    crash_level1((volatile uint64_t *)0x00007F0000000000ULL);   /* direccion no mapeada */
    return 1;
}

static void __attribute__((noinline)) hang_spin(uint64_t until_ms)
{
    while (timer_get_uptime_ms() < until_ms) { }
}

static int cmd_hang(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t secs = 0; while (*args >= '0' && *args <= '9') secs = secs * 10 + (uint32_t)(*args++ - '0');
    if (secs == 0) secs = 8;
    if (secs > 60) secs = 60;
    kprint("hang: reteniendo la CPU sin ceder durante "); kprint_dec(secs);
    kprint(" s (el vigilante avisa a los 5 s; solo x86_64)...\n");
    hang_spin(timer_get_uptime_ms() + (uint64_t)secs * 1000u);
    kprint("hang: terminado\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "hang terminado");
    return 1;
}

/* 'deadlock': DEPURACION (no es CMD_F_SAFE, el agente no puede ejecutarlo). Lanza dos hilos que
 * cogen dos mutex en orden opuesto para ver como los detecta kmutex_lock. Una vez por arranque. */
static struct kmutex dl_a, dl_b;
static int dl_launched = 0;
static void dl_thread1(void *arg) { (void)arg; kmutex_lock(&dl_a); thread_sleep(100); kmutex_lock(&dl_b); kmutex_unlock(&dl_b); kmutex_unlock(&dl_a); }
static void dl_thread2(void *arg) { (void)arg; kmutex_lock(&dl_b); thread_sleep(100); kmutex_lock(&dl_a); kmutex_unlock(&dl_a); kmutex_unlock(&dl_b); }

/* PARCHE 111: prueba de estres de concurrencia (mutex + heap + VFS) con seis hilos */
static int cmd_conctest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int ok = conc_selftest();
    fb_puts(out_buf, max_out, &pos, ok ? "conctest: TODO OK (detalle en la consola)" : "conctest: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_fscrash(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int ok = vfs_crash_selftest();
    ok = vfs_checkpoint_selftest() && ok;                  /* PARCHE 120 */
    fb_puts(out_buf, max_out, &pos, ok ? "fscrash: TODO OK (detalle en la consola)" : "fscrash: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_fscheck(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    int bad = vfs_fscheck();
    fb_puts(out_buf, max_out, &pos, bad == 0 ? "fscheck: disco correcto (detalle en la consola)" : "fscheck: HAY PROBLEMAS (detalle en la consola)");
    return 1;
}

/* PARCHE 117 */
static int cmd_usfuzz(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0, v[3] = { 24, 3000, 1 }; int k = 0;
    const char *p = args ? args : "";
    while (*p && k < 3) {
        while (*p == ' ') p++;
        if (*p < '0' || *p > '9') break;
        uint32_t x = 0; while (*p >= '0' && *p <= '9') { x = x * 10 + (uint32_t)(*p - '0'); p++; }
        v[k++] = x;
    }
    int ok = usermode_fuzz((int)v[0], v[1], v[2]);
    fb_puts(out_buf, max_out, &pos, ok == 1 ? "usfuzz: TODO OK (detalle en la consola)" : "usfuzz: HAY FALLOS (detalle en la consola)");
    return 1;
}

/* PARCHE 127 */
static int cmd_pmmbench(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t pos = 0;
    int ok = pmm_bench();
    fb_puts(out_buf, max_out, &pos, ok == 1 ? "pmmbench: OK (cifras en la consola)" : "pmmbench: FALLO (detalle en la consola)");
    return 1;
}

/* PARCHE 118 */
static int cmd_oomtest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t pos = 0;
    int ok = usermode_oomtest();
    ok = usermode_oomtest_mem() && ok;                    /* PARCHE 119 */
    fb_puts(out_buf, max_out, &pos, ok == 1 ? "oomtest: TODO OK (detalle en la consola)" : "oomtest: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_demo_ai(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    int vivo = args && args[0] == 'v';
    int ok = coder_demo_run(vivo);
    fb_puts(out_buf, max_out, &pos, ok ? "demo_ai: TODO OK (detalle en la consola)" : "demo_ai: HAY FALLOS (detalle en la consola)");
    return 1;
}

static int cmd_deadlock(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t pos = 0;
    if (dl_launched) {
        kprint("deadlock: ya se lanzo en este arranque (mata los hilos dl_t1/dl_t2 con 'kill <TID>')\n");
        fb_puts(out_buf, max_out, &pos, "deadlock ya lanzado");
        return 1;
    }
    dl_launched = 1;
    kmutex_init(&dl_a, "dl_A");
    kmutex_init(&dl_b, "dl_B");
    thread_create("dl_t1", dl_thread1, 0);
    thread_create("dl_t2", dl_thread2, 0);
    kprint("deadlock: lanzados dl_t1 (A->B) y dl_t2 (B->A). Mira 'mutex' y 'ps'; mata con 'kill <TID>'.\n");
    fb_puts(out_buf, max_out, &pos, "deadlock lanzado");
    return 1;
}

static int cmd_mutex(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kmutex_dump();
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Mutex inspeccionados");
    return 1;
}

static int cmd_watchdog(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint64_t now = timer_get_uptime_ms(), beat = watchdog_last_beat_ms();
    kprint("WATCHDOG: limite "); kprint_dec(WATCHDOG_LIMIT_MS);
    kprint(" ms | avisos: "); kprint_dec(watchdog_trips());
    kprint(" | ultimo latido hace "); kprint_dec((uint32_t)(now >= beat ? now - beat : 0));
    kprint(" ms\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Vigilante inspeccionado");
    return 1;
}

/* SOMA_PARCHE_020_AUDIO */
static int cmd_audio(const char *args, char *out_buf, uint32_t max_out)
{
    return audio_sys_command(args, out_buf, max_out);
}

/* SOMA_177_T480S: mic (diagnostico y grabacion por el microfono) y grabadora (la aplicacion) */
static int cmd_mic(const char *args, char *out_buf, uint32_t max_out)
{
    return micrec_command(args, out_buf, max_out);
}
static int cmd_cam(const char *args, char *out_buf, uint32_t max_out)      /* SOMA_178_T480S */
{
    return camera_command(args, out_buf, max_out);
}
static int cmd_camara(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_camera();
    fb_puts(out_buf, max_out, &p, "Camara abierta.");
    return 1;
}
static int cmd_captura(const char *args, char *out_buf, uint32_t max_out)    /* SOMA_179_T480S */
{
    return scrcap_command(args, out_buf, max_out);
}
static int cmd_grabadora(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_recorder();
    fb_puts(out_buf, max_out, &p, "Grabadora abierta.");
    return 1;
}

/* SOMA_PARCHE_018_BIGFS: almacen de archivos grandes */
static int cmd_big(const char *args, char *out_buf, uint32_t max_out)
{
    return bigfs_sys_command(args, out_buf, max_out);
}

/* SOMA_PARCHE_016_EXEC / 019_FILES: exec [-b] [-t ms] <ruta.elf> [argumentos...]  |  procs
 * La ruta puede ser del VFS pequeno (/bin/hello.elf, max 16 KiB) o del almacen grande (/big/ruta, hasta 8 MiB). */
static int cmd_exec_impl(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    int bg = 0; uint32_t ms = 0;
    for (;;) {
        while (*args == ' ') args++;
        if (args[0] == '-' && args[1] == 'b' && (args[2] == ' ' || args[2] == 0)) { bg = 1; args += 2; continue; }
        if (args[0] == '-' && args[1] == 't' && args[2] == ' ') {
            args += 2; while (*args == ' ') args++;
            while (*args >= '0' && *args <= '9') ms = ms * 10 + (uint32_t)(*args++ - '0');
            continue;
        }
        break;
    }
    if (ms == 0) ms = bg ? 60000u : 30000u;
    char path[96]; uint32_t n = 0;
    while (*args && *args != ' ' && n < sizeof(path) - 1) path[n++] = *args++;
    path[n] = 0;
    while (*args == ' ') args++;                      /* lo que queda son los argumentos del programa */
    if (n == 0) { fb_puts(out_buf, max_out, &pos, "uso: exec [-b] [-t plazo_ms] <ruta.elf> [argumentos]   (ej: exec /bin/hello.elf | exec /big/bin/prog.elf datos.bin)"); return 1; }

    char *img = 0; size_t len = 0;
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'g' && (path[4] == '/' || path[4] == 0)) {
        int e; struct bigfs *fs = bigfs_sys(&e);
        uint32_t ino; struct bigfs_stat st;
        if (!fs) { fb_puts(out_buf, max_out, &pos, "exec: BIGFS no disponible: "); fb_puts(out_buf, max_out, &pos, bigfs_strerror(e)); return 1; }
        int t = bigfs_lookup(fs, path[4] ? path + 4 : "/", &ino);
        if (t < 0 || t != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0) { fb_puts(out_buf, max_out, &pos, "exec: no se puede leer "); fb_puts(out_buf, max_out, &pos, path); return 1; }
        if (st.size > 8u * 1024 * 1024) { fb_puts(out_buf, max_out, &pos, "exec: el ejecutable supera 8 MiB"); return 1; }
        img = (char *)kmalloc(st.size ? (size_t)st.size : 1);
        if (!img) { fb_puts(out_buf, max_out, &pos, "exec: sin memoria"); return 1; }
        int64_t g = bigfs_read(fs, ino, 0, img, st.size);
        if (g != (int64_t)st.size) { kfree(img); fb_puts(out_buf, max_out, &pos, "exec: error de lectura de "); fb_puts(out_buf, max_out, &pos, path); return 1; }
        len = (size_t)st.size;
    } else {
        uint32_t ul = 0; img = vfs_read_alloc(path, &ul);   /* PARCHE 087 */
        int l = img ? (int)ul : -1;
        if (l <= 0) { if (img) kfree(img); fb_puts(out_buf, max_out, &pos, "exec: no se puede leer "); fb_puts(out_buf, max_out, &pos, path); return 1; }
        len = (size_t)l;
    }

    const char *base = path; for (const char *q = path; *q; ++q) if (*q == '/') base = q + 1;
    int err = 0;
    int idx = usermode_exec(base, img, len, ms, args, &err);
    kfree(img);
    if (idx < 0) {
        if (err != 0) { fb_puts(out_buf, max_out, &pos, "exec: ELF invalido: "); fb_puts(out_buf, max_out, &pos, elf_strerror(err)); }
        else { fb_puts(out_buf, max_out, &pos, "exec: sin recursos (procesos/memoria), codigo "); fb_put_dec(out_buf, max_out, &pos, (uint64_t)(-idx)); }
        kprint("exec: rechazado: "); kprint(err != 0 ? elf_strerror(err) : "sin recursos"); kprint("\n");
        return 1;
    }
    if (bg) {
        fb_puts(out_buf, max_out, &pos, "exec: "); fb_puts(out_buf, max_out, &pos, base);
        fb_puts(out_buf, max_out, &pos, " en segundo plano (idx "); fb_put_dec(out_buf, max_out, &pos, (uint64_t)idx);
        fb_puts(out_buf, max_out, &pos, "); mira 'procs'");
        return 1;
    }
    int64_t code = usermode_wait(idx);
    fb_puts(out_buf, max_out, &pos, "exec: "); fb_puts(out_buf, max_out, &pos, base); fb_puts(out_buf, max_out, &pos, " termino con codigo ");
    if (code < 0) { fb_puts(out_buf, max_out, &pos, "-"); fb_put_dec(out_buf, max_out, &pos, (uint64_t)(-code)); }
    else fb_put_dec(out_buf, max_out, &pos, (uint64_t)code);
    kprint("exec: terminado\n");
    return 1;
}

static int cmd_exec(const char *args, char *out_buf, uint32_t max_out)
{
    if (max_out) out_buf[0] = 0;
    int r = cmd_exec_impl(args, out_buf, max_out);
    if (max_out && out_buf[0]) { kprint(out_buf); kprint("\n"); }      /* resultado y errores tambien por la consola serie */
    return r;
}

static int cmd_procs(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    usermode_format_table(out_buf, max_out, 1);
    kprint(out_buf);
    return 1;
}

/* SOMA_PARCHE_014_RING3 */
static int cmd_usertest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    int bad = usermode_selftest();
    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, bad == 0 ? "usertest: OK (ring 3, syscalls y aislamiento)" :
                                               "usertest: FALLO (ver consola serie)");
    return 1;
}

/* SOMA_PARCHE_012_RENDER */
static int cmd_fbverify(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (args && args[0] == 'o' && args[1] == 'n') fb_verify_enabled = 1;
    else if (args && args[0] == 'o' && args[1] == 'f') fb_verify_enabled = 0;
    uint32_t f, t, w; fb_debug_stats(&f, &t, &w);
    kprint("FBVERIFY: "); kprint(fb_verify_enabled ? "activo" : "inactivo");
    kprint(" | fotogramas: "); kprint_dec(f);
    kprint(" | pixeles fuera del dano (total): "); kprint_dec(t);
    kprint(" | peor fotograma: "); kprint_dec(w); kprint("\n");
    fb_puts(out_buf, max_out, &pos, "fbverify on|off: comprueba que todo lo dibujado fue marcado sucio");
    return 1;
}

static int cmd_pmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    pmm_dump_stats();
    pmm_test_self();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "PMM: Verificado.");
    return 1;
}

static int cmd_vmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vmm_test_self();
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "VMM: Verificado.");
    return 1;
}

static int cmd_pci(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
#if defined(__aarch64__)
    if (pcie_present()) { pcie_init(); (void)rp1_init(); pcie_format(out_buf, max_out); kprint(out_buf); rp1_format(out_buf, max_out); kprint(out_buf); return 1; }     /* PARCHE 062/067: Pi 5 */
#endif
    pci_scan();
    pci_format_scan(out_buf, max_out);
    return 1;
}

static int cmd_arp(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    arp_format_cache(out_buf, max_out);
    kprint(out_buf);
    return 1;
}

static int cmd_ps(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

/* PARCHE 132 */
static int cmd_stacks(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_format_stacks(out_buf, max_out);
    return 1;
}

/* PARCHE 138: servicios aislados (decodificadores de imagen en ring 3) */
#ifdef __x86_64__
#include "sandbox.h"
static void shell_copy(char *dst, const char *src, uint32_t cap);
static int cmd_sbx(const char *args, char *out_buf, uint32_t max_out)
{
    if (args && args[0] == 't') { int b = sbx_selftest(); shell_copy(out_buf, b ? "sbx: FALLO (ver consola)\n" : "sbx: OK\n", max_out); return 1; }
    struct sbx_stats s; sbx_get_stats(&s);
    char *o = out_buf; uint32_t n = 0;
    #define SP(t) do { const char *_t = (t); while (*_t && n + 1 < max_out) o[n++] = *_t++; } while (0)
    #define SN(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } SP(_b + _i); } while (0)
    SP("Servicios aislados (ring 3, solo exit): decodificadores de imagen\n  llamadas "); SN(s.runs); SP(", correctas "); SN(s.ok);
    SP(", procesos muertos "); SN(s.died); SP(", respuestas rechazadas "); SN(s.bad_reply); SP(", sin memoria "); SN(s.oom);
    SP(", sin ranura "); SN(s.spawn_fail); SP("\n  ultima "); SN(s.last_ms); SP(" ms, maxima "); SN(s.max_ms); SP(" ms, total "); SN(s.total_ms);
    SP(" ms\n  'sbx test' ejecuta las pruebas de aislamiento\n");
    o[n] = 0;
    #undef SP
    #undef SN
    return 1;
}
#endif

static int cmd_threads(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

static int cmd_spawn(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: spawn <comando>  (alias: bg <comando>)\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: spawn <comando>");
        return 1;
    }

    struct bg_job *job = (struct bg_job *)kmalloc(sizeof(struct bg_job));
    if (!job) {
        kprint("Error: Sin memoria para spawn.\n");
        return 1;
    }
    memset(job, 0, sizeof(*job));

    /* AGENT_POLICY_V1: capturar el origen antes de lanzar el hilo. */
    job->agent_origin = (uint8_t)(agent_dispatch_mode != 0);

    uint32_t ci = 0;
    while (args[ci] && ci < sizeof(job->cmd) - 1) {
        job->cmd[ci] = args[ci];
        ci++;
    }
    job->cmd[ci] = '\0';

    char th_name[32];
    th_name[0] = 'b'; th_name[1] = 'g'; th_name[2] = '_';
    uint32_t ni = 3;
    for (uint32_t i = 0; args[i] && args[i] != ' ' && ni < sizeof(th_name) - 1; ++i) {
        th_name[ni++] = args[i];
    }
    th_name[ni] = '\0';

    struct tcb *nt = thread_create(th_name, spawn_job_worker, job);
    if (!nt) {
        kprint("Error: Fallo al crear hilo para spawn.\n");
        kfree(job);
        return 1;
    }

    job->tid = nt->tid;

    /* Formatear /tmp/job_<TID>.log limpiamente */
    uint32_t li = 0;
    const char *pfx = "/tmp/job_";
    while (*pfx) job->log_file[li++] = *pfx++;
    char tbuf[10]; int tn = 0; uint32_t tid_tmp = job->tid;
    if (tid_tmp == 0) tbuf[tn++] = '0';
    else { while (tid_tmp) { tbuf[tn++] = (char)('0' + (tid_tmp % 10)); tid_tmp /= 10; } }
    while (tn > 0) job->log_file[li++] = tbuf[--tn];
    const char *sfx = ".log";
    while (*sfx) job->log_file[li++] = *sfx++;
    job->log_file[li] = '\0';

    kprint("[SPAWN] Tarea '"); kprint(job->cmd);
    kprint("' lanzada en segundo plano [TID "); kprint_dec(job->tid);
    kprint(", Hilo '"); kprint(th_name);
    kprint("'] -> Log: "); kprint(job->log_file); kprint("\n");

    uint32_t pos = 0;
    fb_puts(out_buf, max_out, &pos, "[SPAWN OK] TID=");
    fb_put_dec(out_buf, max_out, &pos, job->tid);
    fb_puts(out_buf, max_out, &pos, " Log=");
    fb_puts(out_buf, max_out, &pos, job->log_file);
    return 1;
}

static int cmd_kill(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t tid = 0; while (*args >= '0' && *args <= '9') tid = tid * 10 + (*args++ - '0');
    thread_kill(tid);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Kill ejecutado");
    return 1;
}

static int cmd_sleep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t ms = 0; while (*args >= '0' && *args <= '9') ms = ms * 10 + (*args++ - '0');
    if (ms == 0) ms = 1000;
    thread_sleep(ms);
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "sleep completado");
    return 1;
}

static int cmd_time(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: time <comando>\n");
        return 1;
    }
    uint64_t t0 = timer_get_uptime_ms(); uint64_t c0 = rdtsc();
    dispatch_command(args, out_buf, max_out);
    uint64_t c1 = rdtsc(); uint64_t t1 = timer_get_uptime_ms();
    kprint("\n[TIME]: "); kprint_dec((uint32_t)(t1 - t0)); kprint(" ms | "); kprint_dec((uint32_t)(c1 - c0)); kprint(" ciclos TSC\n");
    return 1;
}

static int cmd_reboot(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_reboot();
    return 1;
}

static int cmd_poweroff(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_poweroff();
    return 1;
}

static int cmd_halt(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    vfs_sync();
    kprint("Sistema detenido (HALT).\n");
    for (;;) { arch_interrupts_disable(); arch_halt(); }
    return 1;
}

static int cmd_ls(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    char filter[64]; filter[0] = '\0';
    uint32_t fp = 0;

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '-') {
            while (*args && *args != ' ') {
                if (*args == 'a' || *args == 'A') show_all = 1;
                args++;
            }
        } else if (*args) {
            while (*args && *args != ' ' && fp < sizeof(filter) - 1) {
                filter[fp++] = *args++;
            }
            filter[fp] = '\0';
        }
    }

    vfs_list_ext(show_all, filter[0] ? filter : 0);
    vfs_format_list_ext(show_all, filter[0] ? filter : 0, out_buf, max_out);
    return 1;
}

static int cmd_cat(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        if (pipe_stdin_data != 0) {
            kprint(pipe_stdin_data);
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, pipe_stdin_data);
            return 1;
        }
        kprint("Uso: cat <archivo1> [archivo2 ...]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: cat <archivo>");
        return 1;
    }

    uint32_t out_pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    while (*args) {
        while (*args == ' ') args++;
        if (*args == '\0') break;

        char fn[64];
        uint32_t fi = 0;
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';

        char buf[2048];
        int r = vfs_read(fn, buf, sizeof(buf) - 1);
        if (r >= 0) {
            buf[r] = '\0';
            kprint(buf);
            fb_puts(out_buf, max_out, &out_pos, buf);
        } else {
            kprint("cat: no encontrado '"); kprint(fn); kprint("'\n");
            fb_puts(out_buf, max_out, &out_pos, "cat: no encontrado");
        }
    }
    return 1;
}

static int cmd_write(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[64]; uint32_t fi = 0;
    while (*args && *args != ' ' && fi < sizeof(fn) - 1) fn[fi++] = *args++;
    fn[fi] = '\0';
    while (*args == ' ') args++;

    if (agent_write_denied(fn, "write", out_buf, max_out)) return 1;       /* PARCHE 110: toda la zona protegida, no solo dos archivos */
    if (agent_dispatch_mode && (str_eq(fn, "/etc/init.sh") || str_eq(fn, "/etc/soma_manifest.txt"))) {
        kprint("write: operacion denegada por seguridad (Agente no puede modificar scripts de arranque).\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Error: Operacion denegada por politica de seguridad.");
        return 1;
    }

        char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    const char *end_q = 0; /*PL7_WRITE_ROBUST*/
    if (q) {
        const char *scan = args;
        while (*scan) {
            if (*scan == '\\' && *(scan + 1) != '\0') {
                scan += 2;
                continue;
            }
            if (*scan == q) end_q = scan;
            scan++;
        }
    }

    char *payload = (char *)kmalloc(FS_DATA_MAX);
    if (!payload) {
        kprint("write: sin memoria para buffer de escritura.\n");
        return 1;
    }

    uint32_t pi = 0;
    while (*args && pi < FS_DATA_MAX - 1) {
        if (q && args == end_q) {
            break;
        }
        if (*args == '\\' && *(args + 1) != '\0') { /*PL6_WRITE_ESCAPES*/
            char nx = *(args + 1);
            if (nx == 'n') { payload[pi++] = '\n'; args += 2; continue; }
            if (nx == 't') { payload[pi++] = '\t'; args += 2; continue; }
            if (nx == '"') { payload[pi++] = '"'; args += 2; continue; }
            if (nx == '\'') { payload[pi++] = '\''; args += 2; continue; }
            if (nx == '\\') { payload[pi++] = '\\'; args += 2; continue; }
        }
        payload[pi++] = *args++;
    }
    payload[pi] = '\0';

    int wres = vfs_write(fn, payload, pi);
    kfree(payload);

    if (wres == -2) {
        kprint("write: error, archivo demasiado grande (max 16383 bytes).\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Error: El archivo excede la cuota maxima de 16383 bytes.");
        return 1;
    }
    if (wres < 0) {
        kprint("write: error al escribir '"); kprint(fn); kprint("' (acceso denegado o fallo de disco).\n");
        uint32_t epos = 0;
        fb_puts(out_buf, max_out, &epos, "Error: no se pudo escribir el archivo (denegado o fallo de I/O).");
        return 1;
    }
    kprint("Escrito en '"); kprint(fn); kprint("' (");
    kprint_dec(pi); kprint(" bytes)\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "Archivo escrito.");
    return 1;
}

static int cmd_rm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;
    if (*args == '\0') {
        kprint("Uso: rm <archivo>\n");
        fb_puts(out_buf, max_out, &pos, "Error: falta el nombre del archivo.");
        return 1;
    }
    if (agent_write_denied(args, "rm", out_buf, max_out)) return 1;
    int r = vfs_delete(args);
    if (r == 0) {
        kprint("Eliminado '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "Archivo eliminado.");
    } else {
        kprint("rm: no se pudo eliminar '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos,
                "Error: archivo no encontrado o eliminacion denegada.");
    }
    return 1;
}

static int cmd_cp(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    if (agent_write_denied(dst, "cp", out_buf, max_out)) return 1;
    uint32_t rl = 0; char *cp_buf = vfs_read_alloc(src, &rl);   /* PARCHE 087: antes copiaba solo los primeros 4 KiB */
    uint32_t pos = 0;
    if (!cp_buf) { kprint("cp: origen no encontrado\n"); fb_puts(out_buf, max_out, &pos, "cp: error (no existe o sin memoria)"); }
    else if (vfs_write(dst, cp_buf, rl) >= 0) { kprint("cp: copiado OK\n"); fb_puts(out_buf, max_out, &pos, "cp: OK"); }
    else fb_puts(out_buf, max_out, &pos, "cp: no se pudo escribir");
    if (cp_buf) kfree(cp_buf);
    return 1;
}

static int cmd_mv(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char src[48], dst[48]; uint32_t si = 0, di = 0;
    while (*args && *args != ' ' && si < sizeof(src) - 1) src[si++] = *args++;
    src[si] = '\0';
    while (*args == ' ') args++;
    while (*args && *args != ' ' && di < sizeof(dst) - 1) dst[di++] = *args++;
    dst[di] = '\0';
    if (agent_write_denied(src, "mv", out_buf, max_out) || agent_write_denied(dst, "mv", out_buf, max_out)) return 1;
    uint32_t rl = 0; char *mv_buf = vfs_read_alloc(src, &rl);   /* PARCHE 087 */
    uint32_t pos = 0;
    if (!mv_buf) { kprint("mv: origen no encontrado\n"); fb_puts(out_buf, max_out, &pos, "mv: error (no existe o sin memoria)"); }
    else if (vfs_write(dst, mv_buf, rl) >= 0) { vfs_delete(src); kprint("mv: movido OK\n"); fb_puts(out_buf, max_out, &pos, "mv: OK"); }
    else fb_puts(out_buf, max_out, &pos, "mv: no se pudo escribir");
    if (mv_buf) kfree(mv_buf);
    return 1;
}

static int cmd_touch(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char tmp[8]; uint32_t pos = 0;
    if (agent_write_denied(args, "touch", out_buf, max_out)) return 1;
    if (vfs_read(args, tmp, sizeof(tmp)) < 0) { vfs_write(args, "", 0); kprint("touch: creado\n"); fb_puts(out_buf, max_out, &pos, "touch: OK"); }
    else { kprint("touch: ya existe\n"); fb_puts(out_buf, max_out, &pos, "touch: existe"); }
    return 1;
}

static int cmd_tree(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int show_all = 0;
    if (args[0] == '-' && (args[1] == 'a' || args[1] == 'A')) {
        show_all = 1;
    }
    vfs_tree_ext(show_all, out_buf, max_out);
    return 1;
}

static int cmd_head(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char hbuf[4096];

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
            } else {
                kprint("Uso: head [-n N] <archivo>\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: head [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, hbuf, sizeof(hbuf) - 1);
            if (r < 0) {
                kprint("head: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "head: no encontrado");
                return 1;
            }
            hbuf[r] = '\0';
            src_text = hbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t lc = 0, pos = 0;
    for (int i = 0; src_text[i] && lc < lines; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
        if (src_text[i] == '\n') lc++;
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_tail(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char fn[48]; uint32_t fi = 0;
    uint32_t lines = 10;
    const char *src_text = 0;
    static char tbuf[4096];
    uint32_t total_len = 0;

    /* Parsear opciones: -n <N> o -<N> */
    while (*args == '-') {
        args++;
        if (*args == 'n' || *args == 'N') {
            args++;
            while (*args == ' ') args++;
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        } else if (*args >= '0' && *args <= '9') {
            lines = 0;
            while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        while (*args == ' ') args++;
    }

    if (pipe_stdin_data != 0 && *args == '\0') {
        src_text = pipe_stdin_data;
        total_len = pipe_stdin_len;
    } else {
        while (*args && *args != ' ' && fi < sizeof(fn) - 1) {
            fn[fi++] = *args++;
        }
        fn[fi] = '\0';
        while (*args == ' ') args++;
        if (*args >= '0' && *args <= '9' && lines == 10) {
            lines = 0; while (*args >= '0' && *args <= '9') lines = lines * 10 + (*args++ - '0');
        }
        if (fn[0] == '\0') {
            if (pipe_stdin_data != 0) {
                src_text = pipe_stdin_data;
                total_len = pipe_stdin_len;
            } else {
                kprint("Uso: tail [-n N] <archivo>\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: tail [-n N] <archivo>");
                return 1;
            }
        } else {
            int r = vfs_read(fn, tbuf, sizeof(tbuf) - 1);
            if (r < 0) {
                kprint("tail: no encontrado: '"); kprint(fn); kprint("'\n");
                uint32_t p = 0; fb_puts(out_buf, max_out, &p, "tail: no encontrado");
                return 1;
            }
            total_len = (uint32_t)r;
            tbuf[r] = '\0';
            src_text = tbuf;
        }
    }

    if (lines == 0) lines = 10;
    uint32_t tl = 0; for (uint32_t i = 0; i < total_len; ++i) if (src_text[i] == '\n') tl++;
    uint32_t skip = (tl > lines) ? (tl - lines) : 0, cl = 0, si = 0;
    while (si < total_len && cl < skip) if (src_text[si++] == '\n') cl++;
    uint32_t pos = 0;
    for (uint32_t i = si; i < total_len; ++i) {
        kputc(src_text[i]);
        if (pos < max_out - 1) out_buf[pos++] = src_text[i];
    }
    out_buf[pos] = '\0';
    return 1;
}

static int cmd_wc(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    static char wcbuf[4096];
    const char *src_text = 0;
    uint32_t text_len = 0;

    if (*args != '\0') {
        int r = vfs_read(args, wcbuf, sizeof(wcbuf));
        if (r < 0) {
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, "wc: no encontrado");
            return 1;
        }
        text_len = (uint32_t)r;
        wcbuf[r] = '\0';
        src_text = wcbuf;
    } else if (pipe_stdin_data != 0) {
        src_text = pipe_stdin_data;
        text_len = pipe_stdin_len;
    } else {
        kprint("Uso: wc [archivo]  (o bien: <cmd> | wc)\n");
        return 1;
    }

    uint32_t l = 0, w = 0; int in_w = 0;
    for (uint32_t i = 0; i < text_len; ++i) {
        if (src_text[i] == '\n') l++;
        if (src_text[i] == ' ' || src_text[i] == '\t' || src_text[i] == '\n' || src_text[i] == '\r') in_w = 0;
        else if (!in_w) { in_w = 1; w++; }
    }
    kprint("  "); kprint_dec(l); kprint(" lineas  "); kprint_dec(w); kprint(" palabras  "); kprint_dec(text_len); kprint(" bytes\n");
    uint32_t pos = 0;
    fb_put_dec(out_buf, max_out, &pos, l); fb_puts(out_buf, max_out, &pos, " lineas, ");
    fb_put_dec(out_buf, max_out, &pos, w); fb_puts(out_buf, max_out, &pos, " palabras, ");
    fb_put_dec(out_buf, max_out, &pos, text_len); fb_puts(out_buf, max_out, &pos, " bytes");
    return 1;
}

/* grep V2: sin archivo → busca en TODO el RamFS.
 * Sintaxis: grep [-i] [-l] <patron> [archivo]
 *   -i  ignore case
 *   -l  solo listar nombres de archivo con match
 * Un solo archivo opcional (no multi-file estilo GNU). */
static int cmd_grep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int ignore_case = 0;
    int list_only = 0;

    /* Flags: -i -l -r ( -r = global, se ignora la lista de archivos posterior ) */
    int force_global = 0;
    for (;;) {
        if (args[0] == '-' && args[1] == 'i' && (args[2] == ' ' || args[2] == '\0')) {
            ignore_case = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && (args[2] == ' ' || args[2] == '\0')) {
            list_only = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'r' && (args[2] == ' ' || args[2] == '\0')) {
            force_global = 1; args += 2;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'i' && args[2] == 'l' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        if (args[0] == '-' && args[1] == 'l' && args[2] == 'i' &&
            (args[3] == ' ' || args[3] == '\0')) {
            ignore_case = 1; list_only = 1; args += 3;
            while (*args == ' ') args++;
            continue;
        }
        break;
    }

    char pat[64]; uint32_t pi = 0; char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pi < sizeof(pat) - 1) {
        if (q && *args == q) { args++; break; }
        if (!q && *args == ' ') break;
        pat[pi++] = *args++;
    }
    pat[pi] = '\0';
    while (*args == ' ') args++;

    if (pat[0] == '\0') {
        kprint("Uso: grep [-i] [-l] <patron> [archivo]\n");
        kprint("  Sin archivo: busca en todo el RamFS.\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: grep [-i] [-l] <patron> [archivo]");
        return 1;
    }

    /* Extraer SOLO el primer path (ignorar multi-file GNU) */
    char fname[FS_NAME_MAX];
    uint32_t fi = 0;
    if (*args != '\0') {
        if (*args == '\'' || *args == '"') {
            char qq = *args++;
            while (*args && *args != qq && fi < sizeof(fname) - 1) fname[fi++] = *args++;
            if (*args == qq) args++;
        } else {
            while (*args && *args != ' ' && fi < sizeof(fname) - 1) fname[fi++] = *args++;
        }
        fname[fi] = '\0';
    } else {
        fname[0] = '\0';
    }

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    /* -r o sin archivo → búsqueda global (ignora paths extra estilo GNU) */
    if (force_global) fname[0] = '\0';

    /* ---- Sin archivo y sin pipe → búsqueda global en RamFS ---- */
    if (fname[0] == '\0' && pipe_stdin_data == 0) {
        static char g_all[2048];
        int hits = vfs_grep_all(pat, ignore_case, list_only, g_all, sizeof(g_all));
        kprint(g_all);
        fb_puts(out_buf, max_out, &pos, g_all);
        if (hits > 0) {
            kprint("("); kprint_dec((uint32_t)hits); kprint(" archivo(s) con coincidencias)\n");
        }
        return 1;
    }

    /* ---- Un archivo o pipe (comportamiento clásico) ---- */
    const char *src_text = 0;
    static char gbuf[4096];

    if (fname[0] != '\0') {
        int r = vfs_read(fname, gbuf, sizeof(gbuf) - 1);
        if (r < 0) {
            kprint("grep: archivo no encontrado: '"); kprint(fname); kprint("'\n");
            fb_puts(out_buf, max_out, &pos, "grep: archivo no encontrado");
            return 1;
        }
        gbuf[r] = '\0';
        src_text = gbuf;
    } else {
        src_text = pipe_stdin_data;
    }

    uint32_t line_num = 1;
    int matches = 0;
    const char *p = src_text;

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        uint32_t line_len = (uint32_t)(p - line_start);
        char line_tmp[256];
        uint32_t copy_len = line_len < sizeof(line_tmp) - 1 ? line_len : sizeof(line_tmp) - 1;
        for (uint32_t k = 0; k < copy_len; ++k) line_tmp[k] = line_start[k];
        line_tmp[copy_len] = '\0';

        if (find_substr_ci(line_tmp, pat, ignore_case)) {
            matches++;
            if (list_only) {
                if (fname[0]) {
                    kprint(fname); kprint("\n");
                    fb_puts(out_buf, max_out, &pos, fname);
                    fb_puts(out_buf, max_out, &pos, "\n");
                }
                break;
            }
            kprint_dec(line_num); kprint(": "); kprint(line_tmp); kprint("\n");
            fb_put_dec(out_buf, max_out, &pos, line_num);
            fb_puts(out_buf, max_out, &pos, ": ");
            fb_puts(out_buf, max_out, &pos, line_tmp);
            fb_puts(out_buf, max_out, &pos, "\n");
        }

        line_num++;
        if (*p == '\n') p++;
    }

    if (matches == 0) {
        kprint("grep: sin coincidencias.\n");
        fb_puts(out_buf, max_out, &pos, "grep: sin coincidencias.");
    }
    return 1;
}

static int cmd_echo(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    char clean[512];
    uint32_t ci = 0;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }

    while (*args && ci < sizeof(clean) - 1) {
        if (q && *args == q) {
            args++;
            while (*args == ' ') args++;
            if (*args == '"' || *args == '\'') q = *args++;
            else q = 0;
            continue;
        }
        clean[ci++] = *args++;
    }
    clean[ci] = '\0';

    kprint(clean); kprint("\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, clean); fb_putc(out_buf, max_out, &pos, '\n');
    return 1;
}

static int cmd_nano(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    run_nano(args);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "nano cerrado.");
    return 1;
}

static int cmd_hexdump(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    do_hexdump(args, 128, out_buf, max_out);
    return 1;
}

static int cmd_checkpoint(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_save() == 0) {
        kprint("[CHECKPOINT OK]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Checkpoint OK");
    } else { uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Checkpoint: ERROR al guardar (el anterior sigue siendo el ultimo valido solo si no se llego a invalidar)"); }
    return 1;
}

static int cmd_rollback(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (vfs_checkpoint_restore() == 0) {
        kprint("[ROLLBACK OK]\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Rollback OK");
    } else { uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Rollback: no hay checkpoint valido (ausente o corrupto); el sistema de archivos no se ha tocado"); }
    return 1;
}

static int cmd_fs_sync(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_sync();
    kprint("RamFS sincronizado.\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Sync OK");
    return 1;
}

static int cmd_fs_format(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vfs_format();
    kprint("RamFS formateado a fabrica.\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Format OK");
    return 1;
}

static int cmd_sector_read(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    char sbuf[512];
    if (agent_dispatch_mode && !soma_lba_user_zone(sec)) {          /* PARCHE 122: un agente no lee el disco entero (VFS privado, memoria, BIGFS) */
        kprint("Error: sector protegido para agentes.\n");
        uint32_t pe = 0; fb_puts(out_buf, max_out, &pe, "sector protegido para agentes");
        return 1;
    }
    if (virtio_blk_read(sec, sbuf) == 0) {
        kprint("\n[LBA "); kprint_dec((uint32_t)sec); kprint("]: ");
        for (int i=0; i<512 && sbuf[i]; i++) if (sbuf[i]>=32 && sbuf[i]<127) kputc(sbuf[i]);
        kprint("\n");
    }
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "sector leido");
    return 1;
}

static int cmd_sector_write(const char *args, char *out_buf, uint32_t max_out)
{
    uint64_t sec = parse_num(&args);
    if (!soma_lba_user_zone(sec)) {                                  /* PARCHE 122: antes dejaba escribir sobre BIGFS y el checkpoint */
        kprint("Error: sector protegido.\n");
        return 1;
    }
    while (*args == ' ') args++;
    char sbuf[512]; memset(sbuf, 0, 512); int bi = 0;
    while (*args && bi < 511) sbuf[bi++] = *args++;
    virtio_blk_write(sec, sbuf);
    kprint("Sector escrito.\n");
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "sector escrito");
    return 1;
}

static int cmd_ping(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint8_t target[4];
    if (*args) parse_ip(args, target);
    else { target[0] = net_gateway[0]; target[1] = net_gateway[1]; target[2] = net_gateway[2]; target[3] = net_gateway[3]; }
    int r = icmp_ping(target, 1, 2000);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, (r == 1) ? "Ping exitoso" : "Ping fallido");
    return 1;
}

static int cmd_dns(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("dns", args, out_buf, max_out)) return 1;   /* SOMA_193: un nombre inventado tambien saca datos (consulta DNS) */
    while (*args == ' ') args++;
    uint8_t ip[4];
    dns_resolve(args, ip, 3000);
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, "DNS consultado");
    return 1;
}


/* PARCHE_SEARCH_V5: busqueda web multifuente conectada a 10.0.2.2:8095 */
static void search_url_encode(const char *in, char *out, uint32_t max)
{
    uint32_t o = 0;
    static const char *hex = "0123456789ABCDEF";
    while (*in && o + 4 < max) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else if (c == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

static int cmd_search(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: search <consulta>\n");
        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, "Uso: search <consulta>");
        return 1;
    }

    /* Limpiar comillas iniciales/finales si el agente o usuario las puso */
    char clean_query[256];
    uint32_t qp = 0;
    char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && qp < sizeof(clean_query) - 1) {
        if (q && *args == q && *(args + 1) == '\0') break;
        clean_query[qp++] = *args++;
    }
    clean_query[qp] = '\0';
    while (qp > 0 && (clean_query[qp - 1] == ' ' || clean_query[qp - 1] == '\'' || clean_query[qp - 1] == '"')) {
        clean_query[--qp] = '\0';
    }

    char enc[256];
    search_url_encode(clean_query, enc, sizeof(enc));

    char path[300];
    uint32_t pp = 0;
    const char *pre = "/search?q=";
    while (*pre && pp + 1 < sizeof(path)) path[pp++] = *pre++;
    for (uint32_t i = 0; enc[i] && pp + 1 < sizeof(path); ++i) path[pp++] = enc[i];
    path[pp] = '\0';

    /* Host Mac en QEMU NAT = net_gateway (10.0.2.2) */
    uint8_t tip[4];
    tip[0] = net_gateway[0];
    tip[1] = net_gateway[1];
    tip[2] = net_gateway[2];
    tip[3] = net_gateway[3];
    const char *host = "10.0.2.2";
    uint16_t proxy_port = 8095;

    static char http_buf[8192];
    struct http_response resp;
    int code = http_get_host(tip, proxy_port, host, path, http_buf, sizeof(http_buf), &resp, 45000);

    uint32_t pos = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    if (code < 0) {
        kprint("search: error HTTP/red (proxy 10.0.2.2:8095 no responde)\n");
        fb_puts(out_buf, max_out, &pos,
                "search: error de red o timeout (inicia 'python3 search_proxy.py' en el host)");
        return 1;
    }
    if (code < 200 || code >= 400) {
        kprint("search: HTTP error "); kprint_dec((uint32_t)code); kprint("\n");
        fb_puts(out_buf, max_out, &pos, "search: error devuelto por el proxy");
        return 1;
    }

    const char *body = (resp.body && resp.body[0]) ? resp.body : http_buf;
    kprint("\n--- Resultados Web ["); kprint_dec((uint32_t)code); kprint("] ---\n");
    kprint(body);
    kprint("\n------------------------------------\n");
    if (!body[0]) {
        fb_puts(out_buf, max_out, &pos, "search: respuesta vacia");
        return 1;
    }
    fb_puts(out_buf, max_out, &pos, body);
    return 1;
}

static int shell_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int cmd_curl(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("curl", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    /* PARCHE_RED_SEGURO_V1: URL completa delegada al proxy; el kernel no necesita TLS. */
    while (*args == ' ') args++;
    const char *url_arg = args;
    char url_quote = 0;
    if (*url_arg == '\'' || *url_arg == '"') {
        url_quote = *url_arg++;
    }
    if (shell_starts_with(url_arg, "http://") || shell_starts_with(url_arg, "https://")) {
        char target_url[321];
        uint32_t url_len = 0;
        while (url_arg[url_len] && url_len < sizeof(target_url) - 1 &&
               (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            target_url[url_len] = url_arg[url_len];
            url_len++;
        }
        if (url_arg[url_len] &&
            (url_quote ? url_arg[url_len] != url_quote : url_arg[url_len] != ' ')) {
            kprint("curl: URL demasiado larga (maximo 320 caracteres).\n");
            return 1;
        }
        target_url[url_len] = '\0';
        if (url_len == 0) {
            kprint("curl: URL vacia.\n");
            return 1;
        }

        char encoded_url[1000];
        search_url_encode(target_url, encoded_url, sizeof(encoded_url));
        char proxy_path[1024];
        uint32_t path_pos = 0;
        const char *prefix = "/fetch?url=";
        while (*prefix && path_pos + 1 < sizeof(proxy_path)) {
            proxy_path[path_pos++] = *prefix++;
        }
        for (uint32_t i = 0; encoded_url[i] && path_pos + 1 < sizeof(proxy_path); ++i) {
            proxy_path[path_pos++] = encoded_url[i];
        }
        proxy_path[path_pos] = '\0';

        static char proxy_buf[4096];
        struct http_response proxy_resp;
        int proxy_code = http_get_host(net_gateway, 8095, "10.0.2.2", proxy_path,
                                       proxy_buf, sizeof(proxy_buf), &proxy_resp, 40000);
        uint32_t out_pos = 0;
        if (out_buf && max_out) out_buf[0] = '\0';
        if (proxy_code < 0) {
            kprint("curl: el proxy HTTPS 10.0.2.2:8095 no responde.\n");
            fb_puts(out_buf, max_out, &out_pos, "curl: error de red o timeout al consultar el proxy");
            return 1;
        }
        const char *display = (proxy_resp.body && proxy_resp.body[0]) ? proxy_resp.body : proxy_buf;
        kprint("\n--- RESPUESTA HTTP VIA PROXY [");
        kprint_dec((uint32_t)proxy_code);
        kprint("] ---\n");
        kprint(display);
        kprint("\n");
        fb_puts(out_buf, max_out, &out_pos, display);
        return 1;
    }
    char target[64]; uint32_t ti = 0; while (*args && *args != ' ' && ti < sizeof(target) - 1) target[ti++] = *args++; target[ti] = '\0';
    while (*args == ' ') args++;
    uint16_t port = 80; if (*args >= '0' && *args <= '9') { port = 0; while (*args >= '0' && *args <= '9') port = port * 10 + (*args++ - '0'); }
    while (*args == ' ') args++;
    const char *path = (*args) ? args : "/";
    uint8_t tip[4]; const char *hhdr = 0; int is_ip = 1, dots = 0;
    for (int i = 0; target[i]; ++i) { if (target[i] == '.') dots++; else if (target[i] < '0' || target[i] > '9') is_ip = 0; }
    if (dots != 3) is_ip = 0;
    if (is_ip) parse_ip(target, tip);
    else { if (!dns_resolve(target, tip, 3000)) { kprint("DNS error\n"); return 1; } hhdr = target; }
    static char http_buf[4096]; struct http_response resp;
    int code = http_get_host(tip, port, hhdr, path, http_buf, sizeof(http_buf), &resp, 40000);
    if (code >= 0) {
        const char *disp = (resp.body && resp.body[0]) ? resp.body : http_buf;
        kprint("\n--- RESPUESTA HTTP ["); kprint_dec((uint32_t)code); kprint("] ---\n"); kprint(disp); kprint("\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, disp);
    }
    return 1;
}

static int cmd_soma(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("soma", args);
    return 1;
}

static int cmd_buscador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    agent_run("buscador", args);
    return 1;
}

static int cmd_mayordomo(const char *args, char *out_buf, uint32_t max_out)
{
    if (!args || !*args) {
        kprint("Uso: mayordomo <tarea del sistema>\n");
        if (out_buf && max_out) {
            const char *msg = "Uso: mayordomo <tarea del sistema>";
            uint32_t p = 0; while (msg[p] && p + 1 < max_out) { out_buf[p] = msg[p]; ++p; }
            out_buf[p] = '\0';
        }
        return 1;
    }
    agent_run("mayordomo", args);
    if (out_buf && max_out) {
        uint32_t p = 0;
        while (agent_last_response[p] && p + 1 < max_out) { out_buf[p] = agent_last_response[p]; ++p; }
        out_buf[p] = '\0';
    }
    return 1;
}

static int cmd_agent(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
    char target_agent[32];
    uint32_t ap = 0;
    while (*args && *args != ' ' && ap < sizeof(target_agent) - 1) {
        target_agent[ap++] = *args++;
    }
    target_agent[ap] = '\0';
    while (*args == ' ') args++;

    if (str_eq(target_agent, "buscador")) {
        agent_run("buscador", args);
    } else if (str_eq(target_agent, "soma")) {
        agent_run("soma", args);
    } else if (str_eq(target_agent, "mayordomo")) {
        agent_run("mayordomo", args);
    } else {
        /* Si no coincide con un nombre de agente, invocar a soma con la frase completa */
        agent_run("soma", target_agent[0] ? target_agent : args);
    }
    return 1;
}

static int cmd_health(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    int ok = llm_health();
    kprint(ok ? "Servidor LLM: OK\n" : "Servidor LLM: ERROR\n");
    if (!ok) { kprint(llm_last_error); kprint("\nSe elige con la orden llm-config o en Ajustes > IA.\n"); }   /* SOMA_195 */
    uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, ok ? "Servidor LLM: OK" : "Servidor LLM: ERROR");
    return 1;
}

static int cmd_llm(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: llm <mensaje>\n");
        return 1;
    }
    char *reply = (char *)kmalloc(16384);
    if (!reply) {
        kprint("llm: sin memoria.\n");
        return 1;
    }
    kprint("Consultando a nail-35b...\n");
    if (llm_chat(args, reply, 16384, 35000)) {
        kprint("\n[nail-35b]: "); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al consultar el LLM.\n");
    }
    kfree(reply);
    return 1;
}

static int cmd_llm_diag(const char *args, char *out_buf, uint32_t max_out)
{
    static char reply[4096];
    kprint("Solicitando diagnostico al LLM...\n");
    if (llm_diagnose(args, reply, sizeof(reply), 35000)) {
        kprint("\n[DIAGNOSTICO IA]:\n"); kprint(reply); kprint("\n\n");
        uint32_t pos = 0; fb_puts(out_buf, max_out, &pos, reply);
    } else {
        kprint("Error al solicitar diagnostico al LLM.\n");
    }
    return 1;
}

static int cmd_src_ls(const char *args, char *out_buf, uint32_t max_out)
{
    /* SOMA_PARCHE_011_SRCFS: src_ls [prefijo]. out_buf (2 KiB) es para la GUI y se corta con aviso;
     * la consola recibe el listado completo desde un buffer mayor. */
    char prefix[48];
    uint32_t k = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ' && *args != '\n' && *args != '\r' && k < sizeof(prefix) - 1) prefix[k++] = *args++;
    prefix[k] = '\0';

    srcfs_ls_filter(k ? prefix : 0, out_buf, max_out);
    char *big = (char *)kmalloc(16384);
    if (big) {
        srcfs_ls_filter(k ? prefix : 0, big, 16384);
        kprint(big);
        kfree(big);
    } else {
        kprint(out_buf);
    }
    kputc('\n');
    return 1;
}

static int cmd_src_cat(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    fb_puts(full, sizeof(full), &p, "src_cat ");
    fb_puts(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_src_grep(const char *args, char *out_buf, uint32_t max_out)
{
    char full[128]; uint32_t p = 0;
    fb_puts(full, sizeof(full), &p, "src_grep ");
    fb_puts(full, sizeof(full), &p, args);
    src_tool(full, out_buf, max_out);
    return 1;
}

static int cmd_hpatch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    hpatch_command(args);
    return 1;
}

static int cmd_panic(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;
#if defined(__x86_64__)
    if (args[0] == 'w' && args[1] == 'e' && args[2] == 'b') {              /* SOMA_198: fallo real dentro del motor del navegador */
        const char *a = args + 3; while (*a == ' ') a++;
        int ph = (*a >= '1' && *a <= '5') ? *a - '0' : 0;
        if (!ph) { kprint("uso: panic web <1 navegacion | 2 JavaScript | 3 maquetacion | 4 instruccion no valida | 5 desbordamiento de pila>\n"
                          "     El siguiente paso de ese tipo en el navegador fallara de verdad: debe cerrarse la pagina, no el sistema.\n"); return 1; }
        if (browser_r3_active()) {                                          /* SOMA_202: con el motor aislado el fallo ocurre en su proceso de ring 3 */
            int32_t wa[1] = { ph == 4 ? 1 : ph == 5 ? 3 : ph == 2 ? 2 : 0 };
            kprint("panic web: fallo provocado dentro del proceso del navegador ("); kprint(wa[0] == 1 ? "instruccion no valida" : wa[0] == 2 ? "bucle sin fin" : wa[0] == 3 ? "agotar su memoria" : "puntero nulo"); kprint("). El sistema debe seguir como si nada.\n");
            (void)web_r3_send(W3C_CRASH, wa, 1, 0, 0);
            return 1;
        }
        web_fault_inject = ph;
        kprint("panic web: el navegador fallara en su proximo paso de ese tipo (recuperaciones hasta ahora: "); kprint_dec(web_fault_count()); kprint(")\n");
        return 1;
    }
#endif
    if (args[0] == 'd' && args[1] == 'i' && args[2] == 'v') {
        kprint("Provocando division por cero (#DE)...\n");
        volatile int z = 0; volatile int x = 42 / z; (void)x;
    } else if (args[0] == 'u' && args[1] == 'd') {
        kprint("Ejecutando instruccion invalida (#UD)...\n");
#ifdef __x86_64__
        __asm__ volatile ("ud2");
#else
        __builtin_trap();
#endif
#ifdef __x86_64__
    } else if (args[0] == 'n' && args[1] == 'u' && args[2] == 'l') {              /* SOMA_192: comprueba la proteccion de la pagina 0 */
        kprint("Escribiendo a traves de un puntero nulo (debe detener el sistema con #PF)...\n");
        volatile uint64_t *np = 0; __asm__ volatile ("" : "+r"(np)); *np = 0xCAFEBABE;
        kprint("... la escritura NO fallo: la pagina 0 no esta protegida (soma.memprot=off?)\n");
    } else if (args[0] == 'n' && args[1] == 'x') {                                /* SOMA_192: comprueba W^X fuera de la imagen del nucleo */
        uintptr_t fr = pmm_alloc_frame();
        if (!fr) { kprint("sin memoria\n"); return 1; }
        *(volatile uint8_t *)fr = 0xC3;                                           /* ret */
        kprint("Saltando a un marco de datos de la RAM (debe detener el sistema con #PF)...\n");
        void (*fn)(void) = (void (*)(void))fr; __asm__ volatile ("" : "+r"(fn)); fn();
        kprint("... el codigo se ejecuto: la RAM de datos sigue siendo ejecutable (soma.memprot=off?)\n");
        pmm_free_frame(fr);
#endif
    } else {
        kprint("Provocando fallo de pagina (#PF) en 0xDEADBEEF00...\n");
        volatile uint64_t *bad = (volatile uint64_t *)0xDEADBEEF00ULL; *bad = 0xCAFEBABE;
    }
    return 1;
}

static int cmd_env(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    if (!env_initialized) env_init_defaults();
    uint32_t pos = 0;
    kprint("\nVARIABLES DE ENTORNO ACTIVAS:\n------------------------------\n");
    for (int i = 0; i < MAX_ENV_VARS; ++i) {
        if (env_table[i].used) {
            kprint("  "); kprint(env_table[i].key); kputc('='); kprint(env_table[i].val); kprint("\n");
            fb_puts(out_buf, max_out, &pos, env_table[i].key);
            fb_putc(out_buf, max_out, &pos, '=');
            fb_puts(out_buf, max_out, &pos, env_table[i].val);
            fb_putc(out_buf, max_out, &pos, '\n');
        }
    }
    kprint("------------------------------\n");
    return 1;
}

static int cmd_export(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        return cmd_env(args, out_buf, max_out);
    }
    char k[ENV_KEY_LEN], v[ENV_VAL_LEN];
    uint32_t kp = 0, vp = 0;
    while (*args && *args != '=' && *args != ' ' && kp < sizeof(k) - 1) k[kp++] = *args++;
    k[kp] = '\0';
    if (*args == '=' || *args == ' ') args++;
    while (*args == ' ') args++;

    char q = 0;
    if (*args == '"' || *args == '\'') {
        q = *args++;
    }
    while (*args && vp < sizeof(v) - 1) {
        if (q && *args == q) { args++; break; }
        v[vp++] = *args++;
    }
    v[vp] = '\0';

    if (kp > 0) {
        env_set(k, v);
        kprint("export: "); kprint(k); kputc('='); kprint(v); kprint("\n");
        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, k); fb_putc(out_buf, max_out, &p, '='); fb_puts(out_buf, max_out, &p, v);
        return 1;
    }
    kprint("Uso: export VAR=valor\n");
    return 1;
}

static int cmd_unset(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: unset <VAR>\n");
        return 1;
    }
    env_unset(args);
    kprint("unset: "); kprint(args); kprint("\n");
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "unset OK");
    return 1;
}

#define SOURCE_MAX_DEPTH 3

static int cmd_source(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: source <archivo.sh>  (alias: sh <archivo.sh>)\n");
        return 1;
    }

    /* PARCHE 046: profundidad por hilo + buffer propio (antes: static compartido). */
    struct tcb *self = thread_current();
    static int source_depth_fallback = 0;
    int *sdepth = self ? &self->source_depth : &source_depth_fallback;
    uint32_t pos = 0;

    if (*sdepth >= SOURCE_MAX_DEPTH) {
        kprint("source: anidamiento maximo alcanzado (3).\n");
        fb_puts(out_buf, max_out, &pos, "Error: source anidado en exceso");
        return 0;
    }

    char *script_buf = (char *)kmalloc(4096);
    if (!script_buf) {
        kprint("source: sin memoria.\n");
        fb_puts(out_buf, max_out, &pos, "Error: sin memoria");
        return 0;
    }

    int r = vfs_read(args, script_buf, 4095);
    if (r < 0) {
        kprint("source: archivo no encontrado: '"); kprint(args); kprint("'\n");
        fb_puts(out_buf, max_out, &pos, "source: archivo no encontrado");
        kfree(script_buf);
        return 1;
    }
    script_buf[r] = '\0';

    (*sdepth)++;
    const char *p = script_buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        char line[256];
        uint32_t lp = 0;
        while (*p && *p != '\n' && lp < sizeof(line) - 1) {
            if (*p != '\r') line[lp++] = *p;
            p++;
        }
        line[lp] = '\0';
        if (*p == '\n') p++;

        while (lp > 0 && (line[lp - 1] == ' ' || line[lp - 1] == '\t')) {
            line[--lp] = '\0';
        }

        if (lp > 0) {
            char sub_out[1024];
            sub_out[0] = '\0';
            dispatch_command(line, sub_out, sizeof(sub_out));
        }
    }
    (*sdepth)--;
    kfree(script_buf);

    fb_puts(out_buf, max_out, &pos, "source: ejecucion completada");
    return 1;
}

/* ====================================================================
 * TABLA DECLARATIVA CANÓNICA DE COMANDOS DE SOMA
 * ==================================================================== */

static int cmd_history(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    return console_history_dump(out_buf, max_out);
}

/* PARCHE 046: banco de pruebas de la politica del Agente (solo usuario). */
static int cmd_agent_sim(const char *args, char *out_buf, uint32_t max_out)
{
    char cmd[256];
    uint32_t n = 0;
    char q = 0;

    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: agent_sim <comando>   (usa comillas si contiene ; | > &&)\n");
        return 1;
    }
    if (*args == '"' || *args == '\'') q = *args++;
    while (*args && n < sizeof(cmd) - 1) {
        if (q && *args == q && args[1] == '\0') break;
        cmd[n++] = *args++;
    }
    cmd[n] = '\0';

    int r = dispatch_agent_command(cmd, out_buf, max_out);
    kprint("[agent_sim] rc="); kprint_dec(r ? 1 : 0); kprint("\n");
    return 1;
}


/* PARCHE 089: "monitor" - monitor del sistema */
static int cmd_monitor(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_sysmon();
    fb_puts(out_buf, max_out, &p, "Monitor del sistema abierto.");
    return 1;
}

/* PARCHE 088: "imagen [archivo]" - visor de imagenes */
static int cmd_imagen(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    char path[128]; uint32_t k = 0;
    while (args && *args && *args != ' ' && k < sizeof path - 1) path[k++] = *args++;
    path[k] = 0;
    if (k && path[0] != '/') { char t[128] = "/workspace/"; uint32_t l = 11; for (uint32_t i = 0; path[i] && l < sizeof t - 1; i++) t[l++] = path[i]; t[l] = 0; if (vfs_size(t) >= 0) { for (uint32_t i = 0; i <= l; i++) path[i] = t[i]; } }
    gui_open_imgview(path);
    fb_puts(out_buf, max_out, &p, "Visor de imagenes abierto.");
    return 1;
}

/* PARCHE 088: "pdf [archivo]" - lector de PDF */
static int cmd_pdf(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    char path[128]; uint32_t k = 0;
    while (args && *args && *args != ' ' && k < sizeof path - 1) path[k++] = *args++;
    path[k] = 0;
    if (k && path[0] != '/') { char t[128] = "/workspace/"; uint32_t l = 11; for (uint32_t i = 0; path[i] && l < sizeof t - 1; i++) t[l++] = path[i]; t[l] = 0; if (vfs_size(t) >= 0) { for (uint32_t i = 0; i <= l; i++) path[i] = t[i]; } }
    gui_open_pdf(path);
    fb_puts(out_buf, max_out, &p, "Lector PDF abierto.");
    return 1;
}

/* PARCHE 087: "suma <archivo>" - tamano y CRC-32 (comprobar que un archivo grande llega y se guarda entero) */
static int cmd_suma(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[200]; uint32_t p = 0; rep[0] = 0;
    while (args && *args == ' ') args++;
    uint32_t n = 0; char *d = (args && *args) ? vfs_read_alloc(args, &n) : 0;
    if (!d) { fb_puts(rep, sizeof rep, &p, "suma: no existe (uso: suma <archivo>)\n"); shell_emit(rep, out_buf, max_out); return 1; }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= (uint8_t)d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    c = ~c; kfree(d);
    static const char hx[] = "0123456789abcdef"; char h[9]; for (int i = 0; i < 8; i++) h[i] = hx[(c >> (28 - 4 * i)) & 15]; h[8] = 0;
    fb_put_dec(rep, sizeof rep, &p, n); fb_puts(rep, sizeof rep, &p, " bytes  crc32 "); fb_puts(rep, sizeof rep, &p, h); fb_puts(rep, sizeof rep, &p, "  "); fb_puts(rep, sizeof rep, &p, args); fb_puts(rep, sizeof rep, &p, "\n");
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* PARCHE 087: "descargar <url> [archivo]" - guarda en el VFS lo que devuelve una URL (http directo; https por la pasarela) */
static int cmd_descargar(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("descargar", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    char rep[256]; rep[0] = 0;
    char url[300], dst[FS_NAME_MAX]; uint32_t p = 0, k = 0;
    while (args && *args == ' ') args++;
    while (args && *args && *args != ' ' && k < sizeof url - 1) url[k++] = *args++;
    url[k] = 0;
    while (args && *args == ' ') args++;
    k = 0; while (args && *args && *args != ' ' && k < sizeof dst - 1) dst[k++] = *args++;
    dst[k] = 0;
    if (!url[0]) { fb_puts(rep, sizeof rep, &p, "Uso: descargar <url> [archivo]"); { fb_puts(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    if (!dst[0]) {   /* nombre: lo ultimo de la ruta, en /workspace */
        const char *b = url, *q; for (q = url; *q && *q != '?' && *q != '#'; q++) if (*q == '/') b = q + 1;
        char nm[40]; uint32_t n = 0; for (q = b; *q && *q != '?' && *q != '#' && n < sizeof nm - 1; q++) nm[n++] = *q;
        nm[n] = 0; if (!n) { const char *d = "descarga"; while (*d) nm[n++] = *d++; nm[n] = 0; }
        const char *ws = "/workspace/"; uint32_t l = 0; while (ws[l]) { dst[l] = ws[l]; l++; }
        for (n = 0; nm[n] && l < sizeof dst - 1; n++) dst[l++] = nm[n];
        dst[l] = 0;
    }
    if (agent_write_denied(dst, "descargar", out_buf, max_out)) return 1;
    struct wn_req rq; struct wn_resp rs; memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    rq.url = url; rq.timeout_ms = 30000; rq.max_body = 24u << 20; rq.want_binary = 1;
#if defined(__x86_64__)
#define wn_fetch wn_fetch_isolated      /* PARCHE 142: el kernel no interpreta HTTP: lo hace el servicio aislado netsvc */
#define wn_free  wn_free_isolated
#endif
    if (wn_fetch(&rq, &rs) != 0 || !rs.body) { fb_puts(rep, sizeof rep, &p, "descargar: error de red "); fb_puts(rep, sizeof rep, &p, rs.err); wn_free(&rs); { fb_puts(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    if (rs.status < 200 || rs.status >= 300) { fb_puts(rep, sizeof rep, &p, "descargar: estado HTTP "); fb_put_dec(rep, sizeof rep, &p, (uint64_t)rs.status); wn_free(&rs); { fb_puts(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1; }
    int w = vfs_write(dst, rs.body, rs.len);
    if (w < 0) fb_puts(rep, sizeof rep, &p, w == -2 ? "descargar: demasiado grande (max 32 MiB)" : "descargar: no se pudo guardar (sin memoria o ruta no valida)");
    else {
        fb_puts(rep, sizeof rep, &p, "descargados "); fb_put_dec(rep, sizeof rep, &p, rs.len); fb_puts(rep, sizeof rep, &p, " bytes en "); fb_puts(rep, sizeof rep, &p, dst);
        int pers = vfs_big_persist();
        if (rs.len >= FS_DATA_MAX && pers == 0) fb_puts(rep, sizeof rep, &p, " (solo en memoria: BIGFS sin formatear)");
    }
    wn_free(&rs);
    { fb_puts(rep, sizeof rep, &p, "\n"); shell_emit(rep, out_buf, max_out); } return 1;
}

/* PARCHE 097: "programa [archivo.soma] <mision>" crea o modifica un programa con el programador de SOMA */
static int cmd_programa(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    if (!args || !*args) { uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: programa [archivo.soma] <que debe hacer>"); return 1; }
    char target[128]; target[0] = 0;
    const char *m = args;
    { const char *e = args; while (*e && *e != ' ') e++;
      uint32_t n = (uint32_t)(e - args);
      if (n > 5 && n < sizeof target && args[n - 5] == '.' && args[n - 4] == 's' && args[n - 3] == 'o' && args[n - 2] == 'm' && args[n - 1] == 'a') {
          for (uint32_t i = 0; i < n; i++) target[i] = args[i];
          target[n] = 0; m = e; while (*m == ' ') m++;
      } }
    if (!*m) { uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Falta la descripcion de lo que debe hacer el programa."); return 1; }
    coder_run_mission(target, m, out_buf, max_out);
    return 1;
}
/* PARCHE 097: "coderlog" muestra el detalle del ultimo trabajo del programador */
static int cmd_coderlog(const char *args, char *out_buf, uint32_t max_out)
{
    static char t[6144]; uint32_t p = 0;
    int sz = vfs_size("/agent/coder_ultimo.txt");
    if (sz <= 0) { fb_puts(out_buf, max_out, &p, "Aun no hay ningun trabajo del programador registrado."); return 1; }
    int full = args && (args[0] == 't' || args[0] == 'c');                 /* "coderlog todo" */
    char *b = (char *)kmalloc((uint32_t)sz + 1);
    if (!b) return 1;
    int r = vfs_read("/agent/coder_ultimo.txt", b, (uint32_t)sz); if (r < 0) r = 0; b[r] = 0;
    t[0] = 0; uint32_t k = 0;
    /* resumen: solo las lineas de bitacora (# ...) y las cabeceras; "coderlog todo" incluye las respuestas del modelo */
    for (char *l = b; *l; ) {
        char *e = l; while (*e && *e != '\n') e++;
        int keep = full || l[0] == '#' || l[0] == '=' || (l[0] == 'M' && l[1] == 'i') || (l[0] == 'A' && l[1] == 'r');
        if (keep) for (char *q = l; q < e && k + 2 < sizeof t; q++) t[k++] = *q;
        if (keep && k + 2 < sizeof t) t[k++] = '\n';
        l = *e ? e + 1 : e;
    }
    t[k] = 0;
    kfree(b);
    kprint(t);
    fb_puts(out_buf, max_out, &p, t);
    fb_puts(out_buf, max_out, &p, full ? "" : "\n(Con 'coderlog todo' ves tambien lo que respondio el modelo. Archivo completo: /agent/coder_ultimo.txt)");
    return 1;
}

/* PARCHE 093: "studio [archivo.soma]" abre SOMA Studio con el editor */
static int cmd_studio(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && *args) coder_app_load_project(args);
    gui_open_coder_app();
    fb_puts(out_buf, max_out, &p, "SOMA Studio abierto.");
    return 1;
}

#if defined(__x86_64__)
/* SOMA_154_T480S: "pantalla" */
static int cmd_pantalla(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    bootfb_print_info();
    fb_puts(out_buf, max_out, &p, "");
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_155_T480S: "teclado [es|us]" */
static int cmd_teclado(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'e' && args[1] == 's') keyboard_set_layout(1);
    else if (args && args[0] == 'u' && args[1] == 's') keyboard_set_layout(0);
    i8042_print_info();
    kprint("  Distribucion: "); kprint(keyboard_get_layout() ? "espanola (es)\n" : "la de siempre (us + n y tildes)\n");
    kprint("  Cambiar: teclado es | teclado us   (al arrancar: soma.teclado=es|us en la linea de GRUB)\n");
    fb_puts(out_buf, max_out, &p, "");
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_157_T480S: "ramtest [MiB]": comprueba la memoria alta (reserva, escribe, verifica, libera) */
static int cmd_ramtest(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t mib = 0;
    while (args && *args == ' ') args++;
    while (args && *args >= '0' && *args <= '9') mib = mib * 10 + (uint32_t)(*args++ - '0');
    static char b[1024];
    hw_ramtest(mib, b, sizeof b);
    kprint(b);
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
#endif
#if defined(__x86_64__)
/* SOMA_162_T480S: "disco": donde guarda SOMA los archivos (pendrive SOMADATA, virtio o solo RAM) */
static int cmd_disco(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    const struct usbdisk_info *u = usbdisk_info();
    if (args && args[0] == 's') {
        int r = usbdisk_flush();
        kprint(r == 0 ? "disco: cache del pendrive vaciada\n" : "disco: no hay pendrive como disco del sistema\n");
        return 1;
    }
    kprint("Disco del sistema: "); kprint(sysdisk_describe()); kprint("\n");
    if (u->active) {
        uint64_t r, w, e, t; usbdisk_stats(&r, &w, &e, &t);
        kprint("  pendrive: particion "); kprint_dec((uint32_t)u->part_index); kprint(" del MBR, sectores "); kprint_dec((uint32_t)u->part_lba);
        kprint(".."); kprint_dec((uint32_t)(u->part_lba + u->part_sectors - 1)); kprint(" (SOMA usa "); kprint_dec((uint32_t)(u->sectors / 2048)); kprint(" MiB)\n");
        kprint("  estado: "); kprint(u->readonly ? (u->lost ? "SOLO LECTURA (el pendrive cambio o no responde)" : "solo lectura") : "lectura y escritura");
        kprint(u->dirty ? " | cambios pendientes de vaciar\n" : " | todo vaciado al pendrive\n");
        kprint("  peticiones: lecturas "); kprint_dec((uint32_t)r); kprint(", escrituras "); kprint_dec((uint32_t)w); kprint(", errores "); kprint_dec((uint32_t)e);
        kprint(", reintentos "); kprint_dec((uint32_t)t); kprint(", vaciados "); kprint_dec((uint32_t)u->syncs); kprint("\n");
    } else {
        kprint("  pendrive: "); kprint(u->why && u->why[0] ? u->why : "no se intento"); kprint("\n");
    }
    kprint("uso: disco | disco sync (vacia la cache del pendrive; se hace sola cada 5 s)\n");
    return 1;
}
/* SOMA_228_ARRANQUE: "informe": guarda el informe de arranque y hardware (INFORME.TXT del pendrive y /informe.txt); "informe ver" lo muestra */
static int cmd_informe(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'v') {
        uint32_t cap = 240u * 1024u; char *b = (char *)kmalloc(cap);
        if (!b) { kprint("informe: sin memoria\n"); return 1; }
        bootdiag_report(b, cap); kprint(b); kfree(b);
        return 1;
    }
    const char *pf = bootdiag_prev_failure();
    kprint("Arranque: "); if (bootdiag_in_boot()) kprint("en curso"); else { kprint("completo en "); kprint_dec(bootdiag_boot_ms()); kprint(" ms"); } kprint("\n");
    if (pf[0]) { kprint("El arranque anterior no llego al escritorio: se quedo en la "); kprint(pf); kprint("\n"); }
    bootdiag_report_save_async();
    kprint("informe: generando y guardando en segundo plano (INFORME.TXT de la particion de arranque del pendrive y /informe.txt).\n");
    kprint("'informe ver' lo muestra aqui. El estado aparece tambien en Ajustes > Sistema.\n");
    return 1;
}

/* SOMA_165_T480S: "bateria": carga, autonomia, salud y cargador */
static void pr_cap(uint32_t v, int ma) { if (v == 0xFFFFFFFF) { kprint("?"); return; } kprint_dec(v); kprint(ma ? " mAh" : " mWh"); }
static void pr_min(uint32_t m) { kprint_dec(m / 60); kprint(" h "); if (m % 60 < 10) kprint("0"); kprint_dec(m % 60); kprint(" min"); }
static int cmd_bateria(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out; if (out_buf) out_buf[0] = 0;
    const struct acpi_sys_info *a = acpi_sys_info();
    if (!a->active) { kprint("bateria: sin ACPI ("); kprint(a->why); kprint(")\n"); return 1; }
    acpi_power_refresh_now();
    struct acpi_power_state ps; acpi_power_state(&ps);
    kprint("Cargador: "); kprint(ps.ac == 1 ? "enchufado" : ps.ac == 0 ? "desenchufado" : "desconocido");
    kprint(" | tapa: "); kprint(ps.lid == 1 ? "abierta" : ps.lid == 0 ? "cerrada" : "desconocida"); kprint("\n");
    if (!a->nbat) { kprint("No hay baterias en el firmware.\n"); return 1; }
    if (ps.percent != 0xFFFFFFFF) {
        kprint("Total: "); kprint_dec(ps.percent); kprint(" %");
        kprint(ps.charging ? " | cargando" : ps.discharging ? " | descargando" : " | sin carga ni descarga");
        if (ps.minutes != 0xFFFFFFFF) { kprint(ps.discharging ? " | quedan " : " | llena en "); pr_min(ps.minutes); }
        kprint("\n");
    }
    for (int i = 0; i < a->nbat; i++) {
        struct acpi_battery b; int r = acpi_battery_read(i, &b);
        kprint("  bateria "); kprint_dec((uint32_t)i); kprint(": ");
        if (r <= 0) { kprint(r == 0 ? "no colocada\n" : "no se pudo leer\n"); continue; }
        if (b.model[0]) { kprint(b.oem); kprint(" "); kprint(b.model); kprint(" | "); }
        if (b.percent != 0xFFFFFFFF) { kprint_dec(b.percent); kprint(" % | "); }
        kprint("queda "); pr_cap(b.remaining, b.unit_ma); kprint(" de "); pr_cap(b.full, b.unit_ma);
        kprint(" (de fabrica "); pr_cap(b.design, b.unit_ma); kprint(")");
        if (b.health != 0xFFFFFFFF) { kprint(" | salud "); kprint_dec(b.health); kprint(" %"); }
        kprint("\n    ");
        kprint(b.critical ? "CRITICA | " : ""); kprint(b.charging ? "cargando" : b.discharging ? "descargando" : "en reposo");
        if (b.rate != 0xFFFFFFFF) { kprint(" a "); kprint_dec(b.rate); kprint(b.unit_ma ? " mA" : " mW"); }
        if (b.voltage != 0xFFFFFFFF) { kprint(" | "); kprint_dec(b.voltage); kprint(" mV"); }
        if (b.cycles != 0xFFFFFFFF && b.cycles) { kprint(" | ciclos "); kprint_dec(b.cycles); }
        kprint("\n");
    }
    uint32_t pb, lid, bat; acpi_event_counts(&pb, &lid, &bat);
    kprint("Eventos: boton "); kprint_dec(pb); kprint(", tapa "); kprint_dec(lid); kprint(", bateria "); kprint_dec(bat);
    kprint(a->pb_fixed ? " | el boton de encendido apaga SOMA limpiamente\n" : "\n");
    return 1;
}
/* SOMA_208: "escritorio reiniciar" devuelve iconos y gadget a su sitio de fabrica */
extern void gui_desktop_layout_reset(void);
extern int gui_stress_request(int n, char *out, uint32_t max);   /* SOMA_220_PRUEBA */
#if defined(__x86_64__)
extern volatile int gui_rescue_inject;                            /* SOMA_221_RESCATE */
#endif
static int cmd_escritorio(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    if (args && args[0] == 'r') { gui_desktop_layout_reset(); kprint("escritorio: iconos y gadget en su posicion original\n"); }
    else if (args && args[0] == 'p') {                       /* SOMA_220_PRUEBA: escritorio prueba [n] */
        int n = 0; const char *q = args; while (*q && *q != ' ') ++q; while (*q == ' ') ++q;
        while (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0');
        static char res[2048];
        int r = gui_stress_request(n ? n : 20, res, sizeof res);
        if (r == -1) kprint("escritorio prueba: el escritorio no esta en marcha\n");
        else if (r == -2) kprint("escritorio prueba: no termino a tiempo\n");
        else kprint(res);
    }
#if defined(__x86_64__)
    else if (args && args[0] == 'f') {                       /* SOMA_221_RESCATE: escritorio fallo memoria|cuelgue */
        const char *q = args; while (*q && *q != ' ') ++q; while (*q == ' ') ++q;
        gui_rescue_inject = (*q == 'c') ? 2 : 1;
        kprint(*q == 'c' ? "escritorio: la proxima aplicacion que se dibuje entra en un bucle sin fin (se rescata a los 10 s)\n"
                         : "escritorio: la proxima aplicacion que se dibuje hace un acceso a memoria no valido\n");
    }
#endif
    else kprint("uso: escritorio reiniciar | escritorio prueba [n] | escritorio fallo memoria|cuelgue   (arrastra iconos y gadget con el raton para colocarlos; se guardan en /etc/escritorio.conf)\n");
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* SOMA_164B_T480S: "orbe prueba" */
extern int orb_parallel_selftest(char *out, uint32_t max);
static int cmd_orbe(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'p') {
        static char b[512];
        orb_parallel_selftest(b, sizeof b);
        kprint(b);
        if (out_buf && max_out) out_buf[0] = 0;
        return 1;
    }
    extern uint64_t orb_perf_last[16]; extern uint32_t orb_perf_period, orb_perf_windows; extern uint64_t tsc_ticks_per_ms;
    extern uint32_t her_lay_hits, her_lay_miss; extern int her_lay_on;
    {   /* SOMA_209: orbe eco on|off | orbe cintas on|off | orbe verifica [on|off] */
        extern int her_eco_on, her_ribl_on, her_eco_verify; extern uint32_t her_eco_stat[10]; extern int orb_eco_selftest(char *out, uint32_t max);
        const char *q = args ? args : ""; while (*q && *q != ' ') q++; while (*q == ' ') q++;
        int on = (q[0] == 'o' && q[1] == 'n'), off = (q[0] == 'o' && q[1] == 'f');
        if (args && args[0] == 'e') {
            if (on) her_eco_on = 1; else if (off) her_eco_on = 0;
            kprint("ahorro por fotograma (halo por tabla, borrado y volcado por zonas): "); kprint(her_eco_on ? "activado\n" : "desactivado\n");
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'v' && args[1] == 'e' && args[2] == 'n') {
            extern int her_bc_on;
            if (on) her_bc_on = 1; else if (off) her_bc_on = 0;
            kprint("contenido de ventanas en cache (hasta 70 ms): "); kprint(her_bc_on ? "activado" : "desactivado");
            kprint(" | copiado "); kprint_dec(her_eco_stat[2]); kprint(", pintado "); kprint_dec(her_eco_stat[3]); kprint("\n");
            her_eco_stat[2] = her_eco_stat[3] = 0;
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'c' && args[1] == 'i') {
            if (on) her_ribl_on = 1; else if (off) her_ribl_on = 0;
            kprint("cintas en capa (se renuevan cada 80 ms): "); kprint(her_ribl_on ? "activado" : "desactivado (se pintan en cada fotograma)");
            kprint(" | cintas pintadas "); kprint_dec(her_eco_stat[0]); kprint(", reutilizadas "); kprint_dec(her_eco_stat[1]);
            kprint(" | halo calculado "); kprint_dec(her_eco_stat[9]); kprint(", copiado "); kprint_dec(her_eco_stat[8]); kprint("\n");
            her_eco_stat[0] = her_eco_stat[1] = her_eco_stat[8] = her_eco_stat[9] = 0;
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
        if (args && args[0] == 'v') {
            static char b[384];
            if (on || off) { extern int32_t fb_verify_box[4]; fb_verify_box[0] = fb_verify_box[1] = 1 << 30; fb_verify_box[2] = fb_verify_box[3] = -1; her_eco_verify = on; for (int z = 0; z < 10; z++) her_eco_stat[z] = 0; kprint(on ? "verificacion en marcha (1 de cada 8 fotogramas; la interfaz ira algo mas lenta): usa el escritorio y repite 'orbe verifica'\n" : "verificacion apagada\n"); }
            else {
                orb_eco_selftest(b, sizeof b); kprint(b);
                kprint("en vivo ("); kprint(her_eco_verify ? "verificando" : "sin verificar: 'orbe verifica on'"); kprint("): fotogramas "); kprint_dec(her_eco_stat[7]);
                kprint(" | pixeles sin borrar "); kprint_dec(her_eco_stat[5]); kprint(" | pantalla distinta del bufer "); kprint_dec(her_eco_stat[6]);
                kprint(" | contenidos de ventana distintos "); kprint_dec(her_eco_stat[4]); kprint("\n");
                { extern int32_t fb_verify_box[4]; if (fb_verify_box[2] >= 0) { kprint("  pixeles sin borrar en la caja "); kprint_dec((uint32_t)fb_verify_box[0]); kprint(","); kprint_dec((uint32_t)fb_verify_box[1]); kprint(" - "); kprint_dec((uint32_t)fb_verify_box[2]); kprint(","); kprint_dec((uint32_t)fb_verify_box[3]); kprint("\n"); } }
                kprint("contenido de ventanas: copiado "); kprint_dec(her_eco_stat[2]); kprint(", pintado "); kprint_dec(her_eco_stat[3]); kprint("\n");
            }
            if (out_buf && max_out) out_buf[0] = 0;
            return 1;
        }
    }
    if (args && args[0] == 'c') {            /* SOMA_207: orbe capa on|off */
        const char *q = args + 1; while (*q && *q != ' ') q++; while (*q == ' ') q++;
        if (q[0] == 'o' && q[1] == 'f') her_lay_on = 0; else if (q[0] == 'o') her_lay_on = 1;
        kprint("capa de fondo en cache: "); kprint(her_lay_on ? "activada" : "desactivada");
        kprint(" | repintados reutilizados "); kprint_dec(her_lay_hits); kprint(", recalculados "); kprint_dec(her_lay_miss); kprint("\n");
        { extern uint32_t her_lay_why[16]; kprint("  motivos: sin_copia "); kprint_dec(her_lay_why[0]); kprint(" | omitida "); kprint_dec(her_lay_why[1]); kprint(" | caducada "); kprint_dec(her_lay_why[2]); kprint(" | cambio "); kprint_dec(her_lay_why[3]); kprint("\n"); kprint("  causas omitida: fresco "); kprint_dec(her_lay_why[4]); kprint(" arrastre "); kprint_dec(her_lay_why[5]); kprint(" iconos "); kprint_dec(her_lay_why[6]); kprint(" anim "); kprint_dec(her_lay_why[7]); kprint(" lanzador "); kprint_dec(her_lay_why[8]); kprint(" agente "); kprint_dec(her_lay_why[9]); kprint(" resp "); kprint_dec(her_lay_why[10]); kprint(" vortice "); kprint_dec(her_lay_why[11]); kprint(" flash "); kprint_dec(her_lay_why[12]); kprint("\n"); for (int z = 0; z < 16; z++) her_lay_why[z] = 0; }
        her_lay_hits = her_lay_miss = 0;
        if (out_buf && max_out) out_buf[0] = 0;
        return 1;
    }
    uint64_t d = tsc_ticks_per_ms / 100; if (!d) d = 1;                       /* centesimas de ms */
    static const char *nm[] = { "fondo y orbe", "marcos", "contenido de ventanas", "mezcla", "volcado a pantalla" };
    static const char *sub[] = { "borrado", "halo y cintas", "particulas", "", "anillo y destellos", "zonas y dock", "  de ello, halo", "  de ello, cintas" };
    kprint("Fotograma del escritorio (media de 60; "); kprint_dec(orb_perf_windows); kprint(orb_perf_windows == 1 ? " ventana abierta)\n" : " ventanas abiertas)\n");
    uint64_t tot = 0;
    for (int i = 0; i < 5; i++) { uint64_t v = orb_perf_last[i] / d; tot += v; kprint("  "); kprint(nm[i]); kprint(": "); kprint_dec((uint32_t)(v / 100)); kprint(","); if (v % 100 < 10) kprint("0"); kprint_dec((uint32_t)(v % 100)); kprint(" ms\n");
        if (i == 0) for (int k = 0; k < 8; k++) { if (!sub[k][0]) continue; uint64_t w = orb_perf_last[8 + k] / d; kprint("      "); kprint(sub[k]); kprint(": "); kprint_dec((uint32_t)(w / 100)); kprint(","); if (w % 100 < 10) kprint("0"); kprint_dec((uint32_t)(w % 100)); kprint(" ms\n"); } }
    kprint("  total "); kprint_dec((uint32_t)(tot / 100)); kprint(","); if (tot % 100 < 10) kprint("0"); kprint_dec((uint32_t)(tot % 100));
    kprint(" ms | el orbe va a "); kprint_dec(orb_perf_period ? 1000u / orb_perf_period : 0); kprint(" fps (cada "); kprint_dec(orb_perf_period); kprint(" ms) | nucleos "); kprint_dec((uint32_t)smp_cpus()); kprint("\n");
    {   /* SOMA_209: parte de la pantalla que se vuelca de media por fotograma desde la ultima consulta */
        extern uint64_t fb_dirty_px; extern uint32_t fb_dirty_n; static uint64_t lp; static uint32_t ln;
        uint64_t dpx = fb_dirty_px - lp; uint32_t dn = fb_dirty_n - ln; lp = fb_dirty_px; ln = fb_dirty_n;
        uint64_t scr = (uint64_t)fb_get_width() * fb_get_height();
        if (dn && scr) { kprint("  volcado medio: "); kprint_dec((uint32_t)(dpx * 100u / dn / scr)); kprint(" % de la pantalla por volcado ("); kprint_dec(dn); kprint(" volcados)\n"); }
    }
    kprint("uso: orbe | orbe eco|cintas|ventanas|capa [on|off] | orbe verifica [on|off] | orbe prueba (mismo dibujo con 1 y con todos los nucleos) | cpus 1 / cpus todos para comparar\n");
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* SOMA_166_T480S: "energia": perfil, temperatura, frecuencia real y consumo del procesador */
static void pr_w(uint32_t mw) { kprint_dec(mw / 1000); kprint(","); kprint_dec((mw % 1000) / 100); kprint(" W"); }
static int cmd_energia(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    const struct cpupower_info *ci = cpupower_info();
    if (args && *args) {
        int p = args[0] == 'r' ? CP_RENDIMIENTO : args[0] == 'e' ? CP_EQUILIBRADO : args[0] == 'a' ? CP_AHORRO : -1;
        if (p < 0) { kprint("uso: energia | energia rendimiento | energia equilibrado | energia ahorro\n"); return 1; }
        cpupower_set_profile(p, 1);
        kprint("energia: perfil "); kprint(cpupower_profile_name(p)); kprint(" (guardado en /etc/energia.conf)\n");
    }
    kprint("Perfil: "); kprint(cpupower_profile_name(ci->profile));
    if (ci->hwp_on) {
        kprint(" | HWP (Speed Shift): de "); kprint_dec(ci->hwp_lowest * 100); kprint(" a "); kprint_dec((ci->profile == CP_AHORRO ? ci->hwp_guaranteed : ci->hwp_highest) * 100);
        kprint(" MHz (base "); kprint_dec(ci->hwp_guaranteed * 100); kprint(", turbo "); kprint_dec(ci->hwp_highest * 100); kprint(")");
    } else kprint(" | sin HWP (frecuencia que deja el firmware)");
    kprint("\nReposo: "); if (ci->mwait) { kprint("MWAIT hasta C"); kprint_dec((uint32_t)ci->deep_c); kprint(ci->profile == CP_RENDIMIENTO ? " (en rendimiento: C1)" : ""); if (!ci->arat) kprint(" (el nucleo 0 en C1: sin ARAT)"); }
    else kprint(ci->hypervisor ? "HLT (C1; en maquina virtual no se usa MWAIT)" : "HLT (C1)");
    if (ci->pkg_cst_known) { kprint(" | limite del firmware para el paquete: "); kprint_dec(ci->pkg_cst_limit); }
    kprint("\n");
    struct cpupower_sample sm;
    if (cpupower_measure(&sm, 500) != 0) { kprint("(sin lecturas)\n"); return 1; }
    if (ci->rapl) { kprint("Consumo (0,5 s): paquete "); pr_w(sm.pkg_mw); if (ci->rapl_pp0) { kprint(" | nucleos "); pr_w(sm.cores_mw); } if (ci->rapl_pp1) { kprint(" | grafica "); pr_w(sm.gfx_mw); } kprint("\n"); }
    else kprint("Consumo: sin medidor RAPL\n");
    if (sm.pkg_temp > -1000) { kprint("Temperatura del paquete: "); kprint_dec((uint32_t)sm.pkg_temp); kprint(" C (limite "); kprint_dec((uint32_t)ci->tjmax); kprint(" C)\n"); }
    for (int i = 0; i < sm.ncores && i < CP_MAXCORE; i++) {
        kprint("  nucleo "); kprint_dec((uint32_t)i); kprint(": ");
        if (sm.temp[i] > -1000) { kprint_dec((uint32_t)sm.temp[i]); kprint(" C  "); }
        if (sm.mhz[i]) { kprint_dec(sm.mhz[i]); kprint(" MHz  despierto "); kprint_dec(sm.busy_pm[i] / 10); kprint(","); kprint_dec(sm.busy_pm[i] % 10); kprint(" %"); }
        else kprint("(sin APERF/MPERF)");
        kprint("\n");
    }
    kprint("uso: energia | energia rendimiento | energia equilibrado | energia ahorro\n");
    return 1;
}
/* SOMA_168_T480S: "brillo": luz del panel y teclas Fn del ThinkPad */
#include "backlight.h"
#include "acpi_sys.h"
#include "hda.h"
static int cmd_brillo(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    struct backlight_info b; backlight_get(&b);
    while (args && *args == ' ') args++;
    if (args && *args) {
        if (!b.ok) { kprint("brillo: "); kprint(b.why); kprint("\n"); return 1; }
        uint32_t p = b.pct;
        if (*args == '+') p = p + 10 > 100 ? 100 : p + 10;
        else if (*args == '-') p = p < 10 ? 0 : p - 10;
        else if (*args >= '0' && *args <= '9') { p = 0; while (*args >= '0' && *args <= '9') p = p * 10 + (uint32_t)(*args++ - '0'); if (p > 100) p = 100; }
        else { kprint("uso: brillo | brillo 0-100 | brillo + | brillo -\n"); return 1; }
        if (backlight_set_pct(p, 1) != 0) kprint("brillo: la grafica no acepto el cambio\n");
        backlight_get(&b);
    }
    kprint("Brillo: ");
    if (b.ok) {
        kprint_dec(b.pct); kprint(" %"); if (b.blanked) kprint(" (apagado: tapa cerrada)");
        kprint(" | PWM del chipset: ciclo "); kprint_dec(b.duty); kprint(" de "); kprint_dec(b.max);
        kprint(" (minimo "); kprint_dec(b.max / 50 ? b.max / 50 : 1); kprint(")\n");
    } else { kprint("no disponible: "); kprint(b.why); kprint("\n"); }
    if (b.gpu_vendor && b.gpu_vendor != 0xFFFF) {
        kprint("  grafica "); kprint_hex32(b.gpu_vendor); kprint(":"); kprint_hex32(b.gpu_device);
        kprint(" | BAR0 0x"); kprint_hex32((uint32_t)(b.bar >> 32)); kprint_hex32((uint32_t)b.bar);
        if (b.ok || b.ctl1 || b.ctl2) { kprint(" | CTL1 0x"); kprint_hex32(b.ctl1); kprint(" CTL2 0x"); kprint_hex32(b.ctl2); }
        kprint("\n");
    }
    const struct acpi_sys_info *ai = acpi_sys_info();
    kprint("Teclas: volumen y silencio por el teclado | EC "); kprint(ai->ec_node ? "si" : "no");
    kprint(" (consultas "); kprint_dec(ai->ec_queries); kprint(") | HKEY ");
    if (ai->hkey) { kprint("activo, version 0x"); kprint_hex32(ai->hkey_ver); kprint(", teclas 0x"); kprint_hex32(ai->hkey_mask); }
    else kprint("no");
    kprint(" | video "); kprint(ai->video_bcl ? (ai->dos_ok ? "si (_DOS 4)" : "si") : "no"); 
    kprint(" | volumen "); if (hda_ready()) { kprint_dec((uint32_t)hda_get_volume()); kprint(" %"); } else kprint("sin audio"); kprint("\n");
    kprint("  pulsaciones "); kprint_dec(b.key_events); kprint(" | avisos de video "); kprint_dec(b.video_events);
    kprint(" | eventos HKEY "); kprint_dec(b.hkey_events); kprint(" | tapa "); kprint_dec(ai->lid_actions); kprint("\n");
    uint32_t c[8]; int k[8], n = 0; hotkey_recent(c, k, &n);
    if (n) {
        kprint("  ultimos eventos:");
        for (int i = 0; i < n; i++) { kprint(k[i] == 1 ? " EC _Q" : k[i] == 2 ? " HKEY " : " video "); kprint_hex32(c[i]); }
        kprint("\n");
    }
    kprint("uso: brillo 0-100 | brillo + | brillo -   (Fn+F5/F6 cambian el brillo; Fn+F1/F2/F3 el volumen)\n");
    return 1;
}
/* SOMA_167_T480S: "trackpad": gestos del trackpad Synaptics */
#include "syn_gest.h"
extern int trackpad_save_config(int gestos, int natural, int tap, int speed);
extern void trackpad_status(int *gestos, int *natural, int *tap, int *speed, const struct syn_state **st, uint32_t *fallbacks);
static int cmd_trackpad(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    if (!i8042_touchpad()) { kprint("trackpad: no hay un trackpad Synaptics (raton PS/2 normal o USB)\n"); return 1; }
    int g, nat, tap, sp; const struct syn_state *st; uint32_t fb;
    trackpad_status(&g, &nat, &tap, &sp, &st, &fb);
    while (args && *args == ' ') args++;
    if (args && *args) {
        const char *a = args, *v = a; while (*v && *v != ' ') v++; while (*v == ' ') v++;
        int on = !(v[0] == 'n' && v[1] == 'o') && !(v[0] == 'o' && v[1] == 'f') && v[0] != '0';
        int want_g = g;
        if (a[0] == 'g') want_g = 1;
        else if (a[0] == 'r') want_g = 0;
        else if (a[0] == 'n') nat = *v ? on : !nat;
        else if (a[0] == 't') tap = *v ? on : !tap;
        else if (a[0] == 'v') { int n = 0; while (*v >= '0' && *v <= '9') n = n * 10 + (*v++ - '0'); if (n >= 1 && n <= 10) sp = n; }
        else { kprint("uso: trackpad | trackpad gestos | trackpad relativo | trackpad natural si|no | trackpad toque si|no | trackpad velocidad 1-10\n"); return 1; }
        trackpad_save_config(want_g, nat, tap, sp);
        trackpad_status(&g, &nat, &tap, &sp, &st, &fb);
        if (want_g && !g) kprint("trackpad: el trackpad no acepto el modo absoluto\n");
    }
    uint32_t e09, e0c, caps = i8042_syn_caps(&e09, &e0c);
    kprint("Trackpad Synaptics: modo "); kprint(g ? "GESTOS (absoluto)" : "relativo (el de fabrica)");
    kprint(" | clickpad "); kprint(i8042_syn_clickpad() ? "si" : "no"); kprint(" | TrackPoint por pass-through "); kprint(i8042_syn_passthrough() ? "si" : "no");
    kprint("\n  capacidades 0x"); kprint_hex32(caps); kprint(" ext09 0x"); kprint_hex32(e09); kprint(" ext0C 0x"); kprint_hex32(e0c); kprint("\n");
    kprint("  toque = clic: "); kprint(tap ? "si" : "no"); kprint(" | desplazamiento "); kprint(nat ? "natural (como el Mac)" : "clasico"); kprint(" | velocidad "); kprint_dec((uint32_t)sp); kprint("\n");
    if (g) {
        kprint("  paquetes "); kprint_dec(st->packets); kprint(" (malos "); kprint_dec(st->bad); kprint(") | TrackPoint "); kprint_dec(st->tp_packets);
        kprint(" | toques "); kprint_dec(st->taps); kprint(", con dos dedos "); kprint_dec(st->rtaps); kprint(" | desplazamientos "); kprint_dec(st->scrolls);
        kprint(" | palmas "); kprint_dec(st->palms); kprint("\n  ultimo: W "); kprint_dec((uint32_t)st->last_w); kprint(", presion "); kprint_dec((uint32_t)st->last_z);
        kprint(", dedos "); kprint_dec((uint32_t)st->last_fingers); kprint("\n");
    }
    if (fb) { kprint("  vueltas automaticas al modo relativo: "); kprint_dec(fb); kprint("\n"); }
    kprint("Gestos: un dedo mueve | toque = clic | toque con dos dedos = clic derecho | dos dedos arriba/abajo = desplazar |\n");
    kprint("        clic con dos dedos = clic derecho | TrackPoint + boton central = desplazar\n");
    kprint("uso: trackpad gestos | relativo | natural si|no | toque si|no | velocidad 1-10\n");
    return 1;
}
/* SOMA_164_T480S: "acpi": estado del interprete y dispositivos del firmware */
struct acpi_ls { int n, shown, all; };
static const char *acpi_hid_what(const char *h)
{
    static const struct { const char *hid, *what; } t[] = {
        { "PNP0C0A", "bateria" }, { "PNP0C0D", "tapa" }, { "PNP0C0C", "boton de encendido" }, { "PNP0C0E", "boton de suspender" },
        { "PNP0C09", "controlador embebido (EC)" }, { "ACPI0003", "cargador (AC)" }, { "LEN0068", "ThinkPad ACPI (teclas Fn)" },
        { "LEN0268", "ThinkPad ACPI (teclas Fn)" }, { "PNP0C14", "WMI" }, { "INT33A1", "energia de Intel" }, { "PNP0A08", "bus PCI Express" },
        { "PNP0A03", "bus PCI" }, { "PNP0103", "HPET" }, { "ACPI000C", "procesadores (agregador)" }, { "PNP0C0B", "ventilador" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) { const char *x = t[i].hid, *y = h; while (*x && *x == *y) { x++; y++; } if (!*x && !*y) return t[i].what; }
    return 0;
}
static void acpi_dev_cb(const char *path, const char *hid, uint32_t sta, void *u)
{
    struct acpi_ls *l = u; l->n++;
    if (!(sta & 1)) return;                                          /* no presente */
    const char *what = acpi_hid_what(hid);
    if (!l->all && !what) return;
    l->shown++;
    kprint("  "); kprint(hid); { int k = 0; while (hid[k]) k++; for (; k < 9; k++) kprint(" "); }
    kprint(path);
    if (what) { kprint("  <- "); kprint(what); }
    kprint("\n");
}
static int cmd_acpi(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out; if (out_buf) out_buf[0] = 0;
    const struct acpi_sys_info *a = acpi_sys_info();
    kprint("ACPI: "); kprint(a->active ? "interprete uACPI activo" : a->why);
    if (!a->active && a->status) { kprint(" ("); kprint(a->status); kprint(")"); }
    kprint("\n");
    if (!a->active) return 1;
    kprint("  arranque "); kprint_dec(a->init_ms); kprint(" ms | avisos "); kprint_dec(a->warnings); kprint(", errores "); kprint_dec(a->errors); kprint("\n");
    kprint("  EC: ");
    if (a->ec_present) {
        kprint("puertos 0x"); kprint_hex16(a->ec_cmd); kprint("/0x"); kprint_hex16(a->ec_data); kprint(a->ec_from_ecdt ? " (ECDT)" : " (por defecto)");
        kprint(" | accesos "); kprint_dec((uint32_t)a->ec_ops); kprint(", sin respuesta "); kprint_dec((uint32_t)a->ec_timeouts);
    } else kprint("no hay");
    kprint("\n  SCI: IRQ "); kprint_dec(a->sci_irq); kprint(" por sondeo cada 100 ms | eventos atendidos "); kprint_dec((uint32_t)a->sci_handled); kprint("\n");
    while (args && *args == ' ') args++;
    struct acpi_ls l = { 0, 0, args && args[0] == 't' };
    kprint(l.all ? "Dispositivos presentes (_HID):\n" : "Dispositivos conocidos ('acpi todos' para la lista completa):\n");
    acpi_for_each_device(acpi_dev_cb, &l);
    kprint("  ("); kprint_dec((uint32_t)l.shown); kprint(" mostrados de "); kprint_dec((uint32_t)l.n); kprint(" con _HID)\n");
    kprint("uso: acpi | acpi todos | poweroff (apaga por ACPI S5) | reboot\n");
    return 1;
}
/* SOMA_158_T480S: "irq": como llegan las interrupciones (PIC o APIC), el reloj y los contadores */
static int cmd_irq(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    const struct apic_sys_info *ai = apic_system_info();
    kprint("Interrupciones: "); kprint(ai->active ? "APIC (LAPIC + IOAPIC), PIC 8259 enmascarado" : "PIC 8259 (modo antiguo)");
    kprint("\n  estado: "); kprint(ai->why ? ai->why : "sin iniciar"); kprint("\n");
    if (ai->active) {
        kprint("  LAPIC id "); kprint_dec(ai->lapic_id); kprint(ai->x2apic ? " (x2APIC)" : " (xAPIC)");
        kprint(" | ARAT "); kprint(ai->arat ? "si" : "no");
        kprint(" | IOAPIC id "); kprint_dec(ai->ioapic_id); kprint(" en 0x"); kprint_hex32(ai->ioapic_addr); kprint("\n");
        kprint("  reloj: ");
        if (ai->timer_lapic) {
            kprint("temporizador del LAPIC, "); kprint_dec(ai->ticks_per_ms); kprint(" cuentas/ms (bus del LAPIC ");
            kprint_dec(ai->ticks_per_ms * 16u / 1000u); kprint(" MHz)");
        } else kprint("PIT 1 kHz por el IOAPIC");
        kprint(" | comprobacion: "); kprint_dec(ai->verify_ticks); kprint(" ticks en 30 ms\n");
        const char *nm[3] = { "IRQ0  PIT     ", "IRQ1  teclado ", "IRQ12 trackpad" };
        for (int i = 0; i < 3; i++) {
            kprint("  "); kprint(nm[i]); kprint(" -> GSI "); kprint_dec(ai->gsi[i]);
            kprint(ai->level[i] ? " nivel" : " flanco"); kprint(ai->low[i] ? "/baja" : "/alta");
            if (ai->io_used[i]) { uint64_t e = apic_io_redir(ai->io_index[i]); kprint(" | IOAPIC: vector "); kprint_dec((uint32_t)(e & 0xFF)); kprint((e >> 16) & 1 ? " ENMASCARADA" : " activa"); }
            else kprint(" | no se usa");
            kprint("\n");
        }
    }
    kprint("  atendidas: reloj "); kprint_dec((uint32_t)idt_irq_count(32)); kprint(", teclado "); kprint_dec((uint32_t)idt_irq_count(33));
    kprint(", trackpad "); kprint_dec((uint32_t)idt_irq_count(44)); kprint(", espurias LAPIC "); kprint_dec((uint32_t)idt_irq_count(255));
    kprint(", EOI LAPIC "); kprint_dec(apic_eoi_count()); kprint("\n  uptime "); kprint_dec((uint32_t)(timer_get_uptime_ms() / 1000)); kprint(" s\n");
    if (ai->active) {
        uint64_t tk = 0, ts = 0; apic_clock_drift(&tk, &ts);
        kprint("  precision: el reloj conto "); kprint_dec((uint32_t)tk); kprint(" ms; el TSC mide "); kprint_dec((uint32_t)ts); kprint(" ms");
        if (ts > 1000) {
            int64_t dpm = ((int64_t)tk - (int64_t)ts) * 1000 / (int64_t)ts;    /* por mil */
            kprint(" (deriva "); if (dpm < 0) { kprint("-"); dpm = -dpm; } kprint_dec((uint32_t)dpm); kprint(" por mil)");
        }
        kprint("\n");
    }
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
#endif
/* SOMA_156_T480S: "dmesg [N | texto]": registro del nucleo desde el arranque (ultimas N lineas, o las que contienen texto) */
static int dmesg_has(const char *l, uint32_t n, const char *w)
{
    for (uint32_t i = 0; i < n; i++) { uint32_t k = 0; while (w[k] && i + k < n && l[i + k] == w[k]) k++; if (!w[k]) return 1; }
    return 0;
}
static int cmd_dmesg(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    uint32_t want = 60; const char *filt = 0;
    if (args && *args >= '0' && *args <= '9') { want = 0; while (*args >= '0' && *args <= '9') want = want * 10 + (uint32_t)(*args++ - '0'); if (want > 3000) want = 3000; }
    else if (args && *args) { filt = args; want = 300; }
    if (!want) want = 1;
    char *snap = (char *)kmalloc(256u * 1024u + 1);
    if (!snap) { kprint("dmesg: sin memoria\n"); return 1; }
    uint32_t n = klog_snapshot(snap, 256u * 1024u + 1);
    /* 1) cuantas lineas cumplen (todas, o las que contienen el texto) */
    uint32_t total = 0;
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i; while (j < n && snap[j] != '\n') j++;
        if (!filt || dmesg_has(snap + i, j - i, filt)) total++;
        i = j + 1;
    }
    /* 2) se imprimen solo las `want` ultimas */
    uint32_t skip = total > want ? total - want : 0, idx = 0, shown = 0;
    klog_pause(1);                                             /* lo que imprime dmesg no se vuelve a registrar */
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i; while (j < n && snap[j] != '\n') j++;
        if (!filt || dmesg_has(snap + i, j - i, filt)) {
            if (idx++ >= skip) {                                 /* SOMA_156C: sin codigos de control (un "clear" registrado borraba la pantalla) */
                char ln[256]; uint32_t w = 0;
                for (uint32_t r = i; r < j && w < sizeof ln - 1; r++) {
                    char ch = snap[r];
                    if (ch == 27) { if (r + 1 < j && snap[r + 1] == '[') { r += 2; while (r < j && !((snap[r] >= 'A' && snap[r] <= 'Z') || (snap[r] >= 'a' && snap[r] <= 'z'))) r++; } continue; }
                    if ((unsigned char)ch < 32 && ch != '\t') continue;
                    ln[w++] = ch;
                }
                ln[w] = 0; kprint(ln); kprint("\n"); shown++;
            }
        }
        i = j + 1;
    }
    kprint("[dmesg: "); kprint_dec(shown); kprint(" de "); kprint_dec(total); kprint(filt ? " lineas con ese texto" : " lineas");
    kprint(" | "); kprint_dec((uint32_t)(klog_written() / 1024)); kprint(" KiB registrados. Uso: dmesg [N | texto]]\n");
    klog_pause(0);
    kfree(snap);
    if (out_buf && max_out) out_buf[0] = 0;
    return 1;
}
/* PARCHE 085: "navegador [url]" */
static int cmd_navegador(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("navegador", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    uint32_t p = 0;
    while (args && *args == ' ') args++;
    if (args && *args) gui_open_browser(args); else gui_open_web();
    fb_puts(out_buf, max_out, &p, "Navegador abierto.");
    return 1;
}

/* PARCHE 085: "ajustes" - transparencia de las ventanas y fuente de la interfaz */
static int cmd_ajustes(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; uint32_t p = 0;
    gui_open_settings();
    fb_puts(out_buf, max_out, &p, "Ajustes abiertos.");
    return 1;
}

/* PARCHE 084: "latex [archivo.tex]" - abre el maquetador (documento LaTeX) */
static int cmd_latex(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t p = 0;
    kprint("Iniciando el maquetador LaTeX...\n");
    if (*args) gui_open_layout_file(args); else gui_open_layout();
    fb_puts(out_buf, max_out, &p, "Maquetador abierto.");
    return 1;
}

static int cmd_paint(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t p = 0;
    kprint("Iniciando SOMA Paint...\n");
    gui_open_paint(args);
    fb_puts(out_buf, max_out, &p, "SOMA Paint abierto.");
    return 1;
}

/* SOMA_PARCHE_024_TERM: terminal [debug] -> abre la ventana del terminal de la GUI */
static int cmd_terminal(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    int dbg = (args[0] == 'd' && args[1] == 'e' && args[2] == 'b');
    kprint("Iniciando Terminal SOMA...\n");
    gui_open_terminal(dbg);
    uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Terminal cerrado.");
    return 1;
}

/* SOMA_PARCHE_023_MEDIA: media gui [ruta] | list | play ruta | pause | stop | seek seg | status */
static int cmd_media(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (args[0] == 'g' && args[1] == 'u' && args[2] == 'i' && (args[3] == ' ' || args[3] == 0)) {
        args += 3; while (*args == ' ') args++;
        kprint("Iniciando SOMA Media...\n");
        gui_open_media(args);
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "SOMA Media cerrado.");
        return 1;
    }
    if (media_command(args, out_buf, max_out)) return 1;
    uint32_t p = 0;
    fb_puts(out_buf, max_out, &p, "uso: media gui [ruta] | list | play <ruta> | pause | stop | seek <segundos> | status\n(rutas del almacen BIGFS: .avi con video MJPG + audio PCM, o .wav)");
    kprint(out_buf); kprint("\n");
    return 1;
}

static int cmd_memview(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t p = 0;
    kprint("Iniciando Visor de Memoria Multi-Agente...\n");
    gui_open_memview();
    fb_puts(out_buf, max_out, &p, "Visor de memoria multi-agente abierto.");
    return 1;
}

static int cmd_view(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (!*args) {
        kprint("Uso: view <archivo.bmp|ppm>\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: view <archivo.bmp>");
        return 1;
    }

    static uint8_t v_buf[24576];
    int r = vfs_read(args, (char *)v_buf, sizeof(v_buf) - 1);
    if (r <= 0) {
        kprint("view: archivo no encontrado: '"); kprint(args); kprint("'\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "view: archivo no encontrado");
        return 1;
    }

    struct image img;
    if (image_decode(v_buf, (size_t)r, &img) == 0) {
        kprint("\n[IMAGEN]: "); kprint(args);
        kprint(" | Formato: "); kprint(img.format == IMAGE_FMT_BMP ? "BMP TrueColor" : "PPM");
        kprint(" | Dimensiones: "); kprint_dec(img.width); kprint("x"); kprint_dec(img.height);
        kprint(" px | Pixeles: "); kprint_dec(img.width * img.height); kprint("\n");

        if (fb_is_active()) {
            image_render(&img, 100, 100, (int32_t)img.width, (int32_t)img.height);
            fb_present();
            kprint("[view]: Renderizada en pantalla grafica activa.\n");
        } else {
            kprint("[view]: Para verla en pantalla completa, abre el navegador con 'vsoma'.\n");
        }

        uint32_t p = 0;
        fb_puts(out_buf, max_out, &p, "Imagen valida: ");
        fb_put_dec(out_buf, max_out, &p, img.width); fb_puts(out_buf, max_out, &p, "x");
        fb_put_dec(out_buf, max_out, &p, img.height);
        image_free(&img);
    } else {
        kprint("view: formato de imagen no reconocido o archivo corrupto.\n");
        uint32_t p = 0; fb_puts(out_buf, max_out, &p, "view: formato no soportado");
    }
    return 1;
}

static int cmd_font(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    /* SOMA_VECTOR_FONTS_PATCH: 'font size <40-300>' y 'font contrast <0-100>' */
    if (args[0] == 's' && args[1] == 'i' && args[2] == 'z' && args[3] == 'e' &&
        (args[4] == ' ' || args[4] == '\0')) {
        const char *q = args + 4;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_size(v);
        }
        kprint("\n\033[1;32m[FONT]: tamano vectorial = ");
        kprint_dec((uint32_t)font_vector_get_size());
        kprint("%\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Tamano vectorial: ");
        fb_put_dec(out_buf, max_out, &pos, font_vector_get_size());
        fb_puts(out_buf, max_out, &pos, "%");
        if (fb_is_active()) fb_present_full();
        return 1;
    }
    if (args[0] == 'c' && args[1] == 'o' && args[2] == 'n' && args[3] == 't' &&
        args[4] == 'r' && args[5] == 'a' && args[6] == 's' && args[7] == 't' &&
        (args[8] == ' ' || args[8] == '\0')) {
        const char *q = args + 8;
        while (*q == ' ') q++;
        if (*q >= '0' && *q <= '9') {
            int v = 0;
            while (*q >= '0' && *q <= '9' && v < 100000) { v = v * 10 + (*q - '0'); q++; }
            font_vector_set_contrast(v);
        }
        kprint("\n\033[1;32m[FONT]: contraste vectorial = ");
        kprint_dec((uint32_t)font_vector_get_contrast());
        kprint("\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Contraste vectorial: ");
        fb_put_dec(out_buf, max_out, &pos, font_vector_get_contrast());
        if (fb_is_active()) fb_present_full();
        return 1;
    }


    if (*args == '\0') {
        kprint("\nFUENTES TIPOGRAFICAS DISPONIBLES EN SOMA:\n");
        kprint("------------------------------------------\n");
        fb_puts(out_buf, max_out, &pos, "Fuentes disponibles:\n");
        int cur = font_get_current();
        for (int order = 0; order < font_get_count(); ++order) {
            int i = font_get_sorted_id(order);
            if (i < 0) continue;
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(font_get_name(i));
            kprint(" - ");
            kprint(font_get_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVA\033[0m");
            kprint("\n");

            fb_puts(out_buf, max_out, &pos, "  [");
            fb_put_dec(out_buf, max_out, &pos, i);
            fb_puts(out_buf, max_out, &pos, "] ");
            fb_puts(out_buf, max_out, &pos, font_get_name(i));
            fb_puts(out_buf, max_out, &pos, " - ");
            fb_puts(out_buf, max_out, &pos, font_get_desc(i));
            if (i == cur) fb_puts(out_buf, max_out, &pos, " (ACTIVA)");
            fb_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("------------------------------------------\n");
        kprint("Uso: font <nombre|id>  (ej. 'font modern' o 'font 0')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9') {
        int fid = 0;
        const char *q = args;
        while (*q >= '0' && *q <= '9' && fid < 100000) { fid = fid * 10 + (*q - '0'); q++; }
        while (*q == ' ') q++;
        if (*q != '\0') fid = -1;
        ok = font_set(fid);
    } else {
        ok = font_set_by_name(args);
    }

    if (ok) {
        int cur = font_get_current();
        kprint("\n\033[1;32m[FONT]: Fuente del sistema cambiada a: '");
        kprint(font_get_name(cur));
        kprint("' (");
        kprint(font_get_desc(cur));
        kprint(")\033[0m\n\n");

        fb_puts(out_buf, max_out, &pos, "Fuente cambiada a: ");
        fb_puts(out_buf, max_out, &pos, font_get_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Fuente no reconocida '");
        kprint(args);
        kprint("'. Escribe 'font' para listar las disponibles.\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Error: fuente no reconocida");
    }
    return 1;
}

static int cmd_cursor(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t pos = 0;

    if (*args == '\0') {
        kprint("\nPUNTEROS DE RATON DISPONIBLES EN SOMA:\n");
        kprint("---------------------------------------\n");
        fb_puts(out_buf, max_out, &pos, "Punteros disponibles:\n");
        int cur = mouse_get_current_cursor();
        for (int i = 0; i < mouse_get_cursor_count(); ++i) {
            kprint("  ["); kprint_dec((uint32_t)i); kprint("] ");
            kprint(mouse_get_cursor_name(i));
            kprint(" - ");
            kprint(mouse_get_cursor_desc(i));
            if (i == cur) kprint("  \033[1;32m<-- ACTIVO\033[0m");
            kprint("\n");

            fb_puts(out_buf, max_out, &pos, "  [");
            fb_put_dec(out_buf, max_out, &pos, i);
            fb_puts(out_buf, max_out, &pos, "] ");
            fb_puts(out_buf, max_out, &pos, mouse_get_cursor_name(i));
            fb_puts(out_buf, max_out, &pos, " - ");
            fb_puts(out_buf, max_out, &pos, mouse_get_cursor_desc(i));
            if (i == cur) fb_puts(out_buf, max_out, &pos, " (ACTIVO)");
            fb_putc(out_buf, max_out, &pos, '\n');
        }
        kprint("---------------------------------------\n");
        kprint("Uso: cursor <nombre|id>  (ej. 'cursor crosshair' o 'cursor 2')\n\n");
        return 1;
    }

    int ok = 0;
    if (*args >= '0' && *args <= '9' && *(args + 1) == '\0') {
        ok = mouse_set_cursor(*args - '0');
    } else {
        ok = mouse_set_cursor_by_name(args);
    }

    if (ok) {
        int cur = mouse_get_current_cursor();
        kprint("\n\033[1;32m[CURSOR]: Puntero cambiado a: '");
        kprint(mouse_get_cursor_name(cur));
        kprint("' (");
        kprint(mouse_get_cursor_desc(cur));
        kprint(")\033[0m\n\n");

        fb_puts(out_buf, max_out, &pos, "Puntero cambiado a: ");
        fb_puts(out_buf, max_out, &pos, mouse_get_cursor_name(cur));

        if (fb_is_active()) {
            fb_present_full();
        }
    } else {
        kprint("\n\033[1;31m[ERROR]: Puntero no reconocido '");
        kprint(args);
        kprint("'. Escribe 'cursor' para ver la lista.\033[0m\n\n");
        fb_puts(out_buf, max_out, &pos, "Error: puntero no reconocido");
    }
    return 1;
}

/* ====================================================================
 * PARCHE_TERMINAL_V2: nuevas utilidades de terminal
 * ==================================================================== */

static uint32_t shell_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void shell_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void shell_put_dec_pad(char *out, uint32_t max, uint32_t *p, uint64_t v, uint32_t width)
{
    uint32_t n = 0;
    uint64_t t = v;
    if (t == 0) n = 1;
    else while (t) { n++; t /= 10; }
    for (uint32_t i = n; i < width; ++i) fb_putc(out, max, p, ' ');
    fb_put_dec(out, max, p, v);
}

/* Copia el primer argumento (admite comillas) y devuelve el resto de la linea. */
static const char *shell_next_arg(const char *s, char *dst, uint32_t cap)
{
    uint32_t n = 0;
    char q = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '"' || *s == '\'') q = *s++;
    while (*s && n + 1 < cap) {
        if (q ? (*s == q) : (*s == ' ' || *s == '\t')) break;
        dst[n++] = *s++;
    }
    if (q && *s == q) s++;
    dst[n] = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Recorre la lista "Archivos: a, b, c" que produce vfs_format_list_ext. */
static const char *shell_list_start(const char *list)
{
    const char *c = find_substr_ci(list, ": ", 0);
    return c ? c + 2 : list;
}

static const char *shell_list_next(const char *p, char *item, uint32_t cap)
{
    uint32_t n = 0;
    while (*p == ' ' || *p == ',') p++;
    while (*p && *p != ',' && n + 1 < cap) item[n++] = *p++;
    while (n && item[n - 1] == ' ') n--;
    item[n] = '\0';
    return p;
}

static void shell_out_line(char *out_buf, uint32_t max_out, uint32_t *pos, const char *line)
{
    kprint(line);
    kprint("\n");
    if (out_buf && max_out) {
        fb_puts(out_buf, max_out, pos, line);
        fb_putc(out_buf, max_out, pos, '\n');
    }
}

/* ---- identidad ---- */
static int cmd_whoami(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    fb_puts(t, sizeof(t), &p, env_get("USER"));
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_hostname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[ENV_VAL_LEN + 4];
    uint32_t p = 0;
    (void)args;
    fb_puts(t, sizeof(t), &p, env_get("HOST"));
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

static int cmd_uname(const char *args, char *out_buf, uint32_t max_out)
{
    char t[192];
    uint32_t p = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    fb_puts(t, sizeof(t), &p, "SOMA");
    if (args[0] == '-' && args[1] == 'a') {
        fb_putc(t, sizeof(t), &p, ' ');
        fb_puts(t, sizeof(t), &p, env_get("HOST"));
        fb_puts(t, sizeof(t), &p, " 0.2 ");
        fb_puts(t, sizeof(t), &p, MYOS_ARCH_NAME);
        fb_puts(t, sizeof(t), &p, " bare-metal Ring0 multi-agente");
    }
    fb_putc(t, sizeof(t), &p, '\n');
    shell_emit(t, out_buf, max_out);
    return 1;
}

/* ---- resumen global del sistema ---- */
static void sys_put_ip(char *o, uint32_t max, uint32_t *p, const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        fb_put_dec(o, max, p, ip[i]);
        if (i < 3) fb_putc(o, max, p, '.');
    }
}

static int cmd_sysinfo(const char *args, char *out_buf, uint32_t max_out)
{
    char rep[1536];
    uint32_t p = 0;
    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0, threads = 0;
    struct ramstat_info ri;
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000), m = s / 60, h = m / 60;
    (void)args;
    s %= 60; m %= 60;

    ramstat_get(&ri);
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);
    {
        char *tt = (char *)kmalloc(2048);
        if (tt) {
            thread_format_table(tt, 2048);
            for (uint32_t i = 0; tt[i]; ++i) if (tt[i] == '\n') threads++;
            if (threads) threads--;
            kfree(tt);
        }
    }

    rep[0] = '\0';
    fb_puts(rep, sizeof(rep), &p, "=== ESTADO DEL SISTEMA ===\nSOMA 0.3 pre-beta | arch ");
    fb_puts(rep, sizeof(rep), &p, MYOS_ARCH_NAME);
    fb_puts(rep, sizeof(rep), &p, " | host ");
    fb_puts(rep, sizeof(rep), &p, env_get("HOST"));
    fb_puts(rep, sizeof(rep), &p, " | usuario ");
    fb_puts(rep, sizeof(rep), &p, env_get("USER"));
    fb_puts(rep, sizeof(rep), &p, "\nUptime    : ");
    if (h) { fb_put_dec(rep, sizeof(rep), &p, h); fb_puts(rep, sizeof(rep), &p, "h "); }
    fb_put_dec(rep, sizeof(rep), &p, m); fb_puts(rep, sizeof(rep), &p, "m ");
    fb_put_dec(rep, sizeof(rep), &p, s); fb_puts(rep, sizeof(rep), &p, "s\nRAM       : ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.footprint);
    fb_puts(rep, sizeof(rep), &p, " / ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.total);
    fb_puts(rep, sizeof(rep), &p, " (");
    ram_put_pct10(rep, sizeof(rep), &p, ri.pct10);
    fb_puts(rep, sizeof(rep), &p, ") | libre ");
    ram_put_mib_raw(rep, sizeof(rep), &p, ri.free_b);
    fb_puts(rep, sizeof(rep), &p, "\nAlmacen.  : ");
    fb_put_dec(rep, sizeof(rep), &p, files_u);
    fb_putc(rep, sizeof(rep), &p, '/');
    fb_put_dec(rep, sizeof(rep), &p, FS_MAX_FILES);
    fb_puts(rep, sizeof(rep), &p, " inodos | ");
    fb_put_dec(rep, sizeof(rep), &p, bytes_u / 1024);
    fb_puts(rep, sizeof(rep), &p, " KiB usados | pendientes de sync: ");
    fb_put_dec(rep, sizeof(rep), &p, dirty_c);
    fb_puts(rep, sizeof(rep), &p, "\nHilos     : ");
    fb_put_dec(rep, sizeof(rep), &p, threads);
    fb_puts(rep, sizeof(rep), &p, " activos\nRed       : IP ");
    sys_put_ip(rep, sizeof(rep), &p, net_ip);
    fb_puts(rep, sizeof(rep), &p, " | GW ");
    sys_put_ip(rep, sizeof(rep), &p, net_gateway);
    fb_puts(rep, sizeof(rep), &p, "\n(detalle: ram -v | df | ps | status | pci)\n");
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- find / du / stat ---- */
static int cmd_find(const char *args, char *out_buf, uint32_t max_out)
{
    char term[48], root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t hits = 0, p = 0;
    term[0] = root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!term[0]) shell_copy(term, tok, sizeof(term));
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    if (!term[0]) {
        shell_emit("Uso: find <texto> [ruta] [-a]   (-a incluye ocultos y de sistema)\n", out_buf, max_out);
        return 1;
    }
    char *list = (char *)kmalloc(4096);
    char *rep  = (char *)kmalloc(4096);
    if (!list || !rep) {
        if (list) kfree(list);
        if (rep) kfree(rep);
        kprint("find: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && find_substr_ci(item, term, 1)) {
            fb_puts(rep, 4096, &p, item);
            fb_putc(rep, 4096, &p, '\n');
            hits++;
        }
    }
    if (!hits) fb_puts(rep, 4096, &p, "find: sin coincidencias.\n");
    else { fb_put_dec(rep, 4096, &p, hits); fb_puts(rep, 4096, &p, " coincidencia(s)\n"); }
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(rep);
    return 1;
}

static int cmd_du(const char *args, char *out_buf, uint32_t max_out)
{
    char root[48], tok[48], item[FS_NAME_MAX + 4];
    int show_all = 0;
    uint32_t total = 0, count = 0, used = 0, bytes_u = 0, dirty = 0, p = 0;
    root[0] = '\0';
    if (!args) args = "";
    while (*args) {
        args = shell_next_arg(args, tok, sizeof(tok));
        if (!tok[0]) { if (!*args) break; continue; }
        if (tok[0] == '-' && tok[1] == 'a' && tok[2] == '\0') show_all = 1;
        else if (!root[0]) shell_copy(root, tok, sizeof(root));
    }
    char *list = (char *)kmalloc(4096);
    char *data = (char *)kmalloc(FS_DATA_MAX);
    char *rep  = (char *)kmalloc(6144);
    if (!list || !data || !rep) {
        if (list) kfree(list);
        if (data) kfree(data);
        if (rep) kfree(rep);
        kprint("du: sin memoria.\n");
        return 1;
    }
    rep[0] = '\0';
    vfs_format_list_ext(show_all, root[0] ? root : 0, list, 4096);
    const char *q = shell_list_start(list);
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (!item[0]) continue;
        int r = vfs_size(item);
        uint32_t sz = (r > 0) ? (uint32_t)r : 0;
        shell_put_dec_pad(rep, 6144, &p, sz, 7);
        fb_puts(rep, 6144, &p, "  ");
        fb_puts(rep, 6144, &p, item);
        fb_putc(rep, 6144, &p, '\n');
        total += sz;
        count++;
    }
    vfs_get_stats(&used, &bytes_u, &dirty);
    fb_puts(rep, 6144, &p, "Total: ");
    fb_put_dec(rep, 6144, &p, count);
    fb_puts(rep, 6144, &p, " archivos, ");
    fb_put_dec(rep, 6144, &p, total);
    fb_puts(rep, 6144, &p, " bytes (");
    fb_put_dec(rep, 6144, &p, total / 1024);
    fb_puts(rep, 6144, &p, " KiB) | inodos libres: ");
    fb_put_dec(rep, 6144, &p, (used <= FS_MAX_FILES) ? (FS_MAX_FILES - used) : 0);
    fb_putc(rep, 6144, &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(list);
    kfree(data);
    kfree(rep);
    return 1;
}

static int cmd_stat(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], rep[512];
    uint32_t p = 0, lines = 0;
    int binary = 0;
    if (!args) args = "";
    shell_next_arg(args, path, sizeof(path));
    if (!path[0]) {
        shell_emit("Uso: stat <archivo>\n", out_buf, max_out);
        return 1;
    }
    uint32_t dl = 0; char *data = vfs_read_alloc(path, &dl);   /* PARCHE 087 */
    int r = data ? (int)dl : -1;
    if (r < 0) {
        fb_puts(rep, sizeof(rep), &p, "stat: no encontrado: ");
        fb_puts(rep, sizeof(rep), &p, path);
        fb_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        kfree(data);
        return 1;
    }
    for (int i = 0; i < r; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\n') lines++;
        else if (c < 9 || (c > 13 && c < 32)) binary = 1;
    }
    if (r > 0 && data[r - 1] != '\n') lines++;
    rep[0] = '\0';
    fb_puts(rep, sizeof(rep), &p, "Archivo     : ");
    fb_puts(rep, sizeof(rep), &p, path);
    fb_puts(rep, sizeof(rep), &p, "\nTamano      : ");
    fb_put_dec(rep, sizeof(rep), &p, (uint64_t)r);
    fb_puts(rep, sizeof(rep), &p, " bytes\nTipo        : ");
    fb_puts(rep, sizeof(rep), &p, r == 0 ? "vacio" : (binary ? "binario" : "texto"));
    if (r > 0 && !binary) {
        fb_puts(rep, sizeof(rep), &p, "\nLineas      : ");
        fb_put_dec(rep, sizeof(rep), &p, lines);
    }
    fb_puts(rep, sizeof(rep), &p, "\nVisibilidad : ");
    fb_puts(rep, sizeof(rep), &p, vfs_is_hidden(path) ? "oculto" : "visible");
    fb_puts(rep, sizeof(rep), &p, "\nAlmacenaje  : ");
    if (find_substr_ci(path, "/proc/", 0) == path || find_substr_ci(path, "/dev/", 0) == path)
        fb_puts(rep, sizeof(rep), &p, "pseudo-archivo del kernel");
    else if (find_substr_ci(path, "/tmp/", 0) || find_substr_ci(path, "/sys/", 0))
        fb_puts(rep, sizeof(rep), &p, "volatil (solo RAM)");
    else
        fb_puts(rep, sizeof(rep), &p, "persistente (virtio-blk)");
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    kfree(data);
    return 1;
}

/* ---- directorios (el VFS los representa con un marcador oculto <dir>/.dir) ---- */
static int shell_dir_path(const char *args, char *path, uint32_t cap)
{
    char tok[FS_NAME_MAX + 8];
    uint32_t p = 0;
    shell_next_arg(args ? args : "", tok, sizeof(tok));
    if (!tok[0]) return 0;
    if (tok[0] != '/') {
        const char *pre = "/workspace/";
        while (*pre && p + 1 < cap) path[p++] = *pre++;
    }
    for (uint32_t i = 0; tok[i] && p + 1 < cap; ++i) path[p++] = tok[i];
    while (p > 1 && path[p - 1] == '/') p--;
    path[p] = '\0';
    return (int)p;
}

static int shell_user_space(const char *path)
{
    static const char *const roots[] = { "/workspace", "/mis_archivos", 0 };
    for (int r = 0; roots[r]; ++r) {
        uint32_t i = 0;
        while (roots[r][i] && path[i] == roots[r][i]) ++i;
        if (!roots[r][i] && (path[i] == '\0' || path[i] == '/')) return 1;
    }
    return 0;
}

static int shell_dir_marker(const char *path, int n, char *marker, uint32_t cap)
{
    if (n <= 1 || (uint32_t)n + 6 > cap || (uint32_t)n + 6 > FS_NAME_MAX) return 0;
    for (int i = 0; i < n; ++i) marker[i] = path[i];
    marker[n] = '/'; marker[n + 1] = '.'; marker[n + 2] = 'd';
    marker[n + 3] = 'i'; marker[n + 4] = 'r'; marker[n + 5] = '\0';
    return 1;
}

static int cmd_mkdir(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: mkdir <ruta>   (relativa a /workspace si no empieza por /)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("mkdir: los agentes solo pueden crear directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("mkdir: ruta demasiado larga para el VFS (maximo 42 caracteres).\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) >= 0) {
        fb_puts(rep, sizeof(rep), &p, "mkdir: ya existe: ");
    } else if (vfs_write(marker, "", 0) < 0) {
        fb_puts(rep, sizeof(rep), &p, "mkdir: no se pudo crear (VFS lleno o ruta protegida): ");
    } else {
        fb_puts(rep, sizeof(rep), &p, "Directorio creado: ");
    }
    fb_puts(rep, sizeof(rep), &p, path);
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

static int cmd_rmdir(const char *args, char *out_buf, uint32_t max_out)
{
    char path[FS_NAME_MAX + 8], marker[FS_NAME_MAX + 16], prefix[FS_NAME_MAX + 16];
    char item[FS_NAME_MAX + 4], probe[2], rep[160];
    uint32_t p = 0;
    int n = shell_dir_path(args, path, sizeof(path));
    if (n <= 1) {
        shell_emit("Uso: rmdir <ruta>   (solo directorios vacios)\n", out_buf, max_out);
        return 1;
    }
    if (agent_dispatch_mode && !shell_user_space(path)) {
        shell_emit("rmdir: los agentes solo pueden borrar directorios en /workspace o /mis_archivos.\n", out_buf, max_out);
        return 1;
    }
    if (!shell_dir_marker(path, n, marker, sizeof(marker))) {
        shell_emit("rmdir: ruta no valida.\n", out_buf, max_out);
        return 1;
    }
    rep[0] = '\0';
    if (vfs_read(marker, probe, sizeof(probe)) < 0) {
        fb_puts(rep, sizeof(rep), &p, "rmdir: no existe: ");
        fb_puts(rep, sizeof(rep), &p, path);
        fb_putc(rep, sizeof(rep), &p, '\n');
        shell_emit(rep, out_buf, max_out);
        return 1;
    }
    shell_copy(prefix, marker, sizeof(prefix));
    prefix[n + 1] = '\0';                      /* "<ruta>/" */
    char *list = (char *)kmalloc(2048);
    if (!list) { kprint("rmdir: sin memoria.\n"); return 1; }
    vfs_format_list_ext(1, prefix, list, 2048);
    const char *q = shell_list_start(list);
    int nonempty = 0;
    while (*q) {
        q = shell_list_next(q, item, sizeof(item));
        if (item[0] && !str_eq(item, marker)) { nonempty = 1; break; }
    }
    kfree(list);
    if (nonempty) {
        fb_puts(rep, sizeof(rep), &p, "rmdir: el directorio no esta vacio: ");
    } else if (vfs_delete(marker) == 0) {
        fb_puts(rep, sizeof(rep), &p, "Directorio eliminado: ");
    } else {
        fb_puts(rep, sizeof(rep), &p, "rmdir: no se pudo eliminar: ");
    }
    fb_puts(rep, sizeof(rep), &p, path);
    fb_putc(rep, sizeof(rep), &p, '\n');
    shell_emit(rep, out_buf, max_out);
    return 1;
}

/* ---- sort / uniq (archivo o tuberia) ---- */
#define SHELL_MAX_LINES 512

/* Devuelve una copia en heap del texto de entrada (archivo o stdin de tuberia). */
static char *shell_read_input(const char *fn, const char *who)
{
    char *buf;
    if (fn && fn[0]) {
        buf = vfs_read_alloc(fn, 0);                        /* PARCHE 087 */
        if (!buf) { kprint(who); kprint(": no encontrado: "); kprint(fn); kprint("\n"); return 0; }
        return buf;
    }
    if (pipe_stdin_data) {
        uint32_t n = shell_strlen(pipe_stdin_data);
        buf = (char *)kmalloc(n + 1);
        if (!buf) { kprint(who); kprint(": sin memoria.\n"); return 0; }
        memcpy(buf, pipe_stdin_data, n + 1);
        return buf;
    }
    return 0;
}

static uint32_t shell_split_lines(char *buf, char **lines, uint32_t max)
{
    uint32_t n = 0;
    char *p = buf;
    while (*p && n < max) {
        lines[n++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = '\0'; p++; }
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t l = shell_strlen(lines[i]);
        if (l && lines[i][l - 1] == '\r') lines[i][l - 1] = '\0';
    }
    return n;
}

static int32_t shell_leading_int(const char *s)
{
    int neg = 0;
    int32_t v = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9' && v < 100000000) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static int shell_line_cmp(const char *a, const char *b, int numeric)
{
    if (numeric) {
        int32_t x = shell_leading_int(a), y = shell_leading_int(b);
        if (x != y) return x < y ? -1 : 1;
    }
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int cmd_sort(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    int rev = 0, num = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    while (*args == '-') {
        args++;
        while (*args && *args != ' ') {
            if (*args == 'r') rev = 1;
            else if (*args == 'n') num = 1;
            args++;
        }
        while (*args == ' ') args++;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "sort");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: sort [-r] [-n] [archivo]   o   <comando> | sort\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    for (uint32_t i = 1; i < n; ++i) {
        char *key = lines[i];
        uint32_t j = i;
        while (j > 0) {
            int c = shell_line_cmp(lines[j - 1], key, num);
            if (rev ? (c < 0) : (c > 0)) { lines[j] = lines[j - 1]; j--; }
            else break;
        }
        lines[j] = key;
    }
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ++i) shell_out_line(out_buf, max_out, &pos, lines[i]);
    kfree(buf);
    return 1;
}

static int cmd_uniq(const char *args, char *out_buf, uint32_t max_out)
{
    char fn[FS_NAME_MAX + 8];
    char *lines[SHELL_MAX_LINES];
    char tmp[300];
    int count = 0;
    uint32_t pos = 0;
    if (!args) args = "";
    while (*args == ' ') args++;
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == '\0')) {
        count = 1;
        args += 2;
    }
    shell_next_arg(args, fn, sizeof(fn));
    char *buf = shell_read_input(fn, "uniq");
    if (!buf) {
        if (!fn[0] && !pipe_stdin_data) shell_emit("Uso: uniq [-c] [archivo]   o   <comando> | uniq\n", out_buf, max_out);
        return 1;
    }
    uint32_t n = shell_split_lines(buf, lines, SHELL_MAX_LINES);
    if (out_buf && max_out) out_buf[0] = '\0';
    for (uint32_t i = 0; i < n; ) {
        uint32_t j = i + 1, tp = 0;
        while (j < n && str_eq(lines[i], lines[j])) j++;
        tmp[0] = '\0';
        if (count) {
            shell_put_dec_pad(tmp, sizeof(tmp), &tp, j - i, 4);
            fb_putc(tmp, sizeof(tmp), &tp, ' ');
        }
        fb_puts(tmp, sizeof(tmp), &tp, lines[i]);
        shell_out_line(out_buf, max_out, &pos, tmp);
        i = j;
    }
    kfree(buf);
    return 1;
}

/* SOMA_CODER_AUTONOMOUS_PATCH: Sensor sincrono de compilacion */
static int cmd_compile(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: compile <archivo.soma>\n");
        if (out_buf && max_out) {
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, "Uso: compile <archivo.soma>");
        }
        return 1;
    }

    char path[96]; uint32_t pp = 0; char q = 0;
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pp < sizeof(path) - 1) {
        if (q && *args == q) break;
        if (!q && *args == ' ') break;
        path[pp++] = *args++;
    }
    path[pp] = '\0';

    char *file_buf = (char *)kmalloc(16384);
    if (!file_buf) {
        kprint("compile: sin memoria.\n");
        if (out_buf && max_out) {
            uint32_t p = 0; fb_puts(out_buf, max_out, &p, "compile: sin memoria");
        }
        return 1;
    }

    int r = vfs_read(path, file_buf, 16383);
    if (r <= 0 && path[0] != '/') {
        char alt[112]; alt[0] = '/'; uint32_t k = 0;
        while (path[k] && k < sizeof(alt) - 2) { alt[k + 1] = path[k]; k++; }
        alt[k + 1] = '\0';
        r = vfs_read(alt, file_buf, 16383);
        if (r > 0) {
            for (uint32_t i = 0; alt[i] && i < sizeof(path) - 1; ++i) path[i] = alt[i];
            path[k + 1] = '\0';
        }
    }

    if (r <= 0) {
        kprint("compile: archivo no encontrado: '"); kprint(path); kprint("'\n");
        if (out_buf && max_out) {
            uint32_t p = 0;
            fb_puts(out_buf, max_out, &p, "[COMPILE ERROR] Archivo no encontrado: ");
            fb_puts(out_buf, max_out, &p, path);
        }
        kfree(file_buf);
        return 1;
    }

    file_buf[r] = '\0';

    if (r < 10) {
        kprint("compile: archivo vacio o contiene solo placeholder (< 10 bytes).\n");
        if (out_buf && max_out) {
            uint32_t p = 0;
            fb_puts(out_buf, max_out, &p, "[COMPILER ERROR]: Archivo vacio o incompleto. Escribe el codigo real sin '...'.");
        }
        kfree(file_buf);
        return 1;
    }

    static uint8_t test_bc[32768]; uint32_t test_bc_len = 0;
    int err = lang_compile(file_buf, (uint32_t)r, test_bc, sizeof(test_bc), &test_bc_len);

    uint32_t p = 0;
    if (out_buf && max_out) out_buf[0] = '\0';

    if (err == 0) {
        kprint("[COMPILE OK] '"); kprint(path); kprint("': 0 errores.\n");
        if (out_buf && max_out) {
            fb_puts(out_buf, max_out, &p, "[COMPILE OK] Compilacion exitosa: 0 errores (bytecode: ");
            fb_put_dec(out_buf, max_out, &p, test_bc_len);
            fb_puts(out_buf, max_out, &p, " bytes). Codigo valido y listo para 'app'.");
        }
    } else {
        const char *last_err = lang_get_last_error_msg();
        uint32_t err_line = lang_get_last_error_line();
        const char *err_tok = lang_get_last_error_token();

        kprint("[COMPILER ERROR L"); kprint_dec(err_line); kprint("]: ");
        kprint(last_err); kprint(" (cerca de: '"); kprint(err_tok); kprint("')\n");

        if (out_buf && max_out) {
            fb_puts(out_buf, max_out, &p, "[COMPILER ERROR L");
            fb_put_dec(out_buf, max_out, &p, err_line);
            fb_puts(out_buf, max_out, &p, "]: ");
            fb_puts(out_buf, max_out, &p, last_err);
            fb_puts(out_buf, max_out, &p, " (cerca de: '");
            fb_puts(out_buf, max_out, &p, err_tok);
            fb_puts(out_buf, max_out, &p, "'). Corrige este error con 'write'.");
        }
    }

    kfree(file_buf);
    return 1;
}

/* SOMA_LANG_LINT_CMD: Sensor estatico de codigo para shell y Coder */
int cmd_lang_lint(const char *args, char *out_buf, uint32_t max_out)
{
    char path[96];
    char *buf;
    uint32_t pp = 0, p = 0;
    char q = 0;
    int r;
    struct soma_lint_report rep;

    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: lint <archivo.soma>\n");
        if (out_buf && max_out) {
            fb_puts(out_buf, max_out, &p, "Uso: lint <archivo.soma>");
        }
        return 1;
    }
    if (*args == '\'' || *args == '"') q = *args++;
    while (*args && pp < sizeof(path) - 1) {
        if (q && *args == q) break;
        if (!q && *args == ' ') break;
        path[pp++] = *args++;
    }
    path[pp] = '\0';

    buf = (char *)kmalloc(16384);
    if (!buf) {
        kprint("lint: sin memoria.\n");
        return 1;
    }
    r = vfs_read(path, buf, 16383);
    if (r <= 0 && path[0] != '/') {
        char alt[112];
        uint32_t k = 0;
        alt[0] = '/';
        while (path[k] && k < sizeof(alt) - 2) { alt[k + 1] = path[k]; k++; }
        alt[k + 1] = '\0';
        r = vfs_read(alt, buf, 16383);
    }
    if (r <= 0) {
        kprint("lint: archivo no encontrado: '"); kprint(path); kprint("'\n");
        if (out_buf && max_out) {
            fb_puts(out_buf, max_out, &p, "lint: archivo no encontrado");
        }
        kfree(buf);
        return 1;
    }
    buf[r] = '\0';

    lang_lint(buf, (uint32_t)r, &rep);

    kprint("\n=== SOMA-LANG LINT: "); kprint(path); kprint(" ===\n");
    if (out_buf && max_out) out_buf[0] = '\0';

    if (rep.count == 0) {
        kprint("  0 errores, 0 advertencias. Codigo 100% limpio.\n\n");
        if (out_buf && max_out) fb_puts(out_buf, max_out, &p, "[LINT OK] 0 errores, 0 advertencias.");
    } else {
        uint32_t i;
        for (i = 0; i < rep.count; ++i) {
            kprint("  "); kprint(rep.msg[i]); kprint("\n");
            if (out_buf && max_out) {
                fb_puts(out_buf, max_out, &p, rep.msg[i]);
                fb_puts(out_buf, max_out, &p, "\n");
            }
        }
        kprint("\n");
    }
    kfree(buf);
    return 1;
}

static const struct command_entry cmd_table[] = {
        /* Scripting y Automatizacion (SOMA-LANG) */
    { "vm",           cmd_lang_vm,      CMD_F_SAFE,   "Test y estado de la maquina virtual SOMA-VM" },
    { "programa",     cmd_programa,     CMD_F_SAFE,   "Crea o modifica un programa SOMA-LANG con IA: programa [archivo.soma] <mision>" },
    { "coderlog",     cmd_coderlog,     CMD_F_SAFE,   "Detalle del ultimo trabajo del programador (coderlog todo = con respuestas)" },
    { "studio",       cmd_studio,       CMD_F_SAFE,   "Abre SOMA Studio (editor de codigo) [archivo]" },
    { "coder",        cmd_lang_coder,   CMD_F_SAFE,   "Agente programador autonomo SOMA" },
    { "run",          cmd_lang_run,     CMD_F_SAFE,   "Ejecuta un script o tokeniza codigo .soma" },
    { "app",          cmd_lang_run,     CMD_F_SAFE,   "Lanza una aplicacion SOMA-LANG (.soma) en ventana" },
    { "compile",      cmd_compile,      CMD_F_SAFE,   "Verifica y compila sintaxis de un script .soma sin ejecutar" },
    { "lint",         cmd_lang_lint,    CMD_F_SAFE,   "Analisis estatico de codigo SOMA-LANG (anti-Python, placeholders, bucles)" },
    { "check",        cmd_compile,      CMD_F_ALIAS | CMD_F_SAFE, "Alias de compile" }, /*PL5_CMD_APP*/
    { "history",       cmd_history,      CMD_F_SAFE,   "Muestra el historial persistente de comandos" },
    { "source",       cmd_source,       CMD_F_SAFE,   "Ejecuta un script de comandos por lotes (.sh)" },
    { "sh",           cmd_source,       CMD_F_ALIAS,  "Alias de source" },

    /* Sistema, Ayuda e Identidad */
    { "help",         cmd_help,         CMD_F_SAFE,   "Ayuda por categorias: help [categoria|comando|texto|all|agentes]" },
    { "somafetch",    cmd_somafetch,    CMD_F_SAFE,   "Ficha del sistema y telemetria en arte ASCII" },
    { "neofetch",     cmd_somafetch,    CMD_F_ALIAS,  "Alias de somafetch" },
    { "fetch",        cmd_somafetch,    CMD_F_ALIAS,  "Alias de somafetch" },
    { "creador",      cmd_creador,      CMD_F_SAFE,   "Muestra informacion de la autoría de SOMA" },
    { "status",       cmd_status,       CMD_F_SAFE,   "Muestra estado basico de red e IP" },
    { "uptime",       cmd_uptime,       CMD_F_SAFE,   "Muestra el tiempo de actividad del kernel" },
    { "hw",           cmd_hw,           CMD_F_SAFE,   "Hardware: ACPI (x86), arbol de dispositivos (ARM) y PCI; hw -v anade capacidades MSI/MSI-X/PCIe" },
    { "apic",         cmd_apic,         CMD_F_SAFE,   "Estado del LAPIC/IOAPIC (x86_64); apic test | apic route test|on|off: autotest del LAPIC y enrutado de IRQ por el IOAPIC" },
    { "ahci",         cmd_ahci,         CMD_F_SAFE,   "Controlador SATA AHCI (x86_64): info del disco; ahci test (E/S en el disco de prueba QEMU) | ahci read <lba>" },
    { "nvme",         cmd_nvme,         CMD_F_SAFE,   "Controlador NVMe (x86_64): info del disco; nvme test (E/S en el disco de prueba QEMU) | nvme read <lba>" },
    { "usb",          cmd_usb,          CMD_F_SAFE,   "USB xHCI (x86_64 y Pi 5): enumera los dispositivos; usb hid [on|off|status] teclado/raton USB; usb msc disco USB" },
    { "fat",          cmd_fat,          CMD_F_SAFE,   "Lee discos FAT12/16/32 (solo lectura): fat | fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>" },
    { "blk",          cmd_blk,          CMD_F_SAFE,   "Discos (virtio/AHCI/NVMe): blk | blk read <dev> <lba> | blk copy <o> <lba> <d> <lba> <n> | blk test" },
    { "ram",          cmd_ram,          CMD_F_SAFE,   "Memoria RAM: total/usada/libre, barra y heap (-s resumen, -v desglose; alias: free)" },
    { "date",         cmd_date,         CMD_F_SAFE,   "Fecha y hora real UTC desde el reloj CMOS RTC" },
    { "sysinfo",      cmd_sysinfo,      CMD_F_SAFE,   "Resumen del sistema: uptime, RAM, almacenamiento, hilos y red" },
    { "sys",          cmd_sysinfo,      CMD_F_ALIAS | CMD_F_SAFE, "Alias de sysinfo" },
    { "whoami",       cmd_whoami,       CMD_F_SAFE,   "Muestra el usuario actual" },
    { "hostname",     cmd_hostname,     CMD_F_SAFE,   "Muestra el nombre del equipo" },
    { "uname",        cmd_uname,        CMD_F_SAFE,   "Version y arquitectura de SOMA (uname -a: completo)" },
    { "clear",        cmd_clear,        CMD_F_SAFE,   "Limpia el buffer de pantalla VGA" },
    { "cls",          cmd_clear,        CMD_F_ALIAS,  "Alias de clear" },

    /* Variables de Entorno */
    { "env",          cmd_env,          CMD_F_SAFE,   "Muestra variables de entorno activas" },
    { "export",       cmd_export,       CMD_F_SAFE,   "Define o modifica una variable de entorno (VAR=val)" },
    { "unset",        cmd_unset,        CMD_F_SAFE,   "Elimina una variable de entorno activa" },

    /* Diagnóstico, Telemetría y Memoria */
    { "test_suite",   cmd_test_suite,   CMD_F_SAFE,   "Suite integral de auto-test y no-regresion (9 fases)" },
    { "test",         cmd_test_suite,   CMD_F_ALIAS,  "Alias de test_suite" },
    { "mem",          cmd_mem,          CMD_F_SAFE,   "Inspeccion fisica de CR0, CR3, CR4 y paginacion" },
    { "free",         cmd_ram,          CMD_F_ALIAS | CMD_F_SAFE, "Alias de ram" },
    { "df",           cmd_df,           CMD_F_SAFE,   "Capacidad de almacenamiento y estado de RamFS" },
    { "stats",        cmd_stats,        CMD_F_SAFE,   "Estadisticas de paquetes y bytes VirtIO-NET" },
    { "heap",         cmd_heap,         CMD_F_SAFE,   "Telemetria y auto-test del KHeap dinamico" },
    { "pmmbench",     cmd_pmmbench,     CMD_F_NORMAL, "DEPURACION: compara el asignador de marcos nuevo con el escaneo antiguo" },
    { "oomtest",      cmd_oomtest,      CMD_F_NORMAL, "DEPURACION: carga de procesos con memoria agotada en cada punto (sin fugas ni cuelgues)" },
    { "usfuzz",       cmd_usfuzz,       CMD_F_NORMAL, "DEPURACION: fuzzing hostil de syscalls desde ring 3: usfuzz [procesos] [syscalls por proceso] [semilla]" },
    { "demo_ai",      cmd_demo_ai,      CMD_F_NORMAL, "DEMO: la IA escribe un programa, las pruebas detectan sus fallos y los corrige (demo_ai vivo = con el LLM real)" },
    { "fscrash",      cmd_fscrash,      CMD_F_NORMAL, "DEPURACION: simula cortes de energia al guardar y comprueba que ningun archivo queda a medias" },
    { "fscheck",      cmd_fscheck,      CMD_F_NORMAL, "Comprueba el disco del VFS: cabeceras y CRC de cada archivo" },
    { "conctest",     cmd_conctest,     CMD_F_NORMAL, "DEPURACION: prueba de estres de concurrencia (6 hilos: mutex, heap y VFS)" },
    { "deadlock",     cmd_deadlock,     CMD_F_NORMAL, "DEPURACION: provoca un interbloqueo de dos hilos para probar su deteccion" },
    { "mutex",        cmd_mutex,        CMD_F_SAFE,   "Estado de los mutex del kernel: propietario, esperando, interbloqueos" },
    { "audio",        cmd_audio,        CMD_F_NORMAL, "Audio: audio info|tone [hz] [ms]|test|play <ruta.wav>|stop|vol N|stat" },
    { "mic",          cmd_mic,          CMD_F_NORMAL, "Microfono: mic info|fuentes|fuente N|ganancia N|sw N|nivel [s]|grabar [s] [Hz]|parar|reproducir|guardar [ruta]|cargar ruta|borrar [ruta]|probar|nodos|verbo nid verbo payload" },
    { "cam",          cmd_cam,          CMD_F_NORMAL, "Camara web: cam info|estado|iniciar [AnchoxAlto] [fps]|parar|foto [ruta]|video [segundos]|abrir" },
    { "captura",      cmd_captura,      CMD_F_NORMAL, "Captura de pantalla: captura estado|foto|video [s]|audio [s]|av [s]|iniciar video|audio|av|parar|fps N|escala N|abrir" },
    { "cap",          cmd_captura,      CMD_F_NORMAL, "Igual que captura" },
    { "camara",       cmd_camara,       CMD_F_NORMAL, "Abre la aplicacion Camara (vista previa, foto JPEG y video AVI con audio)" },
    { "grabadora",    cmd_grabadora,    CMD_F_NORMAL, "Abre la aplicacion Grabadora (microfono: grabar, escuchar, guardar WAV)" },
    { "big",          cmd_big,          CMD_F_NORMAL, "Almacen de archivos grandes: big info|format yes|ls|mkdir|rm|mv|stat|cat|put|get|write|truncate|fsck|bench" },
    { "exec",         cmd_exec,         CMD_F_NORMAL, "Ejecuta un programa ELF de usuario (ring 3): exec [-b] /bin/hello.elf [plazo_ms]" },
    { "procs",        cmd_procs,        CMD_F_NORMAL, "Lista los procesos de usuario (libera los terminados)" },
    { "usertest",     cmd_usertest,     CMD_F_NORMAL, "DEPURACION: ejecuta programas de prueba en ring 3 (syscalls, aislamiento, plazo)" },
    { "fbverify",     cmd_fbverify,     CMD_F_NORMAL, "DEPURACION: fbverify on|off — verifica el dano del renderizado" },
    { "watchdog",     cmd_watchdog,     CMD_F_SAFE,   "Estado del vigilante (aviso si ningun hilo cede la CPU en 5 s)" },
    { "crash",        cmd_crash,        CMD_F_NORMAL, "DEPURACION: provoca un panic con backtrace (detiene el sistema)" },
    { "hang",         cmd_hang,         CMD_F_NORMAL, "DEPURACION: hang [seg] retiene la CPU para probar el vigilante" },
    { "pmm",          cmd_pmm,          CMD_F_SAFE,   "Telemetria y auto-test del PMM bitmap allocator" },
    { "vmm",          cmd_vmm,          CMD_F_SAFE,   "Verificacion de tablas de paginas MMU 4 KiB" },
    { "pci",          cmd_pci,          CMD_F_SAFE,   "Escaneo completo del bus PCI y BAR0 I/O" },
    { "tls",          cmd_tls,          CMD_F_SAFE,   "HTTPS directo (TLS 1.3 propio, certificado verificado): tls <url> | tls estado" },
    { "wifi",         cmd_wifi,         CMD_F_SAFE,   "WiFi: T480s (Intel 8265): wifi | buscar | conectar <red> [clave] | desconectar | guardadas | olvidar | registro. Pi 5: wifi | prueba | sondear | nucleos | firmware | log | mux | rd | wr" },
    { "placa",        cmd_placa,        CMD_F_SAFE,   "Modelo de Raspberry Pi, memoria y revision (segun el firmware)" },
    { "cpus",         cmd_cpus,         CMD_F_SAFE,   "Nucleos del procesador: cpus | cpus prueba (velocidad en paralelo) | cpus 1 | cpus todos" },
    { "dhcp",         cmd_dhcp,         CMD_F_SAFE,   "Pide IP, mascara, puerta de enlace y DNS al router por DHCP" },
    { "netif",        cmd_netif,        CMD_F_SAFE,   "Interfaces de red: netif | netif usar <eth0|wlan0|virtio0>" },
    { "wpa",          cmd_wpa,          CMD_F_SAFE,   "WPA2: wpa (autoprueba de la criptografia) | wpa psk <ssid> <clave>" },
    { "llm-config",   cmd_llm_config,   CMD_F_NORMAL, "Modelo de lenguaje: llm-config | proveedor <local|openai|claude|gemini> | servidor | modelo | clave | modelos | probar" },
#if defined(__x86_64__)
    { "navegador-aislado", cmd_nav_aislado, CMD_F_NORMAL, "Motor del navegador en un proceso de ring 3: navegador-aislado | on | off" },
    { "webr3",        cmd_webr3,        CMD_F_NORMAL, "DEPURACION: carga una URL con el motor del navegador en ring 3 (sin interfaz)" },
#endif
    { "membench",     cmd_membench,     CMD_F_NORMAL, "DEPURACION: coste de memcpy/memset/memmove/memcmp por tamano" },
    { "agente_red",   cmd_agente_red,   CMD_F_NORMAL, "Destinos de red de los agentes: agente_red | abierto|procedencia|cerrado | permitir <dominio> | quitar <dominio>" },
    { "rng",          cmd_rng,          CMD_F_SAFE,   "Generador aleatorio del nucleo: fuentes (RDSEED/RDRAND), siembras y autoprueba" },
#ifdef __x86_64__
    { "memprot",      cmd_memprot,      CMD_F_SAFE,   "Proteccion de memoria del nucleo: que es ejecutable (solo .text) y estado de la pagina 0" },
#endif
    { "fan",          cmd_fan,          CMD_F_SAFE,   "Ventilador de la Pi 5: fan | fan on | fan off | fan inv" },
    { "hdmi",         cmd_hdmi,         CMD_F_SAFE,   "Framebuffer HDMI (Pi 5): resolucion, bits, pitch, direccion; 'hdmi swap' cambia rojo/azul" },
    { "eth",          cmd_eth,          CMD_F_SAFE,   "Ethernet del RP1 (Pi 5): velocidad y duplex negociados, tramas" },
    { "gpio",         cmd_gpio,         CMD_F_SAFE,   "GPIO del RP1 (Pi 5): gpio | gpio N | gpio N in|out|0|1|toggle|up|down|nopull" },
    { "arp",          cmd_arp,          CMD_F_SAFE,   "Tabla de cache de resolucion dinamica ARP" },

    /* Multitarea, Hilos y Control de Energía */
#ifdef __x86_64__
    { "sbx",          cmd_sbx,          CMD_F_SAFE,   "Servicios aislados en ring 3 (decodificadores de imagen): estadisticas; 'sbx test' = pruebas" },
#endif
    { "stacks",       cmd_stacks,       CMD_F_SAFE,   "Uso maximo de pila de cada hilo (marca de agua); avisa de los que rozan el limite" },
    { "ps",           cmd_ps,           CMD_F_SAFE,   "Tabla de hilos de kernel, demonios y ticks CPU" },
    { "threads",      cmd_threads,      CMD_F_SAFE,   "Supervision de KThreads y stacks virtuales" },
    { "spawn",        cmd_spawn,        CMD_F_SAFE,   "Lanza un comando como hilo en segundo plano" },
    { "bg",           cmd_spawn,        CMD_F_ALIAS,  "Alias de spawn" },
    { "kill",         cmd_kill,         CMD_F_NORMAL, "Termina un hilo en segundo plano (TID > 3)" },
    { "sleep",        cmd_sleep,        CMD_F_SAFE,   "Suspende la CPU con HLT durante N milisegundos" },
    { "time",         cmd_time,         CMD_F_SAFE,   "Mide milisegundos y ciclos TSC de un comando" },
    { "reboot",       cmd_reboot,       CMD_F_NORMAL, "Reinicia fisicamente el equipo (8042 reset)" },
    { "poweroff",     cmd_poweroff,     CMD_F_NORMAL, "Apaga el sistema limpiamente via ACPI" },
    { "shutdown",     cmd_poweroff,     CMD_F_ALIAS,  "Alias de poweroff" },
    { "halt",         cmd_halt,         CMD_F_NORMAL, "Sincroniza discos y detiene la CPU (HLT)" },

    /* Sistema de Archivos y Coreutils */
    { "ls",           cmd_ls,           CMD_F_SAFE,   "Lista archivos persistentes del RamFS" },
    { "cat",          cmd_cat,          CMD_F_SAFE,   "Muestra el contenido de un archivo" },
    { "write",        cmd_write,        CMD_F_SAFE,   "Crea o sobrescribe un archivo en RamFS" },
    { "rm",           cmd_rm,           CMD_F_SAFE,   "Elimina un archivo del RamFS" },
    { "cp",           cmd_cp,           CMD_F_SAFE,   "Copia un archivo a una nueva ruta" },
    { "mv",           cmd_mv,           CMD_F_SAFE,   "Mueve o renombra un archivo en RamFS" },
    { "touch",        cmd_touch,        CMD_F_SAFE,   "Crea un archivo vacio si no existe" },
    { "tree",         cmd_tree,         CMD_F_SAFE,   "Muestra la jerarquia de archivos en arbol" },
    { "head",         cmd_head,         CMD_F_SAFE,   "Muestra las primeras N lineas de un archivo" },
    { "tail",         cmd_tail,         CMD_F_SAFE,   "Muestra las ultimas N lineas de un archivo" },
    { "wc",           cmd_wc,           CMD_F_SAFE,   "Cuenta lineas, palabras y bytes de un archivo" },
    { "grep",         cmd_grep,         CMD_F_SAFE,   "Busca patrones de texto dentro de un archivo" },
    { "echo",         cmd_echo,         CMD_F_SAFE,   "Imprime texto en consola o con redireccion" },
    { "nano",         cmd_nano,         CMD_F_NORMAL, "Editor de texto visual interactivo en 80x25" },
    { "edit",         cmd_nano,         CMD_F_ALIAS,  "Alias de nano" },
    { "hexdump",      cmd_hexdump,      CMD_F_SAFE,   "Volcado canonico hexadecimal y ASCII de archivo" },
    { "xxd",          cmd_hexdump,      CMD_F_ALIAS,  "Alias de hexdump" },
    { "find",         cmd_find,         CMD_F_SAFE,   "Busca archivos por nombre: find <texto> [ruta] [-a]" },
    { "mkdir",        cmd_mkdir,        CMD_F_SAFE,   "Crea un directorio (agentes: solo en /workspace y /mis_archivos)" },
    { "rmdir",        cmd_rmdir,        CMD_F_SAFE,   "Elimina un directorio vacio" },
    { "stat",         cmd_stat,         CMD_F_SAFE,   "Metadatos de un archivo: tamano, tipo, lineas y persistencia" },
    { "du",           cmd_du,           CMD_F_SAFE,   "Uso de disco por archivo y total (du [ruta] [-a])" },
    { "sort",         cmd_sort,         CMD_F_SAFE,   "Ordena lineas (-r inverso, -n numerico); admite tuberias" },
    { "uniq",         cmd_uniq,         CMD_F_SAFE,   "Quita lineas repetidas consecutivas (-c cuenta); admite tuberias" },

    /* Almacenamiento Persistente a Bajo Nivel */
    { "checkpoint",   cmd_checkpoint,   CMD_F_SAFE,   "Guarda snapshot persistente en virtio-blk (LBA 4096)" },
    { "fs-backup",    cmd_checkpoint,   CMD_F_ALIAS,  "Alias de checkpoint" },
    { "rollback",     cmd_rollback,     CMD_F_NORMAL, "Restaura RamFS desde el snapshot fisico" },
    { "fs-restore",   cmd_rollback,     CMD_F_ALIAS,  "Alias de rollback" },
    { "fs-sync",      cmd_fs_sync,      CMD_F_SAFE,   "Fuerza sincronizacion de inodos sucios a disco" },
    { "fs-format",    cmd_fs_format,    CMD_F_NORMAL, "Formatea RamFS restaurando imagen de fabrica" },
    { "sector_read",  cmd_sector_read,  CMD_F_SAFE,   "Lectura fisica directa de sector LBA de 512 B" },
    { "sector_r",     cmd_sector_read,  CMD_F_ALIAS,  "Alias de sector_read" },
    { "sector_write", cmd_sector_write, CMD_F_NORMAL, "Escritura fisica directa en sector LBA de usuario" },
    { "sector_w",     cmd_sector_write, CMD_F_ALIAS,  "Alias de sector_write" },

    /* Red TCP/IP y Web */
    { "httpd",        cmd_httpd,        CMD_F_SAFE,   "Servidor web HTTP (puerto 80 / localhost:8090): httpd | httpd on | httpd off" },
    { "ping",         cmd_ping,         CMD_F_SAFE,   "Envia peticiones ICMP Echo a una IP destino" },
    { "dns",          cmd_dns,          CMD_F_SAFE,   "Resolucion dinamica de nombres de dominio DNS" },
    { "curl",         cmd_curl,         CMD_F_SAFE,   "Cliente HTTP/1.1 con resolucion y cabeceras" },
    { "search",       cmd_search,       CMD_F_SAFE,   "Busqueda web DuckDuckGo Instant Answer" },
    { "ddg",          cmd_search,       CMD_F_ALIAS,  "Alias de search" },

    /* Inteligencia Artificial y Sistema Multi-Agente */
    { "soma",         cmd_soma,         CMD_F_NORMAL, "Invoca al Agente Central SOMA con memoria episódica" },
    { "buscador",     cmd_buscador,     CMD_F_NORMAL, "Invoca al Agente Buscador (Web & System Research)" },
    { "agent",        cmd_agent,        CMD_F_NORMAL, "Despacha misiones a agentes (soma, buscador, etc.)" },
    { "mayordomo",    cmd_mayordomo,     CMD_F_NORMAL, "Agente directo para operaciones del sistema" },
    { "delegate",     delegate_cmd,     CMD_F_SAFE,   "Delega una sub-mision especializada a otro agente" },
    { "myos",         cmd_soma,         CMD_F_ALIAS,  "Alias de soma" },

    /* Memoria Estructurada en Árbol */
    { "mem_tree",     mem_tree_cmd,     CMD_F_SAFE,   "Muestra la jerarquia de temas del arbol de memoria" },
    { "mem_read",     mem_read_cmd,     CMD_F_SAFE,   "Lee un nodo tematico del arbol de memoria" },
    { "mem_write",    mem_write_cmd,    CMD_F_SAFE,   "Escribe o actualiza un nodo en el arbol de memoria" },
    { "mem_search",   mem_search_cmd,   CMD_F_SAFE,   "Busca conceptos o palabras clave en el arbol de memoria" },
    { "bm25",         mem_search_cmd,   CMD_F_ALIAS,  "Alias de mem_search (motor BM25)" },
    { "health",       cmd_health,       CMD_F_SAFE,   "Verifica estado del servidor LLM local (/health)" },
    { "llm",          cmd_llm,          CMD_F_NORMAL, "Consulta libre en lenguaje natural a nail-35b" },
    { "llm-diag",     cmd_llm_diag,     CMD_F_SAFE,   "Diagnostico holistico del kernel mediante IA" },

    /* Inspección de Código Fuente del Kernel (SRCFS) */
    { "src_ls",       cmd_src_ls,       CMD_F_SAFE,   "Lista archivos fuente del kernel empaquetados" },
    { "src_cat",      cmd_src_cat,      CMD_F_SAFE,   "Muestra un fragmento de codigo fuente desde offset" },
    { "src_grep",     cmd_src_grep,     CMD_F_SAFE,   "Busca cadenas en los fuentes con linea y offset" },

    /* Entorno Grafico GUI */
        { "view",         cmd_view,         CMD_F_SAFE,   "Inspecciona y visualiza archivos de imagen (BMP/PPM)" },
{ "cursor",       cmd_cursor,       CMD_F_SAFE,   "Muestra o cambia el puntero activo del raton (arrow|crosshair|hand)" },
    { "font",         cmd_font,         CMD_F_SAFE,   "Muestra o cambia la fuente tipografica activa (classic|modern)" },
    { "terminal",     cmd_terminal,     CMD_F_SAFE,   "Abre la ventana del terminal de la GUI (barra de desplazamiento, seleccion, copiar/pegar, zoom)" },
    { "media",        cmd_media,        CMD_F_SAFE,   "Reproductor multimedia (video AVI y audio WAV del almacen BIGFS): media gui|list|play|pause|stop|seek|status" },
    { "monitor",      cmd_monitor,      CMD_F_SAFE,   "Monitor del sistema: CPU, memoria, procesos, graficos, discos, red y hardware" },
    { "imagen",       cmd_imagen,       CMD_F_SAFE,   "Visor de imagenes: imagen [archivo] (PNG, JPEG, GIF, BMP de cualquier tamano)" },
    { "pdf",          cmd_pdf,          CMD_F_SAFE,   "Lector de PDF: pdf [archivo.pdf]" },
    { "suma",         cmd_suma,         CMD_F_SAFE,   "Tamano y CRC-32 de un archivo: suma <archivo>" },
    { "descargar",    cmd_descargar,    CMD_F_SAFE,   "Descarga una URL a un archivo: descargar <url> [archivo] (sin limite de 16 KiB)" },
#if defined(__x86_64__)
    { "pantalla",     cmd_pantalla,     CMD_F_SAFE,   "Pantalla y arranque: Multiboot, UEFI/BIOS, framebuffer del firmware, ACPI (portatil T480s)" },
    { "teclado",      cmd_teclado,      CMD_F_SAFE,   "Teclado y trackpad PS/2 (portatil T480s): estado y distribucion (teclado es|us)" },
    { "ramtest",      cmd_ramtest,      CMD_F_SAFE,   "Comprueba la memoria alta: ramtest [MiB] (reserva, escribe, verifica y libera; por defecto 1024)" },
    { "irq",          cmd_irq,          CMD_F_SAFE,   "Interrupciones: modo PIC/APIC, reloj, rutas del IOAPIC y contadores" },
    { "bateria",      cmd_bateria,      CMD_F_SAFE,   "Bateria (carga, autonomia, salud), cargador y tapa por ACPI" },
    { "escritorio",   cmd_escritorio,   CMD_F_SAFE,   "Escritorio: escritorio reiniciar devuelve iconos y gadget a su sitio | escritorio prueba [n] abre y cierra n veces cada aplicacion y mira la memoria" },
    { "orbe",         cmd_orbe,         CMD_F_SAFE,   "Orbe del escritorio: coste de cada parte del fotograma y fps | orbe prueba (identico con 1 y con todos los nucleos)" },
    { "energia",      cmd_energia,      CMD_F_SAFE,   "Procesador: perfil (rendimiento/equilibrado/ahorro), temperatura, frecuencia real y consumo" },
    { "brillo",       cmd_brillo,       CMD_F_SAFE,   "Brillo de la pantalla y teclas Fn del ThinkPad: brillo | brillo 0-100 | brillo + | brillo -" },
    { "trackpad",     cmd_trackpad,     CMD_F_SAFE,   "Trackpad del portatil: gestos (dos dedos), toque, desplazamiento natural, velocidad" },
    { "red",          cmd_red,          CMD_F_SAFE,   "Conexion de red: cable Ethernet, movil por USB, direccion IP y estado de las tarjetas" },
    { "acpi",         cmd_acpi,         CMD_F_SAFE,   "Firmware ACPI (uACPI): estado, EC, eventos y dispositivos (bateria, tapa, botones): acpi | acpi todos" },
    { "informe",      cmd_informe,      CMD_F_SAFE,   "Informe de arranque y hardware para compartir: lo guarda en INFORME.TXT del pendrive y /informe.txt (informe ver lo muestra)" },
    { "disco",        cmd_disco,        CMD_F_SAFE,   "Donde se guardan los archivos: particion SOMADATA del pendrive, virtio o solo RAM (disco sync)" },
#endif
    { "dmesg",        cmd_dmesg,        CMD_F_SAFE,   "Registro del nucleo desde el arranque: dmesg [N lineas | texto a buscar]" },
    { "navegador",    cmd_navegador,    CMD_F_SAFE,   "Navegador web: navegador [url] (boton derecho sobre una imagen: Guardar imagen como...)" },
    { "ajustes",      cmd_ajustes,      CMD_F_SAFE,   "Ajustes de la interfaz: transparencia de las ventanas y fuente" },
    { "latex",        cmd_latex,        CMD_F_SAFE,   "Maquetador LaTeX: latex [archivo.tex] (formulas, secciones, tablas; vista Documento/Fuente)" },
    { "paint",        cmd_paint,        CMD_F_SAFE,   "Editor de dibujo: lienzo hasta 640x480, zoom, deshacer y guardado PNG" },
    { "memview",      cmd_memview,      CMD_F_SAFE,   "Visor visual interactivo del arbol de memoria de agentes (GUI)" },
    { "agentmem",     cmd_memview,      CMD_F_ALIAS,  "Alias de memview" },
    { "vsoma",        cmd_vsoma,        CMD_F_SAFE,   "Inicia la interfaz grafica HD (1280x720, 32 bpp, 16.7M colores)" },
    { "gui",          cmd_vsoma,        CMD_F_ALIAS,  "Alias de vsoma" },

    /* Auto-Modificación y Pruebas Críticas */
    { "hpatch",       cmd_hpatch,       CMD_F_NORMAL, "Prueba del buzon de parches y Singularity Loop" },
    { "panic",        cmd_panic,        CMD_F_NORMAL, "Dispara excepciones de CPU para verificar IDT/TSS: panic | div | ud | nulo | nx (los dos ultimos: protecciones del parche 192)" },
    { "agent_sim",    cmd_agent_sim,    CMD_F_NORMAL, "Ejecuta un comando bajo la politica del Agente (pruebas de seguridad)" },
};

static const int cmd_table_count = sizeof(cmd_table) / sizeof(cmd_table[0]);

/* ====================================================================
 * PARCHE_TERMINAL_V2: ayuda por categorias y catalogo vivo de comandos
 * Las categorias solo agrupan por nombre; todo comando nuevo de cmd_table que
 * no figure aqui aparece automaticamente en "Otros (sin categoria)".
 * ==================================================================== */
#define HELP_BUF 12288

struct help_category {
    const char *title;
    const char *names;
};

static const struct help_category help_categories[] = {
                { "SOMA-LANG y Coder",    "vm coder run app compile check lint" },
        { "Ayuda y sistema",      "help app sysinfo somafetch creador status uptime date whoami hostname uname clear history env export unset" }, /*PL5_HELP_CAT*/
    { "Memoria y diagnostico","ram df mem heap pmm vmm pci hw apic ahci nvme blk fat usb arp stats test_suite health llm-diag" },
    { "Procesos",             "ps threads spawn kill sleep time" },
    { "Archivos y texto",     "ls tree cat head tail wc grep find sort uniq echo write touch mkdir rmdir cp mv rm stat du hexdump nano source" },
    { "Almacenamiento",       "checkpoint rollback fs-sync fs-format sector_read sector_write" },
    { "Red y web",            "httpd ping dns curl search" },
    { "IA y multiagente",     "soma buscador mayordomo agent delegate llm mem_tree mem_read mem_write mem_search memview" },
    { "Codigo fuente",        "src_ls src_cat src_grep" },
    { "Interfaz grafica",     "vsoma view cursor font paint" },
    { "Energia y pruebas",    "reboot poweroff halt hpatch panic agent_sim" },
};
#define HELP_NCAT ((int)(sizeof(help_categories) / sizeof(help_categories[0])))

struct help_usage {
    const char *name;
    const char *usage;
};

static const struct help_usage help_usages[] = {
    { "help",       "help [categoria | comando | texto | all | agentes]" },
    { "ram",        "ram [-s | -v | base | reset]   (-s resumen, -v desglose, base/reset: medir una app)" },
    { "uname",      "uname [-a]" },
    { "find",       "find <texto> [ruta] [-a]   (-a incluye ocultos y de sistema)" },
    { "du",         "du [ruta] [-a]" },
    { "stat",       "stat <archivo>" },
    { "mkdir",      "mkdir <ruta>   (relativa a /workspace si no empieza por /)" },
    { "rmdir",      "rmdir <ruta>   (solo directorios vacios)" },
    { "sort",       "sort [-r] [-n] [archivo]   o   <comando> | sort" },
    { "uniq",       "uniq [-c] [archivo]   o   <comando> | uniq" },
    { "ls",         "ls [-a] [ruta]" },
    { "grep",       "grep [-i] [-l] <patron> [archivo]   (sin archivo busca en todo el RamFS)" },
    { "head",       "head [-n N] <archivo>" },
    { "tail",       "tail [-n N] <archivo>" },
    { "cp",         "cp <origen> <destino>" },
    { "mv",         "mv <origen> <destino>" },
    { "write",      "write <ruta> \"<texto>\"   (\\n = salto de linea)" },
    { "curl",       "curl <url> | curl <host> [puerto] [ruta]" },
    { "search",     "search <consulta>" },
    { "spawn",      "spawn <comando>   (segundo plano; log en /tmp/job_<TID>.log)" },
    { "kill",       "kill <TID>" },
    { "delegate",   "delegate <agente> \"<mision>\"" },
    { "mem_search", "mem_search <palabras clave>" },
    { "mayordomo",  "mayordomo <tarea>   p. ej. 'muestra la RAM', 'que procesos hay', 'ayuda ram'" },
    { "font",       "font [nombre | id]" },
    { "cursor",     "cursor [nombre | id]" },
};

static int names_has(const char *list, const char *name)
{
    uint32_t nl = shell_strlen(name);
    if (!nl) return 0;
    while (*list) {
        const char *s;
        uint32_t l = 0;
        while (*list == ' ') list++;
        s = list;
        while (list[l] && list[l] != ' ') l++;
        if (l == nl) {
            uint32_t i = 0;
            while (i < l && s[i] == name[i]) i++;
            if (i == l) return 1;
        }
        list += l;
    }
    return 0;
}

static const struct command_entry *help_find(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 0; i < cmd_table_count; ++i)
        if (str_eq(cmd_table[i].name, name)) return &cmd_table[i];
    return 0;
}

static const struct command_entry *help_primary(const struct command_entry *e)
{
    if (!(e->flags & CMD_F_ALIAS)) return e;
    for (int i = 0; i < cmd_table_count; ++i)
        if (!(cmd_table[i].flags & CMD_F_ALIAS) && cmd_table[i].handler == e->handler)
            return &cmd_table[i];
    return e;
}

static int help_category_of(const char *name)
{
    for (int c = 0; c < HELP_NCAT; ++c)
        if (names_has(help_categories[c].names, name)) return c;
    return -1;
}

static int help_in_cat(const struct command_entry *e, int cat)
{
    if (cat >= 0) return names_has(help_categories[cat].names, e->name);
    return help_category_of(e->name) < 0;
}

static const char *help_usage_for(const char *name)
{
    for (uint32_t i = 0; i < sizeof(help_usages) / sizeof(help_usages[0]); ++i)
        if (str_eq(help_usages[i].name, name)) return help_usages[i].usage;
    return 0;
}

/* Coincidencia por prefijo de cualquier palabra del titulo ("red" -> "Red y web"). */
static int help_cat_match(const char *title, const char *q)
{
    uint32_t ql = shell_strlen(q);
    const char *w = title;
    if (ql < 2) return 0;
    while (*w) {
        uint32_t i = 0;
        while (i < ql && w[i] && to_lower_char(w[i]) == to_lower_char(q[i])) i++;
        if (i == ql) return 1;
        while (*w && *w != ' ') w++;
        while (*w == ' ') w++;
    }
    return 0;
}

static void help_put_wrapped(char *o, uint32_t max, uint32_t *p, const char *word, uint32_t *col)
{
    uint32_t wl = shell_strlen(word);
    if (*col > 4 && *col + 1 + wl > 76) {
        fb_puts(o, max, p, "\n    ");
        *col = 4;
    } else if (*col > 4) {
        fb_putc(o, max, p, ' ');
        (*col)++;
    }
    fb_puts(o, max, p, word);
    *col += wl;
}

/* only_cat: -2 = todas, -1 = sin categoria, >=0 = esa categoria. */
static void help_render(char *o, uint32_t max, uint32_t *p, int with_help,
                        int only_safe, int tag, int only_cat)
{
    for (int ci = 0; ci <= HELP_NCAT; ++ci) {
        int cat = (ci == HELP_NCAT) ? -1 : ci;
        int k = 0;
        uint32_t col = 0;
        if (only_cat != -2 && cat != only_cat) continue;
        for (int i = 0; i < cmd_table_count; ++i) {
            const struct command_entry *e = &cmd_table[i];
            if (e->flags & CMD_F_ALIAS) continue;
            if (only_safe && !(e->flags & CMD_F_SAFE)) continue;
            if (help_in_cat(e, cat)) k++;
        }
        if (!k) continue;
        fb_puts(o, max, p, "== ");
        fb_puts(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
        fb_puts(o, max, p, " (");
        fb_put_dec(o, max, p, (uint64_t)k);
        fb_puts(o, max, p, ") ==\n");
        for (int i = 0; i < cmd_table_count; ++i) {
            const struct command_entry *e = &cmd_table[i];
            if (e->flags & CMD_F_ALIAS) continue;
            if (only_safe && !(e->flags & CMD_F_SAFE)) continue;
            if (!help_in_cat(e, cat)) continue;
            if (with_help) {
                uint32_t nl = shell_strlen(e->name);
                fb_puts(o, max, p, "  ");
                fb_puts(o, max, p, e->name);
                for (uint32_t s = nl; s < 14; ++s) fb_putc(o, max, p, ' ');
                fb_putc(o, max, p, ' ');
                if (tag) fb_puts(o, max, p, (e->flags & CMD_F_SAFE) ? "[A] " : "    ");
                fb_puts(o, max, p, "- ");
                fb_puts(o, max, p, e->help);
                fb_putc(o, max, p, '\n');
            } else {
                if (col == 0) { fb_puts(o, max, p, "    "); col = 4; }
                help_put_wrapped(o, max, p, e->name, &col);
            }
        }
        if (!with_help && col) fb_putc(o, max, p, '\n');
    }
}

static void help_detail(char *o, uint32_t max, uint32_t *p, const struct command_entry *e)
{
    const struct command_entry *pr = help_primary(e);
    const char *usage = help_usage_for(pr->name);
    int cat = help_category_of(pr->name);
    int first = 1;

    fb_puts(o, max, p, pr->name);
    fb_puts(o, max, p, " - ");
    fb_puts(o, max, p, pr->help);
    fb_putc(o, max, p, '\n');
    if (e != pr) {
        fb_puts(o, max, p, "  ('");
        fb_puts(o, max, p, e->name);
        fb_puts(o, max, p, "' es un alias de '");
        fb_puts(o, max, p, pr->name);
        fb_puts(o, max, p, "')\n");
    }
    fb_puts(o, max, p, "  Categoria : ");
    fb_puts(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
    fb_putc(o, max, p, '\n');
    for (int i = 0; i < cmd_table_count; ++i) {
        if ((cmd_table[i].flags & CMD_F_ALIAS) && cmd_table[i].handler == pr->handler) {
            fb_puts(o, max, p, first ? "  Alias     : " : ", ");
            fb_puts(o, max, p, cmd_table[i].name);
            first = 0;
        }
    }
    if (!first) fb_putc(o, max, p, '\n');
    if (usage) {
        fb_puts(o, max, p, "  Uso       : ");
        fb_puts(o, max, p, usage);
        fb_putc(o, max, p, '\n');
    }
    fb_puts(o, max, p, "  Agentes   : ");
    fb_puts(o, max, p, (pr->flags & CMD_F_SAFE)
            ? "permitido (SOMA y Mayordomo pueden ejecutarlo)\n"
            : "solo usuario (bloqueado para agentes)\n");
}

/* ---- API publica para los agentes (Mayordomo / SOMA) ---- */
int shell_command_info(const char *name, uint32_t *flags_out, const char **help_out)
{
    const struct command_entry *e = help_find(name);
    if (!e) return 0;
    if (flags_out) *flags_out = e->flags;
    if (help_out) *help_out = e->help;
    return 1;
}

int shell_format_names(char *out, uint32_t max, int only_agent_safe)
{
    uint32_t p = 0;
    if (!out || !max) return 0;
    out[0] = '\0';
    for (int i = 0; i < cmd_table_count; ++i) {
        const struct command_entry *e = &cmd_table[i];
        if (e->flags & CMD_F_ALIAS) continue;
        if (only_agent_safe && !(e->flags & CMD_F_SAFE)) continue;
        if (p) fb_putc(out, max, &p, ' ');
        fb_puts(out, max, &p, e->name);
    }
    return (int)p;
}

int shell_format_catalog(char *out, uint32_t max, int only_agent_safe, int with_help, int tag_agent)
{
    uint32_t p = 0;
    if (!out || !max) return 0;
    out[0] = '\0';
    help_render(out, max, &p, with_help, only_agent_safe, tag_agent, -2);
    return (int)p;
}

static int cmd_help(const char *args, char *out_buf, uint32_t max_out)
{
    char q[48];
    uint32_t p = 0;
    int total = 0, aliases = 0;
    char *o = (char *)kmalloc(HELP_BUF);
    if (!o) { kprint("help: sin memoria.\n"); return 1; }
    o[0] = '\0';
    shell_next_arg(args ? args : "", q, sizeof(q));
    for (int i = 0; i < cmd_table_count; ++i) {
        if (cmd_table[i].flags & CMD_F_ALIAS) aliases++;
        else total++;
    }

    if (!q[0] || str_eq(q, "all") || str_eq(q, "-a") || str_eq(q, "todo") ||
        str_eq(q, "agentes")) {
        int all = q[0] != 0;
        int only_safe = str_eq(q, "agentes");
        fb_puts(o, HELP_BUF, &p, "Comandos disponibles en SOMA 0.3 pre-beta (");
        fb_put_dec(o, HELP_BUF, &p, (uint64_t)total);
        fb_puts(o, HELP_BUF, &p, " comandos, ");
        fb_put_dec(o, HELP_BUF, &p, (uint64_t)aliases);
        fb_puts(o, HELP_BUF, &p, " alias)\n");
        if (!all) {
            fb_puts(o, HELP_BUF, &p, "Uso: help <categoria> | help <comando> | help <texto> | help all | help agentes\n");
        } else if (only_safe) {
            fb_puts(o, HELP_BUF, &p, "Comandos que SOMA y Mayordomo pueden ejecutar:\n");
        } else {
            fb_puts(o, HELP_BUF, &p, "[A] = utilizable por los agentes (SOMA / Mayordomo)\n");
        }
        help_render(o, HELP_BUF, &p, all, only_safe, all && !only_safe, -2);
    } else {
        const struct command_entry *e = help_find(q);
        int cat = -3;
        if (e) {
            help_detail(o, HELP_BUF, &p, e);
        } else {
            for (int c = 0; c < HELP_NCAT; ++c)
                if (help_cat_match(help_categories[c].title, q)) { cat = c; break; }
            if (cat >= 0) {
                help_render(o, HELP_BUF, &p, 1, 0, 1, cat);
            } else {
                uint32_t hits = 0;
                fb_puts(o, HELP_BUF, &p, "Resultados para '");
                fb_puts(o, HELP_BUF, &p, q);
                fb_puts(o, HELP_BUF, &p, "':\n");
                for (int i = 0; i < cmd_table_count; ++i) {
                    const struct command_entry *c = &cmd_table[i];
                    uint32_t nl;
                    if (c->flags & CMD_F_ALIAS) continue;
                    if (!find_substr_ci(c->name, q, 1) && !find_substr_ci(c->help, q, 1)) continue;
                    nl = shell_strlen(c->name);
                    fb_puts(o, HELP_BUF, &p, "  ");
                    fb_puts(o, HELP_BUF, &p, c->name);
                    for (uint32_t s = nl; s < 14; ++s) fb_putc(o, HELP_BUF, &p, ' ');
                    fb_puts(o, HELP_BUF, &p, " - ");
                    fb_puts(o, HELP_BUF, &p, c->help);
                    fb_putc(o, HELP_BUF, &p, '\n');
                    hits++;
                }
                if (!hits) fb_puts(o, HELP_BUF, &p, "  sin resultados. Escribe 'help' para ver las categorias.\n");
            }
        }
    }
    shell_emit(o, out_buf, max_out);
    kfree(o);
    return 1;
}


int shell_autocomplete(const char *prefix, uint32_t plen, const char *matches[], int max_matches)
{
    int count = 0;
    for (int i = 0; i < cmd_table_count; ++i) {
        const char *name = cmd_table[i].name;
        int match = 1;
        for (uint32_t j = 0; j < plen; ++j) {
            if (name[j] != prefix[j]) { match = 0; break; }
        }
        if (match && count < max_matches) {
            matches[count++] = name;
        }
    }
    return count;
}

/* PARCHE 046: limite de anidamiento (evita desbordar la pila de 32 KiB de los
 * hilos con 'a;b;c;...' o 'a&&b&&...'). El limite cuenta niveles de recursion. */
#define DISPATCH_MAX_DEPTH 8
static int dispatch_depth_fallback = 0;

static int dispatch_command_inner(const char *cmd_line, char *out_buf, uint32_t max_out);

static struct kmutex shell_mx; static int shell_mx_init; static volatile uint32_t shell_mx_owner;
int dispatch_command(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    struct tcb *self = thread_current();
    int *depth = self ? &self->dispatch_depth : &dispatch_depth_fallback;

    if (*depth >= DISPATCH_MAX_DEPTH) {
        console_muted = 0;
        kprint("\n[SEGURIDAD] Anidamiento de comandos excesivo (max 8): abortado.\n");
        if (out_buf && max_out) {
            uint32_t ep = 0;
            fb_puts(out_buf, max_out, &ep, "Error: anidamiento de comandos excesivo");
        }
        return 0;
    }

    /* PARCHE 091: el agente ejecuta ordenes en su propio hilo mientras la GUI sigue viva; el despachador usa bufferes
     * estaticos, asi que solo un hilo a la vez. El hilo principal (GUI) no espera: si esta ocupado, avisa y sigue. */
    /* PARCHE 100: el cerrojo se toma si ESTE hilo no lo tiene ya (antes: solo en profundidad 0). El escritorio lo suelta al
     * arrancar (shell_lock_drop), sea cual sea la orden que lo abrio (vsoma, studio, navegador...): antes, abierto con otra orden,
     * se lo quedaba toda la sesion y las apps en segundo plano no arrancaban nunca. */
    if (!shell_mx_init) { kmutex_init(&shell_mx, "shell"); shell_mx_init = 1; }
    uint32_t me = self ? self->tid + 1u : 1u;
    int locked = 0;
    if (shell_mx_owner != me) {
        if (self && self->tid == 0) {
            if (!kmutex_trylock(&shell_mx)) {
                if (out_buf && max_out) { uint32_t ep = 0; fb_puts(out_buf, max_out, &ep, "El agente SOMA esta usando la consola en este momento; vuelve a intentarlo en unos segundos."); }
                return 1;
            }
        } else kmutex_lock(&shell_mx);
        shell_mx_owner = me; locked = 1;
    }
    (*depth)++;
    int r = dispatch_command_inner(cmd_line, out_buf, max_out);
    (*depth)--;
    if (locked && shell_mx_owner == me) { shell_mx_owner = 0; kmutex_unlock(&shell_mx); }
    return r;
}
void shell_lock_drop(void)
{
    struct tcb *self = thread_current();
    uint32_t me = self ? self->tid + 1u : 1u;
    if (shell_mx_init && shell_mx_owner == me) { shell_mx_owner = 0; kmutex_unlock(&shell_mx); }
}

static int dispatch_command_inner(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    static int redir_active = 0;
    int ret_val = 0;

    if (!cmd_line) return 0;
    while (*cmd_line == ' ') cmd_line++;
    if (*cmd_line == '\0') return 0;

    char *expanded_cmd = (char *)kmalloc(16384);
    char *redir_buf    = (char *)kmalloc(16384);
    char *pipe_buf     = (char *)kmalloc(16384);

    if (!expanded_cmd || !redir_buf || !pipe_buf) {
        if (expanded_cmd) kfree(expanded_cmd);
        if (redir_buf) kfree(redir_buf);
        if (pipe_buf) kfree(pipe_buf);
        kprint("shell: sin memoria en KHeap para despachador de comandos.\n");
        return 0;
    }

    /* 0. Interceptores Lógicos Condicionales ('&&' y '||') */
    int logic_and_idx = -1;
    int logic_or_idx = -1;
    char l_quote = 0;
    for (int i = 0; cmd_line[i] && cmd_line[i + 1]; ++i) {
        char c = cmd_line[i];
        if (i > 0 && cmd_line[i - 1] == '\\') continue;
        if (c == '"' || c == '\'') {
            if (l_quote == 0) l_quote = c;
            else if (l_quote == c) l_quote = 0;
        } else if (!l_quote) {
            if (c == '&' && cmd_line[i + 1] == '&') {
                logic_and_idx = i;
                break;
            }
            if (c == '|' && cmd_line[i + 1] == '|') {
                logic_or_idx = i;
                break;
            }
        }
    }

    if (logic_and_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        if (left_cmd) {
            int lp = 0;
            for (int i = 0; i < logic_and_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *right_cmd = cmd_line + logic_and_idx + 2;
            while (*right_cmd == ' ') right_cmd++;

            int ok = dispatch_command(left_cmd, out_buf, max_out);
            kfree(left_cmd);
            if (ok && *right_cmd != '\0') {
                ret_val = dispatch_command(right_cmd, out_buf, max_out);
            } else {
                ret_val = ok;
            }
        }
        goto cleanup;
    }

    if (logic_or_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        if (left_cmd) {
            int lp = 0;
            for (int i = 0; i < logic_or_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *right_cmd = cmd_line + logic_or_idx + 2;
            while (*right_cmd == ' ') right_cmd++;

            int ok = dispatch_command(left_cmd, out_buf, max_out);
            kfree(left_cmd);
            if (!ok && *right_cmd != '\0') {
                ret_val = dispatch_command(right_cmd, out_buf, max_out);
            } else {
                ret_val = ok;
            }
        }
        goto cleanup;
    }

    /* 1. Interceptor de Comandos Encadenados (';') */
    int semi_idx = -1;
    char s_quote = 0;
    for (int i = 0; cmd_line[i]; ++i) {
        char c = cmd_line[i];
        if (i > 0 && cmd_line[i - 1] == '\\') continue;
        if (c == '"' || c == '\'') {
            if (s_quote == 0) s_quote = c;
            else if (s_quote == c) s_quote = 0;
        } else if (!s_quote && c == ';') {
            semi_idx = i;
            break;
        }
    }
    if (semi_idx >= 0) {
        char *first_cmd = (char *)kmalloc(4096);
        if (first_cmd) {
            int f_p = 0;
            for (int i = 0; i < semi_idx && f_p < 4095; ++i) first_cmd[f_p++] = cmd_line[i];
            while (f_p > 0 && first_cmd[f_p - 1] == ' ') f_p--;
            first_cmd[f_p] = '\0';

            const char *next_cmd = cmd_line + semi_idx + 1;
            while (*next_cmd == ' ') next_cmd++;

            if (f_p > 0) {
                dispatch_command(first_cmd, out_buf, max_out);
            }
            kfree(first_cmd);
            if (*next_cmd != '\0') {
                ret_val = dispatch_command(next_cmd, out_buf, max_out);
            } else {
                ret_val = 1;
            }
        }
        goto cleanup;
    }

    /* 2. Expansión dinámica de variables de entorno ($VAR) en buffer de 16 KiB */
    env_expand(cmd_line, expanded_cmd, 16384);
    cmd_line = expanded_cmd;
    while (*cmd_line == ' ') cmd_line++;

    /* 3. Interceptor de Tuberías Bare-Metal ('|') */
    int pipe_idx = -1;
    char p_quote = 0;
    for (int i = 0; cmd_line[i]; ++i) {
        char c = cmd_line[i];
        if (i > 0 && cmd_line[i - 1] == '\\') continue;
        if (c == '"' || c == '\'') {
            if (p_quote == 0) p_quote = c;
            else if (p_quote == c) p_quote = 0;
        } else if (!p_quote && c == '|') {
            pipe_idx = i;
            break;
        }
    }
    if (pipe_idx >= 0) {
        char *left_cmd = (char *)kmalloc(4096);
        char *right_cmd = (char *)kmalloc(4096);
        if (left_cmd && right_cmd) {
            int lp = 0, rp = 0;
            for (int i = 0; i < pipe_idx && lp < 4095; ++i) left_cmd[lp++] = cmd_line[i];
            while (lp > 0 && left_cmd[lp - 1] == ' ') lp--;
            left_cmd[lp] = '\0';

            const char *r_ptr = cmd_line + pipe_idx + 1;
            while (*r_ptr == ' ') r_ptr++;
            while (*r_ptr && rp < 4095) right_cmd[rp++] = *r_ptr++;
            right_cmd[rp] = '\0';

            if (lp > 0 && rp > 0) {
                pipe_buf[0] = '\0';
                console_muted = 1;
                int r_left = dispatch_command(left_cmd, pipe_buf, 16384);
                console_muted = 0;

                if (r_left) {
                    pipe_stdin_data = pipe_buf;
                    pipe_stdin_len = 0;
                    while (pipe_buf[pipe_stdin_len]) pipe_stdin_len++;

                    ret_val = dispatch_command(right_cmd, out_buf, max_out);
                    pipe_stdin_data = 0;
                    pipe_stdin_len = 0;
                }
            }
        }
        if (left_cmd) kfree(left_cmd);
        if (right_cmd) kfree(right_cmd);
        goto cleanup;
    }

    /* 4. Interceptor de Redirección Universal ('>') */
    if (!redir_active) {
        int gt_idx = -1;
        char active_quote = 0;
        for (int i = 0; cmd_line[i]; ++i) {
            char c = cmd_line[i];
            if (i > 0 && cmd_line[i - 1] == '\\') continue;
            if (c == '"' || c == '\'') {
                if (active_quote == 0) active_quote = c;
                else if (active_quote == c) active_quote = 0;
            } else if (!active_quote && c == '>') {
                int prev_space = (i > 0 && cmd_line[i - 1] == ' ');
                int next_space = (cmd_line[i + 1] == ' ' || cmd_line[i + 1] == '/');
                if (prev_space && next_space) { gt_idx = i; break; }
            }
        }
        if (gt_idx >= 0) {
            char *sub_cmd = (char *)kmalloc(4096);
            char dst_file[64];
            int sp = 0, dp = 0;
            if (sub_cmd) {
                for (int i = 0; i < gt_idx && sp < 4095; ++i) sub_cmd[sp++] = cmd_line[i];
                while (sp > 0 && sub_cmd[sp - 1] == ' ') sp--;
                sub_cmd[sp] = '\0';
                const char *p = cmd_line + gt_idx + 1;
                while (*p == ' ') p++;
                while (*p && *p != ' ' && dp < 63) dst_file[dp++] = *p++;
                dst_file[dp] = '\0';

                if (sp > 0 && dp > 0 && agent_write_denied(dst_file, "redireccion", out_buf, max_out)) { kfree(sub_cmd); ret_val = 1; goto cleanup; }
                if (sp > 0 && dp > 0) {
                    redir_active = 1; console_muted = 1; redir_buf[0] = '\0';
                    int res = dispatch_command(sub_cmd, redir_buf, 16384);
                    console_muted = 0; redir_active = 0;
                    if (res) {
                        uint32_t rlen = 0; while (redir_buf[rlen]) rlen++;
                        if (vfs_write(dst_file, redir_buf, rlen) < 0) {
                            kprint("Error: redireccion fallida hacia '"); kprint(dst_file);
                            kprint("' (acceso denegado o fallo de disco).\n");
                            uint32_t epos = 0;
                            fb_puts(out_buf, max_out, &epos, "Error: redireccion denegada o fallida");
                            ret_val = 1;
                        } else {
                            kprint("Salida redirigida con exito a '"); kprint(dst_file); kprint("' (");
                            kprint_dec(rlen); kprint(" bytes)\n");
                            uint32_t pos = 0;
                            fb_puts(out_buf, max_out, &pos, "Salida redirigida a ");
                            fb_puts(out_buf, max_out, &pos, dst_file);
                            ret_val = 1;
                        }
                    }
                }
                kfree(sub_cmd);
            }
            goto cleanup;
        }
    }

    /* 5. Extraer token del nombre del comando */
    char cmd_name[32];
    uint32_t ci = 0;
    const char *p = cmd_line;
    while (*p && *p != ' ' && ci < sizeof(cmd_name) - 1) {
        cmd_name[ci++] = *p++;
    }
    cmd_name[ci] = '\0';
    while (*p == ' ') p++;

    /* 6. Búsqueda canónica en la tabla declarativa */
    for (int i = 0; i < cmd_table_count; ++i) {
        if (str_eq(cmd_table[i].name, cmd_name)) {
                        if (agent_dispatch_mode && !(cmd_table[i].flags & CMD_F_SAFE)) {
                kprint("\n[SEGURIDAD AGENTE] Comando bloqueado: '");
                kprint(cmd_table[i].name);
                kprint("' no esta autorizado para el Agente ReAct.\n");

                uint32_t denied_pos = 0;
                fb_puts(out_buf, max_out, &denied_pos,
                        "AGENT_DENIED: comando no autorizado");
                ret_val = 0;
                goto cleanup;
            }

                        /*PL8_AUTO_SPAWN_APP*/
            /* Lanzar agentes y apps en background automaticamente desde la GUI para no congelar la terminal */
                        int is_ag = (str_eq(cmd_name, "coder") || str_eq(cmd_name, "soma") || 
                         str_eq(cmd_name, "buscador") || str_eq(cmd_name, "mayordomo") || 
                         str_eq(cmd_name, "agent"));
            /* SOMA_PREFLIGHT_CHECK */
            if (str_eq(cmd_name, "app") || (str_eq(cmd_name, "run") && find_substr_ci(p, ".soma", 1) != 0)) {
                char chk_rep[256]; chk_rep[0] = '\0';
                cmd_compile(p, chk_rep, sizeof(chk_rep));
                if (find_substr_ci(chk_rep, "[COMPILER ERROR", 0) != 0 ||
                    find_substr_ci(chk_rep, "no encontrado", 0) != 0) {
                    kprint("[SOMA-LANG] Pre-flight: error sintactico detectado. Cancelando ejecucion.\n");
                    if (out_buf && max_out) {
                        uint32_t ep = 0; fb_puts(out_buf, max_out, &ep, chk_rep);
                    }
                    ret_val = 0;
                    goto cleanup;
                }
            }
            int is_app = str_eq(cmd_name, "app") ||
                         (str_eq(cmd_name, "run") && find_substr_ci(p, ".soma", 1) != 0); /*PL12_AUTO_SPAWN*/
            /* 2048 es el max_out hardcodeado del worker de background, evitamos recursividad */
            if (((is_ag && !agent_dispatch_mode) || is_app) && fb_is_active() && out_buf != 0 && max_out != 2048) {
                char spawn_wrap[512];
                uint32_t sw_p = 0;
                fb_puts(spawn_wrap, sizeof(spawn_wrap), &sw_p, cmd_name);
                fb_putc(spawn_wrap, sizeof(spawn_wrap), &sw_p, ' ');
                fb_puts(spawn_wrap, sizeof(spawn_wrap), &sw_p, p);
                ret_val = cmd_spawn(spawn_wrap, out_buf, max_out);
            } else {
                ret_val = cmd_table[i].handler(p, out_buf, max_out);
            }
            goto cleanup;
        }
    }

    /* 7. Fallback para utilidades SRCFS */
    if (src_tool(cmd_line, out_buf, max_out)) {
        ret_val = 1;
        goto cleanup;
    }

cleanup:
    kfree(expanded_cmd);
    kfree(redir_buf);
    kfree(pipe_buf);
    return ret_val;
}

void shell_run(void)
{
    char cmd[512];

    for (;;) {
        thread_preempt_point();      /* PARCHE 072: fin de rodaja -> cede la CPU a netd/sysmon/httpd */
        if (agent_resume_pending == 1) {
            const char *pre = agent_resume_name[0] ? agent_resume_name : "soma";
            uint32_t ci = 0;
            while (*pre) cmd[ci++] = *pre++;
            cmd[ci++] = ' ';
            for (uint32_t k = 0; agent_resume_mission[k] && ci < sizeof(cmd) - 1; ++k) cmd[ci++] = agent_resume_mission[k];
            cmd[ci] = '\0';
            kprint("\033[1;36msoma\033[1;32m>\033[0m "); kprint(cmd); kprint(" [reanudacion automatica]\n");
        } else {
            kprint("\033[1;36msoma\033[1;32m>\033[0m ");
            kgetline(cmd, sizeof(cmd));
        }

        char *s0 = cmd; while (*s0 == ' ') s0++;
        if (*s0 == '\0') continue;

        static char shell_buf[2048];
        if (!dispatch_command(s0, shell_buf, sizeof(shell_buf))) {
            kprint("Comando desconocido: '"); kprint(s0); kprint("'. Escribe 'help'.\n");
        }
    }
}
