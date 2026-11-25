# Parche 310 · El tamaño del núcleo, vigilado en la CI

Solo cambian una herramienta, el `Makefile`, una prueba y la CI. **No hace falta `make`.**

## El problema
La imagen del núcleo de x86_64 (código, datos y `.bss`) tiene que acabar antes de los 46 MiB que fija `linker_x86_64.ld`: lo que hay
detrás es de la virtqueue y de la zona baja del PMM. Si un parche la hace crecer de más, el enlazador falla **de golpe** con un
`ASSERT`, sin avisar antes, y sin decir qué es lo que ocupa.

## Qué cambia
* `tools/size_check.py` (nuevo, Python puro: lee el ELF él mismo, sin `nm`, y funciona igual en el Mac):
  * cuánto ocupa cada sección, y qué porcentaje del límite se usa (el límite lo lee del propio `linker_x86_64.ld`);
  * los objetos más grandes, **incluidos los programas de ring 3 incrustados** en el núcleo, que no tienen tamaño en la tabla
    de símbolos: se calcula de su `_start` a su `_end`;
  * avisa desde el 85 % y falla desde el umbral que se le pida.
* `make size-check` (con `SIZE_FALLAR=90` falla desde el 90 %). Funciona en el Mac sin el compilador cruzado, sobre una imagen
  hecha con Docker.
* La CI de GitHub lo ejecuta después de compilar x86_64, con `SIZE_FALLAR=90`.
* `tests/test_size_check_310.py` (en `make test`): construye ELF sintéticos y comprueba OK, aviso, fallo, símbolo que falta,
  `ASSERT` que falta y archivos rotos.

## Cómo está hoy (tras los parches 305-309)
```
fin de la imagen: 0x01FA6740 de 0x02E00000 permitidos -> 68.8 % (31.7 MiB de 46.0 MiB; quedan 14.3 MiB)
     4594 KiB  lcm_ordenes_blob
     2429 KiB  vplay.elf (programa incrustado)
     2208 KiB  websvc.elf (programa incrustado)
     1597 KiB  voz.elf (programa incrustado)
      833 KiB  jsrun.elf (programa incrustado)
      577 KiB  cc.elf (programa incrustado)
```
Solo los pesos del LCM y los programas de ring 3 incrustados ya suman más de 12 MiB. Son los primeros candidatos para pasar al
disco (BIGFS) y cargarlos cuando hagan falta. Así se ganaría memoria y margen sin quitar ninguna función.
