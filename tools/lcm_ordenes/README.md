# LCM · traductor de peticiones a órdenes (parche 253)

Herramientas para reentrenar, medir y regenerar el modelo que va dentro del núcleo (`src/agent/lcm_ordenes_pesos.inc`).
Se ejecutan desde la raíz de SOMA. Necesitan Python 3 y PyTorch (solo en el equipo donde se entrena, nunca en SOMA).

    python3 -m tools.lcm_ordenes.prepara                     descarga los corpus UD (frases reales para entrenar)
    python3 -m tools.lcm_ordenes.traduce entrena             entrena (unos 10 min en CPU) -> tools/lcm_ordenes/traductor.pt
    python3 -m tools.lcm_ordenes.mide lcm -v                 examen 1   (--examen2 para el segundo)
    python3 -m tools.lcm_ordenes.exhaustivo mixto -v         todas las pruebas (exámenes, variantes, miles de frases de UD)
    python3 -m tools.lcm_ordenes.exporta_c /tmp/m.bin --inc src/agent/lcm_ordenes_pesos.inc     pesos nuevos para el núcleo
    cc -O2 -Isrc/agent -o /tmp/lcm_host tools/lcm_ordenes/lcm_host.c && python3 -m tools.lcm_ordenes.paridad_c /tmp/m.bin /tmp/lcm_host

Para añadir una orden: una línea en `catalogo.py` (id, plantilla de la orden, si pide confirmación, frases de ejemplo),
reentrenar, medir, exportar. Si la medida empeora en lo ya conseguido, no se exporta.

Medidas con las que se entregó:
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

