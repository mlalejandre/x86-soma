# Parche 340 · Lo que llega de la red local y de USB, con datos hostiles en cada `make test`

**Hace falta `make`** (cambian `dns.c`, `dhcp_proto.c`, `httpd.c` y `uvc.c`). Requiere el 339.

## Qué se buscaba

Es el punto 2 de la lista de prioridades: entradas que no son de fiar y que el núcleo sigue leyendo en ring 0.

Primero se hizo inventario. Mucho ya tenía fuzzer:
* FAT, exFAT, GPT/MBR;
* xHCI, HID y almacenamiento USB;
* WPA, AVI y WAV;
* la pila TCP/IP.

Lo que faltaba era de dos tipos: fuzzers que existían pero **no se ejecutaban en `make test`** (ni, por tanto, en la CI), y
lectores **sin ninguna prueba**.

| Entrada | Quién la controla | Antes | Ahora |
|---|---|---|---|
| Pila TCP/IP (`net.c`, `ip.c`, `udp.c`, `tcp.c`) | cualquiera en la red | fuzzer solo con `make fuzz-net` | 3 000 tramas en cada `make test` (2 s) |
| Respuestas DNS (`dns.c`) | quien conteste por el servidor DNS (WiFi pública) | sin prueba: el análisis estaba dentro del bucle de red | función pura `dns_respuesta_a` (`src/net/dns_proto.h`) y 300 000 respuestas hostiles |
| Ofertas DHCP (`dhcp_proto.c`) | cualquier servidor DHCP de la red | casos a mano | 200 000 ofertas mutadas y la máscara validada |
| Cámara USB (`uvc.c`) | una cámara falsa | casos a mano | 200 000 descriptores, 120 000 paquetes y 100 000 JPEG mutados |
| Red por USB y disco del sistema (`usbnet_proto.c`, `usbdisk_core.h`) | el aparato enchufado | pruebas con mutación que no se ejecutaban | en `make test` |

## Fallos encontrados y arreglados

1. **Cámara USB: desbordamiento de entero con signo** (`uvc_choose`). `fr->w * fr->h` multiplica dos `uint16_t`, que C promueve
   a `int`. Con un tamaño falso (65280 × 51664) se desborda. En el núcleo (compilado con `-fwrapv`) solo elegía mal el tamaño,
   pero es comportamiento indefinido. Ahora se multiplica en `uint32_t`.
2. **Máscara de red falsa por DHCP.** `dhcp_parse` aceptaba cualquier máscara. El panel web (`httpd on`) solo atiende a la red
   local según esa máscara: un servidor DHCP falso que mandara `128.0.0.0` lo abría a media Internet. Ahora:
   * una máscara no contigua o más ancha que /8 pasa a /24, como cuando no llega ninguna (`dhcp_mask_ok`);
   * el panel no atiende a nadie de fuera de la máquina si la máscara es más ancha que /8, venga de donde venga.
3. **La pizarra de los agentes, a toda la WiFi.** `/api/blackboard` servía `/agent/blackboard.txt`, que guarda la última respuesta
   del agente. El panel está apagado por defecto en un equipo real, pero con `httpd on` cualquiera de la misma red la leía.
   * En una máquina virtual se sigue sirviendo (solo la ve el anfitrión).
   * En un equipo real, el campo sale vacío.

DNS, DHCP, la pila TCP/IP y la red por USB no tuvieron fallos de memoria. Las pruebas quedan para que no aparezcan.

## Lo que se revisó y no hacía falta tocar

* **Almacenamiento USB:** sus lectores reciben bloques de tamaño fijo (13, 8 y 36 bytes), y un pendrive con sectores que no son
  de 512 bytes se rechaza.
* **Disco y pendrive:** FAT, exFAT y GPT/MBR ya tenían pruebas con volúmenes y tablas estropeados al azar en `make test`.
* **Cliente TLS, X.509 y HTTP del núcleo (x86_64):** siguen enlazados, pero desde el 327 solo los llama la autoprueba de QEMU que
  los compara con `netsvc`. No les llega nada de la red. Quitarlos del núcleo x86_64 es limpieza (tamaño), no seguridad: queda
  para otro parche.

## Pruebas

* `tests/test_red_hostil_340.c`: DNS (casos a mano, cortes en cada byte, recuentos y longitudes imposibles, 300 000 al azar),
  DHCP (13 máscaras, 200 000 ofertas mutadas) y la regla del panel.
* `tests/test_uvc_hostil_340.c`: reutiliza el constructor de `test_uvc_178.c`. Después de cada descriptor leído, comprueba que
  la elección de formato y de ajuste alterno apunta dentro de lo leído.
* `make test` ejecuta además `test_usbnet_171`, `test_usbdisk_162` y `fuzz_net` con 3 000 iteraciones (`make fuzz-net` sigue
  sirviendo para pasadas largas).
* Todo compila con gcc 14 y clang 18 con `-Werror`, ASan y UBSan.
