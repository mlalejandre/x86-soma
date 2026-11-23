# Parche 276 · Ajuste fino del LCM (SOMA_276_LCM)

Requiere el 275. Cambia SOLO herramientas y datos de entrenamiento; el modelo de fabrica del nucleo NO cambia (ninguno de los intentos paso la puerta, ver `lcm/modelos/informe_276.txt`).

- `traduce.entrena(..., desde=RUTA.pt, lr=...)`: parte de un modelo ya entrenado. Conserva los ids de las palabras conocidas, añade las nuevas al final, y copia las filas de cada orden por NOMBRE (las ordenes nuevas empiezan al azar). Lo hace `_hereda`.
- `python3 -m tools.lcm_ordenes.reentrena --desde a.pt,b.pt [--lr 8e-4] --semillas 11,29,41,53 --n 150000 --epocas 5`: un .pt de partida por semilla; con 4 semillas se prueban los 6 pares y se queda el mejor que pase la puerta.
- `lcm/manifiestos/cc.json`: 14 construcciones mas de cc (sin copiar las frases retenidas de prueba).
- `tests/test_lcm_ajuste_276.py` (en `make pruebas-lcm`): la herencia de pesos es exacta (palabras, LSTM, etiquetas y ordenes por nombre).

## Resultado honesto
Desde cero (A) el total bajaba 82,4 -> 80,0 % y los examenes 1 y 2 hasta -11 puntos. Con ajuste fino el total sube (83-84 %), html y js salen al 95-100 %, agenda 91-94 % y cc llega al 74 % (minimo 75 %). Siguen sin cumplirse: el examen 2 (-5,5 puntos), unos pocos errores de agenda ejecutados sin preguntar y cc por un punto. Por la regla de siempre (sin regresion no se aplica) no se instala.
Los .pt de fabrica (`t258c.pt`, `t258d.pt`) se identifican porque su exportacion coincide byte a byte con `src/agent/lcm_ordenes_pesos.inc`; no estan en el arbol (7,8 MB cada uno).
