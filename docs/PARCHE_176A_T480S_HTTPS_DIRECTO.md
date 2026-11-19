# Parche 176A · T480s: HTTPS directo (TLS 1.3 propio) en el navegador

Requiere el 175C7.

## Por qué
Con la WiFi ya funcionando (175C7), las páginas `https://` seguían fallando: SOMA no tenía TLS y las pasaba por una pasarela del equipo anfitrión (`10.0.2.2:8096`, `tools/web_gateway.py`) que en el portátil no existe. `http://` ya iba directo.

## Qué trae
- **Cliente TLS 1.3 propio** (`src/core/tls13.c`), solo `TLS_AES_128_GCM_SHA256` + X25519 (los obligatorios en TLS 1.3: todo servidor 1.3 los admite). Handshake completo, comprobación del `Finished` del servidor, `KeyUpdate`, `close_notify`, alertas.
- **Criptografía** (`src/core/tlscrypto.c`): SHA-256, HMAC, HKDF, X25519, AES-128-GCM (sobre el AES de `crypto.c`) y aleatorios (RDRAND + contador de ciclos).
- **Integración en el navegador** (`web_net.c`): en una red real (la IP no es 10.0.2.x) las páginas https van directas, con gzip/chunked/redirecciones/cookies como las http. En QEMU (10.0.2.x) sigue la pasarela, sin cambios. Interruptor: `soma.httpsdirecto=off` fuerza la pasarela.
- **Orden `tls <url>`**: prueba el HTTPS directo desde el terminal (estado HTTP, tipo, bytes, error TLS concreto si falla). Corre en segundo plano.
- **Candado ámbar** en la barra de direcciones cuando la conexión es directa: cifrada, **pero el certificado no está verificado**. Los mensajes de error de TLS salen en la página de error.

## Seguridad: lo que NO hace todavía
No valida el certificado del servidor (cadena, nombre, caducidad): SOMA no lleva almacén de autoridades de certificación. El tráfico va cifrado y no se puede alterar sin que se note, pero alguien en tu misma red podría hacerse pasar por el sitio. **No pongas contraseñas reales** hasta que se añada la verificación (parche aparte: raíces de confianza + RSA/ECDSA + X.509). Tampoco hay TLS 1.2: un sitio que solo lo admita dará «el servidor no admite TLS 1.3» (casi todos los grandes ya admiten 1.3).

## Pruebas
- `tests/test_tls_176.c` (en `make test`): SHA-256, HMAC, X25519 (RFC 7748) y AES-GCM contra vectores de python-cryptography.
- `tests/tls_live_176.c` (manual): cliente real contra `openssl s_server` (ECDSA y RSA, 4 MB, KeyUpdate) y contra un servidor de internet; también dentro de QEMU con la orden `tls`.

## En el portátil
Conecta la WiFi y prueba el navegador con `https://www.20minutos.es`. Si falla: `tls https://www.20minutos.es/` en el terminal y foto.
