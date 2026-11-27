# Parche 363: un solo sitio para los archivos de la persona (Mi unidad y los USB)

## El problema

Cada programa guardaba donde le parecía: el navegador en `/workspace` o en el almacén BIGFS (`big:/imagenes`), Paint y el
Bloc de notas en `/workspace`, la cámara en `/fotos` y `/videos` del almacén, las capturas en `/capturas`, la Grabadora en
`/grabaciones`… Archivos, en cambio, solo enseña **Mi unidad** (`/usuario`). Resultado: «Guardar imagen» decía que había ido
bien, pero la imagen no se podía abrir desde ningún sitio. Y los pendrives que se enchufaban no se veían.

## Lo que cambia

**Todos los programas guardan y abren en la unidad de la persona**, `/usuario`, en las 6 carpetas de serie (Documentos,
Imágenes, Audio, Vídeos, Descargas, Proyectos) y en cuatro subcarpetas que se crean solas la primera vez que hacen falta:
`Imágenes/Fotos`, `Imágenes/Dibujos`, `Imágenes/Capturas` y `Documentos/Hojas de cálculo` (`acceso_carpeta`, `ACC_*`).

| Programa | Antes | Ahora |
|---|---|---|
| Navegador, «Guardar imagen» | `/workspace`, `big:/imagenes` | `/usuario/Imágenes` (y Descargas, Documentos, Mi unidad, los USB) |
| Visor de documentos del navegador | `/workspace/Descargas` | `/usuario/Descargas` |
| Orden `descargar URL` (la persona) | `/workspace` | `/usuario/Descargas` (los agentes siguen en `/workspace`) |
| Paint | `/workspace/dibujo.png` | `/usuario/Imágenes/Dibujos/dibujo.png` |
| Bloc de notas | `/workspace/notas.txt`, lugares `/documentos`, `/workspace`, `/` | `/usuario/Documentos`; lugares: Documentos, Descargas, Mi unidad, USB |
| Maquetación | `/workspace/documento.tex` | `/usuario/Documentos/documento.tex` (imágenes relativas: junto al documento) |
| Programador / editor de código | `/workspace/*.soma` | `/usuario/Proyectos/*.soma` (archivo nuevo: `Proyectos/nuevo.soma`) |
| Cámara | `/fotos`, `/videos` (BIGFS) | `/usuario/Imágenes/Fotos`, `/usuario/Vídeos` |
| Capturas de pantalla, de vídeo y de audio | `/capturas` (BIGFS) | `/usuario/Imágenes/Capturas`, `/usuario/Vídeos`, `/usuario/Audio` |
| Grabadora | `/grabaciones` (BIGFS) | `/usuario/Audio` (la transcripción, junto al audio) |
| Hoja de cálculo | `/usuario/Libro1.xlsx` | `/usuario/Documentos/Hojas de cálculo/Libro1.xlsx`; `hoja abrir gastos.xlsx` busca ahí (y en `/usuario`, para los de antes) |
| Contactos | `/usuario` y `/workspace/Descargas` | `/usuario`, Documentos, Descargas y los USB |

Las grabaciones, fotos y capturas antiguas del almacén BIGFS no se pierden: la Grabadora y Contactos las siguen enseñando.

Si el equipo arranca sin la unidad del usuario (sin pendrive de datos), los programas usan `/tmp` (o, el programador, su
`/workspace`) y la cámara, las capturas y la Grabadora siguen usando el almacén como antes.

## Los medios USB externos: `/usb/<nombre>`

Cualquier pendrive, disco o lector de tarjetas USB que se enchufe aparece en **`/usb/<etiqueta>`** (o `/usb/USB1`, `USB2`…
si no tiene etiqueta; `-2`, `-3` si se repite), en Archivos (sección **USB** de la barra lateral), en los cuadros de abrir y
guardar, en `ls /usb` y en la orden `disco`. Todo su contenido, con todas sus carpetas.

- **exFAT**: lectura y escritura (crear, guardar, renombrar, borrar, carpetas). Cada escritura se manda al pendrive en el acto
  (SYNCHRONIZE CACHE), para que se pueda quitar sin perder nada.
- **FAT12/16/32**: solo lectura (el controlador FAT de SOMA aún no escribe). Archivos lo dice al intentar cambiar algo.
- **NTFS, ext4 y otros**: se ven en `ls /usb` / `disco`, pero no se abren.
- Al enchufar o quitar un disco se vuelve a montar todo solo (hilo `medios`), sin reiniciar.
- **El pendrive del sistema nunca aparece**: se excluye por su número de dispositivo *y* por la firma de su tabla de
  particiones, así que tampoco se cuela si cambia de número tras reconectarse. Nunca se monta dos veces la misma unidad.
- Varios discos USB a la vez (hasta 4): el controlador xHCI ya no se queda con uno solo.

## Seguridad

- La persona (terminal, editores, Archivos) solo escribe en `/usuario`, `/usb` y `/tmp` (`usuario_ruta_escribible`); Archivos
  y los cuadros de abrir no salen de `/usuario` ni de `/usb` (`..` incluido).
- Bajo `/usb` nunca se crean archivos de RAM: si el medio es de solo lectura o ya no está, guardar falla y se avisa.
- Los programas de ring 3 pueden escribir, además de en su `/workspace`, en `/usuario` y `/usb` (antes solo `/workspace`).
- Los agentes mantienen su política (`/workspace`, nada de `/etc`, `/sys`, `/bin`, `/boot`, `/lcm`, `/www`, `/public`).

## Por dentro

- `src/arch/x86_64/xhci.c`: hasta 4 discos de almacenamiento masivo (`xhci_msc_*_n`); el «primario» es el del sistema.
- `src/arch/x86_64/usbdisk.c`: con varios discos, el del sistema es el que tiene SOMADATA (o su marca, o SOMADATA.IMG); tras una
  reconexión lo vuelve a buscar en todos. `usbdisk_disco_sistema()`.
- `src/core/unidades.c`: unidades exFAT externas (`UNID_EXT0`…, `unid_montar_ext`), con las mismas reglas que `/usuario`.
- `src/core/medios.c`: detecta particiones (MBR, GPT o disco sin tabla) y sistemas de archivos, monta exFAT como unidad y FAT
  con `fat.c`, da al VFS el listado de `/usb` y la lectura FAT (`vfs_medios`).
- `src/core/acceso.c`: las carpetas de la persona (`acceso_carpeta`), nombres libres y escritura por trozos (AVI y WAV grandes).
- `src/fs/fs.c`: `/usb` en el VFS y `vfs_read_at` (leer un trozo sin cargar el archivo entero).

## Pruebas

- `tests/test_acceso_363.c` (55 comprobaciones): las carpetas y subcarpetas de cada programa, nombres libres, rutas `/usb/<nombre>`, leer/escribir/renombrar en el USB, que los medios no se
  mezclen, quitar y volver a enchufar, `..`, y las reglas de la persona y de Archivos.
- Para probar en QEMU o en el equipo, con el pendrive del sistema (4 particiones) y dos USB más (exFAT y FAT32): `ls /usb`, leer y escribir en el exFAT,
  leer el FAT32, Archivos con la sección USB, guardar una imagen del navegador en `/usuario/Imágenes`, y que el del sistema no
  aparece.
