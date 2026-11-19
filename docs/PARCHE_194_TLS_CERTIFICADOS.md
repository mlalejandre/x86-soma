# Parche 194 · TLS: validación de certificados

Requiere el 193. Probado en el host (vectores de referencia, raíces reales, servidores OpenSSL, fuzzing) y arrancando en QEMU
(x86_64) contra servidores TLS locales; aarch64 compila. **No probado contra sitios reales de internet**: aquí no hay salida.

## Por qué
El cliente TLS 1.3 propio (parche 176A) cifraba pero no comprobaba con quién hablaba: cualquiera en la misma red podía hacerse
pasar por cualquier sitio. Era el candado ámbar del navegador.

## Qué comprueba ahora cada conexión (`src/core/x509.c`)
1. La cadena de certificados llega, firma a firma, a una autoridad de confianza.
2. El certificado es de ese sitio (`subjectAltName`, comodines de una sola etiqueta, direcciones IP) y está en fecha.
3. El servidor tiene la clave privada del certificado (firma `CertificateVerify` del saludo).

Si algo falla, la conexión **no se establece** y la página de error (y la orden `tls <url>`) dicen el motivo.

## Autoridades de confianza
- **De fábrica:** 117 raíces de la lista de Mozilla, dentro del núcleo (`src/core/tls_roots.c`, 54 KiB). Se regenera con
  `python3 tools/mk_tls_roots.py cacert.pem` (por ejemplo el de https://curl.se/ca/cacert.pem). Conviene actualizarla de vez en cuando.
- **Del usuario:** `/etc/tls_confianza.pem`, uno o varios certificados PEM (hasta 8). Sirve para un servidor propio con certificado
  autofirmado. Un agente no puede escribir en `/etc`.

## Algoritmos
RSA de 2048 a 4096 bits (PKCS#1 v1.5 en certificados, PSS en el saludo) y ECDSA P-256 y P-384, con SHA-256/384/512.
Se rechazan SHA-1, RSA de menos de 2048 bits y los certificados sin `subjectAltName`.

## Órdenes e interruptores
- `tls estado`: verificación activa o no, número de autoridades, estado del reloj.
- `tls <url>`: prueba una conexión y dice por qué falla un certificado.
- `soma.tlsverificar=off` (o el modo seguro): vuelve al cifrado sin verificar (candado ámbar). Solo para diagnóstico.
- Candado verde en el navegador = certificado verificado.

## Límites
- **Sin revocación** (CRL/OCSP) ni restricciones de nombres (`nameConstraints`). Sin Ed25519 ni P-521. Sigue sin TLS 1.2.
- **Reloj:** si el reloj de tiempo real no está puesto (año < 2025) no se comprueban las fechas (`tls estado` lo avisa). Si está
  mal puesto pero es verosímil, los certificados pueden salir como caducados.
- **Cadenas de más de 24 KiB** o de más de 8 certificados se rechazan.
- El análisis de certificados corre en ring 0. Mitigación: cada longitud se valida y el código está sometido a fuzzing.
- En QEMU (red 10.0.2.x) el navegador sigue usando la pasarela del anfitrión para https; el TLS propio se usa en una red real.
- **El canal con el LLM sigue en HTTP plano.** Este parche pone la pieza que faltaba (autenticar un servidor por su certificado,
  también autofirmado); falta que `llm.c` hable HTTPS con un extremo configurable.

## Pruebas
- `tests/test_x509_194.c` (en `make test`): 36 cadenas y 29 firmas de saludo generadas con la biblioteca `cryptography` de Python
  (`tests/x509_vectors_gen.py`): RSA 2048/3072/4096, P-256, P-384, mezclas, comodines, IP, caducados, raíz ajena, intermedia sin
  permiso de autoridad, firmas alteradas, clave que no es la del certificado. Más fuzzing de los analizadores.
- Fuzzing largo: 400 000 mensajes mutados con ASan/UBSan, sin errores y sin aceptar ninguno para otro sitio.
- Raíces reales: la autofirma de 114 de las 118 raíces de Mozilla se verifica con este código (las otras 4 usan algoritmos no admitidos).
- Cliente TLS real contra `openssl s_server` (RSA, P-256, P-384): sitio correcto y comodín aceptados; otro sitio, autoridad ajena y
  cadena sin intermedia rechazados; autofirmado aceptado solo si el usuario lo añade.
- QEMU con HTTPS directo forzado: lo mismo desde dentro de SOMA con `tls https://10.0.2.2:…` y `/etc/tls_confianza.pem`.
- `usfuzz`, autopruebas `sbx` y `user`: siguen bien.

## Revertir
`python3 parche_194_tls_certificados.py --revertir`
