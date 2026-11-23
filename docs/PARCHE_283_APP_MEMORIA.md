# PARCHE 283 · La aplicación de Memoria, rehecha para la memoria nueva

La ventana «Memoria» (antes un visor fijo de archivos de `/agent` y `/mem`) pasa a ser la **consola de la memoria de SOMA** (277–280),
con el aspecto de la interfaz de memorIA: fondo azul noche, tarjetas, insignias que laten, pipeline de las 9 tareas, explorador del árbol y gestor de cartuchos.
Todo lo que muestra es en vivo (se repinta sola cuando la memoria cambia, aunque no tenga el foco) y todo lo que tarda (LLM, servidor, disco) lo hace el hilo de la memoria:
la pantalla no se congela.

## Pestañas
| pestaña | qué hay |
|---|---|
| **Monitor** | hechos vivos y capacidad, ramas, mensajes, archivados, sin vector; cola de extracción y sus cuentas; ciclo de sueño (paso, ciclo, olvidadas, restauradas, fusionadas, próximo paso); **pipeline** de las 9 tareas (la que corre se ilumina; la siguiente, marcada); fase actual; aviso de prioridad cuando un agente usa el LLM; **últimos hechos aprendidos**; **registro de mantenimiento** (los últimos pasos, coloreados por tarea); servidor de embeddings (arrancar, parar, probar). Botones: Dormir 1 paso, Vuelta completa, Guardar ya. |
| **Explorador** | árbol de temas (plegable, con el total de cada rama y un punto ámbar si su descripción está pendiente) y cuatro vistas: Sin ordenar, Archivados, Historia (sustituidos), Mensajes literales. Por rama: + Recuerdo, + Subtema, Renombrar, Copiar a…, Borrar rama, y su descripción editable. Cada recuerdo, en una tarjeta con importancia, estado, tema, fecha y botones de editar y borrar. |
| **Editor** (al abrir un recuerdo) | texto, importancia (1–10), tema (`Tema/Subtema`; se crea si no existe), fecha, fuente, refuerzos, vector, sustituido por. Guardar, Archivar/Restaurar, Borrar, **Copiar a…** y **Mover a…** otro cartucho. Ctrl+Intro guarda; Esc cierra. |
| **Buscar** | por significado (embeddings) en el primario y en los cartuchos conectados, con el % de parecido; si el servidor no contesta, por palabras. Abrir un resultado lo edita. Como en memorIA, lo archivado que una búsqueda trae se restaura solo. |
| **Cartuchos** | tarjetas con estado (PRIMARIO / CONECTADO / DESCONECTADO), hechos, ramas y tamaño; Conectar, Desconectar, Hacer primario, Clonar, Exportar (.smem, con o sin vectores), Vaciar, Borrar; + Nuevo cartucho; Importar .smem en el primario. |
| **Ajustes** | memoria activa, extraer hechos, ciclo de sueño (interruptores); paso de sueño cada N s, hechos y mensajes en el prompt (pasos); dirección y modelo del servidor de embeddings. |
| **Archivos** | los archivos de los agentes de siempre (perfiles, notas, pizarra, configuración…), que se abren en Notas, como hacía la ventana antigua. |

Teclado: `1`–`6` pestañas, `Tab`/`Mayús+Tab` siguiente/anterior, `/` buscar, `n` recuerdo nuevo (Explorador), flechas y rueda para desplazar, `Esc` cierra. Copiar y pegar funcionan en los campos.

## Cartuchos (como memorIA)
Un cartucho es una memoria completa (sus hechos, mensajes, árbol y reloj). El **primario** es donde se aprende y se escribe; hasta **3 más pueden estar conectados** en solo lectura:
el agente los consulta a la vez (salen en el prompt como «Del cartucho de memoria "X"») y aparecen en las búsquedas. El cartucho Principal es la memoria de siempre y sigue en `/memoria/`
(**no se migra nada**); los nuevos van en `/memoria/k_ID/`. El registro está en `/memoria/cartuchos.txt`; los conectados se vuelven a conectar al arrancar.
Copiar/mover entre cartuchos usa el formato `.smem` del 280 por dentro: conserva temas, sustituciones y vectores, y no duplica lo que ya está.
Tope: 8 cartuchos (el sistema de archivos admite 256 archivos en total; cada cartucho usa 2 o más).

También desde la terminal: `memoria cartucho [lista|nuevo NOMBRE|usar ID|conectar ID|desconectar ID|exportar ID [RUTA] [vectores]|clonar ID NOMBRE|vaciar ID|borrar ID]` y
`memoria borrar ID` (borra de verdad; `memoria olvidar ID` solo archiva).

## Borrar de verdad
Antes nada se borraba (solo se archivaba). Ahora **Borrar** quita el recuerdo y rehace la memoria sin huecos: los números de los demás cambian, la arena de textos se recupera
y los archivos sobrantes se eliminan del disco. Si borras el hecho que sustituía a otro, el antiguo vuelve a estar vigente. Mientras se borra o se cambia de cartucho,
el ciclo de sueño, el embebido y el guardado esperan; la extracción de hechos comprueba que los números siguen valiendo antes de tocar nada.

## Arreglo de paso en el núcleo (del 279)
Al podar una rama vacía, los hechos **archivados** que colgaban de ella seguían apuntando a ese número de rama; si luego se creaba otro tema en el mismo hueco, aparecían dentro de él
(lo destapó la prueba de la ventana: «tomó un café» salía en «Proyectos»). Ahora la poda los deja sin tema.

## ¿Hace falta el programa memoria-unificada?
No para funcionar: SOMA lleva su propia memoria completa (núcleo, sueño, cartuchos, búsqueda) y usa directamente el servidor `bge-m3` de tu máquina de IA. El proyecto de memorIA
solo hace falta una vez, en el Mac, para **exportar** tu cartucho real con `tools/exporta_memoria.py` (280). Tampoco se ha portado su interfaz web: se ha rehecho dentro de SOMA.
No se han portado (siguen igual que en el 279): sinapsis y su grafo, selftest, consulta paralela con RRF (los conectados se consultan aparte y van en su propio bloque), ni la ingesta de libros por trozos.

## Pruebas
`make pruebas-memoria` (sin `make test`):
* `test_memoria_283` (89): borrar y compactar (ids, sustituciones, ramas, arena), quitar y renombrar ramas, copiar entre memorias por `.smem` con y sin vectores, poda con archivados.
* `test_memoria_glue_279` (225, antes 134): todo el pegamento con disco, LLM y servidor falsos, ahora con cartuchos (crear, copiar, mover, conectar, primario y reinicio, exportar,
  clonar, vaciar, borrar, topes), borrado de verdad con archivos sobrantes, peticiones caducadas y la API de la ventana.
* `test_memview_283` (128): **la ventana de verdad** dibujada en el anfitrión (fuente real y framebuffer de pruebas) sobre la memoria de verdad, pulsando sus botones y teclas:
  pestañas, crear temas y subtemas, recuerdo nuevo, editar, archivar, restaurar, copiar y mover a cartuchos, borrar con confirmación, renombrar y describir ramas, quitar rama,
  buscar y pegar, todas las acciones de cartuchos, ajustes, Archivos→Notas, teclado y rueda. Con una carpeta como argumento guarda capturas PNG de cada pantalla.

**No probado aquí**: la imagen arrancada en QEMU con el ratón real, el hilo de peticiones corriendo de verdad (en las pruebas se atiende a mano), cartuchos grandes reales
(tiempo de copiar/clonar/exportar decenas de miles de recuerdos en el kernel) y la velocidad de pintado con miles de tarjetas (solo se dibujan las visibles).
Con el modo RAM del 269, lo escrito en `/memoria` queda pendiente hasta `disco sync` o apagar.
