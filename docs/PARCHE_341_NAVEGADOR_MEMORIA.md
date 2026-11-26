# Parche 341 · El navegador ya no se queda sin memoria con las páginas llenas de fotos

**Hace falta `make`.** Requiere el 340.

## El síntoma

En QEMU, en la portada de un periódico (20minutos):

```
[web] img KO: https://imagenes.20minutos.es/files/image_640_360/... h=200 n=42061 sin memoria para mas imagenes
```

Fallaban decenas de fotos, también las pequeñas (240×240), y no volvían a aparecer aunque se bajara hasta ellas.

## Las tres causas

1. **El navegador creía estar en un equipo pequeño.** Desde que el motor va en ring 3 (`websvc`), `kheap_max_bytes()` devuelve
   el montón del proceso, y ese montón era el de cualquier servicio aislado: 192 MiB fijos. El reparto se hacía sobre eso: 1/8
   para imágenes (24 MiB) y «memoria pequeña» (pide 960 MiB). Daba igual que QEMU tuviera 4 GiB o el T480s 16.
2. **Las fotos se guardaban a tamaño original.** Con el ancho puesto por CSS y el alto automático (lo habitual en las portadas), la
   caja es provisional hasta que llega la foto (`size_dep`). En ese caso la imagen se guarda entera para no pixelarla: 990×556
   son 2,2 MB aunque se vea a 240 px. Nadie la reducía después.
3. **Lo que no cabía se perdía para siempre.** Se marcaba como fallida (`state = 3`), así que, aunque se bajara hasta ella y se
   liberaran las lejanas, no volvía a intentarse.

## Qué cambia

* **Montón según la memoria del equipo** (`src/core/web_r3_abi.h`, `web_r3.c`, `websvc.c`):
  * al abrir el navegador, el núcleo le concede la mitad de la memoria libre, entre 192 MiB (lo de antes) y 768 MiB (lo que
    cabe hasta la superficie de pintado);
  * se lo dice en `W3C_INIT` (`a[4]`, en MiB);
  * el montón sigue siendo bajo demanda (solo ocupa lo que se toca) y la reserva del sistema sigue protegida: el proceso no
    crece si el sistema bajaría de su mínimo.
* **Reparto pensado para el proceso del navegador** (`web_util.h`, solo en `websvc`). Ese montón es entero del navegador:
  * imágenes, 1/4;
  * JavaScript, 1/3;
  * «memoria grande» desde 384 MiB.

  En el núcleo (arm64, sin aislar) las cuentas no cambian.
* **Reducir tras maquetar** (`img_scan`). Si una imagen ya cargada se guarda al doble o más de lo que se ve, se reduce. Nunca
  queda por debajo de lo que se ve, así que no pierde nitidez. Recibe otra clave en la caché de imágenes escaladas.
* **Aplazar en vez de fallar** (`img_aplaza`). Una imagen que no cabe, o que no hay memoria para decodificar, y que está lejos
  de la vista vuelve a pendiente:
  * se carga al acercarse;
  * antes de la primera maquetación no se reintenta;
  * tiene un tope de 4 intentos, para no descargarla una y otra vez.
* **`webr3 <url>`.** Dice cuánto montón se concedió y espera hasta 90 s a que la página quede quieta. Sale en cuanto lo está; antes
  cortaba a los 6 s.

## Medido en QEMU

Una portada de prueba con 120 fotos de 990×556 en una rejilla, con el ancho por CSS (`tests/prueba_navegador_341.py`):

| | Imágenes | Montón usado |
|---|---|---|
| Antes (4 GiB) | 15 bien, 105 «sin memoria para más imágenes» | 40 MiB de 192 |
| Solo con más montón | 95 bien, 25 fallan | 216 MiB de 768 |
| Con todo el parche (4 GiB) | **120 bien** | **52 MiB** de 768 |
| Con todo el parche (512 MB) | **120 bien** | **52 MiB** de 221 |

En una página larga con las fotos en columna se siguen cargando solo las cercanas a la vista (28 de 120), como antes.

## Pruebas

* `tests/test_navegador_memoria_341.c` (en `make test`): lo que concede `w3_heap_bytes` con 0 a 4 GiB libres. Siempre entre 192
  y 768 MiB, nunca más de la mitad de lo libre (salvo el mínimo de siempre) y sin pisar la superficie.
* `tests/prueba_navegador_341.py` (QEMU): la portada de arriba con 4 GiB y con 512 MB.
