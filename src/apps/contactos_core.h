/* SOMA_357_CONTACTOS: nucleo de la libreta de contactos. Logica pura (sin nucleo, sin libc): ficha completa, busqueda y orden sin
 * distinguir mayusculas ni acentos, lenguaje natural en castellano ("Luis Perez, email luis@x.es, movil 600 11 22 33, nacio el 3 de mayo
 * de 1990, grupo amigos"), fechas de nacimiento y cumpleanos, y vCard 2.1/3.0/4.0 (importar con fusion y exportar).
 *
 * Seguridad: un .vcf que llega de fuera solo se lee con ct_vcard_lee(), que corre en el servicio aislado mimesvc (ring 3) en x86_64.
 * Lo que vuelve son fichas de tamano fijo, sin punteros; el nucleo las pasa por ct_sanea() antes de usarlas. Las fotos que trae el vCard
 * (PHOTO en base64) salen como bytes aparte (foto_off/foto_len en la respuesta) y se vuelven a decodificar aisladas antes de guardarse.
 *
 * Sincronizacion en los dos sentidos por archivo (Google Contactos, iCloud, Outlook, el movil...): cada ficha lleva `cambiado` (tocada en
 * SOMA desde la ultima exportacion). Al importar, en un conflicto gana lo de SOMA si la ficha esta cambiada y no hay un REV mas nuevo
 * en el archivo; si no, gana lo importado. Un campo vacio en el archivo nunca borra lo que hay en SOMA. Exportar deja todo "sin cambios". */
#ifndef CONTACTOS_CORE_H
#define CONTACTOS_CORE_H
#include <stdint.h>

#define CT_MAX      1500
#define CT_NOM      48
#define CT_APE      64
#define CT_APODO    32
#define CT_NEMAIL   3
#define CT_EMAIL    80
#define CT_NTEL     3
#define CT_TEL      32
#define CT_CALLE    96
#define CT_CIUDAD   48
#define CT_CP       12
#define CT_PROV     48
#define CT_PAIS     40
#define CT_GRUPO    64          /* grupos separados por comas: "Familia,Amigos" */
#define CT_EMPRESA  64
#define CT_CARGO    48
#define CT_WEB      96
#define CT_NOTA     300
#define CT_UID      72
#define CT_REV      20          /* "20261127T101500Z" */
#define CT_URL      200         /* foto en la nube (Google exporta un enlace) */
#define CT_FOTOS    6           /* fotos por contacto (la 1 es la de la ficha) */

enum { CT_T_OTRO = 0, CT_T_CASA, CT_T_TRABAJO, CT_T_MOVIL };

struct ct_contacto {
    uint32_t id;                                /* 0 = hueco libre */
    uint8_t  activo, fav, cambiado, nfotos;     /* nfotos: fotos guardadas en SOMA (0..CT_FOTOS) */
    uint8_t  voz, pad_[3];                      /* voz: hay muestra de voz grabada */
    int16_t  nac_y;                             /* 0 = ano desconocido */
    uint8_t  nac_m, nac_d;                      /* nac_m = 0: sin fecha */
    uint8_t  t_email[CT_NEMAIL], t_tel[CT_NTEL];
    uint32_t foto_off, foto_len;                /* solo en la respuesta del lector aislado: la foto del vCard dentro de los datos */
    char nombre[CT_NOM], apellidos[CT_APE], apodo[CT_APODO];
    char email[CT_NEMAIL][CT_EMAIL], tel[CT_NTEL][CT_TEL];
    char calle[CT_CALLE], ciudad[CT_CIUDAD], cp[CT_CP], prov[CT_PROV], pais[CT_PAIS];
    char grupo[CT_GRUPO], empresa[CT_EMPRESA], cargo[CT_CARGO], web[CT_WEB], nota[CT_NOTA];
    char uid[CT_UID], rev[CT_REV], foto_url[CT_URL];
};
struct ct_libreta { uint32_t sig_id; struct ct_contacto c[CT_MAX]; };

/* ---- la libreta */
void     ct_vacia(struct ct_libreta *l);
struct ct_contacto *ct_nuevo(struct ct_libreta *l);                 /* hueco con id nuevo (activo), o 0 si esta llena */
struct ct_contacto *ct_busca_id(struct ct_libreta *l, uint32_t id);
int      ct_borra(struct ct_libreta *l, uint32_t id);               /* 1 si existia */
uint32_t ct_numero(const struct ct_libreta *l);
void     ct_sanea(struct ct_contacto *c);                           /* cadenas terminadas, UTF-8 valido, sin controles; tipos y fechas en rango */
int      ct_vacio(const struct ct_contacto *c);                     /* 1 si no tiene ni nombre ni apellidos ni apodo ni email ni telefono ni empresa */

/* ---- texto */
void ct_nombre_completo(const struct ct_contacto *c, char *out, uint32_t cap);   /* "Luis Perez Garcia"; si no hay: apodo, empresa, email o telefono */
void ct_iniciales(const struct ct_contacto *c, char out[8]);        /* "LP" (UTF-8) */
uint32_t ct_ficha_txt(const struct ct_contacto *c, int hoy_y, int hoy_m, int hoy_d, char *out, uint32_t cap);   /* todos los datos, una linea por dato */
void ct_linea_txt(const struct ct_contacto *c, char *out, uint32_t cap);         /* "[12] Luis Perez  luis@x.es  600 11 22 33" */
const char *ct_nombre_tipo(int t);                                   /* "casa", "trabajo", "movil", "" */
const char *ct_nombre_mes(int m);                                    /* 1..12 */

/* ---- busqueda y orden (sin mayusculas ni acentos) */
int  ct_coincide(const struct ct_contacto *c, const char *texto);    /* el texto esta en nombre, apellidos, apodo, emails, telefonos, empresa, grupo o ciudad */
int  ct_en_grupo(const struct ct_contacto *c, const char *grupo);
/* indices (en l->c) ordenados por nombre y apellidos; filtro texto (o 0), grupo (o 0), solo_fav. Devuelve cuantos. */
int  ct_lista(const struct ct_libreta *l, const char *texto, const char *grupo, int solo_fav, uint16_t *idx, int max);
/* el contacto al que se refiere un texto ("12", "Ana", "Ana Lopez", "luis@x.es"): indices de los mejores candidatos. */
int  ct_resuelve(const struct ct_libreta *l, const char *txt, uint16_t *idx, int max);
int  ct_grupos(const struct ct_libreta *l, char grupos[][CT_GRUPO], int max);    /* nombres de grupo distintos */
int  ct_pliega(const char *s, char *out, uint32_t cap);              /* minusculas sin acentos (para comparar); bytes escritos */

/* ---- fechas y cumpleanos */
int  ct_parsea_fecha(const char *s, int *y, int *m, int *d);         /* "3 de mayo de 1990", "3/5/1990", "1990-05-03", "--0503", "3 mayo": 0 = bien (y = 0 sin ano) */
int  ct_dias_para_cumple(const struct ct_contacto *c, int hoy_y, int hoy_m, int hoy_d);   /* 0 hoy, 1 manana...; -1 sin fecha */
int  ct_edad(const struct ct_contacto *c, int hoy_y, int hoy_m, int hoy_d);               /* -1 si no se sabe */
/* proximos cumpleanos en `dias` dias: indices ordenados por cercania (y los dias que faltan en `falta`) */
int  ct_cumples(const struct ct_libreta *l, int hoy_y, int hoy_m, int hoy_d, int dias, uint16_t *idx, int16_t *falta, int max);

/* ---- editar */
/* campo: nombre, apellidos, apodo, email|email2|email3, tel|telefono|movil|tel2|tel3|fijo, direccion|calle, ciudad, cp, provincia, pais,
 * grupo, empresa, cargo, nacimiento|cumpleanos, web, nota, favorito. Valor vacio o "-" vacia el campo. 0 = bien, -1 campo desconocido,
 * -2 valor no valido (fecha, email). */
int  ct_pon_campo(struct ct_contacto *c, const char *campo, const char *valor);
int  ct_es_campo(const char *palabra);                               /* longitud de la palabra si es un nombre de campo (para "editar Ana telefono 600") */
/* "Luis Perez, email luis@x.es, movil 600 11 22 33, nacio el 3 de mayo de 1990, vive en Calle Mayor 3, Madrid, grupo amigos".
 * 0 = bien (al menos un nombre, email o telefono), -1 = no hay nada con que hacer una ficha. */
int  ct_parsea_texto(const char *txt, struct ct_contacto *out);
void ct_marca(struct ct_contacto *c, const char *rev);               /* tocado en SOMA: cambiado = 1 y la fecha REV */

/* ---- vCard */
/* una ficha en vCard 3.0 (CRLF, lineas plegadas a 75). interno = 1 anade X-SOMA-* (para el archivo propio de SOMA). foto: bytes JPEG/PNG
 * o 0. Devuelve bytes escritos o 0 si no cabe. */
uint32_t ct_vcard_uno(const struct ct_contacto *c, int interno, const uint8_t *foto, uint32_t foto_len, char *out, uint32_t cap);
/* Lee un .vcf (2.1, 3.0, 4.0; plegado, quoted-printable, latin-1, grupos itemN., TYPE de todas las formas). Escribe hasta max fichas
 * saneadas en out; si fotos != 0, copia ahi las fotos base64 decodificadas (hasta fotos_cap bytes en total, cada una <= foto_max) y
 * anota foto_off/foto_len. Devuelve fichas leidas, o -1 si no es un vCard. *fotos_n = bytes de fotos usados. */
int  ct_vcard_lee(const char *txt, uint32_t len, struct ct_contacto *out, int max, uint8_t *fotos, uint32_t fotos_cap, uint32_t foto_max, uint32_t *fotos_n);
/* Fusiona una ficha importada (saneada) con la libreta. Devuelve 1 nueva, 2 actualizada, 0 sin cambios, -1 libreta llena.
 * *dest = la ficha de la libreta (nueva o la que coincidia). */
int  ct_fusiona(struct ct_libreta *l, const struct ct_contacto *n, struct ct_contacto **dest);
/* lo mismo, pero si manda_nuevo los datos nuevos ganan siempre ("contactos anadir" de alguien que ya estaba) */
int  ct_fusiona_ex(struct ct_libreta *l, const struct ct_contacto *n, struct ct_contacto **dest, int manda_nuevo);

/* ---- base64 (fotos) */
uint32_t ct_b64_dec(const char *s, uint32_t n, uint8_t *out, uint32_t cap);
uint32_t ct_b64_enc(const uint8_t *s, uint32_t n, char *out, uint32_t cap);
#endif
