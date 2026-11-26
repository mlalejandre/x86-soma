#ifndef SOMA_NOTICIAS_H
#define SOMA_NOTICIAS_H
/* SOMA_337_NOTICIAS: la aplicacion de Noticias (estado y trabajo; la ventana esta en noticias_gui.c).
 *   - Categorias que elige la persona (Portada, Espana, Local, Mundo, Economia, Tecnologia, Ciencia, Salud, Deportes, Cultura),
 *     de Google Noticias en espanol; Local busca por el lugar que se le diga (ciudad, provincia o region); canales RSS propios.
 *   - Se descarga por netsvc (ring 3) y el XML se lee en leesvc (ring 3): nada de lo que llega se analiza en el nucleo.
 *   - Resumen con el modelo de lenguaje de cada categoria, citando [n] cada titular, con la lista de fuentes al final.
 *   - "Guardar en memoria": el resumen y los titulares van al cartucho de memoria "Noticias" (se crea solo; no cambia el primario).
 * Configuracion en /etc/noticias.conf. */
#include <stdint.h>
#include "noticias_rss.h"

enum { NTE_NADA, NTE_CARGANDO, NTE_BIEN, NTE_ERROR };
struct nt_vista {
    int estado, n; char err[120]; uint64_t cargada_ms;          /* titulares */
    int res_estado; uint32_t res_ver; char res_err[120];         /* resumen: NTE_* */
    int guarda_estado; char guarda_msg[200];                     /* "Guardar en memoria": NTE_* */
};
void noticias_conf(struct nt_conf *c);                       /* copia de la configuracion */
void noticias_pon_conf(const struct nt_conf *c);             /* la guarda y recarga lo que haga falta */
void noticias_carga(int cat, int forzar);                    /* pide los titulares (en otro hilo); forzar: aunque sean recientes */
void noticias_vista(int cat, struct nt_vista *v);
int  noticias_item(int cat, int i, struct nt_item *o);       /* copia el titular i; 0 si no existe */
int  noticias_resume(int cat);                               /* pide el resumen con IA; 0 = en marcha */
uint32_t noticias_resumen(int cat, char *o, uint32_t cap);   /* el resumen (Markdown) */
int  noticias_guarda(int cat, char *msg, uint32_t cap);      /* al cartucho "Noticias"; 0 = bien (tarda: calcula vectores) */
int  noticias_guarda_pide(int cat);                          /* lo mismo en el hilo de noticias; el resultado, en la vista */
uint32_t noticias_version(void);                             /* cambia cuando hay algo nuevo */
int64_t noticias_ahora(void);                                /* minutos UTC (para "hace 5 min") */
int  noticias_cmd(const char *args, char *out, uint32_t cap);
#endif
