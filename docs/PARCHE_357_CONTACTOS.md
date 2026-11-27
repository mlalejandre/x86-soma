# Parche 357: Contactos (libreta con fotos, voz, vCard y cumpleaños)

Una aplicación nueva, **Contactos** (icono rosa, junto al Calendario), y la orden `contactos`.

## Qué guarda cada ficha
Nombre, apellidos, apodo, hasta 3 emails y 3 teléfonos (con su tipo: móvil, casa, trabajo, otro), dirección (calle, código postal,
ciudad, provincia, país), fecha de nacimiento (con o sin año), grupos (Familia, Amigos, Trabajo… varios a la vez), empresa, cargo, web,
nota, favorito, **hasta 6 fotos** y **una muestra de voz** (hasta 30 s).

- Fichas: `/contactos/contactos.vcf` en DATOS (vCard 3.0 con campos `X-SOMA-*`; sin fotos).
- Fotos: `/contactos/fotos/<id>_<n>.jpg` en BIGFS. Toda foto se decodifica aislada (imgdec, ring 3), se reduce a 640 px y la vuelve a
  codificar SOMA en JPEG: en el disco nunca queda el archivo original que llegó de fuera.
- Voz: `/contactos/voz/<id>.wav` en BIGFS (16 kHz, mono). Se graba y se escucha desde la pestaña **Voz** de la ficha. Si la Grabadora
  tiene algo sin guardar, Contactos no lo pisa.

## La ventana
Lista a la izquierda (buscador sin acentos ni mayúsculas, filtros Todos / ★ / Cumpleaños / grupos) y la ficha a la derecha con tres
pestañas: **Ficha** (pulsa un dato para editarlo; Intro guarda, Tab pasa al siguiente, Esc cancela; pulsa el tipo de un teléfono o email
para cambiarlo), **Fotos** (añadir desde un archivo o desde la Cámara, elegir la de la ficha, quitar) y **Voz**. «+ Nuevo» acepta la ficha
en una frase: *Ana López, móvil 600 11 22 33, email ana@x.es, nació el 3 de mayo de 1990, vive en Calle Mayor 3, Madrid, grupo familia*.
«Escribir correo» abre el Correo con el destinatario puesto.

## Sincronizar con Google (y con iCloud, Outlook o el móvil)
Google no deja usar contraseñas de aplicación para los contactos (solo OAuth), así que la sincronización es por archivo **vCard**, en los
dos sentidos:

1. **De Google a SOMA**: contacts.google.com → Exportar → *vCard (para contactos de iOS)*. Pasa el `.vcf` a `/usuario` y en SOMA:
   `contactos importar /usuario/contacts.vcf` (o «Importar» en la ventana, que sugiere los `.vcf` que encuentra).
2. **De SOMA a Google**: `contactos exportar` (o «Exportar») escribe `/usuario/contactos.vcf` con la foto de cada ficha; en Google:
   Importar → ese archivo. Google junta los duplicados en «Combinar y corregir».

Reglas de la fusión al importar: no se duplican contactos (mismo UID, mismo email o mismo nombre completo). Un campo vacío en el archivo
nunca borra lo que hay en SOMA. Si una ficha la has cambiado en SOMA desde la última exportación, en un conflicto gana lo de SOMA (salvo que
el archivo traiga un `REV` más nuevo); si no, gana lo importado. Exportar deja todas las fichas como «sincronizadas». Los borrados no se
propagan (hay que borrar en los dos lados). Las fotos que Google exporta como enlace se descargan solas, de una en una, cuando hay red.

Formatos que se leen: vCard 2.1 (móviles antiguos: quoted-printable, latin-1), 3.0 y 4.0; líneas plegadas, grupos `itemN.`, `TYPE` en
todas sus formas, `CATEGORIES` (`starred` de Google = favorito), fotos en base64 o `data:`, cumpleaños sin año (`--MM-DD`, el `1604` de
Apple). El `.vcf` que llega de fuera **solo se lee en el servicio aislado mimesvc (ring 3)**; el núcleo recibe fichas de tamaño fijo y las
sanea (cadenas terminadas, UTF-8 válido, tipos y fechas en rango, fotos dentro de lo recibido). Exportar nunca incluye claves ni `.conf`.

## Cumpleaños en el Calendario
Cada contacto con fecha de nacimiento tiene en el Calendario un evento anual de todo el día, «Cumpleaños de …», con el aviso de siempre
(a las 9:00 si los avisos están activos). Se mantienen solos: si cambias o quitas la fecha, el evento cambia o desaparece. Nunca ocupan los
últimos 100 huecos del Calendario y no salen en el `.ics` exportado (van en el `.vcf`).

## El orbe y el agente
- Órdenes nuevas para el LCM (`lcm/manifiestos/contactos.json`, 12 órdenes): «¿cuál es el móvil de Ana?», «¿dónde vive Luis?»,
  «¿cuándo es el cumpleaños de Marta?», «¿quién cumple años esta semana?», «añade a mis contactos …», «borra a … de mis contactos» (pide
  confirmación), «abre mis contactos», «exporta mis contactos», «importa los contactos de …». Hace falta reentrenar el LCM (ver
  `lcm/README.md`) para que el orbe las aprenda; mientras tanto el agente las usa por la orden `contactos`.
- Cuando una petición al agente habla de alguien de la libreta (nombre y apellido, o nombre con «teléfono», «email», «llamar»,
  «felicitar»…) o de cumpleaños, su ficha entra en el contexto: el modelo responde con los datos reales en vez de inventarlos.

## La orden
```
contactos                          resumen y próximos cumpleaños
contactos ana                      busca (si hay uno, su ficha)
contactos ver 12 | ver Ana López
contactos añadir Luis Pérez, email luis@x.es, móvil 600 11 22 33, nació el 3 de mayo de 1990
contactos editar Ana movil 611 22 33 44      (campo vacío o "-" lo vacía)
contactos telefono|email|direccion|cumpleaños Ana
contactos grupos | grupo familia | favoritos | cumpleaños [días]
contactos foto Ana /fotos/foto_003.jpg
contactos exportar [ruta.vcf] [sin fotos] | importar ruta.vcf
contactos abrir [nombre]
```

## Pruebas
- `tests/test_contactos_357.c`: núcleo (lenguaje natural, fechas, búsqueda, edición, vCard de Google/Apple/2.1, fotos plegadas,
  ida y vuelta, fusión en los dos sentidos, saneado y 30 000 entradas al azar): unas 31 700 comprobaciones.
- `tests/test_contactos_manifiesto_357.py`: el manifiesto del LCM contra la orden real y la integración de `contactos.c` con
  `agenda.c` (cumpleaños, `.ics`, importar de Google, contexto del agente).
