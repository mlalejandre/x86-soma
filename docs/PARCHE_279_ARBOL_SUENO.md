# PARCHE 279 · Árbol de temas y ciclo de sueño de la memoria

Completa la memoria de SOMA (277) con lo que memorIA llama su «hipocampo»: los recuerdos se **ordenan en un árbol de temas**, la memoria
**olvida** lo que no se usa, **se limpia sola** mientras SOMA está en calma y **recupera** lo olvidado cuando vuelve a hacer falta.

## Qué cambia para el usuario
* `memoria arbol`: el árbol de temas (`Tema/Subtema/Detalle`, hasta 3 niveles) con el número de hechos de cada nodo y su descripción. `mem_tree` del agente muestra lo mismo.
* `memoria sueno [N]`: ejecuta N pasos del ciclo ahora (máx. 9; 9 pasos = una vuelta completa). `memoria estado` resume reloj, nodos, olvidadas, restauradas y el último paso.
* `memoria ver N` indica el tema del hecho. `memoria config sueno 0|1` y `memoria config sueno_s SEGUNDOS` (por defecto 1 paso cada 90 s).
* Lo archivado por el olvido **vuelve solo** si una consulta lo trae (coseno ≥ 0,5): importancia mínima 6 y reloj renovado. Lo que archivas tú con `memoria olvidar` **no** se restaura.

## El ciclo (las 9 tareas de memorIA, una por paso y por turnos)
| paso | tarea | qué hace |
|---|---|---|
| 1 | repair | recalcula contadores, poda nodos vacíos, deshace sustituciones rotas |
| 2 | embed | embebe lo que quedó sin vector (servidor caído o recuerdos antiguos) |
| 3 | decay | archiva las celdas con importancia efectiva < 0,5 (curva de memorIA) |
| 4 | ordena | coloca hasta 8 hechos sin tema en el árbol (1 llamada al LLM) |
| 5 | describe | describe hasta 2 nodos con descripción pendiente (LLM) |
| 6 | dedupe | busca dos hechos redundantes del mismo tema (palabras ≥ 55 % o coseno ≥ 0,88); el LLM decide fusionar o conservar ambos |
| 7 | reevaluate | rescata, con el LLM, hechos a punto de archivarse (franja 0,5–0,85 de importancia efectiva) |
| 8 | split | parte en dos un nodo con más de 12 hechos (LLM) |
| 9 | merge | une ramas hoja casi vacías y muy parecidas (LLM) |

Solo corre cuando ningún agente usa el LLM desde hace ≥ 30 s y no hay mensajes por extraer; `memoria sueno` lo fuerza.
Nada se borra: lo olvidado queda `archivado`, lo fusionado `sustituido` (con el número del que lo sustituye), las ramas se pueden rehacer.

## La curva del olvido
`eficaz = importancia · exp(−edad / (20 · importancia³))`; se archiva con eficaz < 0,5 y edad ≥ 10. La **edad se mide en pasos del ciclo**, no en segundos
(igual que en memorIA): solo envejece mientras SOMA está encendido y en calma. Con un paso cada 90 s: importancia 1 ≈ 21 min, 2 ≈ 5,5 h, 3 ≈ 1 día,
5 ≈ 6 días, 7 ≈ 19 días, 9 ≈ 44 días (repetir un hecho o que salga en una búsqueda renueva el reloj y sube la importancia). Si te parece rápido: `memoria config sueno_s 300`,
o `sueno 0` para apagar el ciclo (el árbol deja de crecer; la memoria sigue funcionando).
Implementada en enteros (el kernel no usa coma flotante): error máximo 1/256 frente a la fórmula en coma flotante, medido sobre 10 importancias × 60 000 edades.

## Formato y migración
Cada celda guarda su nodo en un byte que antes iba a 0: las memorias del parche 277 se leen tal cual (sin ordenar). El árbol y el reloj van en `/memoria/arbol.bin` (CRC);
si falta o está dañado se descarta y el ciclo lo rehace (el reloj vuelve a la última marca usada).

## Lo que NO se ha portado de memorIA
* **Sinapsis** (enlaces entre nodos que se refuerzan al usarse) y **selftest** (preguntarse con el LLM si cada dato se recuperaría): el selftest se sustituye por `ordena`.
* Refuerzo por número de accesos (access_count): aquí refuerza repetir un hecho o que se restaure, no el uso en cada búsqueda.
* El árbol no se usa para recuperar: la búsqueda sigue siendo por embeddings (lo que mejor midió memorIA); el árbol ordena, describe y sirve para entender qué sabe SOMA.

## Pruebas y límites
`make pruebas-memoria`: núcleo (231 comprobaciones: curva, olvido, restaurar, árbol, parsers de las respuestas del LLM, aplicar fusión/partición, guardado con CRC y
corrupción) y el **pegamento ejecutado en el anfitrión** (75: un LLM, un servidor de embeddings y un disco falsos; las 9 tareas, guardar y recargar, árbol dañado, servidor y LLM caídos, órdenes).
Lo que NO está medido: cómo de bien ordena, describe o decide fusionar el LLM pequeño de SOMA (el falso obedece reglas de palabras clave); el hilo real en el kernel (solo compilado);
cada paso con LLM puede tardar lo que tarde el modelo y un mensaje nuevo espera a que termine (comparten el cerrojo de red). Un LLM flojo puede crear ramas raras: se corrigen con `memoria sueno`
en vueltas posteriores (merge/split) o fijando el tema a mano en una versión futura.
