# Parche 172 · T480s: audio real (altavoces y auriculares)

Requiere el 168B. Ficha 172 del plan. `hda.c` se había probado solo con el códec de QEMU. El T480s tiene el controlador
HD Audio del chipset Sunrise Point-LP (8086:9D71), el códec **Realtek ALC257** (altavoces y conector de auriculares) y el
códec **HDMI de Intel**.

## Qué cambia en `hda.c`
- **BAR de 64 bits**: la dirección del controlador puede estar por encima de 4 GiB.
- **DMA coherente**: en los chipsets de Intel, como `hda_intel` de Linux:
  - sin «no snoop» (DEVC 0x78, bit 11);
  - TCSEL a 0;
  - el ahorro de reloj MISCBDCGE (CGCTL 0x48, bit 6) se quita durante el reinicio.
  Sin esto el controlador puede leer el sonido sin pasar por la caché y sonaría ruido o silencio.
- **Órdenes al códec por los anillos CORB/RIRB**, como Linux: la interfaz inmediata no es fiable en Skylake y posteriores.
  Si los anillos fallan, se vuelve a la inmediata. Los plazos usan el TSC: se espera con las interrupciones cerradas.
- **Varios códecs**: se usa el primero con salidas analógicas. Los pines HDMI/DisplayPort se descartan.
- **Dos caminos**: **altavoz** (pin fijo) y **auriculares** (pin con conector), cada uno con su DAC si hay dos (en el ALC257:
  altavoz 0x14 → DAC 0x02, auriculares 0x21 → DAC 0x03). El mismo flujo alimenta los dos DAC.
- **Selectores**: se elige la entrada correcta de cada pin y selector (verbo 701). En los mezcladores solo queda abierta la
  entrada del camino. Antes no se tocaba y dependía del valor de fábrica.
- **EAPD** (el amplificador externo de los altavoces) y el amplificador de auriculares activados.
- **Conector de auriculares**: se mira cada 250 ms. Al enchufarlos el altavoz calla y al quitarlos vuelve, con un aviso
  en la consola.
- **Cerrojo** para las órdenes al códec: las usan el audio, las teclas de volumen (168) y el hilo del conector a la vez.
- `audio info` enseña el códec elegido, los dos caminos, el estado del conector y las órdenes sin respuesta.
- Interruptor: `soma.audio=off` (y el modo seguro).

## Pruebas
- QEMU con el sonido grabado a un archivo `.wav` y comparado con el patrón de `audio test`: idéntico bit a bit durante
  1 s con `intel-hda + hda-duplex` (ICH6, máquina pc), `ich9-intel-hda + hda-micro` e `ich9-intel-hda + hda-output` (q35).
- `tests/test_hda_172.c` (anfitrión): un ALC257 simulado con la topología del T480s y el códec HDMI:
  - el códec HDMI se descarta;
  - altavoz 0x14 → 0x02 y auriculares 0x21 → 0x03 (el selector del pin en la entrada 1);
  - EAPD, amplificador de auriculares, volumen y silencio;
  - el conector calla el altavoz y lo devuelve;
  - con un solo DAC, los dos caminos lo comparten.

## En el portátil
1. `audio info`: debe decir `codec 0 (0x10EC0257)`, `altavoz: pin 20 -> DAC 2`, `auriculares: pin 33 -> DAC 3` y
   `ordenes por CORB/RIRB`.
2. `audio tone 440 2000` (un la de 2 s) por los altavoces; después, con auriculares.
3. Fn+F2/F3 cambian el volumen y Fn+F1 silencia (parche 168).
4. Si no suena, mándame la salida de `audio info` y las líneas `hda:` del arranque.
