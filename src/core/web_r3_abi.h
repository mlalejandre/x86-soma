/* SOMA_202: ABI del NAVEGADOR AISLADO (nucleo <-> motor web en ring 3, src/user/websvc.c).
 *
 * El motor entero (HTML, CSS, JavaScript, maquetacion, pintado, fuentes, decodificadores de imagen, cliente HTTP y TLS) corre en
 * un proceso de ring 3 con su propio espacio de direcciones. No tiene archivos, ni pantalla, ni mas llamadas al sistema que la red
 * (como capacidad), el reloj y las tres de este canal. Un fallo de memoria del motor ya no es un fallo del nucleo: como mucho se
 * cae ese proceso, y el nucleo lo vuelve a lanzar.
 *
 * Toda la comunicacion va por memoria que POSEE EL NUCLEO (regiones prestadas al proceso) y nunca por punteros del proceso:
 *   W3_IN_VA    (solo lectura para el proceso): la orden en curso (struct w3_cmd + datos) y, mas arriba, el archivo pedido.
 *   W3_OUT_VA   (lectura/escritura): el estado de la pagina (struct w3_state) y la zona de respuestas.
 *   W3_SURF_VA  (lectura/escritura, bajo demanda): los pixeles del ultimo fotograma pintado.
 *   W3_HEAP_VA  (lectura/escritura, bajo demanda): el monton del motor.
 * El nucleo lee esas regiones por sus marcos fisicos, valida cada tamano y termina a la fuerza cada cadena: nada de lo que
 * escribe el proceso se cree sin comprobar. */
#ifndef SOMA_WEB_R3_ABI_H
#define SOMA_WEB_R3_ABI_H
#include <stdint.h>
#include "sbx_abi.h"

#define W3_IN_VA      SBX_IN_VA
#define W3_OUT_VA     SBX_OUT_VA
#define W3_HEAP_VA    SBX_HEAP_VA
#define W3_SURF_VA    0x8040000000ULL            /* USER_BASE + 1 GiB */
/* SOMA_341_NAVEGADOR_MEMORIA: el monton del navegador era el de cualquier servicio aislado (192 MiB) y el motor lo tomaba por toda
 * la memoria del equipo: 24 MiB para imagenes y "sin memoria para mas imagenes" en los periodicos aunque QEMU tuviera 4 GiB.
 * Ahora el nucleo le da la mitad de la memoria libre al arrancarlo, entre 192 MiB y 768 MiB (lo que cabe hasta W3_SURF_VA), y se
 * lo dice en W3C_INIT a[4] (MiB). El monton es bajo demanda: solo ocupa lo que se toca. */
#define W3_HEAP_MAX   (768u << 20)
#define W3_HEAP_MIN   (192u << 20)
static inline uint32_t w3_heap_bytes(uint64_t free_bytes)
{
    uint64_t h = free_bytes / 2;
    if (h < W3_HEAP_MIN) h = W3_HEAP_MIN;
    if (h > W3_HEAP_MAX) h = W3_HEAP_MAX;
    return (uint32_t)(h & ~((uint64_t)(1u << 20) - 1u));             /* MiB enteros */
}
#define W3_MAX_W      3840
#define W3_MAX_H      2160
#define W3_SURF_MAX   ((uint64_t)W3_MAX_W * W3_MAX_H * 4u)
#define W3_CMD_DATA_MAX (256u * 1024u)           /* datos de una orden (URL, texto, historial) */
#define W3_FILE_OFF   (1u << 20)                 /* el archivo pedido con SYS_WEB_FS se deja en W3_IN_VA + W3_FILE_OFF */
#define W3_FILE_MAX   (15u << 20)
#define W3_IN_MAX     (W3_FILE_OFF + W3_FILE_MAX)
#define W3_OUT_PAGES  64u                        /* 256 KiB: estado + respuestas */
#define W3_REPLY_MAX  (160u * 1024u)
#define W3_MAGIC      0x33523357u                /* "W3R3" */
/* SOMA_203 */
#define W3_FRAME_MAX_H (W3_MAX_H * 2)            /* un fotograma puede llevar margen por arriba y por abajo de la vista */
#define W3_BLOB_MAX   (32u << 20)                /* pixeles de una imagen pedida por el usuario (Guardar/Copiar imagen) */
#define W3_SAVE_MAX   (5u << 20)                 /* cookies + almacenamiento web que el nucleo guarda por si el proceso se cae */
#define W3_SAVE_MAGIC 0x56533357u                /* "W3SV" */

/* Llamadas al sistema del canal (solo el proceso del navegador las tiene en su filtro) */
#define SYS_WEB_WAIT  27   /* wait(ms) -> 1 hay una orden nueva en W3_IN_VA | 0 se acabo la espera | 2 piden parar la carga (sin consumir nada) */
#define SYS_WEB_POST  28   /* post(que, a, b): W3P_STATE estado actualizado | W3P_FRAME fotograma listo (a = ancho, b = alto) */
#define SYS_WEB_FS    29   /* fs(0): lee el archivo cuya ruta esta en w3_state.fs_path -> longitud (datos en W3_IN_VA + W3_FILE_OFF) | <0
                            * fs(1) (SOMA_203): devuelve igual la ultima copia de cookies y almacenamiento entregada con W3P_SAVE */
#define USER_SYS_WEB_MASK ((1u << SYS_WEB_WAIT) | (1u << SYS_WEB_POST) | (1u << SYS_WEB_FS))
enum { W3P_STATE = 0, W3P_FRAME = 1, W3P_SAVE = 2 /* SOMA_203: a = direccion en el monton, b = longitud */ };
#define W3F_FIXED 1u         /* frame_flags: la pagina tiene elementos fijos o pegajosos: el fotograma no se puede mostrar corrido */

enum {                     /* ordenes nucleo -> motor (w3_cmd.op) */
    W3C_INIT = 1,          /* a = {vw, vh, verificar_tls, ip, monton_MiB}; u64 = hora Unix (0 = desconocida); seed = 32 bytes al azar */
    W3C_NAVIGATE,          /* a = {flags WPN_*}; datos = URL */
    W3C_HISTORY,           /* a = {delta} */
    W3C_VIEWPORT,          /* a = {vw, vh} */
    W3C_SCROLL,            /* a = {sx, sy} posicion absoluta */
    W3C_MOVE,              /* a = {x, y} en coordenadas de documento */
    W3C_CLICK,             /* a = {x, y, boton, mods, doble} */
    W3C_KEY,               /* a = {tecla, mods}; datos = UTF-8 */
    W3C_SELECT,            /* a = {indice} elige en el <select> desplegado; -1 = cerrar */
    W3C_EVAL,              /* datos = codigo JS */
    W3C_FIND,              /* datos = texto */
    W3C_ALERT_ACK,
    W3C_INNER_SCROLL,      /* a = {x, y, dy} rueda sobre una caja con desplazamiento propio */
    W3C_GET_TEXT,          /* datos = id del elemento -> respuesta: su texto */
    W3C_SET_TEXT,          /* datos = id \0 texto */
    W3C_GET_HIST,          /* respuesta: struct wp_hist[WP_HIST] */
    W3C_SET_HIST,          /* a = {hn, hpos}; datos = struct wp_hist[WP_HIST] */
    W3C_HIT,               /* a = {x, y} -> respuesta: href \0 src de imagen \0 (menu contextual) */
    W3C_GET_IMG,           /* SOMA_203: a = {1 = con pixels}; datos = src -> respuesta: struct w3_img_reply (+ pixels via blob_va/blob_len) */
    W3C_CRASH,             /* solo pruebas (`panic web`): a = {0 puntero nulo | 1 instruccion no valida | 2 bucle sin fin | 3 agotar la memoria} */
};

struct w3_cmd {            /* al principio de W3_IN_VA; los datos van en W3_IN_VA + 256 */
    uint32_t magic, seq, op, len;
    int32_t  a[8];
    uint64_t u64;
    uint8_t  seed[32];
    uint8_t  pad[256 - 88];
};
_Static_assert(sizeof(struct w3_cmd) == 256, "w3_cmd");
_Static_assert(W3_HEAP_VA + W3_HEAP_MAX <= W3_SURF_VA, "el monton del navegador no puede pisar su superficie");

#define W3_DD_MAX 96
struct w3_state {          /* al principio de W3_OUT_VA */
    uint32_t magic;
    uint32_t seq_done;                 /* ultima orden terminada */
    uint32_t frame_seq;                /* fotogramas publicados */
    int32_t  frame_w, frame_h, frame_sx, frame_sy;
    uint32_t frame_flags, paint_ms;    /* SOMA_203: W3F_*; lo que tardo en pintarse el ultimo fotograma */
    int32_t  phase, status_kind, progress;
    int32_t  vw, vh, sx, sy, doc_h;
    int32_t  hn, hpos, can_back, can_fwd;
    int32_t  cursor;                   /* WPK_* bajo el ultimo W3C_MOVE */
    int32_t  focus_text;               /* hay un campo de texto con el foco */
    int32_t  alert_pending;
    int32_t  scroll_req;               /* SOMA_203: suma acumulada de los desplazamientos pedidos por el motor */
    int32_t  dd_open, dd_x, dd_y, dd_w, dd_h, dd_count, dd_cur;
    int32_t  img_ok, img_fail, nimg;
    int32_t  reply_status;             /* de la ultima orden con respuesta */
    uint32_t reply_len;
    uint32_t log_level;                /* aviso para el nucleo (wp_log): 0 = ninguno */
    uint32_t heap_used;
    int32_t  inner_scroll;             /* SOMA_203: la pagina tiene cajas con desplazamiento propio (la rueda se decide en el motor) */
    uint32_t blob_len; uint64_t blob_va; /* SOMA_203: datos grandes de la ultima respuesta, dentro del monton del proceso */
    char     url[4096];
    char     title[160];
    char     status[112];
    char     hover_href[2048];
    char     alert_msg[200];
    char     log_msg[200];
    char     fs_path[512];             /* ruta pedida con SYS_WEB_FS */
    char     dd_opt[W3_DD_MAX][64];    /* textos del <select> desplegado */
    uint8_t  reply[W3_REPLY_MAX];
};
struct w3_img_reply { int32_t ok, w, h, pad; char abs[2048]; };          /* SOMA_203 */
_Static_assert(sizeof(struct w3_state) <= W3_OUT_PAGES * 4096u, "w3_state no cabe en la region de salida");
#endif
