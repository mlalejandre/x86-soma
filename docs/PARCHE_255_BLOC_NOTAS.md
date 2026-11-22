# Parche 255: Bloc de notas renovado

## Qué fallaba (revisión)
- Botones solo de texto, sin iconos ni ayuda al pasar el ratón; el cuadro de archivos usaba la fuente de 8 px con etiquetas `[DIR]`/`[TXT]`.
- **Sin deshacer/rehacer** ni **buscar**.
- **Nuevo** llamaba a `notepad_init()` pero dejaba `np_path`: el siguiente Guardar sobrescribía el archivo anterior sin avisar, y
  si había cambios sin guardar se perdían sin preguntar.
- Un documento sin nombre se guardaba en silencio en `/workspace/notas.txt`.
- El nombre de archivo del cuadro Abrir/Guardar solo admitía ASCII (sin tildes ni ñ).

## Qué hay ahora
- **Barra**: tabla de botones (icono, etiqueta, ayuda) dibujada con `si_button`. Con menos de 700 px de ventana solo iconos; los que no
  caben se ocultan (siguen en el menú contextual). Deshacer/Rehacer/Pegar se apagan cuando no se pueden usar.
- **Deshacer/rehacer** (`np_undo`, `np_redo`): registro de inserciones y borrados (`struct uh_rec`, 192 registros y 24 KiB de datos
  por instancia). Fusiona teclas seguidas (palabras, Supr y Retroceso seguidos); reemplazar una selección son dos registros
  enlazados (`link`) que se deshacen juntos; un cambio nuevo descarta lo que quedaba por rehacer; si el registro se llena, se empieza
  de cero. Carga de archivo, «Nuevo» y la escritura en vivo de la IA vacían el historial.
- **Buscar**: `np_find_*` (sin distinguir mayúsculas ASCII), campo `struct tfield` por instancia, resaltado en `notepad_render_window`,
  contador y botones anterior/siguiente/cerrar. La barra desplaza el editor (`np_geo.find_y`).
- **Atajos**: Ctrl+Z, Ctrl+Mayús+Z (Ctrl+Y ya es el historial del portapapeles del sistema), Ctrl+F, Ctrl+Mayús+S (guardar como).
  Ctrl+F llega como el código 6 (el mismo que la flecha derecha), por eso no se ha añadido el salto por palabra con Ctrl+flechas.
- **Nuevo** restablece la ruta y, con cambios sin guardar, pide repetir la orden en 4 s. **Guardar** sin nombre abre «Guardar como».
- Iconos nuevos en `sysicon`: `SI_CUT` y `SI_PASTE`.

## Archivos
`src/apps/notepad.c`, `src/ui/sysicon.c`, `src/ui/sysicon.h` y `tests/test_iconos_240.c`. Ningún archivo protegido.

## Sin verificar
Rendimiento en el T480s con ficheros de 128 KiB y la búsqueda abierta (se recorre el texto una vez por imagen), y portapapeles real en QEMU.
