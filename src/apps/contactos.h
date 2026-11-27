/* SOMA_357_CONTACTOS: la libreta de contactos dentro del sistema. La logica esta en contactos_core.c (pura, probada en el host).
 *   DATOS:  /contactos/contactos.vcf        las fichas (vCard 3.0 con X-SOMA-*; sin fotos)
 *   BIGFS:  /contactos/fotos/<id>_<k>.jpg   hasta 6 fotos por contacto (JPEG de 640 px como mucho, vuelto a codificar en SOMA)
 *           /contactos/voz/<id>.wav         una muestra de voz (16 kHz, hasta 30 s)
 * Importar/exportar .vcf (Google Contactos, iCloud, Outlook, el movil) en los dos sentidos; los .vcf que llegan de fuera se leen en el
 * servicio aislado mimesvc (ring 3) y sus fotos se decodifican en imgdec (ring 3). Los cumpleanos entran en el Calendario. */
#ifndef CONTACTOS_H
#define CONTACTOS_H
#include <stdint.h>
#include "contactos_core.h"

#define CT_VCF_DEF "/usuario/contactos.vcf"

struct ct_libreta *contactos_libreta(void);         /* carga de DATOS la primera vez (nunca 0 salvo sin memoria) */
int      contactos_guarda(void);                    /* 0 = bien; tambien pone los cumpleanos en el Calendario */
uint32_t contactos_version(void);                   /* cambia con cada modificacion */
const char *contactos_error_carga(void);           /* "" o por que no se pudieron leer (entonces no se sobrescribe nada) */
void     contactos_rev(char out[CT_REV]);           /* ahora, en UTC, para REV */
void     contactos_hoy(int *y, int *m, int *d);     /* fecha local */

int  contactos_anade_texto(const char *txt, char *res, uint32_t cap);   /* >0 id; -1 no entiendo; -2 llena; -3 no se guarda */
int  contactos_borra(uint32_t id);                  /* tambien sus fotos y su voz */
int  contactos_toca(uint32_t id);                   /* marca cambiado + REV y guarda */
int  contactos_importa(const char *ruta, char *res, uint32_t cap);      /* fichas tocadas (nuevas + actualizadas); <0 error (res lo explica) */
int  contactos_exporta(const char *ruta, int con_fotos, char *res, uint32_t cap);

/* fotos (k = 1..nfotos; la 1 es la de la ficha) */
int  contactos_foto_lee(uint32_t id, int k, uint8_t **buf, uint32_t *len);       /* 0 = bien (kfree(*buf)) */
int  contactos_foto_anade(uint32_t id, const char *ruta, char *msg, uint32_t cap);
int  contactos_foto_anade_bytes(uint32_t id, const uint8_t *d, uint32_t n);      /* decodifica aislado, reduce y guarda; 0 = bien */
int  contactos_foto_borra(uint32_t id, int k);
int  contactos_foto_principal(uint32_t id, int k);
int  contactos_imagenes(char rutas[][96], int max);  /* fotos de la camara (/fotos) e imagenes de /usuario, las mas nuevas primero */

/* voz */
enum { CTV_NADA = 0, CTV_GRABANDO, CTV_GUARDANDO, CTV_ESCUCHANDO };
int  contactos_voz_graba(uint32_t id, char *msg, uint32_t cap);          /* 0 = empieza */
void contactos_voz_para(void);                                           /* para (y guarda si grababa) */
int  contactos_voz_escucha(uint32_t id, char *msg, uint32_t cap);
int  contactos_voz_borra(uint32_t id);
int  contactos_voz_estado(uint32_t *id, uint32_t *ms, int *nivel);       /* CTV_* */

int  contactos_tick(void);                          /* la GUI, en cada vuelta: voz (corte a los 30 s y guardado) y fotos de la nube. 1 = repintar */
int  contactos_cmd(const char *args, char *out, uint32_t max);            /* la orden `contactos` */
/* SOMA_357: si una peticion al agente trata de alguien de la libreta (o de cumpleanos), su ficha entra en el contexto */
void contactos_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos);
#endif
