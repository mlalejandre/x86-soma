# SOMA en el Lenovo ThinkPad T480s — plan de acción parche a parche

Este documento es una guía de ejecución. Está escrito para que un modelo (o una persona) pueda hacer cada parche sin
conocer la historia previa. No aplica nada: solo dice qué hacer, en qué orden, cómo probarlo y cuándo darlo por bueno.

**Punto de partida:** árbol con los parches 152 → 155D aplicados. SOMA arranca en el T480s por UEFI desde un pendrive,
se ve en pantalla, y funcionan teclado, trackpad (modo relativo) y TrackPoint.

---

## 1. Reglas para quien ejecute el plan

Estas reglas valen para todos los parches. Si una regla choca con un paso concreto, gana la regla.

1. **Un parche, un objetivo.** Si un parche crece más de ~600 líneas nuevas, pártelo (156, 156B…).
2. **Nunca escribir en el disco interno del portátil** (tiene Windows y Debian) salvo en el parche que lo trata
   expresamente, y ahí solo en una partición que el usuario haya creado para SOMA. Ante la duda, solo lectura.
3. **Todo hardware nuevo lleva interruptor de apagado** en la línea de órdenes de GRUB (`soma.smp=off`,
   `soma.apic=off`, `soma.nvme=off`…), leído con `bootinfo_has_option()`. El menú `grub/grub-t480s.cfg` debe tener
   siempre una entrada **«modo seguro»** que los apague todos. Así un parche fallido no deja el portátil sin arrancar SOMA.
4. **Si el hardware no responde como se espera, seguir como antes.** Cada driver comprueba y, si falla, imprime una
   línea y deja el camino antiguo (PIC, PIT, un núcleo, RAM-disk…). Nunca un pánico por hardware ausente.
5. **Cada parche añade una orden de diagnóstico en la shell** (como `pantalla` y `teclado`) que muestre lo detectado.
   El portátil no tiene puerto serie: la única salida es la pantalla y las fotos del usuario.
6. **Pruebas en tres niveles, siempre:**
   - *Simulador en el anfitrión* para lo que QEMU no emula (modelo: `tests/test_i8042_155.c`, con ASan/UBSan).
   - *QEMU* con `make run-t480s-uefi` y con el `make run` de siempre (Multiboot 1 + BGA): nada debe cambiar ahí.
   - *Portátil real*: lo hace el usuario; pídele qué mirar y una foto de la orden de diagnóstico.
7. **No romper lo demás:** compilar con `WERROR=1` para x86_64 y para `ARCH=aarch64 PLATFORM=rpi5` en cada parche.
8. **Límite de imagen:** el núcleo no puede pasar de 14 MiB (`linker_x86_64.ld`). Los búferes grandes van al montón
   (`kmalloc`), no a variables estáticas. Comprobar con `size build/x86_64/soma.elf`.
9. **Formato de entrega** (igual que 152–155D): script `parche_NNN_nombre.py` con `--plan`, `--self-test`, `--apply`,
   `--test`, `--undo`, encadenado al anterior por `.soma_parches/<nombre>`, más `docs/PARCHE_NNN_*.md`. Verificar la
   cadena completa sobre un árbol limpio y que `--undo` deja el árbol idéntico byte a byte.
10. **Referencias:** usar Linux como especificación (se cita el archivo en cada parche). Reimplementar la técnica; no
    copiar código GPL literalmente.
11. **Medir antes de afirmar.** Toda mejora de rendimiento se entrega con cifra antes/después. Si no mejora, se dice
    y se descarta.
12. **Flujo del usuario en cada parche:** `python3 parche_NNN.py --apply` y luego
    `python3 ejecutar.py -t t480s-usb --disco disk4`.

---

## 2. El hardware objetivo

Los valores exactos dependen de la configuración comprada. **El parche 156 los lee del propio portátil**; hasta
entonces, tratar esta tabla como lo esperado, no como hecho comprobado.

| Componente | Lo esperado en un T480s | Estado en SOMA hoy |
|---|---|---|
| CPU | Intel Core i5/i7 de 8.ª gen. (Kaby Lake-R), 4 núcleos / 8 hilos, AVX2, TSC invariante | 1 núcleo, sin SIMD en el núcleo |
| RAM | 8–24 GB DDR4 | Solo se usan 512 MiB; mapa de identidad de 1 GiB |
| Gráfica | Intel UHD 620, panel 1920×1080 o 2560×1440 | Framebuffer del firmware (GOP), sin aceleración ni brillo |
| Disco | SSD NVMe (M.2) | Sin persistencia: RAM-disk. Hay `nvme.c` (241 líneas) probado solo en QEMU |
| USB | xHCI (USB 3.1), USB-C/Thunderbolt 3 | `xhci.c` existe (teclado/ratón USB); sin almacenamiento |
| Teclado/puntero | i8042: teclado, Synaptics + TrackPoint (y SMBus/RMI4) | Funciona; trackpad en modo relativo, sin gestos |
| Ethernet | Intel I219 (familia e1000e) | Sin driver (solo virtio-net) |
| WiFi | Intel 8265 (iwlwifi) | Sin driver |
| Audio | Intel HDA + códec Realtek | `hda.c` existe, probado solo con el códec de QEMU |
| Energía | Batería(s), EC de ThinkPad, ACPI, tapa, ventilador | Nada: sin batería, sin apagado limpio |
| Interrupciones/tiempo | LAPIC, IOAPIC, HPET, TSC | PIC 8259 + PIT a 1 kHz; `apic.c` solo como sonda/prueba |

---

## 3. Orden general y dependencias

```
156 inventario ─┬─> 157 RAM completa ──> 158 APIC+reloj ──> 159 arranque de núcleos ──> 160 planificador SMP ──> 161 SIMD
                ├─> 162 USB almacenamiento ──> 163 NVMe
                ├─> 164 ACPI (uACPI) ──> 165 batería/tapa/apagado ──> 166 energía CPU ──> 174 suspensión
                ├─> 167 trackpad gestos ──> 168 teclas ThinkPad + brillo
                ├─> 169 pantalla HiDPI + volcado paralelo (necesita 159)
                ├─> 170 Ethernet I219 ──> 171 USB tethering ──> 175 WiFi
                └─> 172 audio real
173 = revisión de rendimiento global (necesita 157–161 y 169)
```

Hacerlos en orden numérico es válido. Lo que más rinde por esfuerzo: **156, 157, 158, 159, 162, 165**.

---

## 4. Los parches

Cada ficha tiene: objetivo, pasos, referencia, pruebas y criterio de aceptación.

### Fase A — Ver y medir

#### 156 · Inventario del hardware y registro de arranque
- **Objetivo:** saber exactamente qué hay en este portátil y poder leer el arranque después de ocurrido.
- **Pasos:**
  1. Búfer circular de 256 KB (en el montón) con todo lo que pasa por `kputc`. Orden `dmesg [texto]` con paginado.
  2. Orden `hw` con secciones: CPU (cadena de marca de CPUID, familia/modelo, núcleos/hilos por la hoja 0Bh, banderas:
     SSE4.2, AVX2, ERMS, x2APIC, TSC invariante, HWP, RDRAND), memoria (mapa de Multiboot 2 por tipos y total), PCI
     (lista completa: bus:dev.fn, vendedor:dispositivo, clase, BAR, IRQ), ACPI (firmas de todas las tablas del
     RSDT/XSDT), y SMBIOS (modelo, versión de BIOS; buscar la tabla por la tabla de configuración EFI).
  3. `hw guardar`: de momento solo muestra por páginas pensadas para fotografiar (40 líneas, pausa con tecla).
- **Referencia:** Linux `arch/x86/kernel/cpu/common.c`, `drivers/pci/probe.c`, `drivers/firmware/dmi_scan.c`.
- **Pruebas:** QEMU q35 (`-cpu max -smp 8`): la orden lista 8 hilos y los dispositivos de QEMU. Simulador: analizador
  de SMBIOS y de la hoja 0Bh con datos fijos.
- **Aceptación:** el usuario envía fotos de `hw`. **Actualizar la tabla de la sección 2 con los datos reales** y
  anotarlos en `docs/T480S_HARDWARE.md`. Los parches siguientes se apoyan en ese archivo.

### Fase B — Memoria, tiempo y núcleos

#### 157 · Usar toda la RAM
> **Hecho (parche 157).** Se resolvió con dos reservas en el PMM (baja < 512 MiB para identidad/DMA, alta para el montón,
> que se mudó a 256 GiB virtuales) en vez de ampliar el mapa de identidad. Ver docs/PARCHE_157_T480S_RAM.md.

- **Objetivo:** pasar de 512 MiB a toda la memoria instalada.
- **Pasos:**
  1. `pmm.c`: quitar `PMM_MAX_FRAMES` fijo. Calcular el marco más alto del mapa de Multiboot 2 y reservar el bitmap
     dinámicamente dentro de la primera región libre grande (1 bit por marco: 24 GB = 768 KB).
  2. Marcar como ocupado todo lo que no sea tipo 1 (libre): ACPI, reservado, y el framebuffer.
  3. `vmm.c`/`boot.s`: hoy solo hay identidad del primer GiB. Ampliar el mapa de identidad a toda la RAM con páginas
     de 2 MiB (o de 1 GiB si CPUID 80000001h:EDX bit 26). Hacerlo tras `pmm_init`, con tablas tomadas del PMM.
  4. Buscar todos los sitios que asumen direcciones físicas de 32 bits (`uint32_t` con marcos, `multiboot_info_addr`,
     colas virtio, DMA de xHCI/NVMe/HDA). Para DMA de dispositivos de 32 bits añadir `pmm_alloc_frame_low()` (< 4 GiB).
  5. Subir el límite del montón (`KHEAP_MAX_SIZE` en `mem.c`, hoy 256 MiB) a 1/4 de la RAM, con tope de 4 GiB.
- **Referencia:** Linux `arch/x86/mm/init_64.c` (mapa directo), `mm/memblock.c`.
- **Pruebas:** QEMU con `-m 512M`, `-m 4G`, `-m 12G`; prueba de estrés: reservar y verificar patrón en 2 GiB; todo lo
  anterior sigue funcionando con `-m 256M`. Simulador del asignador con mapas con huecos.
- **Aceptación:** `ramstat`/`hw` muestran la RAM real del portátil; el visor abre una imagen grande sin agotar memoria.
- **Riesgo:** direcciones > 4 GiB truncadas. Mitigación: compilar con `-Wconversion` solo en `pmm.c`/`vmm.c`.

#### 158 · APIC, IOAPIC y reloj de precisión
> **Hecho (parche 158).** Reloj por el temporizador del LAPIC calibrado contra el TSC (el uptime sigue contando ticks; la
> orden `irq` mide la deriva frente al TSC en el hardware real). Vuelta automática al PIC si el reloj no avanza.

- **Objetivo:** dejar el PIC/PIT de los años 80; base obligatoria para varios núcleos.
- **Pasos:**
  1. Analizar la tabla MADT de ACPI: dirección del LAPIC, IOAPIC, lista de procesadores, redirecciones de IRQ
     (*interrupt source overrides*). Guardarlo en una estructura global.
  2. `apic.c` ya sabe sondear y enrutar (`apic_probe`, `apic_route_on`). Convertirlo en el camino por defecto en
     hardware real: enmascarar el PIC, activar LAPIC (vector espurio), enrutar IRQ 1 y 12 por el IOAPIC respetando
     las redirecciones. `soma.apic=off` vuelve al PIC.
  3. Reloj: temporizador del LAPIC calibrado contra el PIT (o HPET si existe) como tic de 1 kHz. `timer_get_uptime_ms`
     pasa a basarse en el TSC si CPUID indica TSC invariante (80000007h:EDX bit 8).
  4. Orden `irq`: tabla de vectores, contador por IRQ, origen (PIC/IOAPIC).
- **Referencia:** Linux `arch/x86/kernel/apic/apic.c`, `io_apic.c`, `tsc.c`.
- **Pruebas:** QEMU q35: teclado y ratón funcionan con IOAPIC; el reloj no deriva (comparar con RTC durante 60 s).
  Simulador: analizador de MADT con una tabla real volcada.
- **Aceptación:** en el portátil `teclado` dice «IRQ 1/12: llegan» y `irq` muestra IOAPIC. Sin regresión de entrada.

#### 159 · Arranque de todos los núcleos
> **Hecho (parche 159).** Trampolín en memoria baja + INIT–SIPI–SIPI; cada secundario con su GDT/TSS; trabajo por ranuras
> con `hlt` e IPI de despertar al vector 0xF0 (solo si duerme). Sin datos por CPU en GS todavía: los secundarios no
> ejecutan hilos (eso llega con el 160). Ver docs/PARCHE_159_T480S_SMP.md.

- **Objetivo:** despertar los 8 hilos y usarlos como trabajadores.
- **Pasos:**
  1. Leer antes `src/arch/aarch64/smp.c`: ya implementa la interfaz (`smp_cpus`, `smp_parallel`, `smp_parallel_n`,
     `smp_core_info`). La versión x86 (`smp.c`, hoy un stub de 10 líneas) debe ofrecer **la misma interfaz**.
  2. Trampolín de 16 bits copiado a memoria baja (< 1 MiB): modo real → protegido → largo, carga de GDT/IDT/CR3
     compartidos, pila propia por núcleo.
  3. Secuencia INIT–SIPI–SIPI por LAPIC a cada procesador de la MADT, con espera y tiempo límite por núcleo.
  4. Datos por CPU (registro GS): identificador, pila, hilo actual. Activar en cada núcleo lo mismo que en el
     primero: NX, PAT (igual que `pat_enable_wc`), SSE si procede, LAPIC.
  5. Los núcleos secundarios quedan en un bucle de trabajo: esperan tareas de `smp_parallel` y duermen con `hlt`.
     **Todavía no ejecutan hilos del planificador.**
  6. `soma.smp=off` y `soma.smp=N` (limitar núcleos). Orden `cpus`.
- **Referencia:** Linux `arch/x86/kernel/smpboot.c`, `arch/x86/realmode/rm/trampoline_64.S`.
- **Pruebas:** QEMU `-smp 2`, `-smp 8`, `-smp 1`. `fb.c` ya reparte el volcado con `smp_parallel` cuando
  `smp_cpus() > 1`: medir el tiempo de `fb_present` con 1 y con 4 núcleos.
- **Aceptación:** `cpus` lista 8 en el portátil; el volcado de pantalla baja de tiempo; nada se cuelga en 10 minutos.
- **Riesgo:** condiciones de carrera en lo que ejecuten los trabajadores. Regla: los trabajadores solo ejecutan
  funciones puras sobre memoria ya reservada (sin `kmalloc`, sin `kprint`).

> **Antes del 160 (parche 164B):** el dibujo del orbe (halo y cintas), el borrado de la pantalla y la mezcla translúcida se
> reparten entre los núcleos con `smp_parallel` (resultado idéntico píxel a píxel); orden `orbe` para medir.

#### 160 · Planificador en varios núcleos
- **Objetivo:** que los hilos (red, agente, navegador, animación) corran en núcleos distintos.
- **Pasos:**
  1. Inventario de estado compartido en `thread.c`, `mem.c`, `fs`, `net`, `fb.c`. Añadir `spinlock` con guardado de
     interrupciones. Empezar con **un cerrojo grande del núcleo** y pocos cerrojos finos (montón, cola de listos).
  2. Cola de listos por núcleo + robo de trabajo sencillo. Tic del planificador por el temporizador LAPIC de cada
     núcleo. Interrupción entre procesadores para despertar y para invalidar TLB.
  3. Afinidad: el hilo de la GUI fijo en el núcleo 0 al principio.
  4. Prueba de estrés permanente: orden `smptest` (N hilos incrementando contadores con y sin cerrojo, reservas
     concurrentes del montón).
- **Referencia:** Linux `kernel/sched/core.c` (conceptos), `kernel/locking/spinlock.c`.
- **Pruebas:** QEMU `-smp 4` durante 30 min con navegador + `smptest`; ThreadSanitizer sobre las pruebas de anfitrión
  del montón y del planificador (`tests/host_sched_cfg.h` ya existe).
- **Aceptación:** con una página pesada cargando, el ratón no pierde fluidez; `sysmon` muestra carga por núcleo.
- **Riesgo alto.** Es el parche más delicado del plan: partirlo en 160 (cerrojos + cerrojo grande), 160B (colas por
  núcleo), 160C (quitar el cerrojo grande de las rutas calientes).

#### 161 · SIMD en las rutas calientes
- **Objetivo:** aprovechar AVX2/ERMS en copias y mezclas de píxeles.
- **Pasos:**
  1. El núcleo se compila con `-mgeneral-regs-only`. Mantenerlo. Añadir un único archivo `simd_x86.c` compilado con
     `-mavx2` y funciones `kernel_fpu_begin/end` (guardar/restaurar con XSAVE, sin cambio de contexto en medio).
  2. Selección en arranque según CPUID: `rep movsb` (ERMS) para copias; escrituras no temporales hacia el
     framebuffer WC; mezcla alfa y escalado de imágenes con AVX2.
  3. Enganchar: `fast_copy_span` (`fb.c`), mezcla de ventanas translúcidas, escalado de imágenes del navegador,
     cintas del orbe.
- **Referencia:** Linux `arch/x86/lib/memcpy_64.S`, `arch/x86/kernel/fpu/core.c`.
- **Pruebas:** comparación bit a bit contra la versión escalar en el anfitrión; QEMU `-cpu max` y `-cpu qemu64`
  (sin AVX2: debe caer a la versión escalar).
- **Aceptación:** cifras antes/después de `fb_present` y de un fotograma del escritorio. Si la ganancia es < 10 %,
  descartar esa ruta concreta.

### Fase C — Guardar datos

#### 162 · Almacenamiento USB (persistencia en el pendrive)
> **Hecho (parche 162).** El almacenamiento masivo ya existía (parche 049); lo nuevo es que la partición SOMADATA del
> pendrive es el disco del sistema (redirigiendo `virtio_blk_*`), con límites, marca y vuelta a solo lectura si cambia el
> pendrive. Ver docs/PARCHE_162_T480S_USB_DISCO.md. Pendiente: confirmar en el portátil que xHCI enumera el pendrive.

- **Objetivo:** que SOMA guarde archivos sin tocar el disco interno.
- **Pasos:**
  1. Comprobar en el portátil que `xhci.c` enumera dispositivos (orden `usb`). Corregir lo que falle en hardware real
     (traspaso de control BIOS→SO, puertos USB 3, concentradores).
  2. Clase de almacenamiento masivo (Bulk-Only + SCSI: INQUIRY, READ CAPACITY, READ/WRITE 10).
  3. `ejecutar.py -t t480s-usb` crea una segunda partición en el pendrive (tipo propio, etiqueta `SOMADATA`) con la
     imagen de disco de SOMA. El núcleo la busca por etiqueta/tipo y monta ahí SOMAFS/BIGFS a través de `blk_glue.c`.
  4. Volcado seguro: `sync` al cerrar la GUI y cada 5 s si hay cambios.
- **Referencia:** Linux `drivers/usb/storage/transport.c`, `drivers/usb/host/xhci.c`.
- **Pruebas:** QEMU `-device qemu-xhci -device usb-storage`; apagar a mitad de escritura y comprobar que el sistema
  de archivos monta. Simulador de las órdenes SCSI.
- **Aceptación:** crear un archivo en el portátil, reiniciar, y sigue ahí. `hw`/`dmesg` del 156 se pueden guardar en
  un archivo que el usuario lee desde el Mac (añadir esa función aquí).

#### 163 · NVMe del portátil
- **Objetivo:** disco rápido, con consentimiento explícito.
- **Pasos:**
  1. `nvme.c` existe: validarlo en hardware real en **solo lectura** (identificar, leer LBA 0, tabla GPT). Orden `discos`.
  2. Analizador GPT. SOMA solo monta una partición cuyo tipo GUID sea el propio de SOMA; el usuario la crea desde
     Debian con las instrucciones del documento del parche. Sin esa partición, el NVMe queda en solo lectura.
  3. Escritura únicamente dentro de los límites de esa partición (comprobación en la capa de bloques, no en el
     sistema de archivos). Interrupciones MSI-X, varias colas (una por núcleo tras el 160).
- **Referencia:** Linux `drivers/nvme/host/pci.c`, `block/partitions/efi.c`.
- **Pruebas:** QEMU `-device nvme` con una imagen GPT; prueba que intenta escribir fuera de la partición y debe fallar.
- **Aceptación:** Windows y Debian siguen arrancando tras usar SOMA; lectura secuencial medida en MB/s.

### Fase D — ACPI y energía

#### 164 · Intérprete ACPI
> **Hecho (parche 164).** uACPI + capa de adaptación (EC por la ECDT, SCI por sondeo). Ver docs/PARCHE_164_T480S_ACPI_ENERGIA.md.

- **Objetivo:** hablar con el firmware; sin esto no hay batería, tapa, apagado ni suspensión.
- **Pasos:**
  1. Integrar un intérprete AML de código abierto pensado para núcleos propios (**uACPI**, licencia MIT; alternativa
     LAI). Va en `third_party/`, con su capa de adaptación (memoria, puertos, PCI, temporizador, cerrojos).
  2. Cargar DSDT/SSDT, inicializar el espacio de nombres, instalar el manejador del controlador embebido (EC, puertos
     0x62/0x66) y la interrupción SCI.
  3. Orden `acpi`: dispositivos encontrados y sus `_HID`.
- **Pruebas:** QEMU q35 (tiene DSDT propio); prueba de anfitrión ejecutando el intérprete sobre el DSDT volcado del
  portátil (volcarlo a archivo con el 162).
- **Aceptación:** `acpi` lista en el portátil el EC, las baterías (`PNP0C0A`), la tapa (`PNP0C0D`) y el botón.
- **Riesgo:** tamaño de imagen (regla 8). Medir; si no cabe, subir el límite del enlazador con cuidado.

#### 165 · Batería, tapa, botón y apagado
> **Hecho (en el parche 164).** `bateria`, indicador en la barra, botón de encendido, `poweroff` por S5. Apagar la pantalla al cerrar la tapa: hecho en el 168.

- **Pasos:** `_BIF/_BIX` y `_BST` para carga y autonomía; `_LID`; eventos del EC; apagado S5 y reinicio por ACPI
  (hoy se reinicia por el 8042). Indicador de batería en el escritorio; aviso al 10 %; órdenes `bateria`, `apagar`.
- **Referencia:** Linux `drivers/acpi/battery.c`, `button.c`, `ec.c`.
- **Aceptación:** porcentaje coincide (±2 %) con el que da Debian; cerrar la tapa apaga la pantalla; `apagar` apaga.

#### 166 · Gestión de energía y temperatura de la CPU
> **Hecho (parche 166).** MWAIT C6 (HLT en máquinas virtuales), HWP con perfiles, temperatura/frecuencia/RAPL, MSR seguros, orden `energia`. Pendiente: tic variable (sin tic de 1 ms en reposo).

- **Pasos:** reposo con MWAIT y estados C (en vez de `hlt`); activar HWP/Speed Shift (MSR 770h/774h) con preferencia
  equilibrada y perfil «rendimiento»/«ahorro» en Ajustes; lectura térmica (MSR 19Ch/1B1h) y de energía RAPL en
  `sysmon`; reducir el tic cuando no hay trabajo.
- **Referencia:** Linux `drivers/idle/intel_idle.c`, `drivers/cpufreq/intel_pstate.c`.
- **Aceptación:** consumo en reposo medido por la descarga de batería (W) antes/después; temperatura en reposo; el
  ventilador no se dispara con el escritorio vacío.

### Fase E — Entrada y pantalla a la altura del equipo

#### 167 · Trackpad con gestos
> **Hecho (parche 167).** Modo absoluto Synaptics (W), gestos de uno y dos dedos, palma, TrackPoint por pass-through, `/etc/trackpad.conf` y orden `trackpad`, vuelta automática al modo relativo. Pendiente: desplazamiento horizontal, inercia y RMI4 por SMBus.
- **Pasos:** primero el **modo absoluto de Synaptics por PS/2** (paquetes de 6 bytes, modo W extendido): es una
  ampliación de `i8042.c`/`mouse.c` y no necesita bus nuevo. Con posición absoluta y número de dedos: desplazamiento
  con dos dedos (vertical y horizontal), toque con dos dedos = clic derecho, zonas del clickpad, rechazo de palma
  mientras se teclea, y TrackPoint por el canal de paso. Opcional después: RMI4 por SMBus (Intel i801), que es lo que
  usa Linux en este modelo.
- **Referencia:** Linux `drivers/input/mouse/synaptics.c`, `drivers/input/rmi4/`; comportamiento: libinput.
- **Pruebas:** ampliar el simulador `test_i8042_155.c` con paquetes absolutos grabados.
- **Aceptación:** desplazar una web con dos dedos es suave; no hay saltos del puntero al levantar un dedo.

#### 168 · Teclas del ThinkPad y brillo
> **Hecho (parche 168).** Brillo por la PWM del PCH (curva, mínimo, `/etc/brillo.conf`), consultas del EC (`_Qxx`), HKEY del ThinkPad, avisos de video 0x86/0x87 con `_DOS(4)`, volumen por el teclado, la tapa apaga la pantalla, orden `brillo`. Pendiente: retroiluminación del teclado y LED del micrófono, distribución completa (Impr Pant, Inicio/Fin).
- **Pasos:** teclas Fn (volumen, silencio, brillo, F-keys) por eventos del EC/ACPI (necesita 164); brillo del panel
  por ACPI (`_BCM`) o por el registro PWM de la gráfica Intel; retroiluminación del teclado y LED por el EC;
  distribución de teclado completa (teclas de función, Impr Pant, Inicio/Fin).
- **Referencia:** Linux `drivers/platform/x86/thinkpad_acpi.c`, `drivers/gpu/drm/i915/display/intel_backlight.c`.
- **Aceptación:** Fn+brillo y Fn+volumen funcionan y muestran un indicador en pantalla.

#### 168B · El orbe al frente (petición del usuario)
> **Hecho (parche 168B).** Tecla Windows (u Option sola en el Mac) en vez de la x; con aplicaciones abiertas el orbe sale delante de todas con una animación de salto estelar. Diagnóstico de la identificación del trackpad (Synaptics/Elan).

#### 169 · Pantalla: escala HiDPI y volcado eficiente
- **Pasos:** factor de escala de interfaz (1×, 1,5×, 2×) elegido por densidad del panel (EDID por GOP/ACPI) y en
  Ajustes: fuentes, iconos, cursor y ventanas. Volcado repartido entre núcleos (tras 159) y solo de zonas cambiadas.
  Composición de ventanas por capas en caché para no repintar las que no cambian (elimina de raíz el problema que el
  155D solo amortigua).
- **Aceptación:** texto legible en WQHD sin configurar nada; con cinco ventanas abiertas, mover una va a ≥ 50 fps.
- **Nota:** un driver nativo i915 (cambio de modo, sincronía vertical) queda fuera del plan; reevaluar al final.

### Fase F — Red y sonido

#### 170 · Ethernet Intel I219
> **Hecho (parche 170), a confirmar en el portátil.** Controlador e1000e (I219 con ULP y semáforo del firmware; 82574L/82540EM en QEMU), gestor de red con DHCP automático y orden `red`.
- **Pasos:** driver de la familia e1000e (MMIO, anillos de recepción/transmisión, PHY por MDIO, enlace), conectado a
  `netif.c` igual que virtio-net; DHCP ya existe. Orden `red`.
- **Referencia:** Linux `drivers/net/ethernet/intel/e1000e/`.
- **Pruebas:** QEMU `-device e1000e`. **Aceptación:** el navegador carga una web por cable en el portátil.

#### 171 · Red por USB (móvil o adaptador)
> **Hecho (parche 171), a confirmar con un móvil.** RNDIS, CDC-NCM y CDC-ECM en todas las configuraciones, en caliente, preferencia del cable. iPhone (ipheth) pendiente: exige emparejar.
- **Pasos:** clases CDC-ECM/NCM y RNDIS sobre xHCI: internet compartiendo el móvil por cable. Es el puente hasta
  tener WiFi y cuesta una fracción.
- **Referencia:** Linux `drivers/net/usb/cdc_ether.c`, `rndis_host.c`.

#### 172 · Audio real
> **Hecho (parche 172), a confirmar en el portátil.** BAR de 64 bits, DMA con snoop, CORB/RIRB, códec analógico (ALC257), caminos altavoz y auriculares con su DAC, selectores, EAPD, detección del conector. Pendiente: grabación (micrófono) y ajustes propios de Realtek si hacen falta.
- **Pasos:** `hda.c` en hardware real: enumerar el códec, árbol de widgets, encaminar altavoces y auriculares,
  detección de conector, volumen. Orden `audio`.
- **Referencia:** Linux `sound/pci/hda/hda_codec.c`, `patch_realtek.c`.
- **Aceptación:** se oye una pista por altavoces y por auriculares, con cambio automático.

### Fase G — Cierre

#### 173 · Revisión de rendimiento global
- Perfilador integrado (muestreo del contador de programa por el tic del LAPIC, orden `perfil`), que sustituye al
  truco de QEMU usado en el 155D. Medir arranque, apertura de cada aplicación, navegador (`tools/web_bench.py`) y
  escritorio (`tools/desk_bench.py`) en el portátil real. Fijar objetivos: arranque < 3 s hasta el escritorio,
  60 fps en escritorio a 1080p, reposo < 5 % de CPU.

#### 174 · Suspensión (S3) y reanudación
- Guardar/restaurar estado de dispositivos y núcleos, vector de despertar. Solo cuando 160–166 estén estables.

#### 175 · WiFi Intel 8265
> **Fase A (parche 175A).** Firmware iwlwifi-8265-36.ucode descargado e instalado por el parche (módulo de GRUB), lector TLV, carga de la imagen INIT y espera de ALIVE, orden `wifi`. Fases B (búsqueda) y C (WPA2) pendientes.
- El parche más grande del plan: carga de firmware, capa 802.11 y WPA2. Estimar como un proyecto propio de varios
  parches (175A carga de firmware y escaneo; 175B asociación a red abierta; 175C WPA2-PSK). No empezar antes de
  tener 160, 162 (para leer el firmware de un archivo) y 170 funcionando.

---

## 5. Plantilla de cada parche (copiar y rellenar)

```
Parche NNN — título
Requiere: parche anterior aplicado y docs/T480S_HARDWARE.md
Objetivo (una frase):
Interruptor de apagado: soma.xxx=off
Archivos nuevos / modificados:
Pasos de implementación:
Orden de diagnóstico añadida:
Pruebas: [ ] simulador anfitrión (ASan/UBSan)  [ ] QEMU T480s UEFI  [ ] QEMU de siempre  [ ] aarch64 compila
         [ ] tamaño de imagen < 14 MiB  [ ] cadena limpia + --undo exacto
Qué debe probar el usuario en el portátil (3–5 comprobaciones concretas) y qué foto enviar:
Medición antes/después (si es de rendimiento):
Limitaciones que quedan:
```

## 6. Qué hacer cuando algo falla en el portátil

1. Pedir foto de la orden de diagnóstico del parche y de `dmesg`.
2. Arrancar con la entrada «modo seguro» o con el interruptor del parche para confirmar que el fallo es suyo.
3. Reproducir en QEMU quitando lo que el portátil no tiene (`-serial none`, `-nic none`, sin virtio): así se encontró
   el fallo del teclado del 155C. Si QEMU no puede reproducirlo, ampliar el simulador de anfitrión con los datos de
   la foto.
4. Corregir con un parche de letra (NNNB) en vez de rehacer el anterior.
