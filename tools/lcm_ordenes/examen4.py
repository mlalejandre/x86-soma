# -*- coding: utf-8 -*-
# Examen ciego 4: peticiones en español de España -> orden de terminal SOMA.
# esperada = línea exacta; "a||b" = cualquiera de las dos vale; "LLM" = no es una orden.

E = [
    # ---------------- ajustes ----------------
    ("Buenas tardes. Le agradecería mucho que me abriera los ajustes de la interfaz, que quiero cambiar la fuente de las ventanas.", "ajustes"),
    ("abre ajustes q kiero quitar la transparencia de las ventanas", "ajustes"),

    # ---------------- audio vol {N} ----------------
    ("pon el volumen al 40", "audio vol 40"),
    ("Disculpe, ¿sería tan amable de bajar el volumen del sonido a 15? Es que mi nieto está durmiendo la siesta.", "audio vol 15"),
    ("volumen 80 xfa", "audio vol 80"),

    # ---------------- bateria ----------------
    ("bateria?", "bateria"),
    ("Me voy ahora en el tren y no tengo cargador, ¿cuánta batería me queda?", "bateria"),
    ("cuanta bateria tengo y si esta enchufado el cargador", "bateria"),

    # ---------------- brillo + / - / {N} ----------------
    ("sube un poco el brillo", "brillo +"),
    ("Estoy en la terraza y con este sol no veo nada; súbeme el brillo un poquito, por favor.", "brillo +"),
    ("baja el brillo", "brillo -"),
    ("Es de noche y la pantalla me deslumbra, ¿podrías bajar un poco el brillo?", "brillo -"),
    ("brillo al 20", "brillo 20"),
    ("pon el brillo de la pantalla al 75 porfa", "brillo 75"),
    ("Le ruego que ajuste el brillo de la pantalla al cincuenta por ciento.", "brillo 50"),

    # ---------------- cam foto / camara / captura foto ----------------
    ("hazme una foto con la webcam", "cam foto"),
    ("Necesito una foto mía para la ficha del gimnasio, ¿me sacas una con la cámara web?", "cam foto"),
    ("abre la app de la camara", "camara"),
    ("Quisiera ver la vista previa de la cámara, abra usted la aplicación Cámara si es tan amable.", "camara"),
    ("haz una captura de pantalla", "captura foto"),
    ("pantallazo ya", "captura foto"),
    ("Quiero enseñarle a mi hija este error, ¿puedes hacer una captura de la pantalla?", "captura foto"),

    # ---------------- cat {A} ----------------
    ("enseñame lo q pone en notas.txt", "cat notas.txt"),
    ("Hágame el favor de mostrarme el contenido del archivo receta_croquetas.txt.", "cat receta_croquetas.txt"),
    ("cat de config.ini", "cat config.ini"),
    ("muestra el contenido de Lista-Invitados.md", "cat Lista-Invitados.md"),

    # ---------------- checkpoint ----------------
    ("guarda un snapshot del sistema de archivos", "checkpoint"),
    ("Antes de tocar nada quiero tener una copia de seguridad de todo el sistema de archivos guardada en disco, ¿la haces?", "checkpoint"),

    # ---------------- clear ----------------
    ("limpia la pantalla", "clear"),
    ("borra todo lo q hay escrito en la terminal q no se ve nada", "clear"),
    ("Tengo la consola llena de texto; ¿podría limpiarla, por favor?", "clear"),

    # ---------------- cp {A} {B} ----------------
    ("copia informe.pdf a informe_copia.pdf", "cp informe.pdf informe_copia.pdf"),
    ("Antes de corregirlo quiero conservar el original: haz una copia de tesis_v3.docx llamada tesis_v3_backup.docx.", "cp tesis_v3.docx tesis_v3_backup.docx"),
    ("copia foto1.jpg en la carpeta vacaciones", "cp foto1.jpg vacaciones"),
    ("cp presupuesto-2025.xlsx a /mis_archivos", "cp presupuesto-2025.xlsx /mis_archivos"),

    # ---------------- date ----------------
    ("q dia es hoy", "date"),
    ("¿Qué hora tiene el reloj del ordenador?", "date"),
    ("fecha y hora", "date"),

    # ---------------- descargar {U} ----------------
    ("descarga https://ejemplo.es/manual.pdf", "descargar https://ejemplo.es/manual.pdf"),
    ("Mi sobrino me ha mandado este enlace con las fotos de la boda, ¿me lo bajas al ordenador? http://fotosboda.net/album.zip", "descargar http://fotosboda.net/album.zip"),
    ("bajate esto xfa https://cdn.juegoslibres.org/tetris-2.1.tar.gz", "descargar https://cdn.juegoslibres.org/tetris-2.1.tar.gz"),

    # ---------------- df ----------------
    ("disco libre", "df"),
    ("cuanto espacio me queda en el almacenamiento", "df"),
    ("¿Me haría el favor de decirme cuánta capacidad de almacenamiento libre tiene el equipo?", "df"),

    # ---------------- dmesg ----------------
    ("muestra el registro del nucleo", "dmesg"),
    ("El ordenador hizo algo raro al arrancar; enséñame los mensajes del kernel desde el arranque.", "dmesg"),

    # ---------------- dns {H} ----------------
    ("resuelve el dns de elpais.es", "dns elpais.es"),
    ("q ip tiene google.com", "dns google.com"),
    ("Quería saber a qué dirección IP corresponde el dominio biblioteca-municipal.org, si no es molestia.", "dns biblioteca-municipal.org"),

    # ---------------- du ----------------
    ("cuanto ocupa cada archivo", "du"),
    ("Quiero ver qué archivos me están comiendo el espacio: el uso de disco por archivo.", "du"),

    # ---------------- energia ----------------
    ("temperatura del procesador", "energia"),
    ("¿A qué frecuencia va la CPU y cuánto está consumiendo ahora mismo?", "energia"),
    ("q perfil de energia tiene el procesador", "energia"),

    # ---------------- find {T} ----------------
    ("busca archivos que se llamen factura", "find factura"),
    ("No me acuerdo dónde guardé el documento del alquiler; ¿puedes buscar los archivos cuyo nombre contenga alquiler?", "find alquiler"),
    ("encuentra el archivo q se llama algo de Presupuesto", "find Presupuesto"),

    # ---------------- grabadora ----------------
    ("abre la grabadora", "grabadora"),
    ("Voy a dictar unas notas de voz para la reunión, ábreme la aplicación para grabar con el micrófono.", "grabadora"),

    # ---------------- grep {T} {A} ----------------
    ("busca la palabra error en log.txt", "grep error log.txt"),
    ("En el archivo contactos.csv, ¿aparece en algún sitio el nombre Mercedes? Búsquelo, por favor.", "grep Mercedes contactos.csv"),
    ("grep de TODO en main.py", "grep TODO main.py"),
    ("busca donde sale timeout dentro de servidor.conf", "grep timeout servidor.conf"),

    # ---------------- habla {T} ----------------
    ("di en voz alta hola a todos", "habla hola a todos"),
    ("Quiero que el ordenador lea en voz alta: feliz cumpleaños abuela", "habla feliz cumpleaños abuela"),
    ("q diga por el altavoz la cena esta lista", "habla la cena esta lista"),

    # ---------------- head -n {N} {A} ----------------
    ("muestra las 8 primeras lineas de log.txt", "head -n 8 log.txt"),
    ("Enséñeme, por favor, solamente las diez primeras líneas del archivo memorias.txt.", "head -n 10 memorias.txt"),
    ("head 3 lineas de datos_2024.csv", "head -n 3 datos_2024.csv"),

    # ---------------- history ----------------
    ("historial de comandos", "history"),
    ("q comandos escribi antes", "history"),
    ("Ayer hice algo en la terminal que funcionó y no recuerdo qué era; ¿me enseñas el historial de órdenes?", "history"),

    # ---------------- hostname ----------------
    ("como se llama este equipo", "hostname"),
    ("nombre del ordenador?", "hostname"),

    # ---------------- imagen {A} ----------------
    ("abre la imagen playa.jpg", "imagen playa.jpg"),
    ("Mi hija me ha pasado la foto nieto_bautizo.png, ¿me la enseña en el visor de imágenes?", "imagen nieto_bautizo.png"),
    ("ver logo-Empresa.gif", "imagen logo-Empresa.gif"),

    # ---------------- kill {N} ----------------
    ("mata el proceso 12", "kill 12"),
    ("El hilo 27 se ha quedado colgado, termínalo.", "kill 27"),
    ("kill al hilo 104 q no responde", "kill 104"),

    # ---------------- ls / ls {D} ----------------
    ("lista los archivos", "ls"),
    ("q archivos hay aqui", "ls"),
    ("¿Sería usted tan amable de mostrarme qué ficheros tengo guardados?", "ls"),
    ("que hay en la carpeta fotos", "ls fotos"),
    ("Estoy buscando los apuntes; lista el contenido de la carpeta Clase-3B.", "ls Clase-3B"),
    ("ls de /mis_archivos", "ls /mis_archivos"),

    # ---------------- mkdir {D} ----------------
    ("Estoy preparando la clase de mañana, ¿me creas una carpeta clase_martes?", "mkdir clase_martes"),
    ("crea carpeta proyectos", "mkdir proyectos"),
    ("haz una carpeta q se llame Viaje-Roma-2026", "mkdir Viaje-Roma-2026"),
    ("Le pido por favor que cree un directorio nuevo llamado recetas_abuela.", "mkdir recetas_abuela"),

    # ---------------- monitor ----------------
    ("abre el monitor del sistema", "monitor"),
    ("Quiero ver en una ventana gráficas de CPU, memoria y red en tiempo real.", "monitor"),

    # ---------------- mv {A} {B} ----------------
    ("renombra borrador.txt a final.txt", "mv borrador.txt final.txt"),
    ("mueve cancion.wav a la carpeta musica", "mv cancion.wav musica"),
    ("Me equivoqué al poner el nombre: el archivo se llama cartaa.txt y debería llamarse carta.txt. ¿Lo cambia?", "mv cartaa.txt carta.txt"),
    ("cambia el nombre de IMG_0042.jpg por gato.jpg", "mv IMG_0042.jpg gato.jpg"),

    # ---------------- nano {A} ----------------
    ("edita notas.txt", "nano notas.txt"),
    ("Tengo que corregir una cosa en el archivo lista_compra.txt; ábremelo en el editor.", "nano lista_compra.txt"),
    ("abre config.ini en nano", "nano config.ini"),

    # ---------------- navegador / navegador {U} ----------------
    ("abre el navegador", "navegador"),
    ("Quiero navegar por internet un rato, ¿me abre el navegador?", "navegador"),
    ("abre https://www.rtve.es en el navegador", "navegador https://www.rtve.es"),
    ("entra en http://tiempo.aemet.es", "navegador http://tiempo.aemet.es"),
    ("Mi médico me dijo que pidiera cita en https://citas.salud-madrid.org, ¿puede abrir esa página?", "navegador https://citas.salud-madrid.org"),

    # ---------------- paint ----------------
    ("abre paint", "paint"),
    ("Quiero dibujar un poco con el ratón, ábreme el editor de dibujo.", "paint"),

    # ---------------- pdf {A} ----------------
    ("abre el pdf factura_luz.pdf", "pdf factura_luz.pdf"),
    ("Necesito leer las instrucciones del horno, están en Manual-Horno.pdf. ¿Me lo abre?", "pdf Manual-Horno.pdf"),

    # ---------------- ping {H} ----------------
    ("haz ping a 192.168.1.1", "ping 192.168.1.1"),
    ("ping 8.8.8.8", "ping 8.8.8.8"),
    ("No sé si el router responde; compruébalo haciendo un ping a 10.0.0.254.", "ping 10.0.0.254"),
    ("comprueba si responde servidor-casa.local", "ping servidor-casa.local"),

    # ---------------- poweroff / reboot ----------------
    ("apaga el ordenador", "poweroff"),
    ("Ya he terminado por hoy, muchas gracias por todo. Apague el equipo, por favor.", "poweroff"),
    ("reinicia", "reboot"),
    ("me han dicho q para q se aplique hay q reiniciar el pc, hazlo", "reboot"),

    # ---------------- ps ----------------
    ("q procesos hay", "ps"),
    ("lista de hilos", "ps"),
    ("¿Qué programas e hilos se están ejecutando ahora mismo en el equipo?", "ps"),

    # ---------------- ram ----------------
    ("ram?", "ram"),
    ("cuanta memoria libre queda", "ram"),
    ("Últimamente va lento; dígame cuánta memoria RAM está usando y cuánta queda libre.", "ram"),

    # ---------------- rm {A} ----------------
    ("borra fotos_viejas.zip", "rm fotos_viejas.zip"),
    ("elimina el archivo temp.log", "rm temp.log"),
    ("Ese documento ya no lo necesito; tenga la bondad de eliminar borrador-carta-banco.txt.", "rm borrador-carta-banco.txt"),

    # ---------------- rmdir {D} ----------------
    ("borra la carpeta vacia pruebas", "rmdir pruebas"),
    ("Ya vacié la carpeta Antiguo_2019; ahora elimina el directorio.", "rmdir Antiguo_2019"),

    # ---------------- search {T} ----------------
    ("busca en internet receta de paella valenciana", "search receta de paella valenciana"),
    ("Mi nieta me ha preguntado y no lo sé; busque en la web quién pintó Las Meninas.", "search quién pintó Las Meninas||search quien pintó Las Meninas"),
    ("busca en google horario farmacias", "search horario farmacias"),

    # ---------------- stat {A} ----------------
    ("info del archivo video.mp4", "stat video.mp4"),
    ("¿Cuánto ocupa y de qué tipo es el archivo copia_seguridad.tar?", "stat copia_seguridad.tar"),
    ("metadatos de Informe_Q3.pdf", "stat Informe_Q3.pdf"),

    # ---------------- status ----------------
    ("estado de la red", "status"),
    ("q ip tengo", "status"),
    ("¿Estoy conectado a la red? Dígame también cuál es mi dirección IP.", "status"),

    # ---------------- studio ----------------
    ("abre soma studio", "studio"),
    ("Quiero ponerme a programar, ábreme el editor de código.", "studio"),

    # ---------------- sysinfo ----------------
    ("resumen del sistema", "sysinfo"),
    ("dame un resumen general del equipo: tiempo encendido, ram, almacenamiento, hilos y red", "sysinfo"),

    # ---------------- tail -n {N} {A} ----------------
    ("ultimas 20 lineas de servidor.log", "tail -n 20 servidor.log"),
    ("Enséñame las cinco últimas líneas del diario.txt, que es donde escribí lo de ayer.", "tail -n 5 diario.txt"),
    ("tail 50 de errores-red.log", "tail -n 50 errores-red.log"),

    # ---------------- teclado es / us ----------------
    ("pon el teclado en español", "teclado es"),
    ("No me salen la ñ ni los acentos; ¿podría poner la distribución de teclado española?", "teclado es"),
    ("cambia el teclado a ingles americano", "teclado us"),
    ("teclado us", "teclado us"),

    # ---------------- terminal ----------------
    ("abre una terminal", "terminal"),
    ("Necesito otra ventana de terminal para escribir comandos.", "terminal"),

    # ---------------- touch {A} ----------------
    ("crea un archivo vacio llamado ideas.txt", "touch ideas.txt"),
    ("Hazme un fichero en blanco que se llame pendientes-junio.md, luego ya lo relleno.", "touch pendientes-junio.md"),
    ("touch index.html", "touch index.html"),

    # ---------------- tree ----------------
    ("muestra el arbol de carpetas", "tree"),
    ("Quiero ver toda la estructura de carpetas y archivos en forma de árbol.", "tree"),

    # ---------------- uname -a ----------------
    ("q version de soma tengo", "uname -a"),
    ("Dime la versión completa del sistema y la arquitectura.", "uname -a"),

    # ---------------- uptime ----------------
    ("cuanto tiempo lleva encendido", "uptime"),
    ("uptime?", "uptime"),
    ("¿Desde cuándo lleva funcionando el ordenador sin apagarse?", "uptime"),

    # ---------------- wc {A} ----------------
    ("cuantas palabras tiene ensayo.txt", "wc ensayo.txt"),
    ("Mi profesora pide un máximo de palabras; ¿cuántas líneas y palabras tiene trabajo_historia.txt?", "wc trabajo_historia.txt"),
    ("cuenta las lineas de Datos-Ventas.csv", "wc Datos-Ventas.csv"),

    # ---------------- whoami ----------------
    ("con q usuario estoy", "whoami"),
    ("¿Quién soy yo en este ordenador? Me refiero a qué usuario tengo puesto.", "whoami"),

    # ---------------- wifi buscar / conectar / desconectar ----------------
    ("busca redes wifi", "wifi buscar"),
    ("q wifis hay cerca", "wifi buscar"),
    ("Acabo de llegar al hotel; ¿qué redes inalámbricas hay disponibles?", "wifi buscar"),
    ("conectate a la wifi Biblioteca_Libre", "wifi conectar Biblioteca_Libre"),
    ("conecta a la red abierta CafeteriaSol", "wifi conectar CafeteriaSol"),
    ("conecta a Casa_Luis con la clave perro77", "wifi conectar Casa_Luis perro77"),
    ("Estoy en casa de mi hermana; conéctese a su wifi, que se llama MOVISTAR_8A2F, y la contraseña es Gatito2024.", "wifi conectar MOVISTAR_8A2F Gatito2024"),
    ("wifi Oficina-5G clave xK9pL2mq", "wifi conectar Oficina-5G xK9pL2mq"),
    ("desconecta el wifi", "wifi desconectar"),
    ("Voy a desenchufar el router un momento, desconéctate de la red inalámbrica.", "wifi desconectar"),

    # ---------------- write {A} "{T}" ----------------
    ("escribe en compra.txt leche y pan", "write compra.txt \"leche y pan\""),
    ("Estoy organizando la semana: guarda en recordatorio.txt el texto llamar al fontanero el jueves", "write recordatorio.txt \"llamar al fontanero el jueves\""),
    ("crea saludo.txt con hola mundo", "write saludo.txt \"hola mundo\""),
    ("pon en Notas-Examen.txt q el examen es el 12 de mayo", "write Notas-Examen.txt \"el examen es el 12 de mayo\""),

    # ---------------- youtube {T} ----------------
    ("pon en youtube recetas de croquetas", "youtube recetas de croquetas"),
    ("busca videos de gatos graciosos en youtube", "youtube gatos graciosos"),
    ("Me gustaría escuchar a Raphael; búsqueme en YouTube «Raphael en directo».", "youtube Raphael en directo"),

    # ======================================================================
    # NO SON ÓRDENES -> LLM
    # ======================================================================
    # Trampas: mencionan cosas del ordenador pero son preguntas, opiniones, redacción...
    ("¿qué diferencia hay entre la memoria RAM y el disco duro?", "LLM"),
    ("es malo dejar el portatil siempre enchufado para la bateria?", "LLM"),
    ("explicame q es un proceso en un sistema operativo", "LLM"),
    ("¿Por qué el wifi va más lento cuando hay muchas paredes?", "LLM"),
    ("redacta un correo a mi jefe diciendo que el disco del servidor se ha llenado", "LLM"),
    ("q opinas de apagar el ordenador todas las noches, es mejor?", "LLM"),
    ("¿Cómo se borra el historial en un móvil Android?", "LLM"),
    ("escribeme un poema sobre una pantalla rota", "LLM"),
    ("Cuéntame la historia de cómo se inventó el ratón del ordenador.", "LLM"),
    ("¿Cuánta RAM recomiendas para editar vídeo?", "LLM"),
    ("es verdad q borrar archivos no los elimina del todo del disco?", "LLM"),
    ("¿Qué significa copiar y pegar? Mi nieto lo dice mucho y no lo entiendo.", "LLM"),
    ("diferencia entre wifi 5 y wifi 6", "LLM"),
    ("Mi batería del coche está descargada, ¿qué hago?", "LLM"),
    ("¿Es seguro conectarse a la wifi gratis de un aeropuerto?", "LLM"),
    ("hazme un resumen de para q sirve el comando grep en linux", "LLM"),
    ("¿Cuál es mejor para leer, una pantalla OLED o una LCD?", "LLM"),
    ("Tengo que escribir una redacción para clase sobre la memoria histórica, ¿me ayudas con la introducción?", "LLM"),
    ("q es mejor un ssd o un disco duro normal", "LLM"),
    ("¿Cómo puedo explicar a mis alumnos qué es un archivo y qué es una carpeta?", "LLM"),
    ("dame ideas para organizar mejor mis carpetas de fotos", "LLM"),
    ("Si reinicio el router, ¿pierdo la configuración?", "LLM"),
    ("¿Por qué se calienta tanto el procesador cuando juego?", "LLM"),
    ("traduce al ingles: la impresora no tiene papel", "LLM"),
    ("¿Qué es una dirección IP y para qué sirve?", "LLM"),
    ("cuanto brillo es recomendable para no cansar la vista?", "LLM"),
    # Conversación y otras peticiones generales
    ("hola, ¿qué tal estás?", "LLM"),
    ("Buenos días, solo quería darle las gracias por la ayuda de ayer.", "LLM"),
    ("cuentame un chiste", "LLM"),
    ("¿Cuál es la capital de Australia?", "LLM"),
    ("q tiempo hace en sevilla en verano normalmente", "LLM"),
    ("Escríbeme una felicitación de Navidad para mis vecinos.", "LLM"),
    ("¿Cuántos gramos de arroz pongo por persona en una paella?", "LLM"),
    ("explicame la fotosintesis como si tuviera 10 años", "LLM"),
    ("¿Me recomiendas algún libro de misterio?", "LLM"),
    ("cuanto es 17 por 23", "LLM"),
    ("Estoy un poco triste hoy, ¿podemos hablar un rato?", "LLM"),
    ("q significa la palabra efimero", "LLM"),
    ("¿Quién ganó el Mundial de fútbol de 2010?", "LLM"),
    ("dame un plan de entrenamiento para correr 5 km", "LLM"),
    ("Hazme una lista de ideas para el cumpleaños de mi hija de ocho años.", "LLM"),
    ("como se dice buenas noches en italiano", "LLM"),
    ("¿Qué es la inteligencia artificial?", "LLM"),
    ("resume en tres frases la revolución francesa", "LLM"),
    ("Mi perro no quiere comer desde ayer, ¿debería preocuparme?", "LLM"),
    ("vale gracias", "LLM"),
    ("¿Qué diferencia hay entre «haber» y «a ver»?", "LLM"),
    ("escribe una historia corta de dragones para leer antes de dormir", "LLM"),
    ("¿Por qué el cielo es azul?", "LLM"),
    ("ayudame a escribir un mensaje para pedir perdon a un amigo", "LLM"),
]
