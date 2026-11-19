# Parche 168B · El orbe al frente con la tecla Windows

Requiere el 168. Petición del usuario: cambiar la tecla que llama al orbe y que el orbe pueda ponerse delante de cualquier
aplicación con una animación cósmica.

## Qué cambia
- **La tecla**: la `x` ya no llama al orbe (ahora se escribe normal en la barra del agente). En su lugar:
  - en teclados de PC, la **tecla Windows** (izquierda o derecha, códigos E0 5B / E0 5C);
  - en el emulador del Mac, **Option** pulsada y soltada sola (Alt izquierdo). Option junto con otra tecla no cuenta, así
    no estorba a ningún atajo. Si QEMU manda Cmd como tecla Windows, también vale.
- **Escritorio vacío**: la tecla muestra u oculta las aplicaciones en órbita alrededor del orbe, como hacía la `x`.
- **Con aplicaciones abiertas**: el orbe sale del fondo y se pone **delante de todas, en el centro de la pantalla**.
  - Apertura (~0,45 s): las ventanas se oscurecen con un velo azul noche. Un campo de 140 estrellas sale disparado desde el
    centro, como un salto a la velocidad de la luz, y se frena hasta quedar titilando. El orbe emerge de una bruma con ondas
    de choque y partículas, y las aplicaciones salen en órbita.
  - Elegir una aplicación con un clic la abre (o la trae delante si ya estaba abierta).
  - Cierre (~0,3 s, con Esc, otra vez la tecla, un clic fuera o al elegir): ondas que se contraen, estrellas que caen hacia
    el centro, y el orbe vuelve detrás de las ventanas.
  - Mientras está delante se dibuja sobre una copia congelada de las ventanas (no se repinta cada aplicación), a ~60 fps.
- **Rendimiento**: el velo translúcido y las copias de pantalla completa se reparten entre los núcleos (mismo resultado).
- **Trackpad (diagnóstico)**: en el T480s el trackpad no contestó como Synaptics. Ahora la identificación se reintenta
  hasta 3 veces, se prueba también un trackpad **Elan** (algunos T480s lo llevan), y la orden `teclado` muestra lo que
  contestó (`Identificacion: Synaptics ... | Elan ...`) para preparar el controlador adecuado.

## Pruebas
- QEMU, 2560×1440: con el Monitor abierto, tecla Windows → orbe delante con la animación; Option sola → se cierra;
  tecla Windows y Esc → se cierra. Capturas a 120 ms, 370 ms y abierto.
- Simulador del controlador PS/2 (Synaptics + TrackPoint y ratón con rueda): sin cambios en el resultado.
