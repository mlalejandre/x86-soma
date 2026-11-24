# Parche 289 · PDF, LaTeX e imágenes dentro del navegador

## Qué pasaba

- Al abrir un PDF en el navegador, por un enlace, escribiendo su dirección o con `file:///workspace/x.pdf`, salía «Contenido no
  soportado».
- Un `.tex` se veía como texto plano.
- Las imágenes WebP locales no se reconocían por su extensión.

## Qué cambia

Al navegar a un PDF o a un documento LaTeX, la pestaña lo muestra en un **visor de documentos**, como hace Chrome con los PDF.

- **PDF.** Se abre con el lector de PDF de SOMA, con todas sus herramientas: páginas, zoom, ajustar a lo ancho o a la página,
  miniaturas y copiar texto. Funciona con la rueda, el arrastre y el teclado (`n`/`p`, AvPág/RePág, `+`/`-`, `w`, `f`, `t`, `c`).
  Cada pestaña usa su propia instancia del lector, así que no se mezcla con las ventanas del lector que tengas abiertas. La pestaña
  lleva el título del PDF.
- **LaTeX** (`.tex`, `.latex`, `.ltx`). El documento se compone con el motor LaTeX de SOMA en una hoja de papel: título, secciones,
  fórmulas, ecuaciones numeradas, listas y tablas. La barra del visor tiene estos botones:

  | Botón | Qué hace |
  | --- | --- |
  | Documento / Código | Cambia entre la hoja y el fuente coloreado con números de línea |
  | A− / A+ | Cambia el tamaño de la letra |
  | Copiar código | Copia el fuente al portapapeles |
  | Abrir en Maquetación | Abre el `.tex` para editarlo y exportarlo. Si venía de internet, antes lo guarda en `/workspace/Descargas/` |

  Teclas: flechas, espacio, Inicio y Fin para moverse; `+` y `-` para la letra; `c` para cambiar entre documento y código.
- **Imágenes.** Ya se veían en la pestaña, incluidas WebP y SVG desde internet. Ahora los archivos `.webp` locales también se
  reconocen.

### Cuándo se abre el visor

- Si el servidor dice que es un PDF (`application/pdf`) o un LaTeX (`x-tex`, `x-latex`). Así funcionan los PDF sin `.pdf` en la
  dirección, como `arxiv.org/pdf/…`.
- Si el contenido empieza por la firma `%PDF-` y el servidor no dice que sea una página web.
- Si la dirección acaba en `.pdf` o `.tex` y el servidor no devuelve una página web. Un «informe.pdf» que en realidad es una página
  (el visor de GitHub, una pantalla de inicio de sesión) se ve como página.

### Cómo funciona por dentro

- `src/apps/web_tipo.h` toma la decisión. Es lógica pura: la usan el motor web, el navegador y las pruebas.
- El motor web no dibuja el documento. Crea una página marcador cuyo título empieza por un espacio de anchura cero y «PDF» o «TEX».
- El navegador ve ese título y pone el visor (`src/apps/web_visor.c`) encima de la página. Funciona igual con el motor aislado de
  ring 3, porque solo hace falta el título.
- Como la página marcador está en el historial, Atrás, Adelante, Recargar y las pestañas funcionan sin nada especial. Al ir a otra
  página, el visor se cierra, libera su memoria y corta la descarga que hubiera en curso.

### Errores

Los errores salen en una tarjeta, no como una página rota:

- el archivo no existe;
- no hay red;
- el servidor responde HTTP 404 o similar;
- el servidor devuelve una página web en vez del PDF;
- el PDF está dañado.

### Límites

- El PDF se analiza en el hilo de la GUI, igual que al abrirlo en el lector. Uno muy grande puede congelar la pantalla un momento.
- Un PDF de internet se descarga dos veces: primero el motor, que solo mira de qué tipo es, y luego el visor. También se vuelve a
  descargar al cambiar de pestaña y volver, porque el navegador solo mantiene viva una página.
- Las imágenes que incluye un `.tex` (`\includegraphics`) salen como un recuadro, porque no vienen con el documento.
- Las fórmulas que algunas páginas web dibujan con MathJax (JavaScript) no se convierten.

## Pruebas

- `tests/test_visor_289.c` tiene 48 comprobaciones de la decisión: por la extensión, por el tipo y por la firma; el visor de GitHub
  y el arXiv sin extensión; la página marcador; el nombre con `%20` y UTF-8; y las rutas locales sin `..` ni nulos.
- `tests/test_visor_maquina_289.c` tiene 43 comprobaciones del visor completo con el motor LaTeX de verdad y la pantalla, el disco, la
  red y el lector de PDF simulados:
  - apertura, maquetado y pintado; zoom, rueda, botones, código y copiar;
  - Maquetación con un archivo local y con uno remoto;
  - cierre al navegar y URL que cambia antes que el título;
  - PDF en una instancia propia, sin cambiar la que estaba seleccionada, y los atajos del navegador;
  - cada error, la respuesta tardía que se descarta y que no queda nada sin liberar.

  Pasa con ASan, también con detección de fugas.

Hace falta `make`.
