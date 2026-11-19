/* SOMA_194 (seguridad): validacion de certificados para el TLS 1.3 propio. Ver x509.c. Sin dependencias del nucleo (se prueba y se
 * somete a fuzzing en el host: tests/test_x509_194.c). */
#ifndef SOMA_X509_H
#define SOMA_X509_H
#include <stdint.h>
#include <stddef.h>

enum {
    X509_OK = 0,
    X509_E_FORMAT,        /* certificado o mensaje mal formado */
    X509_E_ALG,           /* algoritmo de firma o tipo de clave no admitido */
    X509_E_SIG,           /* una firma de la cadena no es valida */
    X509_E_UNTRUSTED,     /* la cadena no llega a ninguna autoridad de confianza */
    X509_E_EXPIRED,       /* un certificado esta caducado o aun no es valido */
    X509_E_HOST,          /* el certificado no es de este sitio */
    X509_E_NOTCA,         /* un certificado intermedio no es de autoridad */
    X509_E_TOOLONG,       /* cadena demasiado larga */
    X509_E_CV             /* la firma del saludo (CertificateVerify) no es valida: el servidor no tiene la clave del certificado */
};
const char *x509_err_name(int e);

struct x509_key { int type; const uint8_t *a; uint32_t alen; const uint8_t *b; uint32_t blen; };   /* RSA: a = n, b = e; EC: a = punto sin comprimir */
enum { X509_K_NONE = 0, X509_K_RSA, X509_K_P256, X509_K_P384 };

/* Autoridades de confianza de fabrica: bloque de entradas [u16 len_nombre][nombre DER][u16 len_spki][SubjectPublicKeyInfo DER]. */
void x509_set_roots(const uint8_t *blob, uint32_t len);
/* Autoridades anadidas por el usuario: un certificado DER (se copian nombre y clave). 0 = hecho. */
int  x509_add_anchor(const uint8_t *cert, uint32_t len);
void x509_clear_anchors(void);
uint32_t x509_root_count(void);
/* Texto PEM (uno o varios "-----BEGIN CERTIFICATE-----"): devuelve cuantos se anadieron. */
int  x509_add_anchors_pem(const char *pem, uint32_t len);

/* Valida la cadena del mensaje Certificate de TLS 1.3 (cuerpo, sin la cabecera de 4 bytes) para `host` en el instante `now` (segundos
 * Unix; 0 = reloj desconocido: no se comprueban las fechas). Si es valida deja en *leaf la clave del servidor (apunta dentro de msg). */
int x509_verify_tls_chain(const uint8_t *msg, uint32_t len, const char *host, uint64_t now, struct x509_key *leaf);
/* Comprueba el CertificateVerify de TLS 1.3 (cuerpo: esquema u16, longitud u16, firma) sobre la huella de 32 bytes del saludo. */
int x509_verify_tls_cv(const struct x509_key *leaf, const uint8_t *cv, uint32_t cvlen, const uint8_t transcript_hash[32]);

/* Piezas sueltas (pruebas) */
void sha512(const void *p, size_t n, uint8_t out[64]);
void sha384(const void *p, size_t n, uint8_t out[48]);
int  x509_host_match(const char *pattern, uint32_t plen, const char *host);
#endif
