# Parche 331 — Herramientas de disco y aplicación «Discos»

## Qué hace
- **Aplicación Discos** (icono DISCOS en el escritorio, orden `discos`): a la izquierda, una tarjeta por disco
  (nombre, tamaño, controlador, si es el del sistema o está protegido); a la derecha, una barra con las particiones a escala,
  la tabla con tipo, sistema de archivos y etiqueta, y botones para cada acción. Cada acción abre un diálogo con las opciones
  (GPT/MBR, exFAT/FAT32, etiqueta, tamaño) y el progreso se ve en vivo.
- **Acciones:** crear tabla nueva y formatear el disco entero, crear partición en el hueco libre, borrar partición,
  formatear partición, grabar una imagen `.img`/`.iso` (de `/usuario` o del almacén) con verificación posterior.
- **Orden de shell:**
  `discos [lista] | desbloquear <disco> <disco> | formatear <disco> [gpt|mbr] [exfat|fat32] [etiqueta] |`
  `nueva <disco> <MiB|0=todo> [exfat|fat32] [etiqueta] | borrar <disco> <n> | formatear-part <disco> <n> [...] | grabar <disco> <imagen>`

## Seguridad
- Los discos reales siguen protegidos contra escritura. Para escribir hay que **desbloquear** el disco escribiendo su
  nombre (en la aplicación, en un diálogo; en el shell, dos veces). El desbloqueo dura hasta reiniciar o reescanear.
- El disco del sistema (desde el que arrancó SOMA) no se puede desbloquear.
- Las tablas se leen con un analizador que no confía en el disco: CRC32 de la cabecera y de las entradas GPT, límites de
  cada campo, cadenas de EBR con tope de vueltas, particiones solapadas o fuera del disco rechazadas
  (3000 rondas de entradas hostiles en la prueba).
- Al escribir GPT se escribe primero la copia del final y después la principal y el MBR protector: un corte a mitad deja
  siempre una tabla válida.

## Ring 3
El motor de tablas y formato (`src/core/discos_core.c`) es código puro, sin dependencias del núcleo, y se prueba en el
anfitrión. La escritura a disco y la ventana se quedan en el núcleo porque los controladores de bloque (AHCI, NVMe, USB) y
la interfaz gráfica viven ahí; no hay todavía acceso a bloques crudos desde ring 3. Las operaciones largas van en un hilo
propio («discos») para no congelar la interfaz.

## Pruebas
- `python3 tests/prueba_discos_331.py` (QEMU): protegido, desbloqueo, disco del sistema, GPT, FAT32/exFAT,
  grabar con verificación, y desde el anfitrión `sfdisk`, `fsck.fat` y `fsck.exfat`.
- `make pruebas-discos`: ida y vuelta GPT/MBR, copia GPT, EBR lógicas, rechazo de MBR > 2 TiB, entradas hostiles, FAT32
  formateado y detectado, FAT32 pequeño con clusters de 512 bytes (46 comprobaciones). Las imágenes producidas pasan `fsck.fat` y `sfdisk` las lee igual.
- Necesita `make`.
