# PARCHE 284 · Listo para publicar en GitHub

* `CHANGELOG.md`: entradas de los parches 277 a 284 (de ahí salen las notas de la release que crea `subir_github.py`).
* `README.md`: sección «Memoria de SOMA» y la aplicación Memoria en la lista de aplicaciones.
* `.gitignore`: `memoria-export/` y `*.smem` no se suben (llevan tu memoria personal).
* `tests/test_exporta_280.py`: las claves falsas de la prueba se arman al ejecutarla; escritas enteras, el detector de secretos de
  `subir_github.py` las tomaba por reales y no dejaba subir.
* Los documentos de los parches 281 y 282 (que no los traían), aquí debajo.

Los cambios en `CHANGELOG.md`, `README.md` y `.gitignore` se aplican si encuentran su sitio; si tu copia es distinta, el parche avisa y
sigue con lo demás (y `--revertir` deja todo como estaba).

---

## PARCHE 281 · Las pruebas de la memoria pasan en macOS

Las pruebas de la memoria (277, 279, 280 y el pegamento) se ejecutaban con `ASAN_OPTIONS=detect_leaks=1`. LeakSanitizer no existe en
macOS, así que allí abortaban antes de empezar. Ahora usan `detect_leaks=0`, como el resto de pruebas del proyecto. Solo cambia el `Makefile`.

## PARCHE 282 · Zona baja reservada de 48 MiB: la imagen del núcleo vuelve a enlazar

El núcleo ya ocupaba unos 29,3 MiB en secciones (código, datos y bss) y `linker_x86_64.ld` exigía que la imagen acabara antes de 30 MiB
para que la virtqueue cupiera en la zona baja reservada (32 MiB). La memoria nueva solo aportaba unos 0,1 MiB: fue la gota que colmó el vaso.

* `src/arch/x86_64/pmm_core.h`: `PMMC_LOWER` de 32 a 48 MiB (como el parche 202 la subió de 16 a 32).
* `linker_x86_64.ld`: imagen ≤ 46 MiB (`0x02E00000`) y virtqueue ≤ 48 MiB (`0x03000000`).
* Margen: unos 16 MiB. La RAM libre empieza 16 MiB más arriba; nada más cambia (el arranque proyecta el primer GiB con páginas de 2 MiB).
* Pruebas del PMM (`test_pmm_157`, `test_pmm_scan`) sin cambios. Arrancado en QEMU por el usuario tras aplicarlo.
