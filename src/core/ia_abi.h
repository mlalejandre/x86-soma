/* SOMA_329_IA_LOCAL: el motor de IA local (src/user/ia/, llama.cpp + ggml con AVX2 en ring 3) y lo que el nucleo comparte con el
 * (src/arch/x86_64/ia_sys.c). Un solo proceso que vive mientras se usa: carga los modelos GGUF del almacen una vez y atiende
 * ordenes. El nucleo no hace coma flotante: le pasa texto y recibe texto (JSON como el de un servidor compatible con OpenAI), y
 * el resto del sistema (memoria, chat, agente) lo trata igual que la respuesta de un servidor.
 *
 * Region compartida en IA_SH_VA (lectura y escritura para los dos): pagina 0 = struct ia_ctl; despues la entrada (nucleo ->
 * proceso, IA_IN_MAX bytes) y la salida (proceso -> nucleo, IA_OUT_MAX bytes). El monton del proceso es la region grande bajo
 * demanda de USER_HEAP_VA (como la voz). */
#ifndef SOMA_IA_ABI_H
#define SOMA_IA_ABI_H
#include <stdint.h>

#define IA_SH_VA      0x8030000000ULL
#define IA_IN_OFF     4096u
#define IA_IN_MAX     (1024u * 1024u)
#define IA_OUT_OFF    (IA_IN_OFF + IA_IN_MAX)
#define IA_OUT_MAX    (1024u * 1024u)
#define IA_SH_PAGES   ((IA_OUT_OFF + IA_OUT_MAX) / 4096u)
#define IA_MAGIC      0x4C434F4Cu          /* "LOCL" */
#define IA_VERSION    1u

/* ordenes */
enum {
    IA_CMD_NADA = 0,
    IA_CMD_EMBED = 1,        /* entrada: texto UTF-8. salida: {"data":[{"embedding":[...]}]} (vector normalizado) */
    IA_CMD_CHAT = 2,         /* entrada: cuerpo de /v1/chat/completions. salida: respuesta de /v1/chat/completions */
    IA_CMD_TOKENIZA = 3,     /* entrada: texto. salida: los numeros de los tokens separados por espacios (pruebas) */
    IA_CMD_DESCARGA = 4,     /* olvida los modelos cargados (libera su memoria) */
    IA_CMD_SALIR = 5,
    IA_CMD_HUELLA = 6        /* huella del archivo del modelo leido desde ring 3 (pruebas) */
};
/* estado del proceso */
enum { IA_E_ARRANCANDO = 0, IA_E_LISTO = 1, IA_E_OCUPADO = 2, IA_E_FIN = -1 };

struct ia_ctl {
    uint32_t magic, version;
    volatile uint32_t cmd, cmd_seq, cmd_hecho;   /* el nucleo pone cmd y sube cmd_seq; el proceso responde con cmd_hecho = cmd_seq */
    volatile int32_t  cmd_res;                   /* 0 bien; < 0 fallo (motivo en err) */
    volatile int32_t  estado;                    /* IA_E_*: lo escribe el proceso */
    volatile uint32_t cancela;                   /* el nucleo lo pone a 1 para cortar una generacion */
    volatile uint32_t in_len, out_len;           /* out_len crece mientras se genera (texto parcial en parcial_*) */
    volatile uint32_t parcial_len;               /* SOMA_329: respuesta parcial (solo el texto) mientras se genera */
    volatile uint32_t tokens_entrada, tokens_salida, ms_carga, ms_proceso;   /* medidas de la ultima orden */
    uint32_t hilos;                              /* hilos de calculo (0 = los que diga el proceso) */
    volatile uint32_t hilos_usados;              /* los que usa de verdad (lo escribe el proceso) */
    uint32_t contexto;                           /* tokens de contexto del chat (0 = 4096) */
    uint64_t monton;                             /* bytes del monton (region en USER_HEAP_VA) */
    char modelo[160];                            /* ruta del GGUF en el almacen para la orden */
    char err[200];                               /* el motivo de un fallo: lo escribe el proceso */
};
#define IA_PARCIAL_OFF (IA_OUT_OFF + IA_OUT_MAX - 256u * 1024u)   /* los ultimos 256 KiB de la salida: el texto parcial */
#define IA_PARCIAL_MAX (256u * 1024u)
#endif
