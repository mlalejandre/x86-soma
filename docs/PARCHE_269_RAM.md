# PARCHE 269 · Modo RAM: el sistema trabaja en memoria y el pendrive solo recibe lo que se guarda

## Qué había (y por qué no basta con «copiar SISTEMA a RAM»)

El núcleo viaja dentro de `BOOTX64.EFI` y el firmware lo copia a memoria al arrancar; después la partición SISTEMA no se
vuelve a leer. Los archivos de DATOS (`/etc`, `/mem`, `/agent`…) ya viven en RAM (RamFS) y se guardaban en el pendrive en
**cada** escritura. Solo se leía el pendrive a demanda: las fuentes (`srcfs`, ~2,6 MiB), `/modelos` y `/usuario`.

## Qué hace el modo RAM

Se activa con la opción de arranque **`soma.ram=1`** (por defecto NO está activo: SOMA arranca como siempre).

* **Fuentes en memoria:** `srcfs_a_ram()` copia el índice y los datos empaquetados a RAM una vez; después no se lee el pendrive.
* **Guardado diferido de DATOS:** las escrituras, altas y borrados de RamFS quedan marcados como pendientes y no tocan el
  pendrive. Se guardan al **apagar o reiniciar**, con **`disco sync`**, o con cualquier `vfs_sync()`.
* `disco` muestra «MODO RAM: activo, archivos sin guardar: N». `disco sync` guarda los pendientes y vacía la caché.
* `/tmp` sigue siendo temporal; **`/usuario` y `/modelos` no cambian** (son del pendrive y no caben en RAM).

**Riesgo:** un corte de luz o quitar el pendrive antes de apagar pierde lo cambiado desde el último guardado.

## Cómo probarlo

Sin tocar GRUB: en el menú de arranque pulsa `e`, añade ` soma.ram=1` al final de la línea `multiboot2` y pulsa F10 (o Ctrl+X).
En SOMA: `disco` debe decir «MODO RAM: activo». Escribe algo (`write /usuario/...` no cuenta: usa Ajustes o un archivo de
`/tmp`/`/mem`), mira que «sin guardar» sube y que `disco sync` lo baja a 0.

## Pruebas

* `tests/test_fs.c`: con el modo activo ni un sector se escribe hasta `vfs_sync()`; después se guarda todo; sin pendientes no escribe; al
  desactivarlo vuelve el guardado inmediato.
* `tests/test_srcfs.c`: con `srcfs_a_ram()` todos los archivos coinciden y no se lee ni un sector del disco.

## Pendiente

La entrada propia del menú de GRUB («SOMA - modo RAM») va en un parche aparte: necesita la carpeta `grub/` del proyecto.
