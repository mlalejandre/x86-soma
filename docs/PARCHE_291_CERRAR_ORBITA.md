# Parche 291 · Botón derecho para cerrar una aplicación minimizada en órbita

## Qué pasaba

Para cerrar una aplicación minimizada (uno de los iconos que orbitan alrededor del orbe) había que arrastrarla hasta el agujero negro
del centro. Funcionaba, pero era lento si había varias, y nada indicaba que se pudiera hacer de otra forma.

## Qué cambia

- **Botón derecho sobre un icono en órbita: se cierra al momento**, con el mismo efecto de partículas cayendo al horizonte que al
  soltarlo en el agujero negro.
  - El clic izquierdo sigue restaurándola y el arrastre al centro sigue cerrándola.
  - Funciona en el escritorio y con el orbe al frente (tecla Super).
  - Si hay una ventana encima del puntero, el botón derecho es de esa ventana y no cierra nada de la órbita.
- **Lo que tiene cambios sin guardar** (Notas, Paint, Studio) no se pierde al primer clic. Sale el aviso «cambios sin guardar. Pulsa
  otra vez con el botón derecho para cerrar sin guardar», y un segundo clic derecho en los 8 s siguientes la cierra. Es la misma
  protección que al arrastrarla.
- **El rótulo de cada icono en órbita** dice ahora cómo se cierra: «Notas  (boton derecho: cerrar)».

## Archivos

- `src/ui/gui_minimizar.c`: añade `her_min_right_close` y el texto del aviso para el botón derecho.
- `src/ui/gui.c`: el bucle principal, si el clic derecho cae sobre un icono en órbita y no hay ventana encima.
- `src/ui/gui_orbe_frente.c`: lo mismo con el orbe al frente.
- `src/ui/gui_iconos.c`: el rótulo.

Siguen pasando las pruebas del orbe (259, 266 y la compilación sin coma flotante del 259).

Hace falta `make`.
