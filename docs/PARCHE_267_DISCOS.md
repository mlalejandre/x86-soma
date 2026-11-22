# Parche 267 — El pendrive en cuatro particiones

<!-- SOMA_267_DISCOS -->

## Qué cambia

Antes el pendrive tenía dos particiones FAT32 (`SOMA` para arrancar y `SOMADATA`, el disco de SOMA), más una tercera `DATOS`
FAT32 si se preparaba desde macOS. Al grabar de nuevo se borraba todo (Linux) o había que acordarse de no formatear (macOS).
Ahora:

| # | Formato | Etiqueta | Tamaño | Contenido | Al actualizar |
|---|---|---|---|---|---|
| 1 | FAT32 | `SOMA` | 1 GiB | el sistema: `EFI/BOOT/BOOTX64.EFI`, `LEEME.TXT`, `INFORME.TXT` | se rehace (se conserva `INFORME.TXT`) |
| 2 | SOMA (tipo `0xDA`) | `DATOS` | 2 GiB | configuración (`/etc`), memoria (`/mem`), agentes, programas instalados, almacén BIGFS | no se toca |
| 3 | exFAT | `MODELOS` | 16 GiB | modelos de IA: `voz/`, `llm/` | no se toca (se añaden los modelos nuevos de `modelos/`) |
| 4 | exFAT | `USUARIO` | el resto | `Documentos`, `Imágenes`, `Audio`, `Vídeos`, `Descargas`, `Proyectos` | no se toca |

El tamaño de MODELOS se adapta: con más de 24 GiB libres tras DATOS se lleva 16 GiB y USUARIO el resto (al menos 8 GiB);
con menos, se reparten a medias (cada una, como poco, 512 MiB). Un pendrive de 32 GB queda con MODELOS 16 GiB y USUARIO
10,8 GiB; uno de 16 GB, 7 y 7; uno de 8 GB, 2,2 y 2,2. El cálculo está dos veces, en Python (`plan_particiones`) y en C
(`unid_plan`), y las pruebas comprueban que dan lo mismo.

DATOS se queda en el formato propio de SOMA (SOMAFS02 + diario + BIGFS, robusto ante cortes de luz). Windows y macOS no la
montan ni ofrecen formatearla (tipo `0xDA`). Desde fuera se lee y se edita con `tools/datos.py`.

## Grabar el pendrive

`python3 construyeimagen.py --grabar` (Linux y macOS; `ejecutar.py -t t480s-usb` lo llama) mira primero qué hay:

- **Ya es de SOMA (4 particiones):** solo se reescribe la partición 1. Las huellas de DATOS, MODELOS y USUARIO quedan idénticas
  byte a byte (comprobado en la prueba). Los modelos de la carpeta `modelos/` del proyecto que falten se añaden a `MODELOS/voz`.
- **Es de una versión anterior:** se conserva todo y se migra.
  - `SOMADATA` (ya reclamada por SOMA) **se queda en su sitio y con su tamaño** como DATOS: solo cambia el tipo en el MBR y se
    conserva la firma del disco, así que su marca sigue valiendo. Ni un byte de dentro cambia.
  - La partición `DATOS` FAT32 de macOS (los archivos del usuario) se copia a USUARIO (sin la basura `._*`, `.fseventsd`...).
  - Los modelos de `SOMA/MODELOS` pasan a `MODELOS/voz`.
  - `SOMADATA.IMG` (el disco de SOMA en un fichero de la partición SOMA) se copia dentro de la partición DATOS con su marca nueva.
  - `INFORME.TXT` se conserva.
  - Primero se copia al ordenador lo que cambia de sitio y se verifica. Si no cabe en el disco del sistema, se dice cuánto hace
    falta y **no se toca el pendrive** (`--temporal CARPETA` para usar otro disco). Si algo falla a mitad, la copia se queda en
    el ordenador y se dice dónde.
  - Si SOMADATA ocupa casi todo el pendrive y no queda sitio para MODELOS y USUARIO, se avisa (y se sugiere
    `tools/datos.py exportar` + `--formatear`).
- **Cualquier otro pendrive** (o `--formatear`): se prepara de cero tras escribir `BORRAR`.

Windows sigue grabando la imagen con balenaEtcher o Rufus.

### La imagen del repositorio

`construyeimagen.py --carpeta` genera una imagen de **5 GiB + 1 MiB** (SOMA 1 + DATOS 2 + MODELOS 1 + USUARIO 1 GiB) que cabe
en un pendrive de 8 GB; comprimida ocupa menos de 1 MB más lo que ocupe comprimido el BOOTX64.EFI (con uno de prueba de 3 MB, 3,8 MB en total). MODELOS y USUARIO llevan una serie especial: en el primer
arranque desde un pendrive mayor, SOMA las agranda al plan de ese pendrive **si siguen vacías** (solo las carpetas de serie).

El agrandado (`unid_agrandar`) está pensado para cortes de luz: formatea primero USUARIO en su sitio nuevo (que no pisa la
MODELOS vieja), después MODELOS, y escribe el MBR lo último. Si se corta en cualquier momento, el MBR sigue siendo el viejo y el
siguiente arranque lo repite. Solo puede escribir el MBR y desde MODELOS en adelante: nunca en SOMA ni en DATOS (lo impide el
pegamento de `usbdisk.c`, y la prueba aborta si ocurre). `soma.agrandar=off` en GRUB lo desactiva.

## Dentro de SOMA

- `src/core/exfat.c`: exFAT de lectura y escritura (montar con la región de arranque de reserva, listar, leer, crear, reescribir,
  añadir, carpetas, borrar, renombrar/mover, formatear, archivos abiertos). Orden de escritura para cortes: datos → entrada →
  liberar lo viejo. Tabla de mayúsculas propia (`src/core/exfat_upcase.inc`, generada con `tools/exfat_upcase.py`).
- `src/core/unidades.c`: `/usuario` y `/modelos`. Rutas con `.` o `..` se rechazan; `/modelos` es de solo lectura desde el VFS
  (solo los programas del sistema, con `priv`, o desde fuera); no se mueve nada de una unidad a otra.
- `src/arch/x86_64/usbdisk.c`: tras poner DATOS como disco del sistema, agranda si toca y monta las particiones exFAT por su
  etiqueta. Cada unidad tiene E/S acotada a su partición y, como DATOS, deja de escribir si el pendrive cambia.
- `src/fs/fs.c`: `vfs_read`, `vfs_write`, `vfs_delete`, `vfs_size`, `vfs_read_alloc` y los listados van a las unidades cuando
  la ruta empieza por `/usuario` o `/modelos`. Así funcionan `cat`, `write`, `cp`, `mv`, `rm`, `head`, `ls /usuario`, `nano`...
  `mkdir` y `rmdir` también (en `shell_utilidades.c`). La orden `disco` (y el informe) muestran las unidades.
- `src/arch/x86_64/usermode.c`: `SYS_OPEN` abre también `/modelos/...` y `/usuario/...` (solo lectura); si el archivo no está
  en la unidad, se busca en el almacén como siempre.
- `src/arch/x86_64/voz_sys.c`: el modelo de whisper.cpp de `MODELOS/voz` se usa **directamente** (voz.elf lo lee con
  `SYS_OPEN`); `voz instalar` lo dice en vez de copiarlo. Las voces de Piper se siguen copiando al almacén, ahora desde
  `MODELOS/voz` (o desde `SOMA/MODELOS` en un pendrive antiguo).
- En QEMU (disco virtio) no hay unidades: todo sigue como antes.

## DATOS desde fuera: `tools/datos.py`

```
python3 tools/datos.py /dev/sdb listar
python3 tools/datos.py /dev/sdb leer /etc/hostname [destino]
python3 tools/datos.py /dev/sdb escribir /etc/hostname archivo.txt
python3 tools/datos.py /dev/sdb borrar /workspace/viejo.txt
python3 tools/datos.py /dev/sdb exportar CARPETA [--con-secretos]
python3 tools/datos.py /dev/sdb almacen
python3 tools/datos.py /dev/sdb comprobar
python3 tools/datos.py --crudo build/hdd.img listar
```

`tools/somafs.py` implementa el formato exactamente como el núcleo: las ranuras con su CRC y el diario (`slotio_commit`), los
archivos grandes en BIGFS `/.ramfs/NNN` con su cabecera RFS1 y la sustitución atómica (`spill_put_atomic`), los inodos de BIGFS
en un sector con su CRC (datos antes que el inodo) y las mismas reglas de `analyze` al montar. Si encuentra una escritura del
diario a medias, al leer muestra lo que verá el núcleo y al escribir la termina igual que él. `exportar` no copia claves
(`/etc/wifi.conf`, `/etc/llm.conf`, `/etc/secreto/`, `.conf`...) salvo con `--con-secretos`.

## Pruebas y medidas

`make pruebas-discos` (dentro de `make test`; `RAPIDO=1` para la tanda corta, unos 2,5 minutos):

| Prueba | Qué comprueba | Resultado |
|---|---|---|
| `tests/test_exfat_267.c` | en memoria: miles de operaciones al azar contra un modelo (nombres con tildes, emoji, japonés, 255 caracteres), volver a montar, disco lleno, errores, región de arranque dañada, fallos de E/S inyectados, 400 volúmenes estropeados al azar sin escribir nunca fuera | 48/48 (12 semillas) |
| `tests/test_exfat_267.py` | volúmenes de `mkfs.exfat` con clusters de 512 B a 128 KiB: tras las operaciones de SOMA, `fsck.exfat` limpio y libexfat (otra implementación) lee lo mismo; lo de libexfat lo lee SOMA; el exFAT de Python pasa fsck y lo leen C y libexfat | 53/53 |
| `tests/test_soma_disco_267.py` | plan para 4,5 GB … 3 TB; pendrive vacío, actualizar (huellas idénticas), Linux antiguo, macOS antiguo con nombres largos, `SOMADATA.IMG`, sin sitio en el ordenador (no se toca), pendrive ajeno, imagen | 93/93 |
| `tests/test_datos_267.py` | `somafs.py` frente al C real (`bigfs.c`, `slotio.h`, `spillio.h`): lo que escribe uno lo lee el otro, `bigfs_fsck` sin problemas, cortes en el diario y en BIGFS, `datos.py` sobre un pendrive | 20/20 |
| `tests/test_unidades_267.py` | `unid_plan` = `plan_particiones` (1 254 casos), agrandado en 20 GB, no tocar si hay archivos, **corte de luz en cada una de las 140 escrituras** del agrandado, rutas y permisos | 13/13 |

Las pruebas que dependen de herramientas externas (`fsck.exfat`, `fsck.fat`, `pyfatfs`, una segunda implementación en `EXREF=`)
dicen que se saltan si no están; nunca cuentan como bien.

Además se comprobó con mutaciones que las pruebas detectan fallos de verdad (no copiar USUARIO, no mover los modelos, firma del
disco cambiada, CRC en otro sitio, no terminar el diario, no marcar los cluster de BIGFS): todas fallan.

Dos fallos reales que encontraron las pruebas y se corrigieron: la pista de clusters del exFAT no se invalidaba al cambiar la
FAT (archivos ilegibles tras reescribirlos), y un corte mientras se creaban las carpetas de USUARIO en el agrandado dejaba el
trabajo por terminado con carpetas a medias.

## Dos arreglos de las pruebas en macOS

- `tests/stubs_lang.c` declaraba `agent_last_response` de 8 KiB, pero desde el parche 260 `src/agent/agent.h` lo declara de
  384 KiB: `make pruebas-lang` y `make pruebas-cc` no compilaban (por eso el `--comprobar` del 265 decía HAY FALLOS). Ahora el
  tamaño sale de `agent.h`.
- `make browser-boundary-test` (el primer paso de `make test`) no compilaba en macOS: con `-O1` el SDK de Apple convierte
  `memcpy`, `memmove` y `memset` en macros y `src/core/mem.h` los vuelve a declarar. Se compila con `-D_FORTIFY_SOURCE=0`.

## Lo que no hace todavía

- La política de acceso (que dentro de SOMA el usuario solo vea su unidad y que DATOS solo la toquen sus programas) y el
  gestor de archivos sobre `/usuario` van en el parche siguiente. En este, el gestor de archivos sigue mostrando RamFS y el almacén.
- Las rutas de las órdenes del terminal tienen el límite de sus búferes (unos 60 caracteres en `cat`).
- La migración de un pendrive antiguo de menos de 7 GB preparado en macOS (SOMA de 512 MB) puede no tener sitio: se avisa y no
  se toca nada.
