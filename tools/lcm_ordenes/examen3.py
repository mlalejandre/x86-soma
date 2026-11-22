# Examen ciego 3: peticiones en español de España -> orden SOMA o "LLM".
# "||" separa alternativas igual de válidas.

E = [
    # ---------------- ajustes ----------------
    ("abre los ajustes", "ajustes"),
    ("Quiero cambiar la transparencia de las ventanas, ábreme la configuración de la interfaz", "ajustes"),
    ("oye, llévame a ajustes porfa", "ajustes"),

    # ---------------- audio vol {N} ----------------
    ("pon el volumen al 40", "audio vol 40"),
    ("Baja el volumen a 15 por favor", "audio vol 15"),
    ("me puedes subir el sonido al 80?", "audio vol 80"),

    # ---------------- bateria ----------------
    ("necesito saber cuánta batería me queda", "bateria"),
    ("cuanta bateria tengo", "bateria"),
    ("¿Está cargando el portátil? enséñame el estado de la batería", "bateria"),

    # ---------------- brillo + / - / {N} ----------------
    ("sube el brillo", "brillo +"),
    ("Oye, un poco más de brillo, que no veo nada", "brillo +"),
    ("baja el brillo un poco", "brillo -"),
    ("la pantalla me deslumbra, bájale el brillo", "brillo -"),
    ("pon el brillo al 35", "brillo 35"),
    ("Brillo de la pantalla al 70%, gracias", "brillo 70"),
    ("ajusta el brilo a 10", "brillo 10"),

    # ---------------- cam foto / camara / captura foto ----------------
    ("hazme una foto con la webcam", "cam foto"),
    ("Saca una foto con la cámara ahora mismo", "cam foto"),
    ("abre la app de la cámara", "camara"),
    ("quiero ver la vista previa de la camara, ábrela", "camara"),
    ("haz una captura de pantalla", "captura foto"),
    ("Me puedes hacer un pantallazo?", "captura foto"),

    # ---------------- cat {A} ----------------
    ("enséñame lo que pone en notas.txt", "cat notas.txt"),
    ("muestra el contenido de config.ini", "cat config.ini"),
    ("Léeme receta_croquetas.txt por favor", "cat receta_croquetas.txt"),
    ("abre contrato_alquiler.pdf", "cat contrato_alquiler.pdf||pdf contrato_alquiler.pdf"),

    # ---------------- checkpoint ----------------
    ("haz un checkpoint", "checkpoint"),
    ("Guarda una copia de seguridad del sistema de archivos antes de tocar nada", "checkpoint"),

    # ---------------- clear ----------------
    ("limpia la pantalla", "clear"),
    ("borra todo lo que hay escrito en la terminal, que esta hecha un lio", "clear"),

    # ---------------- cp {A} {B} ----------------
    ("copia facturas.xlsx a facturas_copia.xlsx", "cp facturas.xlsx facturas_copia.xlsx"),
    ("Haz una copia de tesis.docx en la carpeta respaldo", "cp tesis.docx respaldo"),
    ("me duplicas presupuesto_2024.txt como presupuesto_2025.txt?", "cp presupuesto_2024.txt presupuesto_2025.txt"),
    ("copia foto_playa.jpg en vacaciones", "cp foto_playa.jpg vacaciones"),

    # ---------------- date ----------------
    ("qué hora es", "date"),
    ("¿A qué día estamos hoy?", "date"),
    ("dime la fecha", "date"),

    # ---------------- descargar {U} ----------------
    ("descarga https://ejemplo.es/manual.pdf", "descargar https://ejemplo.es/manual.pdf"),
    ("Bájate este archivo: http://files.miweb.com/datos.csv", "descargar http://files.miweb.com/datos.csv"),
    ("me descargas https://repo.soma.dev/paquetes/fuente.zip porfa", "descargar https://repo.soma.dev/paquetes/fuente.zip"),

    # ---------------- df ----------------
    ("cuánto espacio libre queda en el disco", "df"),
    ("Necesito saber la capacidad del almacenamiento", "df"),

    # ---------------- dmesg ----------------
    ("enséñame el registro del núcleo", "dmesg"),
    ("quiero ver los mensajes del kernel desde el arranque", "dmesg"),

    # ---------------- dns {H} ----------------
    ("resuelve el dominio elpais.com", "dns elpais.com"),
    ("¿Qué IP tiene google.es?", "dns google.es"),
    ("haz una consulta dns de wikipedia.org", "dns wikipedia.org"),

    # ---------------- du ----------------
    ("cuanto ocupan los archivos", "du"),
    ("Quiero ver el uso de disco por archivo", "du"),

    # ---------------- energia ----------------
    ("a qué temperatura está el procesador", "energia"),
    ("Muéstrame el perfil de energía y el consumo de la CPU", "energia"),
    ("oye, a cuántos GHz va el procesador?", "energia"),

    # ---------------- find {T} ----------------
    ("busca archivos que se llamen presupuesto", "find presupuesto"),
    ("Encuentra el archivo factura_marzo", "find factura_marzo"),
    ("dónde está el fichero curriculum?", "find curriculum"),

    # ---------------- grabadora ----------------
    ("abre la grabadora", "grabadora"),
    ("quiero grabar una nota de voz con el micro, abre la app", "grabadora"),

    # ---------------- grep {T} {A} ----------------
    ("busca la palabra error en log.txt", "grep error log.txt"),
    ("En contactos.csv busca Marisa", "grep Marisa contactos.csv"),
    ("mira si aparece 'timeout' dentro de servidor.log", "grep timeout servidor.log"),
    ("Hay alguna línea con TODO en main.py?", "grep TODO main.py"),

    # ---------------- habla {T} ----------------
    ("di en voz alta hola a todos", "habla hola a todos"),
    ("Lee en voz alta: la cena está lista", "habla la cena está lista"),
    ("pronuncia buenos días Madrid", "habla buenos días Madrid"),
    ("que diga el ordenador: feliz cumpleaños Lucía", "habla feliz cumpleaños Lucía"),

    # ---------------- head -n {N} {A} ----------------
    ("enséñame las primeras 5 líneas de log.txt", "head -n 5 log.txt"),
    ("Muestra las 20 primeras lineas de datos.csv", "head -n 20 datos.csv"),
    ("quiero ver el principio de informe.md, solo 3 líneas", "head -n 3 informe.md"),

    # ---------------- history ----------------
    ("muéstrame el historial de comandos", "history"),
    ("qué órdenes he escrito antes?", "history"),

    # ---------------- hostname ----------------
    ("cómo se llama este equipo", "hostname"),
    ("Dime el nombre del ordenador", "hostname"),

    # ---------------- imagen {A} ----------------
    ("abre la foto gato.png", "imagen gato.png"),
    ("Quiero ver la imagen boda_2019.jpg", "imagen boda_2019.jpg"),
    ("enseñame el dibujo plano_cocina.gif", "imagen plano_cocina.gif"),

    # ---------------- kill {N} ----------------
    ("mata el proceso 12", "kill 12"),
    ("Termina el hilo 45, se ha quedado colgado", "kill 45"),
    ("cierra la tarea con TID 7 porfa", "kill 7"),

    # ---------------- ls / ls {D} ----------------
    ("lista los archivos", "ls"),
    ("¿Qué ficheros hay aquí?", "ls"),
    ("qué hay en la carpeta documentos", "ls documentos"),
    ("Enséñame el contenido de la carpeta fotos_verano", "ls fotos_verano"),
    ("lista lo que hay en /workspace", "ls /workspace"),

    # ---------------- mkdir {D} ----------------
    ("crea una carpeta llamada proyectos", "mkdir proyectos"),
    ("Hazme un directorio nuevo que se llame recetas", "mkdir recetas"),
    ("necesito una carpeta facturas_2025", "mkdir facturas_2025"),

    # ---------------- monitor ----------------
    ("abre el monitor del sistema", "monitor"),
    ("Quiero ver gráficas de CPU y memoria en tiempo real", "monitor"),

    # ---------------- mv {A} {B} ----------------
    ("renombra borrador.txt a final.txt", "mv borrador.txt final.txt"),
    ("Mueve informe.pdf a la carpeta trabajo", "mv informe.pdf trabajo"),
    ("cámbiale el nombre a foto1.jpg, que se llame playa.jpg", "mv foto1.jpg playa.jpg"),
    ("pasa notas_clase.txt a la carpeta universidad por favor", "mv notas_clase.txt universidad"),

    # ---------------- nano {A} ----------------
    ("quiero editar lista_compra.txt", "nano lista_compra.txt"),
    ("Abre carta.txt en el editor para modificarla", "nano carta.txt"),

    # ---------------- navegador / navegador {U} ----------------
    ("abre el navegador", "navegador"),
    ("Quiero navegar por internet", "navegador"),
    ("abre https://www.marca.com en el navegador", "navegador https://www.marca.com"),
    ("Llévame a la página https://elmundo.es", "navegador https://elmundo.es"),
    ("entra en https://github.com/soma-os", "navegador https://github.com/soma-os"),

    # ---------------- paint ----------------
    ("abre paint", "paint"),
    ("Me apetece dibujar algo, abre el editor de dibujo", "paint"),

    # ---------------- pdf {A} ----------------
    ("abre el pdf manual_lavadora.pdf", "pdf manual_lavadora.pdf"),
    ("Quiero leer apuntes_historia.pdf en el lector de pdf", "pdf apuntes_historia.pdf"),

    # ---------------- ping {H} ----------------
    ("haz ping a 8.8.8.8", "ping 8.8.8.8"),
    ("Comprueba si responde 192.168.1.1", "ping 192.168.1.1"),
    ("mira si llego a servidor.local", "ping servidor.local"),

    # ---------------- poweroff / reboot ----------------
    ("apaga el ordenador", "poweroff"),
    ("Ya he terminado, apágalo todo", "poweroff"),
    ("reinicia el equipo", "reboot"),
    ("oye reinicia porfa que va fatal", "reboot"),

    # ---------------- ps ----------------
    ("qué procesos hay en marcha", "ps"),
    ("Enséñame la tabla de hilos", "ps"),

    # ---------------- ram ----------------
    ("cuánta memoria ram me queda libre", "ram"),
    ("Necesito saber cuanta memoria se está usando", "ram"),

    # ---------------- rm {A} ----------------
    ("borra facturas_viejas.txt", "rm facturas_viejas.txt"),
    ("Elimina el archivo temp.log", "rm temp.log"),
    ("me puedes quitar copia_vieja.docx?", "rm copia_vieja.docx"),

    # ---------------- rmdir {D} ----------------
    ("borra la carpeta vacía pruebas", "rmdir pruebas"),
    ("Elimina el directorio antiguos", "rmdir antiguos"),

    # ---------------- search {T} ----------------
    ("busca en internet receta de lentejas", "search receta de lentejas"),
    ("Búscame en la web el horario del Prado", "search horario del Prado"),
    ("busca online precio del alquiler en Valencia", "search precio del alquiler en Valencia"),
    ("googlea quién ganó la liga en 1998", "search quién ganó la liga en 1998"),

    # ---------------- stat {A} ----------------
    ("dame los metadatos de informe.docx", "stat informe.docx"),
    ("¿Cuánto ocupa video_boda.avi y de qué tipo es?", "stat video_boda.avi"),
    ("info del archivo notas.txt", "stat notas.txt"),

    # ---------------- status ----------------
    ("cuál es mi dirección ip", "status"),
    ("Cómo está la red? dime la IP", "status"),

    # ---------------- studio ----------------
    ("abre SOMA Studio", "studio"),
    ("quiero programar, abre el editor de código", "studio"),

    # ---------------- sysinfo ----------------
    ("dame un resumen del sistema", "sysinfo"),
    ("Cómo va el sistema en general?", "sysinfo"),

    # ---------------- tail -n {N} {A} ----------------
    ("muéstrame las últimas 10 líneas de servidor.log", "tail -n 10 servidor.log"),
    ("Las 50 ultimas lineas de errores.txt", "tail -n 50 errores.txt"),
    ("quiero ver el final de diario.txt, las 4 últimas líneas", "tail -n 4 diario.txt"),

    # ---------------- teclado es / us ----------------
    ("pon el teclado en español", "teclado es"),
    ("Cambia la distribución del teclado a inglés americano", "teclado us"),
    ("teclado en ingles por favor", "teclado us"),
    ("no me sale la ñ, ponme el teclado español", "teclado es"),

    # ---------------- terminal ----------------
    ("abre una terminal", "terminal"),
    ("Necesito una ventana de consola nueva", "terminal"),

    # ---------------- touch {A} ----------------
    ("crea un archivo vacío llamado ideas.txt", "touch ideas.txt"),
    ("Hazme un fichero nuevo pendientes.md, vacío de momento", "touch pendientes.md"),

    # ---------------- tree ----------------
    ("enséñame el árbol de directorios", "tree"),
    ("Muestra la estructura de archivos en forma de árbol", "tree"),

    # ---------------- uname -a ----------------
    ("qué versión de SOMA tengo", "uname -a"),
    ("Dime la versión y la arquitectura del sistema", "uname -a"),

    # ---------------- uptime ----------------
    ("cuánto tiempo lleva encendido el ordenador", "uptime"),
    ("Desde cuando esta encendido esto?", "uptime"),

    # ---------------- wc {A} ----------------
    ("cuenta las palabras de ensayo.txt", "wc ensayo.txt"),
    ("¿Cuántas líneas tiene datos.csv?", "wc datos.csv"),

    # ---------------- whoami ----------------
    ("con qué usuario estoy", "whoami"),
    ("quién soy yo en este sistema?", "whoami"),

    # ---------------- wifi ----------------
    ("busca redes wifi", "wifi buscar"),
    ("Qué redes inalámbricas hay cerca?", "wifi buscar"),
    ("conéctate a la wifi Casa_Pili con la clave gato2020", "wifi conectar Casa_Pili gato2020"),
    ("Conecta a MOVISTAR_5G3A, contraseña Xk9pQ2rT", "wifi conectar MOVISTAR_5G3A Xk9pQ2rT"),
    ("me conectas a la red Biblioteca_UAM? la clave es libros123", "wifi conectar Biblioteca_UAM libros123"),
    ("conectate a la red Bar_Manolo con password cañas2024", "wifi conectar Bar_Manolo cañas2024"),
    ("conéctate a la wifi abierta AeropuertoMAD_Free", "wifi conectar AeropuertoMAD_Free"),
    ("Únete a la red Invitados", "wifi conectar Invitados"),
    ("conecta con la wifi Hotel_Sol, no tiene clave", "wifi conectar Hotel_Sol"),
    ("desconéctate de la wifi", "wifi desconectar"),
    ("Quita la conexión inalámbrica por favor", "wifi desconectar"),

    # ---------------- write {A} "{T}" ----------------
    ("escribe pan y fruta en lista.txt", 'write lista.txt "pan y fruta"'),
    ("Guarda en nota.txt el texto: llamar al dentista el martes", 'write nota.txt "llamar al dentista el martes"'),
    ("crea recordatorio.txt que diga comprar pilas", 'write recordatorio.txt "comprar pilas"'),
    ("pon en saludo.txt hola mundo", 'write saludo.txt "hola mundo"'),
    ("Escribe en tareas.md: terminar el informe antes del viernes", 'write tareas.md "terminar el informe antes del viernes"'),
    ("me guardas la frase la reunión es a las 10 en agenda.txt?", 'write agenda.txt "la reunión es a las 10"'),

    # ---------------- youtube {T} ----------------
    ("pon en youtube tutorial de guitarra", "youtube tutorial de guitarra"),
    ("Busca en YouTube recetas de paella valenciana", "youtube recetas de paella valenciana"),
    ("quiero ver vídeos de gatos graciosos en youtube", "youtube gatos graciosos"),

    # =============== NO SON ÓRDENES -> LLM ===============
    # Trampas con vocabulario del ordenador
    ("¿por qué los portátiles se calientan tanto?", "LLM"),
    ("qué diferencia hay entre memoria RAM y memoria ROM", "LLM"),
    ("¿Cuántos ciclos de carga aguanta una batería de litio?", "LLM"),
    ("explícame cómo funciona el wifi", "LLM"),
    ("es malo apagar el ordenador desde el botón directamente?", "LLM"),
    ("¿Qué es mejor, un disco SSD o uno HDD?", "LLM"),
    ("cómo puedo recuperar archivos borrados en general?", "LLM"),
    ("¿Qué es un proceso zombi en un sistema operativo?", "LLM"),
    ("¿Tener el brillo al máximo gasta mucha batería?", "LLM"),
    ("escribe un poema sobre una pantalla rota", "LLM"),
    ("Redacta un correo a mi jefe diciendo que el wifi de la oficina va fatal", "LLM"),
    ("qué opinas de Linux frente a Windows?", "LLM"),
    ("cuál es la historia de los sistemas de archivos FAT", "LLM"),
    ("¿Por qué se llama kernel al núcleo?", "LLM"),
    ("Explícame qué hace el comando grep en Unix", "LLM"),
    ("dame consejos para que me dure más la batería del móvil", "LLM"),
    ("¿Cuánta memoria necesita un ordenador para editar vídeo?", "LLM"),
    ("mi hermano borró sin querer las fotos de su móvil, qué le digo?", "LLM"),
    ("¿Qué significa que un disco esté fragmentado?", "LLM"),
    ("cuéntame un chiste de informáticos", "LLM"),
    ("¿Es verdad que reiniciar el router arregla casi todo?", "LLM"),
    ("Escribe un cuento corto de un robot que no quería apagarse", "LLM"),
    ("qué es una dirección IP y para qué sirve", "LLM"),
    ("¿Cómo elijo una buena contraseña para la wifi de casa?", "LLM"),
    ("resume en tres frases qué es la memoria virtual", "LLM"),
    ("por qué las pantallas OLED gastan menos?", "LLM"),
    # Conversación y preguntas generales
    ("hola, ¿qué tal estás?", "LLM"),
    ("¿Cuál es la capital de Australia?", "LLM"),
    ("dame una receta de tortilla de patatas", "LLM"),
    ("Traduce al inglés: mañana llueve en Bilbao", "LLM"),
    ("¿quién escribió La Regenta?", "LLM"),
    ("estoy aburrido, cuéntame algo interesante", "LLM"),
    ("Escríbeme una felicitación para mi abuela que cumple 90", "LLM"),
    ("¿cuánto es 17 por 23?", "LLM"),
    ("explícame la teoría de la relatividad como si tuviera diez años", "LLM"),
    ("qué me recomiendas visitar en Granada?", "LLM"),
    ("Gracias, eres un crack", "LLM"),
    ("¿Por qué el cielo es azul?", "LLM"),
    ("ayúdame a preparar una entrevista de trabajo", "LLM"),
    ("¿Qué libro me recomiendas para aprender a programar?", "LLM"),
    ("cuales son los síntomas de la gripe", "LLM"),
    ("hazme un plan de entrenamiento para correr 10 km", "LLM"),
    ("¿Qué pasó en la batalla de Lepanto?", "LLM"),
    ("Corrige esta frase: haber si vienes mañana", "LLM"),
    ("¿cómo se dice 'ordenador' en francés?", "LLM"),
    ("me siento un poco agobiado con los exámenes", "LLM"),
    ("¿Qué diferencia hay entre 'sino' y 'si no'?", "LLM"),
    ("dime tres ideas para el cumpleaños de mi hija", "LLM"),
    ("buenas noches", "LLM"),
    ("¿Cuál es el río más largo de España?", "LLM"),
]
