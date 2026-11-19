# Parche 166 · T480s: energía y temperatura del procesador

Requiere el 164D. Ficha 166 del plan. Ahora que SOMA usa los 8 hilos, toca que el procesador duerma de verdad cuando no hay
trabajo, que use el turbo bien y que se vea su temperatura y su consumo.

## Qué cambia
- **Reposo profundo (MWAIT)**: antes el procesador descansaba con `HLT`, que es el estado C1, el más ligero. Ahora el hilo
  inactivo y los núcleos de cálculo entran en **C6** con `MWAIT`.
  - Los núcleos de cálculo vigilan su ranura de trabajo: la propia escritura del núcleo 0 ya los despierta.
  - Sin ARAT (el temporizador del LAPIC se pararía en C6), el núcleo 0 se queda en C1. El T480s tiene ARAT.
  - En una máquina virtual se usa HLT, como Linux: en QEMU, MWAIT no duerme con varios procesadores.
- **HWP / Speed Shift**: el procesador elige su frecuencia, con turbo, según un perfil:
  - **rendimiento**: preferencia 0 y reposo C1, el despertar más rápido;
  - **equilibrado** (por defecto): preferencia 128 y reposo C6;
  - **ahorro**: preferencia 192, sin turbo (como mucho la frecuencia base) y reposo C6.
  - El perfil se guarda en `/etc/energia.conf`.
- **Lecturas**:
  - temperatura de cada núcleo y del paquete (sensor digital, límite TjMax del propio procesador);
  - **frecuencia real** de cada núcleo (APERF/MPERF), para ver el turbo;
  - % del tiempo despierto;
  - **consumo** del paquete, de los núcleos y de la gráfica (RAPL).
- **Monitor del sistema**: ya enseña la temperatura (con su gráfica) y la frecuencia real. Antes ponía «no disponible».
- **MSR seguros**: todo se lee con `msr_read_safe`. Si un registro no existe (otra CPU, QEMU), la #GP se convierte en un
  error en vez de un pánico, con una tabla de arreglos en el manejador como hace Linux. Hay una autoprueba al arrancar.
- **Orden `energia`**: perfil, rango de frecuencias, estado de reposo, consumo en W, temperatura y frecuencia de cada núcleo.
  `energia rendimiento | equilibrado | ahorro` cambia el perfil.

## Interruptores
- `soma.hwp=off`: no activa Speed Shift. Una vez activado, el procesador no lo desactiva hasta reiniciar (así lo define Intel).
- `soma.mwait=off`: reposo con HLT. `soma.mwait=on` lo fuerza incluso en una máquina virtual.
- El modo seguro apaga los dos.

## Pruebas realizadas
- QEMU con `-cpu max`, `Skylake-Client` y `qemu64`: arranca, `energia` funciona y lo que no hay sale como no disponible.
- Autoprueba del mecanismo de arreglos: un #GP provocado vuelve con error, sin pánico.
- Resistencia: 8 rondas de `cpus prueba` con el escritorio y el terminal abiertos, con 8 núcleos.
- Al probar se vio que MWAIT no duerme en QEMU con varios procesadores: los núcleos de cálculo giraban sin parar y el
  sistema iba a paso de tortuga. Por eso en una máquina virtual se usa HLT.
- Raspberry Pi 5: compila.
- **No probado en emulación**: HWP, sensores, RAPL y los estados C reales. QEMU no los tiene; se verán en el portátil.

## En el portátil
1. En el arranque: `ENERGIA: perfil equilibrado | HWP activo (turbo hasta 3600 MHz) | reposo MWAIT C6 | sensor de
   temperatura | medidor de consumo (RAPL) | MSR seguros: OK`.
2. `energia` en reposo: foto (consumo del paquete y temperaturas). Después `cpus prueba` y en seguida `energia`: las
   frecuencias deben subir a ~3,6 GHz.
3. Abre el Monitor del sistema: temperatura y frecuencia.
4. Si quieres, compara la autonomía de `bateria` en reposo con el perfil `energia ahorro`.
5. Si el arranque se queda parado tras la línea SMP: modo seguro de GRUB y foto.

## Revertir
`python3 parche_166_t480s_energia_cpu.py --undo` y `make clean`.
