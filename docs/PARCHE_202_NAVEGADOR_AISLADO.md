# Parche 202: el motor del navegador sale del nucleo (ring 3)

Requiere el parche 201. Solo x86_64 (en aarch64 no cambia nada). Hay que compilar desde cero (`make clean`).

## Que cambia

Hasta ahora el motor web entero (HTML, CSS, JavaScript, maquetacion, pintado, fuentes, cliente HTTP, TLS: lo que procesa datos de
Internet) corria DENTRO del nucleo. Los dos fallos criticos de los parches 200 y 201 estaban ahi, y cualquiera de ese tipo era
memoria del nucleo pisada desde una pagina web.

Ahora ese mismo codigo, sin reescribir, corre en un PROCESO DE RING 3 (`src/user/websvc.c`) con su propio espacio de direcciones:

- no tiene archivos, ni dispositivos, ni pantalla, ni la memoria de nadie mas;
- sus unicas llamadas al sistema son la red (como capacidad), el reloj, ceder la CPU y las tres de su canal con el nucleo;
- el nucleo le manda ordenes (navega, clic, tecla, desplaza...) y lee de vuelta el estado de la pagina y los pixeles ya pintados;
- nada de lo que el proceso escribe se cree sin validar: el nucleo lo lee por los marcos fisicos de las regiones que le presta
  (nunca por punteros del proceso), con tamanos acotados y cadenas terminadas a la fuerza.

Si una pagina consigue corromper la memoria del motor, corrompe la de ese proceso. Si lo tumba, el nucleo lo nota, lanza otro y
ensena el aviso de pagina cerrada. Si lo deja en un bucle sin fin, a los 20 s el nucleo lo termina (mientras tanto el sistema
sigue respondiendo: el proceso se reparte la CPU con los demas).

La interfaz (pestanas, barra de direcciones, barra de desplazamiento, lista de un `<select>`, avisos de `alert`) sigue en el
nucleo y no ha cambiado: `browser.c` llama a las mismas funciones de siempre, que en modo aislado son envoltorios `bx_*` que
mandan la orden al proceso o responden con el ultimo estado recibido.

## Uso

Por defecto el navegador ya es el aislado. No hay nada que hacer.

    navegador-aislado            estado: configurado, proceso en marcha, veces lanzado, caidas, memoria
    navegador-aislado off        vuelve al motor dentro del nucleo (desde el proximo arranque)
    navegador-aislado on
    webr3 <url>                  prueba sin interfaz: carga la pagina en el proceso y dice titulo, fotogramas y memoria
    panic web <1..5>             con el modo aislado provoca el fallo DENTRO del proceso (1/3 puntero nulo, 2 bucle sin fin,
                                 4 instruccion no valida, 5 agotar su memoria): el sistema debe seguir como si nada

Si el proceso no puede arrancar ni una vez (sin memoria), la sesion sigue sola con el motor dentro del nucleo y lo avisa.

## Politica de archivos

El proceso no abre archivos: se los pide al nucleo, y el nucleo solo se los da mientras la pagina que pidio EL USUARIO es local
(`file:` o `about:`); en cuanto la pagina es de la red, ningun archivo. Los secretos de `/etc` siguen vetados siempre (parche 198).
Excepcion: `/etc/tls_confianza.pem` (las autoridades que hayas anadido, que son publicas).

## Lo que se ha comprobado (QEMU)

- Con las fuentes TTF reales (DejaVu Sans, Liberation Sans): la misma pagina pintada por el motor aislado y por el del nucleo sale
  IDENTICA pixel a pixel (texto, negrita, cursiva, acentos, bordes, listas, formularios).
- Navegar, enlaces, atras/adelante, recargar, pestanas (nueva, volver), escribir en campos, `<select>`, rueda, `alert()`, scripts
  con `fetch` y temporizadores, imagenes PNG, paginas locales con sus imagenes.
- HTTPS dentro del proceso: TLS 1.3 con validacion de certificados (rechaza el no confiado; acepta al anadir su autoridad).
- Fallos reales dentro del proceso: puntero nulo, instruccion no valida, memoria agotada y bucle sin fin. En los cuatro el nucleo
  sigue, el proceso se relanza y se puede seguir navegando. `usfuzz`, `conctest`, `oomtest` y `sbx+user` pasan, tambien con 4 CPU.

## Lo que el modo aislado todavia no hace

- "Guardar imagen" y "Copiar imagen" del menu contextual (los pixeles no salen del proceso).
- La rueda sobre cajas con desplazamiento propio (`overflow:auto`) mueve la pagina, no la caja.
- El desplazamiento usa un fotograma nuevo por paso en vez de la instantanea desplazada: en equipos lentos puede notarse.
- Cookies y `sessionStorage` viven en el proceso: si se cae, se pierden.
- El monton del proceso es sencillo (clases de tamano); si alguna pagina muy pesada va mas lenta que antes, es el primer sitio
  donde mirar.

## Lo que queda dentro del nucleo

- El motor antiguo sigue compilado en el nucleo como alternativa (`navegador-aislado off`, y el respaldo automatico). Cuando el
  modo aislado lleve un tiempo usandose sin problemas en el portatil, el siguiente paso es quitarlo del nucleo: son ~1,2 MiB
  menos y desaparece del todo esa superficie.
- El cliente HTTP que usan `curl`, `descargar` y el cliente del LLM (parche 195) sigue en ring 0. Ya existe un servicio de ring 3
  para eso (`netsvc`, opcion `SOMA_BROWSER_NET_RING3`); pasarlos ahi es el paso siguiente.
- Un proceso comprometido conserva la red: puede hablar con equipos de tu red local. Es inherente a un navegador; limitarlo
  (paginas publicas sin acceso a direcciones privadas) es otra mejora posible.

## Cambios en el nucleo que conviene conocer

- `PMMC_LOWER` pasa de 16 a 32 MiB (zona reservada al principio de la RAM): la imagen del nucleo lleva dentro el programa del
  navegador (con sus fuentes) y ya no cabia en 14 MiB. El enlazador avisa si se pasa de 30 MiB.
- El area de codigo de un proceso de usuario pasa de 4 a 12 MiB (`USER_CODE_AREA`).
- Tres llamadas al sistema nuevas (27, 28, 29), solo en el filtro del proceso del navegador.

## Archivos

Nuevos: `src/user/websvc.c`, `src/core/web_r3_abi.h`, `src/arch/x86_64/web_r3.c`, `src/arch/x86_64/web_r3.h`.
Cambiados: `src/apps/browser.c`, `src/apps/web_fault.h`, `src/drivers/fb.c`, `src/arch/x86_64/usermode.c/.h`,
`src/arch/x86_64/pmm_core.h`, `src/arch/x86_64/pmm.c`, `linker_x86_64.ld`, `src/agent/shell.c`, `Makefile`.
