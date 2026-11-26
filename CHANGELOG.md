# Historial de cambios

## v0.3.0-prebeta — fase pre-beta

Primera versión que se publica como **pre-beta**: el sistema es usable a diario en el emulador y en el ThinkPad T480s, pero
todavía es software experimental. Esta fase sirve para encontrar fallos con más gente y más hardware antes de una beta.
Desde el parche 311, lo que llega de Internet (páginas, correo, YouTube, noticias, TLS) y el motor de IA se procesan en
servicios aislados de ring 3; el escritorio y las aplicaciones siguen en ring 0.  <!-- SOMA_339_GITHUB -->

<!-- SOMA_339_GITHUB -->
### Listo para publicar: README, exportadores y CI (339)
- README al día: la red sin clientes en el núcleo (tabla de servicios de ring 3), la IA local, Noticias, Discos, Agentes, Fecha
  y hora y el panel Salida del Studio.
- `exportar_txt.py` ya no exporta ningún `.conf` (antes solo se saltaba una lista de nombres conocidos) ni archivos `.env`;
  temas nuevos en los dos exportadores (noticias, agentes, servicios de ring 3, y los que faltaban).
- La CI compila también dentro del contenedor del `Dockerfile` (Debian), el mismo con el que compila `ejecutar.py`: el fallo de
  enlace del 329 en Debian no se habría visto en la CI, que usa Ubuntu.
- `.gitignore`: ningún `.conf` se sube.

<!-- SOMA_338_AGENTES -->
### La aplicación Agentes, ahora un panel vivo (338)
- Muestra la misión en curso con los especialistas por los que pasa y un botón para detenerla; los ocho especialistas con lo
  que hacen, si trabajan ahora y cuántas veces lo han hecho (con «Probar»); las misiones recientes con su resultado y duración;
  y la actividad de los agentes. Orden `agentes` con lo mismo en texto.

<!-- SOMA_337_NOTICIAS -->
### Aplicación de Noticias (337)
- Nueva aplicación Noticias (icono NOTICIAS, orden `noticias`): categorías a elegir, noticias locales por ciudad o región,
  canales RSS propios, titulares con medio y hora, y un clic para leer la noticia en el Navegador.
- Resumen con IA de cada categoría, con cita [n] de cada dato y la lista de fuentes; «Guardar en memoria» lo lleva al cartucho
  de memoria Noticias (se crea solo).
- Los canales se descargan y se leen en ring 3 (netsvc y leesvc): el XML de Internet no se analiza en el núcleo.

<!-- SOMA_336_INVESTIGA_TEMAS -->
### El investigador adapta fuentes e informe al tema (336)
- Detecta si la pregunta es de deportes, salud, ciencia, tecnología o actualidad, y si hace falta lo de hoy o de esta semana.
- Lee primero las mejores fuentes para ese tema (organismos sanitarios en salud, prensa deportiva y sitios oficiales en
  deportes, NASA/Nature/CSIC en ciencia, documentación oficial en tecnología, agencias en actualidad) y descarta foros,
  tiendas y webs de remedios. Para ciencia, salud y tecnología busca también en la Wikipedia en inglés.
- El informe sigue reglas del tema: fechas y marcadores en deportes, prudencia y fuentes oficiales en salud, consenso frente a
  hipótesis en ciencia. Arreglado: con un modelo que razona no proponía búsquedas.

<!-- SOMA_335_YT_BUSQUEDAS -->
### YouTube: el buscador con IA propone búsquedas de verdad (335)
- Con un modelo que razona, el buscador acababa buscando la frase entera: el límite de 200 fichas se gastaba pensando. Ahora
  tiene 1200, se queda solo con la respuesta (sin `<think>` ni el razonamiento) y descarta frases largas.
- Si el modelo no propone nada (o no hay modelo), se busca lo esencial de la petición: «quiero ver vídeos de cómo hacer pan
  sin amasar, por favor» → «cómo hacer pan sin amasar».

<!-- SOMA_334_ORBE_MD -->
### El orbe dibuja tablas, LaTeX y código como el chat (334)
- La respuesta del orbe usa el maquetador del chat: tablas de verdad, fórmulas LaTeX con el motor de SOMA (fracciones,
  raíces, matrices, entornos) y bloques de código con fondo y resaltado. Se mantiene el revelado de arriba abajo, el
  seguimiento automático y el razonamiento detrás, apagado.
- Nueva orden `orbe <archivo.md>` para ver un texto con formato en el orbe.

<!-- SOMA_333_SALIDA -->
### El programador ya no abre el Terminal al ejecutar (333)
- Python, C y JavaScript se ejecutan con su salida en el panel «Salida» del propio programador: se le puede escribir al
  programa (`input()`), Esc lo para y «Otra vez» lo relanza. Si no imprime nada (un juego con su ventana), el panel se
  cierra solo al terminar. Las páginas HTML se abren directamente en el navegador.

<!-- SOMA_332_HORA -->
### Zona horaria: el reloj ya no va una hora atrasado (332)
- El reloj del escritorio enseñaba la hora UTC del equipo: en España, una hora menos en invierno. Ahora todo (reloj, `date`, el
  agente, la agenda, el programador, Python, las fechas de los archivos del pendrive) usa la zona elegida con su horario de verano.
- Nueva página Ajustes > Fecha y hora y orden `hora` (`hora zona <ciudad o UTC+2>`, `hora zonas`, `hora reloj utc|local`).
  55 zonas con las reglas de verano vigentes. Para equipos que también arrancan Windows: `hora reloj local`.
- Arreglado: `date` sumaba 2 horas todo el año y el agente podía ver el día 32 al pasar la medianoche.

<!-- SOMA_331_DISCOS -->
### Discos: particiones, formato e imágenes (331)
- Nueva aplicación «Discos»: ve todos los discos y sus particiones, crea tablas GPT o MBR, crea, borra y formatea particiones
  (exFAT o FAT32) y graba imágenes `.img`/`.iso` con verificación. También como orden `discos`.
- Los discos siguen protegidos: para escribir hay que desbloquearlos tecleando su nombre; el del sistema no se desbloquea.
- Las tablas de particiones se leen sin fiarse del disco (CRC, límites, cadenas EBR acotadas).

<!-- SOMA_330_RESPUESTA_PRIMERO -->
### La respuesta de verdad antes que la muletilla (330)
- El aviso hablado («se lo paso al modelo de IA…») espera 2,5 s: si la respuesta llega antes, no se dice. La misma frase no se
  repite si se dijo hace menos de 2 minutos. En el orbe y en la conversación sale siempre.
- Cuando llega la respuesta, lo que se esté leyendo se corta al momento, también el sonido que quedaba en la tarjeta. Antes, si la
  parada llegaba mientras se preparaba la lectura, el aviso sonaba entero.

<!-- SOMA_329_IA_LOCAL -->
### Motor de IA local: embeddings y LLM pequeños sin servidor (329)
- Nuevo motor de IA en ring 3 (llama.cpp con AVX2 y varios hilos): carga modelos GGUF de `/modelos` y los mantiene en memoria
  mientras se usan; se para solo tras 10 minutos sin uso.
- La memoria calcula sus vectores con él (`memoria config emb_motor local|servidor|auto`; en `auto` lo usa si está el modelo).
- Nuevo proveedor de LLM «En SOMA» (Ajustes > Inteligencia artificial) para modelos pequeños como LFM2.5-350M.
- Orden `ia` (estado, modelos, instalar, chat, embed, parar). Los modelos se copian de la carpeta MODELOS del pendrive.
- El núcleo sigue sin coma flotante: toda la aritmética del modelo está en ring 3.
- Enlaza también con la libstdc++ de Debian (la del `Dockerfile`) y con glibc anterior a la 2.38: faltaban `mbsrtowcs` y
  `__isoc99_sscanf` en la libc del motor.

<!-- SOMA_328_COMPILA_RAPIDO -->
### Compilar es mucho más rápido (328)
- `make` usa todos los procesadores, cada objeto depende solo de las cabeceras que incluye y los programas de ring 3 se compilan
  en paralelo y solo lo que cambió. `ejecutar.py` ya no borra `build/` en cada ciclo. Sin cambios: unos 0,2 s; tocando un archivo
  `.c`, segundos.
- Arreglado: el navegador aislado no se recompilaba al cambiar una cabecera.

<!-- SOMA_327_RED_FUERA -->
### Ningún cliente de red en el núcleo (327)
- El servidor local del LLM sin clave (salud, agente y chat), los embeddings de la memoria y las órdenes `search` y `curl` van
  por `netsvc` (ring 3, un proceso por petición, hasta 10 minutos de espera y cancelables). El servicio de YouTube de casa va
  por el túnel de ring 3 del 326.
- Prueba nueva en QEMU con un LLM local, un servidor de embeddings y un servicio de YouTube simulados.

<!-- SOMA_326_TUNEL_TLS -->
### El correo y YouTube, sin TLS en el núcleo (326)
- Cada conexión del correo (IMAP y SMTP, también con STARTTLS) y de YouTube nativo va por un túnel de ring 3 (`tlstun`, un
  proceso por conexión) que hace el TCP, el TLS 1.3 y los certificados; el núcleo solo ve el texto en claro.
- Pueden correr 16 procesos de usuario a la vez (antes 8).

<!-- SOMA_325_LLM_AISLADO -->
### El cliente del LLM, fuera del núcleo (325)
- Las peticiones a proveedores en la nube o a servidores con clave van por `netsvc` (ring 3, un proceso por petición); la clave
  viaja en sus cabeceras y el búfer se borra al terminar. El servidor local sin clave sigue por el camino de siempre.
- Prueba nueva en QEMU con un servidor de LLM falso por HTTPS.

<!-- SOMA_324_PRUEBA_CC -->
### La prueba de `cc` en QEMU vuelve a pasar (324)
- `tests/prueba_cc_265.py` daba 7/16 por un fallo del arnés (la consola expandía `$d` y una orden pasaba de 1 KiB), no del
  compilador. Ahora manda los programas en hexadecimal y por trozos: 16/16.

<!-- SOMA_323_BSS -->
### 2 MiB menos de imagen (323)
- La demo del programador, el maquetador LaTeX, la comprobación del programador, el editor de código y una autoprueba reservan
  sus búferes grandes al usarse, no en el arranque. La imagen baja del 60,7 % al 56,3 % de su presupuesto.

<!-- SOMA_322_TEX_HOSTIL -->
### LaTeX: lo mismo que en el chat, y un desbordamiento de enteros (322)
- El motor LaTeX (visor de documentos, maquetador y fórmulas del chat) tenía el mismo fallo que el maquetador de Markdown: con el
  pool lleno, textos que apuntaban más allá de lo escrito. Y un delimitador que crece (`\left(` alrededor de algo enorme)
  desbordaba un entero. Fuzzer nuevo en `make test`.

<!-- SOMA_321_CHAT_MD_HOSTIL -->
### Chat: el maquetador de Markdown ya no lee fuera de su memoria (321)
- Con un mensaje enorme (por ejemplo, un bloque de código de cientos de KB en una respuesta del modelo) el pool de 768 KiB se
  llenaba y varias palabras, enlaces, imágenes y bloques de código quedaban apuntando más allá de lo escrito: medirlos y dibujarlos
  leía fuera del pool. Y al partir una palabra demasiado ancha se miraba un byte de más. Lo encontró un fuzzer nuevo que genera
  Markdown con estructura (ahora en `make test`).

<!-- SOMA_320_ENTRADAS_HOSTILES -->
### Pruebas con datos hostiles de la wifi y del LLM (320)
- Dos pruebas nuevas en `make test` para lo que llega por el aire (balizas, escaneo y el saludo WPA, también con el MIC bien
  calculado, como haría un punto de acceso falso que sabe la contraseña) y para las respuestas del servidor del LLM. No
  encontraron fallos: quedan para que no aparezcan.

<!-- SOMA_319_CORREO_PROTO -->
### Correo: arreglado un desbordamiento al leer lo que manda el servidor IMAP (319)
- Un literal `{4294967295}` hacía que el núcleo diera por bueno un cuerpo de 4 GB dentro de una respuesta de 44 bytes y leyera
  memoria que no era suya. Lo encontró un fuzzer nuevo de los lectores IMAP y SMTP, que ahora corre en `make test`; de paso se
  acotan todas sus lecturas a la longitud recibida.

<!-- SOMA_318_NAVEGADOR_CERRADO -->
### El navegador ya no se pasa al núcleo si el proceso aislado no arranca (318)
- Antes, sin memoria o sin procesos libres, la sesión seguía en silencio con el motor web (y su JavaScript) dentro del núcleo.
  Ahora la página no se abre, la barra de estado lo explica y se reintenta en la siguiente navegación.

<!-- SOMA_317_DESCARGAS_AISLADAS -->
### Las demás descargas, fuera del núcleo; cancelar un proceso al momento (317)
- Las imágenes de las respuestas del chat, el visor de documentos y la orden `tls` descargan con `netsvc` (ring 3, un proceso
  por descarga). En el núcleo solo queda el cliente del LLM.
- Las descargas aisladas se pueden cancelar (el visor al cerrarse), y un proceso que se para muere en ~10 ms en lugar de >2 s.
- Las autopruebas de QEMU esperan a que termine el arranque: ya no dan fugas falsas.

<!-- SOMA_316_RED_AISLADA -->
### Lo que el agente descarga de Internet, fuera del núcleo; y `descargar https://` en hardware real (316)
- `netsvc`, el cliente HTTP aislado de ring 3 (parche 142), lleva ahora su propio TLS 1.3 con verificación de certificados,
  como el navegador aislado. Las descargas de `investiga` y `search` van por él: el apretón TLS, los certificados (X.509) y
  las respuestas HTTP (chunked, gzip, redirecciones) de cualquier web ya no se analizan en ring 0, y no se mandan las cookies
  del navegador.
- Cada descarga va en un proceso nuevo que termina con ella: una respuesta hostil no puede quedarse para espiar las siguientes.
- `descargar https://...` funciona en hardware real (antes `netsvc` solo sabía HTTPS a través de la pasarela de QEMU).
- Prueba nueva en QEMU con una autoridad y un servidor HTTPS de prueba: lo mismo que el núcleo, y rechaza lo que no es de fiar.

<!-- SOMA_315_YOUTUBE_AISLADO -->
### YouTube nativo: sus respuestas se leen fuera del núcleo (315)
- La búsqueda y la ficha de los vídeos (el JSON que devuelve youtube.com) ya no se analizan en ring 0: lo hace `leesvc`, el
  lector aislado del parche 312, con el mismo código. El núcleo sanea lo que devuelve; la dirección del vídeo, que va a la
  petición HTTP, solo puede llevar ASCII visible y tiene que ser de YouTube.
- Arreglado de paso: una respuesta cortada dejaba el canal, el estado o el autor sin terminador.

<!-- SOMA_314_PROGZ -->
### Imagen más ligera: los programas grandes van comprimidos (314)
- El navegador, el reproductor, `voz`, `js`, `cc` y `python` van en la imagen comprimidos con zlib (de 8,2 MB a 4,0 MB) y se
  descomprimen al lanzarlos, en el servicio aislado `zdec`, con comprobación de tamaño y huella. La imagen baja del 68,8 % al
  60,3 % de su presupuesto.
- Si un programa incrustado está dañado no se ejecuta y la consola lo dice.

<!-- SOMA_313_LECTOR_WEB -->
### El lector de páginas web ya no pierde párrafos (313)
- `wl_texto` (`investiga` y `search`) compactaba las líneas útiles en el mismo búfer y, con dos seguidas, machacaba la segunda: de
  un artículo normal el modelo recibía el título, cientos de líneas vacías y el último párrafo. Ahora se hace en dos pasadas y
  llega el texto entero.

<!-- SOMA_312_WEB_AISLADA -->
### Lo que el agente lee de Internet, fuera del núcleo (312)
- El texto de las páginas web que lee `investiga` y los resultados de Wikipedia y DuckDuckGo de `search` ya no se analizan en
  ring 0. Lo hace `leesvc`, un servicio aislado de ring 3 que solo puede llamar a `exit`, con el mismo código. El núcleo sanea
  lo que devuelve.
- Si una página tumba al lector, cuenta como no leída y el agente sigue. Pruebas en QEMU: páginas reales y 300 mutiladas dan lo
  mismo que en el núcleo.

<!-- SOMA_311_CORREO_AISLADO -->
### El correo se lee fuera del núcleo (311)
- Leer un correo (MIME, base64, quoted-printable, juegos de caracteres, RFC 2047, HTML), decodificar sus adjuntos y hacer la ficha
  de la lista ya no se hace en ring 0. Lo hace `mimesvc`, un servicio aislado de ring 3 que solo puede llamar a `exit`, con el mismo
  código de antes. El núcleo sanea todo lo que devuelve.
- Si un mensaje tumba al lector, la aplicación lo dice («correo ilegible») y el sistema sigue. No se vuelve a intentar en el núcleo.
- Pruebas en QEMU: mensajes reales y 300 mutilados dan lo mismo que en el núcleo, sin pérdidas de memoria. Cuesta 0,8 ms por correo.

<!-- SOMA_310_PRESUPUESTO -->
### El tamaño del núcleo, vigilado en la CI (310)
- `make size-check` (`tools/size_check.py`) dice cuánto ocupa la imagen frente a su límite de 46 MiB y qué es lo que más ocupa,
  incluidos los programas de ring 3 incrustados. Avisa desde el 85 %. La CI de GitHub falla desde el 90 %, antes de que el
  enlazador corte de golpe.
- Hoy: 68,8 % (31,7 de 46 MiB). Los pesos del LCM y los programas incrustados suman más de 12 MiB.

<!-- SOMA_309_CODIGO_MUERTO -->
### Fuera las funciones que nadie llama (309)
- 52 funciones (517 líneas en 38 archivos) que no llamaba nadie en todo el proyecto: ni el núcleo de ninguna de las dos
  arquitecturas, ni las pruebas, ni las herramientas. Por ejemplo, el antiguo puente de JavaScript del navegador, pruebas de red de los
  primeros parches y funciones del VFS sustituidas por las órdenes del terminal. La lista completa y el criterio están en
  `docs/PARCHE_309_CODIGO_MUERTO.md`.
- Se conserva `gui_stream_*` (texto a trozos en el orbe): tiene pruebas y falta conectarla.

<!-- SOMA_308_EFICIENCIA -->
### Eficiencia: copias de memoria rápidas en la Raspberry Pi (308)
- En aarch64, `memcpy`, `memmove`, `memset` y `memcmp` iban byte a byte. Ahora van de 8 en 8 bytes con solo accesos alineados
  (`-mstrict-align`), también cuando origen y destino tienen alineaciones distintas. En emulación, una fila de pantalla se copia
  14 veces más rápido y un `memset` grande 23 veces.
- Los 9 módulos de protocolos de la Pi 5 ya no se compilan en x86_64, donde el enlazador los tiraba enteros.

<!-- SOMA_307_SEGURIDAD -->
### Seguridad: el SO protege los mismos archivos que el anfitrión (307)
- El filtro que aplica el propio SO a los parches del agente comparaba la ruta entera (`core/thread.c` no casaba con
  `thread.c`) y tenía su propia lista de 23 archivos frente a los 76 del anfitrión. En la práctica solo protegía el anfitrión.
  Ahora usa la misma lista (`src/agent/agent_protegidos.h`) y compara el nombre de archivo, como `ejecutar.py`.
- Si un parche es de C o del espacio de trabajo se decide por su ruta, no por lo que diga el resto de la respuesta del modelo.
- `audit.py --strict` (`make ci`) falla si las dos listas dejan de coincidir.

<!-- SOMA_306_PILAS -->
### Pilas del núcleo: un desbordamiento siempre se ve (306)
- El núcleo se compila con `-fstack-clash-protection` (x86_64). Una función con un marco de pila mayor que la página de guarda
  (hay más de 50) ya no puede saltársela y escribir en silencio en la pila de otro hilo: el sistema se para con su traza.
- Si un hilo agota su pila, el pánico lo dice: `DESBORDAMIENTO DE PILA del hilo '<nombre>'`.
- Las pilas propias del navegador y de `wn_fetch` (sin página de guarda) llevan un canario. Si se desbordan, se avisa y se para,
  en vez de seguir con el montón corrupto.
- Ninguna función nueva del núcleo puede reservar más de 16 KiB de pila de golpe (error con `WERROR=1`, como en la CI). El motor
  web, que corre en sus pilas grandes, queda fuera del límite.
- `crash pila` (depuración) y un escenario nuevo en `fault-check`. Arreglo menor en `usermode.c` (nulo comprobado tarde).

<!-- SOMA_305_LIMPIEZA -->
### Limpieza: código y herramientas que ya no se usan (305)
- Fuera 25 archivos que nada usaba (unos 5,9 MB):
  - un vídeo de prueba olvidado en la raíz (`in.mkv`) y un volcado antiguo de la interfaz (`fuentes_ui.txt`);
  - los 19 exportadores anteriores a `exportar_txt.py` (`tools/estructura*.py`, `tools/exportar*.py`). Dos de ellos
    copiaban también los `.conf`, y con ellos tus claves;
  - el «JavaScript» de juguete `src/apps/js_runtime.c` (nadie lo llamaba desde que existe el motor `web_js`) y su nota;
  - una copia repetida de `lang.h`.
- `protected.txt`: las cuatro entradas de los `lcm_*.inc` llevaban el comentario detrás del nombre y no protegían nada. Los
  comentarios van ahora en su propia línea, y `ejecutar.py` y `audit.py` ignoran lo que vaya tras `#`.
- `.gitignore` sin líneas repetidas ni rotas, y con `*.mkv`, `fuentes_*.txt` y `estructura*.txt`.
- **Arreglo** (`src/fs/srcfs.c`): leer una fuente del sistema por su nombre corto (`lang.h`) podía dar otro archivo que
  terminara igual (`lang/lang.h`). Ahora gana siempre la ruta exacta.

<!-- SOMA_304_CI -->
### Los servidores falsos de las pruebas con el mismo Python (304)
- Las pruebas que crean un `llama-server` o un `yt-dlp` falsos los escribían con `#!/usr/bin/env python3`. En el Mac de GitHub
  ese `python3` no llegaba a servir (el falso se quedaba «cargando»). Ahora usan el mismo Python que corre la prueba, y la espera
  de la prueba del lanzador sube a 40 s.

<!-- SOMA_303_CI -->
### La prueba del lanzador en macOS (303)
- `test_lanzador_emb_278` esperaba al lanzador 10 s a ciegas, y en la CI de macOS no llegaba a conectar. Ahora espera hasta 40 s a que
  escuche de verdad y, si no lo consigue, enseña lo que dijo el lanzador en vez de colgarse.
- El lanzador admite `SOMA_LANZADOR_HOST` (por defecto `0.0.0.0`, como siempre). La prueba escucha solo en `127.0.0.1`.

<!-- SOMA_302_CI -->
### La CI: macOS y fault-check (302)
- macOS: el SDK de Xcode 26 marca `sprintf` como obsoleto y con `-Werror` es error. Las pruebas de host usan `snprintf` con el tamaño del buffer.
- fault-check: la CI instala Pillow (`python3-pil`), que necesita `tools/mkavi.py` para crear el vídeo de prueba.

<!-- SOMA_301_EXPORTAR -->
### exportar_txt.py al día (301)
- Temas nuevos: `ia`, `lcm`, `memoria`, `chat`, `correo`, `ayuda`, `orbe`, `investiga`, `youtube`, `paint`, `agenda`, `video` y `ci`.
  Se pueden juntar (`--tema correo --tema ia`) sin repetir archivos, y `--temas` dice cuánto ocupa cada uno.
- `--parche 295` o `--parche 290-300`: exporta los archivos que tocó ese parche (los que llevan su marca) con su nota de `docs/`.
- La cabecera dice el último parche aplicado y las reglas que ya conocemos (clang del Mac, gcc 14 de la CI, sin coma flotante...).
- Más protección: nunca exporta `correo.conf`, tu memoria exportada (`memoria-export/`, `*.smem`), modelos `.gguf` ni vídeos; y
  oculta también tokens `github_pat_`, `hf_`, `glpat-`, `AKIA…` y claves privadas pegadas en un texto.

<!-- SOMA_300_CI -->
### La CI con el gcc 14 de GitHub (300)
- GitHub compila con gcc 14, que convierte en error lo que antes era aviso: `websvc.c` llamaba a `sha256` sin declararla,
  `web_f64.c` tenía variables que gcc 14 (aarch64) cree sin inicializar, y libxaac (audio AAC, de terceros) mezcla tipos en punteros a función.
- El arranque de prueba admite `BOOT_MAX_S`: en tu equipo sigue siendo 6 s; la CI usa 30 s (QEMU sin KVM en máquinas compartidas).

<!-- SOMA_299_CI -->
### La CI de GitHub en verde (299)
- Compila con `-Werror` en x86_64 y en aarch64 (que no compilaba), y las pruebas de JavaScript compilan con clang (macOS).
- Una orden escrita en el terminal ya no se pierde en silencio si otro hilo tiene la consola: espera a que quede libre.
- Con una aplicación delante, Esc es de la aplicación aunque estén los consejos del arranque (la tarjeta de F1 se sigue cerrando con Esc).
- El servidor web se enciende solo también en QEMU sin aceleración (y sigue apagado en un equipo real).
- `fault_check.py` y `audit.py` al día con el sistema (rutas de escritura, botones del reproductor y avisos falsos).

### La CI vuelve a ejecutarse (298)
- `.github/workflows/ci.yml` era YAML inválido por un «: » sin comillas en un nombre: GitHub no lanzaba ninguna tarea.

<!-- SOMA_297_GITHUB -->
### Listo para publicar en GitHub, con el correo y la ayuda (297)
- Entradas del historial de los parches 285 a 297 y secciones del README para el chat, la investigación, el visor, Paint con IA,
  la ayuda rápida (F1) y el correo.
- `.gitignore`: no se suben `correo.conf` ni claves o certificados sueltos (`*.pem`, `*.key`, `*.p12`, `*.pfx`): el correo
  guarda ahí tu clave.
- El exportador para Claude (`tools/exporta_para_claude.py`) tiene los temas `correo` y `ayuda`. Sigue sin incluir nunca `.conf` ni claves.

### Arreglo: la prueba del correo compila con el clang nuevo de macOS (296)
- `correo_core.c`: la tabla para quitar tildes tenía tamaño fijo y no dejaba sitio al cero final; el clang de Xcode lo da como
  error (`-Wunterminated-string-initialization` con `-Werror`). Ahora el tamaño lo calcula el compilador.

### Correo: correo electrónico con IA, unido al orbe (295)
- Aplicación **Correo** nueva: IMAP y SMTP de verdad sobre el TLS 1.3 de SOMA (nunca sin cifrar), con Gmail, Yahoo, iCloud, GMX,
  Zoho y cualquier dominio. Carpetas, leer, responder, responder a todos, reenviar, archivar, papelera, estrella, adjuntos y atajos de teclado.
- IA dentro de la interfaz: búsqueda en lenguaje natural («facturas de la luz de octubre»), resumen del mensaje, correos y
  respuestas redactados desde una frase y mejoras del borrador (Corregir, Formal, Cercano, Breve, Inglés). Nunca envía nada sin que lo digas.
- Desde el orbe: «escribe un correo a María diciéndole que llego tarde», «envíalo», «¿tengo correos nuevos?», «busca el correo de Juan con las fotos». Orden `correo` en el terminal.
- Avisos: Outlook/Hotmail exige OAuth (SOMA no lo tiene); la clave se guarda solo en el equipo (`/etc/correo.conf`, sin cifrar).

### La bienvenida del orbe y la ayuda rápida, F1 (294)
- Al arrancar, el orbe saluda y una tarjeta enseña consejos cortos (tecla Windows: aplicaciones y reloj; mantén Alt: hablar;
  escribir y pulsar Intro…). No bloquea nada: el usuario experto puede trabajar desde el segundo cero.
- **F1** vuelve a mostrar la ayuda en cualquier momento.

### Las comprobaciones de los parches 286 a 292 en macOS (293)
- Sin símbolos débiles (el enlazador de macOS no los admite) y las pruebas que necesitan las cabeceras del núcleo solo se exigen en Linux x86_64.

### Dibujar en Paint con la IA (292)
- Botón **IA** en Paint y frases desde el orbe («dibújame un faro al atardecer»): el modelo dibuja paso a paso y el dibujo se retoca o se quita con Ctrl+Z.

### Botón derecho para cerrar una aplicación en órbita (291)
- Clic derecho sobre un icono en órbita la cierra al momento; si tiene cambios sin guardar, pide un segundo clic.

### SOMA habla mientras programa, y webs con JavaScript desde el orbe (290)
- Avisos de voz durante el programador y un resumen final para personas; «hazme una web» crea una página HTML con su JavaScript.

### PDF, LaTeX e imágenes dentro del navegador (289)
- Visor de documentos en la pestaña: PDF con el lector de SOMA, `.tex` compuesto con el motor LaTeX, e imágenes WebP locales.

### Investigación y búsquedas en internet de verdad (288)
- Orden `investiga`: varias búsquedas, lectura de hasta 5 páginas y un informe con fuentes citadas; la voz no lee las citas.

### El orbe ya no va a tirones mientras SOMA trabaja (287)
- La consola deja de escribir en la pantalla VGA con el escritorio activo y el hilo del agente cede la CPU al escribir mucho.

### YouTube con el modelo de verdad, pestaña IA nueva y vídeos desde el orbe (286)
- El modelo local sin clave también funciona en YouTube y el chat; conversación con el vídeo, minutos que se pulsan y atajos.

### La aplicación SOMA: un chat moderno con el agente (285)
- Chat al estilo de Open WebUI: historial agrupado, búsqueda, Markdown, adjuntos, editar y regenerar, y lectura en voz alta.

<!-- SOMA_284_GITHUB -->
### Listo para publicar en GitHub (284)
- Entradas del historial y sección del README para la memoria nueva (277–283); documentación de los parches 281 y 282 (en `docs/PARCHE_284_GITHUB.md`).
- `.gitignore`: no se suben `memoria-export/` ni los `.smem` (llevan tu memoria personal, que exporta `tools/exporta_memoria.py`).
- Las claves falsas de la prueba del exportador se arman al ejecutarla: escritas enteras, el detector de secretos de
  `subir_github.py` no dejaba subir el repositorio.

<!-- SOMA_283_MEMORIA_APP -->
### La aplicación de Memoria, rehecha: monitor en vivo, explorador, búsqueda y cartuchos (283)
- La ventana **Memoria** pasa a ser la consola de la memoria de SOMA con el aspecto de memorIA, en seis pestañas: **Monitor** (hechos,
  cola de extracción, ciclo de sueño, las 9 tareas con la que corre iluminada, últimos hechos aprendidos, registro de mantenimiento y
  servidor de embeddings), **Explorador** (árbol de temas; ver, editar, archivar, restaurar, borrar, copiar y mover recuerdos; crear,
  renombrar, describir y quitar ramas), **Buscar** (por significado), **Cartuchos**, **Ajustes** y **Archivos** (los de los agentes).
  Se repinta sola cuando la memoria cambia; lo que tarda lo hace el hilo de la memoria y la pantalla no se congela.
- **Cartuchos** como en memorIA: el primario aprende; hasta 3 más se conectan en solo lectura y el agente los consulta a la vez.
  Crear, conectar, hacer primario, clonar, exportar e importar `.smem`, vaciar y borrar; copiar y mover recuerdos o ramas entre ellos.
  El Principal sigue en `/memoria/` (sin migrar nada). También `memoria cartucho ...` en el terminal.
- **Borrar de verdad** (`memoria borrar ID`): quita el recuerdo, rehace la memoria sin huecos y libera disco y RAM.
- Arreglado (del 279): al podar una rama vacía, lo archivado seguía apuntando a ella y aparecía dentro del tema que reutilizara el hueco.
- Pruebas: `test_memoria_283` (89), el pegamento con cartuchos (225) y `test_memview_283` (128), que dibuja la ventana de verdad en el
  anfitrión y pulsa sus botones; con una carpeta como argumento guarda capturas. Detalles: `docs/PARCHE_283_APP_MEMORIA.md`.

<!-- SOMA_282_ZONA_BAJA -->
### La imagen del núcleo vuelve a enlazar: zona baja reservada de 48 MiB (282)
- El núcleo ya pesaba unos 29,3 MiB y el enlazador exigía como mucho 30. `PMMC_LOWER` pasa de 32 a 48 MiB y los dos `ASSERT` de
  `linker_x86_64.ld` a 46 MiB (imagen) y 48 MiB (virtqueue), como hizo el 202 de 16 a 32. Pruebas del PMM sin cambios.

<!-- SOMA_281_ARREGLOS -->
### Las pruebas de la memoria pasan en macOS (281)
- Usaban `ASAN_OPTIONS=detect_leaks=1` y LeakSanitizer no existe en macOS; ahora `detect_leaks=0` como el resto.

<!-- SOMA_280_IMPORTAR -->
### Tu memoria de memorIA entra en SOMA, y la agenda en el contexto (280)
- `tools/exporta_memoria.py` (se ejecuta en el Mac): lee `memoria.db` en solo lectura, se salta lo que parece un secreto y escribe
  archivos `.smem`; `--servir` los ofrece en la red local con una clave de un solo uso.
- En SOMA, `memoria importar RUTA.smem`: hechos, sustituciones, árbol con descripciones, mensajes y vectores bge-m3; no duplica.
- Cuando se pregunta por planes, citas o fechas, el contexto lleva la agenda de los próximos 7 días.

<!-- SOMA_279_SUENO -->
### Árbol de temas y ciclo de sueño de la memoria (279)
- Los recuerdos se ordenan en temas de 3 niveles (`memoria arbol`); con SOMA en calma, las 9 tareas de memorIA (reparar, embeber,
  olvidar, ordenar, describir, fusionar, rescatar, partir y unir ramas) se ejecutan por turnos (`memoria sueno [N]`).
- Curva del olvido de memorIA en enteros; lo olvidado vuelve solo si una consulta lo trae.

<!-- SOMA_278_EMBEDDINGS -->
### El servidor de embeddings se arranca desde SOMA (278)
- `memoria servidor [estado|arrancar|parar]`: bge-m3 en la CPU del servidor de IA (puerto 8089), con el mismo lanzador que el LLM
  e independiente de él.

<!-- SOMA_277_MEMORIA -->
### La memoria de memorIA dentro de SOMA (277)
- Sustituye a la memoria antigua del agente: hechos atómicos extraídos de la charla (con sustitución temporal: «vivía en Madrid» →
  «vive en Bilbao»), mensajes literales, búsqueda por significado con embeddings (por palabras si no hay servidor) y regla de atribución.
- Orden `memoria` (`estado`, `recordar`, `buscar`, `ver`, `olvidar`, `config`, `importar`...). Se guarda en `/memoria` por fragmentos.

<!-- SOMA_267_DISCOS -->
### El pendrive en cuatro particiones: sistema, DATOS, MODELOS y USUARIO (267)
- **SOMA** (FAT32, 1 GiB, el sistema), **DATOS** (formato de SOMA, 2 GiB: configuración, memoria, programas instalados),
  **MODELOS** (exFAT, 16 GiB, menos en pendrives pequeños) y **USUARIO** (exFAT, el resto, con `Documentos`, `Imágenes`, `Audio`,
  `Vídeos`, `Descargas` y `Proyectos`). Todo en Python puro: `tools/soma_disco.py` (MBR, FAT32, exFAT, DATOS).
- **Grabar mira primero el pendrive**: si ya es de SOMA solo rehace el sistema (lo demás, idéntico byte a byte); si es de una
  versión anterior conserva todo y lo migra (SOMADATA se queda en su sitio como DATOS; la DATOS FAT32 de macOS pasa a USUARIO; los
  modelos de `SOMA/MODELOS` a `MODELOS/voz`; `SOMADATA.IMG` a DATOS). Lo que cambia de sitio se copia antes al ordenador y, si no
  cabe, no se toca nada. El mismo código en Linux y macOS (`construyeimagen.py --grabar`; `ejecutar.py -t t480s-usb` lo usa).
- **exFAT en el núcleo** (`src/core/exfat.c`, lectura y escritura) y **unidades** `/usuario` (lectura y escritura) y `/modelos`
  (solo lectura desde el VFS) en `src/core/unidades.c`: `cat`, `write`, `cp`, `rm`, `ls`, `mkdir`... funcionan en ellas, y los
  programas de ring 3 las leen con `SYS_OPEN`. Cada unidad solo lee y escribe dentro de su partición.
- **Voz**: los modelos de `MODELOS/voz` se usan directamente desde allí (sin copiarlos a DATOS).
- **Imagen del repositorio** de 5 GiB + 1 MiB (cabe en 8 GB): en un pendrive mayor, el primer arranque agranda MODELOS y USUARIO
  mientras están vacías, a prueba de cortes de luz (probado cortando en cada una de sus 140 escrituras).
- **`tools/datos.py`**: DATOS desde fuera (listar, leer, escribir, borrar, exportar sin secretos, comprobar), con los mismos
  protocolos que el núcleo (diario de ranuras, BIGFS atómico).
- Pruebas (`make pruebas-discos`, dentro de `make test`): exFAT frente a `mkfs.exfat`/`fsck.exfat` y a libexfat, DATOS frente al
  código C real del núcleo, migraciones de pendrives antiguos, agrandado con cortes.
- Arreglado en las pruebas de macOS: `pruebas-lang`/`pruebas-cc` no compilaban desde el 260 (`tests/stubs_lang.c`) y
  `browser-boundary-test` chocaba con las macros de `memcpy` del SDK de Apple.

<!-- SOMA_266_AVISO -->
### El LCM avisa al momento cuando la petición va al modelo de lenguaje (266)
- Antes de una respuesta directa, del agente con herramientas, del programador o del maquetador, el LCM dice **qué pasos se van a
  dar** y **cuánto suele tardar** (media de lo que tardaron las anteriores de ese tipo, guardada en `/etc/lcm.conf`), y pide
  paciencia. Con el saludo delante si lo hubo («Buenas tardes. Voy a...»), varias formulaciones y en castellano para la voz.
- Se ve en el orbe (la respuesta lo sustituye y se revela entera), en la conversación (en ámbar), en el terminal, y se lee en
  voz alta si el orbe lee sus respuestas (la respuesta de verdad corta la lectura). La barra de fases empieza por el LCM.
- **Ajustes → LCM**: avisos encendidos o apagados, y lo que suele tardar cada tipo. Con el LCM apagado, todo como antes.
- Pruebas: `tests/test_aviso_266.c` (33) y `tests/test_orbe_266.c` (9), en `make pruebas-lcm`.

<!-- SOMA_265_CC -->
### Compilador de C dentro de SOMA y el programador en C (265)
- **Orden `cc`**: TinyCC (`third_party/tinycc`, LGPL 2.1) con una biblioteca de C propia (`src/user/cc`), dentro del núcleo y
  ejecutado en ring 3 como Python. `cc hola.c` deja `/workspace/hola.elf`; `cc -run hola.c args` compila y ejecuta;
  `cc -run hola.elf` ejecuta uno ya hecho; varios archivos, `-o`, `-c`, `-w`, `-D`, `-I`, `-heap MiB`.
- **Ventanas desde C** con `#include <soma.h>` (`soma_win_create`, `soma_win_rect`, `soma_win_mouse`, `soma_win_key`...): la misma
  ventana que SOMA-LANG y Python. Archivos del VFS (se escribe solo en `/workspace`), consola, hora del equipo.
- **Biblioteca de C** completa para lo habitual: stdio con `printf`/`scanf` y archivos, stdlib, string, ctype, math (x87), time,
  setjmp, wchar/UTF-8, atómicos. `printf` y `strtod`/`strtof`/`strtold` **exactos como glibc** (idénticos en 240 000 valores al azar).
- **El programador y SOMA Studio escriben C**: botón *C* (o «en C» en la descripción); comprueba con el compilador de verdad,
  ejecuta las pruebas, repara errores de compilación, de lógica y programas que se caen; añade los `#include` que falten.
- Corregido un fallo de TinyCC con `long double` (`save_reg(RC_ST0)` en `x86_64-gen.c`): la batería de TinyCC da lo mismo con y
  sin el cambio. Con el compilador y la biblioteca de SOMA pasan 110 de las 112 pruebas `tests2` de TinyCC.
- Pruebas: `make pruebas-cc` (en `make test`; ejecuta los ELF de SOMA en Linux con un emulador de llamadas al sistema) y
  `tests/prueba_cc_265.py` (QEMU). Detalles: `docs/PARCHE_265_CC.md`.

<!-- SOMA_264_LANG -->
### SOMA-LANG sin límites pequeños: arrays, tablas y cadenas que crecen (264)
- **Arrays de miles de elementos** (antes 48), **miles de arrays y tablas vivos** (antes 48 de cada) y **tablas de hasta 262 144
  entradas** con índice hash. **Cadenas de hasta 1 MB** (antes 16 KB) y 1024 cadenas largas a la vez (antes 24).
- La memoria crece según la necesita el programa, con un **presupuesto de 64 MB por programa**: al pasarse, la operación no se
  hace (como antes al llenar un array), se avisa una vez («[VM] Sin sitio para ...») y el programa sigue; nunca le quita la
  memoria al núcleo. El recolector libera de verdad lo que ya no se usa.
- **1024 variables** (antes 128), **512 funciones** (antes 64), **512 llamadas anidadas** (antes 128), pila de 1024 valores.
- `run` ejecuta programas de **256 KB** (antes 16 KB, con bytecode de hasta 512 KB); el programador y el Studio, de **64 KB** (antes 32 KB).
- Prueba nueva `tests/test_lang_264.c` en `make test` (53 comprobaciones). Detalles: `docs/PARCHE_264_SOMALANG_GRANDE.md`.

<!-- SOMA_262_PUBLICAR: entradas de los parches 257-262 -->
### Publicar sin sorpresas: GitHub, README, exportador para Claude y tamaños del maquetador y el programador (262)
- **`subir_github.py`** reconoce también los parches de un solo archivo (`parche_NNN_*.py`), que no escriben en
  `parches/APLICADOS.txt`: los detecta por su respaldo (`.parche_NNN_respaldo/`) y toma el resumen de su documento, así que el
  commit, la etiqueta y la release los anuncian. Antes de subir pasa las **pruebas rápidas** (`make pruebas-lcm`, unos segundos)
  y se niega a subir respaldos de parches o lo preparado para Claude aunque falte en `.gitignore`.
- **`.gitignore`**: `.parche_*_respaldo/` (con copias de archivos antiguos), `para_claude*.txt` y los exportadores sueltos.
- **`tools/exporta_para_claude.py`**: prepara el `.txt` para una conversación con Claude: lo cambiado desde el último commit
  (por defecto), un tema (`--tema lcm|voz|orbe|llm|agente|maquetador|programador|lang|github|compilar`), archivos concretos o el
  archivo donde se define una función. Sin claves ni binarios.
- **`make pruebas-lcm`**: las pruebas del LCM, el orbe y los tamaños del LLM entran en `make test` y en la CI.
- **Maquetador**: con `llm-limites contexto` el prompt y las notas de investigación crecen (antes 8 KB y 3000 caracteres); la
  respuesta admite lo que diga `llm-limites salida`. Sin configurar nada, igual que antes.
- **Programador**: el prompt pasa de 24 KB a 80 KB (con un programa grande se cortaba, incluida la instrucción final) y el Studio
  abre y respalda programas de hasta 32 KB, como los que ya escribía el programador (antes, de más de 16 KB no se podían modificar).
- README al día: LCM, orbe nuevo, `llm-limites` y el exportador. Detalles: `docs/PARCHE_262_PUBLICAR.md`.

### Arreglo: las pruebas del LCM no enlazaban en macOS (261)
- `gui_fase` (barra de fases del orbe) pasa a ser una definición débil vacía en `lcm_soma.inc`; en el núcleo manda la del orbe.
  No cambia nada en SOMA: solo el banco del 257 en el anfitrión.

### El diálogo con el modelo de lenguaje se ajusta al contexto: `llm-limites` (260)
- Antes, SOMA hablaba con el modelo con búferes fijos: respuesta final de **8 KB** (unos 2000 tokens; lo demás se cortaba sin avisar),
  prompt del agente de 16 KB, historial de 8 KB y, de cada orden ejecutada, el modelo solo veía **400 caracteres**.
- **`llm-limites contexto 250000`** (la ventana de tu modelo) y **`llm-limites salida <tokens>`**: el prompt, el historial, lo que
  el modelo ve de cada orden, la petición y la respuesta HTTP se calculan de ahí. Con 250 000 tokens: prompt de 700 KB, historial
  de 420 KB y 29 000 caracteres de cada orden. `llm-limites` enseña los tamaños; `llm-limites como-antes` vuelve atrás.
- **Sin configurar nada, el agente se comporta igual que antes** (las peticiones salen idénticas byte a byte); solo suben los
  techos: respuesta final de 384 KB, respuesta HTTP de 1 MB. Si algo aún no cabe, termina con «[... respuesta recortada ...]».
- Detalles: `docs/PARCHE_260_LLM_TAMANOS.md`.

### El orbe como teleprompter, con formato y barra de fases (259)
- **Zona de texto fija con letra fija** (Ajustes → LCM → *Letra de las respuestas en el orbe*: compacta, mediana o grande): una
  respuesta larga ya no pasa a letra diminuta. Sube sola mientras se escribe; con el **ratón arriba/abajo** (en el orbe no hay
  puntero) o la **rueda** se recorre a mano, y al llegar al final vuelve a ir sola.
- **Formato**: negrita, cursiva, subrayado, código, títulos, listas, citas, enlaces y **fórmulas** (`$...$`, `$$...$$`: griegas,
  fracciones, raíces, superíndices y subíndices de verdad). El razonamiento va detrás, más tenue.
- **Barra de fases** con iconos vectoriales entre el orbe y la respuesta: LCM, mayordomo, búsqueda, documento, programador, orden,
  pensando, escribiendo, voz y hecho; la de ahora late. Detalles: `docs/PARCHE_259_ORBE_PROMPTER.md`.

### LCM ampliado: 98 órdenes y muchas más formas de pedirlas (258)
- Modelo nuevo (dos redes que votan) entrenado con fórmulas indirectas y educadas («¿serías tan amable de decirme qué archivos
  contienen la palabra casa?»). Examen a ciegas: 79 % exactas, 2 % equivocadas, 0 peligrosas (el 253 daba 42 % y 10 %).
- Órdenes nuevas: **`cerrar`** (`cerrar`, `cerrar todo`, `cerrar navegador`...) y **`contiene <texto>`** (qué archivos lo tienen
  dentro). Detalles: `docs/PARCHE_258_LCM_ORDENES_AMPLIADO.md`.

### El LCM habla con naturalidad (257)
- Saludos y charla sin llamar al modelo de lenguaje, respuestas en una frase pensadas para la voz («Son las cinco y diez»), y
  correcciones: «no era eso» + la orden buena, y lo aprende. Página **Ajustes → LCM** (encendido, voz natural o técnica,
  umbral de seguridad, probar una frase, lo aprendido). Detalles: `docs/PARCHE_257_LCM_NATURAL.md`.

### Esc detiene la misión del agente y la lectura en voz alta (256)
- **Detener al modelo**: con el escritorio o el orbe en foco, **Esc** cancela la misión en marcha (antes no había manera: había que
  esperar al plazo de la petición). El orbe muestra «Esc detiene la misión» mientras trabaja y «Misión detenida.» al cancelar; se puede
  lanzar otra misión enseguida. El hilo del agente se detiene con `thread_kill` (cierra sus conexiones y suelta sus cerrojos).
- **Detener la locución**: **Esc** para la lectura en voz alta desde cualquier aplicación (en el Bloc de notas, después de cerrar
  menús o diálogos; el botón sigue funcionando). Ahora también corta **durante la preparación** (antes solo cuando ya sonaba) y la orden
  `habla texto` usada por el agente. El orbe indica «Esc detiene la lectura en voz alta».
- Límites: cancelar pierde los búferes de trabajo del agente (~200 KiB) y puede dejar a medias un archivo que estuviera escribiendo.
- Toca `gui.c` (dos líneas de Esc), `gui_agente.c`, `gui_cosmos.c` y `voz_sys.[ch]`. Detalles: `docs/PARCHE_256_PARAR.md`.

### Bloc de notas renovado: iconos, deshacer, buscar y revisión (255)
- **Barra con iconos vectoriales** (`sysicon`): nuevo, abrir, guardar, guardar como, deshacer, rehacer, cortar, copiar, pegar, buscar y
  ver en el navegador (iconos nuevos `SI_CUT` y `SI_PASTE`). «Guardar» se resalta cuando hay cambios; con ventana estrecha quedan solo
  iconos; al pasar el ratón la barra de estado explica cada botón. El menú del clic derecho también lleva iconos.
- **Deshacer / rehacer de verdad** (Ctrl+Z, Ctrl+Mayús+Z): las teclas seguidas forman un paso (una palabra), reemplazar una selección
  se deshace de una vez, y funciona con pegar, cortar, Supr y Retroceso.
- **Buscar** (Ctrl+F): barra con campo de texto del sistema (tildes, portapapeles, ratón), coincidencias resaltadas en todo el texto,
  contador «4 de 17», Intro / Mayús+Intro para siguiente / anterior, Esc para cerrar. Si hay una línea seleccionada, es la búsqueda inicial.
- **Fallos corregidos**: «Nuevo» conservaba la ruta del documento anterior (Guardar lo sobrescribía sin avisar) y ahora pide
  confirmación si hay cambios y empieza sin nombre; un documento sin nombre abre «Guardar como» en vez de escribir en
  `/workspace/notas.txt`; el nombre de archivo del cuadro de diálogo admite tildes y ñ y Retroceso quita un carácter UTF-8 entero.
- **Cuadro de archivos** con el mismo aspecto (iconos de carpeta y archivo, tamaños, resaltado al pasar el ratón, botones del sistema).
- Estado: «UTF-8», marcador de documento sin guardar con icono y texto de ayuda cuando el documento está vacío.
- Toca `notepad.c`, `sysicon.[ch]` y `test_iconos_240.c` (ninguno protegido). Detalles: `docs/PARCHE_255_BLOC_NOTAS.md`.

### Maquetador renovado: iconos vectoriales, campos de texto de verdad y revisión (254)
- **Iconos vectoriales** del sistema (`sysicon`) en toda la barra: abrir, recargar, editar, copiar, Documento/Fuente, zoom −/+,
  PDF, HTML, revisar con IA y el destello de «Generar» (iconos nuevos `SI_SPARK` y `SI_SEND`). La barra se adapta al ancho
  (con ventana estrecha quedan solo iconos, y PDF/HTML conservan su texto) y al pasar el ratón se explica cada botón.
- **Descripción para la IA con el campo de texto del sistema**: ya se pueden **escribir tildes, ñ, ¿ ¡** (antes salían rotas: el
  teclado envía UTF-8 y el Maquetador lo trataba como Latin-1), **copiar, cortar y pegar** (Ctrl+C/X/V), mover el cursor con el
  ratón en cualquier punto, **seleccionar arrastrando**, doble clic = palabra, triple = todo. Es de varias líneas (Mayús+Intro
  hace un salto; Intro genera) y crece al escribir.
- **Ruta editable**: se escribe o pega la ruta de un `.tex`/`.html` y Intro lo abre. **Abrir** ahora abre el explorador de
  archivos en la carpeta del documento (antes hacía lo mismo que Recargar).
- Revisión: **Esc** suelta el campo en vez de cerrar la ventana, **Ctrl+C** sin campo copia el código fuente del documento
  (botón **Copiar**), Ctrl+V pega en la descripción, indicador «Página x de n», clic en el carril de la barra de desplazamiento,
  teclas `o` (abrir), `p` (ruta), `g` (descripción). Textos de la interfaz con tildes.
- Toca `gui.c` (portapapeles y Esc del Maquetador), `sysicon.[ch]` y `layout.[ch]`. Detalles: `docs/PARCHE_254_MAQUETADOR.md`.

<!-- SOMA_262_PUBLICAR: el LCM (253) -->
### LCM: SOMA entiende lo que le pides sin el modelo de lenguaje (253)
- Un traductor pequeño (red BiLSTM en enteros, dentro del núcleo) convierte frases como «borra el archivo notas.txt» en órdenes
  del terminal, en milisegundos y sin conexión. Lo que borra, mueve o apaga **siempre pregunta**; lo que no entiende sigue al
  mayordomo o al modelo de lenguaje como antes. Orden `lcm` (`lcm <frase>`, `lcm estado`). Detalles: `docs/PARCHE_253_LCM_ORDENES.md`.

### Transcribir sin equipos externos: el sonido del vídeo, dentro de SOMA (253)
- En **modo nativo**, **Transcribir** (pestaña Texto) ya funciona: SOMA baja el MP4 de 360p de YouTube, **solo se queda con el
  sonido** y lo pasa a whisper. Nada se guarda salvo el WAV temporal `/yt_audio.wav` (16 kHz, mono), que se borra al acabar.
- Un **segundo reproductor de ring 3** (el mismo `vplay.elf`, en modo «solo audio») decodifica el AAC, lo reduce a 16 kHz mono
  y lo entrega por un anillo de salida; el núcleo lo escribe en el almacén BIGFS. **No interrumpe el vídeo** que se está
  viendo: son dos procesos independientes (`vplay_sys.c` ahora tiene dos instancias, `vps_*` para ver y `vpx_*` para extraer).
- Respeta `youtube minutos N` (tope de audio), mira el sitio libre del almacén, enseña el avance (`0:15 de 0:45, 1,1 MiB
  bajados`), se puede cancelar, y si la descarga se corta transcribe lo que llegó. También sirve con `youtube mp4 <dirección>`.
- Prueba nueva en `tests/test_vplay_252.py`: el audio sale igual que el de ffmpeg (relación señal/error > 30 dB).
  Detalles: `docs/PARCHE_253_AUDIO_NATIVO.md`.

### Vídeo nativo y YouTube sin equipos externos (252)
- **YouTube ya no necesita otro equipo**: SOMA busca, pide la ficha y descarga el vídeo hablando directamente con YouTube
  por HTTPS, y lo decodifica él mismo. El servicio `tools/yt_service.py` queda como opción (`youtube modo servicio`).
- **Reproductor nativo** (`vplay.elf`, ring 3): MP4 con H.264 (libavc) y AAC (libxaac), ambos Apache 2.0, en
  `third_party/`. Proceso aislado sin archivos ni red; el vídeo le llega por memoria compartida. Sin tocar `usermode.c`.
- **Cliente HTTPS propio** (`yt_https.c`) sobre el TLS 1.3 del sistema: lee según llega, con `Range` y redirecciones.
- **Límites**: solo 360p (lo único que YouTube da con dirección directa), solo x86_64, y puede romperse cuando YouTube
  cambie (se arregla en `src/apps/yt_inner.h`). En modo nativo aún no hay subtítulos ni transcripción.
- Órdenes: `youtube modo nativo|servicio`, `youtube mp4 <dirección>`, `youtube api`. Herramienta: `tools/sonda_youtube_nativo.py`.
- Pruebas: `test_mp4_252`, `test_yt_inner_252` (respuestas reales de YouTube) y `test_vplay_252.py` (reproductor completo).
  Detalles y lo no verificado: `docs/PARCHE_252_VIDEO_NATIVO.md`.

### YouTube con IA: buscador en lenguaje natural, transcripción y preguntas (251)
- **Buscador con IA**: con el interruptor **IA** de la barra encendido, en la caja se escribe lo que se quiere ver con
  palabras normales. El LLM del sistema lo convierte en hasta tres búsquedas de YouTube, los resultados se juntan numerados
  y sin repetidos, y el LLM escribe un resumen de lo encontrado con sus recomendaciones. Sin LLM se busca el texto tal cual.
- **Descripciones**: el principio en cada tarjeta de resultado y la ficha completa (fecha, vistas, subtítulos disponibles) en
  la pestaña **Info** del reproductor.
- **Transcripción** (pestaña **Texto**): con **whisper** en SOMA (el servicio entrega el sonido en WAV de 16 kHz, se guarda
  en el almacén y pasa por `voz_run`) o con los **subtítulos de YouTube**. Texto con marcas `[m:ss]`: pulsar una línea salta
  a ese momento. Se guarda por vídeo (`/yt_<id>.txt`).
- **Resumen y preguntas** (pestaña **IA**): el LLM responde sobre el contenido del vídeo con la transcripción como contexto.
- Servicio: `/yt/info`, `/yt/audio`, `/yt/subs` y descripción en `/yt/search` (**hay que reiniciar `tools/yt_service.py`**).
- Órdenes: `youtube ia on|off`, `ver N`, `transcribir`, `subtitulos`, `resumen`, `pregunta <texto>`, `idioma`, `minutos`, `contexto`.
- Pruebas nuevas: `tests/test_youtube_ia_251.c` y `tests/test_yt_service_251.py`. Detalles: `docs/PARCHE_251_YOUTUBE_IA.md`.

### Repaso antes de publicar (252)
- README al día: el Maquetador ya no es «#SOMA-LAYOUT v1», la sección de agentes describe los caminos actuales y desaparece la
  mención a un streaming del LLM que no existe.
- `.gitignore`: voces y modelos (`*.onnx`, `*.gguf`), carpeta de pruebas de voz, copias de los parches (`.soma_parches/`),
  audios, PDF y una posible `llm.conf` con claves.
- Ajustes → Acerca de: las voces de Piper en los créditos de terceros.

### Caminos de los agentes más rápidos y correctos (251)
- **Respuesta directa**: conversación, redacciones, explicaciones y traducciones se resuelven con UNA llamada al modelo, en texto
  normal (sin JSON, sin herramientas, sin el catálogo de órdenes en el prompt). Si el modelo necesita herramientas lo dice
  (`[[HERRAMIENTAS]]`) y se sigue por el bucle del agente. Medido con un modelo simulado a 40 tokens/s: «escribe una redacción de
  20 frases sobre la Primera Guerra Mundial» pasa de 15 llamadas y 320 s a 1 llamada y 17 s.
- **Rutas por palabras completas** (`src/agent/agent_ruta.c`): «crea un programa…» y «dime un resumen del programa Erasmus» ya
  no acaban enseñando la RAM («programa» contenía «ram»); el programador va antes que el Mayordomo; «describe» ya no cuenta como
  «escribe» ni «creación» como «crea».
- **Guardar solo si se pide** (guárdalo, en un archivo, una ruta o nombre.txt): SOMA guarda el texto ya escrito; antes rechazaba
  la respuesta y el modelo tenía que repetir la redacción en un `write` y otra vez al final.
- **Búsqueda web de verdad** (`src/agent/busca_web.c`): Wikipedia (API) y DuckDuckGo ligero por HTTPS, 8 s como mucho cada una, y
  aviso inmediato sin red. Antes solo había un proxy en el puerto 8095 de la puerta de enlace: en el portátil era el router y
  cada búsqueda podía esperar 30-45 s para fallar.
- **Delegación**: nadie delega en sí mismo ni en un agente que ya trabaja en la misión (se vio Buscador → Buscador → Buscador);
  profundidad 2; el resultado que vuelve es el real del subagente (antes la pizarra: 500 caracteres o el dictamen de otro).
- **Maquetador solo con formato**: «redacta un informe breve» se contesta en el orbe; el maquetador (HTML, revisión y PDF) se
  usa al pedir PDF, maquetar, presentación, revista...
- **Sin contaminación**: la pizarra solo se ve dentro de una delegación; la memoria corta guarda un extracto (600 caracteres).
- **Tiempos**: 240 s por paso (60 s cortaban textos largos y se regeneraba todo con otro resumen); si el modelo no responde no
  se pide otro resumen; 8 pasos como mucho (antes 14). El prompt pone primero lo que no cambia (el servidor lo reutiliza).
- **Traza** de cada llamada al modelo: quién llama, milisegundos, tamaños y los tokens que diga el servidor (también razonamiento
  oculto y tokens reutilizados) en `/agent/traza.txt`; orden `traza`.
- Las tildes escapadas en JSON (`\u00e1`) se decodifican («está», no «estu00e1»).
- Pruebas nuevas: `tests/test_rutas_251.c` (29 casos de rutas) y `tests/test_busca_251.c` (lectura de Wikipedia y DuckDuckGo).

### YouTube: buscar y ver vídeos dentro de SOMA (250)
- **Aplicación YouTube** (`src/apps/youtube.c`, icono nuevo en el escritorio, orden `youtube [búsqueda]`): caja de búsqueda,
  resultados con miniatura, duración y canal, y un reproductor propio con pausa, saltos de 10 s, barra de progreso, calidad
  (240/360/480p), «anterior/siguiente», lista «A continuación» y paso automático al siguiente vídeo. Teclas: Espacio, flechas,
  `n`/`p`, `q` (calidad), `/` (buscar), Esc (volver a los resultados).
- **Servicio del anfitrión** (`tools/yt_service.py`, lo arranca `ejecutar.py` en 127.0.0.1:8097): SOMA no decodifica H.264/AV1
  ni puede resolver las direcciones de YouTube, así que el Mac/PC busca con yt-dlp, sirve las miniaturas en JPEG y **emite el
  vídeo convertido con ffmpeg a AVI (MJPG + PCM) mientras SOMA lo reproduce**. Cabecera AVI propia con la duración ya puesta;
  si YouTube rechaza a ffmpeg, plan B por tubería desde yt-dlp. `--demo` genera vídeos de prueba sin YouTube ni yt-dlp.
- **Reproducción mientras llega** (`src/apps/media.c`, `media_play_stream`): el motor de SOMA Media lee de una fuente que crece.
  Sin datos espera («Cargando») y reanuda con unos 2 s de margen en vez de dar el vídeo por terminado; el reloj no corre durante
  la espera; la pausa recoloca el audio con puntos de control en vez de recorrer el archivo desde el principio.
- **Nada pasa por el disco**: el vídeo vive en una ventana de 16 MiB de memoria (`yt_ring`, `src/apps/yt_proto.h`) que suelta
  lo ya visto; en pausa la descarga se detiene sola. Saltar es pedir el vídeo desde otro segundo.
- **En un PC real** (T480s): `python3 tools/yt_service.py --bind 0.0.0.0 --clave X` en otro equipo de la red y, en SOMA,
  `youtube servidor <ip>` y `youtube clave X` (o escribir `servidor <ip>` en la caja de búsqueda). Se guarda en
  `/etc/secreto/youtube.conf`, que los agentes no pueden leer.
- `tcp_abort` (`src/net/tcp.c`): cierre inmediato de una descarga cancelada (antes, hasta 3 s de espera).
- Pruebas nuevas: `tests/test_youtube_250.c` (protocolo, configuración y la ventana de memoria con un reproductor simulado,
  más datos al azar) y `tests/test_yt_service_250.py` (el AVI emitido pasa por el lector y el decodificador JPEG de SOMA;
  camino real con un yt-dlp de pega y un «YouTube» local: cabeceras, salto y plan B).

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
