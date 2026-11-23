/* SOMA_277_MEMORIA: la memoria de SOMA (portada de memorIA). Pegamento con el nucleo: configuracion, servidor de embeddings, hilo de
 * ingesta, guardado en /memoria y la orden `memoria`. La logica pura esta en memoria_core.c (probada en el anfitrion). */
#ifndef SOMA_MEMORIA_H
#define SOMA_MEMORIA_H
#include <stdint.h>

/* Para el agente */
int  memoria_activa(void);                                                            /* 1 = la memoria nueva esta en uso (si no, el agente usa la ventana antigua) */
int  memoria_contexto(const char *mision, char *dst, uint32_t max, uint32_t *pos);   /* anade al prompt los recuerdos relacionados; 1 si habia */
void memoria_registra(const char *usuario, const char *respuesta);                   /* guarda el intercambio y encola la extraccion de hechos */
void memoria_agente_entra(void);                                                     /* la extraccion de hechos espera a que los agentes esten libres */
void memoria_agente_sale(void);
int  memoria_busca_agente(const char *consulta, char *out, uint32_t max);            /* mem_search del agente */
int  memoria_recuerda_agente(const char *texto, const char *fuente, char *out, uint32_t max);   /* mem_write del agente */

/* La orden `memoria` (y `memoria ayuda`) */
int  memoria_cmd(const char *args, char *out_buf, uint32_t max_out);
void memoria_guarda_todo(void);                                                      /* apagado / disco sync */
/* SOMA_283_MEMORIA_APP: para la aplicacion de Memoria (src/apps/memview.c). Leer y editar es inmediato (memoria_gui_entra/sale, con el cerrojo
 * de la memoria tomado: poco rato). Lo que tarda (LLM, servidor de embeddings, disco, borrar de verdad, cartuchos) se pide y lo hace un hilo. */
struct mc;
struct mem_vista {
    int activa, ingesta, sueno, sueno_s, n_hechos, n_epis, srv, srv_dim, emb_caido;
    uint32_t emb_ok, emb_mal;
    char url[80], modelo[40];
    uint32_t celdas, episodios, archivadas, sustituidas, sin_vec, nodos, sin_ordenar, total, cap, nfrag, bytes;
    uint32_t tick, pasos, n_rest, n_olv, n_fus, n_nuevos, n_dup, n_sust, n_fallo, qn;
    int agentes, dormido, extrayendo, tarea;                 /* tarea: la que corre (dormido) o la siguiente (0..8) */
    uint32_t seg_calma, seg_proximo;
    uint32_t ninf; char inf[10][150];
    int ops_ocupado; uint32_t ops_n; char ops_msg[200];
    char cartucho[40]; int nmont;
};
struct mem_cart { int idx; char id[20], nombre[40], desc[100]; int primario, montado; uint32_t celdas, nodos, kb; };
struct mem_res { int cart, primario; uint32_t id, t; int32_t score; uint8_t kind, estado, imp, rol; char texto[240]; char cart_nombre[40]; };
enum { MEMP_SUENO = 1, MEMP_SRV_ON, MEMP_SRV_OFF, MEMP_PROBAR, MEMP_GUARDAR, MEMP_BUSCAR, MEMP_BORRA, MEMP_BORRA_RAMA, MEMP_COPIA, MEMP_MUEVE,
       MEMP_C_NUEVO, MEMP_C_PRIM, MEMP_C_MONTA, MEMP_C_DESMONTA, MEMP_C_VACIA, MEMP_C_BORRA, MEMP_C_EXPORTA, MEMP_C_CLONA, MEMP_IMPORTA };
/* a, b: segun la peticion (cartucho, nodo, pasos, con vectores...). ids: elementos del primario. gen: memoria_gui_gen() cuando se eligieron */
struct mem_pet { int op; uint32_t a, b, nids, gen; uint32_t ids[64]; char s1[160], s2[120]; };
void     memoria_vista(struct mem_vista *v);
uint32_t memoria_version(void);                       /* cambia cuando hay algo nuevo que pintar */
uint32_t memoria_gui_gen(void);                       /* cambia cuando los ids dejan de valer */
struct mc *memoria_gui_entra(void);                   /* toma el cerrojo (0 si no esta lista); soltar enseguida */
void     memoria_gui_sale(int cambiado);              /* cambiado = 1: se guardara */
uint32_t memoria_gui_ahora(void);
int      memoria_gui_pide(const struct mem_pet *p);   /* 1 = aceptada; 0 = hay otra en curso */
int      memoria_gui_cartuchos(struct mem_cart *o, int max);
int      memoria_gui_resultados(struct mem_res *o, int max, uint32_t *version);
int      memoria_gui_config(const char *clave, int valor, const char *texto);
#endif
