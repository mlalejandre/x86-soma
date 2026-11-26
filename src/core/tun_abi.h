/* SOMA_326_TUNEL_TLS: el tunel TLS de ring 3 (src/user/tlstun.c) y lo que el nucleo comparte con el (src/apps/tun_tls.c).
 * Una conexion por proceso. El proceso hace el TCP, el TLS 1.3 (apreton, certificados, cifrado) y STARTTLS; el nucleo solo le pasa
 * y recibe TEXTO EN CLARO por dos anillos en una region compartida. Asi el correo (IMAP y SMTP) y YouTube nativo dejan de analizar
 * TLS y X.509 en ring 0 (el HTTP, IMAP y SMTP siguen en el nucleo: son puros y con pruebas hostiles).
 *
 * Region compartida en TUN_SH_VA (lectura y escritura para los dos): pagina 0 = struct tun_ctl; despues el anillo de salida
 * (nucleo -> proceso, TUN_RING bytes) y el de entrada (proceso -> nucleo, TUN_RING bytes). Los contadores tx_w, tx_r, rx_w y rx_r cuentan bytes
 * (mod 2^32): pendiente = w - r. Escribe w quien produce y r quien consume. */
#ifndef SOMA_TUN_ABI_H
#define SOMA_TUN_ABI_H
#include <stdint.h>

#define TUN_SH_VA     0x8020000000ULL
#define TUN_RING      65536u
#define TUN_TX_OFF    4096u
#define TUN_RX_OFF    (4096u + TUN_RING)
#define TUN_SH_PAGES  (1u + 2u * (TUN_RING / 4096u))
#define TUN_MAGIC     0x4E555454u          /* "TTUN" */
#define TUN_VERSION   1u
#define TUN_HEAP      (4u << 20)
#define TUN_ANCLAS_MAX (16u * 1024u)

enum { TUN_CMD_NADA = 0, TUN_CMD_STARTTLS = 1, TUN_CMD_CIERRA = 2 };
enum { TUN_E_CONECTANDO = 0, TUN_E_LISTO = 1, TUN_E_CERRADO = -1, TUN_E_ERROR = -2 };

struct tun_ctl {
    uint32_t magic, version;
    volatile uint32_t cmd, cmd_seq, cmd_hecho;   /* el nucleo pone cmd y sube cmd_seq; el proceso responde con cmd_hecho = cmd_seq */
    volatile int32_t  cmd_res;                   /* 0 bien, < 0 fallo (de la ultima orden) */
    volatile int32_t  estado;                    /* TUN_E_*: lo escribe el proceso */
    volatile uint32_t tx_w, tx_r, rx_w, rx_r;
    char sni[256];                               /* para STARTTLS: lo escribe el nucleo antes de la orden */
    char err[160];                               /* el motivo de un fallo: lo escribe el proceso */
};

/* entrada del proceso (datos de sbx_in): esta cabecera y detras las autoridades del usuario (PEM, anclas_len bytes) */
struct tun_req {
    uint8_t  ip[4];
    uint16_t port, tls;                          /* tls: 1 = TLS desde el principio; 0 = en claro (STARTTLS despues) */
    uint32_t ms_conecta;                         /* plazo de la conexion y del apreton */
    uint32_t verifica;                           /* 1 = comprobar el certificado (soma.tlsverificar) */
    uint32_t anclas_len;
    uint64_t unix_time;                          /* la hora, para las fechas de los certificados (0 = no se comprueban) */
    uint8_t  semilla[32];                        /* azar del nucleo para TLS */
    char     sni[256];
};
#endif
