# Parche 177B · T480s: micrófono (margen del DMA, ganancia y diagnóstico en el terminal)

Requiere el 177.

## Por qué
En el portátil (nodo 0x12 = micrófono digital interno, ADC 0x07 ← selector 0x24) el 177 grababa un ruido metálico con eco y casi nada de voz, y `mic nivel` no mostraba el resultado en el terminal.

## Qué cambia
- **Margen detrás de LPIB (1 KiB)**: el DMA de entrada escribe cerca de la posición que informa LPIB; leer pegado a ella colaba muestras viejas de la vuelta anterior del anillo (eco metálico). Ahora solo se lee lo que queda 256 tramas por detrás y lo leído se pone a cero.
- **Ganancia digital por defecto x2** (el micrófono digital llega muy bajo).
- **`mic nivel [s]`, `mic grabar [s] [Hz]` y `mic probar` esperan y devuelven el resultado en el terminal** (antes solo salía por serie), con datos CRUDOS antes de filtrar: pico y media de cada canal y las primeras muestras. Con eso se ve si el códec entrega señal, silencio o basura.
- `mic fuentes` y `mic info` listan las fuentes en el terminal (con el rms de la última prueba).
- `mic nodos` ahora muestra también las ganancias de entrada (`ampin`).
- Prueba nueva: datos viejos junto a LPIB no llegan a la grabación.

## Qué mirar en el portátil
`mic fuentes`, `mic nivel 5` hablando fuerte cerca, `mic probar` y foto. Si el crudo sale todo a 0 el micrófono no entrega nada (falta algo en el códec); si sale ruido sin relación con la voz, es el formato/DMA; si sale señal, ajustamos ganancia.
