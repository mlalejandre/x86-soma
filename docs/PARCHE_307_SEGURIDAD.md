# Parche 307 · Seguridad: el SO aplica la misma lista de archivos protegidos que el anfitrión

**Hace falta `make`** (cambia `src/agent/agent.c`).

## El problema
Cuando el agente propone un parche del núcleo (acción `patch`), hay dos barreras:
1. **Dentro del SO** (`agent.c`, `is_patch_target_protected`): rechaza al momento los archivos protegidos.
2. **En el anfitrión** (`ejecutar.py` + `protected.txt`): no aplica un parche que toque un archivo protegido.

La de dentro no funcionaba:
* Comparaba la **ruta entera** con el nombre: el parche dice `FILE: src/core/thread.c`, se comparaba `core/thread.c` con
  `thread.c` y no casaba. En la práctica no rechazaba nada salvo los archivos de la raíz de `src/`.
* Tenía su propia lista de 23 nombres; el anfitrión protege 76. Faltaban, por ejemplo, `shell.c`, `tls13.c`, `usermode.c`,
  `sandbox.c`, `agent_policy.h` o `x509.c`.
* Para decidir si el parche era de C miraba si aparecía `.c`, `.h` o `.s` **en cualquier parte del texto** que seguía, y si
  era del espacio de trabajo, si aparecía `.txt`. Una palabra en la explicación del modelo cambiaba la decisión.

Todo dependía, pues, del anfitrión, que sí funciona. No había un hueco abierto, pero sí una barrera de menos.

## Qué cambia
* `src/agent/agent_protegidos.h` (nuevo): la lista del anfitrión entera (78 nombres, con los dos nuevos de abajo) y funciones
  puras que sacan la ruta del texto del parche y comparan **el nombre de archivo**, igual que `ejecutar.py`.
* `agent.c` usa esas funciones. La decisión de si es C o del espacio de trabajo se toma con la ruta del parche, no con el resto
  del texto.
* `protected.txt`: se protegen también `agent_protegidos.h` (la propia lista) y `pila_canario.h` (el canario del 306).
* `tools/audit.py`: lee la lista nueva. Con `--strict`, el que usa `make ci`, **falla si el SO y el anfitrión no protegen
  exactamente lo mismo**. Si añades un archivo a `protected.txt` y no a `agent_protegidos.h` (o al revés), la CI lo dice.

## Pruebas
* `tests/test_agent_protegidos_307.c`: casos con carpetas, `src/` delante, saltos de línea y comillas detrás, nombres
  parecidos (`mythread.c`, `thread.c.bak`) y cada nombre de la lista en cualquier carpeta.
* `make ci`: la auditoría da «coherente» (78 = 78).

## Comprobado
`make ci` (gcc 14), x86_64 y aarch64 con `WERROR=1` desde cero, `boot-smoke` en las dos, y `--revertir` deja el árbol idéntico.
