#ifdef __x86_64__
#include "bootfb.h"   /* SOMA_154_T480S */
#endif
#include "arch/aarch64/rpi_fb.h"
volatile int console_muted = 0;
#include "arch.h"
#include "thread.h"
#include "console.h"
int fb_is_active(void);   /* SOMA_287_FLUIDEZ */
uint64_t timer_get_uptime_ms(void);
#include "mem.h"      /* SOMA_156: kmalloc para el registro */
#include "shell.h"
#include "io.h"
#include "fs.h"

#define COM1 0x3F8

#ifdef __aarch64__
#include "platform.h"
#define PL011_UARTDR ((volatile uint32_t *)(uintptr_t)SOMA_UART_BASE)
#define PL011_UARTFR ((volatile uint32_t *)(uintptr_t)(SOMA_UART_BASE + 0x18))
#else
static volatile uint16_t *const VGA_BUF = (uint16_t *)0xB8000;
static uint32_t vga_row = 0;
static uint32_t vga_col = 0;
static uint8_t  vga_attr = 0x07;
static int      ansi_state = 0;
static int      ansi_arg1 = 0;
static int      ansi_arg2 = 0;
static int      ansi_has_arg2 = 0;

/* Decodificador UTF-8 -> CP437 para renderizado en pantalla VGA */
static uint32_t vga_utf8_state = 0;
static uint32_t vga_utf8_cp = 0;

static uint8_t utf8_to_cp437(uint32_t cp)
{
    switch (cp) {
        case 0x00E1: return 0xA0; /* á */
        case 0x00E9: return 0x82; /* é */
        case 0x00ED: return 0xA1; /* í */
        case 0x00F3: return 0xA2; /* ó */
        case 0x00FA: return 0xA3; /* ú */
        case 0x00C1: return 'A';  /* Á */
        case 0x00C9: return 0x90; /* É */
        case 0x00CD: return 'I';  /* Í */
        case 0x00D3: return 'O';  /* Ó */
        case 0x00DA: return 'U';  /* Ú */
        case 0x00F1: return 0xA4; /* ñ */
        case 0x00D1: return 0xA5; /* Ñ */
        case 0x00BF: return 0xA8; /* ¿ */
        case 0x00A1: return 0xAD; /* ¡ */
        case 0x00FC: return 0x81; /* ü */
        case 0x00DC: return 0x9A; /* Ü */
        default:     return (cp < 128) ? (uint8_t)cp : '?';
    }
}

static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * 80 + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void)
{
    while (vga_row >= 25) {
        for (int i = 0; i < 24 * 80; ++i) {
            VGA_BUF[i] = VGA_BUF[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; ++i) {
            VGA_BUF[i] = 0x0720;
        }
        vga_row--;
    }
}

static void vga_putc(char c)
{
    /* Manejo de secuencias multibyte UTF-8 */
    uint8_t uc = (uint8_t)c;
    if (uc >= 0x80) {
        if ((uc & 0xE0) == 0xC0) {
            vga_utf8_cp = uc & 0x1F;
            vga_utf8_state = 1;
            return;
        } else if ((uc & 0xF0) == 0xE0) {
            vga_utf8_cp = uc & 0x0F;
            vga_utf8_state = 2;
            return;
        } else if ((uc & 0xC0) == 0x80 && vga_utf8_state > 0) {
            vga_utf8_cp = (vga_utf8_cp << 6) | (uc & 0x3F);
            vga_utf8_state--;
            if (vga_utf8_state == 0) {
                c = (char)utf8_to_cp437(vga_utf8_cp);
            } else {
                return;
            }
        } else {
            vga_utf8_state = 0;
            return;
        }
    } else {
        vga_utf8_state = 0;
    }

    if (ansi_state == 0) {
        if ((uint8_t)c == 27) {
            ansi_state = 1;
            return;
        }
    } else if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_arg1 = 0;
            ansi_arg2 = 0;
            ansi_has_arg2 = 0;
            return;
        }
        ansi_state = 0;
    } else if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            if (!ansi_has_arg2) {
                ansi_arg1 = ansi_arg1 * 10 + (c - '0');
            } else {
                ansi_arg2 = ansi_arg2 * 10 + (c - '0');
            }
            return;
        } else if (c == ';') {
            ansi_has_arg2 = 1;
            return;
        } else if (c == 'm') {
            int bold = (ansi_arg1 == 1 || (ansi_has_arg2 && ansi_arg2 == 1));
            int code = (ansi_arg1 == 1 && ansi_has_arg2) ? ansi_arg2 : ansi_arg1;

            if (ansi_arg1 == 0 && !ansi_has_arg2) {
                vga_attr = 0x07;
            } else {
                uint8_t fg = 0x07;
                if (code == 30) fg = 0x00;
                else if (code == 31) fg = 0x04;
                else if (code == 32) fg = 0x02;
                else if (code == 33) fg = 0x06;
                else if (code == 34) fg = 0x01;
                else if (code == 35) fg = 0x05;
                else if (code == 36) fg = 0x03;
                else if (code == 37) fg = 0x07;

                if (bold) fg |= 0x08;
                vga_attr = fg;
            }
            ansi_state = 0;
            return;
        } else {
            ansi_state = 0;
            return;
        }
    }

    if (c == '\r') {
        vga_col = 0;
    } else if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_BUF[vga_row * 80 + vga_col] = (uint16_t)((vga_attr << 8) | ' ');
        }
    } else if ((uint8_t)c >= 32) {
        VGA_BUF[vga_row * 80 + vga_col] = (uint16_t)((vga_attr << 8) | (uint8_t)c);
        vga_col++;
        if (vga_col >= 80) {
            vga_col = 0;
            vga_row++;
        }
    }
    vga_scroll();
    vga_update_cursor();
}
#endif

void console_clear(void)
{
#ifdef __x86_64__
    for (uint32_t i = 0; i < 80 * 25; ++i) {
        VGA_BUF[i] = 0x0720;
    }
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
#else
    kprint("\033[2J\033[H");
#endif
}

/* PARCHE 116: captura por hilo */
static char *volatile cap_buf; static uint32_t cap_cap, cap_len; static struct tcb *volatile cap_owner; static const char *cap_in;
static volatile uint32_t cap_guest;            /* SOMA_237_PYTHON */
static char cap_feed[2048]; static volatile uint32_t cap_feed_len; static volatile int cap_live, cap_feed_eof;
void console_capture_begin(char *buf, uint32_t cap, const char *input)
{
    if (!buf || cap == 0) return;
    buf[0] = 0; cap_cap = cap; cap_len = 0; cap_in = input ? input : ""; cap_owner = thread_current(); cap_buf = buf;
}
uint32_t console_capture_end(void) { uint32_t n = cap_len; cap_buf = 0; cap_owner = 0; cap_in = 0; cap_guest = 0; cap_live = 0; return n; }
void console_capture_abort(void) { cap_buf = 0; cap_owner = 0; cap_in = 0; cap_guest = 0; cap_live = 0; }
/* SOMA_237_PYTHON: un hilo "invitado" (el proceso de ring 3 que lanza la orden capturada) comparte la captura de su dueno: su salida
 * y su entrada simulada. Asi `python` en el terminal de la GUI muestra lo que imprime el script. Se quita al terminar la captura. */
static int cap_mine(void)
{
    if (!cap_buf) return 0;
    struct tcb *t = thread_current();
    return t == cap_owner || (t && cap_guest && t->tid == cap_guest);
}
int console_capture_guest(uint32_t tid)
{
    if (!cap_buf || thread_current() != cap_owner) return 0;
    cap_guest = tid;
    return 1;
}
/* SOMA_237_PYTHON: entrada VIVA. En lugar de un texto fijo, el dueno de la captura (el terminal de la GUI) va dando lineas segun
 * las escribe el usuario; kgetline del hilo capturado espera a la siguiente. feed_eof() cierra la entrada (Esc en el terminal). */
void console_capture_live(int on) { cap_feed_len = 0; cap_feed_eof = 0; cap_live = on; }
int console_capture_feed(const char *line)
{
    if (!cap_live || !cap_buf) return -1;
    uint32_t n = 0; while (line[n]) n++;
    if (cap_feed_len + n + 1 > sizeof cap_feed) return -1;
    for (uint32_t i = 0; i < n; ++i) cap_feed[cap_feed_len + i] = line[i] == '\n' ? ' ' : line[i];
    cap_feed[cap_feed_len + n] = '\n';
    cap_feed_len += n + 1;
    return 0;
}
void console_capture_feed_eof(void) { cap_feed_eof = 1; }
uint32_t console_capture_len(void) { return cap_buf ? cap_len : 0; }
void console_capture_consume(uint32_t n)       /* quita los n primeros bytes ya mostrados (la captura viva no se llena nunca) */
{
    if (!cap_buf) return;
    if (n > cap_len) n = cap_len;
    for (uint32_t i = n; i < cap_len; ++i) cap_buf[i - n] = cap_buf[i];
    cap_len -= n; cap_buf[cap_len] = 0;
}
static int feed_has_line(void) { for (uint32_t i = 0; i < cap_feed_len; ++i) if (cap_feed[i] == '\n') return 1; return 0; }
int console_capture_mine(void) { return cap_mine(); }
int console_capture_input_eof(void)
{
    if (!cap_mine()) return 0;
    if (cap_live) return cap_feed_eof && !feed_has_line();
    return !cap_in || !*cap_in;
}
int console_capture_active(void) { return cap_buf != 0; }   /* SOMA_156B: la captura no se anida */

/* PARCHE 132: la espera del transmisor es ACOTADA. Antes, un UART que nunca declara "vacio" (modulo desconectado, control de flujo, hardware raro)
 * colgaba el sistema entero en la primera impresion. Tras dos esperas agotadas se da por muerto y se sigue sin el. */
#define UART_SPIN_MAX 20000
static unsigned uart_timeouts;
static int uart_dead;
#ifdef __x86_64__
/* SOMA_155B_T480S: los portatiles (el T480s incluido) no tienen COM1. En un puerto inexistente el bus devuelve 0xFF: el bit
 * "hay dato" de LSR parece siempre activo y kgetc() leia 0xFF sin fin (la primera tecla salia y despues la entrada quedaba
 * inundada de bytes basura). Deteccion como el 8250 de Linux: el registro de prueba (scratch) debe devolver lo escrito. */
static int uart_absent;
void console_serial_probe(void)
{
    outb(COM1 + 7, 0x5A);
    uint8_t a = inb(COM1 + 7);
    outb(COM1 + 7, 0xA5);
    uint8_t b = inb(COM1 + 7);
    uart_absent = (a != 0x5A || b != 0xA5 || inb(COM1 + 5) == 0xFF);
    if (uart_absent) uart_dead = 1;                         /* tampoco se escribe en el: arranque mas rapido */
}
int console_serial_present(void) { return !uart_absent; }
#endif
/* SOMA_156_T480S: registro del nucleo ("dmesg"). El portatil no tiene puerto serie y el arranque pasa demasiado deprisa para
 * leerlo: todo lo que sale por kputc se guarda aqui. 16 KiB estaticos al principio; en cuanto hay montón, 256 KiB. */
#define KLOG_EARLY 16384u
#define KLOG_BIG   (256u * 1024u)
static char klog_early[KLOG_EARLY];
static char *klog = klog_early;
static uint32_t klog_cap = KLOG_EARLY;
static volatile uint64_t klog_total;          /* caracteres escritos desde el arranque (la posicion es total % cap) */
static volatile int klog_paused;              /* mientras "dmesg" imprime el propio registro */
static void klog_putc(char c)
{
    if (klog_paused) return;
    klog[klog_total % klog_cap] = c;
    klog_total++;
}
void klog_grow(void)
{
    char *nb = (char *)kmalloc(KLOG_BIG);
    if (!nb) return;
    uint64_t have = klog_total < klog_cap ? klog_total : klog_cap;
    for (uint64_t i = 0; i < have; i++) nb[i] = klog[(klog_total - have + i) % klog_cap];
    klog = nb; klog_cap = KLOG_BIG; klog_total = have;
}
uint32_t klog_snapshot(char *out, uint32_t max)       /* copia lo mas reciente que quepa, en orden */
{
    if (!out || !max) return 0;
    uint64_t have = klog_total < klog_cap ? klog_total : klog_cap;
    if (have > max - 1) have = max - 1;
    for (uint64_t i = 0; i < have; i++) out[i] = klog[(klog_total - have + i) % klog_cap];
    out[have] = 0;
    return (uint32_t)have;
}
uint64_t klog_written(void) { return klog_total; }
void klog_pause(int on) { klog_paused = on; }

void kputc(char c)
{
    if (cap_mine()) {                                            /* SOMA_237: el dueno o su invitado */
        if (cap_len + 1 < cap_cap) { cap_buf[cap_len++] = c; cap_buf[cap_len] = 0; }
        return;
    }
    klog_putc(c);
    if (console_muted) return;
#ifdef __x86_64__
    if (!uart_dead) {
        unsigned spin = 0;
        while ((inb(COM1 + 5) & 0x20) == 0 && spin < UART_SPIN_MAX) spin++;
        if (spin >= UART_SPIN_MAX) { if (++uart_timeouts >= 2) uart_dead = 1; }
        else outb(COM1, (uint8_t)c);
    }
    if (bootfb_use_firmware()) bootfb_putc(c);   /* SOMA_154_T480S: en UEFI no hay modo texto VGA */
    else if (!fb_is_active()) vga_putc(c);       /* SOMA_287_FLUIDEZ: con el escritorio grafico el modo texto no se ve; en QEMU cada
                                                  * escritura en 0xB8000 (y el desplazamiento de 4000 bytes por linea) es una salida de la
                                                  * maquina virtual: el agente imprimiendo su respuesta paraba la animacion del orbe */
#else
    if (!uart_dead) {
        unsigned spin = 0;
        while ((*PL011_UARTFR & (1 << 5)) && spin < UART_SPIN_MAX) spin++;
        if (spin >= UART_SPIN_MAX) { if (++uart_timeouts >= 2) uart_dead = 1; }
        else *PL011_UARTDR = (uint32_t)(uint8_t)c;
    }
    rpi_fb_putc(c);
#endif
}

/* SOMA_287_FLUIDEZ: un texto largo (la respuesta del LLM, la salida de una orden) sale caracter a caracter por el puerto serie, que
 * espera en bucle a la UART, y el planificador del nucleo es cooperativo: mientras el agente escribia, la GUI no avanzaba y el orbe
 * iba a tirones. Desde el hilo del agente (que no tiene cerrojos de espera activa al imprimir) se cede la CPU cada pocos
 * milisegundos, solo con las interrupciones activas (fuera de manejadores y secciones criticas). */
static int kp_puede_ceder(void)
{
#ifdef __x86_64__
    uint64_t fl; __asm__ volatile ("pushfq; popq %0" : "=r"(fl));
    if (!(fl & 0x200u)) return 0;
    struct tcb *t = thread_current();
    if (!t) return 0;
    const char *n = t->name, *a = "soma_agente";
    while (*a && *n == *a) { ++n; ++a; }
    return !*a && !*n;
#else
    return 0;
#endif
}
void kprint(const char *s)
{
    uint32_t k = 0;
    for (; *s; ++s) {
        kputc(*s);
        if ((++k & 127u) == 0 && kp_puede_ceder()) {
            static uint64_t ult; uint64_t t = timer_get_uptime_ms();
            if (t - ult >= 4) { ult = t; thread_yield(); }
        }
    }
}

static void put_nibble(uint8_t v)
{
    v &= 0x0F;
    kputc(v < 10 ? (char)('0' + v) : (char)('A' + v - 10));
}

void kprint_hex8(uint8_t value)
{
    put_nibble((uint8_t)(value >> 4));
    put_nibble(value);
}

void kprint_hex16(uint16_t value)
{
    kprint_hex8((uint8_t)(value >> 8));
    kprint_hex8((uint8_t)value);
}

void kprint_hex32(uint32_t value)
{
    kprint_hex16((uint16_t)(value >> 16));
    kprint_hex16((uint16_t)value);
}

void kprint_dec(uint32_t value)
{
    char tmp[10];
    int n = 0;

    if (value == 0) {
        kputc('0');
        return;
    }

    while (value != 0) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (n > 0) {
        kputc(tmp[--n]);
    }
}

void kprint_mac(const uint8_t *mac)
{
    for (int i = 0; i < 6; ++i) {
        kprint_hex8(mac[i]);
        if (i != 5) kputc(':');
    }
}

void kprint_ip(const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        kprint_dec(ip[i]);
        if (i != 3) kputc('.');
    }
}

#if defined(__x86_64__) || defined(__aarch64__)          /* PARCHE 062: en aarch64 la cola la alimenta el teclado USB (xHCI) */
#define KBD_BUF_SIZE 4096                       /* SOMA_245_PTT: cabe un dictado entero (antes 128) */
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;
static int caps_active = 0;
static int e0_prefix = 0;
static int kbd_dead_key = 0; /* 0: ninguna, 1: agudo (´ / '), 2: dieresis (¨ / "), 3: tilde (~) */

static volatile uint8_t kbd_mod[KBD_BUF_SIZE];   /* PARCHE 094: Ctrl/Mayus en el momento de cada pulsacion */
static volatile uint8_t kbd_last_mod;
static int shift_active;
static int ctrl_active;
static int altgr_active;                /* SOMA_155_T480S: AltGr (E0 38) */
static int kbd_layout_es;               /* SOMA_155_T480S */
static volatile uint32_t kbd_events;      /* SOMA_155D: pulsaciones recibidas (la GUI cede la CPU mientras se teclea) */
uint32_t console_kbd_events(void) { return kbd_events; }
static void kbd_push_byte(char ch)
{
    kbd_events++;
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_mod[kbd_head] = (uint8_t)((ctrl_active ? 1 : 0) | (shift_active ? 2 : 0));
        kbd_buf[kbd_head] = ch;
        kbd_head = next;
    }
}

static void kbd_push_utf8_char(char base, int accent)
{
    /* accent: 1 = agudo (á,é,í,ó,ú), 2 = dieresis (ü,Ü), 3 = tilde (ñ,Ñ) */
    if (accent == 1) {
        switch (base) {
            case 'a': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xA1); return;
            case 'e': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xA9); return;
            case 'i': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xAD); return;
            case 'o': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB3); return;
            case 'u': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xBA); return;
            case 'A': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x81); return;
            case 'E': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x89); return;
            case 'I': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x8D); return;
            case 'O': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x93); return;
            case 'U': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x9A); return;
            default:  kbd_push_byte((char)0x27); kbd_push_byte(base); return;
        }
    } else if (accent == 2) {
        switch (base) {
            case 'u': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xBC); return;
            case 'U': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x9C); return;
            default:  kbd_push_byte((char)0x22); kbd_push_byte(base); return;
        }
    } else if (accent == 3) {
        switch (base) {
            case 'n': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB1); return;
            case 'N': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x91); return;
            default:  kbd_push_byte((char)0x7E); kbd_push_byte(base); return;
        }
    }
    kbd_push_byte(base);
}


static const char kbd_us_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r',
    0, /* Ctrl */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, /* LShift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, /* RShift */
    '*', 0, /* Alt */ ' ', 0 /* CapsLock */
};

static const char kbd_us_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r',
    0, /* Ctrl */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, /* LShift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, /* RShift */
    '*', 0, /* Alt */ ' ', 0 /* CapsLock */
};

static void keyboard_scancode(uint8_t sc);

/* ---------------------------------------------------------------- SOMA_155_T480S: distribucion espanola (ISO)
 * Teclado del ThinkPad T480s en Espana. Juego 1 de codigos (el que deja el 8042 tras traducir). Cada tecla: normal, Mayus,
 * AltGr. Las teclas muertas (acento agudo/dieresis junto a la n, grave/circunflejo junto a la P) se combinan con la vocal
 * siguiente; con espacio sale el propio signo. La salida es UTF-8, como el resto de la consola. */
#define DK_ACUTE 0x110001u
#define DK_DIAER 0x110002u
#define DK_GRAVE 0x110004u
#define DK_CIRC  0x110005u
struct es_key { uint8_t sc; uint32_t n, s, a; };
static const struct es_key es_keys[] = {
    { 0x29, 0xBA, 0xAA, '\\' },        /* º ª \ */
    { 0x02, '1', '!', '|' },  { 0x03, '2', '"', '@' },  { 0x04, '3', 0xB7, '#' },   /* 3 · # */
    { 0x05, '4', '$', '~' },  { 0x06, '5', '%', 0x20AC }, { 0x07, '6', '&', 0xAC }, /* 5 % EUR, 6 & not */
    { 0x08, '7', '/', 0 },    { 0x09, '8', '(', 0 },    { 0x0A, '9', ')', 0 },
    { 0x0B, '0', '=', 0 },    { 0x0C, '\'', '?', 0 },   { 0x0D, 0xA1, 0xBF, 0 },    /* inv. exclamacion / interrogacion */
    { 0x1A, DK_GRAVE, DK_CIRC, '[' }, { 0x1B, '+', '*', ']' },
    { 0x28, DK_ACUTE, DK_DIAER, '{' }, { 0x2B, 0xE7, 0xC7, '}' },                    /* c cedilla */
    { 0x56, '<', '>', 0 },    { 0x33, ',', ';', 0 },    { 0x34, '.', ':', 0 },    { 0x35, '-', '_', 0 },
    { 0x12, 0, 0, 0x20AC },                                                         /* AltGr+E = EUR */
};
static void kbd_push_cp(uint32_t cp)
{
    if (cp < 0x80u) { kbd_push_byte((char)cp); return; }
    if (cp < 0x800u) { kbd_push_byte((char)(0xC0u | (cp >> 6))); kbd_push_byte((char)(0x80u | (cp & 0x3Fu))); return; }
    kbd_push_byte((char)(0xE0u | (cp >> 12))); kbd_push_byte((char)(0x80u | ((cp >> 6) & 0x3Fu))); kbd_push_byte((char)(0x80u | (cp & 0x3Fu)));
}
static uint32_t es_dead_alone(int dk) { return dk == 1 ? 0xB4u : dk == 2 ? 0xA8u : dk == 4 ? '`' : '^'; }
static uint32_t es_compose(int dk, uint32_t c)
{
    static const char vow[] = "aeiouAEIOU";
    static const uint8_t ac[10] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA };
    static const uint8_t di[10] = { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC };
    static const uint8_t gr[10] = { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 };
    static const uint8_t ci[10] = { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB };
    for (int i = 0; i < 10; i++) if ((uint32_t)(unsigned char)vow[i] == c)
        return dk == 1 ? ac[i] : dk == 2 ? di[i] : dk == 4 ? gr[i] : ci[i];
    return 0;
}
static void es_emit(uint32_t cp)
{
    if (kbd_dead_key) {
        int dk = kbd_dead_key; kbd_dead_key = 0;
        if (cp == ' ') { kbd_push_cp(es_dead_alone(dk)); return; }
        uint32_t c = es_compose(dk, cp);
        if (c) { kbd_push_cp(c); return; }
        kbd_push_cp(es_dead_alone(dk));
    }
    kbd_push_cp(cp);
}
/* Pulsacion (sin Ctrl) con la distribucion espanola. 1 = tratada. */
static int kbd_es_press(uint8_t sc)
{
    for (unsigned i = 0; i < sizeof(es_keys) / sizeof(es_keys[0]); i++) {
        if (es_keys[i].sc != sc) continue;
        uint32_t cp = altgr_active ? es_keys[i].a : (shift_active ? es_keys[i].s : es_keys[i].n);
        if (sc == 0x2B && !altgr_active && caps_active) cp = shift_active ? 0xE7u : 0xC7u;   /* BloqMayus en la c cedilla */
        if (!cp) {
            if (altgr_active) return 1;                          /* AltGr + tecla sin signo: nada */
            break;                                               /* (AltGr+E sin AltGr): letra normal */
        }
        if (cp >= 0x110000u) {                                   /* tecla muerta */
            int dk = (int)(cp - 0x110000u);
            if (kbd_dead_key) { int prev = kbd_dead_key; kbd_dead_key = 0; kbd_push_cp(es_dead_alone(prev)); if (prev == dk) { kbd_push_cp(es_dead_alone(dk)); return 1; } }
            kbd_dead_key = dk;
            return 1;
        }
        es_emit(cp);
        return 1;
    }
    if (altgr_active) return 1;
    if (sc == 0x27) {                                            /* n con tilde */
        es_emit((shift_active ^ caps_active) ? 0xD1u : 0xF1u);
        return 1;
    }
    if (sc >= sizeof(kbd_us_lower)) return 1;
    char ch = shift_active ? kbd_us_upper[sc] : kbd_us_lower[sc];
    if (!ch) return 1;
    if (caps_active) {
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    }
    if (ch == '\b' || ch == '\r' || ch == '\t' || ch == 27) { kbd_dead_key = 0; kbd_push_byte(ch); return 1; }
    es_emit((uint32_t)(unsigned char)ch);
    return 1;
}

void keyboard_set_layout(int es) { kbd_layout_es = es ? 1 : 0; kbd_dead_key = 0; }
int  keyboard_get_layout(void) { return kbd_layout_es; }
void keyboard_layout_auto(void)
{
#ifdef __x86_64__
    if (bootinfo_has_option("soma.teclado=us")) keyboard_set_layout(0);
    else if (bootinfo_has_option("soma.teclado=es") || bootinfo_has_option("soma.maquina=t480s")) keyboard_set_layout(1);
    kprint(kbd_layout_es ? "TECLADO: distribucion espanola (AltGr, acentos, n, c cedilla)\n" : "");
#endif
}

#ifdef __x86_64__
void keyboard_irq_handler(void)
{
    uint8_t status = inb(0x64);
    if (status & 0x20) {
        inb(0x60); /* Descartar byte espurio del ratón para no desincronizar */
        return;
    }
    keyboard_scancode(inb(0x60));
}
#endif

/* PARCHE 047: entrada de scancodes PS/2 (juego 1) ya leidos; la usan el IRQ 1 y el teclado USB (xHCI). */
void keyboard_inject_scancode(uint8_t sc)
{
    unsigned long fl;
#ifdef __x86_64__
    __asm__ volatile ("pushfq; cli; popq %0" : "=r"(fl) :: "memory");        /* el IRQ 1 comparte el estado (Shift, E0, teclas muertas) */
#else
    __asm__ volatile ("mrs %0, daif\n msr daifset, #2" : "=r"(fl) :: "memory");
#endif
    keyboard_scancode(sc);
#ifdef __x86_64__
    __asm__ volatile ("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
#else
    __asm__ volatile ("msr daif, %0" :: "r"(fl) : "memory");
#endif
}

extern void scrcap_hotkey(int ctrl) __attribute__((weak));       /* SOMA_179_T480S: scrcap.c */
extern void hotkey_post(int code) __attribute__((weak));   /* SOMA_168_T480S: backlight.c (solo x86_64) */
/* SOMA_168B_ORBE: la tecla Windows (E0 5B / E0 5C) llama al orbe. En el emulador del Mac, Option (Alt izquierdo, 0x38)
 * pulsada y soltada SOLA hace lo mismo; Option + otra tecla no cuenta (asi no estorba a ningun atajo). La GUI lo recoge. */
static volatile uint32_t kbd_super_count;
static int lalt_down, lalt_other;
uint32_t console_take_super(void) { return __sync_lock_test_and_set(&kbd_super_count, 0); }
/* SOMA_294_AYUDA: F1 (0x3B; el teclado USB llega con el mismo codigo) muestra u oculta la ayuda rapida del escritorio. Antes no
 * hacia nada. La GUI lo recoge, como la tecla Windows. */
static volatile uint32_t kbd_help_count;
uint32_t console_take_help(void) { return __sync_lock_test_and_set(&kbd_help_count, 0); }
/* SOMA_245_PTT: el Alt izquierdo es "pulsar para hablar" en todo el sistema. Con el modo encendido (console_set_alt_voice), el
 * teclado solo informa: Alt abajo/arriba, numero de pulsaciones y si se toco otra tecla con Alt (atajo: no es dictado). La GUI
 * graba mientras esta pulsado y, si fue solo un toque, llama al orbe como antes (console_post_super). */
static volatile int kbd_alt_voice, kbd_ptt_down, kbd_ptt_other;
static volatile uint32_t kbd_ptt_seq;
void console_set_alt_voice(int on) { kbd_alt_voice = on ? 1 : 0; }
void console_ptt_state(int *down, int *other, uint32_t *seq) { *down = kbd_ptt_down; *other = kbd_ptt_other; *seq = kbd_ptt_seq; }
void console_post_super(void) { kbd_super_count++; kbd_events++; }
/* inyecta texto (UTF-8) en la cola del teclado, como si se escribiera: lo usa el dictado. Devuelve los bytes que cupieron. */
uint32_t console_inject_text(const char *t)
{
    uint32_t n = 0;
    unsigned long fl;
#ifdef __x86_64__
    __asm__ volatile ("pushfq; cli; popq %0" : "=r"(fl) :: "memory");        /* el IRQ del teclado tambien escribe en la cola */
#else
    __asm__ volatile ("mrs %0, daif\n msr daifset, #2" : "=r"(fl) :: "memory");
#endif
    for (; t && t[n]; n++) {
        uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
        if (next == kbd_tail) break;
        kbd_mod[kbd_head] = 0; kbd_buf[kbd_head] = t[n]; kbd_head = next; kbd_events++;
    }
#ifdef __x86_64__
    __asm__ volatile ("pushq %0; popfq" :: "r"(fl) : "memory", "cc");
#else
    __asm__ volatile ("msr daif, %0" :: "r"(fl) : "memory");
#endif
    return n;
}
static void keyboard_scancode(uint8_t sc)
{
    if (sc == 0xE0) {
        e0_prefix = 1;
        return;
    }
    if (lalt_down && !(sc & 0x80) && (sc != 0x38 || e0_prefix)) { lalt_other = 1; kbd_ptt_other = 1; }   /* SOMA_168B: Option acompanada */
    if (e0_prefix) {
        e0_prefix = 0;
        if (sc == 0x5B || sc == 0x5C) { kbd_super_count++; kbd_events++; return; }   /* SOMA_168B: tecla Windows */
        if (sc == 0xDB || sc == 0xDC) return;
        if (sc == 0x38) { altgr_active = 1; return; }            /* SOMA_155_T480S: AltGr */
        if (sc == 0xB8) { altgr_active = 0; return; }
        if (sc == 0x37) { if (scrcap_hotkey) scrcap_hotkey(ctrl_active); return; }   /* SOMA_179_T480S: ImprPant (con Control: video) */
        if (sc == 0x30 || sc == 0x2E || sc == 0x20) {            /* SOMA_168_T480S: volumen +, volumen -, silencio (Fn+F3/F2/F1) */
            if (hotkey_post) hotkey_post(sc == 0x30 ? 3 : sc == 0x2E ? 4 : 5);
            return;
        }
        if (sc == 0xB0 || sc == 0xAE || sc == 0xA0) return;
        if (sc == 0x1D) { ctrl_active = 1; return; }             /* Ctrl derecho */
        if (sc == 0x9D) { ctrl_active = 0; return; }
        char k = 0;
        if (sc == 0x48) k = 16;
        else if (sc == 0x50) k = 14;
        else if (sc == 0x4B) k = 2;
        else if (sc == 0x4D) k = 6;
        else if (sc == 0x47) k = 1;
        else if (sc == 0x4F) k = 5;
        else if (sc == 0x49) k = 28;          /* SOMA_PARCHE_024_TERM: RePag */
        else if (sc == 0x51) k = 29;          /* AvPag */
        else if (sc == 0x53) k = 4;           /* Supr */
        if (k != 0) kbd_push_byte(k);   /* PARCHE 094: con sus modificadores */
        return;
    }
    if (sc == 0x38) {                                                                   /* SOMA_168B: Option / Alt izquierdo */
        if (!lalt_down) { lalt_down = 1; lalt_other = 0; kbd_ptt_other = 0; kbd_ptt_down = 1; kbd_ptt_seq++; kbd_events++; }   /* SOMA_245_PTT */
        return;
    }
    if (sc == 0xB8) { if (lalt_down && !lalt_other && !kbd_alt_voice) { kbd_super_count++; kbd_events++; } lalt_down = 0; kbd_ptt_down = 0; kbd_events++; return; }
    if (sc == 0x1D) {
        ctrl_active = 1;
        return;
    }
    if (sc == 0x9D) {
        ctrl_active = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_active = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift_active = 0;
        return;
    }
    if (sc == 0x3A) {
        caps_active = !caps_active;
        return;
    }
    if (sc == 0x3B) { kbd_help_count++; kbd_events++; return; }                        /* SOMA_294_AYUDA: F1 */
    if (sc & 0x80) {
        return;
    }
    if (kbd_layout_es) {                                          /* SOMA_155_T480S */
        if (ctrl_active) {
            if (sc == 0x1B || sc == 0x4E) { kbd_dead_key = 0; kbd_push_byte((char)31); return; }   /* Ctrl + (tecla + espanola) */
            if (sc == 0x35 || sc == 0x4A) { kbd_dead_key = 0; kbd_push_byte((char)30); return; }   /* Ctrl - */
        } else if (kbd_es_press(sc)) return;
    }
    if (sc < sizeof(kbd_us_lower)) {
        /* Soporte de teclado español estándar & dead-keys */
        if (!ctrl_active) {
            /* Tecla Ñ / ñ (scancode 0x27) */
            if (sc == 0x27) {
                kbd_dead_key = 0;
                if (shift_active ^ caps_active) {
                    kbd_push_byte((char)0xC3); kbd_push_byte((char)0x91); /* Ñ */
                } else {
                    kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB1); /* ñ */
                }
                return;
            }

            /* Tecla Acento agudo / Diéresis (scancode 0x1A: junto a la P) */
            if (sc == 0x1A) {
                if (shift_active) kbd_dead_key = 2; /* Diéresis ¨ */
                else              kbd_dead_key = 1; /* Acento agudo ´ */
                return;
            }

            /* Signos ¡ y ¿ (scancode 0x0D en teclado ES) */
            if (sc == 0x0D) {
                kbd_dead_key = 0;
                if (shift_active) {
                    kbd_push_byte((char)0xC2); kbd_push_byte((char)0xBF); /* ¿ */
                } else {
                    kbd_push_byte((char)0xC2); kbd_push_byte((char)0xA1); /* ¡ */
                }
                return;
            }

            /* Shift + , -> ;  y  Shift + . -> : en layout ES */
            if (shift_active && sc == 0x33) { kbd_dead_key = 0; kbd_push_byte(';'); return; }
            if (shift_active && sc == 0x34) { kbd_dead_key = 0; kbd_push_byte(':'); return; }
        }

        /* SOMA_PARCHE_024_TERM: Ctrl + / Ctrl - (tambien con el teclado numerico) cambian el tamano del texto */
        if (ctrl_active && (sc == 0x0D || sc == 0x4E)) { kbd_dead_key = 0; kbd_push_byte((char)31); return; }
        if (ctrl_active && (sc == 0x0C || sc == 0x4A)) { kbd_dead_key = 0; kbd_push_byte((char)30); return; }
        char ch = shift_active ? kbd_us_upper[sc] : kbd_us_lower[sc];
        if (ctrl_active) {
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 1);
            else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 1);
            kbd_dead_key = 0;
            if (ch != 0) kbd_push_byte(ch);
            return;
        }

        if (caps_active) {
            if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
        }

        if (ch != 0) {
            /* Dead-keys alternativas (layout US-International: ' y ~) */
            if (sc == 0x28 && !shift_active && kbd_dead_key == 0) {
                kbd_dead_key = 1;
                return;
            }
            if (sc == 0x29 && shift_active && kbd_dead_key == 0) {
                kbd_dead_key = 3;
                return;
            }

            if (kbd_dead_key != 0) {
                int dk = kbd_dead_key;
                kbd_dead_key = 0;
                if (ch == ' ') {
                    if (dk == 1)      kbd_push_byte((char)0x27);
                    else if (dk == 2) kbd_push_byte((char)0x22);
                    else if (dk == 3) kbd_push_byte((char)0x7E);
                } else {
                    kbd_push_utf8_char(ch, dk);
                }
            } else {
                kbd_push_byte(ch);
            }
        }
    }
}
#endif

void keyboard_reset_state(void)
{
#if defined(__x86_64__) || defined(__aarch64__)
    shift_active = 0;
    caps_active = 0;
    ctrl_active = 0;
    altgr_active = 0;
    e0_prefix = 0;
    kbd_head = 0;
    kbd_tail = 0;
    kbd_dead_key = 0;
#endif
}

int kgetc_ready(void)
{
#ifdef __x86_64__
    if (kbd_head != kbd_tail) {
        return 1;
    }
    if (uart_absent) return 0;                              /* SOMA_155B_T480S: sin puerto serie (portatil) */
    uint8_t lsr = inb(COM1 + 5);
    return lsr != 0xFF && (lsr & 0x01) != 0;
#else
    if (kbd_head != kbd_tail) return 1;
    return !(*PL011_UARTFR & (1 << 4));
#endif
}

char kgetc(void)
{
    while (!kgetc_ready()) {
        /* SOMA_PARCHE_007_IDLE: dormir 1 ms en vez de ceder en bucle. Con kernel_main
         * siempre "listo", el hilo idle (que hace HLT) nunca corria: 100% de CPU. */
        thread_sleep(1);
        arch_pause();
    }
#ifdef __x86_64__
    if (kbd_head != kbd_tail) {
        char ch = kbd_buf[kbd_tail];
        kbd_last_mod = kbd_mod[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        return ch;
    }
    return (char)inb(COM1);
#else
    if (kbd_head != kbd_tail) {
        char ch = kbd_buf[kbd_tail];
        kbd_last_mod = kbd_mod[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        return ch;
    }
    return (char)(*PL011_UARTDR & 0xFF);
#endif
}

#define CMD_HISTORY_MAX 16
/* CMD_LINE_MAX definido en console.h (4096) */

static char cmd_history[CMD_HISTORY_MAX][CMD_LINE_MAX];
static int history_count = 0;
static int history_tail = 0;

/* SOMA_193: el historial se guarda en /etc/history.txt. `wifi conectar <red> <clave>` y `wpa psk <ssid> <clave>` dejaban la
 * contrasena en claro en el disco. Se guarda la orden con la clave sustituida por ***; al cargar un historial antiguo se limpia igual. */
static const char *history_scrub(const char *cmd, char *tmp, uint32_t cap)
{
    static const char *const pre[] = { "wifi conectar ", "wpa psk ", "llm-config clave ", "llm-config lanzador clave ", 0 };   /* SOMA_195: tambien la clave de la API del LLM */
    static const int skip[] = { 1, 1, 0, 0 };                       /* palabras que van antes del secreto (el nombre de la red) */
    const char *s = cmd; while (*s == ' ') s++;
    for (int k = 0; pre[k]; ++k) {
        uint32_t n = 0; while (pre[k][n] && s[n] == pre[k][n]) n++;
        if (pre[k][n]) continue;
        const char *a = s + n; while (*a == ' ') a++;
        if (!skip[k]) { a = s + n - 1; }
        else if (*a == '"' || *a == '\'') { char q = *a++; while (*a && *a != q) a++; if (*a) a++; }      /* nombre de red entre comillas */
        else while (*a && *a != ' ') a++;
        const char *after = a; while (*after == ' ') after++;
        if (!*after) return cmd;                                                                    /* sin clave: nada que ocultar */
        uint32_t o = 0;
        for (const char *c = s; c < a && o < cap - 5; ++c) tmp[o++] = *c;
        tmp[o++] = ' '; tmp[o++] = '*'; tmp[o++] = '*'; tmp[o++] = '*'; tmp[o] = '\0';
        return tmp;
    }
    return cmd;
}

static void history_add(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;
    static char scrub_tmp[160];
    cmd = history_scrub(cmd, scrub_tmp, sizeof scrub_tmp);

    if (history_count > 0) {
        int last_idx = (history_tail - 1 + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        int same = 1;
        for (int i = 0; cmd[i] || cmd_history[last_idx][i]; ++i) {
            if (cmd[i] != cmd_history[last_idx][i]) { same = 0; break; }
        }
        if (same) return;
    }

    int idx = history_tail;
    uint32_t i = 0;
    while (cmd[i] && i < CMD_LINE_MAX - 1) {
        cmd_history[idx][i] = cmd[i];
        i++;
    }
    cmd_history[idx][i] = '\0';

    history_tail = (history_tail + 1) % CMD_HISTORY_MAX;
    if (history_count < CMD_HISTORY_MAX) history_count++;
    console_history_sync_to_vfs();
}

#define KEY_ACT_UP    1001
#define KEY_ACT_DOWN  1002
#define KEY_ACT_LEFT  1003
#define KEY_ACT_RIGHT 1004
#define KEY_ACT_HOME  1005
#define KEY_ACT_END   1006
#define KEY_ACT_TAB   '\t'
#define KEY_ACT_ENTER '\n'
#define KEY_ACT_BS    '\b'

static int read_key_action(void)
{
    char c = kgetc();
    if (c == 16) return KEY_ACT_UP;
    if (c == 14) return KEY_ACT_DOWN;
    if (c == 2)  return KEY_ACT_LEFT;
    if (c == 6)  return KEY_ACT_RIGHT;
    if (c == 1)  return KEY_ACT_HOME;
    if (c == 5)  return KEY_ACT_END;

    if (c == 27) {
        if (kgetc_ready()) {
            char c2 = kgetc();
            if (c2 == '[') {
                char c3 = kgetc();
                if (c3 == 'A') return KEY_ACT_UP;
                if (c3 == 'B') return KEY_ACT_DOWN;
                if (c3 == 'C') return KEY_ACT_RIGHT;
                if (c3 == 'D') return KEY_ACT_LEFT;
                if (c3 == 'H') return KEY_ACT_HOME;
                if (c3 == 'F') return KEY_ACT_END;
                if (c3 == '1' || c3 == '4') {
                    if (kgetc_ready()) {
                        char c4 = kgetc();
                        if (c4 == '~') {
                            if (c3 == '1') return KEY_ACT_HOME;
                            if (c3 == '4') return KEY_ACT_END;
                        }
                    }
                }
            }
        }
        return 27;
    }

    if (c == '\r') return KEY_ACT_ENTER;
    if (c == 0x7F || c == 8) return KEY_ACT_BS;

    return (int)(uint8_t)c;
}

static uint32_t utf8_vis_len(const char *s, uint32_t byte_len)
{
    uint32_t vis = 0;
    for (uint32_t i = 0; i < byte_len; ++i) {
        if (((uint8_t)s[i] & 0xC0) != 0x80) {
            vis++;
        }
    }
    return vis;
}

static void redraw_prompt_line(const char *line, uint32_t len, uint32_t cursor, uint32_t prev_len)
{
    kputc('\r');
    kprint("\033[1;36msoma\033[1;32m>\033[0m ");

    for (uint32_t i = 0; i < len; ++i) {
        kputc(line[i]);
    }

    uint32_t vis_len = utf8_vis_len(line, len);
    uint32_t vis_prev = utf8_vis_len(line, prev_len);
    uint32_t max_v = vis_len;
    if (vis_prev > vis_len) {
        for (uint32_t i = vis_len; i < vis_prev; ++i) kputc(' ');
        max_v = vis_prev;
    }

    uint32_t vis_cursor = utf8_vis_len(line, cursor);
    uint32_t steps_back = (max_v > vis_cursor) ? (max_v - vis_cursor) : 0;
    if (steps_back > 0) {
#ifdef __x86_64__
        if (vga_col >= steps_back) vga_col -= steps_back;
        else vga_col = 0;
        vga_update_cursor();
#endif
        for (uint32_t i = 0; i < steps_back; ++i) {
            kputc('\b');
        }
    }
}

int kgetline(char *buf, uint32_t max)
{
    if (!buf || max == 0) return 0;
    if (cap_mine() && cap_live) {                            /* SOMA_237: entrada viva: espera la linea que de el terminal */
        while (cap_buf && !cap_feed_eof && !feed_has_line()) thread_sleep(10);
        uint32_t n = 0, k = 0;
        if (cap_buf && feed_has_line()) {
            while (cap_feed[k] != '\n') { if (n + 1 < max) buf[n++] = cap_feed[k]; k++; }
            k++;
            for (uint32_t i = k; i < cap_feed_len; ++i) cap_feed[i - k] = cap_feed[i];
            cap_feed_len -= k;
        }
        buf[n] = 0; return (int)n;
    }
    if (cap_mine()) {                                        /* PARCHE 116: entrada simulada; sin mas lineas, linea vacia (nunca bloquea) */
        uint32_t n = 0;
        while (cap_in && *cap_in && *cap_in != '\n' && n + 1 < max) buf[n++] = *cap_in++;
        if (cap_in && *cap_in == '\n') cap_in++;
        buf[n] = 0; return (int)n;
    }

    char line[CMD_LINE_MAX];
    char temp_line[CMD_LINE_MAX];
    uint32_t len = 0;
    uint32_t cursor = 0;
    uint32_t prev_len = 0;
    int hist_idx = -1;

    line[0] = '\0';
    temp_line[0] = '\0';

    for (;;) {
        int act = read_key_action();

        if (act == KEY_ACT_ENTER) {
            kputc('\r');
            kputc('\n');
            line[len] = '\0';
            history_add(line);

            uint32_t copy_n = (len < max - 1) ? len : max - 1;
            for (uint32_t i = 0; i < copy_n; ++i) buf[i] = line[i];
            buf[copy_n] = '\0';
            return (int)copy_n;
        }

        if (act == KEY_ACT_BS) {
            if (cursor > 0) {
                prev_len = len;
                uint32_t del_bytes = 1;
                /* Manejo de borrado atómico para caracteres UTF-8 multibyte */
                if (((uint8_t)line[cursor - 1] & 0xC0) == 0x80) {
                    while (cursor > del_bytes && ((uint8_t)line[cursor - del_bytes] & 0xC0) == 0x80) {
                        del_bytes++;
                    }
                }
                for (uint32_t i = cursor - del_bytes; i + del_bytes <= len; ++i) {
                    line[i] = line[i + del_bytes];
                }
                cursor -= del_bytes;
                len -= del_bytes;
                line[len] = '\0';
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_LEFT) {
            if (cursor > 0) {
                cursor--;
                while (cursor > 0 && ((uint8_t)line[cursor] & 0xC0) == 0x80) cursor--;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_RIGHT) {
            if (cursor < len) {
                cursor++;
                while (cursor < len && ((uint8_t)line[cursor] & 0xC0) == 0x80) cursor++;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_HOME) {
            if (cursor > 0) {
                cursor = 0;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_END) {
            if (cursor < len) {
                cursor = len;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_UP) {
            if (history_count == 0) continue;
            prev_len = len;

            if (hist_idx == -1) {
                for (uint32_t i = 0; i <= len; ++i) temp_line[i] = line[i];
                hist_idx = history_count - 1;
            } else if (hist_idx > 0) {
                hist_idx--;
            }

            int start_idx = (history_tail - history_count + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
            int target_idx = (start_idx + hist_idx) % CMD_HISTORY_MAX;
            len = 0;
            while (cmd_history[target_idx][len] && len < sizeof(line) - 1) {
                line[len] = cmd_history[target_idx][len];
                len++;
            }
            line[len] = '\0';
            cursor = len;
            redraw_prompt_line(line, len, cursor, prev_len);
            continue;
        }

        if (act == KEY_ACT_DOWN) {
            if (hist_idx == -1) continue;
            prev_len = len;

            if (hist_idx < history_count - 1) {
                hist_idx++;
                int start_idx = (history_tail - history_count + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
                int target_idx = (start_idx + hist_idx) % CMD_HISTORY_MAX;
                len = 0;
                while (cmd_history[target_idx][len] && len < sizeof(line) - 1) {
                    line[len] = cmd_history[target_idx][len];
                    len++;
                }
                line[len] = '\0';
                cursor = len;
            } else {
                hist_idx = -1;
                len = 0;
                while (temp_line[len] && len < sizeof(line) - 1) {
                    line[len] = temp_line[len];
                    len++;
                }
                line[len] = '\0';
                cursor = len;
            }
            redraw_prompt_line(line, len, cursor, prev_len);
            continue;
        }

        if (act == KEY_ACT_TAB) {
            int has_space = 0;
            for (uint32_t i = 0; i < cursor; ++i) {
                if (line[i] == ' ') { has_space = 1; break; }
            }
            if (!has_space) {
                const char *matches[16];
                int match_count = shell_autocomplete(line, cursor, matches, 16);

                if (match_count == 1) {
                    prev_len = len;
                    const char *m = matches[0];
                    len = 0;
                    while (m[len] && len < sizeof(line) - 2) {
                        line[len] = m[len];
                        len++;
                    }
                    line[len++] = ' ';
                    line[len] = '\0';
                    cursor = len;
                    redraw_prompt_line(line, len, cursor, prev_len);
                } else if (match_count > 1) {
                    kprint("\n");
                    for (int m = 0; m < match_count; ++m) {
                        kprint("  ");
                        kprint(matches[m]);
                    }
                    kprint("\n");
                    prev_len = len;
                    redraw_prompt_line(line, len, cursor, prev_len);
                }
            }
            continue;
        }

        /* Admisión de caracteres ASCII y bytes UTF-8 (>= 32) */
        if ((uint8_t)act >= 32) {
            if (len < sizeof(line) - 2) {
                if (cursor == len) {
                    line[len] = (char)act;
                    len++;
                    cursor++;
                    line[len] = '\0';
                    kputc((char)act);
                } else {
                    prev_len = len;
                    for (uint32_t i = len + 1; i > cursor; --i) {
                        line[i] = line[i - 1];
                    }
                    line[cursor] = (char)act;
                    len++;
                    cursor++;
                    line[len] = '\0';
                    redraw_prompt_line(line, len, cursor, prev_len);
                }
            }
        }
    }
}

int console_history_dump(char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    kprint("\nHISTORIAL DE COMANDOS:\n----------------------\n");
    for (int i = 0; i < history_count; ++i) {
        int idx = (history_tail - history_count + i + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        kprint("  ");
        kprint_dec((uint32_t)(i + 1));
        kprint("  ");
        kprint(cmd_history[idx]);
        kprint("\n");

        if (out_buf && pos < max_out - 64) {
            char tb[10]; int tn = 0; uint32_t v = (uint32_t)(i + 1);
            while (v) { tb[tn++] = (char)('0' + v % 10); v /= 10; }
            while (tn > 0) out_buf[pos++] = tb[--tn];
            out_buf[pos++] = ' '; out_buf[pos++] = ' ';
            const char *p = cmd_history[idx];
            while (*p && pos < max_out - 2) out_buf[pos++] = *p++;
            out_buf[pos++] = '\n';
        }
    }
    if (out_buf) out_buf[pos] = '\0';
    kprint("----------------------\n");
    return 1;
}

void console_history_sync_to_vfs(void)
{
    static char hist_buf[2048];
    uint32_t pos = 0;
    for (int i = 0; i < history_count; ++i) {
        int idx = (history_tail - history_count + i + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        const char *p = cmd_history[idx];
        while (*p && pos < sizeof(hist_buf) - 2) hist_buf[pos++] = *p++;
        hist_buf[pos++] = '\n';
    }
    if (pos > 0) {
        vfs_write("/etc/history.txt", hist_buf, pos);
    }
}

void console_history_load_from_vfs(void)
{
    static char load_buf[2048];
    int r = vfs_read("/etc/history.txt", load_buf, sizeof(load_buf) - 1);
    if (r <= 0) return;
    load_buf[r] = '\0';

    const char *p = load_buf;
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;
        if (*p == '\0') break;

        char line[CMD_LINE_MAX];
        uint32_t lp = 0;
        while (*p && *p != '\n' && *p != '\r' && lp < sizeof(line) - 1) {
            line[lp++] = *p++;
        }
        line[lp] = '\0';
        if (lp > 0) {
            history_add(line);
        }
    }
}

/* SOMA_PARCHE_PORTAPAPELES: estado de Ctrl y Mayus para los atajos de edicion */
int keyboard_ctrl_held(void)
{
#ifdef __x86_64__
    return ctrl_active;
#else
    return 0;
#endif
}
int keyboard_shift_held(void)
{
#ifdef __x86_64__
    return shift_active;
#else
    return 0;
#endif
}

/* PARCHE 094: modificadores que acompanaban a la ultima tecla leida con kgetc (no dependen de cuando se procese) */
int keyboard_last_mods(void) { return kbd_last_mod; }
