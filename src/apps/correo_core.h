/* correo_core.h: SOMA_295_CORREO: la logica pura del correo (sin red, sin pantalla, sin memoria dinamica salvo co_alloc/co_free).
 * Codificaciones (base64, quoted-printable, juegos de caracteres, palabras codificadas RFC 2047, UTF-7 modificado de IMAP, HTML a
 * texto), cabeceras y direcciones, fechas RFC 5322, lectura de mensajes MIME (texto y adjuntos), respuestas IMAP (literales, FETCH,
 * LIST, SEARCH) y SMTP, composicion de mensajes RFC 5322 en UTF-8, busqueda local con filtros, agenda de contactos y lectura de lo
 * que contesta el modelo de lenguaje. Se prueba en el anfitrion: tests/test_correo_295.c. */
#ifndef SOMA_CORREO_CORE_H
#define SOMA_CORREO_CORE_H
#include <stdint.h>

void *co_alloc(uint32_t n);            /* las da quien enlaza (correo.c en SOMA; la prueba en el anfitrion) */
void  co_free(void *p);

/* ---------------------------------------------------------------- texto */
uint32_t co_len(const char *s);
void     co_cpy(char *d, const char *s, uint32_t cap);
void     co_cpyn(char *d, const char *s, uint32_t n, uint32_t cap);
void     co_cat(char *d, const char *s, uint32_t cap);
void     co_num(char *d, uint32_t cap, uint64_t v);           /* anade el numero */
int      co_ieq(const char *a, const char *b);                /* iguales sin mayusculas (ASCII) */
int      co_iempieza(const char *s, const char *p);
const char *co_ibusca(const char *s, uint32_t n, const char *p);   /* p dentro de s[0..n) sin mayusculas (ASCII); 0 si no */
/* minusculas, sin tildes ni dieresis, enie -> n, signos -> espacio (para comparar lo que escribe la gente) */
uint32_t co_norm(const char *s, char *out, uint32_t cap);

/* ---------------------------------------------------------------- codificaciones */
uint32_t co_b64_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap);
uint32_t co_b64_enc(const uint8_t *d, uint32_t n, char *out, uint32_t cap);   /* sin saltos de linea; 0 si no cabe */
uint32_t co_qp_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap, int cabecera);   /* cabecera: '_' = espacio */
uint32_t co_qp_enc(const char *s, uint32_t n, char *out, uint32_t cap);        /* cuerpo: lineas de 76 con '=' suave, CRLF */
/* bytes en un juego de caracteres -> UTF-8 (utf-8, us-ascii, iso-8859-1/15, windows-1252; lo desconocido se deja tal cual) */
uint32_t co_a_utf8(const uint8_t *s, uint32_t n, const char *charset, char *out, uint32_t cap);
/* cabecera -> UTF-8: despliega lineas, decodifica =?charset?B|Q?...?= (y junta las seguidas) */
uint32_t co_cab_dec(const char *s, uint32_t n, char *out, uint32_t cap);
/* UTF-8 -> cabecera: tal cual si es ASCII; si no, palabras =?UTF-8?B?...?= de hasta 75 caracteres separadas por CRLF + espacio */
uint32_t co_cab_enc(const char *utf8, char *out, uint32_t cap);
uint32_t co_utf7_dec(const char *s, char *out, uint32_t cap);   /* nombre de carpeta IMAP (UTF-7 modificado) -> UTF-8 */
uint32_t co_utf7_enc(const char *utf8, char *out, uint32_t cap);
uint32_t co_html_texto(const char *html, uint32_t n, char *out, uint32_t cap);   /* etiquetas fuera, entidades, parrafos y enlaces */

/* ---------------------------------------------------------------- cabeceras, direcciones y fechas */
/* valor de la cabecera "nombre" de un mensaje (o de una parte), desplegado y SIN decodificar; longitud o -1 si no esta */
int co_cabecera(const char *msg, uint32_t n, const char *nombre, char *out, uint32_t cap);
/* parametro de una cabecera estructurada: Content-Type: text/plain; charset="utf-8" -> "utf-8" (RFC 2231 basico incluido) */
int co_param(const char *valor, const char *param, char *out, uint32_t cap);
struct co_dir { char nombre[80]; char email[120]; };
int  co_dirs(const char *s, struct co_dir *out, int max);       /* lista de direcciones (ya decodificada) */
void co_dir_txt(const struct co_dir *d, char *out, uint32_t cap);   /* "Nombre <email>" o "email" */
int  co_email_valido(const char *e);
int64_t co_fecha(const char *s);                                /* RFC 5322 -> minutos UTC desde 1970 (0 si no se entiende) */
void co_fecha_rfc(int64_t utc_min, int off_min, char *out, uint32_t cap);   /* "Tue, 24 Nov 2026 10:53:00 +0100" */
int  co_desfase(int64_t utc_min);                               /* minutos de Europa/Madrid respecto a UTC (60 o 120) */

/* ---------------------------------------------------------------- mensajes MIME */
#define CO_ADJ_MAX 12
enum { CO_CTE_7BIT = 0, CO_CTE_QP, CO_CTE_B64 };
struct co_adj { char nombre[96]; char tipo[64]; uint32_t off, len; int cte; uint32_t tam; int en_linea; };
struct co_mime {
    char asunto[256], de[200], para[600], cc[400], responder_a[200], msgid[200], refs[600];
    int64_t fecha;
    uint32_t texto_n; int html;                 /* texto (UTF-8) en el bufer que se pasa; html = salia de text/html */
    int nadj; struct co_adj adj[CO_ADJ_MAX];
    int truncado;                               /* el mensaje llego incompleto (se pidio solo el principio) */
};
/* raw = mensaje entero (cabeceras + cuerpo). Devuelve 1 si lo entiende. texto: el cuerpo legible, en UTF-8. */
int co_mime_lee(const char *raw, uint32_t n, struct co_mime *m, char *texto, uint32_t cap);
uint32_t co_adj_saca(const char *raw, uint32_t n, const struct co_adj *a, uint8_t *out, uint32_t cap);   /* el adjunto decodificado */
/* el extracto de un texto: una linea, sin citas (">") ni firmas, hasta cap */
void co_extracto(const char *texto, uint32_t n, char *out, uint32_t cap);

/* ---------------------------------------------------------------- IMAP */
#define CO_F_VISTO       1u
#define CO_F_DESTACADO   2u
#define CO_F_RESPONDIDO  4u
#define CO_F_BORRADO     8u
#define CO_F_BORRADOR   16u
/* longitud de la primera respuesta completa del bufer (linea + literales {n}), 0 si aun falta */
uint32_t co_imap_completa(const char *b, uint32_t n);
/* 1 OK, -1 NO o BAD, 0 si la respuesta no es la de esa etiqueta. texto: lo que dice el servidor despues de OK/NO/BAD */
int co_imap_etiqueta(const char *r, uint32_t n, const char *tag, char *texto, uint32_t cap);
struct co_fetch {
    uint32_t seq, uid, flags, tam; int tiene_flags;
    const char *cab; uint32_t cab_n;            /* BODY[HEADER.FIELDS (...)] o BODY[HEADER] */
    const char *cuerpo; uint32_t cuerpo_n;      /* BODY[] o BODY[]<0> */
};
int co_imap_fetch(const char *r, uint32_t n, struct co_fetch *f);    /* "* 12 FETCH (...)": 1 si lo es */
enum { CO_U_NADA = 0, CO_U_ENTRADA, CO_U_ENVIADOS, CO_U_BORRADORES, CO_U_PAPELERA, CO_U_SPAM, CO_U_ARCHIVO, CO_U_TODOS, CO_U_DESTACADOS };
struct co_carpeta { char nombre[96]; char imap[160]; int uso; int noselect; uint32_t no_leidos, total; };
int co_imap_list(const char *r, uint32_t n, struct co_carpeta *c);   /* "* LIST (\HasNoChildren \Sent) "/" "Enviados"": 1 si lo es */
uint32_t co_imap_search(const char *r, uint32_t n, uint32_t *uids, uint32_t max);   /* "* SEARCH 4 8 15": cuantos */
int co_imap_num(const char *r, uint32_t n, const char *clave, uint32_t *v);          /* "* 23 EXISTS", "[UIDNEXT 456]"... */
int co_imap_cap(const char *r, uint32_t n, const char *cap);                          /* la capacidad aparece en CAPABILITY */
/* argumento IMAP: "cadena entre comillas" (con \" y \\), o 0 si hace falta un literal (CR, LF, NUL o mas de 1000 bytes) */
uint32_t co_imap_cadena(const char *s, char *out, uint32_t cap);
const char *co_uso_nombre(int uso);                              /* "Recibidos", "Enviados"... */

/* ---------------------------------------------------------------- SMTP y composicion */
/* 1 si b empieza por una respuesta completa (multilinea "250-..." terminada en "250 ..."); codigo y bytes usados */
int co_smtp_resp(const char *b, uint32_t n, int *codigo, uint32_t *usado);
#define CO_S_STARTTLS  1u
#define CO_S_PLAIN     2u
#define CO_S_LOGIN     4u
#define CO_S_8BIT      8u
#define CO_S_UTF8     16u
#define CO_S_XOAUTH   32u
uint32_t co_smtp_caps(const char *ehlo, uint32_t n);
uint32_t co_smtp_plain(const char *usuario, const char *clave, char *out, uint32_t cap);   /* base64("\0usuario\0clave") */
struct co_envio {
    const char *de_nombre, *de_email, *para, *cc, *cco, *asunto, *cuerpo;
    const char *en_respuesta_a, *referencias;   /* Message-ID del que se responde y su cadena (pueden ser 0) */
    int64_t utc_min; int off_min; uint32_t azar;
    char msgid[160];                            /* sale: el Message-ID puesto */
    /* SOMA_360_HOJA_GRAFICOS: un adjunto (0 = sin adjunto): multipart/mixed con el archivo en base64 */
    const char *adj_nombre; const uint8_t *adj; uint32_t adj_n;
};
const char *co_tipo_mime(const char *nombre);   /* SOMA_360: "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" por la extension */
uint32_t co_compone(struct co_envio *e, char *out, uint32_t cap);       /* mensaje RFC 5322 entero con CRLF; 0 si no cabe */
int  co_destinatarios(const struct co_envio *e, struct co_dir *out, int max);   /* Para + Cc + Cco, sin repetir */
uint32_t co_puntea(const char *msg, uint32_t n, char *out, uint32_t cap);      /* DATA: ".." al principio de linea y "\r\n.\r\n" */
/* "Re: " / "Fwd: " sin repetirlos; cita del mensaje original para responder o reenviar */
void co_asunto_resp(const char *asunto, int reenvio, char *out, uint32_t cap);
uint32_t co_cita(const char *de, int64_t fecha_local, const char *texto, uint32_t n, int reenvio, const char *asunto, const char *para, char *out, uint32_t cap);

/* ---------------------------------------------------------------- lo guardado: mensajes y busqueda */
struct co_msg {
    uint32_t uid, flags, tam;
    int64_t fecha;                              /* minutos UTC */
    char de[96], de_email[100], para[160], asunto[200], extracto[180], msgid[120];
    uint8_t adj, prio, cat, _r;                 /* adj: tiene adjuntos; prio/cat: lo que dijo la IA (0 = sin clasificar) */
};
enum { CO_CAT_NADA = 0, CO_CAT_IMPORTANTE, CO_CAT_PERSONAL, CO_CAT_TRABAJO, CO_CAT_NOTIF, CO_CAT_PROMO };
const char *co_cat_nombre(int cat);
/* cabeceras de un FETCH -> co_msg (lo que se ve en la lista) */
void co_msg_de_cab(struct co_msg *m, const char *cab, uint32_t n);
/* SOMA_311_CORREO_AISLADO: la ficha entera de la lista: cabeceras + extracto del principio del cuerpo + si tiene adjuntos.
 * Conserva uid, flags y tam de m. cuerpo puede ser 0 (solo cabeceras). Antes estaba en correo_red.c (msg_de_fetch). */
void co_ficha(struct co_msg *m, const char *cab, uint32_t cab_n, const char *cuerpo, uint32_t cuerpo_n);
uint32_t co_msgs_serializa(const struct co_msg *v, int n, char *out, uint32_t cap);   /* bytes; 0 si no cabe */
int      co_msgs_carga(const char *t, uint32_t n, struct co_msg *v, int max);          /* cuantos */

struct co_filtro {
    char de[100], para[100], asunto[120], palabras[200];
    int64_t desde, hasta;                       /* minutos UTC (0 = sin limite) */
    int no_leidos, destacados, adjuntos, cat;
};
/* "de:ana asunto:factura tiene:adjunto no leidos hoy reforma" -> filtro. ahora_local: minutos locales */
void co_filtro_texto(const char *q, int64_t ahora_local, struct co_filtro *f);
/* {"de":"...","asunto":"...","palabras":"...","desde":"2026-11-01","hasta":"...","no_leidos":true,...} (lo que da el modelo) */
int  co_filtro_json(const char *json, struct co_filtro *f);
int  co_filtro_vacio(const struct co_filtro *f);
int  co_coincide(const struct co_msg *m, const struct co_filtro *f);   /* puntos (>0 = coincide) */
int  co_busca(const struct co_msg *v, int n, const struct co_filtro *f, int *idx, int max);   /* por puntos y fecha */
/* criterios de UID SEARCH para el servidor (lo que no esta en la copia local) */
uint32_t co_filtro_imap(const struct co_filtro *f, char *out, uint32_t cap);

/* contactos sacados de los mensajes (remitentes y destinatarios), por veces */
int co_contactos(const struct co_msg *v, int n, const char *propio, struct co_dir *out, int *veces, int max);
/* "Ana", "ana garcia", "ana@" -> la direccion mas probable; 1 si la hay */
int co_resuelve(const struct co_msg *v, int n, const char *propio, const char *quien, struct co_dir *out);

/* ---------------------------------------------------------------- lo que contesta el modelo */
/* "PARA: ...\nASUNTO: ...\nCUERPO:\n..." (tambien con **negritas**, sin PARA, o solo el cuerpo). 1 si hay cuerpo */
int co_ia_borrador(const char *r, char *para, uint32_t pcap, char *asunto, uint32_t acap, char *cuerpo, uint32_t ccap);
int co_ia_ids(const char *r, uint32_t *ids, int max);           /* "IDS: 3, 7, 12" -> 3 */
int co_ia_cats(const char *r, uint32_t *ids, uint8_t *cats, int max);   /* "12: importante\n13: promocion" */
uint32_t co_ia_limpia(const char *r, char *out, uint32_t cap);  /* quita <think>, ``` y espacios de mas */

/* ---------------------------------------------------------------- cuentas */
struct co_cuenta {
    char email[120], nombre[80], usuario[120], clave[160];
    char imap[96], smtp[96]; uint16_t imap_p, smtp_p; uint8_t imap_tls, smtp_tls;   /* tls: 1 directo, 2 STARTTLS */
    uint8_t gmail;                              /* Gmail guarda los enviados solo (no se hace APPEND) */
};
#define CO_TLS_DIRECTO  1
#define CO_TLS_STARTTLS 2
/* servidores conocidos por el dominio del correo; 1 si lo conoce, 0 si se ponen imap.<dominio> y smtp.<dominio>. aviso: nota para el usuario */
int co_cuenta_auto(struct co_cuenta *c, const char **aviso);
uint32_t co_cuenta_serializa(const struct co_cuenta *c, char *out, uint32_t cap);
int      co_cuenta_carga(struct co_cuenta *c, const char *t, uint32_t n);
#endif
