# Parche 360 — Hoja de cálculo: gráficos, IA e integración

Última parte de la hoja de cálculo (358: motor y formatos; 359: la ventana).

## Gráficos
- Tipos: columnas, barras, líneas, tarta, área y dispersión (XY). Se crean con el botón de gráficos (icono de barras) a partir
  de la selección o, si solo hay una celda elegida, de la tabla alrededor del cursor.
- La forma de los datos se decide como en Excel (`src/apps/hoja_graf.c`): con más filas que columnas, cada columna es una serie;
  la primera fila son los nombres si es texto; la primera columna son las categorías si tiene texto, si la esquina está vacía o
  si es de dispersión (los X).
- En la ventana (`hoja_gui_graf.inc`): clic para elegirlo, arrastrar para moverlo (se ancla a una celda), la esquina para el
  tamaño, doble clic o el botón para el menú (tipo, título, leyenda, «usar la selección como datos», borrar). Sin FPU: valores en
  milésimas y la tarta con una tabla de arcotangentes.
- **XLSX**: `xl/drawings/drawingN.xml` (twoCellAnchor) y `xl/charts/chartN.xml` (DrawingML) con colores explícitos por serie y
  referencias a las celdas; al abrir un XLSX se leen tipo, rango (la unión de las referencias), título, leyenda, posición y tamaño.
  Probado con openpyxl y con LibreOffice (ida y vuelta, y convertido a PDF: se ven las barras, la tarta y las líneas).
- **ODS**: los gráficos no se exportan a ODS (el formato de objetos de LibreOffice es otro mundo); el formato propio `.hoja` y el
  XLSX sí los guardan.
- Si se borran o mueven hojas, un gráfico con los datos en otra hoja sigue a su hoja.

## IA dentro de la hoja
- Botón IA: una pregunta en lenguaje natural. La ventana prepara el contexto (libro, hojas, celda activa, selección y hasta 300
  filas × 30 columnas como `B5=valor (=fórmula)`, 24 KB) y `hoja_ia.c` llama al modelo en su hilo.
- La respuesta puede traer `FORMULA: =...` y/o `TABLA:` con columnas separadas por tabuladores; aparecen los botones «Poner la
  fórmula en B7» y «Pegar la tabla aquí» (con deshacer). Si la fórmula no es válida, se abre en la barra para corregirla.

## Con el resto de SOMA (`hoja_integra.c`)
- `hoja contactos` / botón «A hoja» de Contactos / Archivo: libro con nombre, apellidos, teléfono (como texto), email, empresa,
  cargo, cumpleaños (fecha), grupo, ciudad y dirección, con títulos inmovilizados, filtro y ordenado.
- `hoja agenda [tramo]`: las citas (vacío: tres meses desde el día 1 del mes) con fecha, horas, cita, lugar y nota.
- `hoja enviar [email]` / Archivo > «Enviar por correo…»: guarda una copia XLSX en /tmp (el libro no cambia de ruta) y abre un
  correo nuevo con ella adjunta. No se envía nada hasta que el usuario pulsa Enviar.
- Contexto del agente (`hoja_contexto`, desde `memoria.c`): si la misión habla de hojas, tablas, celdas, gráficos o nombra un
  libro abierto, el agente recibe el libro (15 filas × 10 columnas) y las órdenes `hoja`. Con el turno de la GUI.

## Correo: adjuntos
- `co_envio` tiene ahora un adjunto; `co_compone` hace `multipart/mixed` con el texto y el archivo en base64 (líneas de 76).
- `correo_redactar_adjunto(ruta, para, asunto)`: solo `/usuario` y `/tmp`, sin `..`, nunca `.conf`, `.key`, `.pem`, `.p12`,
  `.pfx`, `.cfg`, `.ini`, `.env` ni `.age`; 20 MB como mucho. El adjunto se ve en la redacción y se puede quitar.

## Pruebas
- `tests/test_hoja_360.c` (forma de los datos, colocación, 16 por hoja, hojas movidas y borradas, formato propio).
- `tests/test_hoja_graf_360.py` (XLSX con los seis tipos: XML, SOMA, openpyxl, LibreOffice ida y vuelta, `.hoja`, fuzz).
- `tests/test_hoja_ia_360.c` (fórmula y tabla dentro de la respuesta del modelo).
- `tests/test_correo_adj_360.c` (correo con adjunto: se vuelve a leer con el lector MIME de SOMA y los bytes son exactos).
- En QEMU: gráficos creados con la orden y con el ratón, moverlos, IA con un modelo de pruebas (la fórmula se pone en la celda),
  sin modelo (el error se ve), contactos y agenda a una hoja, enviar sin cuenta de correo.
