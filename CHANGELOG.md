# Historial de cambios

## v0.3.0-prebeta — fase pre-beta

Primera versión que se publica como **pre-beta**: el sistema es usable a diario en el emulador y en el ThinkPad T480s, pero
todavía es software experimental (todo el núcleo, las aplicaciones y el navegador corren en ring 0). Esta fase sirve para
encontrar fallos con más gente y más hardware antes de una beta.

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
