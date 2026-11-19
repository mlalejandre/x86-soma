# Parche 177C · T480s: micrófono a 48 kHz con diezmado y reproducción a 48 kHz

Requiere el 177B.

## Por qué
En el portátil `mic nivel 5` contaba 14 893 ms de audio en 5 s reales (el triple): el ADC del ALC257 ignoraba el divisor del formato y entregaba 48 kHz, que se etiquetaban como 16 kHz. Resultado: grabación 3 veces más larga, grave, metálica y ralentizada.

## Qué cambia
- La captura se hace **siempre a 48 kHz** y el motor la diezma por software (media por periodo) a la frecuencia pedida (8, 16, 22,05, 44,1, 48 kHz).
- La reproducción se abre **siempre a 48 kHz** y se interpola linealmente desde la frecuencia grabada, así no depende de que el DAC acepte otras frecuencias.
- Pruebas: 2 s grabados a 16 kHz desde un flujo de 48 kHz dan 64 000 bytes; reproducirlos escribe 2 s de salida a 48 kHz.
