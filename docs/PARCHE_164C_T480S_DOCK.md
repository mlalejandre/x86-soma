# Parche 164C · T480s: iconos del dock más baratos y orbe a ~30 fps

Requiere el 164B. Sale de las medidas de `orbe` en el portátil (15/11/2026):

| Medida | Con 1 núcleo | Con 8 núcleos |
|---|---|---|
| Fotograma total | 30,8 ms | 15,4 ms |
| Orbe | 7 fps | 19 fps |
| Zonas y dock | 5,0 ms | 5,0 ms |

Las zonas y el dock eran ya la parte más cara, y no se repartía.

## Qué cambia
- **Iconos del dock** (giran en órbita y se pegan escalados en cada fotograma):
  - tablas de escalado: antes eran dos divisiones por píxel;
  - la misma mezcla alfa, pero en línea (resultado idéntico);
  - todos los iconos de un fotograma se pegan a la vez, por franjas de filas entre los núcleos;
  - los trazos de las partículas y las etiquetas siguen en el núcleo 0.
  - En el emulador, con un solo núcleo: de 4,3 a 2,6 ms.
- **Presupuesto del fondo con varios núcleos**: la mitad del núcleo 0 (antes 1/3). Con un fotograma de unos 12 ms el orbe
  va a unos 30 fps con ventanas abiertas. Con un núcleo, como siempre (1/4).

## En el portátil
Con el terminal abierto y los iconos visibles (`x` en el escritorio), escribe `orbe`. Antes salía «zonas y dock 5,04 ms» y
19 fps.

## Revertir
`python3 parche_164C_t480s_dock.py --undo`.
