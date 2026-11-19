# Parche 175A · T480s: WiFi Intel 8265, fase A (el firmware arranca en la tarjeta)

Requiere el 171B. Primera de tres fases de la ficha 175:
- **A (esta):** firmware y arranque de la tarjeta;
- **B:** dirección MAC y búsqueda de redes;
- **C:** conexión WPA2 y DHCP.

## Qué hay que descargar (lo hace el parche)
Un solo archivo: **`iwlwifi-8265-36.ucode`** (unos 2,5 MB), el firmware oficial de Intel para la tarjeta del T480s (Intel
Dual Band Wireless-AC 8265). Es parte del paquete `linux-firmware`, el que usan todas las distribuciones de Linux, y su
licencia permite redistribuirlo; se guarda también `LICENCE.iwlwifi_firmware`.

Al aplicar el parche, `tools/firmware_wifi.py`:
1. lo descarga de **kernel.org**; si falla, de la réplica oficial de **GitLab** y de una de **GitHub**;
2. **comprueba** que es un firmware Intel válido (marca `IWL`, bloques TLV completos, imágenes INIT y RUNTIME), y no
   por una huella fija, porque hay dos revisiones válidas del mismo archivo;
3. lo deja en **`firmware/`** con su `sha256`.

Al compilar `make t480s` (o `ejecutar.py -t t480s-usb`), el firmware entra en la imagen del pendrive (`/boot/firmware/`) y
**GRUB lo carga en memoria** junto a SOMA como módulo Multiboot2 (`module2`). No hay que copiar nada al pendrive a mano.

Sin internet: `python3 tools/firmware_wifi.py --archivo RUTA/iwlwifi-8265-36.ucode` (por ejemplo, copiado de
`/lib/firmware` de cualquier Linux). `--comprobar` revisa el instalado.

## Qué cambia en SOMA
- **Módulos de GRUB**: `bootfb.c` lee las etiquetas Multiboot2 de tipo 3 (inicio, fin y nombre), y el gestor de memoria
  **reserva** su memoria. En QEMU con UEFI el módulo cae en los 33 MiB, fuera de la zona protegida, y sigue intacto.
- **`iwl_fw.c`**: lee el formato TLV de iwlwifi: versión, imágenes INIT, RUNTIME y WoWLAN, separadores CPU1/CPU2 y de
  paginación, PHY_SKU, paginación y capacidades.
- **`iwl.c`**, controlador de la tarjeta (PCI 8086:24FD), siguiendo la secuencia de `iwlwifi` de Linux:
  1. preparar la tarjeta (`NIC_READY`), reiniciarla y arrancar sus relojes (APM);
  2. configurar la radio según el firmware y preparar el anillo de recepción (256 descriptores, 64 buffers de 4 KiB);
  3. cargar la imagen **INIT** por el canal de servicio del FH en trozos de 128 KiB, primero la CPU1 y luego la CPU2,
     marcando cada sección en `FH_UCODE_LOAD_STATUS`;
  4. esperar la notificación **ALIVE** (`0xCAFE`), que dice que el firmware corre en la tarjeta.
  Sin interrupciones (se sondea `CSR_INT` y la interrupción INTx de la tarjeta se desactiva). Arranca en segundo plano al
  iniciar SOMA (hilo `wifi`) y escribe una línea `WIFI: ...`.
- **Orden `wifi`**: tarjeta, firmware, fase alcanzada, versiones del firmware, tiempo de carga y, si falla, en qué etapa y
  con qué registros. `wifi arrancar` repite el arranque.
- Interruptor: `soma.wifi=off` (y el modo seguro).
- De paso: en ARM no enlazaba por la función de informe de discos USB del 171B (se compila dos veces); se renombra en la
  segunda copia, como las demás.

## Pruebas
- `tests/test_iwl_fw_175.c` (anfitrión):
  - con el firmware real: `release/core33::9f0a2d68`, INIT de 13 secciones (CPU1 0–4, CPU2 6–9, unos 660 KB a cargar),
    RUNTIME de 12 y paginación de 184 KiB;
  - además, un firmware sintético, cortes en cualquier punto y 200 000 entradas al azar con ASan.
- QEMU con UEFI y con BIOS: GRUB carga el módulo, SOMA lo encuentra y lo valida. QEMU no tiene esta tarjeta, así que la
  carga y el ALIVE **solo se pueden ver en el portátil**.

## En el portátil
1. Al arrancar debe salir `WIFI: Intel 8265: firmware release/core33... ARRANCADO (ALIVE 0x0000CAFE, ...)`.
2. `wifi` da el detalle. **Mándame la foto en cualquier caso**: si falla, dice en qué etapa y con qué registros, y con eso
   preparo la fase B.
