# Parche 272 · Calendario de SOMA (agenda con avisos activos)

Un calendario completo, pensado para que lo usen **tú, el LCM y el LLM por igual**: todos hablan con la misma orden, `agenda`.

## Qué trae

* **Ventana «Calendario»** (icono nuevo en el escritorio): vistas Mes, Semana y Día, navegación con `<` `>` `Hoy`, color por categoría
  y una línea abajo para apuntar en lenguaje natural. Con la línea vacía, las flechas del teclado cambian de día/semana/mes; `Tab` cambia de vista.
* **Orden `agenda`** (terminal, LCM y LLM): `agenda hoy | mañana | semana | mes | proximos | ver el jueves | añadir <texto> | borrar N | mover N <fecha> |
  saltar N <día> | buscar <texto> | detalle N | exportar | importar | avisos ... | abrir`. Sin argumentos: hoy y los próximos 7 días.
* **Español natural**: «el miércoles día 6 a las 13 horas tengo un partido de fútbol con los amigos», «mañana tengo reunión a las 9:30»,
  «cada lunes a las 8 gimnasio», «dentro de 2 horas llamar a Ana», «del 3 al 7 de marzo vacaciones», «a las 8 menos cuarto»,
  «de 10 a 12», «el sábado 12», fechas `dd/mm`, ISO, etc. La categoría (deporte, trabajo, salud, social, viaje, familia, estudio) se deduce del texto.
* **Repeticiones** (cada día / semana / mes / año / laborables, con fecha final), excepciones de un solo día, eventos de día completo y de varios días.
* **Avisos activos** (los da el bucle de la GUI aunque la ventana esté cerrada): en pantalla y **hablados** con la voz de SOMA, «En 14 minutos: Cena con
  los suegros, a las 20:00». Cada aviso se da una sola vez. Los eventos de día completo avisan a las 09:00.
* **Ajustes → Avisos**: activar/desactivar, voz sí/no, pantalla sí/no, antelación (5/10/15/30 min, 1 h) y «Probar aviso».
  También por orden: `agenda avisos off | voz off | visual on | antes 15 | probar`.
* **Hora local**: el reloj del equipo (UTC) se pasa a hora de Madrid con el cambio de verano/invierno correcto.

## Dónde vive cada cosa

| Qué | Dónde |
| --- | --- |
| Eventos | `/agenda/eventos.txt` (texto propio, una línea por evento; va a la partición DATOS) |
| Ajustes de avisos | `/etc/agenda.conf` |
| Exportar / importar | `.ics` estándar (`agenda exportar` → `/usuario/agenda.ics`; `agenda importar /usuario/x.ics`) |
| Lógica pura (fechas, repeticiones, lenguaje, .ics, avisos) | `src/apps/agenda_core.c` (+ `agenda_nl.inc`, `agenda_ics.inc`) |
| Sistema (guardado, reloj, avisos, orden) | `src/apps/agenda.c` |
| Ventana y avisos activos | `src/apps/agenda_gui.c` |
| Declaración para el LCM | `lcm/manifiestos/agenda.json` |

## Para el LCM y el LLM

`lcm/manifiestos/agenda.json` declara 11 órdenes (hoy, mañana, semana, próximos, ver, añadir, borrar, abrir, avisos on/off, exportar) con frases en
castellano, valores de ejemplo y **frases de prueba retenidas**. La pieza `{T}` recoge el texto con la fecha («el miércoles día 6 a las 13 horas partido de
fútbol») y la orden `agenda añadir {T}` lo entiende. `agenda borrar` pide confirmación. Mientras el modelo de fábrica no se reentrene, `agenda ...`
escrita tal cual funciona siempre y el LLM puede usarla como cualquier orden. Para que el LCM lo aprenda:

    export LCM_UD=carpeta_UD; python3 -m tools.lcm_ordenes.reentrena --semillas 11,29,41

La puerta de regresión (271) solo lo aplica si no empeora nada de lo ya conseguido.

## Punto de integración con tu proyecto de memoria

* Lectura: `ag_agenda_rango(cal, desde, hasta, ...)` (núcleo) o `agenda ver <fecha>` / `agenda detalle N` (orden). El archivo `/agenda/eventos.txt` es texto plano y estable.
* Escritura: `agenda_anade_texto("mañana a las 9 reunión con Marta", ...)` en C, o `agenda añadir ...` por orden.
* Sincronizar con Google Calendar: `agenda exportar` / `agenda importar` ya hablan `.ics` (hora flotante local, `RRULE`, `EXDATE`, `VALARM`).
  Falta solo el transporte (CalDAV/API de Google), que se puede añadir encima sin tocar el formato.

## Límites honestos

* Máximo 400 eventos. Zona horaria fija: Europa/Madrid.
* Los avisos hablados dependen de que la voz esté instalada y solo existen en x86_64 (igual que el Bloc de notas).
* El LCM de fábrica **no** conoce aún las frases de agenda (hay que reentrenarlo, ver arriba); el LLM y la orden escrita sí.

## Pruebas

`tests/test_agenda_272.c` (151: fechas, bisiestos, horario de verano, repeticiones, lenguaje, .ics, avisos, fuzz de 64 000 frases con ASAN/UBSAN),
`tests/test_agenda_cmd_272.c` (37: la orden completa con guardado y avisos), `tests/test_agenda_manifiesto_272.py` (170: cada orden que el LCM
puede producir se ejecuta con la orden real). `make test` no hace falta; sí `make`.
