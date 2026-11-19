# Parche 191 · Seguridad, grupo 1

Cuatro correcciones cortas de la auditoria de seguridad. Ninguna cambia el uso normal del sistema.

## 1. El navegador ya no deja leer archivos locales a las paginas web (`src/apps/web_page.c`)
- **Antes:** `load_res()` aceptaba `file:` y rutas `/...` viniera de donde viniera la peticion. El JavaScript de cualquier web podia hacer
  `fetch("file:///etc/wifi.conf")` (o historial, conversaciones, memoria del agente) y enviarlo fuera. Con `history.pushState` una pagina
  podia ponerse la URL de otro sitio y leer sus cookies y su `localStorage`.
- **Ahora:** los subrecursos pedidos por el contenido (scripts, estilos, imagenes, fetch/XHR) pasan por `load_sub()`: un recurso local
  solo lo carga un documento local (`file:`, `about:` o la pagina inicial). Una pagina web tampoco navega a `file:` (enlaces,
  `location`, formularios); la barra de direcciones y el historial siguen pudiendo. `pushState`/`replaceState` no salen del origen.
- **No cubre:** peticiones entre sitios distintos (falta CORS) ni el aislamiento del motor (sigue en ring 0).

## 2. `SFMASK` limpia AC y NT (`src/arch/x86_64/usermode.c`)
- `0x700` -> `0x47700`. Ring 3 puede poner AC con `popf`; sin limpiarlo, toda la syscall corria con SMAP anulado. NT puesto puede dar
  #GP en un `iretq` del kernel.

## 3. Singularity Loop con confirmacion por defecto (`ejecutar.py`, `protected.txt`)
- Los parches que propone la IA muestran el diff y piden `y` **siempre**. `--sin-aprobar` recupera el modo desatendido. Sin terminal
  (CI, tuberias) el parche se rechaza.
- `protected.txt` anade la frontera de seguridad: modo usuario, sandbox, politica del agente, shell, TLS, WPA, red, servidor HTTP.
  **Efecto:** la IA ya no puede proponer cambios en `agent.c` ni `shell.c` (ahi vive su propia politica); si quieres que pueda, quita esas lineas.

## 4. ARP (`src/net/net.c`)
- Entrada nueva: solo de la respuesta a una peticion propia en curso o de una peticion dirigida a nuestra IP.
- Entrada existente: no se cambia al vuelo. Se pregunta por unicast a la MAC antigua; si contesta en `ARP_CHAL_MS` (1 s) el cambio se
  descarta como suplantacion; si no, se acepta.
- Lo no pedido no desaloja a la puerta de enlace de la cache. La orden `arp` muestra los contadores.
- **No cubre:** quien gane la carrera a la PRIMERA respuesta legitima (limite del propio ARP). La defensa de fondo es validar los
  certificados TLS.

## Pruebas
- `tests/test_arp_191.c` (host, ASan/UBSan): no pedido ignorado, respuesta pedida aceptada, suplantacion rechazada, cambio real
  aceptado, la puerta de enlace no se desaloja. Compilar y ejecutar:

      cc -std=gnu11 -O1 -g -w -fsanitize=address,undefined -DARP_CHAL_MS=30 -Itests -Itests/host_inc -Isrc -Isrc/core -Isrc/drivers \
         -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 \
         tests/test_arp_191.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/tests/test_arp_191 && ./build/tests/test_arp_191

- Pendiente de probar arrancando (no se ha podido aqui): `usfuzz` y `sbx-check` con el nuevo SFMASK, y el navegador con
  `file:///workspace/demo_web.html` (debe seguir cargando sus imagenes) y con una web real.

## Revertir
`python3 parche_191_seguridad_grupo1.py --revertir`
