# Parche 189 — Navegador: recolector de basura, velocidad del motor JavaScript y memoria de imágenes

Requiere el parche 188B. Responde a lo visto tras el 188B en QEMU con 4 GiB: el sistema iba lento, el vigilante avisaba
de que `web_worker` no cedía la CPU y los periódicos se quedaban "sin memoria para más imágenes".

## Qué pasaba

1. **El vigilante (`Ningún hilo cedió la CPU en 5 s`)**: las tres trazas del registro están dentro del analizador de
   JavaScript (`parse_stmt`, `parse_primary`, `parse_template`…). Con el 188B los scripts grandes ya se descargan y se
   analizan enteros, y el analizador no cedía nunca la CPU: en un equipo lento (QEMU sin aceleración) analizar megas de
   código tarda segundos y la interfaz se quedaba congelada. Ahora cede cada 4 ms, igual que la ejecución.
2. **"sin memoria para más imágenes"**: el modo de "memoria grande" pedía 2 GiB de montón justos y un QEMU de 4 GiB tiene
   2047 MiB: se quedaba con 20 MiB para imágenes, que además se expulsaban y volvían a decodificarse una y otra vez (de
   ahí la lentitud). Ahora el modo empieza en 960 MiB de montón y la memoria de imágenes es 1/8 del montón (20–192 MiB).
3. **Memoria y tiempo del motor**: el intérprete no liberaba nada hasta cerrar la página y tenía tres búsquedas lineales
   que, con el código real de Google, se llevaban el 60 % del tiempo.

## Qué cambia

### Recolector de basura (`src/apps/web_js_gc.c`)
Marcado y barrido conservador, sin mover objetos. Los bloques pequeños salen de trozos de 64 KiB por tamaño con un mapa
de bits de marcas; los grandes se piden uno a uno. Raíces: la pila de C en curso, las pilas de las corrutinas
suspendidas, la pila de ámbitos y la estructura del intérprete; cualquier palabra que apunte dentro de un bloque lo
mantiene vivo, así que el código del motor no necesita registrar nada. El texto de las cadenas no se recorre. Los
envoltorios de los nodos del DOM se apuntan en una lista dentro del montón (duran lo que la página). Recoge cuando se ha
reservado tanto como lo que quedó vivo la última vez (mínimo 8 MiB) o al acercarse al tope, y cede la CPU mientras marca.
Interruptor: `soma.jsgc=off` (el montón vuelve a crecer sin liberar).

| Página real (motor en el anfitrión) | 188B | 189 |
|---|---|---|
| Portada de Google: memoria JS | 260 MiB | 161 MiB |
| Portada de Google: tiempo de carga | 3,5 s | 1,4 s |
| YouTube: memoria JS | más de 3,9 GiB (se agotaba) | 330 MiB |

### Velocidad
- **Variables**: buscar un nombre recorría todas las variables del ámbito; la función que envuelve un script grande tiene
  miles. Los ámbitos de más de 24 variables llevan un índice por nombre. El número de variables por ámbito pasa de 16 a
  32 bits (con más de 65 535 nombres en un ámbito el contador se desbordaba).
- **`Map`/`Set`**: `get`/`has`/`set` eran una búsqueda lineal; con más de 16 entradas hay un índice de dispersión.
- **Cadenas con acentos o emojis**: `charCodeAt`/`slice` en posiciones no consecutivas recorrían la cadena desde el
  principio; ahora hay puntos de control cada 256 unidades para las últimas cadenas largas.

## Límites
- YouTube ya cabe en memoria y su aplicación arranca, pero sigue sin pintar la portada: falla al construir `ytd-app`
  (su inyector de dependencias no encuentra un proveedor). Es un fallo de compatibilidad por depurar, ya no de memoria.
- El motor sigue siendo un intérprete de árbol: en QEMU sin aceleración (un Mac con procesador ARM emulando x86) una
  página con megas de JavaScript tarda decenas de segundos; en el T480s real, mucho menos.
- El árbol sintáctico de los scripts cargados no se compacta: es la mayor parte de los 161 MiB de Google.

## Verificación
- `tests/test_web_js_gc_189.c` (nuevo): 8 casos a tres ritmos de recolección con ASan; `tests/test_web_js_188.c`: 151.
- Corpus de 160 páginas: mismas métricas con el recolector normal y forzado cada 4000 reservas; con ASan y recolector
  forzado, sin errores (también con cuarentena de lo liberado). TodoMVC interactivo sin cambios.
- Google real: aviso, escritura y envío, en el motor (ASan) y en QEMU. Página de tortura con generadores, `async`,
  temporizadores, `Map` y DOM en QEMU (pilas de corrutina reales).
- Núcleo x86 y ARM con `WERROR=1`; `__kernel_image_end` dentro del límite.
- Con `detect_stack_use_after_return=1` ASan saca las variables locales de la pila real; en ese modo de prueba el
  recolector no actúa (las pruebas del recolector usan `=0`).
