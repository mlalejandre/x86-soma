# Parche 299 · La CI de GitHub en verde

Desde hacía tiempo `.github/workflows/ci.yml` no era YAML válido (lo arregló el parche 298), así que GitHub no ejecutaba nada y
nadie veía lo que se había ido rompiendo. Al volver a ejecutarse fallaron las cuatro tareas. Todo lo de aquí se ha reproducido en
Linux con las mismas órdenes que la CI y ahora pasa.

**Hace falta `make`** (cambia el núcleo).

## Compilar con `-Werror`
* `src/lang/vm.c`: `vm_pool_grow` no usaba su parámetro `vm` (aviso convertido en error).
* aarch64 (Raspberry Pi 5) no compilaba:
  * `src/agent/shell_apps.c`: las frases de voz de batería y procesador usan ACPI y `cpupower`, que solo existen en x86_64.
    En aarch64 responden que no pueden leerlo.
  * `src/core/kernel.c`: la opción de GRUB `soma.ram=1` solo se mira en x86_64 (en aarch64 no hay línea de GRUB).

## macOS (clang)
* `src/apps/web_js_big.c`, `web_js_gc.c`, `web_js_run.c`: en las pruebas de host se llamaba a `strstr` sin declararla. clang la
  trata como error aunque se compile con `-w`, así que las pruebas de JavaScript no compilaban en el Mac.

## Arreglos de verdad que encontraron las pruebas en QEMU
* **Una orden escrita podía perderse sin aviso** (`src/agent/shell.c`). Si otro hilo tenía la consola en ese momento (el informe
  del arranque, el agente), la orden escrita en el terminal se descartaba en silencio. Ahora espera hasta 30 s a que quede libre
  y, si no, lo dice.
* **La ayuda rápida se quedaba con Esc** (`src/ui/gui_ayuda.c`, del parche 294). Con los consejos del arranque a la vista, Esc los
  cerraba aunque hubiera una aplicación delante, y esa aplicación no lo recibía (por ejemplo, para cerrar un menú del terminal). Ahora,
  con una aplicación delante, Esc es de la aplicación y los consejos siguen solos. La tarjeta que se abre con F1 se cierra con Esc siempre.
* **El servidor web en QEMU sin KVM** (`src/net/httpd.c`). Por seguridad solo se enciende solo en una máquina virtual, que se
  detectaba por el bit de hipervisor de CPUID. QEMU sin aceleración (el de la CI) no siempre lo pone. Ahora también cuenta fw_cfg,
  que solo existe en QEMU. En un equipo real sigue apagado.

## Pruebas al día
* `tools/fault_check.py`:
  * los archivos de prueba se escriben en `/tmp`, porque desde el parche 268 el terminal solo escribe en `/usuario` y `/tmp`;
  * los clics del reproductor van a los botones del diseño nuevo.
* `tests/test_ayuda_maquina_294.c`: declara las primitivas de dibujo que usa (clang no compila llamadas sin declarar).
* `tools/audit.py --strict`: no cuenta como «funciones inseguras» las libc de los programas de usuario (TinyCC, voz, vplay), que
  *definen* `strcpy` o `sprintf`, ni los nombres dentro de cadenas de texto.

## Resultado aquí (Linux x86_64, con las órdenes de la CI)
| Tarea de la CI | Antes | Ahora |
|---|---|---|
| `make ci` (pruebas de host + auditoría) | auditoría: 24 avisos falsos | pasa |
| `make ARCH=x86_64 WERROR=1` | error en `vm.c` | compila |
| boot-smoke, sbx-check, idle-check | no llegaba | pasan |
| cpu-matrix | HTTP sin respuesta en las 3 CPU | 3 × 200 |
| fault-check | 5 escenarios fallaban | 20/20 |
| `make ARCH=aarch64 WERROR=1` + boot-smoke | 17 errores | compila y arranca |
