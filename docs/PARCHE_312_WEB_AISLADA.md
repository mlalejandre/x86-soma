# Parche 312 · Lo que el agente lee de Internet, fuera del núcleo

Segundo paso para sacar de ring 0 el código que trata datos de fuera. **Hace falta `make`.**

## El problema
Las órdenes `investiga` (parche 288) y `search` descargan **páginas web cualesquiera** y resultados de Wikipedia y DuckDuckGo,
y los convierten en texto para el modelo: HTML, entidades y JSON. Ese análisis corría en el núcleo, así que una página preparada
para romperlo rompía el sistema, y el agente visita las páginas que le salen en la búsqueda, no unas de confianza.

## Qué cambia
* **`leesvc.elf`** (nuevo, `src/user/leesvc.c`): un servicio aislado como `mimesvc` (311). Solo puede llamar a `exit`; no ve la
  memoria del núcleo y no tiene red ni disco; tiene plazo (10 s) y tope de montón. Ejecuta **el mismo código de antes**:
  `wl_texto` (`web_lee.c`), `bw_parse_wikipedia` y `bw_parse_ddg_lite` (`busca_web.c`).
* `src/agent/web_aislado.c`: `wl_texto_aislado`, `bw_wikipedia_aislado` y `bw_ddg_aislado`, que usan `shell_investiga.c` y
  `shell_red.c`. El núcleo sanea lo que devuelve el servicio: el texto nunca pasa de lo pedido, siempre queda terminado y se
  corta en el primer NUL; el título también; el número de resultados queda entre 0 y el máximo.
* Si el servicio falla, la página o la búsqueda **cuentan como no leídas**: el agente ya sabe seguir sin ellas. No se reintenta en
  el núcleo.
* En aarch64 (sin EL0) y en las pruebas del anfitrión se llama directo, como antes.
* `leesvc.c`, `web_aislado.h` y `web_aislado.c` quedan protegidos frente a la IA (86 = 86).

Lo que sigue en el núcleo: la descarga (HTTP y TLS, `wn_fetch`). El paso siguiente sería hacerla con `netsvc` (parche 142),
que ya existe para la orden `descargar`.

## Pruebas
* **QEMU** (`make sbx-check`): una página real (artículo, scripts, menús, pie, formulario, entidades y UTF-8), unos
  resultados de Wikipedia en JSON y unos de DuckDuckGo en HTML dan lo mismo en el servicio que en el núcleo; el título se lee bien
  y el texto no lleva scripts ni pie; nunca pasa de lo pedido. **300 páginas mutiladas** dan lo mismo y el núcleo sobrevive. Sin
  pérdidas de memoria.
* **Anfitrión** (`tests/test_web_aislado_312.c`): 20 000 respuestas al azar pasan por el saneado y salen seguras.

Al preparar la prueba de QEMU apareció un fallo de `wl_texto` que ya estaba antes: pierde párrafos enteros de algunas páginas.
Lo arregla el parche 313.
