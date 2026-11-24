/* SOMA_295_CORREO: el cliente IMAP y SMTP de SOMA (src/apps/correo_red.c + correo_core.c) con el TLS 1.3 de SOMA (src/core/tls13.c)
 * contra los servidores de prueba de tests/correo_servidores_295.py (TLS 1.3 de OpenSSL, directo y con STARTTLS). Lo lanza
 * tests/test_correo_red_295.py, que despues comprueba en el servidor lo que ha quedado (banderas, carpetas y los correos recibidos
 * por SMTP, leidos con el analizador de correo de Python).
 * Uso: test_correo_red_295 IMAP IMAP_STARTTLS SMTP SMTP_STARTTLS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tls13.h"
#include "correo_red.h"

void *co_alloc(uint32_t n) { return malloc(n ? n : 1); }
void co_free(void *p) { free(p); }
static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); fflush(stdout); }

/* ---------------------------------------------------------------- transporte con sockets */
struct sk { int fd, on; struct tls13 *t; };
static uint64_t ahora_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u; }
static int io_send(void *c, const uint8_t *d, uint32_t n) { int fd = *(int *)c; while (n) { ssize_t k = send(fd, d, n, 0); if (k <= 0) return -1; d += k; n -= (uint32_t)k; } return 0; }
static int io_recv(void *c, uint8_t *d, uint32_t max, uint32_t to)
{
    int fd = *(int *)c; fd_set s; FD_ZERO(&s); FD_SET(fd, &s); struct timeval tv = { (time_t)(to / 1000), (suseconds_t)((to % 1000) * 1000) };
    int r = select(fd + 1, &s, 0, 0, &tv); if (r <= 0) return 0;
    ssize_t k = recv(fd, d, max, 0); if (k == 0) return TLS_IO_EOF; if (k < 0) return TLS_IO_ERR; return (int)k;
}
static int tr_tls(struct sk *k, const char *host, char *err, uint32_t ecap)
{
    k->t = (struct tls13 *)calloc(1, sizeof(struct tls13));
    struct tls_io io = { &k->fd, io_send, io_recv, ahora_ms, 0 };
    if (tls13_connect(k->t, &io, host, 8000)) { snprintf(err, ecap, "TLS rechazado: %s", tls13_err_name(k->t->err)); return -1; }
    k->on = 1; return 0;
}
static int tr_abre(void *c, const char *host, uint16_t port, int tls, char *err, uint32_t ecap)
{
    struct sk *k = (struct sk *)c; k->on = 0; k->t = 0;
    k->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(k->fd, (struct sockaddr *)&a, sizeof a)) { snprintf(err, ecap, "No se pudo conectar con %s", host); close(k->fd); k->fd = -1; return -1; }
    return tls ? tr_tls(k, host, err, ecap) : 0;
}
static int tr_starttls(void *c, const char *host, char *err, uint32_t ecap) { return tr_tls((struct sk *)c, host, err, ecap); }
static int tr_envia(void *c, const void *d, uint32_t n) { struct sk *k = (struct sk *)c; return k->on ? tls13_send(k->t, d, n) : io_send(&k->fd, (const uint8_t *)d, n); }
static int tr_recibe(void *c, char *b, uint32_t max, uint32_t ms)
{
    struct sk *k = (struct sk *)c; int r = k->on ? tls13_recv(k->t, b, max, ms) : io_recv(&k->fd, (uint8_t *)b, max, ms);
    return r == TLS_IO_EOF || r == TLS_IO_ERR ? -1 : r;
}
static int g_tls_usado;
static void tr_cierra(void *c) { struct sk *k = (struct sk *)c; if (k->fd >= 0) { if (k->on) { tls13_close(k->t); g_tls_usado++; } close(k->fd); } free(k->t); k->t = 0; k->fd = -1; k->on = 0; }

static const char CLAVE[] = "cl@ve \"con\" \303\261";
static void cuenta(struct co_cuenta *c, int imap, int imap_tls, int smtp, int smtp_tls)
{
    memset(c, 0, sizeof *c);
    strcpy(c->email, "pepe@prueba.es"); strcpy(c->nombre, "Jos\303\251 Ram\303\263n"); strcpy(c->usuario, "pepe@prueba.es"); strcpy(c->clave, CLAVE);
    strcpy(c->imap, "localhost"); c->imap_p = (uint16_t)imap; c->imap_tls = (uint8_t)imap_tls;
    strcpy(c->smtp, "localhost"); c->smtp_p = (uint16_t)smtp; c->smtp_tls = (uint8_t)smtp_tls;
}
static struct co_msg *busca_uid(struct co_msg *v, int n, uint32_t uid) { for (int i = 0; i < n; i++) if (v[i].uid == uid) return &v[i]; return 0; }

int main(int argc, char **argv)
{
    if (argc < 5) { printf("uso: test_correo_red_295 IMAP IMAP_STARTTLS SMTP SMTP_STARTTLS\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);                                    /* un servidor que cierra no tumba la prueba (macOS) */
    int P_IMAP = atoi(argv[1]), P_IMAP_ST = atoi(argv[2]), P_SMTP = atoi(argv[3]), P_SMTP_ST = atoi(argv[4]);
    tls13_verify = 0;                     /* certificado propio de la prueba (la verificacion de cadenas se prueba en test_tls) */
    struct sk k = { -1, 0, 0 };
    struct co_tr t = { &k, tr_abre, tr_starttls, tr_envia, tr_recibe, tr_cierra, ahora_ms, 0 };
    struct co_cuenta c; static struct co_imap s;
    static struct co_msg v[64]; static struct co_carpeta cp[16];

    /* ---- IMAP con TLS directo, AUTHENTICATE PLAIN, MOVE y UIDPLUS */
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO);
    int r = co_imap_abre(&s, &t, &c);
    chk(r == 0, "IMAP: entra con TLS 1.3 y AUTHENTICATE PLAIN (contrasena con comillas y enie)");
    if (r) { printf("  motivo: %s\n", s.err); return 1; }
    chk(s.move && s.uidplus && !s.gmail, "IMAP: capacidades despues de entrar");
    int nc = co_imap_carpetas(&s, cp, 16);
    int env = -1, pap = -1, ete = -1, com = -1, nosel = -1;
    for (int i = 0; i < nc; i++) {
        if (cp[i].uso == CO_U_ENVIADOS) env = i;
        if (cp[i].uso == CO_U_PAPELERA) pap = i;
        if (!strcmp(cp[i].nombre, "2026 \303\251t\303\251")) ete = i;
        if (!strcmp(cp[i].imap, "Con \"comillas\"")) com = i;
        if (cp[i].noselect) nosel = i;
    }
    chk(nc == 7 && env >= 0 && pap >= 0 && ete >= 0 && com >= 0 && nosel >= 0, "IMAP: carpetas (usos, UTF-7, comillas y no seleccionable)");
    uint32_t nl = 0, tot = 0;
    chk(co_imap_estado(&s, "INBOX", &nl, &tot) == 0 && tot == 6 && nl == 4, "IMAP: STATUS de Recibidos (6, 4 sin leer)");
    chk(co_imap_estado(&s, "Con \"comillas\"", &nl, &tot) == 0 && tot == 0, "IMAP: STATUS de un nombre con comillas");
    chk(co_imap_selecciona(&s, "INBOX", 1) == 0 && s.exists == 6 && s.uidnext == 7 && s.uidvalidity == 4242, "IMAP: SELECT");
    int n = co_imap_ultimos(&s, v, 64, 0);
    chk(n == 6 && v[0].uid == 6 && v[5].uid == 1, "IMAP: los 6, del mas nuevo al mas viejo");
    struct co_msg *m2 = busca_uid(v, n, 2), *m3 = busca_uid(v, n, 3), *m4 = busca_uid(v, n, 4), *m5 = busca_uid(v, n, 5), *m6 = busca_uid(v, n, 6);
    chk(m2 && !strcmp(m2->asunto, "Presupuesto reforma ba\303\261o") && !strcmp(m2->de, "Mar\303\255a L\303\263pez") && m2->adj && !(m2->flags & CO_F_VISTO), "IMAP: asunto y remitente codificados, adjunto");
    chk(m2 && strstr(m2->extracto, "presupuesto del ba\303\261o: 4.300 \342\202\254"), "IMAP: extracto de la parte de texto (Latin-1, QP)");
    chk(m3 && strstr(m3->extracto, "S\303\263lo hoy") && !strchr(m3->extracto, '<'), "IMAP: extracto de un correo solo HTML");
    chk(m4 && strstr(m4->extracto, "Perfecto") && !strstr(m4->extracto, "Te parece"), "IMAP: extracto sin la cita");
    chk(m5 && m5->adj && m5->tam > 300000, "IMAP: el grande, con adjunto y su tamano");
    chk(m6 && (m6->flags & CO_F_DESTACADO) && m6->fecha > 0, "IMAP: banderas y fecha");
    n = co_imap_ultimos(&s, v, 3, 0);
    chk(n == 3 && v[0].uid == 6 && v[2].uid == 4, "IMAP: solo los 3 ultimos");
    n = co_imap_ultimos(&s, v, 64, 4);
    chk(n == 2 && v[0].uid == 6 && v[1].uid == 5, "IMAP: solo los nuevos (UID > 4)");
    n = co_imap_ultimos(&s, v, 64, 6);
    chk(n == 0, "IMAP: nada nuevo (y el '*' de UID FETCH no cuela el ultimo)");
    { uint32_t u[2] = { 2, 4 }; n = co_imap_estos(&s, u, 2, v); chk(n == 2 && v[0].uid == 4 && v[1].uid == 2, "IMAP: unos UID concretos"); }
    /* el mensaje entero y el adjunto */
    static char raw[1 << 20]; int cortado = -1;
    uint32_t rn = co_imap_mensaje(&s, 2, raw, sizeof raw, &cortado);
    static struct co_mime mm; static char tx[20000];
    chk(rn > 500 && !cortado && co_mime_lee(raw, rn, &mm, tx, sizeof tx), "IMAP: el mensaje entero");
    uint8_t adj[200]; uint32_t an = mm.nadj ? co_adj_saca(raw, rn, &mm.adj[0], adj, sizeof adj) : 0;
    chk(mm.nadj == 1 && !strcmp(mm.adj[0].nombre, "presupuesto ba\303\261o.pdf") && an == 26 && !memcmp(adj, "%PDF-1.4 presupuesto SOMA\n", 26), "IMAP: el adjunto, con su nombre RFC 2231, sale entero");
    rn = co_imap_mensaje(&s, 5, raw, 65536, &cortado);
    chk(rn == 65535 && cortado == 1, "IMAP: un mensaje grande se pide solo hasta el tope");
    rn = co_imap_mensaje(&s, 99, raw, sizeof raw, &cortado);
    chk(rn == 0 && strstr(s.err, "ya no est"), "IMAP: un UID que no existe");
    /* banderas, busqueda, mover, borrar, anadir */
    chk(co_imap_banderas(&s, 2, CO_F_VISTO, 1) == 0 && co_imap_banderas(&s, 6, CO_F_DESTACADO, 0) == 0, "IMAP: STORE +Seen y -Flagged");
    { uint32_t u[16], f[16]; int k2 = co_imap_banderas_todas(&s, 1, u, f, 16), bien = k2 == 6;
      for (int i = 0; i < k2; i++) { if (u[i] == 2 && !(f[i] & CO_F_VISTO)) bien = 0; if (u[i] == 6 && (f[i] & CO_F_DESTACADO)) bien = 0; }
      chk(bien, "IMAP: las banderas cambiadas se ven"); }
    uint32_t us[16];
    n = co_imap_busca(&s, "UNSEEN", us, 16); chk(n == 3, "IMAP: SEARCH UNSEEN");
    { struct co_filtro fl; co_filtro_texto("de:maria presupuesto", 0, &fl); char crit[300]; co_filtro_imap(&fl, crit, sizeof crit);
      n = co_imap_busca(&s, crit, us, 16); chk(n == 1 && us[0] == 2, "IMAP: SEARCH con los criterios del filtro"); }
    n = co_imap_busca(&s, "SUBJECT \"no existe\"", us, 16); chk(n == 0, "IMAP: SEARCH sin resultados");
    chk(co_imap_mueve(&s, 3, "Papelera") == 0, "IMAP: MOVE a la papelera");
    chk(co_imap_borra(&s, 1) == 0, "IMAP: borrar (Deleted + UID EXPUNGE)");
    chk(co_imap_selecciona(&s, "INBOX", 1) == 0 && s.exists == 4, "IMAP: quedan 4");
    static char msg[20000];
    struct co_envio en; memset(&en, 0, sizeof en);
    en.de_nombre = c.nombre; en.de_email = c.email; en.para = "Mar\303\255a L\303\263pez <maria@ejemplo.es>";
    en.asunto = "Copia en Enviados"; en.cuerpo = "Hola,\nesto se guarda en Enviados.\n"; en.utc_min = 29000000; en.off_min = 60; en.azar = 7;
    uint32_t mn = co_compone(&en, msg, sizeof msg);
    chk(co_imap_anade(&s, "Enviados", msg, mn, CO_F_VISTO) == 0, "IMAP: APPEND a Enviados (literal)");
    chk(co_imap_estado(&s, "Enviados", &nl, &tot) == 0 && tot == 1 && nl == 0, "IMAP: Enviados tiene 1 (leido)");
    chk(co_imap_selecciona(&s, "No existe", 0) != 0 && strstr(s.err, "no existe"), "IMAP: carpeta que no existe (el motivo del servidor)");
    co_imap_cierra(&s);

    /* ---- IMAP con STARTTLS: LOGIN con literal, sin MOVE ni UIDPLUS */
    cuenta(&c, P_IMAP_ST, CO_TLS_STARTTLS, P_SMTP, CO_TLS_DIRECTO);
    r = co_imap_abre(&s, &t, &c);
    chk(r == 0 && !s.move && !s.uidplus, "IMAP STARTTLS: cifra y entra con LOGIN (contrasena en literal)");
    if (!r) {
        chk(co_imap_selecciona(&s, "INBOX", 1) == 0 && s.exists == 4, "IMAP STARTTLS: SELECT");
        chk(co_imap_mueve(&s, 4, "Papelera") == 0, "IMAP STARTTLS: mover sin MOVE (COPY + Deleted + EXPUNGE)");
        chk(co_imap_selecciona(&s, "INBOX", 1) == 0 && s.exists == 3, "IMAP STARTTLS: quedan 3");
        co_imap_cierra(&s);
    } else printf("  motivo: %s\n", s.err);
    /* ---- errores */
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO); strcpy(c.clave, "mala");
    r = co_imap_abre(&s, &t, &c);
    chk(r != 0 && strstr(s.err, "no acepta el usuario o la contrase\303\261a") && strstr(s.err, "Invalid credentials"), "IMAP: contrasena mala (con el motivo del servidor)");
    cuenta(&c, P_SMTP, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO);
    r = co_imap_abre(&s, &t, &c);
    chk(r != 0 && strstr(s.err, "no parece IMAP"), "IMAP: el puerto es de otro servicio");
    cuenta(&c, 1, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO);
    r = co_imap_abre(&s, &t, &c);
    chk(r != 0 && strstr(s.err, "No se pudo conectar"), "IMAP: nadie escucha");

    /* ---- SMTP: TLS directo con AUTH PLAIN */
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO);
    static char err[300];
    memset(&en, 0, sizeof en);
    en.de_nombre = c.nombre; en.de_email = c.email;
    en.para = "Mar\303\255a L\303\263pez <maria@ejemplo.es>, \"Garc\303\255a, Ana\" <ana@x.com>";
    en.cc = "obra@x.com"; en.cco = "oculto@x.com";
    en.asunto = "Presupuesto reforma ba\303\261o: \302\241adelante!";
    en.cuerpo = "Hola Mar\303\255a:\n\nPerfecto, adelante con el presupuesto de 4.300 \342\202\254.\n.Esta l\303\255nea empieza por punto.\n\nUn saludo,\nJos\303\251";
    en.en_respuesta_a = "<abc123@ejemplo.es>"; en.utc_min = 29000000; en.off_min = 60; en.azar = 0x295;
    mn = co_compone(&en, msg, sizeof msg);
    struct co_dir rc[8]; int nr = co_destinatarios(&en, rc, 8);
    r = co_smtp_envia(&t, &c, rc, nr, msg, mn, err, sizeof err);
    chk(r == 0 && nr == 4, "SMTP: enviado con TLS 1.3 directo y AUTH PLAIN (4 destinatarios)");
    if (r) printf("  motivo: %s\n", err);
    /* ---- SMTP: STARTTLS con AUTH LOGIN */
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP_ST, CO_TLS_STARTTLS);
    en.para = "juan@obra.es"; en.cc = 0; en.cco = 0; en.asunto = "Por el 587"; en.cuerpo = "Enviado con STARTTLS y AUTH LOGIN.\n"; en.en_respuesta_a = 0;
    mn = co_compone(&en, msg, sizeof msg); nr = co_destinatarios(&en, rc, 8);
    r = co_smtp_envia(&t, &c, rc, nr, msg, mn, err, sizeof err);
    chk(r == 0, "SMTP: enviado con STARTTLS y AUTH LOGIN");
    if (r) printf("  motivo: %s\n", err);
    /* ---- SMTP: errores */
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP, CO_TLS_DIRECTO);
    en.para = "bien@x.es, rechaza@x.es"; mn = co_compone(&en, msg, sizeof msg); nr = co_destinatarios(&en, rc, 8);
    r = co_smtp_envia(&t, &c, rc, nr, msg, mn, err, sizeof err);
    chk(r != 0 && strstr(err, "rechaza@x.es") && strstr(err, "Usuario desconocido"), "SMTP: destinatario rechazado (dice cual y por que)");
    strcpy(c.clave, "mala");
    r = co_smtp_prueba(&t, &c, err, sizeof err);
    chk(r != 0 && strstr(err, "no acepta el usuario o la contrase\303\261a") && strstr(err, "not accepted"), "SMTP: contrasena mala");
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_SMTP_ST, CO_TLS_STARTTLS);
    chk(co_smtp_prueba(&t, &c, err, sizeof err) == 0, "SMTP: la prueba de la cuenta (STARTTLS)");
    cuenta(&c, P_IMAP, CO_TLS_DIRECTO, P_IMAP, CO_TLS_DIRECTO);
    r = co_smtp_prueba(&t, &c, err, sizeof err);
    chk(r != 0, "SMTP: el puerto es de otro servicio");
    chk(g_tls_usado >= 8, "todas las conexiones con TLS se cerraron con close_notify");
    printf("== test_correo_red_295 (cliente): %d/%d\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
