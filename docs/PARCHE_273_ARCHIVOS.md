# Parche 273 · Explorador de archivos: solo Mi unidad, menú contextual y renombrar que se puede escribir

## Qué cambia

* **Solo se ve y se toca tu carpeta personal (`/usuario`, «Mi unidad»).** Desaparecen de la barra lateral Workspace, Documentos, Mis archivos, «Sistema (RamFS)»,
  el almacén BIGFS, la Biblioteca por categorías y los medidores de RamFS/BIGFS. La barra lateral muestra «Mi unidad» y debajo sus carpetas de primer nivel.
  Cualquier intento de ir a otra ruta (la orden `explorer`, un enlace, `..`) te deja en la raíz de Mi unidad; el botón «subir» se detiene ahí.
  La ruta de la cabecera empieza en «Mi unidad».
* **Clic derecho completo**, sobre un elemento o sobre el fondo: Abrir, Cortar, Copiar, Pegar, Renombrar, Eliminar, Copiar ruta, **Nueva carpeta**, **Nuevo archivo**,
  Organizar, Actualizar, cambiar de vista. Sobre un elemento también aparecen Nueva carpeta y Nuevo archivo.
* **Renombrar / nueva carpeta / nuevo archivo con un cuadro de texto de verdad**: cursor, selección, Retroceso, Supr (Ctrl+D), flechas izquierda/derecha,
  flecha arriba = inicio, flecha abajo = fin, Ctrl+A = seleccionar todo, tildes y eñes (se borran enteras). Al renombrar se selecciona el nombre **sin la extensión**:
  escribes el nombre nuevo y `.png` se conserva. «Nueva carpeta» propone un nombre libre («Nueva carpeta», «Nueva carpeta 2»...) ya seleccionado.
* **Errores dentro del cuadro** (sin cerrarlo ni perder lo escrito): nombre vacío o con `/ \ : * ? " < > |`, o que ya existe. Mensajes de confirmación en la barra de estado
  («Carpeta creada», «Renombrado», «Eliminado»).
* La ventana nueva del explorador abre directamente Mi unidad, ya con su lista.

## Diagnóstico temporal

En el cuadro de renombrar/crear, abajo a la izquierda y en gris oscuro, aparece «teclas recibidas: N   ultima: C». Si al teclear en el cuadro **N no sube**,
las teclas no están llegando al explorador (el fallo está antes, en la GUI) y con ese dato se localiza. Se retirará cuando confirmes que escribes bien.

## Pruebas

`tests/test_fm_273.c` (27 comprobaciones, con ASAN/UBSAN: edición de texto con selección y UTF-8, validación de nombres, confinamiento a `/usuario` incluidos `..` y falsos
amigos como `/usuarios`). El resto del explorador (dibujo, ratón) no se puede probar en el sandbox: compila sin avisos con las banderas del kernel, y la prueba
de uso es la tuya. `make test` no hace falta; sí `make`.
