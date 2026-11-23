# PARCHE 280 · Importar el cartucho real de memorIA y enlazar la agenda

Cierra el paso 3 de la hoja de ruta: la memoria de SOMA (277–279) recibe **tu memoria de memorIA** (`memoria.db`) con sus hechos, nodos, sustituciones,
mensajes literales y vectores, y el contexto que ve el agente incluye lo apuntado en la **agenda** cuando se habla de planes.

## Cómo se usa (3 pasos)
1. **En el Mac** (donde está memorIA), con el repositorio de SOMA:
   `python3 tools/exporta_memoria.py ~/Desktop/cerebro/memorias/memoria-unificada/body/cartuchos/principal --servir`
   (si no sabes la carpeta, pásale la raíz de memoria-unificada: lista los cartuchos que encuentre). Escribe `memoria-export/memoria-001.smem`, `-002`... (≈ 8 MB cada uno),
   imprime un resumen y las órdenes exactas para SOMA, y sirve los archivos en tu red local con una clave de un solo uso hasta que pulses Ctrl-C.
2. **En SOMA**, por cada parte (las copia el exportador tal cual): `descargar http://IP_DEL_MAC:8099/CLAVE/memoria-001.smem /usuario/memoria-001.smem`
3. `memoria importar /usuario/memoria-001.smem` (`sin-vectores` al final: ignora los vectores del archivo y que los calcule el servidor). Guarda al terminar.
Importar dos veces no duplica: lo que ya está (mismo texto sin mayúsculas, acentos ni puntuación) se salta. `memoria importar` sin ruta sigue cargando el árbol antiguo `/mem`.

## Qué viaja
| memorIA | SOMA |
|---|---|
| celda activa | hecho vigente, con su importancia (1–10), fuente y fecha de vigencia (`valid_from`, o `ingest_timestamp`) |
| celda sustituida (`superseded_by` / `valid_until`) | hecho **sustituido**, con la fecha en que dejó de valer y el enlace a quien lo sustituye (la historia sigue consultable) |
| celda archivada | archivada **por olvido**: vuelve sola si una consulta la trae (como en 279) |
| nodos (nombre, descripción) | árbol de 3 niveles con la descripción de memorIA (no se reescribe); lo más profundo se junta en el tercer nivel |
| episodios (mensajes) | mensajes literales con su fecha y rol (hasta 8 000, los más recientes) |
| vectores `bge-m3` float32 | int8 de norma 127, la misma cuantización que usa SOMA con su servidor (comprobado igual que `mc_parse_embedding`, diferencia ≤ 1 por componente) |

Los vectores solo se aceptan si el modelo del archivo coincide con `emb_modelo` de SOMA y la dimensión con la del servidor (si no, se omiten y los calcula el servidor poco a poco). Mezclar modelos
distintos en la misma memoria rompería la búsqueda, por eso se rechaza.

## Qué NO viaja (a propósito)
* **Secretos**: el exportador solo abre `memoria.db` (en solo lectura); no lee `.env`, claves ni nada más. Además se salta los textos que parecen secretos (claves `sk-`, `ghp_`, `AKIA…`, JWT, claves privadas,
  «contraseña/clave/api key… es X», cadenas hexadecimales de ≥ 40, tokens mezclados largos). Dice cuántos; nunca imprime cuáles. Pueden saltarse alguno inocente (un hash de commit): es el lado seguro.
* El reloj de edad: las celdas importadas **nacen con el reloj de SOMA** (no llegan ya olvidadas); empiezan a decaer desde ahora con la curva de 279.
* Sinapsis, contadores de acceso y marcas del selftest de memorIA (no existen en SOMA).
* Lo que supere el tope: 36 000 elementos por defecto (`--max`; SOMA admite 40 960 y necesita hueco para lo suyo). Con tope, se quedan primero las celdas activas (las más importantes si no caben),
  luego la historia de sustituciones y lo archivado, y por último los mensajes más recientes.

## Detalles que conviene saber
* Las fechas de memorIA son «hora de pared» sin zona: se leen como UTC (desvío de 1–2 h respecto a Madrid en el instante exacto; las fechas no cambian de día salvo en horas de madrugada).
* Árbol: SOMA admite 255 nodos y 3 niveles. Si tu árbol tiene más, las hojas con menos hechos suben a su padre; los hechos que no caben quedan sin ordenar y el ciclo de sueño los ordena.
* RAM: con vectores, 36 000 elementos ocupan ≈ 45–55 MB en SOMA. Si la memoria del sistema no alcanza, la importación se detiene con aviso y lo importado hasta ahí queda guardado.
* Mientras se importa, el ciclo de sueño espera; las demás tareas siguen (la importación suelta la memoria cada 200 líneas).

## Agenda en el contexto
Si lo que se pregunta trata de planes (`agenda`, `calendario`, `hoy`, `mañana`, `semana`, `cita`, `reunión`, `evento`, `próximo`, `pendiente`, `recordatorio`, `cumple`, `libre`…), el bloque de memoria
lleva debajo **«[AGENDA de SOMA: …]»** con los eventos de los próximos 7 días (hora local), con la advertencia de no inventar los que no estén. Solo lee la agenda; no apunta ni cambia nada.
No se convierten recuerdos en eventos solos («cumple el 14 de marzo»): eso sería inventar un aviso; se apunta con `agenda` o el agente lo hace con su herramienta.

## Pruebas y límites
`make pruebas-memoria`: el exportador se prueba con una `memoria.db` de **esquema idéntico al de memorIA** (40 comprobaciones: secretos, escapes, nodos > 3 niveles, sustituciones, vectores con y sin numpy, tope, partes pequeñas, origen
intacto byte a byte); el importador del núcleo lee exactamente esos archivos (101: estado, enlaces, nodos, vectores = los del núcleo, no duplica, líneas rotas, memoria llena, guardado y recarga);
el pegamento ejecutado en el anfitrión (`memoria importar` con 3 variantes de vectores, reinicio, agenda en el contexto): 134 en total con las de 279.
**No medido**: tu `memoria.db` real (no la tengo; si el exportador encuentra algo que no esperaba, lo dirá), el rendimiento de importar decenas de miles de elementos en el kernel real (la tabla de
repetidos y la inserción son lineales, pero el tiempo en SOMA no se ha cronometrado), la descarga real con `descargar` de partes de 8 MB, ni la calidad de las respuestas con vuestra memoria vs la de memorIA.
