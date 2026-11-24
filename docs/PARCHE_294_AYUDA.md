# Parche 294 · La bienvenida del orbe y la ayuda rápida (F1)

## Qué hace

SOMA se puede usar desde el segundo cero. Nada de lo que añade este parche espera a que el usuario lo cierre ni le quita una tecla.

**Al encender.** 1,5 s después de que aparezca el escritorio:

- El orbe da la bienvenida. La escribe en el orbe y, si **Ajustes > Voz** tiene activada la lectura, la dice en voz alta:
  «¡Hola! Soy SOMA. Ya puedes usarme: pulsa la tecla Windows para ver tus aplicaciones y el reloj, mantén Alt pulsada para
  hablarme, o escríbeme y pulsa Intro. Si te pierdes, pulsa F1.»
- Abajo en el centro, encima de la barra del sistema y lejos de la órbita de las aplicaciones, sale una tarjeta con consejos
  cortos. Hay uno cada 6 s y una barra que marca el tiempo:

| Tecla | Consejo |
|---|---|
| Tecla Windows | Pulsa la tecla Windows (o toca Alt) y verás tus aplicaciones y el reloj. |
| Mantén Alt | Mantén Alt pulsada y habla; al soltarla, SOMA escribe lo que dijiste. |
| Escribe | Escribe lo que necesites y pulsa Intro: abrir apps, buscar, programar... |
| Botón X | La X de una ventana la minimiza: queda orbitando el agujero negro. |
| Clic derecho | Clic derecho en una app en órbita la cierra (o arrástrala al agujero negro). |
| F1 | Pulsa F1 cuando quieras para ver la ayuda completa. |

  - Si el usuario hace lo que dice el consejo (pulsar Windows, dictar con Alt, preguntar al orbe con Intro), la tarjeta dice
    **«¡Eso es!»** y pasa al siguiente.
  - Lo que el usuario ya ha hecho no se le vuelve a explicar.
  - Un clic en la tarjeta pasa al siguiente consejo; la **X** o **Esc** la cierran.
  - Todo dura unos 37 s como mucho y luego desaparece sola.

**F1, en cualquier momento.** Muestra u oculta la ayuda completa en el centro de la pantalla, con todas las teclas:

- Windows o un toque de Alt;
- mantener Alt;
- escribir y pulsar Intro;
- Esc;
- el botón X;
- el clic derecho en órbita;
- ImprPant (con Ctrl, vídeo);
- F1.

Se cierra con F1, Esc o su X. Abajo tiene la casilla **«Mostrar estos consejos al iniciar»**. Si se quita, al encender no salen
ni la bienvenida ni los consejos, pero F1 sigue funcionando. El ajuste se guarda en `/etc/ayuda.conf` (`inicio=0` o `inicio=1`).

## Por qué no estorba al usuario con experiencia

- **Las teclas y el ratón siguen funcionando igual.** La ayuda solo toma Esc y el clic cuando hay una tarjeta suya a la vista.
  Windows, Alt, escribir, abrir aplicaciones y el clic fuera de la tarjeta funcionan como siempre.
- **No le habla encima.** Si el usuario actúa antes de que llegue la bienvenida (teclea, pulsa Windows o dicta), no se le lee
  nada.
- **Se aparta si ya sabe.**
  - Si escribe 40 teclas mientras salen los consejos, la tarjeta desaparece.
  - El texto de un dictado no cuenta, porque entra por el teclado.
  - Mientras se dicta, la pastilla «Escuchando» ocupa ese sitio y el consejo no se dibuja.
- **No cuesta.** La tarjeta es opaca y se dibuja entera en cada volcado.
  - Cambiar de consejo, o mover la barra de tiempo (10 veces por segundo), solo vuelca la pantalla.
  - El escritorio solo se repinta cuando la tarjeta aparece o desaparece.
- **Sin micrófono no promete la voz.** El consejo y la fila de Alt dicen que hace falta micrófono, y la bienvenida no menciona
  Alt.
- **Sin la fuente con acentos** (`soma.sysfont=off`), todo se escribe en ASCII con la fuente de siempre.

## Archivos

- `src/ui/ayuda.h` (nuevo): la lógica pura:
  - qué consejo toca y cuándo;
  - qué hace cada tecla o clic;
  - dónde van las tarjetas;
  - el ajuste guardado;
  - el texto sin acentos.
- `src/ui/gui_ayuda.c` (nuevo, lo incluye `gui.c`): la bienvenida, el dibujo de las dos tarjetas y la unión con el escritorio.
- `src/ui/console.c`, `console.h`: F1 (código 0x3B) se cuenta como la tecla Windows y la GUI lo recoge con `console_take_help()`.
  - El teclado USB llega con el mismo código.
  - Antes F1 no hacía nada.
- `src/ui/gui.c`:
  - la ayuda arranca una vez por encendido;
  - F1 y los consejos tienen su turno en el bucle;
  - la tecla Windows, Intro y las teclas escritas se le cuentan;
  - Esc y el clic sobre su tarjeta son suyos.
- `src/ui/gui_voz.c`: la tarjeta se dibuja en el mismo sitio que la pastilla del dictado (encima de todo, debajo de la pastilla).
- `Makefile`:
  - `gui.o` depende de `gui_voz.c` y `gui_ayuda.c`;
  - dos pruebas nuevas en `make test`.

## Pruebas

- `tests/test_ayuda_294.c` (59 comprobaciones, la lógica):
  - la espera, el orden y la duración de los consejos;
  - «Eso es» y lo ya aprendido;
  - que las teclas nunca se las queda;
  - Esc, la X, el clic, F1 y la casilla;
  - que las tarjetas caben de 640×400 a 3840×2160 sin tapar la órbita de las aplicaciones;
  - el archivo de ajuste y el texto sin acentos.
- `tests/test_ayuda_maquina_294.c` (42 comprobaciones): el `gui_ayuda.c` de verdad, sobre un framebuffer en memoria y con la
  fuente de verdad. Comprueba:
  - la bienvenida escrita y leída;
  - que las tarjetas solo pintan dentro de su sitio y dejan el recorte como estaba;
  - que se lee el texto y la barra de tiempo está donde toca;
  - que Esc y el clic solo se toman con tarjeta;
  - que la casilla se guarda y se respeta al siguiente encendido;
  - lo del dictado, Intro en otra app, sin micrófono y sin fuente.

  Con `AYUDA_PPM=prefijo` guarda las imágenes. `make test` solo la exige en Linux x86_64.

En el Mac: si las teclas F están en modo multimedia, F1 es **fn+F1**.
