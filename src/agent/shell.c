// SOMA_233_SHELL_PARTIDO: marcas de parches anteriores cuyo codigo esta ahora en los archivos por temas. Los scripts de parche
// buscan su marca en este archivo para saber si ya estan aplicados; se conservan aqui para que la sigan encontrando:
//   /*PL6_WRITE_ESCAPES*/ | /*PL7_WRITE_ROBUST*/ | PARCHE 032 | PARCHE 034 | PARCHE 037 | PARCHE 039 | PARCHE 040
//   PARCHE 043 | PARCHE 045 | PARCHE 050 | PARCHE 063 | PARCHE 067 | PARCHE 071 | PARCHE 073 | PARCHE 076 | PARCHE 077
//   PARCHE 081 | PARCHE 082 | PARCHE 084 | PARCHE 085 | PARCHE 088 | PARCHE 089 | PARCHE 093 | PARCHE 097 | PARCHE 117
//   PARCHE 118 | PARCHE 119 | PARCHE 120 | PARCHE 122 | PARCHE 127 | PARCHE 132 | PARCHE 138 | PARCHE 142
//   PARCHE_FEEDBACK_V1 | PARCHE_HORA_LS_V1 | PARCHE_RED_SEGURO_V1 | PARCHE_SEARCH_V5 | SOMA_156C | SOMA_157_T480S
//   SOMA_158_T480S | SOMA_159_T480S | SOMA_162_T480S | SOMA_164B_T480S | SOMA_166_T480S | SOMA_167_T480S
//   SOMA_168_T480S | SOMA_170_T480S | SOMA_175A_T480S | SOMA_175C | SOMA_176A | SOMA_177_T480S | SOMA_192 | SOMA_194
//   SOMA_195 | SOMA_196 | SOMA_198 | SOMA_199 | SOMA_202 | SOMA_203 | SOMA_207 | SOMA_208 | SOMA_209 | SOMA_220_PRUEBA
//   SOMA_221_RESCATE | SOMA_CODER_AUTONOMOUS_PATCH | SOMA_LANG_LINT_CMD | SOMA_NVME | SOMA_PARCHE_008_PANIC
//   SOMA_PARCHE_011_SRCFS | SOMA_PARCHE_012_RENDER | SOMA_PARCHE_014_RING3 | SOMA_PARCHE_016_EXEC
//   SOMA_PARCHE_018_BIGFS | SOMA_PARCHE_020_AUDIO | SOMA_PARCHE_023_MEDIA | SOMA_PARCHE_024_TERM | SOMA_SATA
//   SOMA_USB | SOMA_VECTOR_FONTS_PATCH
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
#include "youtube.h"   /* SOMA_250_YOUTUBE */
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
#include "voz_sys.h"   /* SOMA_241_VOZ */
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
#include "coder_core.h"            /* SOMA_275_HTML: cc_js_error */
#include "docagent.h"   /* SOMA_248_DOC */
#include "busca_web.h"  /* SOMA_251_RUTAS */
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
#include "sysdisk.h"
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
#include "memoria.h"   /* SOMA_277_MEMORIA */
#include "llm.h"
#include "io.h"
#include "arch.h"
#include "idt.h"
#include "disk_layout.h"
#include "ramstat.h"
#include "soma_str.h"

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
volatile uint32_t agent_denied_writes;
static int agent_write_denied(const char *path, const char *cmd, char *out_buf, uint32_t max_out)
{
    /* SOMA_268_ACCESO: la persona en el terminal solo escribe en su unidad (/usuario) y en /tmp */
    if (!agent_dispatch_mode && !vfs_usuario_puede_escribir(path)) {
        kprint(cmd); kprint(": solo puedes escribir en /usuario (y /tmp). La configuracion y la memoria se cambian desde sus programas (Ajustes...).\n");
        if (out_buf && max_out) { uint32_t pp = 0; soma_str_put(out_buf, max_out, &pp, "Error: solo puedes escribir en /usuario (y /tmp)."); }
        return 1;
    }
    /* SOMA_193: tampoco /www ni /public: lo que hay ahi lo sirve httpd a toda la red (seria publicar datos del usuario) */
    int published = agent_dispatch_mode && path && (agent_path_has_prefix(path, "/www") || agent_path_has_prefix(path, "/public"));
    if (!agent_dispatch_mode || (!agent_path_protected(path) && !published)) return 0;
    agent_denied_writes++;
    kprint("[SEGURIDAD AGENTE] "); kprint(cmd); kprint(published ? ": denegado, los agentes no pueden publicar en /www ni /public (lo sirve el servidor web).\n" : ": denegado, los agentes no pueden modificar /etc, /sys, /bin ni /boot.\n");
    if (out_buf && max_out) { uint32_t pp = 0; soma_str_put(out_buf, max_out, &pp, "Error: Operacion denegada por politica de seguridad."); }
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
    soma_str_put(b, sizeof b, &p, "# Destinos de red de los agentes (parche 193). Lo cambia la orden agente_red.\nmodo=");
    soma_str_put(b, sizeof b, &p, m == AGENT_NET_ABIERTO ? "abierto" : m == AGENT_NET_CERRADO ? "cerrado" : "procedencia");
    soma_str_put(b, sizeof b, &p, "\n");
    for (int i = 0; agent_net_allow_get(i); ++i) { soma_str_put(b, sizeof b, &p, "permitir="); soma_str_put(b, sizeof b, &p, agent_net_allow_get(i)); soma_str_put(b, sizeof b, &p, "\n"); }
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
        soma_str_put(out_buf, max_out, &pp, "AGENT_DENIED: destino de red no permitido. Solo puedes pedir una URL que aparezca TAL CUAL en la mision del usuario, en un resultado de search o en una pagina ya descargada. No compongas URL ni les anadas datos.");
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

/* SOMA_268_ACCESO: lo que escribe la persona en el terminal (GUI o de texto): se ejecuta en sesion de usuario */
int dispatch_command_usuario(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    int antes = vfs_sesion_usuario(1);
    int r = dispatch_command(cmd_line, out_buf, max_out);
    vfs_sesion_usuario(antes);
    return r;
}

int dispatch_agent_command(const char *cmd_line, char *out_buf, uint32_t max_out)
{
    int previous = agent_dispatch_mode;
    agent_dispatch_mode = 1;

    { extern void gui_fase_orden(const char *linea); gui_fase_orden(cmd_line); }   /* SOMA_259_ORBE: orden o busqueda en la barra del orbe */
    int result = dispatch_command(cmd_line, out_buf, max_out);
    if (out_buf && max_out && agent_cmd_is_external(cmd_line)) agent_net_observe(out_buf);   /* SOMA_193 */

    agent_dispatch_mode = previous;
    return result;
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
    soma_str_put(out, max, &pos, "Dispositivos PCI detectados:\n");
    uint32_t count = 0;

    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFF) continue;
                uint16_t dev = pci_device_id((uint8_t)bus, slot, func);
                count++;

                soma_str_put(out, max, &pos, "  PCI ");
                soma_str_put_hex(out, max, &pos, bus, 4, 1);
                soma_str_putc(out, max, &pos, ':');
                soma_str_put_dec(out, max, &pos, slot);
                soma_str_putc(out, max, &pos, '.');
                soma_str_put_dec(out, max, &pos, func);
                soma_str_put(out, max, &pos, " [0x");
                soma_str_put_hex(out, max, &pos, vendor, 4, 1);
                soma_str_put(out, max, &pos, ":0x");
                soma_str_put_hex(out, max, &pos, dev, 4, 1);
                soma_str_put(out, max, &pos, "] ");
                soma_str_put(out, max, &pos, pci_device_name(vendor, dev));

                if (vendor == 0x1AF4) {
                    uint32_t bar0 = pci_bar0((uint8_t)bus, slot, func);
                    soma_str_put(out, max, &pos, " (BAR0=0x");
                    soma_str_put_hex(out, max, &pos, bar0 & 0xFFFFFFFC, 8, 1);
                    soma_str_putc(out, max, &pos, ')');
                }
                soma_str_putc(out, max, &pos, '\n');
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
        if (out_buf) soma_str_put(out_buf, max_out, &pos, "hexdump: archivo no encontrado");
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
    if (out_buf) soma_str_put(out_buf, max_out, &pos, "[HEXDUMP OK]");
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

    soma_str_put(out_buf, max_out, &pos, "SOMA v0.2 Operational");
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

/* SOMA_233_SHELL_PARTIDO: las ordenes, por temas (se incluyen aqui, en este orden; ver la cabecera de cada archivo) */
#include "shell_sistema.c"         /* sistema y hardware */
#include "shell_seguridad.c"       /* IA, aislamiento y criptografia */
#include "shell_dispositivos.c"    /* dispositivos */
#include "shell_diagnostico.c"     /* diagnostico y pruebas */
#include "shell_procesos.c"        /* programas y procesos */
#include "shell_archivos.c"        /* archivos */
#include "shell_red.c"             /* red */
#include "shell_agente.c"          /* agentes y modelo */
#include "shell_apps.c"            /* aplicaciones y portatil */
#include "shell_agenda.c"          /* SOMA_272_AGENDA: calendario */
#include "shell_js.c"              /* SOMA_274_JS: JavaScript (orden js) */
#include "shell_html.c"            /* SOMA_275_HTML: webs (orden html) */
#include "shell_utilidades.c"      /* utilidades de terminal */
#include "lcm_ordenes_shell.inc"    /* SOMA_258_LCM: ordenes cerrar y contiene (despues de las utilidades: usa sus ayudantes) */
#include "llm_limites_shell.inc"    /* SOMA_260_LLM_TAM: orden llm-limites */

static const struct command_entry cmd_table[] = {
        /* Scripting y Automatizacion (SOMA-LANG) */
    { "vm",           cmd_lang_vm,      CMD_F_SAFE,   "Test y estado de la maquina virtual SOMA-VM" },
    { "programa",     cmd_programa,     CMD_F_SAFE,   "Crea o modifica un programa con IA (SOMA-LANG o Python): programa [archivo.soma|archivo.py] <mision>  (\"en Python\" elige Python)" },
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
    { "habla",        cmd_habla,        CMD_F_NORMAL, "Texto a voz (voz de Piper): habla texto | habla instalar | habla parar | habla (estado)" },
    { "voz",          cmd_voz,          CMD_F_NORMAL, "Voz a texto (whisper.cpp, espanol): voz archivo.wav | voz ultima | voz instalar | voz modelo. Opciones: -T marcas de tiempo, -l idioma, -v detalles" },
    { "python",       cmd_python,       CMD_F_NORMAL, "Python (MicroPython en ring 3; ventanas con from soma import win): python | python script.py [args] | python -c \"codigo\". Rutas relativas: /workspace" },
    { "cc",           cmd_cc,           CMD_F_NORMAL, "Compilador de C (TinyCC en ring 3; ventanas con #include <soma.h>): cc hola.c | cc -run hola.c [args] | cc -run hola.elf | cc a.c b.c -o x.elf. Rutas relativas: /workspace" },   /* SOMA_265_CC */
    { "html",         cmd_html,         CMD_F_NORMAL, "Webs: html nuevo NOMBRE [basica|app|juego] | html comprobar ARCHIVO | html empaquetar ARCHIVO [SALIDA] | html abrir ARCHIVO (rutas relativas: /workspace)" },
    { "js",           cmd_js,           CMD_F_NORMAL, "JavaScript (el motor del navegador en ring 3; ventanas con win.create): js archivo.js [args] | js -c \"codigo\". Rutas relativas: /workspace" },
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
    { "lcm",          cmd_lcm,           CMD_F_NORMAL, "Traductor del LCM: lcm <frase> (que orden entenderia, sin ejecutar) | lcm estado | lcm encender | lcm apagar | lcm modelo [fabrica|datos]" },   /* SOMA_253_LCM SOMA_271_LCM */
    { "cerrar",       cmd_cerrar,        CMD_F_NORMAL, "Cierra aplicaciones: cerrar (la ventana de delante) | cerrar todo | cerrar <aplicacion> (navegador, paint, notas, youtube...)" },   /* SOMA_258_LCM */
    { "contiene",     cmd_contiene,      CMD_F_SAFE,   "Que archivos tienen un texto dentro: contiene <texto> [ruta] [-a]" },   /* SOMA_258_LCM */
    { "llm-limites",  cmd_llm_limites,   CMD_F_NORMAL, "Tamanos del dialogo con el modelo: llm-limites | contexto <tokens> | salida <tokens> | como-antes" },   /* SOMA_260_LLM_TAM */
    { "delegate",     delegate_cmd,     CMD_F_SAFE,   "Delega una sub-mision especializada a otro agente" },
    { "myos",         cmd_soma,         CMD_F_ALIAS,  "Alias de soma" },

    /* Memoria Estructurada en Árbol */
    { "memoria",      memoria_cmd,      CMD_F_NORMAL, "La memoria de SOMA (hechos, mensajes, embeddings): memoria estado | recordar | buscar | ver | olvidar | probar | servidor | config | importar" },   /* SOMA_277_MEMORIA, SOMA_278_EMBEDDINGS */
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
    { "youtube",      cmd_youtube,      CMD_F_NORMAL, "YouTube: youtube [busqueda] | servidor <ip[:puerto]>|auto | clave <texto> | calidad 240|360|480 | fps N | modo nativo|servicio | mp4 <direccion> | ia on|off | ver N | transcribir | subtitulos | resumen | pregunta <texto> | idioma X | minutos N | contexto N | estado" },
    { "monitor",      cmd_monitor,      CMD_F_SAFE,   "Monitor del sistema: CPU, memoria, procesos, graficos, discos, red y hardware" },
    { "agenda",       cmd_agenda,       CMD_F_SAFE,   "Calendario: agenda a\xC3\xB1" "adir el miercoles dia 6 a las 13 horas futbol | hoy | ma\xC3\xB1" "ana | semana | mes | borrar N | abrir | avisos | exportar | importar" },
    { "imagen",       cmd_imagen,       CMD_F_SAFE,   "Visor de imagenes: imagen [archivo] (PNG, JPEG, GIF, BMP de cualquier tamano)" },
    { "pdf",          cmd_pdf,          CMD_F_SAFE,   "Lector de PDF: pdf [archivo.pdf]" },
    { "traza",        cmd_traza,        CMD_F_SAFE,   "Tiempos y tokens de las ultimas llamadas al modelo (/agent/traza.txt)" },
    { "documento",    cmd_documento,    CMD_F_NORMAL, "Documentos profesionales con IA: documento <peticion> | exportar <ruta> [pdf|html|png] | revisar <ruta> | abrir <ruta>" },
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
    { "memoria",    "memoria [estado|recordar TEXTO|buscar CONSULTA|ver N|olvidar N|probar|servidor arrancar|parar|config|importar|guardar|ayuda]" },
    { "mayordomo",  "mayordomo <tarea>   p. ej. 'muestra la RAM', 'que procesos hay', 'ayuda ram'" },
    { "lcm",        "lcm <frase> | estado | encender | apagar | modelo [fabrica|datos] | voz natural|tecnica | umbral N | aprendido | aprende <frase> => <orden> | olvida N|todo" },   /* SOMA_253_LCM SOMA_257_LCM SOMA_271_LCM */
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
        soma_str_put(o, max, p, "\n    ");
        *col = 4;
    } else if (*col > 4) {
        soma_str_putc(o, max, p, ' ');
        (*col)++;
    }
    soma_str_put(o, max, p, word);
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
        soma_str_put(o, max, p, "== ");
        soma_str_put(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
        soma_str_put(o, max, p, " (");
        soma_str_put_dec(o, max, p, (uint64_t)k);
        soma_str_put(o, max, p, ") ==\n");
        for (int i = 0; i < cmd_table_count; ++i) {
            const struct command_entry *e = &cmd_table[i];
            if (e->flags & CMD_F_ALIAS) continue;
            if (only_safe && !(e->flags & CMD_F_SAFE)) continue;
            if (!help_in_cat(e, cat)) continue;
            if (with_help) {
                uint32_t nl = shell_strlen(e->name);
                soma_str_put(o, max, p, "  ");
                soma_str_put(o, max, p, e->name);
                for (uint32_t s = nl; s < 14; ++s) soma_str_putc(o, max, p, ' ');
                soma_str_putc(o, max, p, ' ');
                if (tag) soma_str_put(o, max, p, (e->flags & CMD_F_SAFE) ? "[A] " : "    ");
                soma_str_put(o, max, p, "- ");
                soma_str_put(o, max, p, e->help);
                soma_str_putc(o, max, p, '\n');
            } else {
                if (col == 0) { soma_str_put(o, max, p, "    "); col = 4; }
                help_put_wrapped(o, max, p, e->name, &col);
            }
        }
        if (!with_help && col) soma_str_putc(o, max, p, '\n');
    }
}

static void help_detail(char *o, uint32_t max, uint32_t *p, const struct command_entry *e)
{
    const struct command_entry *pr = help_primary(e);
    const char *usage = help_usage_for(pr->name);
    int cat = help_category_of(pr->name);
    int first = 1;

    soma_str_put(o, max, p, pr->name);
    soma_str_put(o, max, p, " - ");
    soma_str_put(o, max, p, pr->help);
    soma_str_putc(o, max, p, '\n');
    if (e != pr) {
        soma_str_put(o, max, p, "  ('");
        soma_str_put(o, max, p, e->name);
        soma_str_put(o, max, p, "' es un alias de '");
        soma_str_put(o, max, p, pr->name);
        soma_str_put(o, max, p, "')\n");
    }
    soma_str_put(o, max, p, "  Categoria : ");
    soma_str_put(o, max, p, cat >= 0 ? help_categories[cat].title : "Otros (sin categoria)");
    soma_str_putc(o, max, p, '\n');
    for (int i = 0; i < cmd_table_count; ++i) {
        if ((cmd_table[i].flags & CMD_F_ALIAS) && cmd_table[i].handler == pr->handler) {
            soma_str_put(o, max, p, first ? "  Alias     : " : ", ");
            soma_str_put(o, max, p, cmd_table[i].name);
            first = 0;
        }
    }
    if (!first) soma_str_putc(o, max, p, '\n');
    if (usage) {
        soma_str_put(o, max, p, "  Uso       : ");
        soma_str_put(o, max, p, usage);
        soma_str_putc(o, max, p, '\n');
    }
    soma_str_put(o, max, p, "  Agentes   : ");
    soma_str_put(o, max, p, (pr->flags & CMD_F_SAFE)
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
        if (p) soma_str_putc(out, max, &p, ' ');
        soma_str_put(out, max, &p, e->name);
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
        soma_str_put(o, HELP_BUF, &p, "Comandos disponibles en SOMA 0.3 pre-beta (");
        soma_str_put_dec(o, HELP_BUF, &p, (uint64_t)total);
        soma_str_put(o, HELP_BUF, &p, " comandos, ");
        soma_str_put_dec(o, HELP_BUF, &p, (uint64_t)aliases);
        soma_str_put(o, HELP_BUF, &p, " alias)\n");
        if (!all) {
            soma_str_put(o, HELP_BUF, &p, "Uso: help <categoria> | help <comando> | help <texto> | help all | help agentes\n");
        } else if (only_safe) {
            soma_str_put(o, HELP_BUF, &p, "Comandos que SOMA y Mayordomo pueden ejecutar:\n");
        } else {
            soma_str_put(o, HELP_BUF, &p, "[A] = utilizable por los agentes (SOMA / Mayordomo)\n");
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
                soma_str_put(o, HELP_BUF, &p, "Resultados para '");
                soma_str_put(o, HELP_BUF, &p, q);
                soma_str_put(o, HELP_BUF, &p, "':\n");
                for (int i = 0; i < cmd_table_count; ++i) {
                    const struct command_entry *c = &cmd_table[i];
                    uint32_t nl;
                    if (c->flags & CMD_F_ALIAS) continue;
                    if (!find_substr_ci(c->name, q, 1) && !find_substr_ci(c->help, q, 1)) continue;
                    nl = shell_strlen(c->name);
                    soma_str_put(o, HELP_BUF, &p, "  ");
                    soma_str_put(o, HELP_BUF, &p, c->name);
                    for (uint32_t s = nl; s < 14; ++s) soma_str_putc(o, HELP_BUF, &p, ' ');
                    soma_str_put(o, HELP_BUF, &p, " - ");
                    soma_str_put(o, HELP_BUF, &p, c->help);
                    soma_str_putc(o, HELP_BUF, &p, '\n');
                    hits++;
                }
                if (!hits) soma_str_put(o, HELP_BUF, &p, "  sin resultados. Escribe 'help' para ver las categorias.\n");
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
            soma_str_put(out_buf, max_out, &ep, "Error: anidamiento de comandos excesivo");
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
                if (out_buf && max_out) { uint32_t ep = 0; soma_str_put(out_buf, max_out, &ep, "El agente SOMA esta usando la consola en este momento; vuelve a intentarlo en unos segundos."); }
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
                            soma_str_put(out_buf, max_out, &epos, "Error: redireccion denegada o fallida");
                            ret_val = 1;
                        } else {
                            kprint("Salida redirigida con exito a '"); kprint(dst_file); kprint("' (");
                            kprint_dec(rlen); kprint(" bytes)\n");
                            uint32_t pos = 0;
                            soma_str_put(out_buf, max_out, &pos, "Salida redirigida a ");
                            soma_str_put(out_buf, max_out, &pos, dst_file);
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
                soma_str_put(out_buf, max_out, &denied_pos,
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
                        uint32_t ep = 0; soma_str_put(out_buf, max_out, &ep, chk_rep);
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
                soma_str_put(spawn_wrap, sizeof(spawn_wrap), &sw_p, cmd_name);
                soma_str_putc(spawn_wrap, sizeof(spawn_wrap), &sw_p, ' ');
                soma_str_put(spawn_wrap, sizeof(spawn_wrap), &sw_p, p);
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
        if (!dispatch_command_usuario(s0, shell_buf, sizeof(shell_buf))) {
            extern int lcm_terminal(const char *linea);   /* SOMA_253_LCM: una frase en vez de una orden */
            if (!lcm_terminal(s0)) { kprint("Comando desconocido: '"); kprint(s0); kprint("'. Escribe 'help'.\n"); }
        }
    }
}
