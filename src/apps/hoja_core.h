/* SOMA_358_HOJA: nucleo de la hoja de calculo. Logica pura (sin nucleo, sin libc, sin FPU: los numeros son f64 de software,
 * web_f64.c). Se compila en el nucleo, en el servicio aislado mimesvc (lee XLSX/ODS/CSV de fuera) y en el anfitrion (pruebas).
 *
 *   Libro -> hasta HJ_HOJAS hojas -> celdas dispersas (tabla hash por hoja). Estilos compartidos y deduplicados.
 *   Formulas: se guardan en texto canonico (el de Excel/XLSX: nombres en ingles, ',' entre argumentos, '.' decimal) y se compilan a
 *   codigo RPN (pila explicita: nada de recursion en C). En pantalla y al escribir se usan en castellano (SUMA(A1;B1), 1,5).
 *   Recalculo: todas las formulas, en orden de filas, con una pila de trabajo explicita para las dependencias y deteccion de ciclos.
 *
 * Memoria: hj_alloc/hj_free los pone cada entorno (nucleo, mimesvc, pruebas). Limites: HJ_CELDAS_MAX celdas por libro, textos de
 * HJ_TXT_MAX bytes, formulas de HJ_FORM_MAX bytes. */
#ifndef HOJA_CORE_H
#define HOJA_CORE_H
#include <stdint.h>
#include "web_f64.h"

#define HJ_HOJAS       32
#define HJ_FILAS       1048576          /* filas 0..HJ_FILAS-1 (como Excel) */
#define HJ_COLS        16384            /* columnas A..XFD */
#define HJ_CELDAS_MAX  400000
#define HJ_TXT_MAX     32767
#define HJ_FORM_MAX    8192
#define HJ_NOMBRE      32               /* nombre de hoja: 31 bytes como mucho */
#define HJ_ESTILOS     4096
#define HJ_NFCOD       48
#define HJ_ANCHO_DEF   90               /* px */
#define HJ_ALTO_DEF    24
#define HJ_GRAF_MAX    16

void *hj_alloc(uint32_t n);             /* lo define cada entorno */
void  hj_free(void *p);
extern int64_t (*hj_ahora_fn)(void);    /* minutos locales desde 1970 (HOY/AHORA); 0 = 2026-01-01 */

enum { HJ_VACIA = 0, HJ_NUM, HJ_TXT, HJ_BOOL, HJ_ERR };
enum { HJE_NINGUNO = 0, HJE_NULO, HJE_DIV0, HJE_VALOR, HJE_REF, HJE_NOMBRE, HJE_NUM, HJE_NA, HJE_CICLO, HJE_N };
/* formatos de numero */
enum { NF_GENERAL = 0, NF_NUMERO, NF_MONEDA, NF_PORCENTAJE, NF_FECHA, NF_HORA, NF_FECHAHORA, NF_TEXTO, NF_CIENTIFICA, NF_CODIGO };
enum { AL_AUTO = 0, AL_IZQ, AL_CENTRO, AL_DER };

struct hj_estilo {
    uint8_t negrita, cursiva, subrayado, tachado;
    uint8_t alinear, valinear, ajustar, tam;        /* tam: puntos de letra (0 = 11) */
    uint8_t bordes;                                 /* bits: 1 arriba, 2 abajo, 4 izquierda, 8 derecha */
    uint8_t nf, decimales, miles;                   /* NF_*; decimales 0..15; miles = separador de miles */
    uint32_t color, fondo;                          /* 0x01RRGGBB (bit 24 = puesto); 0 = por defecto */
    char codigo[HJ_NFCOD];                          /* codigo de formato de Excel (NF_CODIGO, o el original de un archivo) */
};

struct hj_celda {
    uint32_t fila; uint16_t col, estilo;
    uint8_t t, err, es_form, estado;                /* t: HJ_*; es_form 2 = formula de un archivo con su valor (se respeta si SOMA no la sabe calcular) */
    uint32_t gen;                                   /* recalculo en el que se calculo (formulas) */
    f64 n;                                          /* numero o booleano (0/1) */
    char *txt;                                      /* texto (valor o resultado) */
    char *form;                                     /* formula canonica sin '=' */
    uint8_t *code; uint32_t ncode;                  /* compilada (cache) */
    int32_t sig;                                    /* cadena de la tabla hash */
};

struct hj_tam { uint32_t i; uint16_t px; };
struct hj_grafico {                                 /* SOMA_360 los pinta; aqui solo se guardan y viajan */
    uint8_t tipo;                                   /* 0 barras, 1 columnas, 2 lineas, 3 tarta, 4 area, 5 dispersion */
    uint8_t hoja_datos; uint8_t leyenda, pad;
    uint32_t r1, c1, r2, c2;                        /* rango de datos (primera fila = series, primera columna = categorias) */
    uint32_t fila, col, ancho, alto;                /* donde se dibuja (celda de arriba a la izquierda, px) */
    char titulo[64];
};

struct hj_hoja {
    char nombre[HJ_NOMBRE];
    struct hj_celda *c; int32_t nc, capc;           /* celdas (huecos con t == HJ_VACIA y sin estilo se reutilizan) */
    int32_t *hash; uint32_t nhash;                  /* potencia de 2 */
    int32_t libres;                                 /* lista de huecos (por sig) */
    uint32_t max_fila, max_col;                     /* ultima usada + 1 */
    struct hj_tam *anchos; uint32_t nanchos, capanchos;    /* columnas con ancho propio */
    struct hj_tam *altos; uint32_t naltos, capaltos;       /* filas con alto propio */
    uint32_t inmov_f, inmov_c;                      /* filas/columnas inmovilizadas */
    uint8_t *oculta; uint32_t noculta;              /* filas ocultas (filtro): mapa de bits hasta noculta filas */
    uint32_t filtro_r1, filtro_c1, filtro_r2, filtro_c2; uint8_t hay_filtro;
    struct { uint32_t col; char crit[64]; } fil[8]; uint8_t nfil;         /* criterios del autofiltro (todos a la vez) */
    struct hj_grafico graf[HJ_GRAF_MAX]; uint8_t ngraf;
};

struct hj_libro {
    struct hj_hoja h[HJ_HOJAS]; int nh, activa;
    struct hj_estilo *e; uint32_t ne;               /* el 0 es el normal */
    uint32_t gen;                                   /* generacion de recalculo */
    uint32_t ncel;                                  /* celdas vivas (limite) */
    uint32_t version;                               /* cambia con cada modificacion */
    uint8_t sin_memoria;
};

/* un valor (de una celda o de una evaluacion) */
struct hj_valor { uint8_t t, err; f64 n; const char *s; };

/* ---- libro */
int   hj_libro_nuevo(struct hj_libro *l);           /* 0 = bien (una hoja "Hoja1") */
void  hj_libro_libera(struct hj_libro *l);
int   hj_hoja_nueva(struct hj_libro *l, const char *nombre);       /* indice o -1 */
int   hj_hoja_borra(struct hj_libro *l, int h);                    /* 0 = bien (no la ultima); las referencias a ella pasan a #REF! */
int   hj_hoja_renombra(struct hj_libro *l, int h, const char *nombre);   /* 0 bien, -1 nombre no valido, -2 repetido; las formulas siguen */
int   hj_hoja_mueve(struct hj_libro *l, int de, int a);
int   hj_hoja_busca(const struct hj_libro *l, const char *nombre); /* sin mayusculas ni acentos */

/* ---- celdas */
struct hj_celda *hj_celda(struct hj_libro *l, int h, uint32_t f, uint32_t c);           /* existente o 0 */
const struct hj_celda *hj_celda_c(const struct hj_libro *l, int h, uint32_t f, uint32_t c);
/* lo que escribe el usuario (castellano): "=SUMA(A1:A3)", "1.234,5", "12 %", "3/5/2026", "'texto"... Ajusta el estilo si hace falta
 * (porcentaje, moneda, fecha). 0 = bien; -1 formula no valida (err = posicion), -2 sin memoria o limite */
int   hj_escribe(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *txt, int *err);
int   hj_pon_num(struct hj_libro *l, int h, uint32_t f, uint32_t c, f64 v);
int   hj_pon_txt(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *s);
int   hj_pon_bool(struct hj_libro *l, int h, uint32_t f, uint32_t c, int b);
int   hj_pon_err(struct hj_libro *l, int h, uint32_t f, uint32_t c, int e);
int   hj_pon_form(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *canon);   /* canonica (de un archivo); 0 = bien */
/* formula de un archivo con el valor que traia (ya puesto en la celda con hj_pon_*): marca es_form = 2 */
int   hj_pon_form_cache(struct hj_libro *l, int h, uint32_t f, uint32_t c, const char *canon);
int   hj_pon_estilo(struct hj_libro *l, int h, uint32_t f, uint32_t c, uint16_t est);
void  hj_borra_celda(struct hj_libro *l, int h, uint32_t f, uint32_t c);                   /* contenido (no el estilo) */
void  hj_borra_rango(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int tambien_estilo);
uint32_t hj_estilo_id(struct hj_libro *l, const struct hj_estilo *e);                     /* el existente igual, o uno nuevo (0 si no cabe) */
const struct hj_estilo *hj_estilo_de(const struct hj_libro *l, uint16_t id);
void  hj_tipo_numero(const struct hj_estilo *e, uint8_t *nf, uint8_t *dec, uint8_t *miles);   /* el tipo real (resuelve NF_CODIGO) */

/* lo que se edita (formula en castellano con '=', o el valor como se escribiria) y lo que se ve (con el formato de numero) */
void  hj_texto_edicion(const struct hj_libro *l, int h, uint32_t f, uint32_t c, char *out, uint32_t cap);
void  hj_texto_ver(const struct hj_libro *l, int h, uint32_t f, uint32_t c, char *out, uint32_t cap);
void  hj_formatea(const struct hj_libro *l, uint16_t est, const struct hj_valor *v, char *out, uint32_t cap, uint32_t *color);
void  hj_valor_de(const struct hj_libro *l, int h, uint32_t f, uint32_t c, struct hj_valor *v);
const char *hj_nombre_error(int e, int ingles);     /* "#\302\241DIV/0!" o "#DIV/0!" */
int   hj_error_de_texto(const char *s);             /* "#DIV/0!" o "#\302\241DIV/0!" -> HJE_* (0 si no) */

/* ---- columnas, filas, direcciones */
uint16_t hj_ancho(const struct hj_hoja *h, uint32_t c);
uint16_t hj_alto(const struct hj_hoja *h, uint32_t f);
void  hj_pon_ancho(struct hj_hoja *h, uint32_t c, uint16_t px);
void  hj_pon_alto(struct hj_hoja *h, uint32_t f, uint16_t px);
int   hj_fila_oculta(const struct hj_hoja *h, uint32_t f);
void  hj_oculta_fila(struct hj_hoja *h, uint32_t f, int si);
void  hj_nombre_col(uint32_t c, char *out);          /* 0 -> "A", 26 -> "AA" (out >= 4) */
void  hj_dir(uint32_t f, uint32_t c, char *out);     /* "B3" (out >= 12) */
int   hj_lee_dir(const char *s, uint32_t *f, uint32_t *c);   /* "B3", "b3", "$B$3" -> 0; devuelve bytes leidos o -1 */
int   hj_lee_rango(const char *s, uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2);   /* "A1:C9", "A1", "A:A", "3:3" */

/* ---- operaciones */
int   hj_inserta_filas(struct hj_libro *l, int h, uint32_t f, uint32_t n);     /* desplaza celdas y referencias */
int   hj_borra_filas(struct hj_libro *l, int h, uint32_t f, uint32_t n);
int   hj_inserta_cols(struct hj_libro *l, int h, uint32_t c, uint32_t n);
int   hj_borra_cols(struct hj_libro *l, int h, uint32_t c, uint32_t n);
/* copia un rango a otro sitio (formulas relativas ajustadas, como copiar y pegar) */
int   hj_copia(struct hj_libro *l, int hd, uint32_t fd, uint32_t cd, int ho, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int solo_valores);
/* rellena hacia abajo/derecha desde la primera fila/columna del rango (series 1,2,3 / lunes, martes / enero... si hay dos semillas) */
int   hj_rellena(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, int hacia_derecha);
/* ordena las filas f1..f2 (columnas c1..c2) por la columna `por` (y desempate por `por2` si >= 0) */
int   hj_ordena(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, uint32_t por, int desc, int por2, int desc2);
/* autofiltro: oculta las filas del rango (sin la cabecera) cuya columna `col` no cumple `criterio` ("", ">5", "Madrid", "*ana*").
 * criterio vacio = mostrar todas. Devuelve filas visibles. */
int   hj_filtra(struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2, uint32_t col, const char *criterio);
/* buscar: siguiente celda tras (f,c) cuyo texto visto (o formula) contiene `que`; 1 si la hay */
int   hj_busca(const struct hj_libro *l, int h, uint32_t *f, uint32_t *c, const char *que, int en_formulas, int exacta);
int   hj_reemplaza(struct hj_libro *l, int h, const char *que, const char *por, int exacta);   /* cuantas */

/* SOMA_359_HOJA_VENTANA: una foto de un rango (contenido, formulas y estilo) para deshacer: hj_deja lo repone tal cual estaba */
struct hj_trozo;
struct hj_trozo *hj_toma(const struct hj_libro *l, int h, uint32_t f1, uint32_t c1, uint32_t f2, uint32_t c2);   /* 0 = sin memoria */
void  hj_deja(struct hj_libro *l, const struct hj_trozo *t);
void  hj_suelta(struct hj_trozo *t);
uint32_t hj_trozo_bytes(const struct hj_trozo *t);
void  hj_trozo_rect(const struct hj_trozo *t, int *h, uint32_t *f1, uint32_t *c1, uint32_t *f2, uint32_t *c2);

/* SOMA_360_HOJA_GRAFICOS (hoja_graf.c): la forma de los datos de un grafico */
struct hj_gforma { int h; uint32_t r1, c1, r2, c2; uint8_t por_columnas, titulos, categorias; uint32_t nser, ncat; };
int   hj_grafico_forma(const struct hj_libro *l, int hoja_graf, const struct hj_grafico *g, struct hj_gforma *fm);   /* 0 = bien; -1 sin datos */
void  hj_gforma_nombre(const struct hj_gforma *fm, uint32_t serie, uint32_t *f, uint32_t *c);   /* (si titulos) */
void  hj_gforma_valor(const struct hj_gforma *fm, uint32_t serie, uint32_t i, uint32_t *f, uint32_t *c);
void  hj_gforma_cat(const struct hj_gforma *fm, uint32_t i, uint32_t *f, uint32_t *c);           /* (si categorias) */
int   hj_grafico_nuevo(struct hj_libro *l, int h, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2, int tipo);   /* indice o -1 */
void  hj_grafico_borra(struct hj_libro *l, int h, int k);
const char *hj_grafico_tipo_nombre(int t);

/* ---- formulas */
int   hj_recalcula(struct hj_libro *l);              /* todas; devuelve las calculadas */
/* traduce entre castellano (UI) y canonico. 0 = bien; -1 no se entiende (err = posicion aproximada) */
int   hj_form_a_canon(const struct hj_libro *l, int h, const char *ui, char *out, uint32_t cap, int *err);
int   hj_form_a_ui(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap);
/* para los archivos: XLSX (con _xlfn. en las funciones nuevas) y ODS (OpenFormula, ida y vuelta) */
/* una formula de un archivo de Excel a la canonica de SOMA (sin _xlfn., referencias en mayusculas) */
int   hj_form_normaliza(const struct hj_libro *l, int h, const char *f, char *out, uint32_t cap);
int   hj_form_xlsx(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap);
int   hj_form_ods(const struct hj_libro *l, int h, const char *canon, char *out, uint32_t cap);
int   hj_form_de_ods(const struct hj_libro *l, int h, const char *of, char *out, uint32_t cap);
/* desplaza las referencias relativas de una formula canonica (copiar de una celda a otra) */
int   hj_form_desplaza(const struct hj_libro *l, int h, const char *canon, int32_t df, int32_t dc, char *out, uint32_t cap);
int   hj_funcion_existe(const char *nombre);          /* castellano o ingles */
uint32_t hj_lista_funciones(char *out, uint32_t cap, int ingles);   /* "SUMA, PROMEDIO, ..." */
/* el nombre de una funcion del archivo (ingles, con o sin _xlfn.) al de la interfaz y al reves */
const char *hj_func_es(const char *en);
const char *hj_func_en(const char *es);

/* ---- numeros y fechas */
f64   hj_fecha_serie(int y, int m, int d);           /* serie de Excel (1900): 1 = 1-1-1900 */
void  hj_serie_fecha(f64 serie, int *y, int *m, int *d, int *hh, int *mi, int *ss);
int   hj_lee_numero(const char *s, f64 *v, int *nf, int *dec);   /* "1.234,5" "12 %" "3,5 \342\202\254" "3/5/2026" "14:30": 1 si es un numero */
void  hj_num_txt(f64 v, int decimales, int miles, char *out, uint32_t cap);   /* "1.234,50" */

/* ---- CSV y formato propio */
/* CSV: separador ';' (0 = adivinar entre ; , tab), comillas RFC 4180, numeros en castellano o con punto. Escribe en la hoja h. */
int   hj_csv_lee(struct hj_libro *l, int h, const char *txt, uint32_t n, char sep);
uint32_t hj_csv_escribe(const struct hj_libro *l, int h, char sep, char *out, uint32_t cap);   /* valores vistos; 0 si no cabe */
/* "SOMA-HOJA 1": todo el libro (hojas, celdas, estilos, anchos, inmovilizar, filtro, graficos) en texto */
uint32_t hj_serializa(const struct hj_libro *l, char *out, uint32_t cap);   /* bytes o 0 si no cabe */
uint32_t hj_serializa_tam(const struct hj_libro *l);                        /* cota superior */
int   hj_carga(struct hj_libro *l, const char *txt, uint32_t n);            /* 0 = bien (l nuevo); -1 no es una hoja; -2 sin memoria */

/* ---- utilidades de texto para los lectores */
uint32_t hj_slen(const char *s);
int   hj_ieq_pl(const char *a, const char *b);         /* iguales sin mayusculas ni acentos */
char *hj_strdup(const char *s);
char *hj_strndup(const char *s, uint32_t n);
#endif
