/* correo_red.h: SOMA_295_CORREO: las sesiones IMAP y SMTP sobre un transporte cualquiera (TCP o TLS 1.3).
 * No sabe nada de la pila de red: el transporte lo pone quien llama (src/apps/correo_tcp.c en SOMA; sockets en la prueba del
 * anfitrion, tests/test_correo_red_295.c, contra servidores de prueba con TLS 1.3 de verdad). Bloqueante: se usa desde un hilo. */
#ifndef SOMA_CORREO_RED_H
#define SOMA_CORREO_RED_H
#include <stdint.h>
#include "correo_core.h"

struct co_tr {
    void *ctx;
    int  (*abre)(void *ctx, const char *host, uint16_t port, int tls, char *err, uint32_t ecap);    /* 0 bien; tls: 1 directo */
    int  (*starttls)(void *ctx, const char *host, char *err, uint32_t ecap);                         /* 0 bien */
    int  (*envia)(void *ctx, const void *d, uint32_t n);                                             /* 0 bien */
    int  (*recibe)(void *ctx, char *b, uint32_t max, uint32_t ms);                                   /* >0 bytes, 0 nada aun, <0 cerrado o error */
    void (*cierra)(void *ctx);
    uint64_t (*ms)(void);
    const volatile int *cancela;                                                                    /* pasa a 1: se corta lo que este en marcha */
};

#define CO_ESPERA_MS 45000u           /* lo mas que se espera una respuesta del servidor */
struct co_imap {
    struct co_tr *t;
    char *rb; uint32_t rn, rcap;     /* lo recibido que aun no se ha usado */
    uint32_t tag;
    char caps[600];
    int move, uidplus, gmail, abierta;
    char sel[160]; int sel_rw;
    uint32_t uidvalidity, uidnext, exists;
    char err[240];
};
/* conecta (TLS directo o STARTTLS), lee las capacidades y entra (AUTHENTICATE PLAIN o LOGIN). 0 = dentro */
int  co_imap_abre(struct co_imap *s, struct co_tr *t, const struct co_cuenta *c);
void co_imap_cierra(struct co_imap *s);              /* LOGOUT y cierra */
int  co_imap_carpetas(struct co_imap *s, struct co_carpeta *out, int max);           /* cuantas (-1 error) */
int  co_imap_estado(struct co_imap *s, const char *imap, uint32_t *no_leidos, uint32_t *total);   /* STATUS; 0 bien */
int  co_imap_selecciona(struct co_imap *s, const char *imap, int escribir);         /* 0 bien (uidvalidity, uidnext, exists) */
/* los "max" ultimos (por orden de llegada) con UID > desde_uid, mas nuevos primero, con su extracto. Cuantos (-1 error) */
int  co_imap_ultimos(struct co_imap *s, struct co_msg *out, int max, uint32_t desde_uid);
int  co_imap_estos(struct co_imap *s, const uint32_t *uids, int n, struct co_msg *out);   /* lo mismo para unos UID dados */
int  co_imap_banderas_todas(struct co_imap *s, uint32_t desde_uid, uint32_t *uids, uint32_t *flags, int max);   /* UID y banderas de UID >= desde_uid */
int  co_imap_busca(struct co_imap *s, const char *criterios, uint32_t *uids, int max);   /* UID SEARCH; cuantos (-1 error) */
/* el mensaje entero (o los primeros cap - 1 bytes: *cortado = 1). Devuelve los bytes (0 = error) */
uint32_t co_imap_mensaje(struct co_imap *s, uint32_t uid, char *out, uint32_t cap, int *cortado);
int  co_imap_banderas(struct co_imap *s, uint32_t uid, uint32_t flags, int poner);   /* +FLAGS / -FLAGS; 0 bien */
int  co_imap_mueve(struct co_imap *s, uint32_t uid, const char *destino);            /* MOVE (o COPY + borrar); 0 bien */
int  co_imap_borra(struct co_imap *s, uint32_t uid);                                 /* \Deleted y EXPUNGE; 0 bien */
int  co_imap_anade(struct co_imap *s, const char *carpeta, const char *msg, uint32_t n, uint32_t flags);   /* APPEND; 0 bien */

/* manda un mensaje ya compuesto (co_compone). 0 bien; err: el motivo en claro */
int  co_smtp_envia(struct co_tr *t, const struct co_cuenta *c, const struct co_dir *rcpt, int nr, const char *msg, uint32_t n, char *err, uint32_t ecap);
/* solo comprueba que el SMTP deja entrar (conecta, EHLO, STARTTLS, AUTH y QUIT). 0 bien */
int  co_smtp_prueba(struct co_tr *t, const struct co_cuenta *c, char *err, uint32_t ecap);
#endif
