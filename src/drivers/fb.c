#ifdef __x86_64__
#include "bootfb.h"   /* SOMA_154_T480S */
#endif
#include "fb.h"
#include "smp.h"                                     /* PARCHE 082 */
#include "clipboard.h"   /* SOMA_PARCHE_PORTAPAPELES */
#include "console.h"
#include "pci.h"
#include "vmm.h"
#include "mem.h"
#include "io.h"
#ifdef __aarch64__
#include "rpi_fb.h"
#define FB_SYNC(p, bytes) soma_dcache_clean((p), (bytes))      /* PARCHE 063: la pantalla lee RAM, no la cache de la CPU */
#else
#define FB_SYNC(p, bytes) ((void)0)
#endif

#define BGA_INDEX_PORT      0x01CE
#define BGA_DATA_PORT       0x01CF

#define BGA_REG_ID          0
#define BGA_REG_XRES        1
#define BGA_REG_YRES        2
#define BGA_REG_BPP         3
#define BGA_REG_ENABLE      4
#define BGA_REG_BANK        5
#define BGA_REG_X_OFFSET    8
#define BGA_REG_Y_OFFSET    9

#define BGA_DISABLED        0x00
#define BGA_ENABLED         0x01
#define BGA_LFB_ENABLED     0x40

#define VGA_MISC_WRITE      0x03C2
#define VGA_SEQ_INDEX       0x03C4
#define VGA_SEQ_DATA        0x03C5
#define VGA_CRTC_INDEX      0x03D4
#define VGA_CRTC_DATA       0x03D5
#define VGA_GC_INDEX        0x03CE
#define VGA_GC_DATA         0x03CF
#define VGA_AC_INDEX        0x03C0
#define VGA_INSTAT_READ     0x03DA

static volatile uint32_t *fb_vram = (volatile uint32_t *)FB_VIRT_ADDR;
/* PARCHE 076: la pantalla de la Pi 5 real es de 16 bits (RGB565) y su pitch puede no ser ancho*4: el backbuffer sigue siendo de 32 bits y al volcarlo
 * se convierte. En x86_64 (VGA/BGA) la pantalla es siempre de 32 bits contiguos. */
/* SOMA_154_T480S: en x86 la pantalla tambien puede ser la del firmware (GOP/VBE de un portatil real), con pitch propio,
 * 24 o 32 bits y rojo/azul en cualquier orden: se usa el mismo volcado general que en la Pi. Con BGA (QEMU) sigue el
 * camino rapido de siempre (32 bits contiguos). */
#if defined(__aarch64__) || defined(__x86_64__)
static uint32_t fb_hw_bpp = 32, fb_hw_pitch = 0;
#ifdef __aarch64__
static inline int fb_swap_rb(void) { return rpi_fb_swap_rb(); }
#else
static int fb_fw_mode;                      /* 1 = framebuffer del firmware (bootfb), 0 = BGA */
static inline int fb_swap_rb(void) { return fb_fw_mode && bootfb_swap_rb(); }
#endif
#define FB_HW_NATIVE() (fb_hw_bpp == 32 && fb_hw_pitch == fb_cur_w * 4u && !fb_swap_rb())
static inline uint32_t fb_hw_c(uint32_t c) { return fb_swap_rb() ? ((c & 0xFF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16)) : c; }
static void fb_hw_put_span(int32_t x, int32_t y, const uint32_t *src, int32_t n)
{
    volatile uint8_t *row = (volatile uint8_t *)fb_vram + (uint64_t)y * fb_hw_pitch + (uint64_t)x * (fb_hw_bpp / 8);
    if (fb_hw_bpp == 16) {
        volatile uint16_t *d = (volatile uint16_t *)row;
        for (int32_t i = 0; i < n; i++) { uint32_t c = fb_hw_c(src[i]); d[i] = (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu)); }
    } else if (fb_hw_bpp == 24) {
        for (int32_t i = 0; i < n; i++) { uint32_t c = fb_hw_c(src[i]); row[i * 3] = (uint8_t)c; row[i * 3 + 1] = (uint8_t)(c >> 8); row[i * 3 + 2] = (uint8_t)(c >> 16); }
    } else {
        volatile uint32_t *d = (volatile uint32_t *)row;
        for (int32_t i = 0; i < n; i++) d[i] = fb_hw_c(src[i]);
    }
    FB_SYNC(row, (size_t)n * (fb_hw_bpp / 8));
}
#else
#define FB_HW_NATIVE() 1
#endif

static uint32_t *fb_backbuffer = 0;
static size_t fb_bb_bytes = 0;            /* SOMA_PARCHE_021_FPU: tamano reservado del backbuffer */
static volatile int fb_user_owned = 0;    /* un proceso de usuario tiene la pantalla en exclusiva */
static int fb_prev_active; static uint32_t fb_prev_w, fb_prev_h, fb_prev_lw, fb_prev_lh;   /* modo que habia antes de fb_user_begin */
static uint32_t fb_phys_base = 0;
static int fb_mapped = 0;
static int fb_active = 0;

static uint32_t fb_cur_w = FB_RES_FHD_W;
static uint32_t fb_cur_h = FB_RES_FHD_H;

/* Región de Recorte (Scissor Clipping) */
static int32_t clip_x0 = 0;
static int32_t clip_y0 = 0;
static int32_t clip_x1 = FB_RES_FHD_W;
static int32_t clip_y1 = FB_RES_FHD_H;

/* Lista de rectángulos de daño */
#define FB_MAX_DIRTY_RECTS 48   /* SOMA_209: 16 se llenaban solo con los iconos del escritorio */
struct fb_dirty_rect {
    int32_t x0, y0, x1, y1;
};
static struct fb_dirty_rect dirty_rects[FB_MAX_DIRTY_RECTS];
static int dirty_count = 0;

/* Estado del cursor superpuesto (Overlay) */
static const uint32_t *cursor_overlay_img = 0;
static int32_t cursor_overlay_w = 0;
static int32_t cursor_overlay_h = 0;
static int32_t cursor_pos_x = -1;
static int32_t cursor_pos_y = -1;
static int     cursor_visible = 0;

static int32_t cursor_drawn_x = -1;
static int32_t cursor_drawn_y = -1;
static int     cursor_drawn_visible = 0;

/* SOMA_225_AJUSTES: resolucion de trabajo (Ajustes > Pantalla). La interfaz dibuja a una resolucion logica (fb_cur_w x fb_cur_h) y al
 * volcar se escala con filtro bilineal a la de la pantalla (fb_hw_w x fb_hw_h). Funciona igual con la pantalla del firmware (T480s, sin
 * controlador de GPU para cambiar de modo) que en el emulador. El backbuffer sigue siendo el de la resolucion nativa (cabe la logica).
 * Solo x86_64; en la Pi 5 la resolucion se elige al arrancar. */
static int fb_sc_on;
static uint32_t fb_hw_w = FB_RES_FHD_W, fb_hw_h = FB_RES_FHD_H;
static uint32_t *fb_sc_xt __attribute__((unused)), *fb_sc_yt __attribute__((unused));   /* por columna/fila de la pantalla: (origen << 8) | peso 0..255 */
static int32_t cursor_hot_x, cursor_hot_y;           /* PARCHE 085: punto activo del puntero */

uint32_t fb_get_width(void)  { return fb_cur_w; }
uint32_t fb_get_height(void) { return fb_cur_h; }

static inline uint32_t *fb_target(void)
{
    return fb_backbuffer ? fb_backbuffer : (uint32_t *)fb_vram;
}

volatile uint32_t *fb_get_buffer(void)
{
    return (volatile uint32_t *)fb_target();
}

__attribute__((unused)) static void bga_write(uint16_t reg, uint16_t val)
{
#ifdef __x86_64__
    outw(BGA_INDEX_PORT, reg);
    outw(BGA_DATA_PORT, val);
#else
    (void)reg; (void)val;
#endif
}

__attribute__((unused)) static uint16_t bga_read(uint16_t reg)
{
#ifdef __x86_64__
    outw(BGA_INDEX_PORT, reg);
    return inw(BGA_DATA_PORT);
#else
    (void)reg; return 0;
#endif
}

static uint32_t pci_find_vga_lfb(void)
{
#ifdef __x86_64__
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t v = pci_vendor_id((uint8_t)bus, slot, func);
                uint16_t d = pci_device_id((uint8_t)bus, slot, func);
                if (v == 0x1234 && d == 0x1111) {
                    return pci_bar0((uint8_t)bus, slot, func) & 0xFFFFFFF0;
                }
            }
        }
    }
#endif
    return 0xFD000000;
}

int fb_init(void)
{
    if (fb_mapped) return 0;
#ifdef __aarch64__
    fb_mapped = 1;                                   /* PARCHE 063: la memoria de pantalla la reserva el firmware en fb_set_mode */
    return 0;
#endif
#ifdef __x86_64__
    if (bootfb_use_firmware()) { if (bootfb_map() != 0) return -1; fb_mapped = 1; return 0; }   /* SOMA_154 */
#endif
    fb_phys_base = pci_find_vga_lfb();

    for (uint64_t i = 0; i < 4096; ++i) {
        uint64_t va = FB_VIRT_ADDR + i * 4096;
        uint64_t pa = (uint64_t)fb_phys_base + i * 4096;
        if (vmm_map_page(va, pa, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX) != 0) {
            return -1;
        }
    }
    fb_mapped = 1;
    return 0;
}

void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) {
        clip_x0 = clip_x1 = clip_y0 = clip_y1 = 0;
        return;
    }
    clip_x0 = (x < 0) ? 0 : x;
    clip_y0 = (y < 0) ? 0 : y;
    clip_x1 = (x + w > (int32_t)fb_cur_w) ? (int32_t)fb_cur_w : (x + w);
    clip_y1 = (y + h > (int32_t)fb_cur_h) ? (int32_t)fb_cur_h : (y + h);
}

void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    if (x0) *x0 = clip_x0;
    if (y0) *y0 = clip_y0;
    if (x1) *x1 = clip_x1;
    if (y1) *y1 = clip_y1;
}

void fb_reset_clip(void)
{
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = (int32_t)fb_cur_w;
    clip_y1 = (int32_t)fb_cur_h;
}

static inline int clip_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    if (*w <= 0 || *h <= 0) return 0;
    int32_t x0 = *x;
    int32_t y0 = *y;
    int32_t x1 = x0 + *w;
    int32_t y1 = y0 + *h;

    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;

    if (x0 >= x1 || y0 >= y1) return 0;

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

/* SOMA_PARCHE_012_RENDER: lo que se dibuja cada fotograma se registra en DOS listas:
 *   dirty_rects : lo que fb_present debe copiar a la VRAM (se vacia al presentar)
 *   drawn_rects : todo lo que ya no es color de fondo en el backbuffer desde el ultimo fb_clear;
 *                 fb_clear_changed() borra solo esto en vez de los 8,3 MB de la pantalla entera. */
static struct fb_dirty_rect drawn_rects[FB_MAX_DIRTY_RECTS];
static int drawn_count = 0;
static int drawn_valid = 0;          /* 1 = el backbuffer es drawn_bg salvo lo listado en drawn_rects */
static uint32_t drawn_bg = 0;

static void rect_add(struct fb_dirty_rect *list, int *count, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    /* Intentar fusionar con rectángulos existentes que solapen */
    for (int i = 0; i < *count; ++i) {
        if (x0 <= list[i].x1 && x1 >= list[i].x0 &&
            y0 <= list[i].y1 && y1 >= list[i].y0) {
            {   /* SOMA_209: si fundirlos crea un rectangulo mucho mayor que los dos juntos (una ventana y el orbe que apenas se
                 * tocan daban media pantalla), se dejan separados; que se solapen no hace dano. 'Mucho mayor' = el hueco que
                 * sobraria supera 1/8 del rectangulo fundido. */
                int32_t ux0 = x0 < list[i].x0 ? x0 : list[i].x0, uy0 = y0 < list[i].y0 ? y0 : list[i].y0;
                int32_t ux1 = x1 > list[i].x1 ? x1 : list[i].x1, uy1 = y1 > list[i].y1 ? y1 : list[i].y1;
                int32_t ix0 = x0 > list[i].x0 ? x0 : list[i].x0, iy0 = y0 > list[i].y0 ? y0 : list[i].y0;
                int32_t ix1 = x1 < list[i].x1 ? x1 : list[i].x1, iy1 = y1 < list[i].y1 ? y1 : list[i].y1;
                int64_t in = (ix1 > ix0 && iy1 > iy0) ? (int64_t)(ix1 - ix0) * (iy1 - iy0) : 0;
                int64_t un = (int64_t)(ux1 - ux0) * (uy1 - uy0);
                int64_t sm = (int64_t)(x1 - x0) * (y1 - y0) + (int64_t)(list[i].x1 - list[i].x0) * (list[i].y1 - list[i].y0) - in;
                if (un - sm > un / 8 && un - sm > 4096 && *count < FB_MAX_DIRTY_RECTS) continue;   /* sobra demasiado: separados */
            }
            if (x0 < list[i].x0) list[i].x0 = x0;
            if (y0 < list[i].y0) list[i].y0 = y0;
            if (x1 > list[i].x1) list[i].x1 = x1;
            if (y1 > list[i].y1) list[i].y1 = y1;
            return;
        }
    }

    if (*count < FB_MAX_DIRTY_RECTS) {
        list[*count].x0 = x0;
        list[*count].y0 = y0;
        list[*count].x1 = x1;
        list[*count].y1 = y1;
        (*count)++;
    } else {
        /* SOMA_209: lista llena. Se fusiona con el rectangulo que menos crece (antes siempre con el 0, que con los iconos del
         * escritorio a la vista acababa abarcando la pantalla entera y obligaba a borrarla y volcarla en cada fotograma). */
        int best = 0; int64_t bestg = -1;
        for (int i = 0; i < *count; ++i) {
            int32_t ux0 = x0 < list[i].x0 ? x0 : list[i].x0, uy0 = y0 < list[i].y0 ? y0 : list[i].y0;
            int32_t ux1 = x1 > list[i].x1 ? x1 : list[i].x1, uy1 = y1 > list[i].y1 ? y1 : list[i].y1;
            int64_t g = (int64_t)(ux1 - ux0) * (uy1 - uy0) - (int64_t)(list[i].x1 - list[i].x0) * (list[i].y1 - list[i].y0);
            if (bestg < 0 || g < bestg) { bestg = g; best = i; }
        }
        if (x0 < list[best].x0) list[best].x0 = x0;
        if (y0 < list[best].y0) list[best].y0 = y0;
        if (x1 > list[best].x1) list[best].x1 = x1;
        if (y1 > list[best].y1) list[best].y1 = y1;
    }
}

/* PARCHE 088: dibujar en un bufer propio con las primitivas de siempre (texto, lineas suavizadas...). Mientras dura, el
 * backbuffer, su tamano y el recorte son los del bufer; los rectangulos sucios se restauran al terminar. Solo hilo de la GUI. */
static uint32_t *tg_bb; static uint32_t tg_w, tg_h; static int32_t tg_c[4]; static int tg_dc, tg_dn, tg_on;
static struct fb_dirty_rect tg_dr[FB_MAX_DIRTY_RECTS], tg_dn_r[FB_MAX_DIRTY_RECTS];
void fb_target_begin(uint32_t *buf, uint32_t w, uint32_t h)
{
    if (tg_on || !buf || !w || !h) return;
    tg_on = 1; tg_bb = fb_backbuffer; tg_w = fb_cur_w; tg_h = fb_cur_h;
    tg_c[0] = clip_x0; tg_c[1] = clip_y0; tg_c[2] = clip_x1; tg_c[3] = clip_y1;
    tg_dc = dirty_count; tg_dn = drawn_count; memcpy(tg_dr, dirty_rects, sizeof tg_dr); memcpy(tg_dn_r, drawn_rects, sizeof tg_dn_r);
    fb_backbuffer = buf; fb_cur_w = w; fb_cur_h = h;
    clip_x0 = 0; clip_y0 = 0; clip_x1 = (int32_t)w; clip_y1 = (int32_t)h;
}
void fb_target_end(void)
{
    if (!tg_on) return;
    tg_on = 0; fb_backbuffer = tg_bb; fb_cur_w = tg_w; fb_cur_h = tg_h;
    clip_x0 = tg_c[0]; clip_y0 = tg_c[1]; clip_x1 = tg_c[2]; clip_y1 = tg_c[3];
    dirty_count = tg_dc; drawn_count = tg_dn; memcpy(dirty_rects, tg_dr, sizeof tg_dr); memcpy(drawn_rects, tg_dn_r, sizeof tg_dn_r);
}
#ifdef SOMA_WEBSVC
/* SOMA_202: en el proceso del navegador aislado (ring 3) no hay pantalla: las primitivas de dibujo de este archivo se usan solo
 * sobre la superficie que se fija con fb_target_begin(). */
void fb_offscreen_enable(void) { fb_active = 1; }
#endif
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) return;
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    if (x1 > (int32_t)fb_cur_w) x1 = (int32_t)fb_cur_w;
    if (y1 > (int32_t)fb_cur_h) y1 = (int32_t)fb_cur_h;
    if (x0 >= x1 || y0 >= y1) return;

    rect_add(dirty_rects, &dirty_count, x0, y0, x1, y1);
    rect_add(drawn_rects, &drawn_count, x0, y0, x1, y1);
}

/* Transferencias en ráfaga alineadas de 64 bits */
static inline void fast_fill_span(uint32_t *dst, uint32_t color, int32_t count)
{
    if (count <= 0) return;
    uint64_t c2 = ((uint64_t)color << 32) | (uint64_t)color;

    if (((uintptr_t)dst & 7) != 0) {
        *dst++ = color;
        count--;
    }

    size_t qwords = (size_t)count / 2;
    if (qwords > 0) {
#if defined(__x86_64__)
        __asm__ volatile (
            "cld\n\t"
            "rep stosq"
            : "+D"(dst), "+c"(qwords)
            : "a"(c2)
            : "memory"
        );
#else
        uint64_t *d64 = (uint64_t *)dst;
        while (qwords >= 4) {
            d64[0] = c2; d64[1] = c2; d64[2] = c2; d64[3] = c2;
            d64 += 4; qwords -= 4;
        }
        while (qwords--) *d64++ = c2;
        dst = (uint32_t *)d64;
#endif
    }
    if (count & 1) {
        *dst = color;
    }
}

static inline void fast_copy_span(volatile uint32_t *dst, const uint32_t *src, int32_t count)
{
    if (count <= 0) return;

    if (((uintptr_t)dst & 7) == ((uintptr_t)src & 7)) {
        if (((uintptr_t)dst & 7) != 0) {
            *dst++ = *src++;
            count--;
        }
        size_t qwords = (size_t)count / 2;
        if (qwords > 0) {
#if defined(__x86_64__)
            __asm__ volatile (
                "cld\n\t"
                "rep movsq"
                : "+D"(dst), "+S"(src), "+c"(qwords)
                :
                : "memory"
            );
#else
            volatile uint64_t *d64 = (volatile uint64_t *)dst;
            const uint64_t *s64 = (const uint64_t *)src;
            while (qwords >= 4) {
                d64[0] = s64[0]; d64[1] = s64[1]; d64[2] = s64[2]; d64[3] = s64[3];
                d64 += 4; s64 += 4; qwords -= 4;
            }
            while (qwords--) *d64++ = *s64++;
            dst = (volatile uint32_t *)d64;
            src = (const uint32_t *)s64;
#endif
        }
        if (count & 1) {
            *dst = *src;
        }
    } else {
        while (count--) *dst++ = *src++;
    }
}

static void fb_cguard_present(void);
static void fb_cguard_drop(void);
/* Presentación a VRAM con superposición de cursor */
/* PARCHE 082: volcado de un rectangulo grande repartido por filas entre los nucleos (sobre todo la conversion a 16 bits de la Pi 5) */
struct fb_rows_job { int32_t x0, y0, w, h; };
static void fb_rows_part(int idx, int n, void *arg)
{
    struct fb_rows_job *j = (struct fb_rows_job *)arg;
    int32_t a = (int32_t)((int64_t)j->h * idx / n), b = (int32_t)((int64_t)j->h * (idx + 1) / n);
    for (int32_t r = a; r < b; ++r) {
        uint32_t off = (uint32_t)((j->y0 + r) * fb_cur_w + j->x0);
        if (!FB_HW_NATIVE()) { fb_hw_put_span(j->x0, j->y0 + r, fb_backbuffer + off, j->w); continue; }
        fast_copy_span(fb_vram + off, fb_backbuffer + off, j->w);
        FB_SYNC(fb_vram + off, (size_t)j->w * 4);
    }
}
static uint64_t fb_par_rects;
uint64_t fb_parallel_rects(void) { return fb_par_rects; }

/* ================================================================== PARCHE 085: ventanas translucidas
 * La ventana activa se repinta sola muchas veces (cada app dibuja directamente en el backbuffer). Para mezclarla con lo que tiene debajo
 * sin "desteñir" lo ya mezclado se guardan dos capas del tamano de la pantalla: lo que habia debajo (tr_under, capturado justo antes de
 * dibujar la ventana) y el ultimo resultado mezclado (tr_last). En cada fb_present, dentro de la ventana y de los rectangulos sucios, un
 * pixel distinto de tr_last es nuevo (lo acaba de pintar la app): se mezcla con tr_under y se apunta en tr_last; uno igual ya estaba
 * mezclado y se deja. Las zonas excluidas (menus emergentes) quedan opacas. */
static uint32_t *tr_under, *tr_last; static uint32_t tr_cap;
static int32_t tr_x, tr_y, tr_w, tr_h; static uint32_t tr_alpha = 255; static int tr_fresh;
static int32_t tr_ex_x, tr_ex_y, tr_ex_w, tr_ex_h;
static int tr_alloc(void)
{
    uint32_t need = fb_cur_w * fb_cur_h;
    if (tr_under && tr_cap >= need) return 1;
    if (tr_under) kfree(tr_under);
    if (tr_last) kfree(tr_last);
    { uint8_t ow = kheap_owner; kheap_owner = 2; tr_under = (uint32_t *)kmalloc((size_t)need * 4); tr_last = (uint32_t *)kmalloc((size_t)need * 4); kheap_owner = ow; }
    if (!tr_under || !tr_last) { if (tr_under) kfree(tr_under); if (tr_last) kfree(tr_last); tr_under = tr_last = 0; tr_cap = 0; return 0; }
    tr_cap = need;
    return 1;
}
static int tr_clip(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > (int32_t)fb_cur_w) *w = (int32_t)fb_cur_w - *x;
    if (*y + *h > (int32_t)fb_cur_h) *h = (int32_t)fb_cur_h - *y;
    return *w > 0 && *h > 0;
}
static inline uint32_t tr_mix(uint32_t top, uint32_t under, uint32_t a)
{
    uint32_t ia = 255 - a;
    uint32_t rb = ((top & 0x00FF00FFu) * a + (under & 0x00FF00FFu) * ia + 0x00800080u) >> 8;
    uint32_t g = ((top & 0x0000FF00u) * a + (under & 0x0000FF00u) * ia + 0x00008000u) >> 8;
    return (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
}
typedef void (*fb_rows_fn)(void *ctx, int32_t y0, int32_t y1);
static void fb_rows_run(fb_rows_fn fn, void *ctx, int32_t y0, int32_t y1, int64_t area);
/* SOMA_164D_T480S: copia de un rectangulo de la pantalla por filas (antes pixel a pixel), repartida si es grande */
struct cp_ctx { uint32_t *dst; uint32_t dst_stride, dst_x0; int32_t x, w, y0; };
static void copy_rows(void *c, int32_t y0, int32_t y1)
{
    struct cp_ctx *k = (struct cp_ctx *)c;
    for (int32_t y = y0; y < y1; y++)
        memcpy(k->dst + (uint32_t)(y - k->y0) * k->dst_stride + k->dst_x0, fb_backbuffer + (uint32_t)y * fb_cur_w + (uint32_t)k->x, (size_t)k->w * 4);
}
void fb_tr_begin(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t alpha)
{
    tr_alpha = alpha > 255 ? 255 : alpha;
    if (tr_alpha >= 255 || !fb_backbuffer || !tr_clip(&x, &y, &w, &h) || !tr_alloc()) { tr_alpha = 255; return; }
    tr_x = x; tr_y = y; tr_w = w; tr_h = h; tr_fresh = 1;
    struct cp_ctx k = { tr_under + (uint32_t)y * fb_cur_w, fb_cur_w, (uint32_t)x, x, w, y };
    fb_rows_run(copy_rows, &k, y, y + h, (int64_t)w * h);
}
void fb_tr_off(void) { tr_alpha = 255; }
void fb_tr_release(void) { tr_alpha = 255; if (tr_under) kfree(tr_under); if (tr_last) kfree(tr_last); tr_under = tr_last = 0; tr_cap = 0; }   /* PARCHE 086 */
void fb_tr_exclude(int32_t x, int32_t y, int32_t w, int32_t h) { tr_ex_x = x; tr_ex_y = y; tr_ex_w = w; tr_ex_h = h; }
/* Mezcla inmediata de una ventana que no se volvera a pintar sola (las de detras): under = copia previa del rectangulo */
uint32_t *fb_rect_save(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!fb_backbuffer || !tr_clip(&x, &y, &w, &h)) return 0;
    uint32_t *b = (uint32_t *)kmalloc((size_t)w * h * 4); if (!b) return 0;
    struct cp_ctx k = { b, (uint32_t)w, 0, x, w, y };
    fb_rows_run(copy_rows, &k, y, y + h, (int64_t)w * h);
    return b;
}
void fb_rect_blend_saved(uint32_t *saved, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t alpha)
{
    if (!saved) return;
    if (fb_backbuffer && alpha < 255 && tr_clip(&x, &y, &w, &h))
        for (int32_t r = 0; r < h; r++) for (int32_t c = 0; c < w; c++) { uint32_t o = (uint32_t)(y + r) * fb_cur_w + (uint32_t)(x + c); fb_backbuffer[o] = tr_mix(fb_backbuffer[o], saved[r * w + c], alpha); }
    kfree(saved);
}
static void tr_process_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x0 < tr_x) x0 = tr_x;
    if (y0 < tr_y) y0 = tr_y;
    if (x1 > tr_x + tr_w) x1 = tr_x + tr_w;
    if (y1 > tr_y + tr_h) y1 = tr_y + tr_h;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t a = tr_alpha;
    for (int32_t y = y0; y < y1; y++) {
        int ey = tr_ex_w > 0 && y >= tr_ex_y && y < tr_ex_y + tr_ex_h;
        uint32_t o = (uint32_t)y * fb_cur_w;
        for (int32_t x = x0; x < x1; x++) {
            uint32_t c = fb_backbuffer[o + (uint32_t)x];
            if (!tr_fresh && c == tr_last[o + (uint32_t)x]) continue;
            if (ey && x >= tr_ex_x && x < tr_ex_x + tr_ex_w) { tr_last[o + (uint32_t)x] = c; continue; }
            uint32_t m = tr_mix(c, tr_under[o + (uint32_t)x], a);
            fb_backbuffer[o + (uint32_t)x] = m; tr_last[o + (uint32_t)x] = m;
        }
    }
}
/* SOMA_164B_T480S: la mezcla de la ventana translucida por franjas de filas entre los nucleos (cada pixel es independiente) */
struct tr_job { int32_t x0, y0, x1, y1; };
static void tr_rows_part(int idx, int n, void *arg)
{
    struct tr_job *j = (struct tr_job *)arg;
    int32_t h = j->y1 - j->y0;
    tr_process_rect(j->x0, j->y0 + (int32_t)((int64_t)h * idx / n), j->x1, j->y0 + (int32_t)((int64_t)h * (idx + 1) / n));
}
static void tr_process_rect_par(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if ((int64_t)(x1 - x0) * (y1 - y0) >= 32768 && smp_cpus() > 1) { struct tr_job j = { x0, y0, x1, y1 }; smp_parallel(tr_rows_part, &j); }
    else tr_process_rect(x0, y0, x1, y1);
}
static void tr_present(void)
{
    if (tr_alpha >= 255 || !tr_under || !fb_backbuffer) return;
    if (tr_fresh) { tr_process_rect_par(tr_x, tr_y, tr_x + tr_w, tr_y + tr_h); fb_damage(tr_x, tr_y, tr_w, tr_h); tr_fresh = 0; }
    else for (int i = 0; i < dirty_count; i++) tr_process_rect_par(dirty_rects[i].x0, dirty_rects[i].y0, dirty_rects[i].x1, dirty_rects[i].y1);
    tr_ex_w = 0;
}

/* PARCHE 089: datos para el Monitor del sistema */
static uint64_t fb_presents;
void fb_get_info(struct fb_info *o)
{
    memset(o, 0, sizeof *o);
    o->w = fb_cur_w; o->h = fb_cur_h;
    o->bpp = fb_hw_bpp ? fb_hw_bpp : 32; o->pitch = fb_hw_pitch ? fb_hw_pitch : fb_cur_w * 4;
    o->phys = (uint64_t)(uintptr_t)fb_vram; o->backbuffer_bytes = fb_bb_bytes; o->presents = fb_presents; o->active = fb_active;
    o->tr_bytes = (uint64_t)tr_cap * 8u;
#ifdef __aarch64__
    o->driver = "VideoCore (buzon de la Pi)";
#else
    if (fb_fw_mode) { o->driver = bootinfo_efi() ? "Firmware UEFI GOP (framebuffer lineal)" : "Firmware VBE (framebuffer lineal)"; o->phys = bootfb_get()->addr; }
    else { uint16_t id = bga_read(BGA_REG_ID); o->driver = (id & 0xFFF0) == 0xB0C0 ? "Bochs VBE (BGA)" : "VGA"; o->hw_id = id; o->phys = fb_phys_base; }
#endif
}

uint64_t fb_dirty_px; uint32_t fb_dirty_n;
#if defined(__x86_64__)
/* ---- SOMA_225_AJUSTES: volcado escalado ---- */
static inline uint32_t sc_lerp(uint32_t a, uint32_t b, uint32_t w)       /* w 0..256 */
{
    uint32_t iw = 256u - w;
    uint32_t rb = (((a & 0xFF00FFu) * iw + (b & 0xFF00FFu) * w) >> 8) & 0xFF00FFu;
    uint32_t g  = (((a & 0x00FF00u) * iw + (b & 0x00FF00u) * w) >> 8) & 0x00FF00u;
    return rb | g;
}
static inline uint32_t sc_px(int32_t hx, int32_t hy)
{
    uint32_t xt = fb_sc_xt[hx], yt = fb_sc_yt[hy];
    uint32_t sx = xt >> 8, wx = xt & 255u, sy = yt >> 8, wy = yt & 255u;
    uint32_t sx1 = sx + 1 < fb_cur_w ? sx + 1 : sx, sy1 = sy + 1 < fb_cur_h ? sy + 1 : sy;
    const uint32_t *r0 = fb_backbuffer + sy * fb_cur_w, *r1 = fb_backbuffer + sy1 * fb_cur_w;
    uint32_t top = wx ? sc_lerp(r0[sx], r0[sx1], wx) : r0[sx];
    if (!wy) return top;
    uint32_t bot = wx ? sc_lerp(r1[sx], r1[sx1], wx) : r1[sx];
    return sc_lerp(top, bot, wy);
}
static void sc_row(int32_t hy, int32_t hx0, int32_t hx1)
{
    uint32_t buf[256];
    int native = fb_hw_bpp == 32 && !fb_swap_rb();
    for (int32_t x = hx0; x < hx1; ) {
        int32_t n = hx1 - x; if (n > 256) n = 256;
        for (int32_t i = 0; i < n; i++) buf[i] = sc_px(x + i, hy);
        if (native) {
            volatile uint32_t *d = (volatile uint32_t *)((volatile uint8_t *)fb_vram + (uint64_t)hy * fb_hw_pitch) + x;
            fast_copy_span(d, buf, n);
        } else fb_hw_put_span(x, hy, buf, n);
        x += n;
    }
}
struct sc_job { int32_t x0, y0, x1, y1; };
static void sc_rows_part(int idx, int n, void *arg)
{
    struct sc_job *j = (struct sc_job *)arg;
    int32_t h = j->y1 - j->y0, a = j->y0 + (int32_t)((int64_t)h * idx / n), b = j->y0 + (int32_t)((int64_t)h * (idx + 1) / n);
    for (int32_t y = a; y < b; y++) sc_row(y, j->x0, j->x1);
}
static void sc_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* rectangulo logico -> pantalla */
{
    int32_t hx0 = (int32_t)((int64_t)x0 * fb_hw_w / fb_cur_w) - 1, hy0 = (int32_t)((int64_t)y0 * fb_hw_h / fb_cur_h) - 1;
    int32_t hx1 = (int32_t)(((int64_t)x1 * fb_hw_w + fb_cur_w - 1) / fb_cur_w) + 1, hy1 = (int32_t)(((int64_t)y1 * fb_hw_h + fb_cur_h - 1) / fb_cur_h) + 1;
    if (hx0 < 0) hx0 = 0;
    if (hy0 < 0) hy0 = 0;
    if (hx1 > (int32_t)fb_hw_w) hx1 = (int32_t)fb_hw_w;
    if (hy1 > (int32_t)fb_hw_h) hy1 = (int32_t)fb_hw_h;
    if (hx1 <= hx0 || hy1 <= hy0) return;
    fb_dirty_px += (uint64_t)((int64_t)(hx1 - hx0) * (hy1 - hy0));
    if ((int64_t)(hx1 - hx0) * (hy1 - hy0) >= 32768 && smp_cpus() > 1) {
        struct sc_job j = { hx0, hy0, hx1, hy1 };
        smp_parallel(sc_rows_part, &j);
        fb_par_rects++;
        return;
    }
    for (int32_t y = hy0; y < hy1; y++) sc_row(y, hx0, hx1);
}
static int32_t sc_cur_hx, sc_cur_hy;
static void fb_present_scaled(void)
{
    int32_t hx = (int32_t)((int64_t)(cursor_pos_x + cursor_hot_x) * fb_hw_w / fb_cur_w) - cursor_hot_x;
    int32_t hy = (int32_t)((int64_t)(cursor_pos_y + cursor_hot_y) * fb_hw_h / fb_cur_h) - cursor_hot_y;
    if (cursor_drawn_visible && (cursor_drawn_x != hx || cursor_drawn_y != hy || !cursor_visible)) {
        int32_t lx = (int32_t)((int64_t)cursor_drawn_x * fb_cur_w / fb_hw_w) - 2, ly = (int32_t)((int64_t)cursor_drawn_y * fb_cur_h / fb_hw_h) - 2;
        fb_damage(lx, ly, cursor_overlay_w * (int32_t)fb_cur_w / (int32_t)fb_hw_w + 5, cursor_overlay_h * (int32_t)fb_cur_h / (int32_t)fb_hw_h + 5);
    }
    if (dirty_count > 0) {
        fb_dirty_n++;
        for (int i = 0; i < dirty_count; ++i) sc_rect(dirty_rects[i].x0, dirty_rects[i].y0, dirty_rects[i].x1, dirty_rects[i].y1);
        dirty_count = 0;
    }
    if (cursor_visible && cursor_overlay_img && cursor_overlay_w > 0 && cursor_overlay_h > 0) {
        int native = fb_hw_bpp == 32 && !fb_swap_rb();
        for (int32_t r = 0; r < cursor_overlay_h; ++r) {
            int32_t vy = hy + r;
            if (vy < 0 || vy >= (int32_t)fb_hw_h) continue;
            for (int32_t c = 0; c < cursor_overlay_w; ++c) {
                int32_t vx = hx + c;
                if (vx < 0 || vx >= (int32_t)fb_hw_w) continue;
                uint32_t fg = cursor_overlay_img[r * cursor_overlay_w + c];
                if ((fg >> 24) == 0) continue;
                uint32_t px = fb_blend_color(sc_px(vx, vy), fg);
                if (native) ((volatile uint32_t *)((volatile uint8_t *)fb_vram + (uint64_t)vy * fb_hw_pitch))[vx] = px;
                else fb_hw_put_span(vx, vy, &px, 1);
            }
        }
        cursor_drawn_x = hx; cursor_drawn_y = hy; cursor_drawn_visible = 1;
    } else cursor_drawn_visible = 0;
    (void)sc_cur_hx; (void)sc_cur_hy;
}
#endif
uint32_t fb_hw_width(void)  { return fb_sc_on ? fb_hw_w : fb_cur_w; }
uint32_t fb_hw_height(void) { return fb_sc_on ? fb_hw_h : fb_cur_h; }
int fb_scaled(void) { return fb_sc_on; }
/* SOMA_225_AJUSTES: resolucion logica; 0 o la nativa = sin escalar. Devuelve 0 si se aplico. */
int fb_set_logical(uint32_t lw, uint32_t lh)
{
#if defined(__x86_64__)
    if (!fb_mapped || !fb_backbuffer || fb_user_owned) return -1;
    uint32_t hw = fb_sc_on ? fb_hw_w : fb_cur_w, hh = fb_sc_on ? fb_hw_h : fb_cur_h;
    if (!lw || !lh || lw >= hw || lh >= hh) { lw = hw; lh = hh; }
    if (lw < 640u) lw = 640u;
    if (lh < 400u) lh = 400u;
    if (lw == hw && lh == hh) {
        if (!fb_sc_on) return 0;
        fb_sc_on = 0; fb_cur_w = hw; fb_cur_h = hh;
    } else {
        uint32_t *xt = (uint32_t *)kmalloc((size_t)(hw + hh) * 4u);
        if (!xt) return -2;
        for (uint32_t x = 0; x < hw; x++) {
            int32_t f = (int32_t)(((int64_t)(2 * x + 1) * lw * 256) / (2 * (int64_t)hw)) - 128; if (f < 0) f = 0;
            uint32_t sx = (uint32_t)f >> 8, w = (uint32_t)f & 255u; if (sx >= lw - 1) { sx = lw - 1; w = 0; }
            xt[x] = (sx << 8) | w;
        }
        uint32_t *yt = xt + hw;
        for (uint32_t y = 0; y < hh; y++) {
            int32_t f = (int32_t)(((int64_t)(2 * y + 1) * lh * 256) / (2 * (int64_t)hh)) - 128; if (f < 0) f = 0;
            uint32_t sy = (uint32_t)f >> 8, w = (uint32_t)f & 255u; if (sy >= lh - 1) { sy = lh - 1; w = 0; }
            yt[y] = (sy << 8) | w;
        }
        uint32_t *old = fb_sc_xt;
        fb_sc_on = 0;                                   /* sin volcados a medias mientras cambian las tablas */
        fb_sc_xt = xt; fb_sc_yt = yt;
        if (old) kfree(old);
        fb_hw_w = hw; fb_hw_h = hh; fb_cur_w = lw; fb_cur_h = lh;
        fb_sc_on = 1;
    }
    fb_reset_clip();
    dirty_count = 0; cursor_drawn_visible = 0;
    fb_clear(COLOR_DEEP_NAVY);
    fb_present_full();
    return 0;
#else
    (void)lw; (void)lh; return -1;
#endif
}

#ifndef SOMA_WEBSVC
void voz_ptt_overlay_draw(void) __attribute__((weak));   /* SOMA_245_PTT: pastilla del dictado (src/ui/gui_voz.c) */
#endif
void fb_present(void)
{
    if (!fb_mapped) return;
    fb_presents++;
    if (!fb_user_exclusive()) tr_present();          /* PARCHE 085: antes que las esquinas y los avisos (esos van opacos) */
    if (!fb_user_exclusive()) fb_cguard_present();   /* SOMA_PARCHE_028_UI: esquinas redondeadas de la ventana activa */
    if (!fb_user_exclusive()) clip_overlay_draw();   /* SOMA_PARCHE_PORTAPAPELES: avisos y panel del portapapeles */
#ifndef SOMA_WEBSVC
    if (!fb_user_exclusive() && voz_ptt_overlay_draw) voz_ptt_overlay_draw();   /* SOMA_245_PTT */
#endif

#if defined(__x86_64__)
    if (fb_backbuffer && fb_sc_on) { fb_present_scaled(); return; }   /* SOMA_225_AJUSTES */
#endif
    if (fb_backbuffer) {
        /* Si el cursor cambió de posición o visibilidad, restaurar el fondo viejo en VRAM */
        if (cursor_drawn_visible && (cursor_drawn_x != cursor_pos_x || cursor_drawn_y != cursor_pos_y || !cursor_visible)) {
            fb_damage(cursor_drawn_x, cursor_drawn_y, cursor_overlay_w, cursor_overlay_h);
        }

        /* 1. Volcar sólo los rectángulos sucios */
        if (dirty_count > 0) {
            fb_dirty_n++;
            for (int i = 0; i < dirty_count; ++i) {
                int32_t x0 = dirty_rects[i].x0;
                int32_t y0 = dirty_rects[i].y0;
                int32_t w  = dirty_rects[i].x1 - x0;
                int32_t h  = dirty_rects[i].y1 - y0;
                fb_dirty_px += (uint64_t)((int64_t)w * h);       /* SOMA_209: para 'orbe' (parte de la pantalla que se vuelca) */
                if ((int64_t)w * h >= 32768 && smp_cpus() > 1) {
                    struct fb_rows_job j = { x0, y0, w, h };
                    smp_parallel(fb_rows_part, &j);
                    fb_par_rects++;
                    continue;
                }

                for (int32_t r = 0; r < h; ++r) {
                    uint32_t off = (uint32_t)((y0 + r) * fb_cur_w + x0);
                    if (!FB_HW_NATIVE()) { fb_hw_put_span(x0, y0 + r, fb_backbuffer + off, w); continue; }
                    fast_copy_span(fb_vram + off, fb_backbuffer + off, w);
                    FB_SYNC(fb_vram + off, (size_t)w * 4);
                }
            }
            dirty_count = 0;
        }

        /* 2. Superponer el cursor en VRAM sin tocar el backbuffer */
        if (cursor_visible && cursor_overlay_img && cursor_overlay_w > 0 && cursor_overlay_h > 0) {
            for (int32_t r = 0; r < cursor_overlay_h; ++r) {
                int32_t vy = cursor_pos_y + r;
                if (vy < 0 || vy >= (int32_t)fb_cur_h) continue;

                for (int32_t c = 0; c < cursor_overlay_w; ++c) {
                    int32_t vx = cursor_pos_x + c;
                    if (vx < 0 || vx >= (int32_t)fb_cur_w) continue;

                    uint32_t fg = cursor_overlay_img[r * cursor_overlay_w + c];
                    if ((fg >> 24) != 0) {
                        uint32_t off = (uint32_t)(vy * fb_cur_w + vx);
                        uint32_t bg = fb_backbuffer[off];
                        if (!FB_HW_NATIVE()) { uint32_t px = fb_blend_color(bg, fg); fb_hw_put_span(vx, vy, &px, 1); continue; }
                        fb_vram[off] = fb_blend_color(bg, fg);
                        FB_SYNC(&fb_vram[off], 4);
                    }
                }
            }
            cursor_drawn_x = cursor_pos_x;
            cursor_drawn_y = cursor_pos_y;
            cursor_drawn_visible = 1;
        } else {
            cursor_drawn_visible = 0;
        }
    }
}

void fb_present_full(void)
{
    fb_damage(0, 0, (int32_t)fb_cur_w, (int32_t)fb_cur_h);
    fb_present();
}

void fb_cursor_set_image(const uint32_t *argb, int32_t w, int32_t h)
{
    cursor_overlay_img = argb;
    cursor_overlay_w = w;
    cursor_overlay_h = h;
}

void fb_cursor_set_hotspot(int32_t hx, int32_t hy) { cursor_hot_x = hx; cursor_hot_y = hy; }
void fb_cursor_show(int32_t x, int32_t y)
{
    cursor_pos_x = x - cursor_hot_x;
    cursor_pos_y = y - cursor_hot_y;
    cursor_visible = 1;
}

void fb_cursor_hide(void)
{
    cursor_visible = 0;
}

int fb_set_mode(uint32_t width, uint32_t height)
{
#if defined(__x86_64__) || defined(__aarch64__)
#ifdef __aarch64__
    {                                                /* PARCHE 063: framebuffer del VideoCore (puede dar menos de lo pedido) */
        volatile uint32_t *v = 0; uint32_t gw = 0, gh = 0;
        uint32_t gb = 32, gp = 0;
        if (rpi_fb_set_mode(width, height, &v, &gw, &gh, &gb, &gp) != 0) return -1;
        fb_vram = v; width = gw; height = gh; fb_hw_bpp = gb; fb_hw_pitch = gp;
        fb_mapped = 1;
    }
#else
    if (bootfb_use_firmware()) {
        /* SOMA_154_T480S: pantalla del firmware. No se puede cambiar de resolucion sin un controlador de la GPU (Intel UHD
         * 620): se usa la nativa que dejo el firmware/GRUB y la GUI se adapta (lee fb_get_width/height). */
        const struct bootfb_info *bf = bootfb_get();
        if (bootfb_map() != 0) return -2;
        fb_mapped = 1; fb_fw_mode = 1;
        fb_vram = (volatile uint32_t *)(uintptr_t)(FB_VIRT_ADDR + (bf->addr & 0xFFFull));
        width = bf->width; height = bf->height; fb_hw_bpp = bf->bpp; fb_hw_pitch = bf->pitch;
        bootfb_console_stop();                               /* la GUI toma la pantalla */
    } else {
    uint16_t id = bga_read(BGA_REG_ID);
    if ((id & 0xFFF0) != 0xB0C0) return -1;
    if (!fb_mapped) { if (fb_init() != 0) return -2; }

    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES, (uint16_t)width);
    bga_write(BGA_REG_YRES, (uint16_t)height);
    bga_write(BGA_REG_BPP, 32);
    bga_write(BGA_REG_BANK, 0);
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);
    fb_hw_bpp = 32; fb_hw_pitch = width * 4;
    }
#endif

    fb_sc_on = 0;                                    /* SOMA_225_AJUSTES: un modo nuevo vuelve a la resolucion nativa */
    fb_hw_w = width; fb_hw_h = height;
    fb_cur_w = width;
    fb_cur_h = height;
    fb_reset_clip();

    size_t bytes = (size_t)width * height * sizeof(uint32_t);
    /* FALLO CORREGIDO (parche 021): antes se reutilizaba el backbuffer del primer modo aunque el nuevo fuera MAYOR
     * (p. ej. 800x600 y despues 1920x1080): los dibujos desbordaban la memoria. */
    if (fb_backbuffer && bytes > fb_bb_bytes) {
        kfree(fb_backbuffer);
        fb_backbuffer = 0;
    }
    if (!fb_backbuffer) {
        { uint8_t ow = kheap_owner; kheap_owner = 2; fb_backbuffer = (uint32_t *)kmalloc(bytes); kheap_owner = ow; }   /* PARCHE 089: ACCT_SCREEN */
        fb_bb_bytes = fb_backbuffer ? bytes : 0;
    }
    if (fb_backbuffer) {
        memset((void *)fb_backbuffer, 0, bytes);
    }

    fb_active = 1;
    dirty_count = 0;
    cursor_drawn_visible = 0;
    fb_clear(COLOR_DEEP_NAVY);
    fb_present_full();
    return 0;
#else
    (void)width; (void)height; return -1;
#endif
}

/* ---- SOMA_PARCHE_021_FPU: pantalla en exclusiva para programas de usuario (video, juegos) ---- */
int fb_user_begin(uint32_t w, uint32_t h)
{
    if (fb_user_owned) return -16;
    if (w < 160 || h < 120 || (w & 7) || (uint64_t)w * h > 1920ULL * 1080ULL) return -22;
#ifdef __x86_64__
    if (bootfb_use_firmware() && (w != bootfb_get()->width || h != bootfb_get()->height)) return -19;   /* SOMA_154: sin cambio de modo */
#endif
    fb_prev_active = fb_active; fb_prev_w = fb_sc_on ? fb_hw_w : fb_cur_w; fb_prev_h = fb_sc_on ? fb_hw_h : fb_cur_h;
    fb_prev_lw = fb_sc_on ? fb_cur_w : 0; fb_prev_lh = fb_sc_on ? fb_cur_h : 0;   /* SOMA_225_AJUSTES */
    if (fb_set_mode(w, h) != 0) return -19;
    fb_clear(0);
    fb_present_full();
    fb_user_owned = 1;
    return 0;
}
void fb_user_end(void)
{
    if (!fb_user_owned) return;
    fb_user_owned = 0;
    if (fb_prev_active) { fb_set_mode(fb_prev_w, fb_prev_h); if (fb_prev_lw) fb_set_logical(fb_prev_lw, fb_prev_lh); }   /* la GUI sigue creyendo que esta resolucion es la suya */
    else fb_restore_text_mode();                                 /* sin GUI: se vuelve a la consola de texto */
}
int fb_user_exclusive(void) { return fb_user_owned; }

#ifdef __x86_64__
static const uint8_t vga_mode3_seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const uint8_t vga_mode3_crtc[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
};
static const uint8_t vga_mode3_gc[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
static const uint8_t vga_mode3_ac[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void vga_hardware_restore_mode3(void)
{
    outb(VGA_MISC_WRITE, 0x67);
    outb(VGA_SEQ_INDEX, 0x00); outb(VGA_SEQ_DATA, 0x01);
    for (uint8_t i = 1; i < 5; ++i) {
        outb(VGA_SEQ_INDEX, i); outb(VGA_SEQ_DATA, vga_mode3_seq[i]);
    }
    outb(VGA_SEQ_INDEX, 0x00); outb(VGA_SEQ_DATA, 0x03);

    outb(VGA_CRTC_INDEX, 0x11);
    uint8_t crtc11 = inb(VGA_CRTC_DATA);
    outb(VGA_CRTC_DATA, crtc11 & 0x7F);

    for (uint8_t i = 0; i < 25; ++i) {
        outb(VGA_CRTC_INDEX, i); outb(VGA_CRTC_DATA, vga_mode3_crtc[i]);
    }
    for (uint8_t i = 0; i < 9; ++i) {
        outb(VGA_GC_INDEX, i); outb(VGA_GC_DATA, vga_mode3_gc[i]);
    }
    inb(VGA_INSTAT_READ);
    for (uint8_t i = 0; i < 21; ++i) {
        outb(VGA_AC_INDEX, i); outb(VGA_AC_INDEX, vga_mode3_ac[i]);
    }
    outb(VGA_AC_INDEX, 0x20);

    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x04);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x06);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x04);

    volatile uint8_t *font_dest = (volatile uint8_t *)0xA0000;
    for (int ch = 0; ch < 96; ++ch) {
        const uint8_t *glyph = font_get_glyph(ch + 32);
        for (int row = 0; row < 16; ++row) {
            font_dest[(ch + 32) * 32 + row] = glyph[row / 2];
        }
    }

    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x03);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x10);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x0E);
}
#endif

void fb_restore_text_mode(void)
{
#ifdef __x86_64__
    if (fb_fw_mode) { fb_active = 0; bootfb_console_resume(); return; }   /* SOMA_154: no hay modo texto VGA en UEFI */
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    vga_hardware_restore_mode3();
    fb_active = 0;
    console_clear();
#endif
}

int fb_is_active(void) { return fb_active; }

/* ====================================================================
 * PRIMITIVAS 2D DE ALTO RENDIMIENTO
 * ==================================================================== */

struct fb_clear_job { uint32_t *p; uint32_t color, n; };
static void fb_clear_part(int idx, int n, void *arg)
{
    struct fb_clear_job *j = (struct fb_clear_job *)arg;
    uint32_t a = (uint32_t)((uint64_t)j->n * (uint32_t)idx / (uint32_t)n) & ~15u, b = idx == n - 1 ? j->n : (uint32_t)((uint64_t)j->n * (uint32_t)(idx + 1) / (uint32_t)n) & ~15u;
    if (b > a) fast_fill_span(j->p + a, j->color, (int32_t)(b - a));
}
void fb_clear(uint32_t color)
{
    if (!fb_active) return;
    fb_cguard_drop();                /* SOMA_PARCHE_028_UI: el fondo guardado ya no vale */
    uint32_t *target = fb_target();
    uint32_t total = fb_cur_w * fb_cur_h;
    if (total >= 1024u * 1024u && smp_cpus() > 1) {               /* SOMA_164B_T480S: pantalla entera repartida entre los nucleos */
        struct fb_clear_job j = { target, color, total };
        smp_parallel(fb_clear_part, &j);
    } else fast_fill_span(target, color, (int32_t)total);
    fb_damage(0, 0, (int32_t)fb_cur_w, (int32_t)fb_cur_h);
    drawn_count = 0;                 /* todo es fondo ahora */
    drawn_valid = 1;
    drawn_bg = color;
}

/* Borra solo lo dibujado desde el ultimo borrado (y lo marca para presentar). Si no hay garantia
 * de que el resto sea fondo, hace un borrado completo. El llamador debe dibujar TODO su contenido
 * mediante primitivas que avisen del dano (o fb_damage) para que esto sea correcto. */
void fb_clear_changed(uint32_t color)
{
    if (!fb_active) return;
    if (!drawn_valid || color != drawn_bg) { fb_clear(color); return; }
    fb_cguard_drop();

    uint32_t *target = fb_target();
    struct fb_dirty_rect snap[FB_MAX_DIRTY_RECTS];
    int n = drawn_count;

    for (int i = 0; i < n; ++i) snap[i] = drawn_rects[i];
    drawn_count = 0;
    for (int i = 0; i < n; ++i) {
        int32_t w = snap[i].x1 - snap[i].x0;
        for (int32_t y = snap[i].y0; y < snap[i].y1; ++y)
            fast_fill_span(target + y * fb_cur_w + snap[i].x0, color, w);
        /* hay que presentarlo (ahora es fondo), pero no cuenta como "dibujado" */
        rect_add(dirty_rects, &dirty_count, snap[i].x0, snap[i].y0, snap[i].x1, snap[i].y1);
    }
}

/* ---- Verificador (depuracion): pixeles distintos del fondo FUERA de lo marcado como sucio ---- */
volatile int fb_verify_enabled = 0;
static uint32_t verify_frames, verify_bad_total, verify_bad_worst;

int32_t fb_verify_box[4] = { 1 << 30, 1 << 30, -1, -1 };   /* SOMA_209: caja de los pixeles sueltos vistos */
uint32_t fb_debug_verify(uint32_t bg)
{
    if (!fb_active || !fb_backbuffer) return 0;
    uint32_t bad = 0;
    for (uint32_t y = 0; y < fb_cur_h; ++y) {
        for (uint32_t x = 0; x < fb_cur_w; ++x) {
            if (((fb_backbuffer[y * fb_cur_w + x] ^ bg) & 0x00FFFFFFu) == 0) continue;
            int inside = 0;
            for (int i = 0; i < dirty_count; ++i) {
                if ((int32_t)x >= dirty_rects[i].x0 && (int32_t)x < dirty_rects[i].x1 &&
                    (int32_t)y >= dirty_rects[i].y0 && (int32_t)y < dirty_rects[i].y1) { inside = 1; break; }
            }
            if (!inside) { bad++; if ((int32_t)x < fb_verify_box[0]) fb_verify_box[0] = (int32_t)x; if ((int32_t)y < fb_verify_box[1]) fb_verify_box[1] = (int32_t)y; if ((int32_t)x > fb_verify_box[2]) fb_verify_box[2] = (int32_t)x; if ((int32_t)y > fb_verify_box[3]) fb_verify_box[3] = (int32_t)y; }
        }
    }
    verify_frames++;
    verify_bad_total += bad;
    if (bad > verify_bad_worst) verify_bad_worst = bad;
    return bad;
}

/* SOMA_209: pixeles en los que la pantalla real no coincide con el backbuffer tras un volcado (sin contar el puntero). Solo para
 * comprobar el volcado por zonas; leer la VRAM es lento. */
uint32_t fb_debug_vram_diff(void)
{
    if (!fb_mapped || !fb_backbuffer || !FB_HW_NATIVE() || fb_sc_on) return 0;
    uint32_t bad = 0;
    for (uint32_t y = 0; y < fb_cur_h; ++y) for (uint32_t x = 0; x < fb_cur_w; ++x) {
        if (cursor_drawn_visible && (int32_t)x >= cursor_drawn_x && (int32_t)x < cursor_drawn_x + cursor_overlay_w &&
            (int32_t)y >= cursor_drawn_y && (int32_t)y < cursor_drawn_y + cursor_overlay_h) continue;
        if ((fb_vram[y * fb_cur_w + x] ^ fb_backbuffer[y * fb_cur_w + x]) & 0x00FFFFFFu) bad++;
    }
    return bad;
}

void fb_debug_stats(uint32_t *frames, uint32_t *bad_total, uint32_t *bad_worst)
{
    if (frames) *frames = verify_frames;
    if (bad_total) *bad_total = verify_bad_total;
    if (bad_worst) *bad_worst = verify_bad_worst;
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!fb_active) return;
    if ((int32_t)x < clip_x0 || (int32_t)x >= clip_x1 ||
        (int32_t)y < clip_y0 || (int32_t)y >= clip_y1) return;

    fb_target()[y * fb_cur_w + x] = color;
    fb_damage((int32_t)x, (int32_t)y, 1, 1);
}

/* SOMA_164D_T480S: rellenos, cajas redondeadas y sombras grandes por franjas de filas entre los nucleos (cada fila es
 * independiente; mismo resultado). Solo para areas grandes: en las pequenas no compensa despertar a los demas. */
#define FB_PAR_MIN_PX 65536
struct fb_rows_par { fb_rows_fn fn; void *ctx; int32_t y0, y1; };
static void fb_rows_par_part(int idx, int n, void *arg)
{
    struct fb_rows_par *j = (struct fb_rows_par *)arg;
    int32_t h = j->y1 - j->y0;
    j->fn(j->ctx, j->y0 + (int32_t)((int64_t)h * idx / n), j->y0 + (int32_t)((int64_t)h * (idx + 1) / n));
}
static void fb_rows_run_min(fb_rows_fn fn, void *ctx, int32_t y0, int32_t y1, int64_t area, int64_t min)
{
    if (area >= min && y1 - y0 >= 16 && smp_cpus() > 1) { struct fb_rows_par j = { fn, ctx, y0, y1 }; smp_parallel(fb_rows_par_part, &j); }
    else fn(ctx, y0, y1);
}
static void fb_rows_run(fb_rows_fn fn, void *ctx, int32_t y0, int32_t y1, int64_t area) { fb_rows_run_min(fn, ctx, y0, y1, area, FB_PAR_MIN_PX); }
struct fr_ctx { uint32_t *t; int32_t cx, cw; uint32_t color; };
static void fill_rows(void *c, int32_t y0, int32_t y1)
{
    struct fr_ctx *f = (struct fr_ctx *)c;
    for (int32_t y = y0; y < y1; ++y) fast_fill_span(f->t + (uint32_t)y * fb_cur_w + (uint32_t)f->cx, f->color, f->cw);
}
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!fb_active) return;
    int32_t cx = (int32_t)x, cy = (int32_t)y, cw = (int32_t)w, ch = (int32_t)h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    struct fr_ctx f = { fb_target(), cx, cw, color };
    fb_rows_run_min(fill_rows, &f, cy, cy + ch, (int64_t)cw * ch, 4 * FB_PAR_MIN_PX);   /* relleno simple: solo si es muy grande */
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!fb_active || w == 0 || h == 0) return;
    fb_fill_rect(x, y, w, 2, color);
    fb_fill_rect(x, y + h - 2, w, 2, color);
    fb_fill_rect(x, y, 2, h, color);
    fb_fill_rect(x + w - 2, y, 2, h, color);
}

/* ====================================================================
 * COMPOSICIÓN ALFA
 * ==================================================================== */

uint32_t fb_blend_color(uint32_t bg, uint32_t fg)
{
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0) return bg;
    if (a == 255) return fg | 0xFF000000;

    uint32_t bg_r = (bg >> 16) & 0xFF;
    uint32_t bg_g = (bg >> 8)  & 0xFF;
    uint32_t bg_b = bg & 0xFF;

    uint32_t fg_r = (fg >> 16) & 0xFF;
    uint32_t fg_g = (fg >> 8)  & 0xFF;
    uint32_t fg_b = fg & 0xFF;

    uint32_t inv_a = 255 - a;

    /* Fórmula entera exacta de división por 255 */
    uint32_t r = ((fg_r * a + bg_r * inv_a + 1) + ((fg_r * a + bg_r * inv_a + 1) >> 8)) >> 8;
    uint32_t g = ((fg_g * a + bg_g * inv_a + 1) + ((fg_g * a + bg_g * inv_a + 1) >> 8)) >> 8;
    uint32_t b = ((fg_b * a + bg_b * inv_a + 1) + ((fg_b * a + bg_b * inv_a + 1) >> 8)) >> 8;

    return (r << 16) | (g << 8) | b | 0xFF000000;
}

static void alpha_rows(void *c, int32_t y0, int32_t y1)
{
    struct fr_ctx *f = (struct fr_ctx *)c;
    for (int32_t y = y0; y < y1; ++y) {
        uint32_t *row = f->t + (uint32_t)y * fb_cur_w + (uint32_t)f->cx;
        for (int32_t i = 0; i < f->cw; ++i) row[i] = fb_blend_color(row[i], f->color);
    }
}
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (!fb_active) return;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    struct fr_ctx f = { fb_target(), cx, cw, color };
    fb_rows_run(alpha_rows, &f, cy, cy + ch, (int64_t)cw * ch);   /* SOMA_168B: velo de pantalla completa repartido */
}

/* ====================================================================
 * GEOMETRÍA: BORDES REDONDEADOS, CÍRCULOS Y DEGRADADOS
 * ==================================================================== */

/* ====================================================================
 * SOMA_PARCHE_028_UI: geometria suavizada (antialias), sombras difusas y esquinas redondeadas de ventana.
 * Solo enteros. Todas respetan el recorte (scissor) y registran el area sucia.
 * ==================================================================== */
static uint32_t ui_isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > v) bit >>= 2;
    while (bit) { if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (uint32_t)r;
}
/* distancia*256 del centro del pixel (i,j) de la esquina al centro del arco de radio r */
static inline int32_t ui_corner_d256(int32_t r, int32_t i, int32_t j)
{
    int32_t a = 2 * (r - i) - 1, b = 2 * (r - j) - 1;
    return (int32_t)ui_isqrt64(((uint64_t)((int64_t)a * a + (int64_t)b * b)) << 14);
}
static inline uint32_t ui_cov(int32_t r256, int32_t d256)       /* cobertura 0..256 de un disco de radio r256/256 */
{
    int32_t c = r256 + 128 - d256;
    return c <= 0 ? 0u : (c >= 256 ? 256u : (uint32_t)c);
}
static inline void ui_mix(uint32_t *p, uint32_t rgb, uint32_t a)   /* a: 0..256 */
{
    if (a >= 256u) { *p = rgb & 0x00FFFFFFu; return; }
    if (!a) return;
    uint32_t d = *p, ia = 256u - a;
    uint32_t rb = (((rgb & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
    uint32_t g  = (((rgb & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
    *p = rb | g;
}
static inline void ui_px(int32_t x, int32_t y, uint32_t rgb, uint32_t a)
{
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) return;
    ui_mix(fb_target() + (uint32_t)y * fb_cur_w + (uint32_t)x, rgb, a);
}
static void ui_span(int32_t x0, int32_t x1, int32_t y, uint32_t rgb, uint32_t a)   /* [x0,x1) */
{
    if (y < clip_y0 || y >= clip_y1) return;
    if (x0 < clip_x0) x0 = clip_x0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (x1 <= x0 || !a) return;
    uint32_t *row = fb_target() + (uint32_t)y * fb_cur_w;
    if (a >= 256u) { fast_fill_span(row + x0, rgb & 0x00FFFFFFu, x1 - x0); return; }
    for (int32_t x = x0; x < x1; ++x) ui_mix(row + x, rgb, a);
}
/* SOMA_190: primer i (0..r) de la fila j de la esquina con cobertura > 0 (busqueda binaria: la distancia decrece con i) */
static inline int32_t ui_corner_first(int32_t r, int32_t j)
{
    int32_t lo = 0, hi = r;
    while (lo < hi) {
        int32_t m = (lo + hi) >> 1;
        if (ui_cov(r * 256, ui_corner_d256(r, m, j)) > 0u) hi = m; else lo = m + 1;
    }
    return lo;
}
/* relleno redondeado con antialias; alpha 0..256 */
struct rrf_ctx { int32_t x, y, w, h, r; uint32_t rgb, alpha; };
static void rr_fill_rows(void *ctx, int32_t y0, int32_t y1)
{
    struct rrf_ctx *c = (struct rrf_ctx *)ctx;
    int32_t x = c->x, y = c->y, w = c->w, h = c->h, r = c->r; uint32_t rgb = c->rgb, alpha = c->alpha;
    for (int32_t py = y0; py < y1; ++py) {
        int32_t row = py - y;
        int32_t j = row < r ? row : (row >= h - r ? h - 1 - row : -1);
        if (j < 0) { ui_span(x, x + w, py, rgb, alpha); continue; }
        int32_t i = ui_corner_first(r, j);
        for (; i < r; ++i) {
            uint32_t cv = ui_cov(r * 256, ui_corner_d256(r, i, j));
            if (cv >= 256u) break;
            if (cv) { uint32_t a = (cv * alpha) >> 8; ui_px(x + i, py, rgb, a); ui_px(x + w - 1 - i, py, rgb, a); }
        }
        ui_span(x + i, x + w - i, py, rgb, alpha);
    }
}
static void ui_rr_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb, uint32_t alpha)
{
    if (!fb_active || w <= 0 || h <= 0 || !alpha) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);
    struct rrf_ctx c = { x, y, w, h, r, rgb, alpha };
    fb_rows_run(rr_fill_rows, &c, cy, cy + ch, (int64_t)cw * ch);
}
/* contorno redondeado de 1 px con antialias; alpha 0..256 */
static void ui_rr_stroke(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t rgb, uint32_t alpha)
{
    if (!fb_active || w <= 1 || h <= 1 || !alpha) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);
    ui_span(x + r, x + w - r, y, rgb, alpha);
    ui_span(x + r, x + w - r, y + h - 1, rgb, alpha);
    for (int32_t py = (y + r > cy ? y + r : cy); py < y + h - r && py < cy + ch; ++py) { ui_px(x, py, rgb, alpha); ui_px(x + w - 1, py, rgb, alpha); }
    for (int32_t j = 0; j < r; ++j) {
        int32_t yt = y + j, yb = y + h - 1 - j;
        if ((yt < clip_y0 || yt >= clip_y1) && (yb < clip_y0 || yb >= clip_y1)) continue;
        /* SOMA_190: la distancia al centro de la esquina baja al aumentar i: solo el anillo de 1 px tiene cobertura distinta (antes se recorrian
         * los r*r pixeles de la esquina con una raiz cuadrada cada uno: cajas grandes con radio enorme tardaban segundos en maquinas lentas) */
        for (int32_t i = ui_corner_first(r, j); i < r; ++i) {
            int32_t d = ui_corner_d256(r, i, j);
            uint32_t co = ui_cov(r * 256, d), ci = ui_cov((r - 1) * 256, d);
            if (ci >= 256u) break;
            if (co <= ci) continue;
            uint32_t a = ((co - ci) * alpha) >> 8;
            ui_px(x + i, yt, rgb, a); ui_px(x + w - 1 - i, yt, rgb, a);
            ui_px(x + i, yb, rgb, a); ui_px(x + w - 1 - i, yb, rgb, a);
        }
    }
}

void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    if (r <= 0) { fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color); return; }
    ui_rr_fill(x, y, w, h, r, color, 256u);
}

void fb_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    if (r <= 0) { fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color); return; }
    ui_rr_stroke(x, y, w, h, r, color, 256u);
}

/* variantes con alfa: argb = 0xAARRGGBB (AA 0 se toma como opaco, igual que el resto de primitivas) */
static inline uint32_t ui_alpha_of(uint32_t argb) { uint32_t a = argb >> 24; return a ? (a * 256u + 127u) / 255u : 256u; }
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    ui_rr_fill(x, y, w, h, r, argb & 0x00FFFFFFu, ui_alpha_of(argb));
}
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    ui_rr_stroke(x, y, w, h, r, argb & 0x00FFFFFFu, ui_alpha_of(argb));
}

/* Sombra difusa de una caja redondeada (x,y,w,h,r), como box-shadow de CSS: desplazada (dx,dy), agrandada `spread` px y
 * desenfocada `blur` px. No pinta debajo de la propia caja (su fondo la tapa), asi que el coste es solo el del halo. */
/* SOMA_PARCHE_028_UI: la forma de una sombra solo depende de su geometria, no de donde se dibuje. Se calcula una vez como
 * mascara de alfa (8 bits) y se reutiliza: las ventanas y las tarjetas de las paginas se repintan en cada fotograma con el
 * mismo tamano, asi que el coste por fotograma queda en una mezcla por pixel del halo (sin raices ni divisiones). */
#define SHC_N 6
struct shc { int32_t w, h, r, dx, dy, spread, blur; uint32_t a; int32_t bw, bh; uint8_t *m; uint32_t cap; uint32_t use; };
static struct shc shc_tab[SHC_N]; static uint32_t shc_clock;
static struct shc *shadow_mask(int32_t w, int32_t h, int32_t r, int32_t dx, int32_t dy, int32_t spread, int32_t blur, uint32_t amax)
{
    struct shc *slot = 0;
    for (int i = 0; i < SHC_N; ++i) {
        struct shc *s = &shc_tab[i];
        if (s->m && s->w == w && s->h == h && s->r == r && s->dx == dx && s->dy == dy && s->spread == spread && s->blur == blur && s->a == amax) { s->use = ++shc_clock; return s; }
        if (!slot || (slot->m && (!s->m || s->use < slot->use))) slot = s;
    }
    int32_t sw = w + 2 * spread, sh = h + 2 * spread;
    if (sw <= 0 || sh <= 0) return 0;
    int32_t bw = sw + 2 * blur, bh = sh + 2 * blur;
    uint32_t need = (uint32_t)bw * (uint32_t)bh;
    if (need > (6u << 20)) return 0;                              /* demasiado grande: sin cache */
    if (slot->cap < need) { if (slot->m) kfree(slot->m); slot->m = (uint8_t *)kmalloc(need); slot->cap = slot->m ? need : 0; if (!slot->m) return 0; }
    slot->w = w; slot->h = h; slot->r = r; slot->dx = dx; slot->dy = dy; slot->spread = spread; slot->blur = blur; slot->a = amax; slot->bw = bw; slot->bh = bh; slot->use = ++shc_clock;
    int32_t rr = r + spread;
    if (rr > sw / 2) rr = sw / 2;
    if (rr > sh / 2) rr = sh / 2;
    if (rr < 1) rr = 1;
    int32_t ix0 = blur + rr, ix1 = blur + sw - rr, iy0 = blur + rr, iy1 = blur + sh - rr, lim16 = (blur + rr) * 16;
    uint8_t lut[96 * 16 + 1];
    for (int32_t o = 0; o <= blur * 16; ++o) { uint32_t t = (uint32_t)(blur * 16 - o); uint32_t v = (uint32_t)(((uint64_t)amax * t * t) / ((uint64_t)blur * blur * 256u)); lut[o] = (uint8_t)(v > 255u ? 255u : v); }
    for (int32_t py = 0; py < bh; ++py) {
        int32_t ddy = py < iy0 ? iy0 - py : (py >= iy1 ? py - iy1 + 1 : 0);
        uint8_t *row = slot->m + (uint32_t)py * (uint32_t)bw;
        for (int32_t px = 0; px < bw; ++px) {
            int32_t ddx = px < ix0 ? ix0 - px : (px >= ix1 ? px - ix1 + 1 : 0), d16;
            if (!ddx) d16 = ddy * 16; else if (!ddy) d16 = ddx * 16; else d16 = (int32_t)ui_isqrt64(((uint64_t)(ddx * ddx + ddy * ddy)) << 8);
            int32_t o16 = d16 - rr * 16; uint32_t al;
            if (d16 >= lim16) al = 0; else if (o16 <= 0) al = amax > 255u ? 255u : amax; else al = o16 >= blur * 16 ? 0u : lut[o16];
            row[px] = (uint8_t)al;
        }
    }
    return slot;
}
struct shd_ctx { struct shc *m; uint32_t *target; int32_t x, y, w, h, r, bx, by, cx, cw; uint32_t rgb; };
static void shadow_rows(void *ctx, int32_t y0, int32_t y1)
{
    struct shd_ctx *c = (struct shd_ctx *)ctx;
    for (int32_t py = y0; py < y1; ++py) {
        /* tramo tapado por la propia caja en esta fila (no se pinta: su fondo la cubre) */
        int32_t hx0 = 0, hx1 = 0;
        if (py >= c->y && py < c->y + c->h) { int32_t row = py - c->y, ins = (row < c->r || row >= c->h - c->r) ? c->r : 0; hx0 = c->x + ins; hx1 = c->x + c->w - ins; }
        const uint8_t *mr = c->m->m + (uint32_t)(py - c->by) * (uint32_t)c->m->bw - c->bx;
        uint32_t *row = c->target + (uint32_t)py * fb_cur_w;
        for (int32_t px = c->cx; px < c->cx + c->cw; ++px) {
            if (px >= hx0 && px < hx1) { px = hx1 - 1; continue; }
            uint32_t al = mr[px];
            if (al) ui_mix(row + px, c->rgb, al);
        }
    }
}
void fb_draw_box_shadow(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t dx, int32_t dy, int32_t spread, int32_t blur, uint32_t argb)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    if (blur < 1) blur = 1;
    if (blur > 96) blur = 96;
    uint32_t amax = ui_alpha_of(argb), rgb = argb & 0x00FFFFFFu;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    struct shc *m = shadow_mask(w, h, r, dx, dy, spread, blur, amax);
    if (!m) return;
    int32_t bx = x + dx - spread - blur, by = y + dy - spread - blur;
    int32_t cx = bx, cy = by, cw = m->bw, ch = m->bh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);
    struct shd_ctx c = { m, fb_target(), x, y, w, h, r, bx, by, cx, cw, rgb };
    fb_rows_run(shadow_rows, &c, cy, cy + ch, (int64_t)cw * ch);
}
void fb_draw_shadow(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t blur, int32_t off_y, uint32_t argb)
{
    if (blur <= 0) return;
    fb_draw_box_shadow(x, y, w, h, r, 0, off_y, 0, blur, argb);
}

/* Linea gruesa suavizada (capsula). Coordenadas y grosor en 1/16 de pixel. Con los dos extremos iguales dibuja un disco. */
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t argb)
{
    if (!fb_active || width16 <= 0) return;
    uint32_t amax = ui_alpha_of(argb), rgb = argb & 0x00FFFFFFu;
    int32_t hw = width16 / 2;
    int32_t minx = (x0 < x1 ? x0 : x1) - hw - 16, maxx = (x0 > x1 ? x0 : x1) + hw + 16;
    int32_t miny = (y0 < y1 ? y0 : y1) - hw - 16, maxy = (y0 > y1 ? y0 : y1) + hw + 16;
    int32_t bx = minx >> 4, by = miny >> 4, bw = (maxx >> 4) - bx + 1, bh = (maxy >> 4) - by + 1;
    int32_t cx = bx, cy = by, cw = bw, ch = bh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();
    int64_t vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int32_t py = cy; py < cy + ch; ++py) {
        uint32_t *row = target + (uint32_t)py * fb_cur_w;
        int64_t qy = (int64_t)py * 16 + 8 - y0;
        for (int32_t px = cx; px < cx + cw; ++px) {
            int64_t qx = (int64_t)px * 16 + 8 - x0, d2;
            if (len2 == 0) d2 = qx * qx + qy * qy;
            else {
                int64_t t = qx * vx + qy * vy;
                if (t <= 0) d2 = qx * qx + qy * qy;
                else if (t >= len2) { int64_t ex = qx - vx, ey = qy - vy; d2 = ex * ex + ey * ey; }
                else { int64_t cr = qx * vy - qy * vx; d2 = (cr * cr) / len2; }
            }
            int32_t d = (int32_t)ui_isqrt64((uint64_t)d2 << 8);     /* 1/256 px */
            int32_t c = (hw * 16 + 128 - d);                        /* cobertura *256 */
            if (c <= 0) continue;
            uint32_t a = c >= 256 ? amax : ((uint32_t)c * amax) >> 8;
            ui_mix(row + px, rgb, a);
        }
    }
}

/* ---- Esquinas redondeadas de ventana -------------------------------------------------------------
 * Las aplicaciones pintan su cuerpo como un rectangulo. Para que la ventana conserve las esquinas inferiores
 * redondeadas, el gestor guarda el fondo de esas esquinas ANTES de dibujar la ventana (fb_cguard_capture) y lo
 * recompone por fuera del arco DESPUES (fb_cguard_apply), trazando ademas el borde. fb_cguard_keep deja la guarda
 * de la ventana activa registrada: fb_present la reaplica si la aplicacion repinta su cuerpo entre dos fotogramas. */
static void cg_copy(uint32_t *dst, int32_t x, int32_t y, int32_t r)
{
    uint32_t *target = fb_target();
    for (int32_t j = 0; j < r; ++j) for (int32_t i = 0; i < r; ++i) {
        int32_t px = x + i, py = y + j;
        dst[j * FB_CGUARD_R + i] = (px >= 0 && py >= 0 && px < (int32_t)fb_cur_w && py < (int32_t)fb_cur_h) ? target[(uint32_t)py * fb_cur_w + (uint32_t)px] : 0u;
    }
}
void fb_cguard_capture(struct fb_cguard *g, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    if (!g) return;
    g->valid = 0;
    if (!fb_active || r <= 0 || w < 2 * r || h < 2 * r) return;
    if (r > FB_CGUARD_R) r = FB_CGUARD_R;
    g->x = x; g->y = y; g->w = w; g->h = h; g->r = r; g->border = 0; g->applied = 0;
    cg_copy(g->save[0], x, y + h - r, r);
    cg_copy(g->save[1], x + w - r, y + h - r, r);
    g->valid = 1;
}
static void cg_apply(struct fb_cguard *g, int force)
{
    if (!g || !g->valid || !fb_active) return;
    uint32_t *target = fb_target();
    int32_t r = g->r; uint32_t brgb = g->border & 0x00FFFFFFu, ba = (g->border >> 24) ? ui_alpha_of(g->border) : 0u;
    int touched = 0;
    for (int side = 0; side < 2; ++side) {
        int32_t ox = side ? g->x + g->w - r : g->x, oy = g->y + g->h - r;
        for (int32_t j = 0; j < r; ++j) for (int32_t i = 0; i < r; ++i) {
            int32_t px = ox + i, py = oy + j;
            if (px < 0 || py < 0 || px >= (int32_t)fb_cur_w || py >= (int32_t)fb_cur_h) continue;
            int32_t ci = side ? r - 1 - i : i, cj = r - 1 - j;                /* indices desde el vertice exterior */
            int32_t d = ui_corner_d256(r, ci, cj);
            uint32_t co = ui_cov(r * 256, d);
            uint32_t *p = target + (uint32_t)py * fb_cur_w + (uint32_t)px, *res = &g->res[side][j * FB_CGUARD_R + i];
            if (co >= 256u && !ba) continue;
            uint32_t cur = *p & 0x00FFFFFFu;
            if (!force && g->applied && cur == *res) continue;                /* ya recompuesto: no acumular */
            uint32_t v = cur;
            if (ba) { uint32_t cin = ui_cov((r - 1) * 256, d); if (co > cin) { uint32_t t = v; ui_mix(&t, brgb, ((co - cin) * ba) >> 8); v = t; } }
            if (co < 256u) { uint32_t t = g->save[side][j * FB_CGUARD_R + i] & 0x00FFFFFFu; ui_mix(&t, v, co); v = t; }
            if (co >= 256u && v == cur) { *res = v; continue; }
            *p = v; *res = v; touched = 1;
        }
    }
    g->applied = 1;
    if (touched) { fb_damage(g->x, g->y + g->h - r, r, r); fb_damage(g->x + g->w - r, g->y + g->h - r, r, r); }
}
void fb_cguard_apply(struct fb_cguard *g, uint32_t border_argb)
{
    if (!g || !g->valid) return;
    g->border = border_argb;
    cg_apply(g, 1);
}
static struct fb_cguard cg_kept;
void fb_cguard_keep(const struct fb_cguard *g)
{
    if (!g || !g->valid) { cg_kept.valid = 0; return; }
    memcpy(&cg_kept, g, sizeof cg_kept);
}
static void fb_cguard_present(void) { if (cg_kept.valid) cg_apply(&cg_kept, 0); }
static void fb_cguard_drop(void) { cg_kept.valid = 0; }
/* Recorte redondeado para contenido (overflow:hidden + border-radius en la web): se guardan las 4 esquinas de la caja
 * antes de pintarla (buf: 4*r*r pixeles) y, pintado el contenido, se repone lo que queda fuera del arco. */
static void cs_corner(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int k, int32_t *ox, int32_t *oy)
{
    *ox = (k & 1) ? x + w - r : x; *oy = (k & 2) ? y + h - r : y;
}
int fb_corners_save(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t *buf)
{
    if (!fb_active || !buf || r <= 0 || w < 2 * r || h < 2 * r) return 0;
    uint32_t *target = fb_target();
    for (int k = 0; k < 4; ++k) {
        int32_t ox, oy; cs_corner(x, y, w, h, r, k, &ox, &oy);
        uint32_t *dst = buf + (size_t)k * (size_t)r * (size_t)r;
        for (int32_t j = 0; j < r; ++j) for (int32_t i = 0; i < r; ++i) {
            int32_t px = ox + i, py = oy + j;
            dst[j * r + i] = (px >= 0 && py >= 0 && px < (int32_t)fb_cur_w && py < (int32_t)fb_cur_h) ? target[(uint32_t)py * fb_cur_w + (uint32_t)px] : 0u;
        }
    }
    return 1;
}
void fb_corners_restore(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const uint32_t *buf)
{
    if (!fb_active || !buf || r <= 0 || w < 2 * r || h < 2 * r) return;
    uint32_t *target = fb_target();
    for (int k = 0; k < 4; ++k) {
        int32_t ox, oy; cs_corner(x, y, w, h, r, k, &ox, &oy);
        const uint32_t *src = buf + (size_t)k * (size_t)r * (size_t)r;
        int touched = 0;
        for (int32_t j = 0; j < r; ++j) {
            int32_t py = oy + j; if (py < clip_y0 || py >= clip_y1) continue;
            int32_t cj = (k & 2) ? r - 1 - j : j;
            for (int32_t i = 0; i < r; ++i) {
                int32_t px = ox + i; if (px < clip_x0 || px >= clip_x1) continue;
                int32_t ci = (k & 1) ? r - 1 - i : i;
                uint32_t co = ui_cov(r * 256, ui_corner_d256(r, ci, cj));
                if (co >= 256u) continue;
                uint32_t *p = target + (uint32_t)py * fb_cur_w + (uint32_t)px;
                uint32_t t = src[j * r + i] & 0x00FFFFFFu; ui_mix(&t, *p & 0x00FFFFFFu, co);
                *p = t; touched = 1;
            }
        }
        if (touched) fb_damage(ox, oy, r, r);
    }
}
/* copia un rectangulo del backbuffer (para instantaneas de menus); devuelve 1 si lo hizo */
int fb_read_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t *out)
{
    if (!fb_active || !out || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > (int32_t)fb_cur_w || y + h > (int32_t)fb_cur_h) return 0;
    uint32_t *target = fb_target();
    for (int32_t j = 0; j < h; ++j) memcpy(out + (size_t)j * (size_t)w, target + (uint32_t)(y + j) * fb_cur_w + (uint32_t)x, (size_t)w * 4u);
    return 1;
}

void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    if (!fb_active) return;
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        fb_draw_pixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void fb_fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    if (!fb_active || r <= 0) return;
    /* SOMA_209: el circulo pinta las filas cy-r..cy+r y las columnas cx-r-1..cx+r; antes se declaraba una caja de 2r x 2r y el
     * borde derecho/inferior quedaba sin declarar (con borrado por zonas podia quedar un resto hasta el siguiente borrado completo). */
    int32_t min_x = cx - r - 1, min_y = cy - r, max_w = r * 2 + 2, max_h = r * 2 + 1;
    if (!clip_rect(&min_x, &min_y, &max_w, &max_h)) return;

    fb_damage(min_x, min_y, max_w, max_h);
    uint32_t *target = fb_target();
    int32_t r2 = r * r;

    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t py = cy + dy;
        if (py < clip_y0 || py >= clip_y1) continue;

        int32_t span = 0;
        while (span * span + dy * dy <= r2) span++;
        if (span > 0) {
            int32_t lx = cx - span, lw = span * 2;
            int32_t dummy_y = py, dummy_h = 1;
            if (clip_rect(&lx, &dummy_y, &lw, &dummy_h)) {
                fast_fill_span(target + py * fb_cur_w + lx, color, lw);
            }
        }
    }
}

void fb_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c_top, uint32_t c_bottom)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    int32_t r0 = (int32_t)((c_top >> 16) & 0xFF);
    int32_t g0 = (int32_t)((c_top >> 8) & 0xFF);
    int32_t b0 = (int32_t)(c_top & 0xFF);

    int32_t r1 = (int32_t)((c_bottom >> 16) & 0xFF);
    int32_t g1 = (int32_t)((c_bottom >> 8) & 0xFF);
    int32_t b1 = (int32_t)(c_bottom & 0xFF);

    int32_t denom = (h > 1) ? (h - 1) : 1;

    for (int32_t row = 0; row < ch; ++row) {
        int32_t orig_y = (cy - y) + row;
        uint32_t r = (uint32_t)(r0 + ((r1 - r0) * orig_y) / denom);
        uint32_t g = (uint32_t)(g0 + ((g1 - g0) * orig_y) / denom);
        uint32_t b = (uint32_t)(b0 + ((b1 - b0) * orig_y) / denom);
        uint32_t col = (r << 16) | (g << 8) | b | 0xFF000000;

        uint32_t *dst_row = target + (cy + row) * fb_cur_w + cx;
        fast_fill_span(dst_row, col, cw);
    }
}

/* ====================================================================
 * MAPAS DE BITS, FOTOGRAFÍAS Y VÍDEO
 * ==================================================================== */

struct bl_ctx { uint32_t *t; const uint32_t *src; int32_t stride, cx, cw, cy, off_x, off_y; };
static void blit_rows(void *c, int32_t y0, int32_t y1)
{
    struct bl_ctx *k = (struct bl_ctx *)c;
    for (int32_t y = y0; y < y1; ++y)
        fast_copy_span(k->t + (uint32_t)y * fb_cur_w + (uint32_t)k->cx, k->src + (int64_t)(k->off_y + y - k->cy) * k->stride + k->off_x, k->cw);
}
void fb_blit(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0) return;
    int32_t cx = dx, cy = dy, cw = sw, ch = sh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    int32_t off_x = cx - dx;
    int32_t off_y = cy - dy;

    fb_damage(cx, cy, cw, ch);
    struct bl_ctx k = { fb_target(), src, stride, cx, cw, cy, off_x, off_y };
    fb_rows_run(blit_rows, &k, cy, cy + ch, (int64_t)cw * ch);     /* SOMA_168B: copias grandes repartidas */
}

void fb_blit_alpha(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0) return;
    int32_t cx = dx, cy = dy, cw = sw, ch = sh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    int32_t off_x = cx - dx;
    int32_t off_y = cy - dy;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t *dst_row = target + (cy + r) * fb_cur_w + cx;
        const uint32_t *src_row = src + (off_y + r) * stride + off_x;
        for (int32_t c = 0; c < cw; ++c) {
            dst_row[c] = fb_blend_color(dst_row[c], src_row[c]);
        }
    }
}

void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    int32_t cx = dx, cy = dy, cw = dw, ch = dh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    uint32_t step_x = ((uint32_t)sw << 16) / (uint32_t)dw;
    uint32_t step_y = ((uint32_t)sh << 16) / (uint32_t)dh;

    uint32_t start_src_y = (uint32_t)(cy - dy) * step_y;
    uint32_t start_src_x = (uint32_t)(cx - dx) * step_x;

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t sy = (start_src_y + (uint32_t)r * step_y) >> 16;
        if (sy >= (uint32_t)sh) sy = (uint32_t)(sh - 1);

        const uint32_t *src_row = src + sy * stride;
        uint32_t *dst_row = target + (cy + r) * fb_cur_w + cx;

        uint32_t curr_x = start_src_x;
        for (int32_t c = 0; c < cw; ++c) {
            uint32_t sx = curr_x >> 16;
            if (sx >= (uint32_t)sw) sx = (uint32_t)(sw - 1);
            dst_row[c] = src_row[sx];
            curr_x += step_x;
        }
    }
}

/* ====================================================================
 * RENDERIZADO DE GLIFOS
 * ==================================================================== */

void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale)
{
    if (!fb_active || !glyph || scale <= 0) return;
    uint32_t *target = fb_target();

    for (int r = 0; r < 8; ++r) {
        uint8_t row_bits = glyph[r];
        if (!row_bits) continue;
        for (int col = 0; col < 8; ++col) {
            if (row_bits & (1 << (7 - col))) {
                int32_t px = x + col * scale;
                int32_t py = y + r * scale;
                if (scale == 1) {
                    if (px >= clip_x0 && px < clip_x1 && py >= clip_y0 && py < clip_y1) {
                        target[py * fb_cur_w + px] = fg;
                    }
                } else {
                    int32_t cx = px, cy = py, cw = scale, ch = scale;
                    if (clip_rect(&cx, &cy, &cw, &ch)) {
                        for (int32_t sr = 0; sr < ch; ++sr) {
                            uint32_t *drow = target + (cy + sr) * fb_cur_w + cx;
                            fast_fill_span(drow, fg, cw);
                        }
                    }
                }
            }
        }
    }
}

void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale)
{
    if (!fb_active || !glyph || scale <= 0) return;
    fb_damage(x, y, 8 * scale, 8 * scale);
    fb_draw_glyph8_nodmg(x, y, glyph, fg, scale);
}


/* ==== SOMA_VECTOR_FONTS_PATCH: mascara alfa de 8 bits (texto/iconos vectoriales) ====
 * Mezcla 'rgb' sobre el backbuffer usando 'mask' (w*h bytes de cobertura) y un
 * alfa global 0..255. Respeta el recorte (scissor) y registra el area sucia. */
void fb_draw_mask8(int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h,
                   uint32_t rgb, uint32_t alpha)
{
    if (!fb_active || !mask || w <= 0 || h <= 0) return;
    if (alpha > 255u) alpha = 255u;

    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);

    uint32_t *target = fb_target();
    uint32_t sr = (rgb >> 16) & 0xFFu;
    uint32_t sg = (rgb >> 8) & 0xFFu;
    uint32_t sb = rgb & 0xFFu;

    for (int32_t r = 0; r < ch; ++r) {
        const uint8_t *mrow = mask + (int32_t)(cy + r - y) * w + (cx - x);
        uint32_t *drow = target + (uint32_t)(cy + r) * fb_cur_w + (uint32_t)cx;
        for (int32_t c = 0; c < cw; ++c) {
            uint32_t a = mrow[c];
            if (!a) continue;
            if (alpha != 255u) a = (a * alpha + 127u) / 255u;
            if (!a) continue;
            if (a >= 255u) { drow[c] = rgb & 0x00FFFFFFu; continue; }
            uint32_t bg = drow[c];
            uint32_t ia = 255u - a;
            uint32_t rr = (sr * a + ((bg >> 16) & 0xFFu) * ia + 127u) / 255u;
            uint32_t gg = (sg * a + ((bg >> 8) & 0xFFu) * ia + 127u) / 255u;
            uint32_t bb = (sb * a + (bg & 0xFFu) * ia + 127u) / 255u;
            drow[c] = (rr << 16) | (gg << 8) | bb;
        }
    }
}
