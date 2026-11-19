/* SOMA_PARCHE_016_EXEC: biblioteca minima para programas de usuario (sin libc).
 * Se compilan con -mgeneral-regs-only: el kernel NO guarda el estado SSE/x87 al cambiar de proceso. */
#ifndef SOMA_ULIB_H
#define SOMA_ULIB_H

typedef unsigned long u64;
typedef long i64;

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_YIELD  2
#define SYS_SLEEP  3
#define SYS_GETPID 4
#include "errno.h"            /* PARCHE 124: codigos de error unicos del sistema (SOMA_E*), los mismos que ve el kernel */
#define SYS_OPEN    5
#define SYS_READ    6
#define SYS_SEEK    7
#define SYS_CLOSE   8
#define SYS_FSIZE   9
#define SYS_GETARGS 10
#define SYS_AUDIO_OPEN  11
#define SYS_AUDIO_WRITE 12
#define SYS_AUDIO_CLOSE 13
#define SYS_FB_OPEN  14
#define SYS_FB_INFO  15
#define SYS_FB_BLIT  16
#define SYS_FB_CLOSE 17
#define SYS_AUDIO_STATUS 18
#define SYS_UPTIME_MS    19

/* El kernel destruye rcx, r11 y rdi/rsi/rdx/r8-r10 en cada llamada; conserva rbx, rbp, r12-r15, rsp. */
static inline i64 u_sys3(i64 nr, i64 a, i64 b, i64 c)
{
    i64 r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

static inline void u_exit(i64 code) { u_sys3(SYS_EXIT, code, 0, 0); for (;;) { } }
static inline i64  u_write(const char *p, u64 n) { return u_sys3(SYS_WRITE, (i64)p, (i64)n, 0); }
static inline void u_sleep(u64 ms) { u_sys3(SYS_SLEEP, (i64)ms, 0, 0); }
static inline void u_yield(void) { u_sys3(SYS_YIELD, 0, 0, 0); }
static inline i64  u_getpid(void) { return u_sys3(SYS_GETPID, 0, 0, 0); }

/* Audio: PCM 16 bits estereo. u_audio_write bloquea hasta aceptar todo; u_audio_close(1) espera a que suene lo escrito. */
static inline i64 u_audio_open(u64 rate) { return u_sys3(SYS_AUDIO_OPEN, (i64)rate, 0, 0); }
static inline i64 u_audio_write(const void *buf, u64 n) { return u_sys3(SYS_AUDIO_WRITE, (i64)buf, (i64)n, 0); }
static inline i64 u_audio_status(u64 out[3]) { return u_sys3(SYS_AUDIO_STATUS, (i64)out, 0, 0); }
static inline u64 u_uptime_ms(void) { return (u64)u_sys3(SYS_UPTIME_MS, 0, 0, 0); }
static inline i64 u_audio_close(int drain) { return u_sys3(SYS_AUDIO_CLOSE, drain, 0, 0); }

/* Pantalla en exclusiva: pixeles XRGB de 32 bits. u_fb_blit copia un rectangulo w x h (w*4 bytes por fila) en (x, y). */
static inline i64 u_fb_open(u64 w, u64 h) { return u_sys3(SYS_FB_OPEN, (i64)w, (i64)h, 0); }
static inline i64 u_fb_info(unsigned int out[3]) { return u_sys3(SYS_FB_INFO, (i64)out, 0, 0); }
static inline i64 u_fb_blit(const void *px, u64 w, u64 h, u64 x, u64 y) { return u_sys3(SYS_FB_BLIT, (i64)px, (i64)((w << 16) | h), (i64)((x << 16) | y)); }
static inline i64 u_fb_close(void) { return u_sys3(SYS_FB_CLOSE, 0, 0, 0); }

/* Ficheros del almacen BIGFS (solo lectura). Errores: valores negativos de errno.h (SOMA_ENOENT -2 no existe, SOMA_EFAULT -14 puntero invalido, SOMA_EACCES -13 ruta privada, ...). */
static inline u64 u_strlen(const char *s);
static inline i64 u_open(const char *path) { return u_sys3(SYS_OPEN, (i64)path, (i64)u_strlen(path), 0); }
static inline i64 u_read(i64 fd, void *buf, u64 n) { return u_sys3(SYS_READ, fd, (i64)buf, (i64)n); }
static inline i64 u_seek(i64 fd, i64 off, i64 whence) { return u_sys3(SYS_SEEK, fd, off, whence); }   /* whence: 0 inicio, 1 actual, 2 final */
static inline i64 u_close(i64 fd) { return u_sys3(SYS_CLOSE, fd, 0, 0); }
static inline i64 u_fsize(i64 fd) { return u_sys3(SYS_FSIZE, fd, 0, 0); }
static inline i64 u_getargs(char *buf, u64 n) { return u_sys3(SYS_GETARGS, (i64)buf, (i64)n, 0); }

static inline u64 u_strlen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static inline void u_puts(const char *s) { u_write(s, u_strlen(s)); }
static inline void u_putdec(u64 v)
{
    char b[24]; int i = 23; b[i] = 0;
    if (v == 0) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    u_puts(b + i);
}

#endif
