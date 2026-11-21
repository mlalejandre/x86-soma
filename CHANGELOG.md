# Historial de cambios

## v0.3.0-prebeta — fase pre-beta

Primera versión que se publica como **pre-beta**: el sistema es usable a diario en el emulador y en el ThinkPad T480s, pero
todavía es software experimental (todo el núcleo, las aplicaciones y el navegador corren en ring 0). Esta fase sirve para
encontrar fallos con más gente y más hardware antes de una beta.

### El orbe lee sus respuestas, elección de voz, página Sonido y fuentes en listas desplegables (250)
- **El orbe habla** (activado por defecto): cada respuesta se lee con la voz de Piper elegida, en un hilo (buscar o instalar la
  voz no frena el escritorio). Alt o una pregunta nueva cortan la lectura. Texto preparado para leer (`src/core/habla_texto.h`):
  sin markdown, código, colores de consola ni enlaces («el enlace»); las rutas se leen por su nombre; hasta ~1500 caracteres.
- **Ajustes → Voz → Lectura en voz alta**: activar/desactivar, voz (todas las de `/voces`, con nombre, país y calidad), velocidad
  (lenta, normal, rápida, muy rápida), hablante en voces con varios, **Probar voz** / **Parar** e **Instalar del pendrive** (ahora
  instala todas las voces de `MODELOS`, no solo la primera). `/etc/habla.conf`; Notas y `habla` usan la misma voz y velocidad.
- **Ajustes → Sonido** (página nueva): volumen, silencio y prueba de la salida; entrada y ganancia del micrófono. `/etc/sonido.conf`,
  aplicado al arrancar; `ajustes sonido`.
- **Fuentes**: la fuente del escritorio (todo el catálogo, con su descripción), la de la interfaz y la del editor se eligen en
  **listas desplegables** (con rueda y barra) en vez de botones que no cabían. Widget reutilizable en Ajustes.
- Prueba nueva: `tests/test_tts_250.c` (limpieza del texto que se lee).

### Dictado: sin copias repetidas del mismo texto (249)
- **Fallo**: al dictar con Alt, whisper a veces entraba en un bucle y escribía la frase tres o cuatro veces (y la última a
  medias). El filtro de 245 solo quitaba bloques de hasta 8 palabras repetidos 3 veces, y tramo a tramo.
- **Arreglo** (`src/user/voz/voz.cpp`, `voz_filtros.h`): bloques de 4 o más palabras repetidos 2 veces seguidas se dejan una sola
  vez (2-3 palabras con 3 copias, 1 palabra con 4), sin mirar mayúsculas, acentos ni puntuación; se quita la copia a medias del
  final; la limpieza se hace con todos los tramos juntos; tope de tokens por tramo según la duración del audio; y si el resultado
  sigue pareciendo un bucle (o pasa de 5,5 palabras por segundo), se reconoce otra vez con la ventana de audio completa.
- **Dictados largos**: el texto del dictado tenía un tope de 256 bytes y se cortaba sin avisar; ahora 4 KiB.
- Pruebas: casos nuevos en `tests/test_voz_245.cpp` (el caso real de la frase repetida tres veces y media, y repeticiones legítimas
  que no se tocan).

### Documentos con acabado profesional y revisión visual con IA (248)
- **Agente de documentos** (`src/agent/docagent.c`): `documento <petición>`, misiones del orbe ("redacta un informe... en
  PDF") y **Generar** del Maquetador. Detecta el tipo (informe, paper, revista, presentación, carta, CV, email, libro) y le da
  al modelo unas indicaciones de editorial (hoja de estilo de la casa, tipografía española, gráficos SVG, prohibiciones) y una
  receta por tipo. Guarda en `/workspace/docs/` la fuente y el PDF y abre el resultado en el Maquetador.
- **Motor de documentos** (`src/apps/docgen.c`): HTML con la hoja de estilo de la casa o LaTeX, paginado en A4, A4 apaisado,
  A5, carta, 16:9 y email; cortes en blanco entre bloques sin títulos huérfanos; cabecera y pie con `{n}/{N}/{titulo}`;
  números de diapositiva; gráficos SVG con sus textos; revisión automática (`dg_check`); HTML autónomo con `@page`.
- **Motor web**: escala del CSS por hilo (documentos a 192 ppp sin tocar el navegador), texto justificado de verdad, atributos
  `width`/`height` escalados; los documentos usan siempre Liberation Sans.
- **PDF** (`src/core/pdfw.c`): páginas en imagen comprimida (Flate con predictor PNG), tamaño en puntos y título en UTF-16.
- **Revisión con visión**: `llm_chat_ex` manda indicaciones de sistema propias e imágenes PNG a local/OpenAI (`image_url`),
  Claude (`image` base64) y Gemini (`inline_data`); si el modelo no ve imágenes se recuerda y se revisa con el informe.
  El revisor devuelve `VEREDICTO` y el documento corregido; solo se acepta si no empeora (hasta dos rondas).
- **Programador**: tras escribir una app gráfica, la abre, captura su ventana (`gui_soma_win_snapshot`) y la hace criticar a
  un "diseñador de interfaces"; una ronda de mejora si hace falta.
- **Maquetador**: vista de páginas (con sombra, a la anchura de la ventana, A−/A+ de 40 a 250 %), botones **PDF**, **HTML** y
  **Revisar IA**; exporta en un hilo aparte.
- **GUI**: `gui_turn_begin/gui_turn_end`, turno de pantalla para cualquier hilo que dibuje fuera de pantalla; `gui_leave` espera
  a que el escritorio lo vea antes de volver (evita una carrera al pedir turno seguido); `gui_agent_submit`.
- Pruebas nuevas: `tests/test_docgen_248.c` (paginación, formatos, revisión, PDF con su xref, HTML autónomo, tipo de documento,
  extracción, veredicto, nombres) y `tests/test_llm_vision_248.c` (peticiones con imágenes de los cuatro proveedores).

### Texto a voz: Notas lee en voz alta con voces de Piper (247)
- **Notas → Leer en voz alta** (y en el menú del botón derecho): lee la selección o todo el documento; el botón pasa a
  **Parar lectura**. Orden `habla texto | habla instalar | habla parar | habla`.
- **Motor propio para las voces VITS de Piper** (`src/user/voz/habla_vits.h`, sin onnxruntime): lector mínimo de ONNX
  (`habla_onnx.h`), codificador de texto con atención relativa, predictor estocástico de duraciones (splines racionales
  cuadráticos), flujo de acoplamiento con WaveNet y generador HiFi-GAN (ConvTranspose por fases, resblocks de tipo 1 y 2).
  Los hiperparámetros salen de los pesos y de los atributos de los nodos, así que valen todas las calidades; también voces
  de varios hablantes. Convoluciones con im2col + AVX2/FMA repartidas entre los núcleos libres. Comprobado contra
  onnxruntime: misma salida (diferencia máxima 2e-5) y, por reconocimiento con whisper, el texto exacto.
- **Texto a fonemas en español** (`habla_g2p_es.h`) al estilo de espeak-ng: números, símbolos, siglas, sílabas, acento y
  acentos secundarios, alófonos, asimilación de la n, palabras átonas; seseo y yeísmo para voces de América. 95 % de
  palabras iguales a espeak-ng en un corpus de 380 frases.
- Va dentro de `voz.elf` (`--habla`); la voz se instala sola desde la carpeta `MODELOS` del pendrive a `/voces` (QEMU:
  `-fw_cfg name=opt/soma/voz,file=x.onnx` y `name=opt/soma/voz_json,file=x.onnx.json`). Suena por la tarjeta de sonido
  mientras calcula la frase siguiente.
- Prueba nueva: `tests/test_habla_247.cpp` (fonemas frente a espeak-ng, números, seseo, lector ONNX).

### Voz: elegir el modelo de transcripción en Ajustes (246)
- **Ajustes > Voz** (página nueva): tres niveles de modelo, **Rápido** (`ggml-base-q5_1.bin`, el de siempre), **Preciso**
  (`ggml-small-q5_1.bin`, recomendado) y **Máxima precisión** (`ggml-large-v3-turbo-q5_0.bin`). Cada uno dice si está
  instalado, en la carpeta `MODELOS` del pendrive o no disponible; **Instalar desde el pendrive** lo copia al almacén con barra
  de progreso. Lo usan el dictado con Alt, la Grabadora y la orden `voz`.
- Si el elegido no está instalado pero sí en el pendrive, se instala solo al dictar (la pastilla muestra "Instalando el modelo
  de voz... N %"); si no está en ningún sitio, se usa el instalado más parecido.
- Terminal: `voz modelo rapido|preciso|maximo`; `voz` dice el elegido y el que está en uso; `voz instalar` instala el elegido;
  `ajustes voz` abre Ajustes en esa página. La elección se guarda en `/etc/voz.conf`.
- QEMU: `-fw_cfg name=opt/soma/modelo_preciso,file=...` y `opt/soma/modelo_maximo` para probar los otros niveles.

### Voz: dictado en todo el sistema manteniendo Alt y menos alucinaciones de whisper (245)
- **Pulsar para hablar**: se mantiene **Alt** izquierda mientras se habla y al soltarla se transcribe. El texto entra por el
  teclado, así que llega a quien tenga el foco (orbe, terminal, notas, editor de programas, cualquier campo) igual que si se
  hubiera escrito; en el orbe además se envía. Toque corto de Alt = abrir el orbe, como antes; Alt + otra tecla = atajo (se
  descarta lo grabado sin avisar); **Esc** cancela. El aviso es una pastilla global sobre todas las ventanas ("Escuchando" con
  el nivel del micrófono, "Transcribiendo...", o el motivo si algo falla). Sustituye al modo de 244 (Alt para empezar y Alt para
  parar, solo en el orbe).
- **Contra las alucinaciones**: detector de voz (umbral adaptado al ruido, recorte de silencios y pausas; sin voz no se llama
  a whisper), búsqueda en haz de 5, `suppress_nst`, sin contexto previo, umbrales de entropía/logprob/no-voz con reintento a
  más temperatura, y filtros por tramo: probabilidad de no-voz + confianza media de los tokens, frases típicas de subtítulos
  (Amara, "suscríbete", "gracias por ver"...), frases cortas dudosas, anotaciones (`[Música]`, `(risas)`, ♪) y bucles de
  repetición. `-v` dice qué se descartó y por qué; `-b 1` vuelve a la decodificación voraz; `-V` no recorta silencios.
- En el ordenador de pruebas, la búsqueda en haz cuesta ~45 % más de tiempo de reconocimiento que la voraz (2,6 s frente a 1,8 s
  para 7 s de audio, 1 hilo) y acierta más palabras en las grabaciones de prueba.
- Prueba nueva: `tests/test_voz_245.cpp` (detector de voz, frases típicas, anotaciones, repeticiones).

### Voz: hilos en varios nucleos y dictado al orbe con Alt (244)
- **Hilos de usuario en los nucleos secundarios**: un proceso puede ejecutar hilos de calculo en los nucleos libres (ring 3 con
  el espacio de direcciones del proceso; sin llamadas al sistema). La orden `voz` usa hasta 4 hilos (el principal + 3
  secundarios): en QEMU con 2 nucleos, 7 s de audio pasaron de 1149 s a 558 s (x2,1). Mientras hay hilos de usuario, los
  repartos de calculo del sistema (smp_parallel) se hacen solo en el nucleo 0. Si el proceso muere, sus hilos se paran con una
  interrupcion entre procesadores. `-t N` fija el numero de hilos.
- **Dictado al orbe**: con el campo de la pregunta listo ("soma> escribe una mision...   (Alt: dictar)"), **Alt** empieza a
  grabar, **Alt** otra vez para, transcribe y envia el texto a SOMA como si se hubiera escrito. **Esc** cancela. Sobre el orbe
  se ve "Escuchando..." con el nivel del microfono y luego "Transcribiendo...". Escribir a mano sigue igual. En ese modo, Alt
  sola ya no llama al orbe (la tecla Windows si).
- Con silencio, whisper se inventa frases: ahora `voz` avisa de que no se oye nada y descarta los segmentos sin voz.

### Voz a texto: el modelo se instala solo (243)
- Si no hay modelo en el almacén, `voz` y **Transcribir** lo copian solos desde la carpeta `MODELOS` del pendrive (antes había
  que escribir `voz instalar`). Se ignoran las copias `._*.bin` que deja macOS (antes podían elegirse en lugar del modelo).

### Voz a texto en español con whisper.cpp (241)
- **Orden `voz`**: transcribe un WAV del almacén sin conexión (`voz ultima`, `voz /grabaciones/grab_1.wav`, `-T` con marcas de
  tiempo, `-l` idioma). `voz instalar` copia el modelo ggml desde la carpeta `MODELOS` del pendrive (o, en QEMU, desde
  `-fw_cfg name=opt/soma/modelo,file=...`) a `/modelos`. `voz` solo muestra el estado (CPU y modelo).
- **Grabadora → Transcribir** (o `T`): el texto aparece en un panel sobre la onda, se copia con un botón y se guarda en
  `/grabaciones/grab_N.txt`. Se puede seguir usando SOMA mientras transcribe y se puede detener.
- whisper.cpp + ggml (`third_party/whisper`, MIT) como proceso de ring 3 (`src/user/voz/`): C++17 sin excepciones ni RTTI,
  libc mínima propia, un solo hilo. Para audios cortos el codificador trabaja solo sobre la duración real (3 veces más rápido).
- **AVX en ring 3**: el núcleo activa XSAVE/AVX al arrancar y guarda el estado AVX de cada proceso (antes solo SSE, con
  FXSAVE). Los procesos pueden tener una región de memoria grande bajo demanda con marcos de la memoria alta (el T480s tiene
  ~15 GiB que casi no se usaban).
- `ejecutar.py -t t480s-usb` copia al pendrive los modelos de la carpeta `modelos/` del proyecto.
- Pruebas: `tests/test_voz_241.cpp` (divisor de palabras contra `std::regex`); `make voz-host` para probar en Linux.

- **24 iconos nuevos del sistema** (`sysicon.h`): música, película, retroceder, avanzar, pista anterior y siguiente, lista,
  código, deshacer, rehacer, buscar, reemplazar, ir a la línea, comentario, documento, código fuente, disco, editar,
  cuadrícula, copiar, cerrar, sin sonido, repetir y terminal. Se ven en *Ajustes → Iconos*.
- **SOMA Media rediseñado** con el estilo de la Grabadora: biblioteca lateral con icono por tipo (vídeo o audio), el que suena
  marcado con *play*/*pausa*; cabecera con el archivo, sus datos y una pastilla de estado; escenario con estados vacío,
  cargando, terminado y error; barra de progreso con tirador y tiempos; controles redondos con nombre (detener, anterior,
  −10 s, reproducir/pausa, +10 s, siguiente y recargar la biblioteca); visualización del audio en degradado. Abre a
  880×600 y, si la ventana es estrecha, deja solo los iconos.
- **SOMA Studio**: pestañas, barra de herramientas y botones con iconos; si la barra no cabe, deshacer, rehacer, ir a la línea
  y comentar (y después buscar y reemplazar) pasan a ser botones de solo icono con su nombre al pasar el ratón. Archivos
  con icono de código coloreado por lenguaje (Python en ámbar). Tarjetas con icono en el título, pasos del programador con
  icono y marca al terminar, ficha del proyecto con insignia y lenguaje, y estados vacíos con icono.
- Prueba en el anfitrión `tests/test_iconos_240.c` (todos los iconos a 48 y 14 px).

- **`soma.win`**: ventana, dibujo (rect, text, line, circle, pixel), ratón y teclado para Python; la misma API que `win.*`
  de SOMA-LANG. El dibujo va al núcleo por lotes (una llamada al sistema por fotograma). La ventana se cierra sola al
  terminar el script.
- **SOMA Studio → Crear con IA** elige el lenguaje: **SOMA-LANG o Python**. Para Python, el programador usa una referencia y
  ejemplos de Python, comprueba cada candidato con el intérprete real (sin escribir el archivo), ejecuta las pruebas de los
  programas de consola, repara con el error de Python y aprende en su propio archivo de lecciones. *Probar* abre el
  Terminal con `python archivo.py`. El editor verifica los `.py` con Python. Orden `programa archivo.py ...`.
- **Esc** en la ventana de una aplicación (SOMA-LANG o Python) la cierra, como dice su referencia (antes salía del escritorio).
- `open(...).write(...)` guarda al momento (antes solo al cerrar el archivo).
- Prueba en QEMU `tests/prueba_python_239.py`; prueba en el anfitrión `tests/test_coder_py_239.c`.

### La configuración se guarda en el pendrive (238)
- Un pendrive **sin la partición SOMADATA** (preparado con una versión antigua) ya puede guardar: SOMA usa como disco el
  archivo `SOMADATA.IMG` de la unidad *SOMA*, que `ejecutar.py -t t480s-usb` ofrece crear sin borrar nada.
- SOMADATA se reconoce aunque la tabla de particiones haya cambiado (tamaño o firma del disco): antes se ignoraba y no se
  guardaba nada, sin avisar.
- Al arrancar se espera hasta 8 s a que el pendrive responda (antes, uno lento dejaba SOMA en memoria todo el arranque).
- **Aviso en el escritorio** cuando nada se va a guardar, con el motivo en *Ajustes → Sistema* y en la orden `disco`.
- `INFORME.TXT` se escribe también cuando no hay disco del sistema.

### Python (237)
- Orden **`python`**: intérprete de Python 3 (MicroPython 1.30) como proceso aislado de ring 3. Modo interactivo, scripts de
  `/workspace` con argumentos e `import` entre ellos, y `python -c "código"`.
- En el Terminal del escritorio la salida sale al momento, `input()` lee lo que escribes y **Esc** detiene el script.
- `open()`, `input()`, `exit()` y los módulos `math`, `random`, `json`, `re`, `time`, `struct`, `collections`, `heapq`,
  `binascii`, `io` y `soma` (archivos y reloj de SOMA). Lee el VFS salvo las rutas privadas; escribe solo en `/workspace`.
- Dos llamadas al sistema nuevas que solo tiene este proceso (consola y archivos) y que pasan el mismo filtro de rutas que
  `SYS_OPEN`.

### Diagnóstico del arranque y otros equipos (228)
- **Etapas del arranque** en pantalla (`[ARRANQUE n/13]`) y, si el núcleo falla durante el arranque, la etapa y la opción de
  GRUB que la salta en la pantalla de error.
- **Memoria entre arranques**: si un arranque no llega al escritorio, el siguiente dice dónde se quedó (consola, Ajustes →
  Sistema e informe).
- **Informe de hardware** en `INFORME.TXT` de la partición de arranque del pendrive (y `/informe.txt`): equipo, etapas,
  dispositivos PCI con su driver en SOMA (o la falta de él), pantalla, teclado, disco, WiFi, batería, energía y registro del
  núcleo. Orden `informe` y botón *Guardar informe* en Ajustes → Sistema.
- **SOMADATA se amplía** en el primer arranque hasta el final del pendrive (como mucho 8 GiB) si es la última partición.
- Entrada de GRUB **«otro PC con teclado inglés (US)»**.

### Arranque desde pendrive USB (x86_64)
- SOMA arranca en un PC real desde un pendrive (probado en el ThinkPad T480s). Instrucciones para Windows, Linux y
  macOS en el README.

### Escritorio
- **Animación al minimizar**: la ventana se desintegra en un mosaico y vuela en espiral hasta su sitio en la órbita (223).
- Órbita estable de las ventanas minimizadas, nombres al pasar el ratón y animación al cerrar en el agujero negro (206, 208).
- Iconos y gadget del reloj arrastrables, con la disposición guardada en `/etc/escritorio.conf` (`escritorio reiniciar`
  vuelve a la de fábrica) (208).
- **Cuadrícula virtual**: al soltar un icono se alinea a la celda más cercana; si está ocupada, intercambia el sitio (218).
- Botones de **Apagar** y **Reiniciar** rediseñados con iconos vectoriales, estados al pasar el ratón y cuenta atrás
  de confirmación (218).
- Iconos vectoriales del sistema (`sysicon`) y Grabadora, Cámara y Captura con aspecto profesional (210).

### Varias ventanas por aplicación
- Notas, Imágenes, Lector PDF, Archivos, Paint y SOMA Studio abren tantas ventanas como se quiera (hasta 6–8 según la
  aplicación), cada una con su documento y su estado (211–217).
- La X minimiza (la ventana orbita con su número), un clic la restaura y arrastrarla al agujero negro la cierra.
- Monitor, Ajustes, Cámara, Grabadora, Captura y Agente siguen siendo de una sola ventana a propósito.

### Rendimiento
- Capa de fondo, cachés de iconos y de contenido de ventanas, borrado y volcado por zonas (207, 209).
- Con varias ventanas abiertas el orbe va fluido y el Monitor del sistema ya no se redibuja sin cambios: en el T480s, de
  40–50 % de CPU en reposo a ~22 % (216).

### Estabilidad
- **Aviso de cambios sin guardar** al cerrar Notas, Paint o Studio en el agujero negro; hay que soltarlo otra vez para
  cerrar sin guardar (219).
- **Prueba de esfuerzo** `escritorio prueba [n]`: abre y cierra cada aplicación n veces y comprueba que la memoria vuelve
  exactamente a su valor y que el heap está íntegro (220).
- **Rescate de aplicaciones**: si una aplicación hace un acceso a memoria no válido (u otra excepción de CPU) o se queda
  10 s sin soltar el procesador, se cierra su ventana con un aviso y el sistema sigue, en vez de un pánico o un cuelgue
  total (221). Pruebas: `escritorio fallo memoria`, `escritorio fallo cuelgue`.
- Ya existían y siguen activos: guardado atómico con diario y CRC en SOMAFS02, decodificadores de imagen y PDF en
  ring 3 con fuzzing (ASan/UBSan), contención de fallos del motor del navegador y vigilante del núcleo.

### Limitaciones conocidas
- Contención, no aislamiento: una aplicación averiada puede haber dañado memoria del núcleo antes de fallar.
- El rescate solo existe en x86_64 (en la Raspberry Pi 5 un fallo de una aplicación sigue siendo un pánico).
- El orbe animado gasta ~20 % de un núcleo en el T480s con una ventana abierta.
