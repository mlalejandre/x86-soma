/* SOMA_333_SALIDA: los programas del programador se ejecutan en su propio hilo con la salida capturada, sin abrir el Terminal.
 * Ver coder_run.h. La parte de arriba (el formato de la salida) es pura y se prueba en el anfitrion; la de abajo usa el nucleo. */
#include "coder_run.h"

/* ---------------------------------------------------------------- salida en lineas (puro) */
void crun_buf_vacia(struct crun_buf *b) { b->ini = 0; b->n = 0; b->pn = 0; b->par[0] = 0; b->esc = 0; }
static void linea_nueva(struct crun_buf *b)
{
    int k;
    if (b->n < CRUN_NL) k = (b->ini + b->n++) % CRUN_NL;
    else { k = b->ini; b->ini = (b->ini + 1) % CRUN_NL; }     /* llena: se pierde la mas antigua */
    int i = 0; for (; i < b->pn && i < CRUN_LW - 1; i++) b->l[k][i] = b->par[i];
    b->l[k][i] = 0;
    b->pn = 0; b->par[0] = 0;
}
void crun_buf_mete(struct crun_buf *b, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (b->esc == 1) { b->esc = c == '[' ? 2 : 0; continue; }               /* ESC sin '[': se descarta el siguiente */
        if (b->esc == 2) { if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) b->esc = 0; continue; }
        if (c == 27) { b->esc = 1; continue; }
        if (c == '\r') continue;
        if (c == '\n') { linea_nueva(b); continue; }
        if (c == '\t') { do { if (b->pn < CRUN_LW - 1) b->par[b->pn++] = ' '; } while (b->pn % 4 && b->pn < CRUN_LW - 1); b->par[b->pn] = 0; continue; }
        if (c < 32 || c == 127) continue;
        if (b->pn >= CRUN_LW - 1) {                       /* linea larga: se parte, sin cortar un caracter UTF-8 por la mitad */
            int corte = b->pn;
            if ((c & 0xC0) == 0x80) {                     /* c sigue a un caracter empezado: se corta antes de su primer byte */
                int q = b->pn - 1;
                while (q > 0 && ((unsigned char)b->par[q] & 0xC0) == 0x80) q--;
                if (q > 0) corte = q;
            }
            char resto[8]; int rn = 0;
            for (int k = corte; k < b->pn && rn < 7; k++) resto[rn++] = b->par[k];
            b->pn = corte; linea_nueva(b);
            for (int k = 0; k < rn; k++) b->par[b->pn++] = resto[k];
        }
        b->par[b->pn++] = (char)c; b->par[b->pn] = 0;
    }
}
void crun_buf_cierra(struct crun_buf *b) { if (b->pn) linea_nueva(b); }
const char *crun_buf_linea(const struct crun_buf *b, int i) { return i >= 0 && i < b->n ? b->l[(b->ini + i) % CRUN_NL] : ""; }

#ifndef CRUN_SOLO_FORMATO
/* ---------------------------------------------------------------- el programa en marcha (nucleo) */
#include "console.h"
#include "thread.h"
#include "mem.h"
#include "idt.h"
#include "mutex.h"

int  dispatch_command(const char *cmd, char *out, uint32_t max_out);
int  shell_python_stop(void);
void shell_suelta_al_esperar(int on);            /* src/agent/shell.c: mientras corre el programa, el shell queda libre */

enum { CAP = 64 * 1024 };
static struct crun_buf g_b;
static char *g_cap;                              /* la captura de la consola del hilo del programa */
static char g_cmd[256], g_nombre[96], g_out[2048], g_prompt[CRUN_LW];
static volatile int g_estado, g_viva, g_ocupado, g_parado;
static volatile uint32_t g_capn;                 /* bytes que quedaron en la captura al terminar */
static int g_visible, g_volcado;
static uint64_t g_t0, g_t1;
static struct kmutex g_lk; static volatile int g_lk_ok;   /* la GUI recoge la salida y el shell (o el agente) puede escribirle al programa */
static void lk(void) { if (!g_lk_ok) { kmutex_init(&g_lk, "coder_run"); g_lk_ok = 1; } kmutex_lock(&g_lk); }
static void ulk(void) { kmutex_unlock(&g_lk); }

static void hilo(void *a)
{
    (void)a;
    g_out[0] = 0;
    if (console_capture_active()) { g_ocupado = 1; g_estado = CRUN_FIN; return; }
    console_capture_begin(g_cap, CAP, "");
    console_capture_live(1);                     /* input()/scanf esperan las lineas que se escriban en el panel */
    g_viva = 1;
    shell_suelta_al_esperar(1);
    dispatch_command(g_cmd, g_out, sizeof g_out);
    shell_suelta_al_esperar(0);
    g_viva = 0;
    g_capn = console_capture_end();
    g_t1 = timer_get_uptime_ms();
    g_estado = CRUN_FIN;
}
int crun_start(const char *cmd, const char *nombre)
{
    if (g_estado == CRUN_MARCHA) return -1;
    if (console_capture_active()) return -2;
    if (!g_cap) { g_cap = (char *)kmalloc(CAP); if (!g_cap) return -2; }
    uint32_t i = 0; while (cmd[i] && i < sizeof g_cmd - 1) { g_cmd[i] = cmd[i]; i++; } g_cmd[i] = 0;
    i = 0; while (nombre && nombre[i] && i < sizeof g_nombre - 1) { g_nombre[i] = nombre[i]; i++; } g_nombre[i] = 0;
    lk(); crun_buf_vacia(&g_b); g_prompt[0] = 0; ulk(); g_ocupado = 0; g_parado = 0; g_volcado = 0; g_capn = 0;
    g_t0 = g_t1 = timer_get_uptime_ms();
    g_estado = CRUN_MARCHA; g_visible = 1;
    if (!thread_create("programa", hilo, 0)) { g_estado = CRUN_NADA; g_visible = 0; return -2; }
    return 0;
}
int crun_poll(void)
{
    if (g_estado == CRUN_NADA) return 0;
    lk();
    int cambio = 0;
    if (g_estado == CRUN_MARCHA && g_viva && g_cap) {
        uint32_t n = console_capture_len(), ult = 0;
        for (uint32_t i = 0; i < n; i++) if (g_cap[i] == '\n') ult = i + 1;
        if (ult) { crun_buf_mete(&g_b, g_cap, ult); console_capture_consume(ult); n -= ult; cambio = 1; }
        char p[CRUN_LW]; uint32_t k = 0;                          /* lo que queda sin salto: la pregunta del programa */
        for (uint32_t i = 0; i < n && k < sizeof p - 1; i++) if (g_cap[i] != '\r' && g_cap[i] != 27) p[k++] = g_cap[i];
        p[k] = 0;
        uint32_t j = 0; while (p[j] && p[j] == g_prompt[j]) j++;
        if (p[j] || g_prompt[j]) { for (uint32_t i = 0; i <= k; i++) g_prompt[i] = p[i]; cambio = 1; }
    }
    if (g_estado == CRUN_FIN && !g_volcado) {
        g_volcado = 1;
        if (g_ocupado) { const char *m = "No se puede ejecutar ahora: el Terminal tiene otra orden en marcha."; uint32_t l = 0; while (m[l]) l++; crun_buf_mete(&g_b, m, l); }
        else if (g_cap) crun_buf_mete(&g_b, g_cap, g_capn);
        crun_buf_cierra(&g_b);
        uint32_t l = 0; while (g_out[l]) l++;
        if (l) { crun_buf_mete(&g_b, g_out, l); crun_buf_cierra(&g_b); }
        g_prompt[0] = 0;
        if (g_b.n == 0 && !g_parado) g_visible = 0;              /* no imprimio nada (un juego con su ventana): el panel se va solo */
        cambio = 1;
    }
    ulk();
    return cambio;
}
int crun_feed(const char *linea)
{
    if (g_estado != CRUN_MARCHA || !g_viva) return -1;
    lk();
    char eco[CRUN_LW * 2]; uint32_t e = 0;                       /* la pregunta y la respuesta quedan en la salida, como en un terminal */
    for (uint32_t i = 0; g_prompt[i] && e < sizeof eco - 2; i++) eco[e++] = g_prompt[i];
    for (uint32_t i = 0; linea[i] && e < sizeof eco - 2; i++) eco[e++] = linea[i];
    eco[e++] = '\n';
    crun_buf_mete(&g_b, eco, e);
    console_capture_consume(console_capture_len());
    g_prompt[0] = 0;
    int r = console_capture_feed(linea) < 0 ? -1 : 0;
    ulk();
    return r;
}
void crun_parar(void)
{
    if (g_estado != CRUN_MARCHA) return;
    g_parado = 1;
    (void)shell_python_stop();                                    /* Python y los programas de C van en un proceso de ring 3 */
    console_capture_feed_eof();                                   /* y si estaba esperando una linea, deja de esperar */
}
void crun_cerrar(void) { if (g_estado != CRUN_MARCHA) { g_visible = 0; g_estado = CRUN_NADA; } }
int crun_estado(void) { return g_estado; }
int crun_visible(void) { return g_visible; }
int crun_lineas(void) { return g_b.n; }
const char *crun_linea(int i) { return crun_buf_linea(&g_b, i); }
const char *crun_prompt(void) { return g_prompt; }
const char *crun_nombre(void) { return g_nombre; }
uint32_t crun_ms(void) { return (uint32_t)((g_estado == CRUN_MARCHA ? timer_get_uptime_ms() : g_t1) - g_t0); }
int crun_parado(void) { return g_parado; }
int crun_fallo(void)                                              /* "python: codigo de salida 1" (en la salida o en la respuesta de la orden) */
{
    if (g_estado != CRUN_FIN || g_parado) return 0;
    if (g_ocupado) return 1;
    static const char C[] = "codigo de salida ";
    for (int l = g_b.n - 1; l >= 0 && l >= g_b.n - 4; l--) {
        const char *t = crun_buf_linea(&g_b, l);
        for (uint32_t i = 0; t[i]; i++) {
            uint32_t k = 0; while (C[k] && t[i + k] == C[k]) k++;
            if (!C[k]) { const char *v = t + i + k; return !(v[0] == '0' && (v[1] < '0' || v[1] > '9')); }
        }
    }
    return 0;
}
#endif
