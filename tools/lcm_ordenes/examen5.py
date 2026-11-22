# Examen ciego 5: traductor de peticiones en lenguaje natural a órdenes de SOMA.
# E = [(peticion, esperada)]; esperada = orden exacta (alternativas con "||") o "LLM".

E = [
    # ---------------- Órdenes del alcance ----------------
    # ajustes
    ("Abre los ajustes, por favor", "ajustes"),
    ("quiero cambiar la transparencia de las ventanas, abreme la configuracion", "ajustes"),
    # audio vol {N}
    ("Pon el volumen al 40", "audio vol 40"),
    ("volumen al 15 xfa", "audio vol 15"),
    # bateria
    ("bateria?", "bateria"),
    ("Disculpe, ¿sería tan amable de decirme cuánta batería le queda al portátil?", "bateria"),
    # brillo +
    ("sube un poco el brillo, que no veo nada", "brillo +"),
    ("mas brillo porfa", "brillo +"),
    # brillo -
    ("Baja el brillo, que me deslumbra", "brillo -"),
    ("la pantalla está muy fuerte, bájale un poquito el brillo", "brillo -"),
    # brillo {N}
    ("pon el brillo al 70", "brillo 70"),
    ("Estoy en la terraza y no veo bien; deja el brillo al 100", "brillo 100"),
    # cam foto
    ("hazme una foto con la webcam", "cam foto"),
    ("saca una foto con la cámara ahora mismo, sin abrir la aplicación", "cam foto"),
    # camara
    ("abre la cámara", "camara"),
    ("Quiero grabar un vídeo para mi nieta, ¿me abre usted la aplicación de la cámara?", "camara"),
    # captura foto
    ("haz una captura de pantalla", "captura foto"),
    ("pantallazo xfa", "captura foto"),
    # cat {A}
    ("enséñame lo que pone en receta.txt", "cat receta.txt"),
    ("muestra el contenido de config.ini", "cat config.ini"),
    # checkpoint
    ("haz una copia de seguridad del sistema", "checkpoint"),
    ("He estado toda la tarde ordenando las carpetas y no quiero perderlo; guarda una instantánea ahora", "checkpoint"),
    # clear
    ("limpia la pantalla", "clear"),
    ("borra todo lo q hay escrito en la pantalla de la terminal", "clear"),
    # cp {A} {B}
    ("copia foto_playa.jpg a la carpeta vacaciones", "cp foto_playa.jpg vacaciones"),
    ("haz una copia de presupuesto.txt que se llame presupuesto_viejo.txt", "cp presupuesto.txt presupuesto_viejo.txt"),
    # date
    ("¿qué hora es?", "date"),
    ("que dia es hoy", "date"),
    # descargar {U}
    ("descarga https://ejemplo.org/mapa_rutas.zip", "descargar https://ejemplo.org/mapa_rutas.zip"),
    ("bájame este archivo: http://descargas.huertadigital.es/manual.pdf", "descargar http://descargas.huertadigital.es/manual.pdf"),
    # df
    ("¿cuánto espacio libre queda en el almacenamiento?", "df"),
    ("cuanto almacenamiento me queda", "df"),
    # dmesg
    ("enséñame el registro del núcleo desde el arranque", "dmesg"),
    ("Al encender ha salido un mensaje raro; muéstrame los mensajes del kernel", "dmesg"),
    # dns {H}
    ("resuelve el dominio wikipedia.org", "dns wikipedia.org"),
    ("¿qué IP tiene elmundo.es?", "dns elmundo.es"),
    # du
    ("¿cuánto ocupa cada archivo?", "du"),
    ("Quiero saber qué archivos ocupan más espacio en disco", "du"),
    ("uso de disco por archivo, rapido", "du"),
    # energia
    ("¿qué perfil de energía tengo puesto?", "energia"),
    ("¿A qué temperatura va el procesador y cuánto consume?", "energia"),
    ("como va la energia del procesador", "energia"),
    # find {T}
    ("busca un archivo que se llame factura", "find factura"),
    ("no encuentro las notas de clase, busca algún archivo con 'apuntes' en el nombre", "find apuntes"),
    # grabadora
    ("abre la grabadora", "grabadora"),
    ("quiero grabar una nota de voz con el micro", "grabadora"),
    # grep {T} {A}
    ("busca la palabra error dentro de registro.log", "grep error registro.log"),
    ("¿en qué líneas de clientes.csv sale Murcia?", "grep Murcia clientes.csv"),
    # habla {T}
    ("di en voz alta: la cena está lista", "habla la cena está lista"),
    ("Léeme en voz alta esta frase, por favor: feliz cumpleaños, Carmen", "habla feliz cumpleaños, Carmen"),
    # head -n {N} {A}
    ("enséñame las 10 primeras líneas de diario.txt", "head -n 10 diario.txt"),
    ("las 5 primeras lineas de datos.csv", "head -n 5 datos.csv"),
    # history
    ("¿qué órdenes he escrito antes?", "history"),
    ("muestrame el historial de comandos", "history"),
    # hostname
    ("¿cómo se llama este equipo?", "hostname"),
    ("nombre del ordenador?", "hostname"),
    # imagen {A}
    ("abre la imagen gato.png", "imagen gato.png"),
    ("enséñame la foto boda_2019.jpg", "imagen boda_2019.jpg"),
    # kill {N}
    ("mata el proceso 42", "kill 42"),
    ("El hilo 17 se ha quedado colgado, termínalo", "kill 17"),
    # ls
    ("¿qué archivos tengo?", "ls"),
    ("lista los archivos", "ls"),
    # ls {D}
    ("qué hay en la carpeta fotos", "ls fotos"),
    ("enseñame lo q hay dentro de deberes", "ls deberes"),
    # mkdir {D}
    ("crea una carpeta llamada recetas", "mkdir recetas"),
    ("Hazme una carpeta nueva que se llame viaje_roma", "mkdir viaje_roma"),
    # monitor
    ("abre el monitor del sistema", "monitor"),
    ("quiero ver la CPU y los procesos en gráficas", "monitor"),
    # mv {A} {B}
    ("cambia el nombre de borrador.txt a final.txt", "mv borrador.txt final.txt"),
    ("mueve cancion.wav a la carpeta musica", "mv cancion.wav musica"),
    # nano {A}
    ("quiero editar lista.txt", "nano lista.txt"),
    ("abre notas.txt en el editor de texto para modificarlo", "nano notas.txt"),
    # navegador
    ("abre el navegador", "navegador"),
    ("abre el navegdor", "navegador"),
    # navegador {U}
    ("abre elpais.com en el navegador", "navegador elpais.com||navegador https://elpais.com"),
    ("entra en https://www.aemet.es", "navegador https://www.aemet.es"),
    # paint
    ("abre el Paint", "paint"),
    ("quiero dibujar algo, abre el programa de dibujo", "paint"),
    # pdf {A}
    ("abre el pdf contrato_alquiler.pdf", "pdf contrato_alquiler.pdf"),
    ("lee manual_lavadora.pdf en el lector de PDF", "pdf manual_lavadora.pdf"),
    # ping {H}
    ("haz ping a 8.8.8.8", "ping 8.8.8.8"),
    ("comprueba si responde el equipo 192.168.1.40", "ping 192.168.1.40"),
    # poweroff
    ("apaga el ordenador", "poweroff"),
    ("apga el pc ya", "poweroff"),
    # ps
    ("¿qué procesos se están ejecutando?", "ps"),
    ("lista los hilos del kernel", "ps"),
    # ram
    ("¿cuánta memoria RAM me queda libre?", "ram"),
    ("ram?", "ram"),
    # reboot
    ("reinicia el ordenador", "reboot"),
    ("Se ha quedado todo muy lento desde esta mañana; reinicie el equipo, si es tan amable", "reboot"),
    # rm {A}
    ("borra el archivo viejo.txt", "rm viejo.txt"),
    ("elimina prueba2.py", "rm prueba2.py"),
    # rmdir {D}
    ("borra la carpeta vacía temporal", "rmdir temporal"),
    ("elimina el directorio pruebas, que ya está vacío", "rmdir pruebas"),
    # search {T}
    ('busca en internet "receta de salmorejo"', "search receta de salmorejo"),
    ("busca en internet: horario del museo del Prado", "search horario del museo del Prado"),
    # stat {A}
    ("dame los metadatos de presupuesto.ods", "stat presupuesto.ods"),
    ("cuánto pesa y de qué tipo es foto1.png", "stat foto1.png"),
    # status
    ("¿estoy conectado a la red? ¿qué IP tengo?", "status"),
    ("estado de la red", "status"),
    # studio
    ("abre SOMA Studio", "studio"),
    ("abre el editor de código", "studio"),
    ("quiero programar un rato, abreme el studio", "studio"),
    # sysinfo
    ("dame un resumen del sistema", "sysinfo"),
    ("resumen rápido: uptime, RAM, almacenamiento y red", "sysinfo"),
    # tail -n {N} {A}
    ("las últimas 20 líneas de servidor.log", "tail -n 20 servidor.log"),
    ("enséñame el final de diario.txt, las 3 últimas líneas", "tail -n 3 diario.txt"),
    # teclado es
    ("pon el teclado en español", "teclado es"),
    ("no me salen la ñ ni las tildes, cambia la distribución del teclado a español", "teclado es"),
    # teclado us
    ("pon el teclado en inglés americano", "teclado us"),
    ("teclado us porfa", "teclado us"),
    # terminal
    ("abre una terminal", "terminal"),
    ("abre la consola de comandos en una ventana", "terminal"),
    # touch {A}
    ("crea un archivo vacío que se llame pendientes.txt", "touch pendientes.txt"),
    ("crea el archivo index.html vacío", "touch index.html"),
    # tree
    ("enséñame el árbol de carpetas", "tree"),
    ("muestra la jerarquía de archivos", "tree"),
    # uname -a
    ("¿qué versión de SOMA tengo?", "uname -a"),
    ("version y arquitectura del sistema completa", "uname -a"),
    # uptime
    ("¿cuánto tiempo lleva encendido el ordenador?", "uptime"),
    ("cuanto llevo con el pc encendido", "uptime"),
    # wc {A}
    ("cuántas palabras tiene redaccion.txt", "wc redaccion.txt"),
    ("cuenta las líneas de lista_invitados.txt", "wc lista_invitados.txt"),
    # whoami
    ("¿con qué usuario estoy?", "whoami"),
    ("quien soy en este pc", "whoami"),
    # wifi buscar
    ("busca redes wifi", "wifi buscar"),
    ("¿qué redes wifi hay por aquí?", "wifi buscar"),
    # wifi conectar {R}
    ("conéctate a la red Biblioteca_Municipal", "wifi conectar Biblioteca_Municipal"),
    ("conecta al wifi CafeteriaSol, es abierta", "wifi conectar CafeteriaSol"),
    # wifi conectar {R} {K}
    ("conéctate al wifi Casa_Luis con la clave perro77", "wifi conectar Casa_Luis perro77"),
    ("Estoy en casa de mi hermana: la red es MOVISTAR_8A2F y la contraseña es girasol2024, conéctame", "wifi conectar MOVISTAR_8A2F girasol2024"),
    # wifi desconectar
    ("desconéctate del wifi", "wifi desconectar"),
    ("quita el wifi", "wifi desconectar"),
    # write {A} "{T}"
    ("escribe en compra.txt: leche y pan", 'write compra.txt "leche y pan"'),
    ("guarda en recordatorio.txt la frase 'llamar al dentista el martes'", 'write recordatorio.txt "llamar al dentista el martes"'),
    # youtube {T}
    ("pon en YouTube recetas de croquetas", "youtube recetas de croquetas"),
    ("busca en youtube tutorial de ganchillo", "youtube tutorial de ganchillo"),
    # cerrar {P}
    ("cierra el navegador", "cerrar navegador"),
    ("cierra el Paint", "cerrar paint"),
    ("quita YouTube", "cerrar youtube"),
    ("cierra el monitor xfa", "cerrar monitor"),
    ("Cuando pueda, cierre usted la grabadora, que ya he terminado", "cerrar grabadora"),
    ("cierra la terminal", "cerrar terminal"),
    ("cierra la camara", "cerrar camara||cerrar cámara"),
    ("ya he leído el contrato, cierra el pdf", "cerrar pdf"),
    ("cierra el reproductor", "cerrar reproductor"),
    ("cierra los ajustes", "cerrar ajustes"),
    # cerrar
    ("cierra eso", "cerrar"),
    ("cierra la ventana de delante", "cerrar"),
    ("Cierra esta ventana, por favor", "cerrar"),
    # cerrar todo
    ("cierra todas las ventanas", "cerrar todo"),
    ("cierralo todo q viene mi madre", "cerrar todo"),
    ("Quiero dejar el escritorio limpio: cierre todas las aplicaciones abiertas", "cerrar todo"),
    # latex
    ("abre el maquetador", "latex"),
    ("abre el editor de LaTeX", "latex"),
    ("quiero escribir unas fórmulas con LaTeX, abre el maquetador", "latex"),
    # latex {A}
    ("abre tesis.tex en el maquetador", "latex tesis.tex"),
    ("maqueta articulo.tex", "latex articulo.tex"),
    ("Tengo que repasar el trabajo de física; abre trabajo_fisica.tex con LaTeX", "latex trabajo_fisica.tex"),
    # media gui
    ("abre el reproductor", "media gui"),
    ("abre el reproductor multimedia", "media gui"),
    ("quiero ver uno de los vídeos que tengo guardados, abre el reproductor", "media gui"),
    # media pause
    ("pausa el vídeo", "media pause"),
    ("pon en pausa la música del reproductor", "media pause"),
    ("pausa xfa q me llaman", "media pause"),
    # media stop
    ("para el vídeo", "media stop"),
    ("detén la reproducción", "media stop"),
    ("Ya está bien de música por hoy, pare usted el reproductor", "media stop"),
    # habla parar
    ("cállate", "habla parar"),
    ("deja de hablar", "habla parar"),
    ("para la voz, que me está leyendo todo el texto", "habla parar"),
    # studio {A}
    ("abre juego.soma en el Studio", "studio juego.soma"),
    ("edita web.py con el editor de código", "studio web.py"),
    ("quiero seguir con mi programa calculadora.soma, ábremelo en SOMA Studio", "studio calculadora.soma"),
    # energia rendimiento
    ("pon el procesador a máximo rendimiento", "energia rendimiento"),
    ("modo rendimiento q voy a jugar", "energia rendimiento"),
    ("Voy a compilar un proyecto enorme; pon el perfil de energía en rendimiento", "energia rendimiento"),
    # energia equilibrado
    ("pon el perfil de energía equilibrado", "energia equilibrado"),
    ("ni ahorro ni rendimiento, déjalo en equilibrado", "energia equilibrado"),
    ("modo equilibrado", "energia equilibrado"),
    # energia ahorro
    ("pon el modo ahorro de energía", "energia ahorro"),
    ("me queda poca batería y no tengo cargador, que el procesador gaste lo menos posible", "energia ahorro"),
    ("ahorro de energia porfa", "energia ahorro"),
    # wifi guardadas
    ("¿qué redes wifi tengo guardadas?", "wifi guardadas"),
    ("lista las redes wifi que recuerda el ordenador", "wifi guardadas"),
    ("wifis guardadas?", "wifi guardadas"),
    # disco
    ("¿dónde se guardan mis archivos?", "disco"),
    ("¿Mis archivos se guardan en el pendrive o solo en la RAM?", "disco"),
    ("en que disco se guarda todo", "disco"),
    # informe
    ("crea un informe de arranque y hardware", "informe"),
    ("El técnico me ha pedido un informe del hardware para ver qué falla; genéralo, por favor", "informe"),
    ("haz el informe del sistema para compartir", "informe"),
    # somafetch
    ("enséñame la ficha del sistema", "somafetch"),
    ("pon el logo de SOMA en ASCII con los datos del sistema", "somafetch"),
    ("quiero presumir de sistema en clase, sácame la ficha esa con arte ASCII", "somafetch"),
    # creador
    ("¿quién ha creado SOMA?", "creador"),
    ("quien hizo este sistema operativo", "creador"),
    ("Me gustaría saber quién es el autor de SOMA", "creador"),
    # du {D}
    ("¿cuánto ocupa la carpeta fotos?", "du fotos"),
    ("uso de disco de descargas", "du descargas"),
    ("Antes de pasarlo al pendrive necesito saber cuánto pesa la carpeta proyectos", "du proyectos"),
    # cpus
    ("¿cuántos núcleos tiene el procesador?", "cpus"),
    ("cuantos cores tengo", "cpus"),
    ("enséñame los núcleos de la CPU", "cpus"),
    # fs-sync
    ("guarda en disco lo que esté pendiente", "fs-sync"),
    ("sincroniza el sistema de archivos con el disco", "fs-sync"),
    ("Voy a desenchufar el pendrive en un momento; asegúrate antes de que todo lo pendiente quede escrito en el disco", "fs-sync"),
    # rollback
    ("restaura la copia de seguridad", "rollback"),
    ("he borrado algo sin querer, vuelve a la última copia de seguridad", "rollback"),
    ("recupera el snapshot", "rollback"),
    # usb
    ("¿qué tengo conectado por USB?", "usb"),
    ("lista los dispositivos usb", "usb"),
    ("He enchufado un ratón y no sé si lo detecta; muéstrame lo conectado por USB", "usb"),
    # escritorio reiniciar
    ("recoloca los iconos del escritorio", "escritorio reiniciar"),
    ("se me han descolocado los iconos, ponlos en su sitio", "escritorio reiniciar"),
    ("reinicia el escritorio", "escritorio reiniciar"),
    # python {A}
    ("ejecuta juego.py", "python juego.py"),
    ("corre el script de python limpiar_fotos.py", "python limpiar_fotos.py"),
    ("Mi nieto me ha dejado un programa llamado adivina.py; ¿podría ejecutarlo con Python?", "python adivina.py"),
    # run {A}
    ("ejecuta el script backup.soma en la terminal, sin ventana", "run backup.soma"),
    ("corre contador.soma aquí en la consola", "run contador.soma"),
    ("ejecuta saludo.soma", "run saludo.soma||app saludo.soma"),
    # app {A}
    ("lanza la aplicación reloj.soma en una ventana", "app reloj.soma"),
    ("abre la app tetris.soma", "app tetris.soma"),
    ("He hecho una agenda en SOMA-LANG, agenda.soma; ábrela como aplicación", "app agenda.soma"),
    # voz {A}
    ("pásame a texto el audio entrevista.wav", "voz entrevista.wav"),
    ("transcribe reunion.wav", "voz reunion.wav"),
    ("Grabé la clase de ayer en clase_historia.wav; ¿me la puede transcribir?", "voz clase_historia.wav"),
    # dhcp
    ("pide una IP nueva al router", "dhcp"),
    ("renueva la ip", "dhcp"),
    ("Me he cambiado de router y no tengo IP; pide la configuración por DHCP", "dhcp"),
    # memview
    ("abre el visor de memoria de los agentes", "memview"),
    ("enséñame el árbol de memoria de los agentes", "memview"),
    ("quiero ver qué recuerdan los agentes, abre memview", "memview"),

    # ---------------- No son órdenes: trampas con vocabulario del ordenador ----------------
    ("¿Es malo cerrar la tapa del portátil sin apagarlo antes?", "LLM"),
    ("¿Por qué la batería del móvil dura menos en invierno?", "LLM"),
    ("¿Qué diferencia hay entre la memoria RAM y el disco duro?", "LLM"),
    ("Explícame qué es el wifi 6 y si merece la pena", "LLM"),
    ("¿Me recomiendas algún canal de YouTube para aprender a cocinar?", "LLM"),
    ("Mi abuela dice que el wifi da dolor de cabeza, ¿eso es verdad?", "LLM"),
    ("¿Cómo organizo mejor los archivos de la empresa en carpetas? Dame consejos", "LLM"),
    ("Escríbeme un correo para mi jefe diciendo que mañana abro la tienda una hora más tarde", "LLM"),
    ("¿Qué energía es más barata en España, la solar o la eólica?", "LLM"),
    ("¿Cómo se hace una captura de pantalla en un iPhone?", "LLM"),
    ("¿Qué opinas de Paint como herramienta para hacer arte digital?", "LLM"),
    ("¿Cómo se dice 'apagar el ordenador' en inglés?", "LLM"),
    ("Tengo muy mala memoria para los nombres, ¿algún truco para recordarlos?", "LLM"),
    ("¿Qué es un proceso en informática? Explícamelo como si tuviera diez años", "LLM"),
    ("Redacta un aviso para la comunidad de vecinos: el martes cortan el agua de 9 a 12", "LLM"),
    ("¿Merece la pena comprar un portátil con 16 GB de RAM o con 8 me basta?", "LLM"),
    ("¿Por qué se calienta tanto el procesador cuando juego?", "LLM"),
    ("Hazme un poema corto sobre un archivo que nadie abre", "LLM"),
    ("¿Cuánto dura la batería de un coche eléctrico antes de tener que cambiarla?", "LLM"),
    ("¿Qué navegador es más seguro, Firefox o Chrome?", "LLM"),
    ("¿Es verdad que reiniciar el router cada semana mejora la velocidad?", "LLM"),
    ("Mi hijo se pasa el día en YouTube, ¿cuántas horas de pantalla son razonables a los 12 años?", "LLM"),
    ("¿Cómo cierro una conversación incómoda con un cliente sin quedar mal?", "LLM"),
    ("¿Qué significa que un USB sea 3.0?", "LLM"),
    ("¿Cómo puedo ahorrar energía en casa este invierno?", "LLM"),
    ("¿Por qué dicen que Linux es mejor para programar en Python?", "LLM"),
    ("Cuéntame quién inventó el primer ordenador", "LLM"),

    # ---------------- No son órdenes: conversación y preguntas generales ----------------
    ("¿Cuál es la capital de Mongolia?", "LLM"),
    ("Dame ideas para el cumpleaños de mi hija de 7 años", "LLM"),
    ("Calcula el 15% de 240", "LLM"),
    ("¿Por qué el cielo es azul?", "LLM"),
    ("Tradúceme al francés: mañana vuelvo a casa", "LLM"),
    ("Ayúdame a escribir una carta de presentación para un trabajo de camarero", "LLM"),
    ("¿Qué puedo cocinar con garbanzos, espinacas y un huevo?", "LLM"),
    ("Resúmeme la Guerra de la Independencia en cinco líneas", "LLM"),
    ("Estoy un poco triste hoy porque se ha muerto mi perro", "LLM"),
    ("¿Cuántas calorías tiene una tortilla de patatas?", "LLM"),
    ("Explícame la regla de tres con un ejemplo", "LLM"),
    ("xq los gatos ronronean", "LLM"),
    ("Escríbeme un chiste sobre informáticos", "LLM"),
    ("¿Qué libro me recomiendas si me gustó La sombra del viento?", "LLM"),
    ("oye tengo examen de mates el lunes, me explicas las ecuaciones de segundo grado?", "LLM"),
    ("¿Cómo se conjuga el verbo 'satisfacer' en pretérito indefinido?", "LLM"),
    ("Necesito un plan de entrenamiento para correr 10 km en dos meses", "LLM"),
    ("¿Qué diferencia hay entre 'haber' y 'a ver'?", "LLM"),
    ("Dime tres planes para un domingo de lluvia en Bilbao", "LLM"),
    ("¿Quién escribió El Quijote y cuándo?", "LLM"),
    ("Ayúdame a responder a mi casero, que quiere subirme el alquiler un 10%", "LLM"),
    ("q opinas de la tortilla con cebolla", "LLM"),
    ("¿Cómo le explico a mi madre que me voy a vivir a Valencia?", "LLM"),
    ("Inventa un cuento para dormir con un dragón que tiene miedo a la oscuridad", "LLM"),
    ("¿Cuántos días faltan para Navidad si hoy es 22 de noviembre?", "LLM"),
]
