# Parche 317 · Las demás descargas, fuera del núcleo; cancelar un proceso al momento

Sigue al 316 (TLS en `netsvc`). **Hace falta `make`.**

## El problema
Después del 316 quedaban en el núcleo tres descargas de **direcciones que no elige el sistema**:
* `chat.c`: las **imágenes de las respuestas del modelo** (la URL la escribe el modelo; una página con instrucciones escondidas
  puede hacer que ponga la que quiera);
* `web_visor.c`: el **visor de documentos** (PDF y LaTeX enlazados desde cualquier página);
* la orden de diagnóstico **`tls <url>`**.

El visor necesita **cancelar** (al cerrarlo se corta la descarga), y `netsvc` no sabía.

## Qué cambia
* Las tres pasan a `wn_fetch_isolated`: cada descarga en un proceso `netsvc` nuevo (HTTP y TLS en ring 3, sin las cookies del
  navegador). En el núcleo solo queda el cliente del LLM (`llm.c`), que lleva la clave de la API: es el paso siguiente.
* **Cancelación**: `wn_fetch_isolated` acepta `rq->cancel`. `sbx_run_caps_cancel` (`sandbox.c`) espera con
  `usermode_wait_cancel` (`usermode.c`), que para el proceso en cuanto `*cancel` vale 1. La descarga devuelve -13
  («cancelada»), como `wn_fetch`.
* **Un proceso parado (o fuera de plazo) muere en su siguiente llamada al sistema** (`usermode_syscall`). Antes el plazo solo
  se miraba en el tick de reloj que caía en ring 3, y un proceso que pasa casi todo el tiempo esperando dentro del núcleo
  (la red) tardaba **más de 2 s** en parar; ahora tarda **~10 ms**. Es la misma regla (pasado el plazo, muere), aplicada antes.
* `tls <url>` informa del motivo del fallo tal como lo ve `netsvc` («TLS: el certificado no lo firma ninguna autoridad de
  confianza»), que es el TLS que usan el agente y `descargar`.
* **Prueba más fiable** (`kernel.c`): las autopruebas de QEMU esperan a que el escritorio termine de arrancar antes de empezar.
  Lo que el escritorio reservaba al terminar podía caer entre las dos medidas de una comprobación de fugas y dar un FALLO falso
  (pasó una vez preparando este parche: 550 marcos «perdidos» frente a 542 permitidos, en el bloque de fuzzing). Con la espera, las
  medidas cuadran exactas (antes/después iguales, el montón no crece).

## Pruebas
* **QEMU** (`make sbx-check`, `SBXNET`): una descarga de una ruta que no responde se cancela a los 300 ms y vuelve en 309 ms
  con -13; una ya cancelada ni empieza. Todo lo demás (`SBX`, `SBXNET`, `SBXTLS`, `SBXWEB`, `SBXWREF`, `SBXSRC`) sigue bien, dos
  veces seguidas.
* **Anfitrión**: `test_chatapp_285` (115/115) y `test_visor_maquina_289` dan también los falsos de `wn_fetch_isolated` y
  `wn_free_isolated`, que ahora usan `chat.c` y `web_visor.c`.
* **QEMU**: `make fault-check` bien; `prueba_python_237` 15/15, `prueba_python_239` 8/8 (Esc y los plazos de Python siguen
  igual), `prueba_progz_314` 6/6.
