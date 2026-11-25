/* SOMA_154_T480S: informacion de arranque Multiboot 1/2, pantalla del firmware y consola de texto sobre ella.
 * Ver bootfb.h. Pensado para el Lenovo ThinkPad T480s (UEFI o BIOS heredada/CSM, Intel UHD 620), pero es generico:
 * sirve en cualquier PC cuyo firmware deje un framebuffer lineal de 32 (o 24) bits. */
#include "bootfb.h"
#include "fb.h"
#include "vmm.h"
#include "mem.h"
#include "console.h"
#include "gui_font.h"

/* ------------------------------------------------------------------ informacion de arranque */
#define MB1_MAGIC 0x2BADB002u
#define MB2_MAGIC 0x36D76289u

static struct bootfb_info g_fb;
static int g_mb2, g_efi;
static uint64_t g_rsdp;
static uint64_t g_efi_systab;                 /* SOMA_156_T480S: tabla de sistema EFI (para encontrar SMBIOS) */
static char g_cmdline[256], g_loader[64];
static const uint8_t *g_mb2_info;           /* copia que hizo boot.s (memoria del nucleo, siempre accesible) */
static uint32_t g_mb2_size;

static void copy_str(char *d, const char *s, uint32_t max)
{
    uint32_t i = 0; if (!s) { d[0] = 0; return; }
    for (; s[i] && i + 1 < max; i++) d[i] = s[i];
    d[i] = 0;
}
static void set_fb(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp, uint8_t type,
                   uint8_t rp, uint8_t gp, uint8_t bp)
{
    g_fb.addr = addr; g_fb.pitch = pitch; g_fb.width = w; g_fb.height = h; g_fb.bpp = bpp; g_fb.type = type;
    g_fb.r_pos = rp; g_fb.g_pos = gp; g_fb.b_pos = bp;
    /* solo RGB directo de 24/32 bits con un tamano razonable (hasta 4K) */
    g_fb.valid = type == 1 && (bpp == 32 || bpp == 24) && addr && w >= 320 && h >= 200 && w <= 4096 && h <= 2304 &&
                 pitch >= w * (bpp / 8u) && (uint64_t)pitch * h <= (64ull << 20);
}

/* SOMA_175A_T480S: modulos que carga GRUB (module2) junto al nucleo: el firmware de la WiFi */
static struct { uint32_t start, end; char name[48]; } g_mod[4];
static int g_nmod;
int bootinfo_module_count(void) { return g_nmod; }
int bootinfo_module_at(int i, uint64_t *start, uint64_t *len, const char **name)
{
    if (i < 0 || i >= g_nmod) return 0;
    if (start) *start = g_mod[i].start;
    if (len) *len = g_mod[i].end - g_mod[i].start;
    if (name) *name = g_mod[i].name;
    return 1;
}
int bootinfo_module(const char *want, uint64_t *start, uint64_t *len)
{
    for (int i = 0; i < g_nmod; i++) {
        const char *n = g_mod[i].name;
        for (const char *s = n; *s; s++) {                        /* el nombre contiene 'want' */
            const char *a = s, *b = want;
            while (*a && *b && *a == *b) { a++; b++; }
            if (!*b) { *start = g_mod[i].start; *len = g_mod[i].end - g_mod[i].start; return 1; }
        }
        const char *a = n, *b = want; while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) { *start = g_mod[i].start; *len = g_mod[i].end - g_mod[i].start; return 1; }
    }
    return 0;
}
void bootinfo_parse(uint32_t magic, uint32_t addr)
{
    g_mb2 = 0; g_fb.valid = 0; g_rsdp = 0; g_efi_systab = 0; g_cmdline[0] = 0; g_loader[0] = 0; g_nmod = 0;
    if (!addr) return;
    if (magic == MB2_MAGIC) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
        uint32_t total = *(const uint32_t *)p;
        if (total < 16) return;
        if (total > 32768) total = 32768;                       /* boot.s copia como mucho 32 KiB */
        g_mb2 = 1; g_mb2_info = p; g_mb2_size = total;
        for (uint32_t off = 8; off + 8 <= total;) {
            const uint8_t *t = p + off;
            uint32_t type = *(const uint32_t *)t, size = *(const uint32_t *)(t + 4);
            if (type == 0 || size < 8) break;
            switch (type) {
            case 1: copy_str(g_cmdline, (const char *)(t + 8), sizeof g_cmdline); break;
            case 2: copy_str(g_loader, (const char *)(t + 8), sizeof g_loader); break;
            case 8:                                              /* framebuffer */
                /* comun (32 bytes): addr u64 @8, pitch @16, width @20, height @24, bpp u8 @28, type u8 @29; RGB: posiciones @32/@34/@36 */
                if (size >= 32) set_fb(*(const uint64_t *)(t + 8), *(const uint32_t *)(t + 16), *(const uint32_t *)(t + 20),
                                       *(const uint32_t *)(t + 24), t[28], t[29], size >= 38 ? t[32] : 16, size >= 38 ? t[34] : 8, size >= 38 ? t[36] : 0);
                break;
            case 14: if (!g_rsdp) g_rsdp = (uint64_t)(uintptr_t)(t + 8); break;      /* ACPI 1.0: copia del RSDP dentro del tag */
            case 15: g_rsdp = (uint64_t)(uintptr_t)(t + 8); break;                  /* ACPI 2.0+ (preferido) */
            case 11: g_efi = 1; if (size >= 12) g_efi_systab = *(const uint32_t *)(t + 8); break;   /* tabla de sistema EFI 32 bits */
            case 12: g_efi = 1; if (size >= 16) g_efi_systab = *(const uint64_t *)(t + 8); break;   /* 64 bits (el T480s) */
            case 3:                                              /* SOMA_175A: modulo (firmware WiFi): inicio u32, fin u32, nombre */
                if (size >= 17 && g_nmod < 4) {
                    g_mod[g_nmod].start = *(const uint32_t *)(t + 8); g_mod[g_nmod].end = *(const uint32_t *)(t + 12);
                    copy_str(g_mod[g_nmod].name, (const char *)(t + 16), sizeof g_mod[g_nmod].name);
                    if (g_mod[g_nmod].end > g_mod[g_nmod].start) g_nmod++;
                }
                break;
            default: break;
            }
            off += (size + 7u) & ~7u;
        }
        return;
    }
    if (magic == MB1_MAGIC) {
        const uint8_t *mbi = (const uint8_t *)(uintptr_t)addr;
        uint32_t flags = *(const uint32_t *)mbi;
        if ((flags & (1u << 2)) && *(const uint32_t *)(mbi + 16)) copy_str(g_cmdline, (const char *)(uintptr_t)*(const uint32_t *)(mbi + 16), sizeof g_cmdline);
        if ((flags & (1u << 9)) && *(const uint32_t *)(mbi + 64)) copy_str(g_loader, (const char *)(uintptr_t)*(const uint32_t *)(mbi + 64), sizeof g_loader);
        if (flags & (1u << 12))
            set_fb(*(const uint64_t *)(mbi + 88), *(const uint32_t *)(mbi + 96), *(const uint32_t *)(mbi + 100), *(const uint32_t *)(mbi + 104),
                   mbi[108], mbi[109], mbi[110], mbi[112], mbi[114]);
    }
}
int bootinfo_is_mb2(void) { return g_mb2; }
int bootinfo_efi(void) { return g_efi; }
uint64_t bootinfo_efi_systab(void) { return g_efi_systab; }
/* SOMA_156_T480S: interruptores. "soma.X=off" apaga la pieza X; "soma.seguro" (entrada "modo seguro" del menu) apaga todo
 * lo que tenga interruptor. Cada driver nuevo del T480s debe preguntar aqui antes de tocar su hardware. */
int bootinfo_disabled(const char *feature)
{
    if (bootinfo_has_option("soma.seguro")) return 1;
    char opt[48]; size_t n = 0;
    const char *pre = "soma.";
    while (*pre && n < sizeof opt - 1) opt[n++] = *pre++;
    while (feature && *feature && n < sizeof opt - 5) opt[n++] = *feature++;
    const char *suf = "=off";
    while (*suf && n < sizeof opt - 1) opt[n++] = *suf++;
    opt[n] = 0;
    return bootinfo_has_option(opt);
}
const char *bootinfo_cmdline(void) { return g_cmdline; }
uint64_t bootinfo_rsdp(void) { return g_rsdp; }
int bootinfo_has_option(const char *opt)
{
    for (const char *s = g_cmdline; *s; s++) {
        const char *a = s, *b = opt;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b && (*a == 0 || *a == ' ') && (s == g_cmdline || s[-1] == ' ')) return 1;
    }
    return 0;
}
int bootinfo_mmap_next(uint32_t *it, uint64_t *base, uint64_t *len, uint32_t *type)
{
    if (!g_mb2) return 0;
    /* *it: 0 = buscar el tag; despues, desplazamiento absoluto de la siguiente entrada dentro de la copia */
    static uint32_t tag_off, tag_end, esz;
    if (*it == 0) {
        tag_off = 0;
        for (uint32_t off = 8; off + 8 <= g_mb2_size;) {
            const uint8_t *t = g_mb2_info + off;
            uint32_t ty = *(const uint32_t *)t, sz = *(const uint32_t *)(t + 4);
            if (ty == 0 || sz < 8) break;
            if (ty == 6 && sz >= 16) { tag_off = off; tag_end = off + sz; esz = *(const uint32_t *)(t + 8); break; }
            off += (sz + 7u) & ~7u;
        }
        if (!tag_off || esz < 24) return 0;
        *it = tag_off + 16;
    }
    if (*it + 24 > tag_end) return 0;
    const uint8_t *e = g_mb2_info + *it;
    *base = *(const uint64_t *)e; *len = *(const uint64_t *)(e + 8); *type = *(const uint32_t *)(e + 16);
    *it += esz;
    return 1;
}

/* ------------------------------------------------------------------ framebuffer del firmware */
const struct bootfb_info *bootfb_get(void) { return &g_fb; }
int bootfb_use_firmware(void) { return g_fb.valid && !bootinfo_has_option("soma.fb=bga"); }
int bootfb_swap_rb(void) { return g_fb.valid && g_fb.r_pos == 0 && g_fb.b_pos == 16; }

static int g_mapped;
#define PTE_PWT (1ULL << 3)
static inline void cpuid1(uint32_t *edx) { uint32_t a = 1, b, c, d; __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d)); *edx = d; }
/* PAT: la entrada 1 (PWT=1, PCD=0) pasa de write-through a write-combining, como hace Linux. El framebuffer se proyecta con
 * PWT: las escrituras se agrupan en rafagas en vez de ir de una en una sin cache (en hardware real, varias veces mas rapido).
 * Las proyecciones existentes de MMIO usan PWT|PCD (entrada 3 = UC) y no cambian. */
static void pat_enable_wc(void)
{
    uint32_t edx; cpuid1(&edx);
    if (!(edx & (1u << 16))) return;
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x277u));
    lo = (lo & ~0x0000FF00u) | 0x00000100u;                     /* PA1 = 01h (WC) */
    __asm__ volatile ("wbinvd" ::: "memory");
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0x277u));
    __asm__ volatile ("wbinvd" ::: "memory");
    uint64_t cr3; __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3)); __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}
int bootfb_map(void)
{
    if (g_mapped) return 0;
    if (!bootfb_use_firmware()) return -1;
    pat_enable_wc();
    uint64_t bytes = (uint64_t)g_fb.pitch * g_fb.height;
    uint64_t base = g_fb.addr & ~0xFFFull, off = g_fb.addr & 0xFFFull;
    uint64_t pages = (bytes + off + 4095) / 4096;
    for (uint64_t i = 0; i < pages; i++)
        if (vmm_map_page(FB_VIRT_ADDR + i * 4096, base + i * 4096, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX | PTE_PWT) != 0) return -1;
    g_mapped = 1;
    return 0;
}
static inline volatile uint8_t *fbp(void) { return (volatile uint8_t *)(uintptr_t)(FB_VIRT_ADDR + (g_fb.addr & 0xFFFull)); }

/* ------------------------------------------------------------------ consola de texto */
/* fuente de 8x8 a escala 2 (celdas de 16x16, legible en 1080p); escala 3 en pantallas de 2400 px o mas (WQHD) */
static uint32_t con_scale = 2;
#define CON_SCALE con_scale
#define CON_CW (8 * con_scale)
#define CON_CH (8 * con_scale)
#define CON_MAXC 256
#define CON_MAXR 144
static uint8_t  con_ch[CON_MAXR][CON_MAXC];
static uint8_t  con_at[CON_MAXR][CON_MAXC];
static uint32_t con_cols, con_rows, con_x, con_y;
static int con_on, con_started, con_stopped;
static uint8_t con_attr = 7;
static int con_esc, con_narg; static uint32_t con_arg[4];
static char early[16384]; static uint32_t early_len;           /* lo impreso antes de poder pintar */

static const uint32_t con_pal[16] = {
    0x000000, 0xCD3131, 0x0DBC79, 0xE5E510, 0x2472C8, 0xBC3FBC, 0x11A8CD, 0xD0D0D0,
    0x666666, 0xF14C4C, 0x23D18B, 0xF5F543, 0x3B8EEA, 0xD670D6, 0x29B8DB, 0xFFFFFF };

static inline void px_put(volatile uint8_t *row, uint32_t x, uint32_t rgb)
{
    if (bootfb_swap_rb()) rgb = (rgb & 0x00FF00u) | ((rgb >> 16) & 0xFFu) | ((rgb & 0xFFu) << 16);
    if (g_fb.bpp == 32) ((volatile uint32_t *)row)[x] = rgb;
    else { row[x * 3] = (uint8_t)rgb; row[x * 3 + 1] = (uint8_t)(rgb >> 8); row[x * 3 + 2] = (uint8_t)(rgb >> 16); }
}
static void cell_draw(uint32_t cx, uint32_t cy)
{
    uint8_t c = con_ch[cy][cx], a = con_at[cy][cx];
    const uint8_t *g = font_get_glyph((char)(c >= 32 && c < 127 ? c : '?'));
    uint32_t fg = con_pal[a & 15], bg = 0x000000;
    volatile uint8_t *base = fbp();
    for (uint32_t r = 0; r < 8; r++) {
        uint8_t bits = (c == ' ' || !g) ? 0 : g[r];
        for (uint32_t sy = 0; sy < CON_SCALE; sy++) {
            volatile uint8_t *row = base + (uint64_t)(cy * CON_CH + r * CON_SCALE + sy) * g_fb.pitch;
            for (uint32_t col = 0; col < 8; col++) {
                uint32_t v = (bits & (0x80u >> col)) ? fg : bg;
                for (uint32_t sx = 0; sx < CON_SCALE; sx++) px_put(row, cx * CON_CW + col * CON_SCALE + sx, v);
            }
        }
    }
}
static void con_redraw(void)
{
    for (uint32_t y = 0; y < con_rows; y++) for (uint32_t x = 0; x < con_cols; x++) cell_draw(x, y);
}
static void con_clear(void)
{
    for (uint32_t y = 0; y < con_rows; y++) for (uint32_t x = 0; x < con_cols; x++) { con_ch[y][x] = ' '; con_at[y][x] = 7; }
    volatile uint8_t *base = fbp();
    for (uint32_t y = 0; y < g_fb.height; y++) { volatile uint8_t *row = base + (uint64_t)y * g_fb.pitch; for (uint32_t x = 0; x < g_fb.width; x++) px_put(row, x, 0); }
    con_x = con_y = 0;
}
/* desplazamiento "a saltos" de un tercio de pantalla: se redibuja desde las celdas (solo escrituras: leer del framebuffer
 * es muy lento en hardware real) y se hace pocas veces aunque el arranque imprima cientos de lineas */
static void con_scroll(void)
{
    uint32_t n = con_rows / 3; if (n < 1) n = 1;
    for (uint32_t y = 0; y + n < con_rows; y++) for (uint32_t x = 0; x < con_cols; x++) { con_ch[y][x] = con_ch[y + n][x]; con_at[y][x] = con_at[y + n][x]; }
    for (uint32_t y = con_rows - n; y < con_rows; y++) for (uint32_t x = 0; x < con_cols; x++) { con_ch[y][x] = ' '; con_at[y][x] = 7; }
    con_y -= n;
    con_redraw();
}
static void con_sgr(void)
{
    if (con_narg == 0) { con_attr = 7; return; }
    int bold = (con_attr & 8) != 0;
    for (int i = 0; i < con_narg; i++) {
        uint32_t a = con_arg[i];
        if (a == 0) { con_attr = 7; bold = 0; }
        else if (a == 1) bold = 1;
        else if (a >= 30 && a <= 37) con_attr = (uint8_t)(a - 30);
        else if (a >= 90 && a <= 97) con_attr = (uint8_t)(a - 90 + 8);
    }
    if (bold) con_attr |= 8;
}
static void con_putc(char c)
{
    unsigned char u = (unsigned char)c;
    if (con_esc == 1) { if (c == '[') { con_esc = 2; con_narg = 0; con_arg[0] = con_arg[1] = con_arg[2] = con_arg[3] = 0; } else con_esc = 0; return; }
    if (con_esc == 2) {
        if (c >= '0' && c <= '9') { if (con_narg == 0) con_narg = 1; if (con_narg <= 4) con_arg[con_narg - 1] = con_arg[con_narg - 1] * 10 + (uint32_t)(c - '0'); return; }
        if (c == ';') { if (con_narg == 0) con_narg = 1; if (con_narg < 4) con_narg++; return; }
        if (c == '?') return;
        con_esc = 0;
        if (c == 'm') con_sgr();
        else if (c == 'J') con_clear();
        else if (c == 'H') con_x = con_y = 0;
        return;
    }
    if (u == 27) { con_esc = 1; return; }
    if (c == '\r') { con_x = 0; return; }
    if (c == '\n') { con_x = 0; con_y++; }
    else if (c == '\b') { if (con_x) { con_x--; con_ch[con_y][con_x] = ' '; cell_draw(con_x, con_y); } return; }
    else if (c == '\t') { con_x = (con_x + 8) & ~7u; }
    else if (u >= 0x80 && u < 0xC0) return;                    /* continuacion UTF-8: el caracter ya salio como '?' */
    else if (u >= 32) { con_ch[con_y][con_x] = u >= 0x80 ? '?' : u; con_at[con_y][con_x] = con_attr; cell_draw(con_x, con_y); con_x++; }
    else return;
    if (con_x >= con_cols) { con_x = 0; con_y++; }
    if (con_y >= con_rows) con_scroll();
}

void bootfb_putc(char c)
{
    if (con_on) { con_putc(c); return; }
    if (!con_started && !con_stopped && early_len < sizeof early) early[early_len++] = c;   /* se pinta al arrancar la consola */
}
void bootfb_console_start(void)
{
    if (con_started || !bootfb_use_firmware() || bootfb_map() != 0) return;
    con_scale = (g_fb.width >= 2400u) ? 3u : 2u;
    con_cols = g_fb.width / CON_CW; con_rows = g_fb.height / CON_CH;
    if (con_cols > CON_MAXC) con_cols = CON_MAXC;
    if (con_rows > CON_MAXR) con_rows = CON_MAXR;
    con_started = 1; con_on = 1;
    con_clear();
    for (uint32_t i = 0; i < early_len; i++) con_putc(early[i]);
    early_len = 0;
}
void bootfb_console_stop(void) { con_on = 0; con_stopped = 1; }
void bootfb_console_resume(void) { if (con_started) { con_on = 1; con_redraw(); } }
void bootfb_console_panic(void)
{
    if (!con_started) { bootfb_console_start(); return; }
    if (!con_on) { con_on = 1; con_attr = 15; con_clear(); }
}

static void pdec(uint64_t v) { char t[21]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } while (n) { char o[2] = { t[--n], 0 }; kprint(o); } }
static void phex(uint64_t v) { kprint("0x"); for (int s = 60; s >= 0; s -= 4) { char o[2] = { "0123456789ABCDEF"[(v >> s) & 15], 0 }; kprint(o); } }
void bootfb_print_info(void)
{
    kprint("arranque: "); kprint(g_mb2 ? "Multiboot 2" : "Multiboot 1"); kprint(g_efi ? " desde UEFI" : " (BIOS o desconocido)");
    if (g_loader[0]) { kprint(", cargador '"); kprint(g_loader); kprint("'"); }
    kprint("\nlinea de ordenes: '"); kprint(g_cmdline); kprint("'\n");
    if (!g_fb.valid) { kprint("pantalla del firmware: no hay (se usa la tarjeta Bochs de QEMU si existe)\n"); }
    else {
        kprint("pantalla del firmware: "); pdec(g_fb.width); kprint("x"); pdec(g_fb.height); kprint(" "); pdec(g_fb.bpp);
        kprint(" bits, pitch "); pdec(g_fb.pitch); kprint(", fisica "); phex(g_fb.addr);
        kprint(bootfb_swap_rb() ? ", RGBX" : ", BGRX"); kprint(bootfb_use_firmware() ? ", EN USO\n" : ", ignorada (soma.fb=bga)\n");
    }
    kprint("ACPI RSDP: "); if (g_rsdp) phex(g_rsdp); else kprint("no lo paso el cargador (se busca en la zona BIOS)"); kprint("\n");
}
