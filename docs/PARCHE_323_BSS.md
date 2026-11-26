# Parche 323 · 2 MiB menos de imagen: memoria que solo se reserva al usarse

**Hace falta `make`.**

## El problema
La imagen del núcleo tiene 12 MiB de `.bss` (memoria estática a cero). Parte son búferes de aplicaciones concretas que ocupan memoria
**en todos los arranques**, aunque esa aplicación no se abra nunca, y cuentan para el presupuesto de 46 MiB de la imagen.

## Qué cambia
| búfer | dónde | antes | ahora |
|---|---|---:|---|
| máquina virtual de la demo del programador (`g_demo_vm`) | `coder_demo.c` | 627 KiB | se reserva al ejecutar y se libera |
| operaciones y pool del maquetador LaTeX (`ly_ops`, `ly_pool`) | `layout.c` | 629 KiB | se reservan la primera vez que se maqueta |
| bytecode de la comprobación del programador (`cdr_bc`) | `coder_app.c` | 256 KiB | se reserva al compilar y se libera (no se usaba después) |
| búfer temporal del editor de código (`ce_tmp`) | `codeedit.c` | 256 KiB | se reserva la primera vez que se usa |
| patrón de 300 KB de la autoprueba de red | `sandbox_test.c` | 292 KiB | se reserva solo al hacer la prueba |

La imagen pasa del **60,7 % al 56,3 %** de su presupuesto (25,9 de 46 MiB; quedan 20,1 MiB), y esa memoria queda libre mientras
no se usan esas aplicaciones.

Si no hubiera memoria al reservar, cada sitio lo dice y sigue: la demo devuelve «sin memoria para la máquina virtual», el
programador «sin memoria para compilar», el maquetador «Sin memoria para maquetar el documento», el editor no hace la operación.

No se toca `coder_core.c` (`g_bc`, 256 KiB): lo compilan seis pruebas del anfitrión sin `kmalloc`. Otros búferes grandes
(`agent_last_response`, `pr_src`, `np_rows`...) se usan en muchos sitios y no merece la pena el riesgo.

## Pruebas
* **QEMU**: `demo_ai` (la demo entera, con su máquina virtual reservada al vuelo): «TODO OK»; `latex` abre el maquetador sin
  avisos; `make sbx-check` completo bien (incluida la descarga del binario de 300 KB).
* Compila sin avisos en x86_64 y aarch64.
