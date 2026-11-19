# Parche 177 · T480s: micrófono y aplicación Grabadora

Requiere el 176A (y la cadena anterior).

## Qué trae
- **Captura HDA** (`hda.c`): busca en el códec (ALC257) todas las fuentes de entrada: recorre cada ADC → selectores/mezcladores → pines con capacidad de entrada y conexión física. Las ordena (micrófono interno primero, luego el del conector, luego línea). Configura el camino elegido (selector, pin con polarización 80 %/50 %, "boost" del pin o ganancia del ADC), usa un flujo DMA de entrada propio (etiqueta 2, anillo de 128 KiB) y **apaga el pin cuando no se captura**.
- **Motor de grabación** (`micrec.c`): vacía el anillo cada 5 ms, elige el canal con señal (el otro puede venir mudo), quita el nivel continuo, aplica ganancia digital (x1–x16), mide nivel/pico/saturación/pérdidas, y guarda mono de 16 bits en RAM (hasta 64 MiB: unos 35 min a 16 kHz, 11 min a 48 kHz). Detecta si el DMA no avanza y lo avisa. Escuchar por la salida de audio, guardar/cargar/borrar WAV en `/grabaciones` del almacén BIGFS.
- **Aplicación Grabadora** (icono GRABAR en el dock; orden `grabadora`): GRABAR/PARAR, ESCUCHAR, NUEVA, GUARDAR, NIVEL (medidor sin grabar), selector de fuente `< >` y AUTO (prueba todas y elige la de más señal), ganancia del códec y digital, calidad (8, 16, 22, 44,1, 48 kHz), medidor, onda en vivo, lista de grabaciones. Teclas: espacio, P, S, N, L, Esc.
- **Orden `mic`** (diagnóstico): `mic info|fuentes|fuente N|ganancia N|sw N|nivel [s]|grabar [s] [Hz]|parar|reproducir|guardar [ruta]|cargar ruta|borrar [ruta]|probar|nodos|verbo nid verbo payload`.
- Interruptores: `soma.audio=off` (todo el audio) y `soma.mic=off` (solo el micrófono).

## Para guardar
El almacén BIGFS debe estar formateado (`big format yes`; solo toca la región de BIGFS del pendrive, nunca el NVMe interno). Sin él, se puede grabar y escuchar, pero no guardar.

## Si no se oye nada en el portátil
1. `mic info` y `mic fuentes`: ¿cuántas fuentes?
2. `mic nivel 5` hablando; si el resultado dice rms 0, `mic probar` (habla) y `mic fuentes` muestra el rms de cada una.
3. `mic nodos` y foto: es el árbol del códec; con él se ajusta el parche.
