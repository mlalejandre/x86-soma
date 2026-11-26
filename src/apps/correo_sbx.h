/* SOMA_311_CORREO_AISLADO: el correo lee los mensajes en un SERVICIO AISLADO de ring 3 (src/user/mimesvc.c).
 *
 * Un mensaje de correo es lo mas hostil que entra en SOMA: lo escribe cualquiera en Internet y llega tal cual. Leerlo es decodificar
 * MIME, base64, quoted-printable, juegos de caracteres, palabras RFC 2047 y HTML. Antes todo eso corria en el nucleo (ring 0): un
 * fallo en cualquiera de esos analizadores era un fallo del sistema entero, y uno explotable daba el control de la maquina.
 * Ahora corre en mimesvc.elf, que solo puede llamar a exit: no ve la memoria del nucleo ni la de otros procesos, no tiene red ni
 * disco, tiene plazo y su monton tiene tope. Si un mensaje lo tumba, la aplicacion lo dice y el sistema sigue.
 *
 * Lo que devuelve el servicio NO se cree: correo_mime_sanea y correo_msg_sanea (abajo, puras: tests/test_correo_sbx_311.c) cierran
 * cada cadena, acotan contadores y quitan los adjuntos cuyo trozo no cae dentro del mensaje.
 * En aarch64 (todavia sin modo usuario, EL0) las tres funciones llaman directamente a correo_core.c, como antes. */
#ifndef SOMA_CORREO_SBX_H
#define SOMA_CORREO_SBX_H
#include <stdint.h>
#include "correo_core.h"

/* Lo que usa la aplicacion (correo.c, correo_red.c) en vez de co_mime_lee / co_adj_saca / co_ficha */
int      correo_mime_lee(const char *raw, uint32_t n, struct co_mime *m, char *texto, uint32_t cap);   /* 1 si lo entiende */
uint32_t correo_adj_saca(const char *raw, uint32_t n, const struct co_adj *a, uint8_t *out, uint32_t cap);
void     correo_ficha(struct co_msg *m, const char *cab, uint32_t cab_n, const char *cuerpo, uint32_t cuerpo_n);

/* Texto que se ensena cuando el lector aislado no ha podido con el mensaje */
#define CORREO_ILEGIBLE "SOMA no ha podido leer este correo de forma segura: el lector aislado lo ha rechazado o se ha detenido " \
                        "(el mensaje puede estar mal formado o ser malicioso). El resto del sistema no se ha visto afectado."

static inline void correo_cierra(char *s, uint32_t cap) { if (cap) s[cap - 1] = 0; }

/* Deja una co_mime que llega del servicio en un estado que el nucleo puede usar sin mirar mas:
 * cadenas terminadas, banderas a 0/1, texto_n <= texto_max - 1, y solo los adjuntos cuyo trozo [off, off+len) cae en el mensaje. */
static inline void correo_mime_sanea(struct co_mime *m, uint32_t raw_n, uint32_t texto_max)
{
    correo_cierra(m->asunto, sizeof m->asunto); correo_cierra(m->de, sizeof m->de); correo_cierra(m->para, sizeof m->para);
    correo_cierra(m->cc, sizeof m->cc); correo_cierra(m->responder_a, sizeof m->responder_a);
    correo_cierra(m->msgid, sizeof m->msgid); correo_cierra(m->refs, sizeof m->refs);
    m->html = m->html ? 1 : 0;
    m->truncado = m->truncado ? 1 : 0;
    if (texto_max == 0) m->texto_n = 0;
    else if (m->texto_n > texto_max - 1) m->texto_n = texto_max - 1;
    int n = m->nadj, k = 0;
    if (n < 0) n = 0;
    if (n > CO_ADJ_MAX) n = CO_ADJ_MAX;
    for (int i = 0; i < n; ++i) {
        struct co_adj a = m->adj[i];
        if (a.off > raw_n || a.len > raw_n - a.off) continue;        /* fuera del mensaje: se descarta */
        correo_cierra(a.nombre, sizeof a.nombre); correo_cierra(a.tipo, sizeof a.tipo);
        if (a.cte != CO_CTE_7BIT && a.cte != CO_CTE_QP && a.cte != CO_CTE_B64) a.cte = CO_CTE_7BIT;
        a.en_linea = a.en_linea ? 1 : 0;
        if (a.tam > a.len) a.tam = a.len;                           /* decodificado nunca ocupa mas que codificado */
        m->adj[k++] = a;
    }
    for (int i = k; i < CO_ADJ_MAX; ++i) { struct co_adj z = { {0}, {0}, 0, 0, 0, 0, 0 }; m->adj[i] = z; }
    m->nadj = k;
}

/* Lo mismo para la ficha de la lista. uid, flags y tam los pone el llamante (vienen del FETCH, no del servicio). */
static inline void correo_msg_sanea(struct co_msg *m)
{
    correo_cierra(m->de, sizeof m->de); correo_cierra(m->de_email, sizeof m->de_email); correo_cierra(m->para, sizeof m->para);
    correo_cierra(m->asunto, sizeof m->asunto); correo_cierra(m->extracto, sizeof m->extracto); correo_cierra(m->msgid, sizeof m->msgid);
    m->adj = m->adj ? 1 : 0;
    if (m->cat > CO_CAT_PROMO) m->cat = CO_CAT_NADA;
    if (m->prio > 3) m->prio = 0;
}
#endif
