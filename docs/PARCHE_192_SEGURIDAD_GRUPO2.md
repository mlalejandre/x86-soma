# Parche 192 · Seguridad, grupo 2

Requiere el 191. Probado arrancando en QEMU (x86_64: `pc` y `q35`, 1 y 4 núcleos, con y sin SMEP/SMAP/RDRAND; aarch64 `virt`: solo arranque).

## 1. Generador aleatorio único del núcleo (`src/core/rng.c`, orden `rng`)
- **Antes:** seis generadores distintos y casi todos predecibles.
  - `/dev/urandom` salía de un xorshift con semilla **constante** (`0x88888888`): la misma secuencia en cada arranque.
  - El DNS sembraba un xorshift de 32 bits con el tiempo de arranque (ID y puerto adivinables).
  - TLS, sin RDRAND, dependía solo del contador de ciclos. Los nonces de WPA en la Pi 5, de ciclos + ticks + MAC.
  - `crypto.getRandomValues` y `randomUUID` de JavaScript usaban `Math.random`, sembrado con la hora.
- **Ahora:** ChaCha20 con borrado rápido de clave. Semilla = SHA-256 de RDSEED/RDRAND (si existen), ruido del contador de ciclos,
  relojes y los instantes de llegada de las tramas de red; se resiembra cada minuto o cada MiB. Lo usan DNS, `/dev/urandom`, TLS,
  el secreto de los ISN de TCP, WPA (x86 y Pi 5) y JavaScript.
- **Límite:** sin RDRAND/RDSEED (QEMU con la CPU por defecto, Raspberry Pi 5) la entropía es solo ruido de tiempos y de red.
  `rng` lo indica. En QEMU: `-cpu max`. Pendiente: guardar una semilla en disco entre arranques.

## 2. W^X en todo el espacio del núcleo y página 0 protegida (`src/arch/x86_64/memprot.c`, orden `memprot`)
- **Antes:** el primer GiB quedaba mapeado escribible y ejecutable salvo la imagen del núcleo; la dirección 0 se podía leer y
  escribir. Comprobado con `soma.memprot=off`: `panic nulo` escribe en la dirección 0 sin fallo y `panic nx` ejecuta código
  desde un marco de datos.
- **Ahora:** solo `.text` es ejecutable (1012 MiB dejan de serlo con 1 GiB de RAM). La página 0 es de solo lectura: escribir por
  un puntero nulo detiene el sistema con su backtrace. Al partir una página de 2 MiB ya no se pierde el NX (`vmm.c`).
- **Interruptores de arranque:**
  - `soma.pagina0=estricta`: quita la página 0 del mapa (también fallan las lecturas). Recomendado para desarrollo: caza punteros
    nulos que hoy leen basura. En QEMU el escritorio, el terminal, el navegador, `hw`, `acpi` y `usfuzz` funcionan así.
  - `soma.memprot=off` (o el modo seguro): lo desactiva todo.
- **Sin probar en el T480s.** Si no arrancase, usa la entrada de modo seguro y dímelo.

## 3. `usfuzz` vuelve a pasar (`usermode.c`, `usermode.h`)
- **No lo rompió el parche 191:** fallaba desde el parche 138. El filtro de llamadas mata al proceso ante una llamada no
  permitida, y el fuzzer exige que una llamada inexistente devuelva un error. Los 24 procesos morían con código -6000.
- **Ahora:** un programa normal recibe `ENOSYS` y sigue; los procesos aislados (decodificadores, PDF, red) siguen muriendo.
  `SYS_SVC_NEXT` (20) sale del filtro por defecto: solo la usan los servicios, que la llevan en el suyo.

## 4. Retoques al parche 191
- ARP: hasta 4 peticiones propias a la vez (con una sola ranura, dos hilos resolviendo direcciones distintas se pisaban y uno
  esperaba 500 ms). La petición se anota antes de enviarla.
- Navegador: tras un `pushState` rechazado, `document.URL` y la base de las URL relativas ya no se quedan con la URL rechazada.

## 5. `make test` vuelve a enlazar
- `test_sched` y `test_mutex` no enlazaban desde el parche 166 (`cpu_idle`), y las tres pruebas de TCP y `fuzz-net` desde el 162
  (`netif_active`). Añadidos `tests/stubs_netif.c` y un stub en `tests/stubs_sched.c`. Nuevo: `tests/test_rng_192.c`.

## Comprobado en QEMU
| Prueba | Resultado |
|---|---|
| `usfuzz 24 3000` | TODO OK (antes: 24 procesos muertos) |
| Autopruebas `sbx` y `user` (ring 3) | OK |
| `oomtest`, `conctest` | TODO OK |
| q35, `-cpu max` (SMEP+SMAP, RDRAND+RDSEED), 4 núcleos, 4 GiB | arranca; `usfuzz`, `cpus prueba`, `oomtest` OK |
| `panic nulo`, `panic nx` | #PF y parada (con `soma.memprot=off`: no fallan) |
| Página web hostil (HTTP) | `fetch`/XHR/`<script>` a `file:`, navegar a `file:` y `pushState` a otro origen: bloqueados |
| Coste del fotograma del escritorio (`orbe`) | igual que antes del parche |

## Órdenes nuevas
`rng`, `memprot`, `panic nulo`, `panic nx`.

## Revertir
`python3 parche_192_seguridad_grupo2.py --revertir`
