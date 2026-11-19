# Parche 159 · T480s: arranque de todos los núcleos

Requiere el 158 (modo APIC). Despierta los 8 hilos del i5-8350U y los usa como trabajadores de cálculo, con la misma
interfaz que ya tenía la Raspberry Pi 5 (`src/core/smp.h`).

## Qué cambia
- **`src/arch/x86_64/smp.c`** (antes un stub de un solo núcleo):
  - **Trampolín** de 16 KiB en memoria baja (por defecto `0x8000`; se busca un hueco que el firmware declare como RAM libre
    bajo 1 MiB). Una página de código (modo real → protegido → largo) y tablas de páginas propias (identidad del primer GiB
    con páginas de 2 MiB). Al llegar a 64 bits salta al núcleo, carga el CR3 de verdad y su pila.
  - **INIT–SIPI–SIPI** por el LAPIC a cada procesador habilitado de la MADT, de uno en uno, con tiempo límite. Si uno no
    responde, no se despierta ninguno más (evita que uno tardío use la pila del siguiente).
  - **Cada secundario** carga su propia GDT y su propio TSS (con su pila IST1 para #DF/#PF), la IDT común, y copia del
    núcleo 0 CR0, CR4, EFER, la PAT (así la pantalla sigue siendo write-combining en todos) y XCR0. El LAPIC queda con
    todo enmascarado salvo las IPI.
  - **Bucle de trabajo**: espera activa corta y luego `hlt`. El núcleo 0 publica el trabajo y, solo si el secundario
    está dormido, le manda una IPI al vector `0xF0` (su manejador solo hace EOI). Antes de cada trabajo el secundario
    vacía su TLB (el montón quita y pone páginas). Los secundarios no ejecutan hilos del planificador: eso es el 160.
- **Quién lo aprovecha ya**: el volcado de pantalla (`fb_present` reparte por filas los rectángulos grandes), PBKDF2 y
  `cpus prueba`.
- **`apic.c`**: envío de IPI (xAPIC y x2APIC, con la barrera que exige x2APIC), preparación del LAPIC de un secundario,
  y la MADT ya analizada para `smp.c`. Opción de prueba `soma.x2apic=on`.
- **`idt.c`**: `idt_load` e `idt_set_vector`. **`pmm.c`**: `pmm_range_is_ram`.
- **Orden `cpus`**: hasta 16 núcleos, ID de LAPIC, trabajos, tiempo ocupado, despertares y lo que tardó cada uno en
  arrancar. `hw cpu` dice cuántos usa SOMA.
- **GRUB**: nueva entrada «un solo núcleo» (`soma.smp=off`).
- **Pruebas en QEMU**: `-fw_cfg name=opt/soma/cmds,string="cpus;irq"` ejecuta órdenes al arrancar y las saca por serie
  (`@3000` espera 3 s). En el portátil no existe esa entrada y no hace nada.
- **Arreglo**: `cpus prueba` guardaba los resultados en una tabla de 4 (el máximo de la Pi 5); con 8 núcleos se desbordaba
  la pila. Lo detectó el protector de pila en la prueba de resistencia.

## Órdenes
- `cpus` — núcleos en marcha y su actividad.
- `cpus prueba` — SHA-1 de 6 MiB con 1 núcleo y con todos; muestra la aceleración.
- `cpus 1` / `cpus todos` — apaga o enciende el reparto sin reiniciar.

## Interruptores
- `soma.smp=off` (o la entrada «un solo núcleo» de GRUB, o el modo seguro): solo el núcleo 0.
- `soma.smp=N`: como mucho N núcleos (contando el 0).
- `soma.apic=off` también lo apaga (hace falta el modo APIC).

## Pruebas realizadas
- **Modelo del protocolo en el anfitrión** (`tests/test_smp_159.c`, con ThreadSanitizer): 8 hilos, 20 000 repartos de
  1 a 8 trozos; cada trozo se ejecuta exactamente una vez y no se pierde ningún despertar. Comprobado que la prueba
  detecta el error clásico (marcar «dormido» después de mirar si hay trabajo): se cuelga y lo informa.
- **QEMU q35 + UEFI** con `-smp 1, 2, 4, 8` y **PC clásico con BIOS** con `-smp 4, 8`: arrancan todos; `cpus prueba`
  acelera ×1,8 (2), ×2,4–4,2 (4), ×5,9–7,3 (8) en emulación, compartiendo los núcleos del anfitrión.
- **Resistencia**: 8 núcleos, escritorio en marcha, 25 rondas de `cpus prueba`: 2 346 trabajos por núcleo y 2 321 volcados
  de pantalla repartidos, sin errores. El escritorio se dibuja bien.
- **Arranque desde pendrive USB** (xHCI, UEFI, 8 núcleos): 6 de 6.
- `soma.smp=3` arranca 3 de 8; `soma.smp=off` deja uno.
- **Raspberry Pi 5 (aarch64)**: compila sin avisos.
- **No probado en emulación: el modo x2APIC.** El QEMU disponible no ofrece x2APIC sin KVM. El código sigue a Linux (ICR
  por el MSR 0x830 con barrera previa). Si el firmware del portátil entrega el LAPIC en x2APIC, `irq` dirá «x2APIC» y
  `cpus` mostrará si arrancaron.

## En el portátil
1. Arranca normal. En el arranque debe salir `SMP: 8 nucleos en marcha de 8`.
2. `cpus` → 8 núcleos con APIC 0..7. `cpus prueba` → aceleración (en el T480s se espera ×4–6: 4 núcleos físicos con
   2 hilos cada uno).
3. Usa el escritorio unos minutos: en `cpus` suben los «volcados de pantalla repartidos».
4. Si algo se cuelga al arrancar: entrada «un solo núcleo» de GRUB, y foto de la pantalla.

## Revertir
`python3 parche_159_t480s_smp.py --undo` y `make clean`.
