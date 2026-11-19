#ifndef SOMA_TLS13_H
#define SOMA_TLS13_H
/* SOMA_176A: cliente TLS 1.3 (RFC 8446) bloqueante, solo TLS_AES_128_GCM_SHA256 + X25519 (los obligatorios: todo servidor TLS 1.3 los admite).
 * Entrada/salida por callbacks (en SOMA, el TCP de la pila; en el host, un socket). NO verifica el certificado del servidor (ver tls13.c):
 * cifra y comprueba que el servidor domina la clave de sesion (Finished), pero no que sea quien dice ser. */
#include <stdint.h>
#include <stddef.h>

#define TLS_IO_EOF (-2)
#define TLS_IO_ERR (-1)
#define TLS_MAX_REC 16640

struct tls_io {
    void *ctx;
    int (*send)(void *ctx, const uint8_t *d, uint32_t n);                     /* 0 = bien, <0 error; debe enviarlo entero */
    int (*recv)(void *ctx, uint8_t *d, uint32_t max, uint32_t timeout_ms);    /* >0 bytes, 0 = nada aun, TLS_IO_EOF, TLS_IO_ERR */
    uint64_t (*now_ms)(void);
    void (*yield)(void);                                                      /* opcional */
};
#define TLS_CERT_MAX 24576    /* SOMA_194: cadena de certificados mas larga que se acepta */
#define TLS_CV_MAX   640
struct tls13 {
    struct tls_io io;
    uint8_t ck[16], civ[12], sk[16], siv[12]; uint64_t cseq, sseq;           /* claves de trafico actuales (cliente / servidor) */
    uint8_t cts[32], sts[32];                                                 /* secretos de trafico (para KeyUpdate) */
    uint8_t rec[TLS_MAX_REC + 5]; uint32_t rec_n;                              /* registro en curso */
    uint8_t plain[TLS_MAX_REC]; uint32_t plain_n, plain_off;                  /* datos de aplicacion pendientes de leer */
    int state;                                                                /* 0 sin conectar, 1 listo, 2 cerrado por el servidor, -1 error */
    int err; int alert;                                                       /* codigo de error propio y alerta TLS recibida (0 si no) */
    int cert_verified;                                                        /* 1: cadena, nombre, fechas y firma del saludo comprobados (SOMA_194) */
    uint8_t certmsg[TLS_CERT_MAX]; uint32_t certmsg_n;                         /* SOMA_194: mensaje Certificate del servidor */
    uint8_t cvmsg[TLS_CV_MAX]; uint32_t cvmsg_n;                               /* SOMA_194: mensaje CertificateVerify */
    uint32_t cert_len, cert_n; 
    uint8_t out[TLS_MAX_REC + 64];
};
enum { TLS_E_IO = 1, TLS_E_ALERT, TLS_E_VERSION, TLS_E_HELLO, TLS_E_DECRYPT, TLS_E_FINISHED, TLS_E_PROTO, TLS_E_TIMEOUT, TLS_E_CERTREQ, TLS_E_SIZE,
       TLS_E_CERT = 32 };                                                     /* SOMA_194: 32 + codigo X509_E_* (x509.h): el certificado no es valido */
/* SOMA_194: politica de verificacion. 1 (por defecto) = una conexion cuyo certificado no valida FALLA. 0 = como antes del parche 194
 * (cifrado sin comprobar con quien se habla): solo para diagnostico (soma.tlsverificar=off). */
extern int tls13_verify;
extern uint64_t (*tls13_unix_time)(void);                                     /* hora actual (segundos Unix) o 0 si el reloj no es fiable; 0 = sin funcion */
const char *tls13_err_name(int e);
/* handshake; 0 = bien. sni = nombre del servidor (puede ser NULL) */
int tls13_connect(struct tls13 *t, const struct tls_io *io, const char *sni, uint32_t timeout_ms);
int tls13_send(struct tls13 *t, const void *d, uint32_t n);                  /* 0 = bien, <0 error */
/* >0 bytes, 0 = nada todavia (timeout), TLS_IO_EOF = el servidor cerro, TLS_IO_ERR = error */
int tls13_recv(struct tls13 *t, void *buf, uint32_t max, uint32_t timeout_ms);
void tls13_close(struct tls13 *t);                                           /* envia close_notify */
#endif
