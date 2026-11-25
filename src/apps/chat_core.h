#ifndef SOMA_CHAT_CORE_H
#define SOMA_CHAT_CORE_H
/* SOMA_285_CHAT: el historial de conversaciones de la aplicacion de chat (src/apps/chat.c), sin nada del nucleo: se prueba en el anfitrion.
 * Una conversacion es una lista de mensajes (usuario / SOMA) con su modo (solo LLM o agente), si usaba la memoria, la hora, lo que tardo
 * la respuesta y los adjuntos. Todo se guarda en un solo archivo de texto (/chat/historial.txt) con longitudes delante de cada texto, de
 * modo que cualquier contenido (comillas, saltos de linea, binario) vuelve igual. Las horas son minutos locales desde 1970 (agenda_ahora). */
#include <stdint.h>

#define CC_TIT_MAX   64
#define CC_TXT_MAX   (1u << 20)          /* un mensaje no pasa de 1 MiB */
#define CC_CONV_MAX  400
#define CC_MSG_MAX   2000                /* por conversacion */

enum { CC_USER = 0, CC_ASIS = 1 };
enum { CC_MODO_LLM = 0, CC_MODO_AGENTE = 1 };
enum { CC_OK = 0, CC_ERROR = 1, CC_CORTADA = 2 };

struct cc_msg {
    uint8_t  rol, modo, mem, estado;
    uint32_t t;                          /* minutos locales */
    uint32_t dur;                        /* ms que tardo la respuesta (SOMA) */
    char    *txt; uint32_t n;
    char    *adj;                        /* rutas de los adjuntos separadas por '\n' (NULL = ninguno) */
};
struct cc_conv {
    uint32_t id, t0, t1;                 /* creada, ultimo mensaje */
    uint8_t  modo, mem, fija, titulo_manual;
    char     titulo[CC_TIT_MAX];
    struct cc_msg *m; uint32_t nm, cap;
};
struct cc_hist { struct cc_conv *c; uint32_t n, cap, sig_id; };

/* el que usa el modulo debe darlos (en el anfitrion, malloc/free) */
void *cc_alloc(uint32_t bytes);
void  cc_free(void *p);

void cc_init(struct cc_hist *h);
void cc_libera(struct cc_hist *h);
struct cc_conv *cc_nueva(struct cc_hist *h, uint32_t t, int modo, int mem);    /* NULL = no cabe */
struct cc_conv *cc_por_id(struct cc_hist *h, uint32_t id);
int  cc_indice(const struct cc_hist *h, uint32_t id);                         /* -1 = no esta */
int  cc_borra(struct cc_hist *h, uint32_t id);                                /* 1 = borrada */
struct cc_msg *cc_anade(struct cc_conv *c, int rol, const char *txt, uint32_t n, const char *adj, uint32_t t);
void cc_trunca(struct cc_conv *c, uint32_t desde);                            /* quita los mensajes desde ese */
void cc_titulo_auto(struct cc_conv *c);                                       /* de la primera pregunta (si no lo puso el usuario) */
void cc_titulo_pon(struct cc_conv *c, const char *t);

/* archivo */
uint32_t cc_serializa(const struct cc_hist *h, char *out, uint32_t max);      /* bytes; con out NULL, los que hacen falta */
int  cc_carga(struct cc_hist *h, const char *buf, uint32_t n);                /* conversaciones leidas; -1 = no es un historial */

/* lista lateral: fijadas primero, despues por ultimo mensaje (mas reciente arriba); filtro en titulo y textos (sin mayusculas ni tildes) */
int  cc_orden(const struct cc_hist *h, const char *filtro, uint32_t *ids, int max);
int  cc_grupo(const struct cc_conv *c, uint32_t ahora);                       /* 0 fijadas, 1 hoy, 2 ayer, 3 7 dias, 4 30 dias, 5 anteriores */
const char *cc_grupo_nombre(int g);
int  cc_coincide(const char *texto, uint32_t n, const char *filtro);          /* 1 = filtro dentro de texto (sin mayusculas ni tildes) */

/* contexto para el modelo */
uint32_t cc_ventana(const struct cc_conv *c, uint32_t hasta, uint32_t presupuesto);   /* primer mensaje que cabe con [x, hasta) <= presupuesto */
uint32_t cc_contexto(const struct cc_conv *c, uint32_t hasta, char *dst, uint32_t max);   /* "Usuario: ...\nSOMA: ...\n" de lo anterior */

/* fechas */
void cc_hora(uint32_t t, char *out);                                          /* "14:05" (6 bytes) */
void cc_fecha(uint32_t t, uint32_t ahora, char *out, int cap);                /* "hoy, 14:05" / "ayer, 9:30" / "23 nov, 14:05" / "23 nov 2025" */
#endif
