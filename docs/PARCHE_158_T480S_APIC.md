# Parche 158 — T480s: APIC, IOAPIC y reloj del LAPIC

Tercer parche del plan (`docs/PLAN_T480S.md`). Requiere el 157. Es la base obligatoria del 159 (los 8 hilos): con el
PIC 8259 de los años 80 solo se puede tener un procesador.

## Qué cambia

| Pieza | Antes | Ahora |
|---|---|---|
| Controlador de interrupciones | PIC 8259 | **LAPIC + IOAPIC**; el PIC queda enmascarado entero |
| Reloj del sistema (1 kHz) | PIT 8254 por el PIC | **Temporizador del LAPIC**, periódico, calibrado contra el TSC (10 ms) |
| Teclado (IRQ1) y trackpad (IRQ12) | por el PIC | por el **IOAPIC**, con la GSI, polaridad y disparo que dice la MADT |
| Modo x2APIC | no soportado | soportado (si el firmware lo deja activo, se usa por MSR) |

### Cómo se activa (al arrancar, tras el teclado)
1. Se lee la **MADT**, que en UEFI está por encima de 1 GiB y se alcanza con `hw_map_phys` del 156: LAPIC, IOAPIC y
   redirecciones (en el T480s, IRQ0 → GSI 2 y la SCI por nivel).
2. **LAPIC:** activado por software (vector espurio 255), TPR 0, LINT0 enmascarado (por ahí entraba el PIC) y la línea
   NMI que diga la MADT (LINT1).
3. **IOAPIC:** IRQ1 → vector 33 y IRQ12 → vector 44, los mismos vectores de antes.
4. **Reloj:** si la CPU tiene **ARAT** (el temporizador sigue contando en reposo; el i5-8350U lo tiene), se usa el
   temporizador del LAPIC. Si no, el PIT a través del IOAPIC.
5. **PIC 8259** enmascarado.
6. **Comprobación:** el reloj debe avanzar en 30 ms. Si no, se deshace todo (incluido el estado del LAPIC, que en modo
   PIC es la entrada de las interrupciones del 8259) y se sigue con el PIC.

### Análisis de la MADT
`hwinfo.c` guarda ahora la GSI base y el ID de cada IOAPIC y la lista de procesadores con su ID de APIC (tipos 0 y 9, sin
duplicados), que es lo que necesitará el 159 para despertar los núcleos. También guarda la línea NMI. La función
`hw_isa_irq_route()` traduce una IRQ ISA a GSI, polaridad y disparo con las reglas de ACPI.

## Órdenes
- **`irq`** (nueva). Muestra:
  - el modo (APIC o PIC) y por qué;
  - el LAPIC (ID, xAPIC/x2APIC, ARAT) y el IOAPIC;
  - el reloj (cuentas por ms y frecuencia del bus del LAPIC);
  - la ruta de cada IRQ con lo que realmente contiene el IOAPIC;
  - los contadores de interrupciones atendidas;
  - la **precisión del reloj**: los ms contados desde que se activó, frente a los que mide el TSC, con la deriva en tanto por mil.
- **El banner del terminal** dice qué reloj se usa.
- **La orden `apic` antigua** (parches 037–039) ya no puede apagar el modo del sistema ni pisar su temporizador.

## Interruptores
- `soma.apic=off` (o la entrada «modo seguro»): se queda en el PIC, como antes.
- `soma.apic=pit`: modo APIC pero con el PIT como reloj.

## Pruebas realizadas
- **Analizador de la MADT en el anfitrión** (`tests/test_apic_158.c`, ASan/UBSan):
  - RSDP + XSDT + MADT como las del T480s: 8 hilos con IDs 0–7, una entrada deshabilitada, un duplicado x2APIC, el IOAPIC
    2 en `0xFEC00000`, IRQ0 → GSI 2, la SCI por nivel y la NMI en LINT1;
  - 20 000 MADT mutadas sin errores.
- **El analizador ACPI anterior** (`tests/test_hwinfo.c`) sigue pasando.
- **QEMU q35 + UEFI, `-cpu max`** (con ARAT):
  - modo APIC con el temporizador del LAPIC;
  - teclado y ratón por el IOAPIC: los contadores suben al teclear y mover el ratón en el terminal;
  - el navegador carga una página con 12 imágenes;
  - la autoprueba antigua del LAPIC no toca el reloj.
- **`-cpu qemu64` (sin ARAT) y QEMU clásico** (BIOS, Multiboot 1): reloj por el PIT a través del IOAPIC respetando
  IRQ0 → GSI 2; la comprobación cuenta 30 ticks en 30 ms exactos.
- **Fallo provocado** (temporizador del LAPIC enmascarado a propósito): se detecta, se deshace y el sistema sigue en modo
  PIC con el reloj, el teclado y la interfaz funcionando. Esta prueba encontró un fallo en la primera versión de la vuelta
  atrás (no restauraba LINT0 y el reloj quedaba parado), ya corregido.
- **Arranque desde pendrive USB (xHCI) repetido:** en la primera versión fallaba 1 de cada 3–5 arranques con un
  #GP (código `0xFFFFFFFA`) justo al reactivar las interrupciones. El registro de interrupciones de QEMU mostró la
  causa: el PIC tenía pedida una IRQ del PIT cuando se enmascaraba LINT0, y llegaba sin vector. Ahora el PIC se enmascara
  lo primero, antes de tocar el LAPIC (el mismo orden que Linux). Después: 12 de 12 arranques correctos, 0 entregas sin
  vector.
- **Raspberry Pi 5 (aarch64):** compila sin avisos.

En emulación (QEMU sin KVM) el reloj avanza a un ~65 % del tiempo real, tanto con APIC como con PIC: QEMU pierde
interrupciones del temporizador cuando la CPU emulada va saturada. En hardware real eso no ocurre. Para comprobarlo,
`irq` muestra la deriva frente al TSC.

## En el portátil
Escribe `irq` tras un minuto de uso. Deberías ver:
- «APIC (LAPIC + IOAPIC)» y el reloj «temporizador del LAPIC» (el bus del LAPIC del i5-8350U debería dar 24 MHz: el reloj de
  cristal del chipset);
- los contadores del teclado y del trackpad subiendo;
- una deriva cercana a 0 por mil.

## Revertir
`python3 parche_158_t480s_apic.py --undo`
