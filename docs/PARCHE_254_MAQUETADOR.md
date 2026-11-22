# Parche 254: Maquetador renovado

## Qué fallaba (revisión)
- **Tildes rotas**: el teclado entrega UTF-8 (`á` = `C3 A1`) y `layout_handle_key` lo convertía como si fuera Latin-1 (`u >= 0xA0`), así
  que cada tilde acababa como dos caracteres raros. Ahora la descripción usa `textfield.c`, que ensambla UTF-8 bien.
- **Sin portapapeles** en la descripción y **cursor siempre al final**: la caja era un búfer propio sin cursor ni selección.
- **Abrir** y **Recargar** hacían lo mismo (`load_path`).
- **Esc cerraba la ventana** aunque se estuviera escribiendo.
- Iconos dibujados a mano con líneas sueltas, sin ayuda al pasar el ratón; la barra se salía de la ventana si era estrecha.
- Sin forma de copiar el código fuente; el carril de la barra de desplazamiento no respondía a clics.

## Qué hay ahora
- Barra con iconos de `sysicon` (rejilla 48x48, suavizados), selector Documento/Fuente, zoom, exportar y revisar. Con menos de ~1000 px
  se ven solo iconos (PDF y HTML mantienen su texto). Pasar el ratón por un botón explica qué hace en la barra de estado.
- **Descripción**: `struct tfield` multilínea (600 bytes). Intro = generar; Mayús+Intro = salto de línea; los saltos se convierten
  en espacios al mandarla al agente. Crece de una a tres filas al enfocarla o al tener texto.
- **Ruta**: `struct tfield` de una línea; Intro abre el documento. `Abrir` lleva al explorador de archivos.
- **Portapapeles**: `layout_clip_op()` (gancho nuevo en `gui.c`). Con un campo enfocado va a ese campo; sin campo, Ctrl+C copia el
  código fuente del documento y Ctrl+V pega en la descripción.
- **Esc**: `layout_escape()` suelta el campo; sin campo cierra la ventana como antes.
- Indicador «Página x de n» sobre el documento y clic en el carril de la barra de desplazamiento (salta hasta ahí).
- Iconos nuevos en `sysicon`: `SI_SPARK` (destello de IA) y `SI_SEND`.

## Archivos
`src/apps/layout.c`, `src/apps/layout.h`, `src/ui/sysicon.c`, `src/ui/sysicon.h`, `src/ui/gui.c` (3 ganchos: portapapeles, Esc y tecla Esc) y `tests/test_iconos_240.c` (dos iconos nuevos).
No añade archivos a `src/` (el índice de fuentes sigue en 510 de 511). Ningún archivo de `protected.txt`.

## No incluido (pendiente si lo quieres)
- Seleccionar y copiar texto directamente del documento paginado (es una imagen: haría falta que `docgen` devuelva las cajas de
  texto) y de la vista Fuente.
- Deshacer en los campos de texto.
