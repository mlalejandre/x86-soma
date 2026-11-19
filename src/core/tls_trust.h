/* SOMA_194: confianza del TLS del nucleo (pegamento con el reloj, el VFS y las opciones de arranque). Ver tls_trust.c. */
#ifndef SOMA_TLS_TRUST_H
#define SOMA_TLS_TRUST_H
#include <stdint.h>
struct tls_trust_info { int verify; uint32_t roots; int user; uint64_t now; };
void tls_trust_setup(void);                       /* antes de cada conexion TLS (barato) */
void tls_trust_get_info(struct tls_trust_info *i);
#endif
