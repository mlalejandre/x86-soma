# Parche 253: el LCM traduce peticiones a órdenes, dentro del núcleo

Requiere el parche 251 (rutas del agente). Probado sobre el árbol del parche 252.

## Qué es

Un modelo pequeño entrenado (no reglas) lee una petición en español y decide dos cosas a la vez: qué orden de SOMA es (o si
no es una orden) y qué palabras son sus piezas (archivo, destino, carpeta, número, red, clave, equipo, web, texto). Con eso
arma la línea del terminal. Es el trabajo del LCM (github.com/mlalejandre/LCM, `src/ordenes`) adaptado a SOMA:

- **Corre en el núcleo, sin coma flotante.** El núcleo compila con `-mgeneral-regs-only`, así que la red (una BiLSTM) va
  cuantizada: pesos int8 por fila, activaciones en Q12, tanh y exp como tablas. Igual en x86_64 y aarch64.
- **No sale nada del equipo** y no hace falta el servidor del LLM para las órdenes que entiende.
- **Si no está seguro, no hace nada**: la petición sigue exactamente por el camino de antes (mayordomo y LLM).

## Cómo se usa

    soma qué procesos hay                  -> [LCM] -> ps
    soma borra el archivo notas.txt        -> [LCM] He entendido: rm notas.txt ... ¿La ejecuto? Responde 'si' o 'no'
    si                                     -> [LCM] -> rm notas.txt
    dime cuánto disco queda                -> (en el terminal, sin 'soma'): [LCM] -> df
    soma explícame qué es la memoria ram   -> no es una orden: va directa al LLM (antes el mayordomo enseñaba la RAM)
    lcm copia informe.pdf a copias         -> entiendo: cp (99 %) -> orden: cp informe.pdf copias [pediria confirmacion]
                                              (no se ha ejecutado nada)
    lcm estado | lcm apagar | lcm encender -> apagado, todo vuelve a ir como antes del parche (se guarda en /etc/lcm.conf)

## Dónde actúa (y dónde no)

1. `agent_run("soma", ...)`: después de los caminos del maquetador y del programador, y antes del mayordomo.
   - Orden clara → la ejecuta (o la deja pendiente de confirmación).
   - «No es una orden», o duda → al LLM, sin pasar por las reglas del mayordomo (medido: con ellas se ejecutan más órdenes
     equivocadas, 6,1 % frente a 3,9 % en el examen ciego). Las preguntas por el catálogo («qué órdenes hay») siguen yendo al
     mayordomo.
   - Una orden escrita tal cual (orden de SOMA y como mucho un argumento: `soma teclado us`) no se reinterpreta.
2. `mayordomo_run`: tras las órdenes escritas tal cual (`try_command`), antes del catálogo y las reglas.
3. Terminal: si una línea no es una orden de SOMA, se le pasa al LCM. Si no la entiende, sale el «Comando desconocido» de
   siempre. Si la línea empieza por una orden que existe, no la toca.

Solo con peticiones **del usuario**: en modo agente (`shell_agent_mode`) o dentro de una delegación no hace nada. Así un LLM no
puede usar el LCM para saltarse la política de órdenes de agente.

## Seguridad

- **Piden confirmación** (`si`/`no`) todas las que cambian algo: `rm`, `rmdir`, `kill`, `reboot`, `poweroff`, `write`, `cp`, `mv`,
  `mkdir`, `touch`, `wifi conectar/desconectar`, `descargar`, `cam foto`. Sin preguntar solo van las que miran o abren algo
  (ls, cat, ram, ps, ping, search, navegador...) y los ajustes reversibles (brillo, volumen, teclado). La pendiente se olvida
  si la siguiente petición es otra cosa.
- **No se arma la orden** si una pieza trae `; | & < > `` ` `` $ " \`, ni si una pieza que no es el texto final lleva espacios
  (p. ej. `rm mis notas.txt` borraría `mis`). En ese caso se pide el dato de nuevo.
- **Falta una pieza** («copia el archivo»): lo dice y enseña el uso de la orden; no ejecuta nada.
- Si el modelo incrustado no carga, el LCM se desactiva solo y lo avisa.

## Medidas

Exámenes (petición -> orden exacta; «mal» = ejecutaría una orden distinta; «sin preguntar» = mal y sin pedir confirmación):

| examen | casos | quién lo escribió | mayordomo de antes | LCM (lo que se instala) |
|---|---|---|---|---|
| 4, ciego | 231 | otro agente, sin ver el LCM; medido una vez | 9,9 % exactas · 18,2 % mal · 1 borrado sin confirmar | 51,9 % exactas · 3,9 % mal · 2,6 % sin preguntar |
| 4, tras dos ajustes generales | 231 | (ya visto) | | 53,0 % · 3,5 % mal · 2,6 % sin preguntar |
| 3, ciego | 223 | otro agente; medido una vez | 12,7 % exactas · 12,6 % mal | 71,1 % exactas · 6,3 % mal |
| 1 y 2 | 172 | yo, antes de construir | 15 % exactas | 92,4 % y 89,1 % · 0 mal |

- No-órdenes de los exámenes ciegos (100 preguntas, la mitad trampas con «memoria», «borrar», «wifi»...): 100/100 al LLM.
- 2.281 frases reales de UD que no son órdenes (nunca vistas): ejecutarían algo 2 (0,09 %), nada peligroso.
- Órdenes peligrosas sin confirmación: 0 en todas las pruebas (el mayordomo de antes: 7 en el conjunto).
- Variantes (todos los exámenes, 626 casos): sin tildes 73,3 % exactas; Mayúscula inicial 69,7 %; con una errata 55,5 % (6,2 % mal);
  con cortesías («oye, ... gracias») 68,5 % (5 % mal).
- C del núcleo frente al modelo de Python: 4.764 frases, 99,8 % mismo resultado, ninguna orden distinta. ~1,3 ms por petición
  (x86-64, anfitrión). Modelo de 1,5 MB.

Criterio fijado antes de empezar: >= 90 % exactas y <= 1 % mal ejecutadas. NO se cumple en los exámenes ciegos.
Lo que sí: es mucho mejor que el mayordomo de antes en todo, y lo que hace mal sin preguntar son órdenes que solo miran
(un find, un dns, un wc...): todo lo que cambia algo pide confirmación. Cuando duda, no hace nada y pasa al LLM.


## Archivos

Nuevos: `src/agent/lcm_ordenes.inc` (la red en C), `src/agent/lcm_ordenes_pesos.inc` (el modelo, generado),
`tests/test_lcm_ordenes_253.c`, `tools/lcm_ordenes/` (entrenar, medir, exportar; ver su README), este documento.
Cambiados: `src/agent/agent.c`, `src/agent/shell.c`, `src/agent/shell_agente.c` y, si existe y encaja, `protected.txt`.
Los `.inc` no se compilan por separado (los incluye `agent.c`), así que el `Makefile` no cambia.

## Revertir

    python3 parche_253_lcm_ordenes.py --revertir

Restaura los archivos cambiados byte a byte (con la fecha de ahora, para que `make` los recompile) y borra los nuevos.
Sin recompilar también se puede desactivar en marcha con `lcm apagar`.
