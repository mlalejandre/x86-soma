# Parche 266 · El LCM avisa al momento cuando la petición va al modelo de lenguaje

Hasta ahora, una petición que iba al modelo de lenguaje (una pregunta, un programa, un documento, una búsqueda en internet)
dejaba el orbe pensando sin decir nada durante segundos o minutos. Ahora el **LCM**, que decide en milisegundos y sin el
modelo, **contesta al instante** antes de llamarlo. Dice qué se va a hacer, en qué pasos, y cuánto suele tardar. Por ejemplo:

- **Pregunta o texto** → «Esto se lo paso al modelo de IA: lo piensa y te contesto. Suele tardar unos 15 segundos, dame un
  momento.»
- **Programa** (en C, Python o SOMA-LANG, según lo pedido) → «Voy a programarlo en C con el modelo de IA: primero planifico el
  programa, después escribo el código y lo compruebo, y al final lo pruebo y corrijo lo que falle. Suele tardar unos 2 minutos,
  ten un poco de paciencia.» Si es cambiar un programa que ya existe: «Lo arreglo en Python con el modelo de IA: leo el
  programa, le pido los cambios, y antes de guardarlo lo compruebo y lo pruebo...»
- **Documento** (maquetador) → «Voy a preparar el documento con el modelo de IA: primero redacta el contenido, después lo
  maqueto con su formato, lo reviso y al final lo exporto...»
- **Con herramientas** (agente):
  - en internet: «...Primero busco en internet, después leo lo que encuentre y al final te lo resumo...»;
  - con archivos: «...Primero miro los archivos que hagan falta...»;
  - lo demás: «...decido qué herramientas hacen falta...».
- Si la respuesta directa resulta necesitar herramientas: «Para responderte bien necesito usar herramientas. Voy a buscarlo en
  internet...».

**El tiempo no se inventa.** Se calcula con lo que tardaron de verdad las peticiones anteriores del mismo tipo (media móvil;
pesan más las últimas), medido desde el aviso hasta la respuesta, y se guarda en `/etc/lcm.conf`. Sin medidas todavía, el aviso
dice «puede tardar un poco». No se cuentan los fallos del modelo, lo que pide otro agente ni los valores absurdos.

Si la petición empezaba con un saludo, el aviso también lo lleva («Buenas tardes. Voy a...»). Como el resto de frases del LCM,
tiene varias formulaciones y no usa listas numeradas, para que suene bien en voz alta.

## Dónde se ve

- **Orbe**: el aviso aparece en el teleprompter. Cuando llega la respuesta lo sustituye y se revela entera; con la respuesta en
  directo, se borra al empezar a escribirse. La barra de fases empieza por el icono del LCM.
- **Conversación**: una línea `[soma]:` en ámbar.
- **Terminal**: `[SOMA] ...`.
- **Voz**: se lee en voz alta si el orbe lee sus respuestas (Ajustes → Voz). Si el aviso aún se está leyendo cuando llega la
  respuesta, la respuesta corta la lectura.

## Ajustes → LCM

- **Antes de usar el modelo de IA, avisar...**: *Sin aviso* o *Avisar* (por defecto *Avisar*).
- En *Desde que se encendió el equipo* aparece lo que suele tardar cada tipo: «respuesta 12 s, con herramientas 40 s, programa
  2 min, documento 1 min».
- Con el LCM apagado, todo va como antes y no hay avisos.

## Cómo está hecho

- `src/agent/lcm_soma.inc`:
  - `lcm_aviso_texto` escribe el aviso y `lcm_aviso_mide` apunta lo que se tardó.
  - Los avisos y las medias se guardan en `/etc/lcm.conf` (`aviso=1`, `t3=125000/4`...). Un archivo de antes, sin estas líneas,
    sirve igual, y los valores rotos se ignoran.
- `src/agent/agent.c`: `agent_aviso` y `agent_aviso_fin`, antes y después del maquetador, del programador, de la respuesta
  directa y del bucle con herramientas.
- `src/ui/gui_agente.c`: `gui_aviso`. Lee el aviso en voz alta y, con la pantalla cedida un momento (`gui_enter`, el mecanismo
  de siempre del hilo del agente), lo pone en la conversación y en el orbe.
- `src/ui/gui_vortice.c` y `gui_prompter.c`: el aviso se marca para que la respuesta lo sustituya entera.

## Pruebas

En `make pruebas-lcm`:

- **`tests/test_aviso_266.c`**: 33 comprobaciones.
  - Cada tipo dice sus pasos y pide paciencia; el saludo va delante; hay variedad de frases.
  - Sin medidas no da tiempo; después da 15 s, 2 min, 1 min, «solo unos segundos»...
  - Las medias se guardan y se recuperan, y los valores absurdos o rotos no se usan.
  - Avisos apagados, LCM apagado, otro agente y modo agente: sin aviso.
  - Un `/etc/lcm.conf` antiguo sigue sirviendo, y las órdenes del LCM no llevan aviso.
- **`tests/test_orbe_266.c`**: 9 comprobaciones. El aviso en el orbe, la respuesta que lo sustituye, el texto en directo y las
  fases.

Además siguen pasando las anteriores: 675 casos del traductor y 38, 11, 57 y 26 comprobaciones del 257 al 260. El LCM con el
aviso compila como en el núcleo (`-ffreestanding -mgeneral-regs-only -Werror`).

Se quita con `python3 parche_266_aviso_lcm.py --revertir`, que deja todo exactamente como estaba.
