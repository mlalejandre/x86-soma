# Parche 198: navegador, contencion de fallos

Requiere el parche 197. Solo x86_64 (en aarch64 nada cambia).

## El problema

El motor del navegador (HTML, CSS, JavaScript y maquetacion: unas 21.000 lineas que procesan lo que llega de Internet) corre en
ring 0, dentro del nucleo. Los decodificadores de imagenes, zlib y PDF ya estan aislados en ring 3; el motor no. Dos consecuencias:

1. Un solo error en cualquiera de esas lineas (un puntero nulo ante un HTML raro) era un PANICO DEL NUCLEO: una pagina web podia
   tumbar el sistema entero, con todo lo que estuviera abierto.
2. El motor puede leer cualquier archivo del sistema.

Sacar el motor entero a ring 3 es el arreglo de fondo, y es un trabajo de varios parches (hay que llevarse tambien el dibujo, las
fuentes y el DOM). Este parche es el primer paso: que un fallo del motor deje de ser un fallo del sistema.

## Lo que cambia

### 1. Un fallo del motor cierra la pagina, no el sistema

Si el hilo de trabajo del navegador provoca una excepcion de CPU (acceso a memoria no valido, proteccion general, instruccion no
valida, division por cero) o el protector de pila detecta un desbordamiento:

- el hilo vuelve a un punto seguro, suelta los cerrojos y conexiones que tuviera a medias,
- la pagina averiada se abandona (su memoria se da por perdida: liberarla con el motor a medias no es seguro),
- se abre una pagina nueva con el aviso `about:fallo` (que dice que paso), conservando el historial,
- y SOMA sigue funcionando. En la consola queda `[WEB] El motor del navegador ha fallado (...)`.

Limites, a proposito:

- Si el fallo ocurre dentro del asignador de memoria del nucleo (el heap podria estar corrupto) NO se recupera: panico, como antes.
- Como mucho 8 recuperaciones por arranque. La novena es un panico: algo va mal de verdad y cada recuperacion pierde memoria.
- Solo cubre el hilo de trabajo del navegador. El pintado de la pagina (hilo de la interfaz) y los hilos de descarga de imagenes
  no estan cubiertos.
- Es contencion de averias, no aislamiento de seguridad: un fallo de memoria explotable en el motor sigue dando privilegios de
  nucleo. Eso solo lo arregla llevar el motor a ring 3.

Prueba: `panic web 1` (navegacion), `2` (JavaScript), `3` (maquetacion), `4` (instruccion no valida), `5` (desbordamiento de
pila). La siguiente pagina que cargues fallara de verdad en ese punto: debe aparecer el aviso y el sistema debe seguir.

### 2. El motor no puede leer los secretos

Los archivos que el parche 193 oculto a los agentes (`/etc/wifi.conf`, `/etc/llm.conf`, `/etc/history.txt`, `/etc/secreto/`) tampoco
los puede leer ya el hilo del navegador: ni con `file:///etc/llm.conf` en la barra ni desde un script de una pagina local.

## Archivos

Nuevo: `src/apps/web_fault.h`. Cambiados: `src/apps/browser.c`, `src/apps/web_page.c`, `src/apps/web_js_run.c`,
`src/arch/x86_64/idt.c`, `src/core/ssp.c`, `src/core/mem.c/.h` (`kheap_busy`), `src/fs/fs.c`, `src/agent/shell.c`.
