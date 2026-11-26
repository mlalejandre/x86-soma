# Parche 320 · Pruebas con datos hostiles: la wifi y el LLM

**No hace falta `make`**: solo añade pruebas (corren en `make test` y en la CI).

El parche 319 encontró un fallo real en el correo con un fuzzer de pocas líneas. Aquí se hace lo mismo con otros dos lectores del
núcleo que reciben datos de fuera y no tenían pruebas con datos hostiles.

## `tests/test_wpa_hostil_320.c`: lo que llega por el aire (`src/core/wpa.c`)
Lo puede mandar cualquiera que esté cerca, sin estar en la red:
* **IE de balizas y respuestas de sondeo** (`wpa_classify`, `wpa_parse_rsn`; los usan `iwl.c` y el wifi de la Pi): 300 000
  conjuntos mutados (RSN, WPA1, empresa, mezclas con SSID y tasas).
* **Resultados de escaneo** del firmware de la Pi (`wifi_escan_parse`): 100 000 mutados; nunca más de los pedidos y el SSID siempre
  terminado.
* **Saludo de 4 pasos y renovación de grupo** (`wpa_sm_rx`): 30 000 tramas mutadas tal cual (cabeceras y longitudes, antes del MIC)
  y 30 000 con las **KDE mutadas pero cifradas con la KEK y con el MIC bien calculado**, lo que haría un punto de acceso falso que
  conoce la contraseña (una wifi pública). Así se llega de verdad a leer la clave de grupo: unas 23 000 se rechazan como deben y
  7 000 se aceptan, siempre con una clave de grupo de tamaño válido.

## `tests/test_llm_hostil_320.c`: las respuestas del LLM (`src/agent/llm_proto.c`)
El servidor lo configura el usuario, pero puede ser uno local por `http` en la red de casa, o uno comprometido. 200 000 respuestas
de los cuatro proveedores, errores, listas de modelos y estado del lanzador, mutadas (comillas, barras, `\u`, pares subrogados,
llaves) y cortadas, con salidas de 1 a 300 bytes: la salida siempre queda terminada.

## Resultado
Ningún fallo de ASan ni de UBSan, con gcc 14 y con clang. Este código estaba bien escrito; las pruebas quedan para que siga así.
