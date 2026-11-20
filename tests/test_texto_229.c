/* SOMA_229_TEXTO: prueba de equivalencia. Cada archivo del nucleo tenia su propia copia de "anadir texto/numero a un buffer";
 * ahora todos usan src/lang/soma_str.h. Aqui estan las copias ANTIGUAS, literales, y se comprueba con secuencias al azar que
 * la nueva escritura deja exactamente los mismos bytes (y la misma posicion) que cada una de ellas, en los estados a los que
 * llegan sus llamadores (buffer recien empezado: posicion 0 y NUL al principio). Tambien se vigila que nadie escriba fuera.
 * Uso: test_texto_229 [iteraciones] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_str.h"

/* ---------------- copias literales de las versiones antiguas ---------------- */
/* hwscan.c */
struct tb { char *b; uint32_t max, p; };
static void tc(struct tb *t, char c) { if (t->p + 1 < t->max) { t->b[t->p++] = c; t->b[t->p] = 0; } }
static void ts(struct tb *t, const char *s) { while (s && *s) tc(t, *s++); }
static void tdec(struct tb *t, uint64_t v) { char d[24]; int n = 0; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) tc(t, d[--n]); }
static void thex(struct tb *t, uint64_t v, int digits) { for (int i = digits - 1; i >= 0; i--) tc(t, "0123456789ABCDEF"[(v >> (i * 4)) & 15]); }
/* bootdiag.c (228) */
struct obd { char *b; uint32_t max, n; };
static void os(struct obd *o, const char *s) { while (s && *s && o->n + 1 < o->max) o->b[o->n++] = *s++; o->b[o->n] = 0; }
static void od(struct obd *o, uint64_t v) { char d[24]; int n = 0; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) { char c[2] = { d[--n], 0 }; os(o, c); } }
static void ox(struct obd *o, uint32_t v, int digits) { for (int i = digits - 1; i >= 0; i--) { char c[2] = { "0123456789ABCDEF"[(v >> (i * 4)) & 15], 0 }; os(o, c); } }
/* bigfs_glue.c */
struct ob { char *p; uint32_t max, pos; };
static void ob_s(struct ob *o, const char *s) { while (*s && o->pos + 1 < o->max) o->p[o->pos++] = *s++; o->p[o->pos < o->max ? o->pos : o->max - 1] = 0; }
static void ob_d(struct ob *o, uint64_t v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10); v /= 10; } ob_s(o, b + i); }
/* shell.c */
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
static void usb_hex(char *b, uint32_t m, uint32_t *p, uint32_t v, int digits)
{
    const char hx[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) fb_putc(b, m, p, hx[(v >> (4 * i)) & 15]);
}
/* gui.c */
static void ot_puts(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void ot_dec(char *o, uint32_t max, uint32_t *p, uint32_t v) { char d[12]; int n = 0; do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v); char t[12]; int k = 0; while (n) t[k++] = d[--n]; t[k] = 0; ot_puts(o, max, p, t); }
/* hda.c */
static void put_s(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p] = 0; }
/* net.c */
static void append_ch(char *dst, uint32_t max, uint32_t *pos, char c)
{
    if (*pos + 1 < max) { dst[(*pos)++] = c; dst[*pos] = '\0'; }
}
static void append_str_arp(char *dst, uint32_t max, uint32_t *pos, const char *s)
{
    while (*s && *pos + 1 < max) dst[(*pos)++] = *s++;
    dst[*pos] = '\0';
}
static void append_dec_u8(char *dst, uint32_t max, uint32_t *pos, uint8_t v)
{
    char tmp[4]; int n = 0;
    if (v == 0) { append_ch(dst, max, pos, '0'); return; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) append_ch(dst, max, pos, tmp[--n]);
}
static void append_hex_nibble(char *dst, uint32_t max, uint32_t *pos, uint8_t n)
{
    n &= 0x0F;
    append_ch(dst, max, pos, n < 10 ? (char)('0' + n) : (char)('A' + n - 10));
}
static void append_hex_u8(char *dst, uint32_t max, uint32_t *pos, uint8_t v)
{
    append_hex_nibble(dst, max, pos, v >> 4);
    append_hex_nibble(dst, max, pos, v);
}
/* httpd.c */
static void buf_append(char *dst, uint32_t max, uint32_t *pos, const char *src)
{
    while (*src && *pos < max - 1) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = '\0';
}
static void buf_append_dec(char *dst, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        if (*pos + 1 < max) dst[(*pos)++] = '0';
        dst[*pos] = '\0';
        return;
    }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && *pos + 1 < max) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}
/* pcie.c (Pi 5) */
static void put_pcie(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; o[*p < max ? *p : max - 1] = 0; }
static void puthex_pcie(char *o, uint32_t max, uint32_t *p, uint64_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) { char c = "0123456789abcdef"[(v >> (4 * i)) & 15]; if (*p + 1 < max) o[(*p)++] = c; }
    if (*p < max) o[*p] = 0;
}
/* rp1.c (Pi 5) */
static void put_rp1(char *o, uint32_t max, uint32_t *p, const char *s) { while (*s && *p + 1 < max) o[(*p)++] = *s++; if (max) o[*p < max ? *p : max - 1] = 0; }
static void puthex_rp1(char *o, uint32_t max, uint32_t *p, uint64_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) { char c = "0123456789abcdef"[(v >> (4 * i)) & 15]; if (*p + 1 < max) o[(*p)++] = c; }
    if (max) o[*p < max ? *p : max - 1] = 0;
}

/* ---------------- banco de pruebas ---------------- */
#define G 16                                   /* bytes de guarda detras del buffer */
static const char *const STR[] = { "", "a", "hola", "0123456789", "Disco del sistema: ", "x\ny", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij" };
static uint64_t rnd64(void) { uint64_t v = ((uint64_t)rand() << 33) ^ ((uint64_t)rand() << 11) ^ (uint64_t)rand(); switch (rand() % 5) { case 0: return 0; case 1: return v % 10; case 2: return v % 100000; case 3: return v & 0xFFFFFFFFu; default: return v; } }

static long ops, fails;
static char A[64 + G], B[64 + G];
static void fresh(uint32_t cap) { memset(A, 0x55, sizeof A); memset(B, 0x55, sizeof B); A[0] = 0; B[0] = 0; (void)cap; }
static void cmp(const char *fam, int op, uint32_t cap, uint32_t pa, uint32_t pb)
{
    ops++;
    if (pa != pb || memcmp(A, B, cap + G) != 0) {
        if (fails++ < 10) printf("DIFERENCIA en %s (op %d, tamano %u): pos %u / %u\n", fam, op, cap, pa, pb);
    }
}

int main(int argc, char **argv)
{
    long it = argc > 1 ? atol(argv[1]) : 200000;
    srand(229);
    for (long n = 0; n < it; n++) {
        uint32_t cap = 1 + (uint32_t)(rand() % 48);
        int fam = rand() % 11, steps = 1 + rand() % 12;
        fresh(cap);
        uint32_t pa = 0, pb = 0;
        struct tb t = { A, cap, 0 }; struct obd o = { A, cap, 0 }; struct ob g = { A, cap, 0 };
        soma_sbuf_t s = { B, cap, 0 };
        for (int k = 0; k < steps; k++) {
            int op = rand() % 3; const char *str = STR[rand() % 7]; uint64_t v = rnd64(); int dig = 1 + rand() % 16;
            switch (fam) {
            case 0:   /* hwscan: struct tb */
                if (op == 0) { ts(&t, str); soma_sbuf_append_str(&s, str); }
                else if (op == 1) { tdec(&t, v); soma_sbuf_append_u64(&s, v); }
                else { thex(&t, v, dig); soma_sbuf_append_hex(&s, v, dig, 1); }
                cmp("hwscan", op, cap, t.p, s.len); break;
            case 1:   /* bootdiag */
                if (op == 0) { os(&o, str); soma_sbuf_append_str(&s, str); }
                else if (op == 1) { od(&o, v); soma_sbuf_append_u64(&s, v); }
                else { ox(&o, (uint32_t)v, dig > 8 ? 8 : dig); soma_sbuf_append_hex(&s, (uint32_t)v, dig > 8 ? 8 : dig, 1); }
                cmp("bootdiag", op, cap, o.n, s.len); break;
            case 2:   /* bigfs */
                if (op == 0) { ob_s(&g, str); soma_sbuf_append_str(&s, str); }
                else { ob_d(&g, v); soma_sbuf_append_u64(&s, v); }
                cmp("bigfs", op, cap, g.pos, s.len); break;
            case 3:   /* shell */
                if (op == 0) { int c = rand() % 3; if (c == 0) { fb_puts(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); } else if (c == 1) { fb_putc(A, cap, &pa, str[0] ? str[0] : 'z'); soma_str_putc(B, cap, &pb, str[0] ? str[0] : 'z'); } else { fb_put_dec(A, cap, &pa, v); soma_str_put_dec(B, cap, &pb, v); } }
                else if (op == 1) { int c = rand() % 3; if (c == 0) { fb_put_hex16(A, cap, &pa, (uint16_t)v); soma_str_put_hex(B, cap, &pb, (uint16_t)v, 4, 1); } else if (c == 1) { fb_put_hex32(A, cap, &pa, (uint32_t)v); soma_str_put_hex(B, cap, &pb, (uint32_t)v, 8, 1); } else { usb_hex(A, cap, &pa, (uint32_t)v, dig > 8 ? 8 : dig); soma_str_put_hex(B, cap, &pb, (uint32_t)v, dig > 8 ? 8 : dig, 0); } }
                else { fb_puts(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); }
                cmp("shell", op, cap, pa, pb); break;
            case 4:   /* gui */
                if (op == 0) { ot_puts(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); } else { ot_dec(A, cap, &pa, (uint32_t)v); soma_str_put_dec(B, cap, &pb, (uint32_t)v); }
                cmp("gui", op, cap, pa, pb); break;
            case 5:   /* hda */
                put_s(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); cmp("hda", op, cap, pa, pb); break;
            case 6:   /* net */
                if (op == 0) { append_str_arp(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); }
                else if (op == 1) { append_dec_u8(A, cap, &pa, (uint8_t)v); soma_str_put_dec(B, cap, &pb, (uint8_t)v); }
                else if (rand() & 1) { append_hex_u8(A, cap, &pa, (uint8_t)v); soma_str_put_hex(B, cap, &pb, (uint8_t)v, 2, 1); }
                else { append_ch(A, cap, &pa, '.'); soma_str_putc(B, cap, &pb, '.'); }
                cmp("net", op, cap, pa, pb); break;
            case 7:   /* httpd */
                if (op == 0) { buf_append(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); } else { buf_append_dec(A, cap, &pa, v); soma_str_put_dec(B, cap, &pb, v); }
                cmp("httpd", op, cap, pa, pb); break;
            case 8:   /* pcie */
                if (op == 0) { put_pcie(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); } else { puthex_pcie(A, cap, &pa, v, dig); soma_str_put_hex(B, cap, &pb, v, dig, 0); }
                cmp("pcie", op, cap, pa, pb); break;
            default:  /* rp1 */
                if (op == 0) { put_rp1(A, cap, &pa, str); soma_str_put(B, cap, &pb, str); } else { puthex_rp1(A, cap, &pa, v, dig); soma_str_put_hex(B, cap, &pb, v, dig, 0); }
                cmp("rp1", op, cap, pa, pb); break;
            }
        }
    }
    if (fails) { printf("texto_229: %ld diferencias en %ld operaciones\n", fails, ops); return 1; }
    printf("texto_229: %ld operaciones en 10 familias: la escritura comun da los mismos bytes que cada copia antigua\n", ops);
    return 0;
}
