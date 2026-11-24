# Parche 295 · Correo: correo electrónico con IA, unido al orbe

SOMA_295_CORREO. Una aplicación de correo nueva para el escritorio, **Correo**, que habla IMAP y SMTP de verdad con el cliente
TLS 1.3 de SOMA. Tiene la IA dentro de la propia interfaz y el orbe la puede usar.

## Qué se puede hacer

**Cuenta.** La primera vez que se abre Correo pide nombre, dirección y clave, y pone solos los servidores del proveedor:

| Proveedor | Recibir (IMAP) | Enviar (SMTP) | Nota |
|---|---|---|---|
| Gmail | `imap.gmail.com` 993 | `smtp.gmail.com` 465 | Contraseña de aplicación (con verificación en dos pasos). Gmail guarda los enviados solo. |
| Yahoo | `imap.mail.yahoo.com` 993 | `smtp.mail.yahoo.com` 465 | Contraseña de aplicación. |
| iCloud | `imap.mail.me.com` 993 | `smtp.mail.me.com` 587 (STARTTLS) | Contraseña específica para apps. |
| GMX | `imap.gmx.com` / `.net` 993 | `mail.gmx.com` / `.net` 587 (STARTTLS) | Hay que activar IMAP en GMX. |
| Zoho | `imap.zoho.com` / `.eu` 993 | `smtp.zoho.com` / `.eu` 465 | |
| Outlook / Hotmail | `outlook.office365.com` 993 | `smtp-mail.outlook.com` 587 | **Aviso:** Microsoft ya solo acepta OAuth, que SOMA no tiene; lo más probable es que rechace la clave. |
| Otro dominio | `imap.<dominio>` 993 | `smtp.<dominio>` 465 | En «Servidores» se cambia el servidor, el puerto, TLS o STARTTLS y el usuario. |

«Probar y guardar» entra en los dos servidores antes de guardar nada.

- Si algo falla, dice qué parte ha fallado y qué contestó el servidor (por ejemplo, `[AUTHENTICATIONFAILED] Invalid credentials`).
- La cuenta anterior se conserva.
- La clave se guarda en `/etc/correo.conf` codificada en base64. No va en claro a la vista, pero **no está cifrada**: queda solo en el equipo.

**La ventana** tiene tres columnas:

- **Carpetas.** Las del servidor, con su nombre en castellano y los no leídos. Debajo, las vistas con IA: Importantes, Sin leer y Destacados.
- **Lista.** Avatar, remitente, fecha (hoy con hora, «ayer», día de la semana o fecha), asunto, extracto del texto, etiqueta de la IA, clip si hay adjuntos y estrella.
- **Lectura**, con la barra de acciones:
  - responder, responder a todos y reenviar;
  - destacar y marcar como no leído;
  - archivar (en Gmail, quitarlo de Recibidos) y papelera (desde la papelera, borrar del todo);
  - «Añadir al calendario».

Más cosas de la ventana:

- Los adjuntos se guardan en `/usuario`.
- Si la ventana es estrecha, la lectura tapa la lista y hay un botón para volver.
- Lo leído queda en una copia local (`/correo`), así que al abrir se ve al instante y después se sincroniza solo. Las carpetas se refrescan cada 3 minutos mientras la ventana está abierta.

**Atajos de teclado:** `c` redactar, `r` responder, `a` a todos, `f` reenviar, `s` estrella, `u` no leído, `e` archivar, `#` o Supr
borrar, flechas para moverse y `/` para buscar. En la redacción, Ctrl+Intro envía.

## La IA en la interfaz

- **Búsqueda con IA** (botón «IA» de la barra de búsqueda): «el correo donde me hablaban del presupuesto de la reforma».
  1. El modelo convierte la petición en un filtro: remitente, destinatario, asunto, palabras, fechas, sin leer, con adjuntos o categoría.
  2. Se busca en la copia local y en el servidor (`UID SEARCH`).
  3. Si no sale nada, o salen demasiados, el modelo elige entre los candidatos.
  4. Debajo se ve en claro lo que entendió.
- **Búsqueda escrita**, sin IA: `de:ana asunto:"factura luz" tiene:adjunto no leídos hoy` (también ayer, esta semana, este mes,
  `antes:` y `después:`).
- **Resumir** un correo en 2 a 4 frases, con «Te pide:» o «Fecha:» cuando los hay.
- **Responder con IA.** Abre la respuesta y la IA la escribe en el tono elegido. En la fila de la IA se le puede decir qué
  contestar. La cita del original se conserva.
- **Escribir con IA** un correo nuevo. El destinatario se saca de los contactos (Enviados y Recibidos): «dile a Juan que el
  lunes no puedo» pone `Juan García <juan@obra.es>`.
- **Mejorar el borrador:** Corregir, Formal, Cercano, Breve, Inglés. Solo se reescribe lo nuevo, no la cita.
- **Importantes.** La IA clasifica la bandeja en importante, personal, trabajo, avisos o promociones, y lo marca en la lista.
- **Añadir al calendario.** La IA saca del correo la cita o el plazo y lo apunta en la Agenda.

Todo lo que escribe la IA queda en la ventana para revisarlo. Nunca se envía nada sin pulsar Enviar o decir «envíalo».

## Desde el orbe

| Se dice | Qué hace |
|---|---|
| «escribe un correo a María diciéndole que llego 10 minutos tarde» | Redacta con la IA, busca la dirección en los contactos y abre Correo con el borrador. Contesta: «He preparado el correo para María López <…> con asunto «Llego tarde»… pulsa Enviar o dime «envíalo»». Si no sabe la dirección, lo dice y deja el campo Para listo para escribirla. |
| «responde al correo de Ana diciéndole que sí» | Lo mismo, como respuesta en el hilo del último correo de Ana. |
| «envíalo» / «sí, mándalo» | Envía el borrador que preparó el orbe, tal como esté en la ventana (si el usuario lo ha cambiado, va con los cambios). |
| «¿tengo correos nuevos?» / «resume mis correos de hoy» | Sincroniza Recibidos y la IA cuenta qué hay y qué piden. |
| «busca el correo de Juan con las fotos de la obra» | Búsqueda con IA. Los resultados quedan en Correo y el orbe dice los primeros. |
| «abre el correo» | Abre la aplicación. |

Lo que lleva «en internet», lo de programar («haz un programa que mande correos») y las preguntas («¿qué es un correo
electrónico?») no van al correo. La orden de terminal es `correo [abrir | estado | sincroniza | busca <texto>]`.

## Cómo está hecho

| Archivo | Qué es |
|---|---|
| `src/apps/correo_core.c/.h` | Lógica pura, probada en el anfitrión (detalle debajo). |
| `src/apps/correo_red.c/.h` | Sesiones IMAP4rev1 y SMTP sobre un transporte cualquiera (detalle debajo). |
| `src/apps/correo.c/.h` | La aplicación, más el transporte de SOMA (TCP + `tls13` con `tls_trust`, el mismo que el navegador). Un hilo `correo` hace la red y la IA desde una cola, y los resultados se aplican de una vez. |
| `src/agent/agent_ruta.c` | `ar_correo` decide qué es del correo. |
| `src/agent/agent.c` | Las rutas del orbe (antes de investigar en internet). |
| `src/agent/shell_correo.c` | La orden `correo`. |
| `src/ui/*` | Correo en el escritorio: icono vectorial (sobre), color, título, tamaño y teclas. |

`correo_core.c` contiene:

- base64, quoted-printable, juegos de caracteres (UTF-8, Latin-1/9 y Windows-1252; si un correo que dice UTF-8 no lo es, se lee
  como Windows-1252), RFC 2047, RFC 2231, UTF-7 de IMAP y HTML a texto;
- lectura MIME (texto y adjuntos);
- respuestas IMAP (literales, FETCH, LIST con usos especiales, SEARCH) y SMTP;
- composición RFC 5322 en UTF-8 con quoted-printable, In-Reply-To y References;
- filtros y búsqueda, contactos, lo que contesta el modelo y los proveedores.

`correo_red.c` cubre:

- TLS directo y STARTTLS. Nunca se manda la clave sin cifrar.
- `AUTHENTICATE PLAIN`, o `LOGIN` con literal si la clave lleva caracteres especiales.
- MOVE, o COPY + `\Deleted` + EXPUNGE si el servidor no tiene MOVE.
- Copia en Enviados (APPEND) y mensajes grandes pedidos solo hasta 4 MB.
- AUTH PLAIN o LOGIN en SMTP, y errores en claro: qué destinatario rechazó el servidor y por qué.

El núcleo crece unos 190 KB de código.

## Pruebas

| Prueba | Comprobaciones | Qué cubre |
|---|---|---|
| `tests/test_correo_295.c` | 154 | Toda la lógica pura y las rutas del orbe. |
| `tests/test_correo_red_295.c` | 46 | El cliente IMAP/SMTP de SOMA con su TLS 1.3, contra los servidores de prueba con TLS 1.3 de OpenSSL. |
| `tests/test_correoapp_295.c` | 81 | La aplicación entera usada como un usuario, contra los mismos servidores y con un modelo de mentira. |
| `tests/test_correo_red_295.py` | 31 | Lo que queda en los servidores tras las dos anteriores. |

Los servidores de prueba están en `tests/correo_servidores_295.py`:

- un IMAP con AUTH=PLAIN, MOVE y UIDPLUS;
- otro IMAP con STARTTLS, LOGINDISABLED antes de cifrar y sin MOVE;
- SMTP en 465 y en 587 con STARTTLS.

La prueba del cliente (46 comprobaciones) recorre las dos formas de entrar, las carpetas, STATUS, SELECT, la lista y lo nuevo,
el mensaje entero con su adjunto, el corte de los grandes, las banderas, SEARCH, mover con y sin MOVE, borrar, APPEND, los envíos
y los errores.

La prueba de la aplicación (81 comprobaciones) usa los botones y teclas de verdad para:

- dar de alta la cuenta (también con la clave mala);
- leer, resumir y guardar el adjunto;
- responder con IA, pasarlo a formal y enviar;
- destacar, borrar y marcar como no leído;
- buscar escrito y con IA, también cuando la IA elige entre candidatos;
- Importantes, el calendario y redactar con IA;
- el orbe: redactar, «envíalo», leer y buscar;
- la ventana estrecha y la orden `correo`.

Además guarda capturas en PNG si se le pasa una carpeta.

La parte de Python (31 comprobaciones) mira en los servidores:

- banderas, Papelera y Enviados;
- que antes de cifrar solo se dijo CAPABILITY y STARTTLS, y que la contraseña no aparece en ninguna orden;
- los correos recibidos, leídos con el analizador de correo de Python: asunto, remitente y destinatarios con tildes y comas,
  Cco solo en el sobre, el hilo, la zona horaria y el cuerpo exacto (incluida una línea que empieza por punto).

`make test` exige las dos últimas solo en Linux x86_64. Necesitan `openssl` y un `python3` con TLS 1.3. En otro equipo se omiten
diciéndolo.

**Lo que no se ha podido probar aquí:** servidores reales (Gmail, iCloud…). Desde el entorno de desarrollo no hay salida a sus
puertos. El TLS es el mismo que ya usa el navegador con esos dominios.
