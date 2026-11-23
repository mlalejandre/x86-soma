# PARCHE 268 · Política de acceso: el usuario solo escribe en su unidad

(El número 268 se reutiliza: «memoria infinita» se dejó fuera de SOMA porque vive en otro proyecto.)

## Qué cambia

El usuario trabaja en **`/usuario`** (su unidad, partición USUARIO). Lo demás (configuración, memoria,
programas instalados, DATOS) lo ve en solo lectura y solo lo cambian los programas del sistema (Ajustes,
memoria, instalador...) o se edita desde fuera de SOMA (`tools/datos.py`).

| Herramienta del usuario | Dónde puede escribir | Fuera de ahí |
|---|---|---|
| Terminal (`write`, `rm`, `cp`, `mv`, `touch`, `mkdir`, `rmdir`, `>`, `descargar`, `fat get`) | `/usuario` y `/tmp` | se deniega con un mensaje; se puede leer (`cat`, `ls`) |
| `nano` | `/usuario` y `/tmp` | «Solo puedes guardar en /usuario (y /tmp)» |
| Gestor de archivos | `/usuario` y `/tmp` | lo ve, no lo cambia |

El gestor de archivos arranca en **Mi unidad** (`/usuario`): lista la unidad exFAT con sus tamaños y carpetas
reales, y permite crear archivos y carpetas, renombrar, mover, copiar, pegar y borrar carpetas enteras.

## Cómo está hecho

* `usuario_ruta_escribible()` (`src/agent/agent_policy.h`): función pura que normaliza la ruta (`//`, `/./`, `/../`)
  y solo admite `/usuario` y `/tmp`. Una ruta que no cabe se trata como no escribible.
* **Sesión de usuario** (`vfs_sesion_usuario`, `src/fs/fs.c`): solo mientras se ejecuta lo que escribe la persona en
  el terminal (`dispatch_command_usuario`). Los programas del sistema no pasan por ahí y siguen escribiendo en DATOS,
  así que Ajustes, `wifi`, `llm-config`, la memoria... no cambian. Los agentes (LLM) tienen su política de siempre.
* La comprobación está en el punto común de las órdenes (`agent_write_denied`), en `mkdir`/`rmdir`, en `nano` y en
  el gestor de archivos. El VFS en sí no cambia.
* `unid_borrar_rec` (`src/core/unidades.c`): borra una carpeta con todo lo que contiene, de a una entrada por vuelta y
  con poca pila (profundidad máxima 8); nunca borra la raíz de la unidad.

## Pruebas

* `tests/test_agent_policy.c`: casos de evasión de `usuario_ruta_escribible`.
* `tests/test_fs.c`: sesión de usuario (anidada, agentes, sin desbordar la tabla), y que el VFS no cambia fuera de la sesión.
* `tests/test_unidades_267.c`: borrado recursivo sobre pendrives reales (raíz protegida, MODELOS solo lectura, profundidad).

## Límites conocidos

* La Biblioteca del gestor (Imágenes, Vídeo...) sigue construyéndose con los archivos de la RAM; los de `/usuario` se
  ven en «Mi unidad».
* Un script del usuario (`.soma`, Python, C) que escribe con sus propias llamadas no pasa por la comprobación del terminal.
