# Parche 257: el LCM habla como una persona

Requiere el parche 253 (LCM: peticiones a órdenes). No cambia el modelo: lo ya medido sigue igual.

## Qué cambia para ti

**Charla sin el LLM.** Saludos, «¿cómo estás?», «gracias», «adiós», «¿quién eres?», «¿qué sabes hacer?», «vale», «bien, ¿y tú?»…
Contesta el LCM al momento, con varias fórmulas distintas para cada cosa (nunca repite la última) y teniendo en cuenta la hora:
a las 4 de la madrugada dice «buenas noches». Si saludas con «buenos días», te contesta igual.
Si el saludo va delante de una petición («hola, ¿qué hora es?») contesta las dos cosas: «Buenas noches. Son las 2 y 40 de la madrugada.»

**Respuestas para la voz.** Cuando la orden la decide el LCM (no cuando escribes la orden tal cual en el terminal), la respuesta es una
frase corta y natural en vez de la salida de la orden:

| Pides | Antes | Ahora |
|---|---|---|
| qué hora es | `[LCM] -> date` `CEST: 2026-11-22 05:45:10 CEST` | Son las 5 menos cuarto de la madrugada. |
| qué día es hoy | (lo mismo) | Hoy es domingo, 22 de noviembre de 2026. |
| cuánta memoria queda | la tabla de `ram` | Estás usando 612 megas de 7 coma 7 gigas, un 8 por ciento. |
| qué archivos tengo | el listado | Tienes 7 cosas: notas.txt, fotos, informe.pdf, datos.csv, a.txt y 2 más. |
| cuánta batería queda | la tabla de ACPI | Te queda un 76 por ciento, unas 3 horas y 10 minutos. |
| está caliente el procesador | perfiles, MHz, RAPL... | El procesador está a 47 grados, en modo equilibrado. |
| cuál es mi ip | `ARCH: ... Red: ...` | Estás conectado con la dirección 192.168.1.37. |
| busca el archivo presupuesto | rutas y recuento | Hay 2 que encajan: presupuesto_2026.xlsx y presupuesto.txt. |
| abre el navegador / pon el brillo al 40 | `[LCM] -> ...` | Ahí tienes el navegador. / Pongo el brillo al 40 por ciento. |

En el orbe, la orden y su salida siguen visibles como «razonamiento» (el orbe lo enseña pero no lo lee); la voz solo lee la frase.
En el terminal sale `[LCM] -> orden` y la frase. La hora es la de Madrid de verdad (UTC+1 en invierno, UTC+2 en verano), aunque la orden
`date` siga diciendo «CEST» todo el año.

**Confirmaciones variadas**: «¿Borro notas.txt? Dime sí o no.», «Voy a reiniciar. ¿Lo hago?», «¿Copio a.txt en b.txt?»... Valen «sí»,
«vale», «adelante», «claro», «dale»... y «no», «déjalo», «mejor no»... Ahora también piden confirmación crear carpetas y archivos.

**Aprende de sus errores.** Si se equivoca, dile «no era eso» (o «te has equivocado», «eso no»...). Te pide la orden buena; escríbela
tal cual (p. ej. `ping servidor.local`) y la ejecuta y la recuerda. La próxima vez, una frase igual con otro dato también vale
(«mira si llego a google.com» → `ping google.com`). Lo aprendido va antes que la red, se guarda en `/etc/lcm_aprendido.txt` y lo que
cambia algo sigue pidiendo confirmación.

## Ajustes > LCM (o `ajustes lcm`)

Encendido / apagado · respuestas naturales o técnicas · seguridad (umbral 60, 70, 80 o 90 %) · probar una frase sin ejecutar nada ·
lo aprendido (con «Olvidar todo») · cuántas peticiones atendió, cuántas fueron al LLM, cuántas se confirmaron o cancelaron.
Se guarda en `/etc/lcm.conf` (lee también el formato del 253).

## Orden `lcm`

    lcm estado | encender | apagar | voz natural | voz tecnica | umbral 80
    lcm aprendido | lcm aprende mira si llego a google.com => ping google.com | lcm olvida 2 | lcm olvida todo
    lcm <frase>          qué haría, sin ejecutar

## Lo que no cambia

El modelo, la seguridad del 253 (solo peticiones del usuario; nada en modo agente; caracteres de shell rechazados), y las órdenes
escritas tal cual. Con «voz técnica» o `lcm apagar` se vuelve al comportamiento anterior sin recompilar.

## Archivos

Nuevos: `src/agent/lcm_soma.inc`, `tests/test_lcm_257.c` (35 comprobaciones de los flujos), `tests/test_lcm_257_nucleo.c`
(compila como el núcleo, sin coma flotante), este documento. Cambiados: `src/agent/agent.c` (el pegamento del 253 pasa a
`#include "lcm_soma.inc"`), `src/agent/shell_apps.c` (frases de batería y procesador, `ajustes lcm`), `src/apps/settings.c`
(página LCM), `Makefile` (agent.o depende de `src/agent/lcm_*.inc`) y, si encajan, `protected.txt` y el uso de `lcm` en `shell.c`.

Revertir: `python3 parche_257_lcm_natural.py --revertir` (antes que el 253, si quitas los dos).
