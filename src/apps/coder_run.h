#ifndef SOMA_CODER_RUN_H
#define SOMA_CODER_RUN_H
/* SOMA_333_SALIDA: el programador ejecuta los programas (Python, C, JavaScript) sin abrir el Terminal. Lo que imprimen sale en
 * el panel "Salida" del propio programador; si piden datos (input(), scanf) se escriben ahi; Esc o "Parar" los detiene. Si el
 * programa no imprime nada (un juego con su ventana) el panel no estorba y se cierra solo al terminar. */
#include <stdint.h>

enum { CRUN_NADA, CRUN_MARCHA, CRUN_FIN };

int  crun_start(const char *cmd, const char *nombre);   /* 0 = en marcha; -1 ya hay uno; -2 el sistema esta capturando otra orden */
int  crun_estado(void);
int  crun_poll(void);                                    /* pasa a lineas lo que ha impreso; 1 si hay que redibujar */
int  crun_feed(const char *linea);                       /* una linea para el programa; -1 si no la espera nadie */
void crun_parar(void);
void crun_cerrar(void);                                  /* oculta el panel (si ya termino) */
int  crun_visible(void);
int  crun_lineas(void);
const char *crun_linea(int i);                           /* 0 = la mas antigua que se conserva */
const char *crun_prompt(void);                           /* lo que el programa dejo sin salto de linea ("Nombre? ") */
const char *crun_nombre(void);
uint32_t crun_ms(void);                                  /* cuanto lleva (o cuanto duro) */
int  crun_parado(void);                                  /* 1 si lo paro la persona */
int  crun_fallo(void);                                   /* 1 si termino con error (codigo de salida distinto de 0) */

/* --- el formato de la salida, sin nucleo (se prueba en el anfitrion) --- */
#define CRUN_NL   400                                    /* lineas que se conservan */
#define CRUN_LW   200                                    /* caracteres por linea (las largas se parten) */
struct crun_buf { char l[CRUN_NL][CRUN_LW]; int ini, n; char par[CRUN_LW]; int pn; int esc; };
void crun_buf_vacia(struct crun_buf *b);
void crun_buf_mete(struct crun_buf *b, const char *s, uint32_t n);   /* texto tal cual: quita ANSI y \r, parte en lineas */
void crun_buf_cierra(struct crun_buf *b);                             /* lo que quedo sin salto pasa a ser una linea */
const char *crun_buf_linea(const struct crun_buf *b, int i);

#endif
