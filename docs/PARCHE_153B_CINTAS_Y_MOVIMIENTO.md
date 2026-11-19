# Parche 153B — Cintas del orbe más baratas y continuas, movimiento uniforme

Se aplica encima del parche 153.

## Cintas a media resolución

- **Antes:** las cintas se dibujaban a resolución completa, con una muestra cada ~1,9 px a lo largo de la curva. En las
  cintas finas eso dejaba huecos, que aparecían como puntos. Al moverse, los puntos parpadeaban: parte de los "saltos"
  que se veían.
- **Ahora:** las cintas se acumulan en un búfer a media resolución, donde las muestras quedan continuas, y se amplían
  ×2 con interpolación bilineal.
- **Coste de la ampliación:** se hace recorriendo solo los píxeles con contenido. Cada uno reparte su color en una
  "tienda" de 3×3 píxeles, que da el mismo resultado que la bilineal clásica y evita visitar el recuadro vacío del
  centro.
- **El cálculo de cada muestra no tiene divisiones:** una división por ángulo en lugar de una por muestra, y la posición
  a lo ancho de la cinta se avanza sumando.
- **Aspecto:** mismo brillo medio (8,2 frente a 8,5 sobre 255 en la prueba), cintas continuas y algo más suaves. La
  comparación lado a lado está en `cintas_antes_despues.png`.

## Movimiento uniforme ("sin saltos")

- **Estrellas:** su física avanzaba **un paso por fotograma**. A 4 fps se arrastraban, a 30 fps volaban, y cada fotograma
  lento era un tirón. Ahora avanza en pasos fijos de 33 ms según el tiempo real transcurrido.
- **Rosquilla de partículas:** su giro y su caída dependían del número de fotogramas. Ahora son proporcionales al tiempo
  transcurrido.
- **Reloj de las animaciones del escritorio:** avanza como mucho 70 ms por fotograma (`HER_ANIM_DT_MAX`). Tras una pausa
  (el fondo congelado mientras usas el navegador), el movimiento continúa donde estaba, sin el salto de hasta 100 ms de
  antes.
- **Ritmo del fondo con el navegador delante:** pasa a 15 fps por defecto (`SOMA_GUI_BG_MS=66`, antes 250 ms). Se puede
  cambiar al compilar.

## Medidas (QEMU, comparación alternada en el mismo arranque, `tools/desk_bench.py`)

| Caso | Fotograma del escritorio | Solo las cintas |
|---|---|---|
| Ventana opaca (153 + 153B) | 48,6 → 32,7 Mciclos (−33 %) | 15,9 → 3,4 (−79 %) |
| Ventana translúcida (solo 153B) | 48,8 → 44,2 Mciclos (−9 %) | 14,9 → 10,9 (−27 %) |

Con la ventana translúcida, lo que más pesa del fotograma ya no son las cintas. Pesan el cuerpo de la ventana, la
sombra, el marco y la propia mezcla de la transparencia.

Opciones de compilación:

- `-DSOMA_ORB_RIB_HALF=0`: vuelven las cintas originales.
- `-DHER_ANIM_DT_MAX=100`: vuelve el límite anterior del reloj de animación.
