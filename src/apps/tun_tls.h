/* SOMA_326_TUNEL_TLS: conexiones TCP con TLS 1.3 (o STARTTLS) para el correo y YouTube nativo.
 * En x86_64 cada conexion es un proceso de ring 3 (src/user/tlstun.c): el TLS y los certificados del servidor no se analizan en el
 * nucleo. En aarch64 (sin EL0) se hace aqui mismo, como antes. Ver src/core/tun_abi.h. */
#ifndef SOMA_TUN_TLS_H
#define SOMA_TUN_TLS_H
#include <stdint.h>

struct tun;
/* conecta con ip:port; tls = 1: TLS desde el principio (sni = nombre del servidor). NULL si no se pudo (err explica) */
struct tun *tun_abre(const uint8_t ip[4], uint16_t port, int tls, const char *sni, uint32_t ms, char *err, uint32_t ecap);
int  tun_starttls(struct tun *t, const char *sni, char *err, uint32_t ecap);      /* 0 bien (err explica si no) */
int  tun_envia(struct tun *t, const void *d, uint32_t n);                        /* 0 bien, < 0 error */
int  tun_recibe(struct tun *t, void *b, uint32_t max, uint32_t ms);             /* > 0 bytes, 0 nada aun, -1 cerrado, -2 error */
void tun_cierra(struct tun *t, int sin_despedida);                               /* libera t */
#endif
