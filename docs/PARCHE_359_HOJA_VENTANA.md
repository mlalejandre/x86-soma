# Parche 359 — Hoja de cálculo: la ventana

Segunda parte de la hoja de cálculo (358: motor y formatos; 360: gráficos, IA e integración con las demás apps).

## Cómo se abre
- Icono **HOJA** del escritorio (verde, columna de la derecha).
- `hoja ventana` o `hoja ventana /usuario/cuentas.xlsx`.
- Doble clic en un `.xlsx`, `.xlsm`, `.ods`, `.csv`, `.tsv` o `.hoja` en Archivos.
La ventana muestra el libro actual de la orden `hoja`: lo que hace el agente con `hoja pon …` se ve al momento.

## Teclado
| Tecla | Qué hace |
|---|---|
| Escribir | empieza a escribir en la celda (las flechas confirman y se mueven) |
| Intro / doble clic / clic en la barra de fórmulas | editar la celda (las flechas mueven el cursor) |
| Intro, Tab (con Mayús: al revés) | confirmar y bajar / ir a la derecha |
| Ctrl+Intro | escribir lo mismo en toda la selección (las fórmulas se desplazan) |
| Esc | cancelar la edición; cerrar menús y barras; quitar la selección; cerrar la ventana |
| Flechas, RePág/AvPág, Inicio, Fin | moverse (con Mayús: seleccionar) |
| Ctrl+↑/↓ | hasta el borde de los datos |
| Supr / Retroceso | borrar el contenido (Retroceso empieza a escribir) |
| Ctrl+Z / Ctrl+Mayús+Z | deshacer / rehacer |
| Ctrl+C, Ctrl+X, Ctrl+V | copiar, cortar, pegar |
| Ctrl+B, Ctrl+I, Ctrl+U | negrita, cursiva, subrayado |
| Ctrl+D, Ctrl+R | rellenar hacia abajo / a la derecha |
| Ctrl+F, Ctrl+G, Ctrl+O, Ctrl+S, Ctrl+A | buscar, ir a, abrir, guardar, seleccionar lo usado |
| Ctrl + / Ctrl − / Ctrl+rueda | zoom |

Mientras se escribe una fórmula, un clic o un arrastre en la cuadrícula pone la referencia (`B2:B9`); en otra pestaña, `Hoja2!B2`.
Tab acepta la primera función propuesta.

## Diseño
- `src/apps/hoja_gui.c`: pintado de lo visible (filas y columnas inmovilizadas primero), zonas pulsables anotadas al pintar.
- Deshacer: cada paso guarda el rango que cambia (`hj_toma`/`hj_deja`, nuevas en el núcleo) o, para los cambios de estructura
  (filas, columnas, hojas, filtros, cortar), el libro entero en el formato propio. Hasta 80 pasos y 48 MB; un libro de más de
  24 MB no guarda pasos de estructura (se avisa).
- Dar formato a una columna entera se limita a lo usado; como mucho 200.000 celdas por operación y por pegado.
- Copiar dentro de la hoja conserva fórmulas y formato mientras el portapapeles no cambie; hacia otras apps sale como texto con
  tabuladores (64 KB).
- La orden `hoja` se ejecuta con el turno de la GUI (`gui_turn_begin`) para que el agente y la ventana no toquen el libro a la vez.

## Arreglos de paso
- `gui.c`: las apps que atienden Esc (Calendario, Contactos, Discos, Noticias, Agentes, Monitor, Grabadora, Cámara, Captura y la
  Hoja) ya no pasan por el bloque que sacaba del escritorio al modo texto.
- `shell.c`: `$A1`, `$A$1`, `D$5` no se expanden como variables si no existe una con ese nombre.

## Pruebas
- `tests/test_hoja_359.c` (deshacer: rangos, estilos, fórmulas de archivo, hojas borradas, rellenar).
- Probado en QEMU: escribir, fórmulas con referencias por ratón, autosuma, rellenar arrastrando, deshacer, ordenar con títulos,
  filtro, copiar y pegar, menús, pestañas, Esc.

## Límites conocidos
- Con la distribución de teclado «la de siempre» (us + ñ) no hay tecla para `=`: usa `teclado es` (Mayús+0).
- No hay celdas combinadas ni comentarios. Los gráficos llegan en el 360.
