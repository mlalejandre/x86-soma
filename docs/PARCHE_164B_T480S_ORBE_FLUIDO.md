# Parche 164B · T480s: el orbe fluido con ventanas abiertas

Requiere el 164. Responde a «el orbe se ralentiza cuando hay otra ventana delante».

## La causa (medida)
Con ventanas abiertas, cada fotograma del escritorio se compone entero en el núcleo 0. Medido en el emulador a
2560×1440, con el terminal abierto:

| Parte | Coste |
|---|---|
| Fondo y orbe | 70 % (de ello, el halo y las cintas el 75 %, y el borrado de la pantalla el 22 %) |
| Volcado a pantalla con la mezcla translúcida | 20 % |
| Marcos y contenido | 10 % |

Además, el parche 155D (de cuando SOMA usaba un solo núcleo en el T480s) frenaba el orbe a propósito para que el ratón no
fuera a saltos:
- usaba como mucho 1/4 de la CPU (entre 4 y 30 fps);
- **se paraba 150 ms cada vez que movías el ratón o tecleabas**.

## Qué cambia
- **Halo y cintas del orbe repartidos entre los núcleos**:
  - el halo se reparte por franjas de filas;
  - cada cinta se pinta a la vez en su propio búfer de media resolución;
  - la ampliación ×2 se reparte por franjas de filas de pantalla, y cada núcleo escribe solo las suyas.
  - Las sumas son saturadas y positivas, así que el orden no cambia nada: **el dibujo es idéntico píxel a píxel** al de un
    núcleo.
- **Borrado de la pantalla entera** repartido entre los núcleos.
- **Mezcla de la ventana translúcida** (en el volcado) repartida por filas.
- **Freno del 155D más suave con varios núcleos**: presupuesto de 1/3 de la CPU (antes 1/4) y, mientras escribes o
  mueves el ratón, el orbe sigue a unos 15 fps en vez de pararse. Con un núcleo (`cpus 1`, `soma.smp=off`) todo queda
  como antes.
- **Orden `orbe`**: cuánto cuesta cada parte del fotograma (media de 60) y a cuántos fps va el orbe. Sirve para medir en
  el portátil. `orbe prueba` dibuja el halo y las cintas con 1 núcleo y con todos, en 4 posturas, y comprueba que salen
  iguales.

## Pruebas realizadas
- `orbe prueba`: idéntico píxel a píxel con 1 y con 8 núcleos, en 4 posturas de las cintas.
- Captura de pantalla con el terminal abierto sobre el orbe: sin costuras entre franjas.
- Con el terminal abierto, el orbe pasa de 10 a 23 fps en el emulador solo por el cambio de criterio.
- La aceleración por el reparto **no se puede medir aquí**: el servidor de pruebas tiene 2 núcleos reales para emular 8.
  En el T480s hay 4 núcleos físicos (8 hilos).
- Raspberry Pi 5: compila. El borrado y la mezcla repartidos también la aprovechan.

## En el portátil
1. Abre el terminal y escribe `orbe`: foto. Después `cpus 1`, espera unos segundos, `orbe` otra vez (foto), y
   `cpus todos`. La diferencia entre las dos fotos es lo que gana el reparto.
2. `orbe prueba`: debe decir «idénticos».
3. Prueba a escribir con el orbe detrás: ya no debe pararse.

## Revertir
`python3 parche_164B_t480s_orbe_fluido.py --undo` y `make clean`.
