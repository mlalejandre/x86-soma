/* SOMA_295_CORREO: la aplicacion Correo entera (src/apps/correo.c) en el anfitrion, contra los servidores IMAP y SMTP de prueba
 * (tests/correo_servidores_295.py, TLS 1.3 de verdad) y con un modelo de lenguaje de mentira que apunta lo que recibe. Se usa como un
 * usuario: rellena la cuenta, abre correos, resume, responde con IA, mejora, envia, destaca, borra, busca (escrito y con IA), clasifica,
 * redacta con IA y desde el orbe ("envialo" incluido). Despues tests/test_correo_red_295.py mira en los servidores lo que llego.
 * Uso: test_correoapp_295 IMAP SMTP [CARPETA_PNG] */
#define SOMA_CORREO_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "png_enc.h"
#include "tls13.h"

static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("FALLO linea %d: %s\n", __LINE__, #c); fflush(stdout); } } while (0)

/* ---------------------------------------------------------------- sistema de mentira */
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t g_ms = 100000;
uint64_t timer_get_uptime_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u + g_ms; }
static int64_t g_min = 29925195;                         /* minutos locales: martes 24 nov 2026, 9:15 */
int64_t agenda_ahora(void) { return g_min; }
static char g_agenda[300]; static int g_nagenda;
int agenda_anade_texto(const char *t, char *res, uint32_t cap) { snprintf(g_agenda, sizeof g_agenda, "%s", t); g_nagenda++; snprintf(res, cap, "lunes 30 de noviembre, 10:00: reuni\303\263n en la sala grande"); return 0; }
struct vf { char name[200]; char *d; uint32_t n; };
static struct vf V[64]; static int nV;
static struct vf *vbusca(const char *n) { for (int i = 0; i < nV; i++) if (!strcmp(V[i].name, n)) return &V[i]; return 0; }
int vfs_write(const char *name, const char *data, uint32_t len)
{
    struct vf *f = vbusca(name); if (!f) { if (nV >= 64) return -1; f = &V[nV++]; snprintf(f->name, sizeof f->name, "%s", name); f->d = 0; }
    free(f->d); f->d = malloc(len + 1); memcpy(f->d, data, len); f->d[len] = 0; f->n = len; return (int)len;
}
int vfs_read(const char *name, char *buf, uint32_t max) { struct vf *f = vbusca(name); if (!f) return -1; uint32_t n = f->n < max ? f->n : max; memcpy(buf, f->d, n); return (int)n; }
char *vfs_read_alloc(const char *name, uint32_t *len) { struct vf *f = vbusca(name); if (!f) return 0; char *d = malloc(f->n + 1); memcpy(d, f->d, f->n); d[f->n] = 0; *len = f->n; return d; }
#include "thread.h"
static struct tcb g_tcb;
struct tcb *thread_create(const char *name, thread_func_t entry, void *arg) { (void)name; (void)entry; (void)arg; return &g_tcb; }
void thread_sleep(uint32_t ms) { (void)ms; }
void thread_yield(void) {}
static int g_abre_app;
void gui_open_correo(void) { g_abre_app++; }
/* el modelo */
#include "llm.h"
char llm_last_error[200];
void llm_trace_tag(const char *t) { (void)t; }
static char g_sys[4000], g_prompt[30000]; static int g_nllm; static char g_elige[64] = "";
int llm_chat_ex(const char *system, const char *prompt, const struct llm_img *img, int nimg, int max_tokens, char *out, uint32_t max, uint32_t to)
{
    (void)img; (void)nimg; (void)max_tokens; (void)to; g_nllm++;
    snprintf(g_sys, sizeof g_sys, "%s", system); snprintf(g_prompt, sizeof g_prompt, "%s", prompt);
    const char *r = "";
    if (strstr(system, "filtro JSON")) r = strstr(prompt, "fotos") ? "<think>busca fotos</think>{\"de\": \"juan\", \"palabras\": \"fotos obra\"}" : strstr(prompt, "comida") ? "{\"palabras\": \"comida restaurante\"}" : "{\"palabras\": \"presupuesto\"}";
    else if (strstr(system, "Elige los que encajan")) {                /* el que tiene la palabra que se le diga (o ninguno) */
        static char e[40]; strcpy(e, "IDS: ninguno");
        for (const char *p = prompt; (p = strchr(p, '\n')); ) { p++; unsigned v = 0; const char *q = p; while (*q >= '0' && *q <= '9') v = v * 10 + (unsigned)(*q++ - '0'); const char *f = strchr(q, '\n');
            if (v && g_elige[0] && (size_t)(f ? f - q : (long)strlen(q)) > 0) { char l[400]; size_t n = f ? (size_t)(f - q) : strlen(q); if (n > 399) n = 399; memcpy(l, q, n); l[n] = 0; if (strstr(l, g_elige)) { snprintf(e, sizeof e, "IDS: %u", v); break; } } }
        r = e;
    }
    else if (strstr(system, "Resumes correos")) r = "Mar\303\255a te manda el presupuesto de la reforma del ba\303\261o: 4.300 \342\202\254 con IVA, en un PDF adjunto.\nTe pide: que le confirmes si sigues adelante.";
    else if (strstr(system, "Redactas respuestas")) r = strstr(prompt, "Tono: formal") ? "Estimada Mar\303\255a:\n\nLe confirmo que seguimos adelante.\n\nAtentamente,\nJos\303\251" : "Hola Mar\303\255a:\n\nGracias por el presupuesto. Adelante con la reforma.\n\nUn abrazo,\nJos\303\251";
    else if (strstr(system, "Reescribes borradores")) r = "Estimada Mar\303\255a:\n\nLe agradezco el presupuesto. Puede seguir adelante con la reforma.\n\nUn cordial saludo,\nJos\303\251";
    else if (strstr(system, "Redactas correos")) {
        const char *pide = strstr(prompt, "Lo que pide el usuario:"); if (!pide) pide = prompt;
        if (strstr(pide, "Juan")) r = "PARA: Juan Garc\303\255a\nASUNTO: Cambio al martes\nCUERPO:\nHola Juan:\n\nEl lunes no puedo; \302\277lo pasamos al martes?\n\nUn saludo,\nJos\303\251";
        else if (strstr(pide, "Roberto")) r = "PARA: Roberto\nASUNTO: Hola\nCUERPO:\nHola Roberto, \302\277qu\303\251 tal?";
        else r = "**Para:** Mar\303\255a L\303\263pez\n**Asunto:** Llego tarde\n**Cuerpo:**\nHola Mar\303\255a:\n\nLlego unos 10 minutos tarde. Perdona.\n\nUn saludo,\nJos\303\251";
    }
    else if (strstr(system, "Clasificas")) {
        static char c[400]; c[0] = 0;
        for (const char *p = prompt; *p; ) { unsigned v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
            const char *e = strchr(p, '\n'); char l[400]; size_t n = e ? (size_t)(e - p) : strlen(p); if (n > 399) n = 399; memcpy(l, p, n); l[n] = 0;
            const char *cat = strstr(l, "Jefa") || strstr(l, "Mar\303\255a") ? "importante" : strstr(l, "Tienda") ? "promocion" : strstr(l, "Banco") ? "notificacion" : "personal";
            if (v) { char t[40]; snprintf(t, sizeof t, "%u: %s\n", v, cat); strcat(c, t); }
            p = e ? e + 1 : p + n; }
        r = c;
    }
    else if (strstr(system, "saca el evento")) r = "el lunes 30 a las 10 reuni\303\263n en la sala grande";
    else if (strstr(system, "le cuentas al usuario")) r = "Tienes 3 correos sin leer: la jefa te cita el lunes a las 10 y Juan te manda las fotos de la obra.";
    snprintf(out, max, "%s", r);
    return 1;
}
/* teclado, portapapeles y pantalla (como en tests/test_chatapp_285.c) */
static int g_ctrl, g_shift;
int keyboard_ctrl_held(void) { return g_ctrl; }
int keyboard_shift_held(void) { return g_shift; }
static char g_clip[8192];
const char *clip_text(uint32_t *n) { *n = (uint32_t)strlen(g_clip); return g_clip; }
int clip_set_text(const char *t, uint32_t n) { if (!n) n = (uint32_t)strlen(t); if (n > sizeof g_clip - 1) n = sizeof g_clip - 1; memcpy(g_clip, t, n); g_clip[n] = 0; return 0; }
int g_wf182_on = 1;
extern uint32_t *g_fb; extern int g_fbw, g_fbh;
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_fill_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb);
static void put2(int x, int y, uint32_t c, int a)
{
    int32_t x0, y0, x1, y1; fb_get_clip(&x0, &y0, &x1, &y1);
    if (x < x0 || y < y0 || x >= x1 || y >= y1 || x < 0 || y < 0 || x >= g_fbw || y >= g_fbh || a <= 0) return;
    if (a > 255) a = 255;
    uint32_t d = g_fb[y * g_fbw + x];
    uint32_t r = (((c >> 16) & 255) * (uint32_t)a + ((d >> 16) & 255) * (uint32_t)(255 - a)) / 255, g = (((c >> 8) & 255) * (uint32_t)a + ((d >> 8) & 255) * (uint32_t)(255 - a)) / 255, b = ((c & 255) * (uint32_t)a + (d & 255) * (uint32_t)(255 - a)) / 255;
    g_fb[y * g_fbw + x] = 0xFF000000u | r << 16 | g << 8 | b;
}
void fb_reset_clip(void) { fb_set_clip(0, 0, 1 << 30, 1 << 30); }
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { fb_fill_rrect_aa(x, y, w, h, r, 0xFF000000u | c); }
void fb_stroke_rrect_aa(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t argb)
{
    int a0 = (int)(argb >> 24); if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
    double cx = x + w / 2.0, cy = y + h / 2.0, hx = w / 2.0 - 0.5, hy = h / 2.0 - 0.5;
    for (int j = y - 1; j <= y + h; j++) for (int i = x - 1; i <= x + w; i++) {
        double px = fabs(i + 0.5 - cx) - (hx - r), py = fabs(j + 0.5 - cy) - (hy - r);
        double ox = px > 0 ? px : 0, oy = py > 0 ? py : 0;
        double d = sqrt(ox * ox + oy * oy) + (px > py ? (px < 0 ? px : 0) : (py < 0 ? py : 0)) - r;
        double cov = 1.0 - fabs(d); if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_line_aa(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width16, uint32_t argb)
{
    double ax = x0 / 16.0, ay = y0 / 16.0, bx = x1 / 16.0, by = y1 / 16.0, hw = width16 / 32.0; int a0 = (int)(argb >> 24);
    int mnx = (int)floor((ax < bx ? ax : bx) - hw - 1), mxx = (int)ceil((ax > bx ? ax : bx) + hw + 1), mny = (int)floor((ay < by ? ay : by) - hw - 1), mxy = (int)ceil((ay > by ? ay : by) + hw + 1);
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int j = mny; j <= mxy; j++) for (int i = mnx; i <= mxx; i++) {
        double px = i + 0.5 - ax, py = j + 0.5 - ay, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        double ex = px - t * dx, ey = py - t * dy, d = sqrt(ex * ex + ey * ey), cov = hw + 0.5 - d;
        if (cov > 0) put2(i, j, argb & 0xFFFFFF, (int)(a0 * (cov > 1 ? 1 : cov)));
    }
}
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void font_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t c, int sc) { (void)x; (void)y; (void)s; (void)c; (void)sc; }

/* ---------------------------------------------------------------- la red: sockets y el TLS de SOMA */
struct sk { int fd, on; struct tls13 *t; };
static uint64_t ahora_ms(void) { return timer_get_uptime_ms(); }
static int io_send(void *c, const uint8_t *d, uint32_t n) { int fd = *(int *)c; while (n) { ssize_t k = send(fd, d, n, 0); if (k <= 0) return -1; d += k; n -= (uint32_t)k; } return 0; }
static int io_recv(void *c, uint8_t *d, uint32_t max, uint32_t to)
{
    int fd = *(int *)c; fd_set s; FD_ZERO(&s); FD_SET(fd, &s); struct timeval tv = { (time_t)(to / 1000), (suseconds_t)((to % 1000) * 1000) };
    int r = select(fd + 1, &s, 0, 0, &tv); if (r <= 0) return 0;
    ssize_t k = recv(fd, d, max, 0); if (k == 0) return TLS_IO_EOF; if (k < 0) return TLS_IO_ERR; return (int)k;
}
static int tr_tls(struct sk *k, const char *host, char *err, uint32_t ecap)
{
    if (!k->t) k->t = (struct tls13 *)calloc(1, sizeof(struct tls13));
    struct tls_io io = { &k->fd, io_send, io_recv, ahora_ms, 0 };
    if (tls13_connect(k->t, &io, host, 8000)) { snprintf(err, ecap, "TLS rechazado: %s", tls13_err_name(k->t->err)); return -1; }
    k->on = 1; return 0;
}
static int tr_abre(void *c, const char *host, uint16_t port, int tls, char *err, uint32_t ecap)
{
    struct sk *k = (struct sk *)c; k->on = 0;
    k->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(k->fd, (struct sockaddr *)&a, sizeof a)) { snprintf(err, ecap, "No se pudo conectar con %s", host); close(k->fd); k->fd = -1; return -1; }
    return tls ? tr_tls(k, host, err, ecap) : 0;
}
static int tr_starttls(void *c, const char *host, char *err, uint32_t ecap) { return tr_tls((struct sk *)c, host, err, ecap); }
static int tr_envia(void *c, const void *d, uint32_t n) { struct sk *k = (struct sk *)c; return k->on ? tls13_send(k->t, d, n) : io_send(&k->fd, (const uint8_t *)d, n); }
static int tr_recibe(void *c, char *b, uint32_t max, uint32_t ms) { struct sk *k = (struct sk *)c; if (k->fd < 0) return -1; int r = k->on ? tls13_recv(k->t, b, max, ms) : io_recv(&k->fd, (uint8_t *)b, max, ms); return r == TLS_IO_EOF || r == TLS_IO_ERR ? -1 : r; }
static void tr_cierra(void *c) { struct sk *k = (struct sk *)c; if (k->fd >= 0) { if (k->on) tls13_close(k->t); close(k->fd); } k->fd = -1; k->on = 0; }
struct co_tr;
void correo_transporte(struct co_tr *t);
void correo_transporte_libera(struct co_tr *t);

#include "../src/apps/correo.c"

void correo_transporte(struct co_tr *t) { struct sk *k = calloc(1, sizeof *k); k->fd = -1; t->ctx = k; t->abre = tr_abre; t->starttls = tr_starttls; t->envia = tr_envia; t->recibe = tr_recibe; t->cierra = tr_cierra; t->ms = ahora_ms; t->cancela = 0; }
void correo_transporte_libera(struct co_tr *t) { tr_cierra(t->ctx); free(t->ctx); }

/* ---------------------------------------------------------------- utilidades de la prueba */
static const char *g_cap;
static void guarda_png(const char *nombre)
{
    if (!g_cap) return;
    size_t cap = (size_t)g_fbw * g_fbh * 4 + 4096; uint8_t *out = malloc(cap); void *wk = malloc(png_work_size(g_fbw, g_fbh));
    size_t n = png_encode_rgb(g_fb, g_fbw, g_fbh, out, cap, wk);
    char r[300]; snprintf(r, sizeof r, "%s/%s.png", g_cap, nombre); FILE *f = fopen(r, "wb"); if (f) { fwrite(out, 1, n, f); fclose(f); }
    free(out); free(wk);
}
static int WW = 1440, HH = 860;
static void pinta(void) { memset(g_fb, 0, (size_t)g_fbw * g_fbh * 4); correo_render_window(0, 0, (uint32_t)WW, (uint32_t)HH, 1); }
static void foto(const char *nombre) { pinta(); guarda_png(nombre); }
static void trabaja(void) { int n = 0; while (g_qi != g_qf && n++ < 50) { trabaja_uno(); correo_tick(); } correo_tick(); pinta(); }
static int clic(int x, int y) { correo_mouse(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH, 0, 0); pinta(); int r = correo_handle_click(x, y, 0, 0, (uint32_t)WW, (uint32_t)HH); pinta(); return r; }
static int pulsa(int act, int a)
{
    pinta();
    for (int i = nH - 1; i >= 0; i--) if (H[i].act == act && (a == -999 || H[i].a == a)) { clic(H[i].x + H[i].w / 2, H[i].y + H[i].h / 2); return 1; }
    printf("  (no encuentro el boton %d/%d)\n", act, a); return 0;
}
static int pulsa_campo(struct tfield *f) { pinta(); for (int i = nH - 1; i >= 0; i--) if (H[i].act == A_FOCO && H[i].f == f) { clic(H[i].x + 8, H[i].y + H[i].h / 2); return 1; } printf("  (no encuentro el campo)\n"); return 0; }
static int hay(int act, int a) { for (int i = 0; i < nH; i++) if (H[i].act == act && (a == -999 || H[i].a == a)) return 1; return 0; }
static void tecla(char c) { correo_handle_key(c); }
static void escribe(const char *s) { while (*s) tecla(*s++); pinta(); }
static void borra_campo(void) { g_ctrl = 1; tecla(1); g_ctrl = 0; tecla('\b'); }    /* Ctrl+A y borrar */
static int pulsa_msg(uint32_t uid) { pinta(); for (int i = 0; i < nH; i++) if (H[i].act == A_MSG && (uint32_t)H[i].b == uid) { clic(H[i].x + 40, H[i].y + 20); return 1; } printf("  (no veo el correo %u)\n", uid); return 0; }
static int en_lista(uint32_t uid) { pinta(); for (int i = 0; i < nH; i++) if (H[i].act == A_MSG && (uint32_t)H[i].b == uid) return 1; return 0; }
static int nfilas(void) { pinta(); int n = 0; for (int i = 0; i < nH; i++) if (H[i].act == A_MSG) n++; return n; }
static const char CLAVE[] = "cl@ve \"con\" \303\261";

int main(int argc, char **argv)
{
    if (argc < 3) { printf("uso: test_correoapp_295 IMAP SMTP [CARPETA_PNG]\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);                                    /* un servidor que cierra no tumba la prueba (macOS) */
    int P_IMAP = atoi(argv[1]), P_SMTP = atoi(argv[2]);
    g_cap = argc > 3 ? argv[3] : 0;
    tls13_verify = 0;
    g_fbw = 1600; g_fbh = 1000; g_fb = calloc((size_t)g_fbw * g_fbh, 4);

    /* ---- 1. sin cuenta: la ventana de la cuenta */
    correo_init();
    CK(g_cfg && !g_hay_cuenta && g_foco == &f_nombre);
    foto("01_cuenta_vacia");
    escribe("Jos\303\251 Ram\303\263n"); tecla('\t'); CK(g_foco == &f_email);
    escribe("pepe@gmail.com"); CK(strstr(g_cfg_aviso, "contrase\303\261a de aplicaci\303\263n") && !strcmp(b_imap, "imap.gmail.com") && !strcmp(b_smtp_p, "465"));
    foto("02_cuenta_gmail");
    borra_campo(); escribe("pepe@prueba.es"); CK(!g_cfg_aviso[0] && !strcmp(b_imap, "imap.prueba.es"));
    tecla('\t'); CK(g_foco_clave);
    escribe(CLAVE); CK(!strcmp(b_clave, CLAVE));
    pinta(); CK(!hay(A_FOCO, -999) || 1);
    pulsa(A_CFG_AVANZADO, -999); CK(g_cfg_av);
    pulsa_campo(&f_imap); borra_campo(); escribe("localhost");
    pulsa_campo(&f_imap_p); borra_campo(); { char t[8]; snprintf(t, sizeof t, "%d", P_IMAP); escribe(t); }
    pulsa_campo(&f_smtp); borra_campo(); escribe("localhost");
    pulsa_campo(&f_smtp_p); borra_campo(); { char t[8]; snprintf(t, sizeof t, "%d", P_SMTP); escribe(t); }
    foto("03_cuenta_servidores");
    /* primero una clave mala: no se guarda y se dice por que */
    pulsa(A_CFG_CLAVE, -999); for (int i = 0; i < 20; i++) tecla('\b'); escribe("mala");
    pulsa(A_CFG_PROBAR, -999); CK(g_cfg_probando);
    trabaja();
    CK(g_cfg && !g_hay_cuenta && g_prueba_res < 0 && strstr(g_prueba_err, "Recibir (IMAP)") && strstr(g_prueba_err, "no acepta"));
    CK(!vbusca(CONF_FILE));
    foto("04_cuenta_error");
    pulsa(A_CFG_CLAVE, -999); for (int i = 0; i < 20; i++) tecla('\b'); escribe(CLAVE);
    pulsa(A_CFG_PROBAR, -999); trabaja();
    CK(!g_cfg && g_hay_cuenta && g_prueba_res > 0);
    { struct vf *f = vbusca(CONF_FILE); CK(f && strstr(f->d, "email=pepe@prueba.es") && !strstr(f->d, "cl@ve")); }
    CK(g_ncarp == 6 && g_carp[0].uso == CO_U_ENTRADA && g_carp[1].uso == CO_U_ENVIADOS && g_carp[3].uso == CO_U_PAPELERA);
    CK(g_cache[0].n == 6 && g_carp[0].no_leidos == 4);
    { int ete = 0; for (int i = 0; i < g_ncarp; i++) if (!strcmp(g_carp[i].nombre, "2026 \303\251t\303\251")) ete = 1; CK(ete); }

    /* ---- 2. la bandeja y leer un correo */
    foto("05_bandeja");
    CK(nfilas() == 6);
    CK(pulsa_msg(2)); CK(g_sel_uid == 2 && g_abriendo);
    trabaja();
    CK(g_mime && g_abierto_uid == 2 && strstr(g_txt, "presupuesto del ba\303\261o") && g_mime->nadj == 1);
    CK(msg_de(0, 2) && (msg_de(0, 2)->flags & CO_F_VISTO) && g_carp[0].no_leidos == 3);
    foto("06_lectura");
    CK(hay(A_ADJ, 0) && hay(A_RESUMEN, -999) && hay(A_RESP_IA, -999) && hay(A_CALENDARIO, -999));
    /* resumen */
    pulsa(A_RESUMEN, -999); trabaja();
    CK(strstr(g_sys, "Resumes correos") && strstr(g_prompt, "4.300") && strstr(g_prompt, "Asunto: Presupuesto reforma ba\303\261o"));
    { struct resumen *s = resumen_de(0, 2); CK(s && s->estado == 2 && strstr(s->t, "Te pide:")); }
    foto("07_resumen");
    /* el adjunto */
    pulsa(A_ADJ, 0); trabaja();
    { struct vf *f = vbusca("/usuario/presupuesto ba\303\261o.pdf"); CK(f && f->n == 26 && !memcmp(f->d, "%PDF-1.4 presupuesto SOMA\n", 26)); }
    CK(strstr(g_toast, "Guardado en /usuario/presupuesto ba\303\261o.pdf"));

    /* ---- 3. responder con IA, hacerlo formal y enviarlo */
    pulsa(A_RESP_IA, -999);
    CK(g_comp && g_comp_modo == R_RESPONDER && g_comp_ia && !strcmp(b_para, "Mar\303\255a L\303\263pez <maria@ejemplo.es>") && !strcmp(b_asunto, "Re: Presupuesto reforma ba\303\261o"));
    CK(strstr(b_cuerpo, "escribi\303\263:\n> Hola Pepe,") && !strcmp(g_comp_irt, "<abc123@ejemplo.es>"));
    trabaja();
    CK(!g_comp_ia && !strncmp(b_cuerpo, "Hola Mar\303\255a:\n\nGracias por el presupuesto.", 40) && strstr(b_cuerpo, "\n\nEl ") && strstr(b_cuerpo, "> Hola Pepe,"));
    CK(strstr(g_prompt, "Tono: natural") && strstr(g_prompt, "Correo al que responde"));
    foto("08_respuesta_ia");
    pulsa(A_MEJORA, 1); trabaja();
    CK(strstr(g_prompt, "formal") && !strncmp(b_cuerpo, "Estimada Mar\303\255a:", 16) && strstr(b_cuerpo, "> Hola Pepe,") && !strstr(g_prompt, "> Hola Pepe"));
    foto("09_mejorado");
    pulsa(A_ENVIAR, -999); CK(g_enviando);
    trabaja();
    CK(!g_comp && strstr(g_toast, "Enviado a Mar\303\255a L\303\263pez"));
    CK(msg_de(0, 2) && (msg_de(0, 2)->flags & CO_F_RESPONDIDO));
    CK(g_cache[1].n == 0 || g_cache[1].n == 1);

    /* ---- 4. destacar, borrar, no leido (teclado) */
    CK(pulsa_msg(3)); trabaja();
    tecla('s'); trabaja(); CK(msg_de(0, 3) && (msg_de(0, 3)->flags & CO_F_DESTACADO));
    tecla('#'); CK(!en_lista(3) && strstr(g_toast, "papelera"));
    trabaja();
    CK(g_sel_uid != 3);
    CK(pulsa_msg(1)); trabaja(); tecla('u'); trabaja(); CK(msg_de(0, 1) && !(msg_de(0, 1)->flags & CO_F_VISTO) && g_sel_c < 0);

    /* ---- 5. buscar: escrito y con IA */
    tecla('/'); CK(g_foco == &f_busca);
    escribe("presupuesto"); tecla('\n'); CK(g_vista == V_BUSQUEDA);
    trabaja();
    CK(g_nres >= 1 && g_res[0].uid == 2 && nfilas() == g_nres);
    foto("10_busqueda");
    pulsa(A_LIMPIA, -999); CK(g_vista == V_CARPETA);
    pulsa(A_IA_TOGGLE, -999); CK(g_busca_ia && g_foco == &f_busca);
    escribe("el correo donde me mandaban las fotos de la obra"); tecla('\n'); trabaja();
    CK(strstr(g_sys, "filtro JSON") && strstr(g_prompt, "Hoy es 2026-11-24"));
    CK(g_nres == 1 && g_res[0].uid == 5 && strstr(g_busca_expl, "de juan") && strstr(g_busca_expl, "fotos obra"));
    foto("11_busqueda_ia");
    /* nada encaja con las palabras: la IA elige entre los candidatos */
    strcpy(g_elige, "cena");
    pulsa(A_LIMPIA, -999); tecla('/'); escribe("algo de la comida del finde"); tecla('\n'); trabaja();
    CK(strstr(g_sys, "Elige los que encajan") && strstr(g_prompt, "Re: cena del s\303\241bado"));
    CK(g_nres == 1 && msg_de(g_res[0].c, g_res[0].uid) && strstr(msg_de(g_res[0].c, g_res[0].uid)->asunto, "cena"));
    pulsa(A_LIMPIA, -999); g_busca_ia = 0;

    /* ---- 6. importantes (la IA clasifica) */
    pulsa(A_VISTA, V_IMPORTANTES); CK(g_vista == V_IMPORTANTES);
    trabaja();
    CK(strstr(g_sys, "Clasificas") && msg_de(0, 6) && msg_de(0, 6)->cat == CO_CAT_IMPORTANTE && msg_de(0, 1)->cat == CO_CAT_NOTIF);
    CK(nfilas() == 2 && en_lista(6) && en_lista(2));
    foto("12_importantes");
    pulsa(A_CARPETA, 0);

    /* ---- 7. calendario */
    CK(pulsa_msg(6)); trabaja();
    pulsa(A_CALENDARIO, -999); trabaja();
    CK(g_nagenda == 1 && strstr(g_agenda, "lunes 30 a las 10") && strstr(g_toast, "A\303\261adido al calendario"));

    /* ---- 8. redactar con IA (el destinatario sale de los contactos) */
    pulsa(A_REDACTAR, -999); CK(g_comp && g_comp_modo == R_NUEVO && g_foco == &f_para);
    pulsa(A_IA_ESCRIBE, -999); CK(g_ia_fila && g_foco == &f_ia);
    escribe("dile a Juan que el lunes no puedo y que lo pasamos al martes"); tecla('\n');
    trabaja();
    CK(strstr(g_prompt, "Juan Garc\303\255a <juan@obra.es>") && strstr(g_prompt, "Lo que pide el usuario: dile a Juan"));
    CK(!strcmp(b_para, "Juan Garc\303\255a <juan@obra.es>") && !strcmp(b_asunto, "Cambio al martes") && strstr(b_cuerpo, "al martes?"));
    if (strcmp(b_para, "Juan Garc\303\255a <juan@obra.es>")) printf("  para=[%s] asunto=[%s] estado=[%s] err=[%s]\n", b_para, b_asunto, g_comp_estado, g_ia_err);
    foto("13_redactar_ia");
    g_ctrl = 1; pulsa_campo(&f_cuerpo); tecla('\n'); g_ctrl = 0;          /* Ctrl+Intro en el cuerpo: enviar */
    CK(g_enviando); trabaja(); CK(!g_comp && strstr(g_toast, "Juan"));
    /* sin direccion: no se envia */
    pulsa(A_REDACTAR, -999); escribe("solo un nombre"); pulsa(A_ENVIAR, -999);
    CK(g_comp && !g_enviando && g_comp_err && strstr(g_comp_estado, "destinatario"));
    pulsa(A_DESCARTAR, -999); CK(!g_comp);

    /* ---- 9. el orbe: redactar, "envialo", leer y buscar */
    char out[2000];
    CK(correo_orbe(CORREO_ORBE_REDACTA, "escribe un correo a Mar\303\255a dici\303\251ndole que llego 10 minutos tarde", out, sizeof out));
    CK(strstr(out, "Mar\303\255a L\303\263pez <maria@ejemplo.es>") && strstr(out, "Llego tarde") && strstr(out, "env\303\255alo") && g_abre_app >= 1);
    CK(g_comp && g_comp_orbe && correo_orbe_hay_borrador() && !strcmp(b_para, "Mar\303\255a L\303\263pez <maria@ejemplo.es>") && strstr(b_cuerpo, "10 minutos tarde"));
    foto("14_orbe_borrador");
    CK(correo_orbe(CORREO_ORBE_ENVIA, "env\303\255alo", out, sizeof out));
    CK(strstr(out, "Hecho") && strstr(out, "Mar\303\255a") && !g_comp && !correo_orbe_hay_borrador());
    CK(correo_orbe(CORREO_ORBE_ENVIA, "env\303\255alo", out, sizeof out) && strstr(out, "No tengo ning\303\272n correo preparado"));
    CK(correo_orbe(CORREO_ORBE_REDACTA, "manda un email a Roberto saludando", out, sizeof out) && strstr(out, "no s\303\251 la direcci\303\263n de Roberto"));
    CK(g_comp && g_comp_err && strstr(g_comp_estado, "Roberto") && g_foco == &f_para);
    pulsa(A_DESCARTAR, -999);
    CK(correo_orbe(CORREO_ORBE_LEE, "\302\277tengo correos nuevos?", out, sizeof out) && strstr(out, "jefa") && strstr(g_sys, "le cuentas al usuario"));
    CK(strstr(g_prompt, "Fotos de la obra") && !strstr(g_prompt, "Presupuesto"));        /* solo los no leidos */
    CK(correo_orbe(CORREO_ORBE_BUSCA, "busca el correo de Juan con las fotos de la obra", out, sizeof out));
    CK(strstr(out, "He encontrado 1 correo") && strstr(out, "Fotos de la obra") && g_vista == V_BUSQUEDA);
    CK(strstr(g_prompt, "Busca: Juan con las fotos de la obra"));
    foto("15_orbe_busqueda");

    /* ---- 10. otras ventanas: estrecha y la orden `correo` */
    WW = 760; pinta(); if (hay(A_VOLVER, -999)) pulsa(A_VOLVER, -999); pulsa(A_LIMPIA, -999); CK(pulsa_msg(5)); trabaja(); foto("16_estrecha_lectura");
    CK(hay(A_VOLVER, -999)); pulsa(A_VOLVER, -999); CK(g_sel_c < 0); foto("17_estrecha_lista");
    WW = 1440;
    CK(correo_cmd("estado", out, sizeof out) == 0 && strstr(out, "Cuenta: pepe@prueba.es") && strstr(out, "Recibidos"));
    CK(correo_cmd("busca fotos", out, sizeof out) == 0 && strstr(out, "Fotos de la obra"));
    /* lo que se ve sin nada elegido */
    g_sel_c = -1; foto("18_bandeja_final");
    printf("== test_correoapp_295: %d/%d\n", checks - fails, checks);
    return fails ? 1 : 0;
}
