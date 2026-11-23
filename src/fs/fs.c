#include "image.h"
#include "mutex.h"
#include <stdint.h>
#include "console.h"
#include "fs.h"
#include "acct.h"          /* SOMA_198: ACCT_WEB */
#include "rng.h"
#include "mem.h"
#include "sysdisk.h"
#include "slotio.h"
#include "ckptio.h"
#include "sysinfo.h"
#include "pmm.h"
#include "vmm.h"
#include "thread.h"
#include "rtc.h"
#include "net.h"
#include "io.h"
#include "arch.h"
#include "idt.h"
#include "virtio_net.h"
#include "disk_layout.h"
#include "agent_policy.h"  /* SOMA_268_ACCESO */
#include "unidades.h"      /* SOMA_267_DISCOS: /usuario y /modelos (las unidades exFAT del pendrive) */

#define FS_SUPER_LBA         SOMA_LBA_FS_SUPER
#define FS_DATA_LBA          SOMA_LBA_FS_DATA
#define FS_SECTORS_PER_FILE  33
#define FS_DATA_SECTORS      32
#define FS_CHECKPOINT_LBA    SOMA_LBA_CHECKPOINT
#define FS_JOURNAL_LBA       SOMA_LBA_FS_JOURNAL
#define FS_TOTAL_SECTORS     (1 + FS_DISK_SLOTS * FS_SECTORS_PER_FILE)
_Static_assert(SOMA_LBA_CKPT_B_SPLIT < FS_TOTAL_SECTORS && SOMA_LBA_CKPT_B_DATA + SOMA_LBA_CKPT_B_SPLIT == SOMA_LBA_SRCFS_INDEX &&
               SOMA_LBA_CKPT_B_DATA2 + (FS_TOTAL_SECTORS - SOMA_LBA_CKPT_B_SPLIT) == SOMA_LBA_CHECKPOINT - 1, "ranura B del checkpoint: tramos fuera de sitio");
static struct ckpt_slot fs_ckpt_a(void) { return ckpt_linear(FS_CHECKPOINT_LBA - 1, FS_CHECKPOINT_LBA, FS_TOTAL_SECTORS); }
static struct ckpt_slot fs_ckpt_b(void) { struct ckpt_slot x; x.marker = SOMA_LBA_CKPT_B_MARKER; x.base = SOMA_LBA_CKPT_B_DATA; x.split = SOMA_LBA_CKPT_B_SPLIT; x.base2 = SOMA_LBA_CKPT_B_DATA2; return x; }

struct fs_superblock {
    char     magic[8];       /* "SOMAFS02" */
    uint32_t version;
    uint32_t num_files;
    uint32_t max_files;
    uint32_t max_file_size;
    uint8_t  pad[488];
} __attribute__((aligned(64)));

struct fs_disk_header {
    char     name[FS_NAME_MAX]; /* 48 B */
    uint32_t size;              /* 4 B */
    uint8_t  used;              /* 1 B */
    uint8_t  spilled;           /* PARCHE 087: 'B' = contenido en BIGFS (los nucleos viejos ven size >= 16 KiB y lo ignoran) */
    uint8_t  pad[10];           /* 10 B -> 64 B */
    uint8_t  sec_pad[448];      /* Total 512 B (Sector 0 del inodo) */
} __attribute__((aligned(64)));

static struct vfs_file files[FS_MAX_FILES] __attribute__((aligned(8)));
static struct kmutex vfs_mutex __attribute__((aligned(16)));
static int fs_initialized = 0;
static int fs_spill_warned;

/* PARCHE 087: reserva exacta (con margen) para el contenido; conserva los datos si keep */
static int ensure_cap(int i, uint32_t need, int keep)
{
    if (need > FS_FILE_MAX + 1) return -2;
    if (files[i].data && files[i].cap >= need) return 0;
    uint32_t c = need < 64 ? 64 : need + need / 8;
    if (c > FS_FILE_MAX + 1) c = FS_FILE_MAX + 1;
    uint8_t ow = kheap_owner; kheap_owner = 1;               /* PARCHE 089: memoria de RamFS (ACCT_RAMFS) */
    char *nd = (char *)kmalloc(c);
    kheap_owner = ow;
    if (!nd) return -1;
    if (keep && files[i].data && files[i].size) memcpy(nd, files[i].data, files[i].size);
    if (files[i].data) kfree(files[i].data);
    files[i].data = nd; files[i].cap = c;
    return 0;
}
static void drop_data(int i) { if (files[i].data) kfree(files[i].data); files[i].data = 0; files[i].cap = 0; }
/* el contenido va al almacen grande si no cabe en una ranura o si el archivo no tiene ranura */
static int needs_spill(int i) { return i >= FS_DISK_SLOTS || files[i].size >= FS_DATA_MAX; }

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static int vfs_names_equal(const char *stored, const char *query)
{
    if (!stored || !query) return 0;
    if (str_eq(stored, query)) return 1;
    if (stored[0] == '/' && str_eq(stored + 1, query)) return 1;
    if (query[0] == '/' && str_eq(stored, query + 1)) return 1;
    return 0;
}

static const char *fs_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

static uint32_t str_len(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void fb_puts(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    if (!out || max == 0 || !pos || *pos >= max - 1) return;
    while (*s && *pos + 1 < max) out[(*pos)++] = *s++;
    out[*pos] = '\0';
}

static void fb_put_dec(char *out, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24]; int n = 0;
    if (v == 0) { if (*pos + 1 < max) { out[(*pos)++] = '0'; out[*pos] = '\0'; } return; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && *pos + 1 < max) out[(*pos)++] = tmp[--n];
    out[*pos] = '\0';
}

static int is_ephemeral_path(const char *path)
{
    return (fs_strstr(path, "/sys/") != 0 || fs_strstr(path, "/tmp/") != 0);
}

static int is_proc_path(const char *path)
{
    return (path[0] == '/' && path[1] == 'p' && path[2] == 'r' && path[3] == 'o' && path[4] == 'c' && (path[5] == '/' || path[5] == '\0'));
}

static int is_dev_path(const char *path)
{
    return (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && (path[4] == '/' || path[4] == '\0'));
}

int vfs_is_hidden(const char *name)
{
    if (!name) return 0;
    const char *p = name;
    while (*p) {
        if (*p == '/' && *(p + 1) == '.') return 1;
        p++;
    }
    if (name[0] == '.') return 1;

    if (fs_strstr(name, "/agent/state") != 0) return 1;
    if (fs_strstr(name, "_stm.txt") != 0) return 1;
    if (fs_strstr(name, "/sys/") != 0) return 1;
    if (fs_strstr(name, "/tmp/") != 0) return 1;
    if (fs_strstr(name, "manifest") != 0) return 1;

    return 0;
}

/* SOMA_193 (seguridad): archivos con secretos que un AGENTE no puede LEER por ninguna via (cat, grep, cp, head, source, busquedas...):
 * la clave de la WiFi (/etc/wifi.conf guarda la PMK, equivalente a la contrasena), el historial del terminal (puede llevar
 * contrasenas tecleadas), la configuracion del LLM y todo lo que el usuario ponga en /etc/secreto/. Se decide aqui, en el VFS, y no
 * en cada orden: asi no hay orden que se lo salte. El usuario (terminal, aplicaciones) no se ve afectado. */
int vfs_agent_secret(const char *name)
{
    static const char *const secret[] = { "/etc/wifi.conf", "/etc/history.txt", "/etc/llm.conf", 0 };
    struct tcb *t = thread_current();
    if (!t || !name) return 0;
    if (!t->agent_mode && t->owner != ACCT_WEB) return 0;                  /* SOMA_198: tampoco el motor del navegador (file:///etc/llm.conf) */
    if (fs_strstr(name, "/etc/secreto/") == name) return 1;
    for (int i = 0; secret[i]; ++i) if (vfs_names_equal(secret[i], name)) return 1;
    return 0;
}

/* SOMA_268_ACCESO: hilos en sesion de usuario (el terminal y el terminal de texto; a lo sumo unos pocos a la vez) */
#define SESION_USU_MAX 4
static struct tcb *sesion_usu[SESION_USU_MAX];
int vfs_sesion_usuario_activa(void)
{
    struct tcb *t = thread_current();
    if (!t || t->agent_mode) return 0;                                  /* los agentes tienen su propia politica (agent_policy.h) */
    for (int i = 0; i < SESION_USU_MAX; ++i) if (sesion_usu[i] == t) return 1;
    return 0;
}
int vfs_sesion_usuario(int on)
{
    struct tcb *t = thread_current();
    int antes = 0;
    if (!t) return 0;
    for (int i = 0; i < SESION_USU_MAX; ++i) if (sesion_usu[i] == t) { antes = 1; if (!on) sesion_usu[i] = 0; }
    if (on && !antes) for (int i = 0; i < SESION_USU_MAX; ++i) if (!sesion_usu[i]) { sesion_usu[i] = t; break; }
    return antes;
}
int vfs_usuario_puede_escribir(const char *name)
{
    return !vfs_sesion_usuario_activa() || usuario_ruta_escribible(name);
}

static int agent_path_denied(const char *name)
{
    static const char *const deny_exact[] = {
        "/etc/init.sh", "/etc/soma_manifest.txt", "/etc/history.txt", 0
    };
    struct tcb *t = thread_current();
    if (!t || !t->agent_mode || !name) return 0;

    if (str_eq(name, "/agent/state") || 
        str_eq(name, "/agent/blackboard.txt") ||
        fs_strstr(name, "_stm.txt") != 0) {
        return 0;
    }

    int deny = (fs_strstr(name, "/agent/") == name);
    for (int i = 0; !deny && deny_exact[i]; ++i) {
        if (str_eq(name, deny_exact[i])) deny = 1;
    }
    if (deny) {
        kprint("\n[SEGURIDAD AGENTE] Escritura denegada en '");
        kprint(name);
        kprint("' (ruta de sistema protegida).\n");
    }
    return deny;
}

/* SOMA_192: /dev/urandom salia de un xorshift con semilla CONSTANTE (0x88888888): la misma secuencia en cada arranque.
 * Ahora sale del generador unico del nucleo, sin sesgo (se descartan los valores 62 y 63 de cada 6 bits). */
static void urandom_text(char *out, uint32_t n)
{
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    uint8_t r[64]; uint32_t have = 0, i = 0;
    while (i < n) {
        if (!have) { rng_bytes(r, sizeof r); have = sizeof r; }
        uint8_t v = r[--have] & 63;
        if (v < 62) out[i++] = charset[v];
    }
}

static int read_proc_file(const char *name, char *buf_out, uint32_t max_len)
{
    uint32_t pos = 0;
    if (str_eq(name, "/proc/version")) {
        fb_puts(buf_out, max_len, &pos, "SOMA version 0.2-release (" MYOS_ARCH_NAME ") gcc-freestanding #1 Oct 2026\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/uptime")) {
        uint64_t ms = timer_get_uptime_ms();
        fb_put_dec(buf_out, max_len, &pos, ms / 1000ULL);
        fb_puts(buf_out, max_len, &pos, ".");
        uint32_t frac = (uint32_t)(ms % 1000ULL);
        if (frac < 100) fb_puts(buf_out, max_len, &pos, "0");
        if (frac < 10)  fb_puts(buf_out, max_len, &pos, "0");
        fb_put_dec(buf_out, max_len, &pos, frac);
        fb_puts(buf_out, max_len, &pos, " seconds\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/meminfo")) {
        size_t f_free = 0, f_used = 0, f_total = 0;
        pmm_get_stats(&f_free, &f_used, &f_total);
        size_t h_used = 0, h_free = 0;
        kheap_stats(&h_used, &h_free);
        fb_puts(buf_out, max_len, &pos, "MemTotal:       "); fb_put_dec(buf_out, max_len, &pos, (f_total * 4096ULL) / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nMemFree:        "); fb_put_dec(buf_out, max_len, &pos, (f_free * 4096ULL) / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nHeapFree:       "); fb_put_dec(buf_out, max_len, &pos, h_free / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\nHeapUsed:       "); fb_put_dec(buf_out, max_len, &pos, h_used / 1024ULL);
        fb_puts(buf_out, max_len, &pos, " kB\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/threads")) {
        return thread_format_table(buf_out, max_len);
    }
    if (str_eq(name, "/proc/net/dev")) {
        uint32_t tx_p = 0, rx_p = 0; uint64_t tx_b = 0, rx_b = 0;
        virtio_net_stats(&tx_p, &rx_p, &tx_b, &rx_b);
        fb_puts(buf_out, max_len, &pos, "Interface eth0: rx_bytes="); fb_put_dec(buf_out, max_len, &pos, rx_b);
        fb_puts(buf_out, max_len, &pos, " tx_bytes="); fb_put_dec(buf_out, max_len, &pos, tx_b);
        fb_puts(buf_out, max_len, &pos, "\n");
        return (int)pos;
    }
    if (str_eq(name, "/proc/net/arp")) {
        return arp_format_cache(buf_out, max_len);
    }
    return -1;
}

static int read_dev_file(const char *name, char *buf_out, uint32_t max_len)
{
    if (str_eq(name, "/dev/null")) { buf_out[0] = '\0'; return 0; }
    if (str_eq(name, "/dev/zero")) {
        uint32_t n = max_len > 1 ? max_len - 1 : 0;
        for (uint32_t i = 0; i < n; ++i) buf_out[i] = '\0';
        buf_out[n] = '\0'; return (int)n;
    }
    if (str_eq(name, "/dev/urandom")) {
        uint32_t n = 64; if (n >= max_len) n = max_len - 1;
        urandom_text(buf_out, n);
        buf_out[n] = '\0'; return (int)n;
    }
    if (str_eq(name, "/dev/rtc")) {
        struct rtc_time t;
        rtc_get_datetime(&t);
        uint32_t pos = 0;
        fb_puts(buf_out, max_len, &pos, "RTC: ");
        fb_put_dec(buf_out, max_len, &pos, t.year);
        fb_puts(buf_out, max_len, &pos, "-");
        if (t.month < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.month);
        fb_puts(buf_out, max_len, &pos, "-");
        if (t.day < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.day);
        fb_puts(buf_out, max_len, &pos, " ");
        if (t.hour < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.hour);
        fb_puts(buf_out, max_len, &pos, ":");
        if (t.min < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.min);
        fb_puts(buf_out, max_len, &pos, ":");
        if (t.sec < 10) { fb_puts(buf_out, max_len, &pos, "0"); }
        fb_put_dec(buf_out, max_len, &pos, t.sec);
        fb_puts(buf_out, max_len, &pos, " UTC\n");
        return (int)pos;
    }
    return -1;
}

/* PARCHE 112: E/S del VFS con inyeccion de cortes (solo para pruebas: fs_fault_budget < 0 = desactivado). */
static long fs_fault_budget = -1;
static uint32_t fs_spill_fixed;
static uint32_t fs_jseq, fs_commits, fs_crc_bad, fs_jreplayed, fs_jdiscarded;
static int fs_blk_write(void *ctx, uint64_t lba, const void *buf)
{
    (void)ctx;
    if (fs_fault_budget == 0) return -1;
    if (fs_fault_budget > 0) fs_fault_budget--;
    return sysdisk_write(lba, buf);
}
static int fs_blk_read(void *ctx, uint64_t lba, void *buf) { (void)ctx; return sysdisk_read(lba, buf); }
static int fs_blk_flush(void *ctx) { (void)ctx; return sysdisk_flush(); }
static const struct slotdev fs_dev = { fs_blk_read, fs_blk_write, 0, fs_blk_flush };

static int sync_file_to_disk(int i)
{
    uint8_t sec_buf[512] __attribute__((aligned(16)));
    struct fs_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* PARCHE 087: archivos grandes y archivos sin ranura -> BIGFS */
    int spill = files[i].used && needs_spill(i);
    int del_spill_after = 0;
    if (spill) {
        if (!fs_spill_ready() || fs_spill_put(i, files[i].name, files[i].data, files[i].size) != 0) {
            files[i].ram_only = 1; files[i].dirty = 0;
            if (!fs_spill_warned) { fs_spill_warned = 1; kprint("VFS: archivo grande sin almacen BIGFS: queda solo en memoria (usa 'big format yes')\n"); }
            if (i >= FS_DISK_SLOTS) return 0;
            spill = 0;                                      /* la ranura queda libre en disco */
        } else { files[i].spilled = 1; files[i].ram_only = 0; }
    } else if (files[i].spilled) {
        /* PARCHE 113: si pasa de BIGFS a ranura en linea, el contenido en BIGFS se borra DESPUES de confirmar el nuevo (antes: primero,
         * y un corte intermedio dejaba la cabecera apuntando a un archivo inexistente). Si se borra el archivo, el orden no importa. */
        if (!files[i].used) { fs_spill_del(i); files[i].spilled = 0; } else del_spill_after = 1;
    }
    if (!files[i].used) { files[i].ram_only = 0; if (i >= FS_DISK_SLOTS) { files[i].dirty = 0; return 0; } }
    if (i >= FS_DISK_SLOTS) { files[i].dirty = 0; return 0; }

    if (files[i].used && (spill || !files[i].ram_only)) {
        memcpy(hdr.name, files[i].name, FS_NAME_MAX);
        hdr.size = files[i].size;
        hdr.used = 1;
        hdr.spilled = spill ? 'B' : 0;
    }

    uint64_t start_lba = FS_DATA_LBA + (uint64_t)i * FS_SECTORS_PER_FILE;

    /* PARCHE 112: CRC de los datos en la cabecera (pad[0..3], marca 'C' en pad[4]; los nucleos viejos ignoran pad). */
    if (hdr.used && !hdr.spilled) {
        uint32_t crc = slotio_crc32(0, files[i].data ? (const void *)files[i].data : (const void *)"", files[i].data ? files[i].size : 0);
        memcpy(hdr.pad, &crc, 4); hdr.pad[4] = 'C';
    }
    memcpy(sec_buf, &hdr, sizeof(hdr));

    if (hdr.used && !hdr.spilled) {
        /* Guardado ATOMICO con diario: tras un corte, el archivo queda entero viejo o entero nuevo (ver slotio.h). */
        static uint8_t hbuf[512] __attribute__((aligned(16)));
        memset(hbuf, 0, sizeof hbuf); memcpy(hbuf, &hdr, sizeof hdr);
        if (slotio_commit(&fs_dev, FS_JOURNAL_LBA, start_lba, (uint32_t)i, hbuf, (const uint8_t *)files[i].data, files[i].size, ++fs_jseq) != 0) return -1;
        fs_commits++;
    } else {
        /* Borrado o contenido en BIGFS: un solo sector de cabecera (la escritura de un sector ya es atomica). */
        if (fs_blk_write(NULL, start_lba, sec_buf) != 0) return -1;
    }

    if (del_spill_after) { fs_spill_del(i); files[i].spilled = 0; }
    files[i].dirty = 0;
    return 0;
}

static int load_file_from_disk(int i)
{
    uint8_t sec_buf[512] __attribute__((aligned(16)));
    struct fs_disk_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    uint64_t start_lba = FS_DATA_LBA + (uint64_t)i * FS_SECTORS_PER_FILE;

    /* 1. Leer cabecera en sector 0 */
    if (sysdisk_read(start_lba, sec_buf) != 0) return -1;
    memcpy(&hdr, sec_buf, sizeof(hdr));

    if (hdr.used > 1) return -1;
    if (hdr.used && hdr.spilled != 'B' && hdr.size >= FS_DATA_MAX) return -1;
    if (hdr.used && hdr.spilled == 'B' && hdr.size > FS_FILE_MAX) return -1;
    if (hdr.used) {
        int name_term = 0;
        for (uint32_t n = 0; n < FS_NAME_MAX; ++n) {
            if (hdr.name[n] == '\0') { name_term = 1; break; }
        }
        if (!name_term || hdr.name[0] == '\0') return -1;
    }

    if (hdr.used && hdr.spilled == 'B') {                 /* PARCHE 087: contenido en BIGFS */
        uint32_t sl = 0; char nm[FS_NAME_MAX];
        if (!fs_spill_ready() || fs_spill_stat(i, nm, &sl) != 0) return -1;
        if (sl != hdr.size) {                                /* PARCHE 113: el contenido en BIGFS se confirma ANTES que la cabecera; si un corte
                                                              * llego entre ambos, el archivo valido es el de BIGFS (nuevo y completo) */
            if (sl > FS_FILE_MAX || !nm[0]) return -1;
            hdr.size = sl; memcpy(hdr.name, nm, FS_NAME_MAX); hdr.name[FS_NAME_MAX - 1] = 0; fs_spill_fixed++;
            kprint("VFS: archivo grande '"); kprint(hdr.name); kprint("' recuperado tras un corte (BIGFS estaba mas reciente que la cabecera)\n");
        }
        files[i].size = 0;
        if (ensure_cap(i, hdr.size + 1, 0) != 0) return -1;
        if (fs_spill_get(i, files[i].data, hdr.size) != 0) { drop_data(i); return -1; }
        files[i].data[hdr.size] = '\0';
        memcpy(files[i].name, hdr.name, FS_NAME_MAX);
        files[i].size = hdr.size; files[i].used = 1; files[i].dirty = 0; files[i].spilled = 1; files[i].ram_only = 0;
        return 0;
    }
    memcpy(files[i].name, hdr.name, FS_NAME_MAX);
    files[i].size = hdr.size;
    files[i].used = hdr.used;
    files[i].dirty = 0;
    files[i].spilled = 0; files[i].ram_only = 0;

    if (hdr.used) {
        if (ensure_cap(i, hdr.size + 1, 0) != 0) return -1;
        memset(files[i].data, 0, hdr.size + 1);

        uint8_t *raw = (uint8_t *)files[i].data;
        for (int s = 0; s < FS_DATA_SECTORS; ++s) {
            if (sysdisk_read(start_lba + 1 + s, sec_buf) != 0) return -1;
            uint32_t off = (uint32_t)s * 512;
            if (off < hdr.size) {
                uint32_t take = hdr.size - off;
                if (take > 512) take = 512;
                memcpy(raw + off, sec_buf, take);
            }
        }
        files[i].data[hdr.size] = '\0';
        if (hdr.pad[4] == 'C') {                              /* PARCHE 112: verificar integridad */
            uint32_t want; memcpy(&want, hdr.pad, 4);
            if (slotio_crc32(0, files[i].data, hdr.size) != want) {
                fs_crc_bad++;
                kprint("VFS: AVISO: el archivo '"); kprint(hdr.name); kprint("' no coincide con su CRC (dato danado en disco)\n");
            }
        }
    } else {
        drop_data(i);
    }

    return 0;
}
/* PARCHE 087: archivos por encima de las 64 ranuras guardados en BIGFS */
static int load_spill_slot(int i)
{
    char nm[FS_NAME_MAX]; uint32_t sl = 0;
    if (fs_spill_stat(i, nm, &sl) != 0 || sl > FS_FILE_MAX || !nm[0]) return -1;
    nm[FS_NAME_MAX - 1] = 0;
    if (ensure_cap(i, sl + 1, 0) != 0) return -1;
    if (fs_spill_get(i, files[i].data, sl) != 0) { drop_data(i); return -1; }
    files[i].data[sl] = '\0';
    memcpy(files[i].name, nm, FS_NAME_MAX);
    files[i].size = sl; files[i].used = 1; files[i].dirty = 0; files[i].spilled = 1; files[i].ram_only = 0;
    return 0;
}

static int vfs_sync_unlocked(void)
{
    /* PARCHE 083: sin disco virtio (Pi 5 real o emulada) RamFS vive solo en memoria: guardar no es un error (antes cada escritura
     * en una ruta normal devolvia fallo aunque el archivo quedaba bien en RAM; Paint decia "Error al guardar"). */
    if (sysdisk_capacity() == 0) {
        for (int i = 0; i < FS_MAX_FILES; ++i) files[i].dirty = 0;
        return 0;
    }
    struct fs_superblock sb;
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, "SOMAFS02", 8);
    sb.version = 2;
    sb.max_files = FS_DISK_SLOTS;
    sb.max_file_size = FS_DATA_MAX;

    uint32_t used_count = 0;
    int any_dirty = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && i < FS_DISK_SLOTS && !files[i].ram_only) used_count++;
        if (files[i].dirty) {
            any_dirty = 1;
            if (sync_file_to_disk(i) != 0) return -1;
        }
    }
    sb.num_files = used_count;
    if (any_dirty) {
        if (sysdisk_write(FS_SUPER_LBA, &sb) != 0) return -1;
    }
    return 0;
}

/* SOMA_269_RAM: guardado diferido (modo RAM). Todo guardado automatico pasa por aqui; vfs_sync() siempre guarda de verdad. */
static int fs_diferido;
static int vfs_sync_auto_unlocked(void) { return fs_diferido ? 0 : vfs_sync_unlocked(); }
void vfs_modo_ram(int on) { kmutex_lock(&vfs_mutex); fs_diferido = on ? 1 : 0; kmutex_unlock(&vfs_mutex); }
int  vfs_modo_ram_activo(void) { return fs_diferido; }
uint32_t vfs_sin_guardar(void)
{
    uint32_t n = 0;
    kmutex_lock(&vfs_mutex);
    for (int i = 0; i < FS_MAX_FILES; ++i) if (files[i].dirty) n++;
    kmutex_unlock(&vfs_mutex);
    return n;
}

static int vfs_create_internal(const char *name, const char *initial_data, uint32_t len)
{
    if (!name || name[0] == '\0') return -1;
    /* SOMA_PARCHE_002_NAMELEN: un nombre que no cabe se aceptaba "con exito"
     * al truncarse, y luego el archivo no se podia leer ni borrar. */
    if (str_len(name) >= FS_NAME_MAX) return -1;
    if (len > FS_FILE_MAX) return -2;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && str_eq(files[i].name, name)) {
            { int e = ensure_cap(i, len + 1, 0); if (e) return e; }
            if (initial_data && len > 0) memcpy(files[i].data, initial_data, len);
            files[i].data[len] = '\0';
            files[i].size = len;
            files[i].dirty = !is_ephemeral_path(name);
            return 0;
        }
    }
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used) {
            { int e = ensure_cap(i, len + 1, 0); if (e) return e; }
            files[i].used = 1; files[i].spilled = 0; files[i].ram_only = 0;
            files[i].dirty = !is_ephemeral_path(name);
            str_copy(files[i].name, name, FS_NAME_MAX);
            files[i].size = 0;
            if (initial_data && len > 0) {
                memcpy(files[i].data, initial_data, len);
                files[i].data[len] = '\0';
                files[i].size = len;
            } else {
                files[i].data[0] = '\0';
            }
            return 0;
        }
    }
    return -1;
}

/* SOMA_PARCHE_002_LIT: la longitud se calcula en compilacion (antes estaba
 * escrita a mano y en varios casos no coincidia con el texto). */
#define VFS_CREATE_LIT(path, lit) vfs_create_internal((path), (lit), (uint32_t)(sizeof(lit) - 1))

static void vfs_format_unlocked(void)
{
    static uint8_t zero_buf[512] __attribute__((aligned(16)));
    memset(zero_buf, 0, sizeof(zero_buf));

    uint32_t fs_total_sectors = 1 + FS_DISK_SLOTS * FS_SECTORS_PER_FILE;
    for (uint32_t s = 0; s < fs_total_sectors; ++s) {
        sysdisk_write(FS_SUPER_LBA + s, zero_buf);
    }
    sysdisk_write(FS_CHECKPOINT_LBA - 1, zero_buf);
    sysdisk_write(FS_JOURNAL_LBA, zero_buf);                /* PARCHE 112: sin diario pendiente */

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].spilled) fs_spill_del(i);
        files[i].used = 0;
        files[i].size = 0;
        files[i].dirty = 0;
        files[i].spilled = 0; files[i].ram_only = 0;
        files[i].name[0] = '\0';
        drop_data(i);
    }

    /* 1. Archivos Base de Sistema */
    VFS_CREATE_LIT("/etc/hostname", "soma-node1\n");
    VFS_CREATE_LIT("/etc/os-release", "NAME=SOMA\nVERSION=0.2-release\nARCH=" MYOS_ARCH_NAME "\nSTORAGE=virtio-blk-v2 (256 archivos, grandes en BIGFS)\nDESCRIPTION=Sistema Operativo Multi-Agente\n");
    VFS_CREATE_LIT("/sys/status.txt", "KERNEL: SOMA " MYOS_ARCH_NAME " | VIRTIO: OK | NETWORK: 10.0.2.15 | STORAGE: PERSISTENT V2\n");

    const char *init_code = "#!/bin/soma\n# SOMA Init Script\nexport MOTD=\"SOMA Bare-Metal Core Activo\"\necho \"$MOTD ($HOST en $ARCH)\"\n";
    vfs_create_internal("/etc/init.sh", init_code, str_len(init_code));
    VFS_CREATE_LIT("/etc/soma_manifest.txt", "Identity: SOMA AI Kernel (Nail-35b)\nRole: Multi-Agent System Core Intelligence\nStatus: Fully Operational (VFS V2 Active)\n");
    VFS_CREATE_LIT("/etc/mem_user.txt", "Usuario: Marcos | Rol: Administrador y Arquitecto Principal del Sistema SOMA | Preferencias: Respuestas tecnicas concisas, directas y veraces.\n");
    VFS_CREATE_LIT("/etc/mem_hw.txt", "CPU: 64-bit Ring 0 | RAM: 512 MiB | IP: 10.0.2.15 | Almacenamiento: virtio-blk (SOMAFS02: 256 archivos, sin limite de 16 KiB)\n");

    /* 2. Directorio de Usuario */
    VFS_CREATE_LIT("/mis_archivos/bienvenida.txt", "Carpeta personal del usuario Marcos.\nEspacio destinado a documentos, proyectos y notas personales.\n");

    /* 3. Espacio de Trabajo para Agentes (/workspace/) */
    
    /* Recursos visuales y multimedia para el navegador web */
    static uint8_t demo_bmp[10240];
    int demo_bmp_sz = image_create_demo_bmp(demo_bmp, sizeof(demo_bmp), 80, 40);
    if (demo_bmp_sz > 0) {
        vfs_create_internal("/workspace/logo_soma.bmp", (const char *)demo_bmp, (uint32_t)demo_bmp_sz);
    }

    const char *demo_html = "<!DOCTYPE html><html><body>"
                                "<h1>SOMA OS - Red Multimedia Activa</h1>"
                                "<p>Demostracion de renderizado HTML con soporte de imagenes locales y de RED.</p>"
                                "<hr>"
                                "<p>1. Imagen BMP cargada desde disco (RamFS local):</p>"
                                "<img src=\"/workspace/logo_soma.bmp\" width=\"120\" height=\"60\">"
                                "<p>2. Imagen BMP descargada nativamente por HTTP (TCP/IP):</p>"
                                "<img src=\"http://127.0.0.1/workspace/logo_soma.bmp\" width=\"120\" height=\"60\">"
                                "<p>El navegador ha asignado un buffer dinamico, forjado la peticion HTTP GET, recibido la respuesta binaria y decodificado la imagen al vuelo.</p>"
                                "<hr>"
                                "<a href=\"http://10.0.2.15/\">Ver Dashboard (Telemetria)</a>"
                                "</body></html>";
    vfs_create_internal("/workspace/demo_web.html", demo_html, (uint32_t)str_len(demo_html));

    VFS_CREATE_LIT("/workspace/README.txt", "Espacio de trabajo preferente para la creacion de archivos y tareas de agentes.\n");
    VFS_CREATE_LIT("/workspace/soma.txt", "[Workspace SOMA] Tareas, diagnosticos y scripts activos.\n");
    VFS_CREATE_LIT("/workspace/buscador.txt", "[Workspace Buscador] Sintesis de investigacion y recopilacion web.\n");

    /* 4. Coordinación y Memoria de Agentes */
    VFS_CREATE_LIT("/agent/soma.txt", "Agente SOMA Core: Especialista en gestion del kernel bare-metal, administracion de procesos, diagnósticos y seguridad.\n");
    VFS_CREATE_LIT("/agent/buscador.txt", "Agente Buscador: Especialista en exploracion web, sintesis de informacion, noticias y busquedas en el arbol de memoria.\n");
    VFS_CREATE_LIT("/agent/blackboard.txt", "[Pizarra Compartida Multi-Agente]\nEstado: Inicializado.\n");

    /* 5. Árbol de Conocimiento Semántico (/mem/) */
    VFS_CREATE_LIT("/mem/indice.txt", "/mem/ (Arbol de Memoria Estructurada)\n+-- ciencia/\n|   +-- ciencia_fisica.txt\n|   +-- ciencia_computacion.txt\n+-- historia/\n|   +-- historia_antigua.txt\n|   +-- historia_moderna.txt\n+-- conversaciones/\n    +-- conversaciones.txt\n");
    VFS_CREATE_LIT("/mem/ciencia_fisica.txt", "Fisica: Principios de mecanica clasica, termodinamica, relatividad y fisica cuantica.\n");
    VFS_CREATE_LIT("/mem/ciencia_computacion.txt", "Computacion: Arquitectura x86_64, paginacion de 4 niveles, VirtIO, pilas de red bare-metal y sistemas multi-agente.\n");
    VFS_CREATE_LIT("/mem/historia_antigua.txt", "Historia Antigua: Civilizaciones de Mesopotamia, Egipto, Grecia clasica y el Imperio Romano.\n");
    VFS_CREATE_LIT("/mem/historia_moderna.txt", "Historia Moderna: Renacimiento, Ilustracion, Revolucion Industrial, Era Digital e Inteligencia Artificial.\n");
    VFS_CREATE_LIT("/mem/conversaciones.txt", "Registro historico de temas clave tratados entre el usuario y los agentes del sistema.\n");
    VFS_CREATE_LIT("/src/kernel.c", "/* SOMA Kernel Entry */\nvoid kernel_main(void) {\n  virtio_blk_init();\n  vfs_init();\n  shell_run();\n}\n");

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) files[i].dirty = 1;
    }
    vfs_sync_unlocked();
}


static void vfs_ensure_demo_files(void)
{
    static const char demo_html[] =
        "<!DOCTYPE html><html><body>"
        "<h1 style=\"color: #38bdf8;\">SOMA OS - Motor Visual HTML & CSS</h1>"
        "<p style=\"color: #94a3b8;\">Renderizado grafico bare-metal acelerado en Ring 0</p>"
        "<hr><br>"
        "<p style=\"background-color: #1e293b; color: #f8fafc;\">1. Logotipo Oficial SOMA (BMP 24bpp TrueColor):</p>"
        "<img src=\"/workspace/logo_soma.bmp\" width=\"160\" height=\"80\">"
        "<br>"
        "<p style=\"background-color: #0f172a; color: #34d399;\">2. Microchip de Control (Grafico Bare-Metal):</p>"
        "<img src=\"/workspace/chip_soma.bmp\" width=\"128\" height=\"64\">"
        "<br><hr>"
        "<a href=\"http://10.0.2.15/\" style=\"background-color: #0284c7; color: #ffffff;\">[ VER TELEMETRIA DEL SISTEMA ]</a>"
        "</body></html>";

    /* AGENTE_MAYORDOMO_PATCH_V1: perfiles persistentes sin borrar la STM previa. */
    char mayordomo_profile_probe[2];
    if (vfs_read("/agent/mayordomo.txt", mayordomo_profile_probe, sizeof(mayordomo_profile_probe)) < 0) {
        static const char mayordomo_profile[] = "Mayordomo: operaciones nativas y respuestas directas para RAM, disco, VFS y archivos.\n";
        vfs_write("/agent/mayordomo.txt", mayordomo_profile, sizeof(mayordomo_profile) - 1);
    }
    if (vfs_read("/agent/mayordomo_stm.txt", mayordomo_profile_probe, sizeof(mayordomo_profile_probe)) < 0) {
        vfs_write("/agent/mayordomo_stm.txt", "", 0);
    }

    vfs_write("/workspace/demo_web.html", demo_html, sizeof(demo_html) - 1);

    static uint8_t bmp1[10240];
    int sz1 = image_create_demo_bmp(bmp1, sizeof(bmp1), 80, 40);
    if (sz1 > 0) {
        vfs_write("/workspace/logo_soma.bmp", (const char *)bmp1, (uint32_t)sz1);
    }

    static uint8_t bmp2[10240];
    int sz2 = image_create_chip_bmp(bmp2, sizeof(bmp2), 64, 32);
    if (sz2 > 0) {
        vfs_write("/workspace/chip_soma.bmp", (const char *)bmp2, (uint32_t)sz2);
    }
}

void vfs_init(void)
{
    if (fs_initialized) return;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].dirty = 0;
        files[i].name[0] = '\0';
        files[i].data = 0; files[i].cap = 0; files[i].spilled = 0; files[i].ram_only = 0;
    }

    fs_initialized = 1;
    kmutex_init(&vfs_mutex, "vfs_lock");

    if (sysdisk_capacity() > 0) {                              /* PARCHE 120: una restauracion de checkpoint interrumpida se completa ANTES de leer nada */
        struct ckpt_slot ca = fs_ckpt_a(), cb = fs_ckpt_b();
        int cr = ckpt_recover(&fs_dev, &ca, &cb, FS_SUPER_LBA, FS_TOTAL_SECTORS, SOMA_LBA_CKPT_INTENT, FS_JOURNAL_LBA);
        if (cr == 1) kprint("VFS: se completo una restauracion de checkpoint interrumpida\n");
        else if (cr == 2) kprint("VFS: marca de restauracion sin checkpoint valido (descartada)\n");
    }
    struct fs_superblock sb;
    if (sysdisk_read(FS_SUPER_LBA, &sb) == 0 &&
        memcmp(sb.magic, "SOMAFS02", 8) == 0 &&
        sb.version == 2 &&
        sb.max_files == FS_DISK_SLOTS &&
        sb.num_files <= FS_DISK_SLOTS) {
        kprint("VFS: Sistema de archivos persistente detectado (SOMAFS02: 256 archivos, sin limite de 16 KiB; los grandes en BIGFS)\n");
        {   /* PARCHE 112: rehacer o descartar una escritura interrumpida por un corte, ANTES de leer ningun archivo */
            int jr = slotio_recover(&fs_dev, FS_JOURNAL_LBA, FS_DATA_LBA, FS_SECTORS_PER_FILE, FS_DISK_SLOTS);
            if (jr == 1) { fs_jreplayed++; kprint("VFS: diario: se completo una escritura interrumpida (archivo recuperado)\n"); }
            else if (jr == 2) { fs_jdiscarded++; kprint("VFS: diario: se descarto una escritura sin terminar (el archivo conserva su contenido anterior)\n"); }
        }
        int loaded = 0;
        for (int i = 0; i < FS_DISK_SLOTS; ++i) {
            int r = load_file_from_disk(i);
            if (r == 0 && files[i].used) loaded++;
        }
        if (fs_spill_ready())                               /* PARCHE 087: archivos mas alla de las 64 ranuras */
            for (int i = FS_DISK_SLOTS; i < FS_MAX_FILES; ++i) if (load_spill_slot(i) == 0) loaded++;
        kprint("VFS: "); kprint_dec((uint32_t)loaded); kprint(" archivos cargados desde virtio-blk (KHeap dinamico).\n");
        kprint("VFS: Directorios '/workspace/' y '/mis_archivos/' activos.\n");

        char chk_init[16];
        if (vfs_read("/etc/init.sh", chk_init, sizeof(chk_init)) <= 0) {
            const char *init_code = "#!/bin/soma\n# SOMA Init Script\nexport MOTD=\"SOMA Bare-Metal Core Activo\"\necho \"$MOTD ($HOST en $ARCH)\"\n";
            vfs_write("/etc/init.sh", init_code, str_len(init_code));
        }
        vfs_ensure_demo_files();
        return;
    }

    kprint("VFS: Formateando SOMAFS02 (256 archivos; los grandes van a BIGFS)...\n");
    vfs_format();
    vfs_ensure_demo_files();
}

static int vfs_create_unlocked(const char *name, const char *initial_data, uint32_t len)
{
    if (!fs_initialized) vfs_init();
    int r = vfs_create_internal(name, initial_data, len);
    if (r == 0 && !is_ephemeral_path(name) && vfs_sync_auto_unlocked() != 0) {
        return -1;
    }
    return r;
}

/* SOMA_PARCHE_LIVE_V1: hook opcional (lo registra el GUI). */
void (*vfs_live_hook)(const char *name, const char *data, uint32_t len) = 0;

/* SOMA_267_DISCOS: las rutas /usuario/... y /modelos/... no son de RamFS: van a las unidades exFAT del pendrive (src/core/unidades.c).
 * Asi cat, write, rm, cp, mv, head, ls, nano... funcionan alli sin cambiar cada orden. /modelos es de solo lectura desde aqui. */
static int unid_es(const char *name) { return name && unid_ruta(name, 0) >= 0; }
static void ul_put(char *o, uint32_t max, uint32_t *p, const char *s) { while (s && *s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p] = 0; }
/* lista una carpeta de una unidad: "Archivos: /usuario/Documentos/, /usuario/a.txt" (o por la consola) */
static int unid_lista(int show_all, const char *dir, char *out, uint32_t max, int consola)
{
    struct exfat_dirent *d = (struct exfat_dirent *)kmalloc(64 * sizeof *d);    /* por llamada: dos listados a la vez no se pisan */
    int total = 0, n = d ? unid_listar(dir, d, 64, &total) : EXFAT_E_NOMEM;
    uint32_t p = 0, dl = str_len(dir);
    if (out && max) { out[0] = 0; ul_put(out, max, &p, "Archivos: "); }
    if (consola) { kprint("\nARCHIVOS EN LA UNIDAD "); kprint(dir); kprint(":\n--------------------------------------------------\n"); }
    if (n < 0) {
        if (consola) { kprint("  "); kprint(unid_activa(unid_ruta(dir, 0)) ? exfat_err_str(n) : "la unidad no esta en este pendrive"); kprint("\n"); }
        if (d) kfree(d);
        return out ? (int)p : 0;
    }
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (d[i].name[0] == '.' && !show_all) continue;
        if (out && max) {
            if (k) ul_put(out, max, &p, ", ");
            ul_put(out, max, &p, dir); if (dl && dir[dl - 1] != '/') ul_put(out, max, &p, "/");
            ul_put(out, max, &p, d[i].name); if (d[i].is_dir) ul_put(out, max, &p, "/");
        }
        if (consola) {
            kprint("  "); kprint(d[i].name); kprint(d[i].is_dir ? "/" : "");
            if (!d[i].is_dir) { for (uint32_t s2 = str_len(d[i].name); s2 < 36; ++s2) kputc(' '); kprint_dec((uint32_t)(d[i].size > 0xFFFFFFFFu ? 0xFFFFFFFFu : d[i].size)); kprint(" bytes"); }
            kprint("\n");
        }
        k++;
    }
    if (consola) {
        kprint("--------------------------------------------------\n"); kprint_dec((uint32_t)k); kprint(" elementos");
        if (total > n) { kprint(" (se muestran "); kprint_dec((uint32_t)n); kprint(" de "); kprint_dec((uint32_t)total); kprint(")"); }
        kprint(unid_ruta(dir, 0) == UNID_MODELOS ? "  [MODELOS: solo lectura]\n\n" : "\n\n");
    }
    kfree(d);
    return out ? (int)p : k;
}

/* SOMA_268_ACCESO: carpetas de la unidad del usuario para el gestor de archivos */
int vfs_unid_mkdir(const char *name) { return unid_es(name) && unid_mkdir(name, 0) == 0 ? 0 : -1; }
int vfs_unid_rename(const char *de, const char *a) { return unid_es(de) && unid_es(a) && unid_renombrar(de, a, 0) == 0 ? 0 : -1; }
int vfs_unid_borrar_rec(const char *name) { return unid_es(name) && unid_borrar_rec(name, 0) == 0 ? 0 : -1; }

static int vfs_write_core(const char *name, const char *data, uint32_t len);

int vfs_write(const char *name, const char *data, uint32_t len)
{
    int r = vfs_write_core(name, data, len);
    if (r >= 0 && vfs_live_hook) vfs_live_hook(name, data, len);
    return r;
}

static int vfs_write_core(const char *name, const char *data, uint32_t len)
{
    if (!fs_initialized) vfs_init();
    if (!name) return -1;
    if (is_proc_path(name)) return -1;
    if (agent_path_denied(name)) return -1;
    if (str_eq(name, "/dev/null") || str_eq(name, "/dev/zero")) return (int)len;
    if (unid_es(name)) return unid_escribir(name, data, len, 0) == 0 ? (int)len : -1;     /* SOMA_267_DISCOS */

    kmutex_lock(&vfs_mutex);
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            if (len > FS_FILE_MAX) { kmutex_unlock(&vfs_mutex); return -2; }
            { int e = ensure_cap(i, len + 1, 0); if (e) { kmutex_unlock(&vfs_mutex); return e; } }
            if (data && len > 0) memcpy(files[i].data, data, len);
            files[i].data[len] = '\0';
            files[i].size = len;
            if (!is_ephemeral_path(name)) {
                files[i].dirty = 1;
                if (vfs_sync_auto_unlocked() != 0) { kmutex_unlock(&vfs_mutex); return -1; }
            }
            kmutex_unlock(&vfs_mutex);
            return (int)len;
        }
    }
    int r = vfs_create_unlocked(name, data, len);
    kmutex_unlock(&vfs_mutex);
    return r == 0 ? (int)len : -1;
}

static int vfs_read_unlocked(const char *name, char *buf_out, uint32_t max_len)
{
    if (!fs_initialized) vfs_init();
    if (!name || !buf_out || max_len == 0) return -1;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            if (!files[i].data) return -1;
            uint32_t to_copy = files[i].size;
            if (to_copy >= max_len) to_copy = max_len - 1;
            memcpy(buf_out, files[i].data, to_copy);
            buf_out[to_copy] = '\0';
            return (int)to_copy;
        }
    }
    return -1;
}

int vfs_read(const char *name, char *buf_out, uint32_t max_len)
{
    if (!fs_initialized) vfs_init();
    if (!name || !buf_out || max_len == 0) return -1;

    if (is_proc_path(name)) return read_proc_file(name, buf_out, max_len);
    if (is_dev_path(name))  return read_dev_file(name, buf_out, max_len);
    if (vfs_agent_secret(name)) { buf_out[0] = '\0'; return -1; }          /* SOMA_193 */
    if (unid_es(name)) {                                                    /* SOMA_267_DISCOS */
        int r = unid_leer(name, 0, buf_out, max_len - 1);
        if (r < 0) { buf_out[0] = '\0'; return -1; }
        buf_out[r] = '\0';
        return r;
    }

    kmutex_lock(&vfs_mutex);
    int r = vfs_read_unlocked(name, buf_out, max_len);
    kmutex_unlock(&vfs_mutex);
    return r;
}

int vfs_delete(const char *name)
{
    if (unid_es(name)) return agent_path_denied(name) || unid_borrar(name, 0) ? -1 : 0;   /* SOMA_267_DISCOS */
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();
    if (is_proc_path(name) || is_dev_path(name)) { kmutex_unlock(&vfs_mutex); return -1; }
    if (agent_path_denied(name)) { kmutex_unlock(&vfs_mutex); return -1; }

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            files[i].used = 0;
            files[i].size = 0;
            files[i].name[0] = '\0';
            files[i].dirty = 1;
            drop_data(i);
            int r = vfs_sync_auto_unlocked();
            kmutex_unlock(&vfs_mutex);
            return r != 0 ? -1 : 0;
        }
    }
    kmutex_unlock(&vfs_mutex);
    return -1;
}

void vfs_list(void)
{
    vfs_list_ext(0, 0);
}

void vfs_list_ext(int show_all, const char *filter_dir)
{
    if (unid_es(filter_dir)) { (void)unid_lista(show_all, filter_dir, 0, 0, 1); return; }   /* SOMA_267_DISCOS */
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    kprint("\nARCHIVOS EN RamFS (");
    if (filter_dir && *filter_dir) {
        kprint(filter_dir);
    } else {
        kprint("/");
    }
    if (show_all) kprint(" - Modo Todo/Sistema");
    kprint("):\n--------------------------------------------------\n");

    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            const char *fn = files[i].name;

            if (filter_dir && *filter_dir && !str_eq(filter_dir, "/")) {
                if (fs_strstr(fn, filter_dir) != fn) continue;
            }

            int hidden = vfs_is_hidden(fn);
            if (hidden && !show_all) continue;

            kprint("  ");
            kprint(fn);
            if (hidden) kprint(" [OCULTO]");
            uint32_t nl = str_len(fn) + (hidden ? 9 : 0);
            for (uint32_t s = nl; s < 36; ++s) kputc(' ');
            kprint_dec(files[i].size); kprint(" bytes\n");
            count++;
        }
    }

    if (show_all) {
        kprint("\nPSEUDO-FILESYSTEM (Memoria Virtual):\n");
        kprint("  /proc/version, /proc/uptime, /proc/meminfo, /proc/threads\n");
        kprint("  /dev/null, /dev/zero, /dev/urandom, /dev/rtc\n");
    }
    kprint("--------------------------------------------------\n");
    kprint("Total: "); kprint_dec((uint32_t)count); kprint(" archivos visibles (Capacidad: 256 archivos, sin limite de 16 KiB).\n\n");
    kmutex_unlock(&vfs_mutex);
}

int vfs_format_list(char *out_buf, uint32_t max)
{
    return vfs_format_list_ext(0, 0, out_buf, max);
}

int vfs_format_list_ext(int show_all, const char *filter_dir, char *out_buf, uint32_t max)
{
    if (unid_es(filter_dir)) return unid_lista(show_all, filter_dir, out_buf, max, 0);    /* SOMA_267_DISCOS */
    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();
    uint32_t pos = 0;
    const char *hdr = "Archivos: ";
    while (*hdr && pos < max - 1) out_buf[pos++] = *hdr++;

    int count = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            const char *n = files[i].name;
            if (filter_dir && *filter_dir && !str_eq(filter_dir, "/")) {
                if (fs_strstr(n, filter_dir) != n) continue;
            }
            if (vfs_is_hidden(n) && !show_all) continue;

            if (count > 0 && pos < max - 2) { out_buf[pos++] = ','; out_buf[pos++] = ' '; }
            while (*n && pos < max - 1) out_buf[pos++] = *n++;
            count++;
        }
    }
    out_buf[pos] = '\0';
    kmutex_unlock(&vfs_mutex);
    return (int)pos;
}

void vfs_get_stats(uint32_t *files_used, uint32_t *bytes_used, uint32_t *dirty_count)
{
    kmutex_lock(&vfs_mutex);
    uint32_t f = 0, b = 0, d = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used) {
            f++;
            b += files[i].size;
            if (files[i].dirty) d++;
        }
    }
    if (files_used) *files_used = f;
    if (bytes_used) *bytes_used = b;
    if (dirty_count) *dirty_count = d;
    kmutex_unlock(&vfs_mutex);
}

/* ====================================================================
 * MOTOR DE BÚSQUEDA SEMÁNTICA Y PONDERADA BM25 / TF-IDF BARE-METAL
 * ==================================================================== */

static char normalize_char_utf8(const char **p)
{
    unsigned char c = (unsigned char)**p;
    if (c == 0) return 0;
    (*p)++;

    if (c == 0xC3 && **p) {
        unsigned char c2 = (unsigned char)**p;
        (*p)++;
        switch (c2) {
            case 0xA1: case 0x81: return 'a'; /* á, Á */
            case 0xA9: case 0x89: return 'e'; /* é, É */
            case 0xAD: case 0x8D: return 'i'; /* í, Í */
            case 0xB3: case 0x93: return 'o'; /* ó, Ó */
            case 0xBA: case 0x9A: return 'u'; /* ú, Ú */
            case 0xBC: case 0x9C: return 'u'; /* ü, Ü */
            case 0xB1: case 0x91: return 'n'; /* ñ, Ñ */
            default: return '?';
        }
    }

    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return (char)c;
    return ' ';
}

static int is_bm25_stopword(const char *w, uint32_t len)
{
    static const char *const stops[] = {
        "el", "la", "los", "las", "un", "una", "unos", "unas", "de", "del",
        "en", "con", "por", "para", "que", "se", "su", "sus", "es", "son",
        "fue", "era", "ha", "han", "este", "esta", "estos", "estas", "como",
        "cuando", "donde", "quien", "cual", "cuales", "sobre", "entre", "sin",
        "the", "of", "and", "in", "to", "for", "is", "on", "that", "by", "with", 0
    };

    if (len < 2) return 1;
    for (int i = 0; stops[i]; ++i) {
        uint32_t slen = str_len(stops[i]);
        if (slen == len) {
            int match = 1;
            for (uint32_t k = 0; k < len; ++k) {
                if (w[k] != stops[i][k]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

#define MAX_BM25_TERMS 8
#define MAX_TERM_LEN   32

struct bm25_query {
    char terms[MAX_BM25_TERMS][MAX_TERM_LEN];
    uint32_t term_lens[MAX_BM25_TERMS];
    uint32_t term_count;
};

static void parse_bm25_query(const char *query, struct bm25_query *q)
{
    q->term_count = 0;
    const char *p = query;

    while (*p && q->term_count < MAX_BM25_TERMS) {
        while (*p && (unsigned char)*p <= 32) p++;
        if (!*p) break;

        char term[MAX_TERM_LEN];
        uint32_t tlen = 0;

        while (*p && tlen < MAX_TERM_LEN - 1) {
            const char *prev = p;
            char nc = normalize_char_utf8(&p);
            if (nc == ' ' || nc == 0) {
                if (nc == 0) p = prev + 1;
                break;
            }
            term[tlen++] = nc;
        }
        term[tlen] = '\0';

        if (tlen >= 2 && !is_bm25_stopword(term, tlen)) {
            memcpy(q->terms[q->term_count], term, tlen + 1);
            q->term_lens[q->term_count] = tlen;
            q->term_count++;
        }
    }
}

struct bm25_match {
    int file_idx;
    uint32_t score;
    uint32_t best_snippet_offset;
};

int vfs_search_bm25(const char *query, const char *path_prefix, char *out, uint32_t max)
{
    if (!query || !*query || !out || max == 0) return 0;
    out[0] = '\0';

    struct bm25_query q;
    parse_bm25_query(query, &q);
    if (q.term_count == 0) {
        uint32_t p = 0;
        fb_puts(out, max, &p, "bm25: consulta demasiado generica o sin terminos significativos.\n");
        return 0;
    }

    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    struct bm25_match matches[FS_MAX_FILES];
    uint32_t match_count = 0;

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used || !files[i].data) continue;
        const char *fn = files[i].name;

        if (path_prefix && *path_prefix) {
            if (fs_strstr(fn, path_prefix) != fn) continue;
        }

        if (vfs_is_hidden(fn) && (!path_prefix || !*path_prefix)) continue;
        if (vfs_agent_secret(fn)) continue;                                  /* SOMA_193: las busquedas de un agente no ven los secretos */

        uint32_t score = 0;
        uint32_t unique_hits = 0;
        uint32_t best_off = 0;
        uint32_t max_para_hits = 0;

        const char *data = files[i].data;
        uint32_t dlen = files[i].size;
        if (dlen == 0) continue;

        for (uint32_t t = 0; t < q.term_count; ++t) {
            const char *term = q.terms[t];
            const char *fp = fn;
            char fn_norm[64]; uint32_t fnp = 0;
            while (*fp && fnp < sizeof(fn_norm) - 1) {
                fn_norm[fnp++] = normalize_char_utf8(&fp);
            }
            fn_norm[fnp] = '\0';

            if (fs_strstr(fn_norm, term) != 0) {
                score += 30;
                unique_hits++;
            }
        }

        uint32_t off = 0;
        while (off < dlen) {
            uint32_t para_start = off;
            while (off < dlen && data[off] != '\n') off++;
            uint32_t para_len = off - para_start;
            if (off < dlen && data[off] == '\n') off++;

            char para_norm[512];
            uint32_t pnp = 0;
            const char *pp = data + para_start;
            const char *pend = pp + (para_len < 400 ? para_len : 400);

            while (pp < pend && pnp < sizeof(para_norm) - 1) {
                para_norm[pnp++] = normalize_char_utf8(&pp);
            }
            para_norm[pnp] = '\0';

            uint32_t para_hits = 0;
            for (uint32_t t = 0; t < q.term_count; ++t) {
                const char *term = q.terms[t];
                const char *pos = para_norm;
                int term_hit_in_para = 0;

                while ((pos = fs_strstr(pos, term)) != 0) {
                    para_hits++;
                    term_hit_in_para++;
                    pos += q.term_lens[t];
                    score += 8;
                }
                if (term_hit_in_para) unique_hits++;
            }

            if (para_hits > max_para_hits) {
                max_para_hits = para_hits;
                best_off = para_start;
            }
        }

        score += (unique_hits * 25);
        if (fs_strstr(fn, "/workspace/") == fn) score += 10;
        if (fs_strstr(fn, "/mem/") == fn) score += 15;

        if (score > 15) {
            matches[match_count].file_idx = i;
            matches[match_count].score = score;
            matches[match_count].best_snippet_offset = best_off;
            match_count++;
        }
    }

    kmutex_unlock(&vfs_mutex);

    if (match_count == 0) {
        uint32_t p = 0;
        fb_puts(out, max, &p, "bm25: sin coincidencias relevantes en memoria.\n");
        return 0;
    }

    for (uint32_t a = 0; a < match_count; ++a) {
        for (uint32_t b = a + 1; b < match_count; ++b) {
            if (matches[b].score > matches[a].score) {
                struct bm25_match tmp = matches[a];
                matches[a] = matches[b];
                matches[b] = tmp;
            }
        }
    }

    uint32_t pos = 0;
    uint32_t top = match_count < 3 ? match_count : 3;

    for (uint32_t r = 0; r < top; ++r) {
        int fi = matches[r].file_idx;
        const char *fn = files[fi].name;
        const char *data = files[fi].data;
        uint32_t size = files[fi].size;
        uint32_t snip_off = matches[r].best_snippet_offset;

        fb_puts(out, max, &pos, "[");
        fb_put_dec(out, max, &pos, r + 1);
        fb_puts(out, max, &pos, "] ");
        fb_puts(out, max, &pos, fn);
        fb_puts(out, max, &pos, " (Score: ");
        fb_put_dec(out, max, &pos, matches[r].score);
        fb_puts(out, max, &pos, ")\n    ");

        uint32_t snip_len = 0;
        while (snip_off + snip_len < size && snip_len < 220) {
            char c = data[snip_off + snip_len];
            if (c == '\r') { snip_len++; continue; }
            if (c == '\n') {
                if (snip_len > 80) break;
                c = ' ';
            }
            if (pos + 1 < max) out[pos++] = c;
            snip_len++;
        }
        if (snip_off + snip_len < size) {
            fb_puts(out, max, &pos, "...");
        }
        fb_puts(out, max, &pos, "\n\n");
    }

    return (int)top;
}

int vfs_grep_all(const char *pat, int ignore_case, int list_only,
                 char *out, uint32_t max)
{
    if (!pat || !*pat) return -1;
    if (max && out) out[0] = '\0';

    uint32_t pos = 0;
    int files_hit = 0;

    kmutex_lock(&vfs_mutex);
    if (!fs_initialized) vfs_init();

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used || !files[i].data) continue;
        if (vfs_agent_secret(files[i].name)) continue;                      /* SOMA_193 */

        const char *data = files[i].data;
        uint32_t size = files[i].size;
        uint32_t line_no = 1;
        uint32_t off = 0;
        int file_matched = 0;

        while (off < size) {
            uint32_t line_start = off;
            while (off < size && data[off] != '\n') off++;
            uint32_t line_len = off - line_start;
            if (off < size && data[off] == '\n') off++;

            char line[256];
            uint32_t cl = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
            for (uint32_t k = 0; k < cl; ++k) line[k] = data[line_start + k];
            line[cl] = '\0';

            const char *h = line;
            const char *n = pat;
            int match = 0;
            while (*h) {
                const char *hh = h;
                const char *nn = n;
                while (*hh && *nn) {
                    char ch = ignore_case && (*hh >= 'A' && *hh <= 'Z') ? (char)(*hh + 32) : *hh;
                    char cn = ignore_case && (*nn >= 'A' && *nn <= 'Z') ? (char)(*nn + 32) : *nn;
                    if (ch != cn) break;
                    hh++; nn++;
                }
                if (!*nn) { match = 1; break; }
                h++;
            }

            if (match) {
                if (!file_matched) {
                    files_hit++;
                    file_matched = 1;
                }
                if (list_only) break;

                if (out && max > 8) {
                    const char *nm = files[i].name;
                    while (*nm && pos + 1 < max) out[pos++] = *nm++;
                    if (pos + 1 < max) out[pos++] = ':';
                    char nb[12]; int nn = 0; uint32_t v = line_no;
                    if (v == 0) nb[nn++] = '0';
                    else { while (v) { nb[nn++] = (char)('0' + v % 10); v /= 10; } }
                    while (nn && pos + 1 < max) out[pos++] = nb[--nn];
                    if (pos + 1 < max) out[pos++] = ':';
                    if (pos + 1 < max) out[pos++] = ' ';
                    for (uint32_t k = 0; k < cl && pos + 1 < max; ++k) out[pos++] = line[k];
                    if (pos + 1 < max) out[pos++] = '\n';
                    out[pos] = '\0';
                }
            }
            line_no++;
        }

        if (list_only && file_matched && out && max > 2) {
            const char *nm = files[i].name;
            while (*nm && pos + 1 < max) out[pos++] = *nm++;
            if (pos + 1 < max) out[pos++] = '\n';
            out[pos] = '\0';
        }
    }

    kmutex_unlock(&vfs_mutex);

    if (files_hit == 0 && out && max > 24) {
        const char *msg = "grep: sin coincidencias en RamFS.\n";
        pos = 0;
        while (*msg && pos + 1 < max) out[pos++] = *msg++;
        out[pos] = '\0';
    }
    return files_hit;
}

void vfs_tree(char *out_buf, uint32_t max)
{
    vfs_tree_ext(0, out_buf, max);
}

void vfs_tree_ext(int show_all, char *out_buf, uint32_t max)
{
    kmutex_lock(&vfs_mutex);
    uint32_t pos = 0;
    const char *hdr = show_all
        ? "/ (SOMAFS02 - 256 archivos - Arbol Completo)\n+-- dev/\n|   +-- null\n|   +-- zero\n|   +-- urandom\n|   +-- rtc\n+-- proc/\n|   +-- version\n|   +-- uptime\n|   +-- meminfo\n|   +-- cpuinfo\n|   +-- threads\n|   +-- net/\n|       +-- dev\n|       +-- arp\n"
        : "/ (SOMAFS02 - Archivos Visibles)\n";

    kprint(hdr);
    fb_puts(out_buf, max, &pos, hdr);

    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (!files[i].used) continue;
        const char *fn = files[i].name;
        if (vfs_is_hidden(fn) && !show_all) continue;

        if (fn[0] == '/') fn++;
        kprint("+-- "); kprint(fn); kprint("\n");
        fb_puts(out_buf, max, &pos, "+-- ");
        fb_puts(out_buf, max, &pos, fn);
        fb_puts(out_buf, max, &pos, "\n");
    }
    out_buf[pos] = '\0';
    kmutex_unlock(&vfs_mutex);
}

int vfs_sync(void)
{
    kmutex_lock(&vfs_mutex);
    int r = vfs_sync_unlocked();
    kmutex_unlock(&vfs_mutex);
    return r;
}

void vfs_format(void)
{
    kmutex_lock(&vfs_mutex);
    vfs_format_unlocked();
    kmutex_unlock(&vfs_mutex);
}

int vfs_create(const char *name, const char *initial_data)
{
    kmutex_lock(&vfs_mutex);
    uint32_t len = str_len(initial_data);
    int r = vfs_create_unlocked(name, initial_data, len);
    kmutex_unlock(&vfs_mutex);
    return r;
}

/* PARCHE 120: checkpoint con CRC, barreras y restauracion reanudable (src/fs/ckptio.h) */
int vfs_checkpoint_save(void)
{
    if (!fs_initialized) vfs_init();
    kmutex_lock(&vfs_mutex);
    int rc = vfs_sync_unlocked();                                  /* antes se ignoraba el resultado */
    if (rc == 0) { struct ckpt_slot a = fs_ckpt_a(), b = fs_ckpt_b(); rc = ckpt_save(&fs_dev, FS_SUPER_LBA, &a, &b, FS_TOTAL_SECTORS); }
    kmutex_unlock(&vfs_mutex);
    return rc == 0 ? 0 : -1;
}

int vfs_checkpoint_restore(void)
{
    if (!fs_initialized) vfs_init();
    kmutex_lock(&vfs_mutex);
    struct ckpt_slot a = fs_ckpt_a(), b = fs_ckpt_b();
    int rc = ckpt_restore(&fs_dev, &a, &b, FS_SUPER_LBA, FS_TOTAL_SECTORS, SOMA_LBA_CKPT_INTENT, FS_JOURNAL_LBA);
    if (rc == 0) for (int i = 0; i < FS_DISK_SLOTS; ++i) load_file_from_disk(i);
    kmutex_unlock(&vfs_mutex);
    return rc == 0 ? 0 : -1;
}

/* ================================================================== PARCHE 087: archivos de cualquier tamano */
int vfs_size(const char *name)
{
    if (!fs_initialized) vfs_init();
    if (!name) return -1;
    if (is_proc_path(name) || is_dev_path(name)) { static char t[FS_DATA_MAX]; int r = vfs_read(name, t, sizeof t); return r; }
    if (unid_es(name)) {                                                    /* SOMA_267_DISCOS */
        uint64_t t = 0; int d = 0;
        return unid_tam(name, &t, &d) || d || t > 0x7FFFFFFFu ? -1 : (int)t;
    }
    kmutex_lock(&vfs_mutex);
    int r = -1;
    for (int i = 0; i < FS_MAX_FILES; ++i) if (files[i].used && vfs_names_equal(files[i].name, name)) { r = (int)files[i].size; break; }
    kmutex_unlock(&vfs_mutex);
    return r;
}
char *vfs_read_alloc(const char *name, uint32_t *len_out)
{
    if (len_out) *len_out = 0;
    if (!fs_initialized) vfs_init();
    if (!name) return 0;
    if (is_proc_path(name) || is_dev_path(name)) {
        char *b = (char *)kmalloc(FS_DATA_MAX + 1); if (!b) return 0;
        int r = vfs_read(name, b, FS_DATA_MAX + 1); if (r < 0) { kfree(b); return 0; }
        if (len_out) *len_out = (uint32_t)r;
        return b;
    }
    if (vfs_agent_secret(name)) return 0;                                    /* SOMA_193 */
    if (unid_es(name)) {                                                    /* SOMA_267_DISCOS: como mucho FS_FILE_MAX */
        uint64_t t = 0; int d = 0;
        if (unid_tam(name, &t, &d) || d || t > FS_FILE_MAX) return 0;
        char *u = (char *)kmalloc((size_t)t + 1);
        if (!u) return 0;
        if (unid_leer(name, 0, u, (uint32_t)t) != (int)t) { kfree(u); return 0; }
        u[t] = 0; if (len_out) *len_out = (uint32_t)t;
        return u;
    }
    kmutex_lock(&vfs_mutex);
    char *b = 0;
    for (int i = 0; i < FS_MAX_FILES; ++i) {
        if (files[i].used && vfs_names_equal(files[i].name, name)) {
            if (!files[i].data && files[i].size) break;
            b = (char *)kmalloc((size_t)files[i].size + 1);
            if (b) { if (files[i].size) memcpy(b, files[i].data, files[i].size); b[files[i].size] = 0; if (len_out) *len_out = files[i].size; }
            break;
        }
    }
    kmutex_unlock(&vfs_mutex);
    return b;
}
int vfs_big_persist(void)
{
    if (sysdisk_capacity() == 0) return -1;
    return fs_spill_ready() ? 1 : 0;
}

/* ================================================================== PARCHE 112: autotest de cortes y comprobacion de disco */
static void ct_fill(char *b, uint32_t n, uint32_t seed) { for (uint32_t k = 0; k < n; ++k) b[k] = (char)('a' + (k * 7 + seed * 13 + (k >> 6)) % 26); }

/* Simula cortes de energia en cada escritura posible de un guardado y comprueba que el archivo queda ENTERO viejo o ENTERO nuevo. */
/* PARCHE 120: autoprueba REAL del checkpoint sobre el disco: guardar, modificar, restaurar; y un checkpoint corrupto se rechaza sin tocar el VFS vivo. */
int vfs_checkpoint_selftest(void)
{
    if (!fs_initialized) vfs_init();
    if (sysdisk_capacity() == 0) { kprint("[CKPT] sin disco virtio: nada que probar\n"); return 1; }
    const char *path = "/workspace/_ckpt.tmp"; int bad = 0; static char rb[64];
    vfs_write(path, "version-1", 9);
    if (vfs_checkpoint_save() != 0) { kprint("[CKPT] FALLO: guardar\n"); return 0; }
    vfs_write(path, "version-2-distinta", 18);
    if (vfs_checkpoint_restore() != 0) { kprint("[CKPT] FALLO: restaurar\n"); bad++; }
    memset(rb, 0, sizeof rb); int n = vfs_read(path, rb, sizeof rb - 1);
    if (n != 9 || memcmp(rb, "version-1", 9) != 0) { kprint("[CKPT] FALLO: tras restaurar el archivo no vuelve a version-1\n"); bad++; }
    /* corrupcion: se estropea un byte de las DOS ranuras, el VFS vivo no debe tocarse (PARCHE 130: con una sola ranura corrupta se usaria la otra) */
    vfs_write(path, "version-3", 9);
    static uint8_t sec[512] __attribute__((aligned(16)));
    struct ckpt_slot s0a = fs_ckpt_a(), s0b = fs_ckpt_b();
    uint64_t cl = ckpt_lba(&s0a, 40), cl_b = ckpt_lba(&s0b, 40);
    static uint8_t sec_b[512] __attribute__((aligned(16)));
    if (sysdisk_read(cl, sec) == 0 && sysdisk_read(cl_b, sec_b) == 0) {
        uint8_t keep = sec[7], keep_b = sec_b[7]; sec[7] ^= 0x55; sec_b[7] ^= 0x55;
        sysdisk_write(cl, sec); sysdisk_write(cl_b, sec_b); sysdisk_flush();
        int r = vfs_checkpoint_restore();
        memset(rb, 0, sizeof rb); n = vfs_read(path, rb, sizeof rb - 1);
        if (r == 0) { kprint("[CKPT] FALLO: restauro un checkpoint corrupto\n"); bad++; }
        if (n != 9 || memcmp(rb, "version-3", 9) != 0) { kprint("[CKPT] FALLO: un checkpoint corrupto altero el VFS vivo\n"); bad++; }
        sec[7] = keep; sec_b[7] = keep_b; sysdisk_write(cl, sec); sysdisk_write(cl_b, sec_b); sysdisk_flush();
        if (vfs_checkpoint_restore() != 0) { kprint("[CKPT] FALLO: con la copia reparada no restaura\n"); bad++; }
    } else bad++;
    {   /* PARCHE 130: dos generaciones. Se guardan "gen-A" y "gen-B" (van a ranuras distintas); si la mas reciente se corrompe, se restaura la anterior */
        struct ckpt_slot sa = fs_ckpt_a(), sb = fs_ckpt_b(); uint32_t qa = 0, qb = 0;
        vfs_write(path, "gen-A", 5); if (vfs_checkpoint_save() != 0) { kprint("[CKPT] FALLO: guardar gen-A\n"); bad++; }
        vfs_write(path, "gen-B", 5); if (vfs_checkpoint_save() != 0) { kprint("[CKPT] FALLO: guardar gen-B\n"); bad++; }
        int ia = ckpt_slot_info(&fs_dev, &sa, FS_TOTAL_SECTORS, &qa), ib = ckpt_slot_info(&fs_dev, &sb, FS_TOTAL_SECTORS, &qb);
        if (ia != 2 || ib != 2) { kprint("[CKPT] FALLO: tras dos guardados las dos ranuras deben ser validas\n"); bad++; }
        else {
            const struct ckpt_slot *nuevo = (int32_t)(qa - qb) > 0 ? &sa : &sb;
            uint64_t cl2 = ckpt_lba(nuevo, 100);
            if (sysdisk_read(cl2, sec) == 0) {
                uint8_t keep = sec[3]; sec[3] ^= 0x7F; sysdisk_write(cl2, sec); sysdisk_flush();
                vfs_write(path, "gen-C", 5);
                if (vfs_checkpoint_restore() != 0) { kprint("[CKPT] FALLO: con la ranura nueva corrupta no cayo a la anterior\n"); bad++; }
                memset(rb, 0, sizeof rb); n = vfs_read(path, rb, sizeof rb - 1);
                if (n != 5 || memcmp(rb, "gen-A", 5) != 0) { kprint("[CKPT] FALLO: la ranura anterior no devolvio gen-A\n"); bad++; }
                sec[3] = keep; sysdisk_write(cl2, sec); sysdisk_flush();
            } else bad++;
        }
    }
    vfs_delete(path); vfs_checkpoint_save();
    kprint(bad == 0 ? "[CKPT] TODO OK: guardar/restaurar, rechazo de copia corrupta y dos generaciones\n" : "[CKPT] HAY FALLOS\n");
    return bad == 0;
}

int vfs_crash_selftest(void)
{
    if (!fs_initialized) vfs_init();
    if (sysdisk_capacity() == 0) { kprint("[FSCRASH] sin disco virtio: nada que probar\n"); return 1; }
    static const uint32_t cases[][2] = { {700, 3000}, {3000, 700}, {15000, 4000}, {1, 15000}, {512, 512}, {0, 900} };
    static char oldb[16384], newb[16384];
    const char *path = "/workspace/_fscrash.tmp";
    int fails = 0, runs = 0, kept_old = 0, got_new = 0;
    kprint("[FSCRASH] cortando la energia (simulado) tras cada escritura posible de un guardado...\n");
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        uint32_t os = cases[c][0], ns = cases[c][1];
        ct_fill(oldb, os, 1); ct_fill(newb, ns, 2);
        for (long cut = 0; cut <= (long)slotio_nd(ns) + 37; ++cut) {
            vfs_delete(path);
            if (vfs_write(path, oldb, os) != (int)os) { kprint("[FSCRASH] no se pudo preparar el archivo\n"); fails++; break; }
            kmutex_lock(&vfs_mutex);
            int idx = -1;
            for (int i = 0; i < FS_DISK_SLOTS; ++i) if (files[i].used && vfs_names_equal(files[i].name, path)) { idx = i; break; }
            if (idx < 0) { kmutex_unlock(&vfs_mutex); fails++; break; }
            kmutex_unlock(&vfs_mutex);
            fs_fault_budget = cut;
            int wr = vfs_write(path, newb, ns);
            fs_fault_budget = -1;
            kmutex_lock(&vfs_mutex);
            slotio_recover(&fs_dev, FS_JOURNAL_LBA, FS_DATA_LBA, FS_SECTORS_PER_FILE, FS_DISK_SLOTS);     /* "reinicio" */
            int lr = load_file_from_disk(idx);
            int is_old = (lr == 0 && files[idx].used && files[idx].size == os && (os == 0 || memcmp(files[idx].data, oldb, os) == 0));
            int is_new = (lr == 0 && files[idx].used && files[idx].size == ns && (ns == 0 || memcmp(files[idx].data, newb, ns) == 0));
            kmutex_unlock(&vfs_mutex);
            runs++;
            if (is_new) got_new++; else if (is_old) kept_old++;
            if (!is_old && !is_new) { fails++; kprint("[FSCRASH] FALLO: archivo a medias (caso "); kprint_dec(c); kprint(", corte "); kprint_dec((uint32_t)cut); kprint(")\n"); }
            if (wr >= 0 && !is_new) { fails++; kprint("[FSCRASH] FALLO: escritura dada por buena pero no esta (caso "); kprint_dec(c); kprint(")\n"); }
        }
    }
    vfs_delete(path);
    kprint("[FSCRASH] "); kprint_dec((uint32_t)runs); kprint(" cortes simulados: "); kprint_dec((uint32_t)kept_old); kprint(" conservaron el contenido anterior, ");
    kprint_dec((uint32_t)got_new); kprint(" completaron el nuevo, ");
    kprint_dec((uint32_t)fails); kprint(" archivos a medias\n");
    kprint(fails ? "[FSCRASH] HAY FALLOS\n" : "[FSCRASH] TODO OK: ningun corte dejo un archivo a medias\n");
    return fails == 0;
}

/* Recorre los inodos en disco: cabeceras coherentes y CRC de los datos. Devuelve el numero de problemas. */
int vfs_fscheck(void)
{
    if (!fs_initialized) vfs_init();
    if (sysdisk_capacity() == 0) { kprint("[FSCHECK] sin disco virtio\n"); return 0; }
    static uint8_t sec[512] __attribute__((aligned(16)));
    static uint8_t buf[FS_DATA_SECTORS * 512];
    int used = 0, crc_ok = 0, sin_crc = 0, bad = 0;
    kmutex_lock(&vfs_mutex);
    for (int i = 0; i < FS_DISK_SLOTS; ++i) {
        struct fs_disk_header h; uint64_t b = FS_DATA_LBA + (uint64_t)i * FS_SECTORS_PER_FILE;
        if (sysdisk_read(b, sec) != 0) { bad++; continue; }
        memcpy(&h, sec, sizeof h);
        if (h.used == 0) continue;
        if (h.used > 1 || h.name[0] == 0 || (h.spilled != 'B' && h.size >= FS_DATA_MAX)) { bad++; kprint("[FSCHECK] inodo "); kprint_dec((uint32_t)i); kprint(": cabecera incoherente\n"); continue; }
        used++;
        if (h.spilled == 'B') continue;
        uint32_t nd = slotio_nd(h.size); int io = 0;
        for (uint32_t s = 0; s < nd; ++s) if (sysdisk_read(b + 1 + s, buf + s * 512) != 0) io = 1;
        if (io) { bad++; continue; }
        if (h.pad[4] != 'C') { sin_crc++; continue; }
        uint32_t want; memcpy(&want, h.pad, 4);
        if (slotio_crc32(0, buf, h.size) == want) crc_ok++; else { bad++; kprint("[FSCHECK] '"); kprint(h.name); kprint("': CRC no coincide\n"); }
    }
    kmutex_unlock(&vfs_mutex);
    kprint("[FSCHECK] archivos en disco: "); kprint_dec((uint32_t)used); kprint(" | CRC correcto: "); kprint_dec((uint32_t)crc_ok);
    kprint(" | sin CRC (escritos por un nucleo anterior): "); kprint_dec((uint32_t)sin_crc); kprint(" | problemas: "); kprint_dec((uint32_t)bad); kprint("\n");
    kprint("[FSCHECK] diario: "); kprint_dec(fs_commits); kprint(" guardados atomicos, "); kprint_dec(fs_jreplayed); kprint(" rehechos y ");
    kprint_dec(fs_jdiscarded); kprint(" descartados en el arranque, "); kprint_dec(fs_crc_bad); kprint(" avisos de CRC al cargar, "); kprint_dec(fs_spill_fixed); kprint(" archivos grandes recuperados\n");
    return bad;
}

/* ================================================================== PARCHE 114: prueba de cortes REALES (tools/killtest.py)
 * El script marca el disco (LBA 30), arranca QEMU y lo mata con kill -9 en mitad de escrituras; en cada arranque este codigo verifica que
 * ningun archivo de prueba quedo a medias y que `fscheck` no ve problemas, y vuelve a escribir sin parar hasta que lo vuelvan a matar. */
static void kt_fill(char *b, uint32_t n, uint32_t seed)
{
    for (uint32_t k = 0; k < n; ++k) b[k] = (char)('a' + (k * 7 + seed * 13 + (k >> 6)) % 26);
    if (n >= 8) { memcpy(b, &seed, 4); memcpy(b + 4, &n, 4); }              /* semilla y longitud dentro del propio archivo: se autoverifica */
}
static int kt_check(const char *path)           /* -1 no existe, 0 danado, 1 correcto */
{
    static char b[41000], e[41000];
    int n = vfs_read(path, b, sizeof b);
    if (n < 0) return -1;
    if (n < 8) return 0;
    uint32_t seed, len; memcpy(&seed, b, 4); memcpy(&len, b + 4, 4);
    if (len != (uint32_t)n) return 0;
    kt_fill(e, (uint32_t)n, seed);
    return memcmp(b, e, (size_t)n) == 0 ? 1 : 0;
}
void vfs_killtest_boot(void)
{
    static uint8_t sec[512] __attribute__((aligned(16)));
    if (sysdisk_capacity() <= SOMA_LBA_KILLTEST + 1 || sysdisk_read(SOMA_LBA_KILLTEST, sec) != 0 || memcmp(sec, "SOMA_KILLTEST", 13) != 0) return;
    if (!fs_initialized) vfs_init();
    const char *names[3] = { "/workspace/k0.bin", "/workspace/k1.bin", "/workspace/k2.bin" };
    int bad = 0, present = 0;
    for (int f = 0; f < 3; ++f) {
        int r = kt_check(names[f]);
        if (r == 0) { bad++; kprint("[KV] ARCHIVO DANADO: "); kprint(names[f]); kprint("\n"); } else if (r == 1) present++;
    }
    int pb = vfs_fscheck();
    kprint(bad == 0 && pb == 0 ? "[KV] OK presentes=" : "[KV] FALLO presentes="); kprint_dec((uint32_t)present); kprint("\n");
    static char b[41000]; uint32_t seed = 1000, x = 12345;
    for (;;) {
        seed++; x = x * 1664525u + 1013904223u;
        uint32_t sz = 100 + (x >> 8) % 39000; int f = (int)((x >> 4) % 3);
        if ((x >> 20) % 5 == 0) sz = 8 + (x >> 8) % 300;                     /* tambien muchos pequenos */
        kt_fill(b, sz, seed);
        vfs_write(names[f], b, sz);
        if (seed % 20 == 0) kprint("[KW] .\n");
    }
}
