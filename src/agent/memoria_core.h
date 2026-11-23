/* SOMA_277_MEMORIA: nucleo de la memoria de SOMA, portado de memorIA (memoria-unificada v0.3.0a6).
 * Codigo puro (sin nucleo, sin libc, sin coma flotante): lo usan el glue del kernel (memoria.c) y las pruebas del anfitrion.
 *
 * Que es: una memoria con dos tipos de elementos, como en memorIA:
 *   - CELDAS: hechos atomicos y autosuficientes ("El usuario vive en Bilbao"), con fuente, fecha de vigencia e importancia.
 *   - EPISODIOS: los mensajes literales, que nunca se resumen ni se olvidan.
 * Cada elemento puede llevar un vector de embedding cuantizado a int8 (norma 127; coseno ~ producto escalar / 16129). Sin servidor de
 * embeddings la busqueda cae a un canal lexical (palabras en comun ponderadas por rareza), igual que memorIA cae a FTS5/BM25.
 * La memoria se guarda en fragmentos de MC_SHARD elementos (un archivo por fragmento) para reescribir solo lo que cambia.
 *
 * SOMA_279_SUENO: arbol de nodos (temas/subtemas), curva del olvido, restaurar lo archivado y las piezas del ciclo de sueno
 * (ordenar, describir, deduplicar, rescatar, partir y unir ramas). Lo que decide el LLM llega aqui como respuestas ya leidas. */
#ifndef SOMA_MEMORIA_CORE_H
#define SOMA_MEMORIA_CORE_H
#include <stdint.h>

#define MC_DIM_MAX      1024
#define MC_SHARD        1024u                 /* elementos por archivo */
#define MC_MAX_SHARDS   40u                   /* 40 960 elementos (el VFS admite 256 archivos en total) */
#define MC_MAX_ITEMS    (MC_SHARD * MC_MAX_SHARDS)
#define MC_TEXTO_MAX    1200                  /* bytes por texto de celda o episodio */
#define MC_FUENTE_MAX   200
#define MC_K_MAX        64
#define MC_EPI_CLIP     500                   /* bytes de un mensaje literal que se muestran en el prompt */
#define MC_COS_UNO      16129                 /* 127 * 127: producto escalar de dos vectores iguales */

enum { MC_CELDA = 1, MC_EPISODIO = 2 };
enum { MC_ACTIVO = 0, MC_ARCHIVADO = 1, MC_SUSTITUIDO = 2 };
enum { MC_ROL_USUARIO = 0, MC_ROL_ASISTENTE = 1, MC_ROL_DOC = 2 };
enum { MC_REL_NINGUNA = 0, MC_REL_SUSTITUYE, MC_REL_IGUAL, MC_REL_COMPLEMENTA, MC_REL_CONFLICTO, MC_REL_NO_RELACIONADO };

struct mc_item {
    uint32_t id;               /* 1.. correlativo; el indice en el array es id - 1 */
    uint32_t t;                /* segundos Unix: cuando se supo (valid_from) */
    uint32_t t_hasta;          /* 0 = vigente; si no, cuando dejo de valer */
    uint32_t sust_por;         /* id de la celda que lo sustituye */
    uint32_t tick;             /* marca del ultimo refuerzo (la usa el ciclo de sueno) */
    char    *texto, *fuente;   /* terminados en NUL, dentro de la arena */
    int8_t  *vec;              /* NULL = aun sin embedding del servidor */
    uint16_t tlen, flen, dim;
    uint8_t  kind, estado, imp, rol, refuerzos, nodo, _pad[2];     /* nodo: 0 = sin ordenar aun (ciclo de sueno) */
};

/* SOMA_279_SUENO: nodos del arbol de memoria (como memorIA: Tema/Subtema/Detalle) */
#define MC_NODOS_MAX    255u
#define MC_NIVELES      3u
#define MC_NODO_NOMBRE  48
#define MC_NODO_DESC    120
#define MC_MAX_HIJOS    12u                   /* un nodo con mas celdas se parte en dos */
struct mc_nodo {
    uint8_t usado, padre, nivel, fijo, sucio, _p[3];        /* sucio = su descripcion esta desactualizada */
    uint16_t cuenta, _q;                                     /* celdas vigentes (se recalcula) */
    char nombre[MC_NODO_NOMBRE], desc[MC_NODO_DESC];
};

#define MC_BLOQUE (256u * 1024u)
#define MC_BLOQUES_MAX 640u

struct mc {
    struct mc_item *it;
    uint32_t n, cap;
    uint8_t  sucio[MC_MAX_SHARDS];
    uint8_t *bloque[MC_BLOQUES_MAX];
    uint32_t nbloques, uso;    /* bytes usados del ultimo bloque */
    struct mc_nodo nodo[MC_NODOS_MAX + 1];                   /* SOMA_279_SUENO: nodo[0] no se usa */
    uint32_t tick, pasos;                                    /* reloj de mantenimiento: sube un tick por paso del ciclo de sueno */
    uint8_t  arbol_sucio;
};

/* el que usa el nucleo debe darlos (en el anfitrion, malloc/free) */
void *mc_alloc(uint32_t bytes);
void  mc_free(void *p);

int  mc_init(struct mc *m, uint32_t cap);               /* 0 = bien */
void mc_destroy(struct mc *m);
const struct mc_item *mc_get(const struct mc *m, uint32_t id);   /* 0 si no existe */

/* Anade un elemento. vec/dim opcionales (vec ya cuantizado). Devuelve el id (> 0) o 0 si no cabe. */
uint32_t mc_add(struct mc *m, int kind, int rol, const char *texto, const char *fuente, int imp, uint32_t t, uint32_t tick,
                const int8_t *vec, uint32_t dim);
int  mc_set_vec(struct mc *m, uint32_t id, const int8_t *vec, uint32_t dim);        /* 0 = bien */
int  mc_set_estado(struct mc *m, uint32_t id, int estado, uint32_t t_hasta, uint32_t sust_por);
int  mc_refuerza(struct mc *m, uint32_t id, uint32_t tick);                         /* repetir un hecho lo hace mas importante (hasta 10) */
uint32_t mc_find_exacto(const struct mc *m, int kind, const char *texto);           /* id de la celda vigente con el mismo texto (sin mayusculas/acentos/puntuacion) o 0 */

/* ---- busqueda ----
 * qvec/qdim: vector de la consulta (NULL = sin servidor). qtext: la consulta, para el canal lexical.
 * kindmask: bits (1<<MC_CELDA) | (1<<MC_EPISODIO). historial: 1 = tambien archivados y sustituidos.
 * minsim: coseno minimo (x MC_COS_UNO) para el canal vectorial. Devuelve cuantos (<= k) en ids/score, mejores primero.
 * score: producto escalar del canal vectorial (>= 0) o, para el canal lexical, -(1 + puntos) (negativo: se distingue por el signo). */
int  mc_busca(const struct mc *m, const int8_t *qvec, uint32_t qdim, const char *qtext, uint32_t kindmask, int historial, int minsim,
              uint32_t k, uint32_t *ids, int32_t *score);
/* los k vecinos mas cercanos de un vector entre las celdas vigentes (para deduplicar/sustituir). Devuelve cuantos. */
int  mc_vecinos(const struct mc *m, const int8_t *qvec, uint32_t qdim, uint32_t k, uint32_t *ids, int32_t *dots);
int32_t mc_dot(const int8_t *a, const int8_t *b, uint32_t dim);

/* ---- embeddings ---- */
/* Lee de la respuesta JSON de /v1/embeddings el primer "embedding": [...] y lo deja normalizado y cuantizado a int8 (norma 127).
 * Devuelve la dimension (> 0) o <= 0 si no hay vector valido. */
int  mc_parse_embedding(const char *json, int8_t *out, uint32_t max_dim);

/* ---- textos ---- */
uint32_t mc_fold(const char *s, char *out, uint32_t cap);       /* minusculas, sin acentos, solo letras/cifras separadas por un espacio */
int  mc_parece_actualizacion(const char *viejo, const char *nuevo);   /* heuristica de memorIA (_looks_like_temporal_update) */
int  mc_noFactual(const char *texto);                           /* saludos, preguntas, cortesias: no se extraen hechos */
uint32_t mc_epoch(int y, int mo, int d, int h, int mi, int s);  /* fecha civil UTC -> segundos Unix */
void mc_fecha(uint32_t t, char *out, uint32_t cap);             /* "dd/mm/aaaa hh:mm" */

/* ---- respuestas del LLM ---- */
struct mc_hecho { char texto[600]; int imp; int instruccion; };
/* {"hechos":[{"texto":"...","importancia":8,"tipo":"dato"}]} (tolera ```json y texto alrededor). Devuelve cuantos o -1 si no es JSON valido. */
int  mc_parse_hechos(const char *reply, struct mc_hecho *out, int max);
/* {"relation":"supersedes|same|...","confidence":0.9}: devuelve MC_REL_* y la confianza en centesimas (0..100) */
int  mc_parse_relacion(const char *reply, int *conf100);

/* ---- prompts (los de memorIA, medidos) ---- */
int  mc_prompt_hechos(const char *texto, char *out, uint32_t cap);
int  mc_prompt_relacion(const char *viejo, uint32_t id_viejo, const char *nuevo, const char *fecha, char *out, uint32_t cap);

/* Bloque de memoria para el prompt del agente: celdas (hechos consolidados con su fuente) y episodios literales. Devuelve los bytes escritos. */
uint32_t mc_contexto(const struct mc *m, const uint32_t *celdas, uint32_t nc, const uint32_t *episodios, uint32_t ne, char *out, uint32_t cap);

/* Los ultimos mensajes de la charla (en el orden dado), con su propio titulo: es el "corto plazo" de memorIA. */
uint32_t mc_contexto_charla(const struct mc *m, const uint32_t *ids, uint32_t n, char *out, uint32_t cap);

/* ---- guardado por fragmentos ---- */
uint32_t mc_nfrag(const struct mc *m);                                   /* fragmentos en uso */
uint32_t mc_frag_sucio(const struct mc *m, uint32_t idx);                /* 1 si hay que guardarlo */
void mc_frag_limpio(struct mc *m, uint32_t idx);
int  mc_frag_guarda(const struct mc *m, uint32_t idx, uint8_t *buf, uint32_t cap);     /* bytes escritos, o -1 si no cabe */
int  mc_frag_carga(struct mc *m, const uint8_t *buf, uint32_t len);                    /* 0 = bien; el fragmento debe seguir al ultimo cargado */
uint32_t mc_frag_max_bytes(void);                                        /* tamano maximo de un fragmento (para reservar el buffer) */
uint32_t mc_crc32(const uint8_t *p, uint32_t n);

/* ---- SOMA_279_SUENO: arbol de nodos, olvido y ciclo de sueno ----
 * Un nodo es un tema (Tema/Subtema/Detalle, hasta MC_NIVELES). Cada celda vigente cuelga de un nodo (0 = aun sin ordenar).
 * El reloj de mantenimiento (m->tick) sube un tick por paso del ciclo de sueno: la edad se mide en ticks, no en segundos (como memorIA). */
int  mc_asigna(struct mc *m, uint32_t id, uint32_t nodo);                       /* 0 = bien */
uint32_t mc_nodo_ruta(struct mc *m, const char *ruta, int crear);               /* "Tema/Sub": id del nodo mas profundo, o 0 */
void mc_nodo_ruta_texto(const struct mc *m, uint32_t nodo, char *out, uint32_t cap);
uint32_t mc_recuenta(struct mc *m);                                              /* recalcula las cuentas; devuelve los nodos en uso */
uint32_t mc_poda_nodos(struct mc *m);                                            /* quita los nodos vacios sin hijos; devuelve cuantos */
uint32_t mc_nodo_hijos(const struct mc *m, uint32_t nodo);
int  mc_nodo_set_desc(struct mc *m, uint32_t nodo, const char *desc);
int  mc_nodo_fusiona(struct mc *m, uint32_t a, uint32_t b, const char *nombre); /* b desaparece dentro de a */
uint32_t mc_nodo_celdas(const struct mc *m, uint32_t nodo, uint32_t *ids, uint32_t max);   /* celdas vigentes de un nodo */
uint32_t mc_sin_nodo(const struct mc *m, uint32_t *ids, uint32_t max);          /* celdas vigentes sin ordenar, las mas antiguas primero */
uint32_t mc_nodo_sucio(const struct mc *m);                                      /* nodo con descripcion pendiente (el mas profundo), o 0 */
uint32_t mc_nodo_sobrecargado(struct mc *m);                                     /* nodo con mas de MC_MAX_HIJOS celdas, o 0 */
int  mc_set_texto(struct mc *m, uint32_t id, const char *texto);                 /* cambia el texto; el vector queda pendiente */
int  mc_set_imp(struct mc *m, uint32_t id, int imp, uint32_t tick);

/* Curva del olvido de memorIA: eff = imp * exp(-edad / (tau * imp^3)), en enteros (x256). */
uint32_t mc_eff_q8(uint32_t imp, uint32_t edad, uint32_t tau);
uint32_t mc_olvida(struct mc *m, uint32_t tick, uint32_t min_edad, uint32_t tau, uint32_t bajo_q8);   /* archiva las celdas con eff < bajo; devuelve cuantas */
uint32_t mc_restaura(struct mc *m, const uint32_t *ids, uint32_t n, uint32_t tick);   /* lo archivado por olvido que vuelve a salir en una busqueda; cuantas */
uint32_t mc_candidato_rescate(const struct mc *m, uint32_t tick, uint32_t min_edad, uint32_t tau, uint32_t bajo_q8,
                              const uint32_t *saltar, uint32_t ns);   /* la mas vieja a punto de archivarse (sin las ya juzgadas), o 0 */

/* Parejas candidatas. `vistos`: claves ya juzgadas (a<<32 | b, con a < b) que se saltan. */
int  mc_par_redundante(const struct mc *m, uint32_t *cursor, const uint64_t *vistos, uint32_t nv, uint32_t *a, uint32_t *b);   /* 1 si hay pareja */
int  mc_par_ramas(const struct mc *m, const uint64_t *vistos, uint32_t nv, uint32_t *a, uint32_t *b, int *sim100);          /* sim100 < 0: sin vectores */

/* Prompts de memorIA y lectura de lo que contesta el LLM */
struct mc_asig { uint32_t id; char ruta[96]; };
int  mc_prompt_ordena(const struct mc *m, const uint32_t *ids, uint32_t n, char *out, uint32_t cap);
int  mc_parse_ordena(const char *reply, struct mc_asig *out, int max);           /* cuantas, o -1 */
int  mc_prompt_describe(const struct mc *m, uint32_t nodo, char *out, uint32_t cap);
int  mc_parse_descripcion(const char *reply, char *out, uint32_t cap);           /* 1 si hay */
struct mc_fusion { int redundante; char texto[600]; int imp; };
int  mc_prompt_fusion(const struct mc *m, uint32_t a, uint32_t b, char *out, uint32_t cap);
int  mc_parse_fusion(const char *reply, struct mc_fusion *f);                    /* 1 si se entiende */
int  mc_aplica_fusion(struct mc *m, uint32_t a, uint32_t b, const struct mc_fusion *f, uint32_t tick, uint32_t ahora);   /* 0 = hecho */
int  mc_prompt_rescate(const struct mc *m, uint32_t id, char *out, uint32_t cap);
int  mc_parse_rescate(const char *reply, int *imp);                              /* 1 conservar (imp 5..10), 0 no, -1 ilegible */
struct mc_split { char nombre_a[MC_NODO_NOMBRE], nombre_b[MC_NODO_NOMBRE]; uint32_t ids_a[16], ids_b[16]; uint32_t na, nb; };
int  mc_prompt_split(const struct mc *m, uint32_t nodo, char *out, uint32_t cap);
int  mc_parse_split(const char *reply, struct mc_split *sp);                     /* 1 si se entiende */
int  mc_aplica_split(struct mc *m, uint32_t nodo, const struct mc_split *sp);    /* celdas movidas */
int  mc_prompt_unir(const struct mc *m, uint32_t a, uint32_t b, int sim100, char *out, uint32_t cap);
int  mc_parse_unir(const char *reply, char *nombre, uint32_t cap);               /* 1 si, 0 no, -1 ilegible */

/* El arbol y el reloj se guardan aparte de los fragmentos (un archivo pequeño) */
uint32_t mc_arbol_max_bytes(void);
int  mc_arbol_guarda(const struct mc *m, uint8_t *buf, uint32_t cap);            /* bytes o -1 */
int  mc_arbol_carga(struct mc *m, const uint8_t *buf, uint32_t len);             /* 0 = bien */

/* ---- SOMA_280_IMPORTAR: lee el formato .smem que escribe tools/exporta_memoria.py (el cartucho de memorIA) ----
 * Lineas de texto con campos separados por TAB (dentro de un campo: \\ \t \n \r). La primera: "#SOMAMEM 1 modelo=bge-m3 dim=1024 parte=1/3".
 *   N  ruta  descripcion                                         un nodo del arbol
 *   C  ord imp epoch estado sust hasta ruta fuente texto [vec]   una celda (estado 0 activa, 1 archivada por olvido, 2 sustituida; sust = ord de la que la sustituye en ESTA parte)
 *   E  epoch rol texto [vec]                                     un mensaje literal (rol 0 usuario, 1 asistente, 2 documento)
 * vec = int8 de norma 127 en base64. Lo que ya esta en la memoria (mismo texto sin mayusculas ni acentos) se salta: importar dos veces no duplica. */
struct mc_imp {
    uint32_t *ord_id, *ord_sust, ord_cap, ord_max;     /* ordinal de la parte -> id en la memoria / ordinal que lo sustituye */
    uint64_t *hs; uint32_t hs_cap;                     /* tabla abierta de los textos que ya hay */
    uint32_t tick, ahora, vec_ok, dim_vec;             /* ahora: reloj para lo que viene sin fecha; */
    /* vec_ok: aceptar vectores; dim_vec: dimension que se espera (0 = la de la cabecera) */
    uint32_t celdas, epis, repetidas, malas, sin_vec, nodos_llenos, lleno, nodos_desc;
    uint32_t cab_ok, cab_dim, cab_parte, cab_partes;
    char modelo[40];
};
int  mc_imp_ini(struct mc *m, struct mc_imp *s, uint32_t ord_cap);               /* reserva las tablas; 0 = bien */
int  mc_imp_linea(struct mc *m, struct mc_imp *s, char *linea);                  /* 0 = entra, 1 = ignorada o repetida, -1 = mal formada, -2 = memoria llena. Modifica `linea`. */
void mc_imp_fin(struct mc *m, struct mc_imp *s);                                 /* enlaza las sustituciones, deja las cuentas al dia y libera */

/* ---- SOMA_283_MEMORIA_APP: lo que necesita la aplicacion de Memoria (borrar de verdad, ramas a mano, copiar entre cartuchos) ---- */
/* Borra de verdad los elementos marcados (quita[i] = 1 para el id i + 1) y rehace la memoria sin huecos: los ids cambian, las sustituciones se
 * renumeran (si se borra la celda que sustituia a otra, la antigua vuelve a estar vigente) y la arena se recupera (tambien lo que dejaron los
 * textos editados). Todos los fragmentos quedan por guardar. Devuelve cuantos se borraron, o -1 si no hay memoria (entonces no cambia nada). */
int  mc_compacta(struct mc *m, const uint8_t *quita);
int  mc_nodo_renombra(struct mc *m, uint32_t nodo, const char *nombre);         /* 0 = bien; -1 si no existe o ya hay un hermano con ese nombre */
int  mc_nodo_dentro(const struct mc *m, uint32_t nodo, uint32_t rama);          /* 1 si nodo es la rama o cuelga de ella */
/* Quita una rama (el nodo y sus subtemas). Sus celdas quedan sin ordenar; si marca != 0, marca[i] = 1 para cada celda de la rama. Devuelve cuantas. */
uint32_t mc_nodo_quita(struct mc *m, uint32_t nodo, uint8_t *marca);
/* Escritura en el formato .smem (el mismo que lee mc_imp_linea): asi se copian recuerdos entre cartuchos y se exportan. Devuelven bytes (0 = no cabe). */
uint32_t mc_smem_cabecera(const char *modelo, uint32_t dim, char *out, uint32_t cap);
uint32_t mc_smem_nodo(const struct mc *m, uint32_t nodo, char *out, uint32_t cap);
uint32_t mc_smem_item(const struct mc *m, uint32_t id, uint32_t ord, uint32_t sust_ord, int con_vec, char *out, uint32_t cap);   /* C (ord >= 1) o E */
#define MC_SMEM_LINEA_MAX (2 * MC_TEXTO_MAX + 2 * MC_FUENTE_MAX + 3 * 52 + MC_DIM_MAX * 4 / 3 + 128)
uint32_t mc_bytes(const struct mc *m);                                           /* RAM de la arena en uso */

/* estadisticas */
void mc_stats(const struct mc *m, uint32_t *celdas, uint32_t *episodios, uint32_t *archivadas, uint32_t *sustituidas, uint32_t *sin_vec);
#endif
