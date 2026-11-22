# Parche 258: el LCM entiende más cosas y de más maneras

Requiere el parche 257. Modelo nuevo, entrenado otra vez y medido con un examen ciego escrito antes de entrenar.

## Qué cambia

**Más formas de pedir lo mismo.** El entrenamiento combina cada orden con muchas fórmulas: directas («borra notas.txt»),
educadas e indirectas («¿serías tan amable de decirme qué archivos contienen la palabra casa?», «¿te importaría cerrar el
navegador?», «¿me harías el favor de…?», «quisiera…», «¿sabrías decirme…?», «tengo curiosidad por saber cuánta batería queda»),
telegráficas («batería?», «disco libre»), con contexto delante («estoy preparando la clase y necesito una carpeta…»), con
cortesías y prisas detrás, con abreviaturas («xfa», «q») y sin tildes. Las mismas fórmulas se entrenan también con preguntas que
no son órdenes («¿serías tan amable de explicarme qué es la memoria RAM?»), para que la educación no se confunda con una orden.

**Más órdenes: de 66 a 98.** Nuevas: cerrar aplicaciones, qué archivos contienen un texto, el maquetador (con o sin archivo),
el reproductor (abrir, pausar, parar), callar la voz, el editor de código con un archivo, perfiles de energía (rendimiento,
equilibrado, ahorro), redes WiFi guardadas, dónde se guardan los archivos, informe, ficha del sistema, quién creó SOMA, lo que
ocupa una carpeta, núcleos, guardar en disco, restaurar la copia de seguridad, USB, recolocar el escritorio, ejecutar un programa
de Python, un script o una aplicación SOMA, transcribir un audio, pedir IP nueva y la memoria de los agentes.

**Dos órdenes nuevas en SOMA** (`src/agent/lcm_ordenes_shell.inc`):

    cerrar                 la ventana de delante
    cerrar todo            todas (también las minimizadas)
    cerrar <aplicación>    navegador, terminal, paint, notas, youtube, reproductor, ajustes, cámara... (con sinónimos:
                           internet, consola, explorador, fotos, dibujo, editor de código, configuración, webcam...)
    contiene <texto> [ruta] [-a]   qué archivos tienen ese texto DENTRO (find busca por nombre; grep, en un solo archivo)

**Números con palabras.** «Pon el brillo al cuarenta y cinco», «las cinco primeras líneas», «mata la tarea doce». Solo se
convierten si la orden pide un número y no lo encuentra en cifras («tengo dos gatos» no se toca).

**Varias órdenes en una frase.** «Abre el navegador y luego pon el brillo al 50». Se encadenan si todas son claras y ninguna
pide confirmación. Si alguna la pide («abre Paint y después borra notas.txt»), no hace solo una: te pide ir de una en una.

**El tipo de archivo decide con qué se abre**: un .pdf con el lector, una imagen con el visor, el resto con cat.

**Los datos tienen forma**: un archivo lleva extensión o ruta, un equipo un punto o cifras; «la», «llamada», «abierta»... no son
datos (en «crea una carpeta llamada viajes» el dato es viajes). La cortesía del final («... por favor», «... ya») no entra en el
dato. Si un dato no encaja, pregunta en vez de inventar.

## Medidas

Examen 5, CIEGO (286 casos, escrito por otro agente antes de entrenar, con las 97 formas de orden; medido una sola vez):

| | órdenes exactas | mal ejecutadas | mal sin preguntar | peligrosas sin confirmar |
|---|---|---|---|---|
| mayordomo de antes | 8,5 % | 15,4 % | 15,4 % | 1 |
| LCM del 253 | 41,9 % | 10,5 % | 9,4 % | 0 |
| LCM del 258 | 79,1 % | 2,1 % | 1,75 % | 0 |

Los 6 fallos: find notas (por apuntes), habla con media frase, abrir la grabadora en vez de cerrarla, wifi buscar por wifi
guardadas, python saludo.soma (por run; pide confirmación) y la RAM ante «tengo mala memoria para los nombres». 51 de 52
preguntas que no eran órdenes fueron al LLM.

Desarrollo (691 casos de los exámenes 1-4 y la batería de estrés, ya vistos; con las mismas reglas para los dos):
253: 68,6 % exactas, 2,0 % mal, 1,6 % sin preguntar · 258: 83,8 % exactas, 1,6 % mal, 1,2 % sin preguntar.
Con erratas: 3,7 % mal (253: 5,0 %). Con cortesías («oye, ... gracias»): 1,3 % mal (253: 2,6 %).
2.281 frases de UD que no son órdenes: ejecutarían algo 3 (0,13 %), nada peligroso.

El modelo es un conjunto de dos redes entrenadas con semillas distintas (11 y 29); el núcleo promedia sus probabilidades.
Una sola red acertaba igual o más, pero se equivocaba con más seguridad (3,2 % mal): el conjunto quitó esa regresión.
C del núcleo frente a Python: 4.764 frases, 99,8 % mismo resultado, ninguna orden distinta. Modelo de 4,4 MB.

Criterio fijado al principio (>= 90 % exactas, <= 1 % mal) todavía NO se cumple en un examen ciego, pero la distancia se ha
reducido mucho (253: 52 % y 3,9 % en su examen ciego; 258: 79 % y 2,1 %).

Reentrenar el conjunto:
    LCM_TRAD_RUTA=/tmp/a.pt python3 -c "from tools.lcm_ordenes.traduce import entrena; entrena(n=150000, epocas=8, semilla=11)"
    LCM_TRAD_RUTA=/tmp/b.pt python3 -c "from tools.lcm_ordenes.traduce import entrena; entrena(n=150000, epocas=8, semilla=29)"
    LCM_TRAD_RUTA=/tmp/a.pt:/tmp/b.pt python3 -m tools.lcm_ordenes.exhaustivo soma --estres --examen5
    LCM_TRAD_RUTA=/tmp/a.pt:/tmp/b.pt python3 -m tools.lcm_ordenes.exporta_c /tmp/m.bin --inc src/agent/lcm_ordenes_pesos.inc


## Archivos

Nuevos: `src/agent/lcm_ordenes_shell.inc`, `tests/test_lcm_258_ordenes.c`, `tools/lcm_ordenes/examen5.py` y `estres.py`, este
documento. Sustituidos (los creó el 253 o el 257): `src/agent/lcm_ordenes.inc`, `lcm_ordenes_pesos.inc`, `lcm_soma.inc`,
`tests/test_lcm_ordenes_253.c` (casos del modelo nuevo), `tests/test_lcm_257.c`, `tools/lcm_ordenes/*.py`. Cambiados:
`src/agent/shell.c` (incluye las órdenes nuevas tras `shell_utilidades.c` y las añade a la tabla), `Makefile` (shell.o depende
de `lcm_ordenes_shell.inc`) y, si encaja, `protected.txt`.

Revertir: `python3 parche_258_lcm_ordenes_ampliado.py --revertir` (antes que el 257 y el 253).
