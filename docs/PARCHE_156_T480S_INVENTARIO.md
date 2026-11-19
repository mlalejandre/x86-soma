# Parche 156 — T480s: inventario del hardware y registro del núcleo

Primer parche del plan de excelencia en el T480s (`docs/PLAN_T480S.md`). Requiere el 155D. No cambia el comportamiento
del sistema: añade la forma de **ver** el hardware real y de **leer el arranque**, que es la base de todos los parches
siguientes.

## Qué añade

### `hw` por secciones (pensadas para fotografiar)
| Orden | Muestra |
|---|---|
| `hw` | Resumen (equipo, CPU, RAM, pantalla) + ACPI + PCI |
| `hw cpu` | Nombre, núcleos e hilos, frecuencias, familia/modelo, extensiones (AVX2, ERMS, HWP, TSC invariante…), cachés |
| `hw mem` / `hw mem -v` | RAM según el firmware frente a la que gestiona SOMA; mapa de memoria completo con `-v` |
| `hw smbios` | Modelo y tipo de máquina, BIOS, procesador, módulos de RAM (tamaño, tipo, velocidad, referencia) |
| `hw acpi` | Tablas ACPI, procesadores de la MADT, LAPIC/IOAPIC, redirecciones de IRQ, HPET, PCIe ECAM |
| `hw pci [-v]` | Dispositivos PCI con fabricante y nombre de lo que lleva un T480s |
| `hw todo` | smbios + cpu + mem seguidos |

El número de serie del equipo **no** se lee.

### `dmesg`
Todo lo que imprime el núcleo desde el primer instante queda en un registro circular de 256 KiB (16 KiB estáticos hasta
que existe el montón). En el portátil no hay puerto serie y el arranque pasa demasiado rápido para leerlo.

- `dmesg`: las últimas 60 líneas.
- `dmesg 200`: las últimas 200.
- `dmesg PS/2`: solo las líneas que contienen «PS/2» (hasta 300).

### Tablas del firmware por encima de 1 GiB
El núcleo solo tenía proyectado en identidad el primer GiB. En un PC con UEFI las tablas ACPI y SMBIOS suelen estar entre
2 y 4 GiB, así que en el portátil `hw` y `apic` decían «ACPI no disponible». `hw_map_phys()` (en `hwscan.c`) proyecta en
identidad y **solo lectura** las páginas que falten por debajo de 4 GiB, y nunca toca páginas que ya se usen para otra cosa
(montón, ventana del framebuffer). Los parches de APIC (158) y de núcleos (159) leen la MADT por aquí.

### SMBIOS
Analizador puro en `hwinfo.c` (`hw_smbios_entry`, `hw_smbios_parse`) para los tipos 0, 1, 2, 4, 16 y 17. En UEFI se
encuentra por la tabla de configuración de la tabla de sistema EFI (Multiboot 2 entrega su dirección; el 156 la guarda).
En arranque BIOS se busca en `F0000–FFFFF`.

### Interruptores y modo seguro
`bootinfo_disabled("x")` devuelve 1 con `soma.x=off` en la línea de GRUB o con `soma.seguro`. A partir del 157, cada
driver nuevo pregunta ahí antes de tocar su hardware. El menú del T480s tiene una entrada nueva, **«SOMA - T480s (modo
seguro: sin drivers nuevos)»**, para arrancar si un parche futuro falla.

## Qué necesito del portátil

Arranca SOMA, abre el terminal y fotografía la salida de:

1. `hw smbios`
2. `hw cpu`
3. `hw mem`
4. `hw pci` (si no cabe en pantalla, desplaza y haz dos fotos)
5. `hw acpi`

Con eso relleno `docs/T480S_HARDWARE.md`, y el parche 157 (toda la RAM) se ajustará al mapa de memoria real.

## Pruebas realizadas

- **SMBIOS en el anfitrión** (`tests/test_smbios_156.c`, ASan/UBSan):
  - una tabla sintética con la forma de la de un T480s, campo a campo;
  - puntos de entrada `_SM3_` y `_SM_`, con suma de comprobación correcta e incorrecta;
  - todos los truncamientos posibles y 20 000 mutaciones aleatorias, sin una sola lectura fuera de límites.
- **El analizador ACPI anterior** (`tests/test_hwinfo.c`) sigue pasando.
- **QEMU q35 + UEFI, 4 GiB, 8 hilos** (como el portátil):
  - ACPI y SMBIOS, colocados por encima de 1 GiB, se leen bien;
  - `apic` ya lee la MADT;
  - `hw mem -v` muestra las 22 regiones;
  - `dmesg` filtra y no se registra a sí mismo.
- **QEMU clásico** (BIOS, Multiboot 1): `hw` funciona y encuentra SMBIOS en la zona `F0000`.
- **Raspberry Pi 5 (aarch64):** compila sin avisos; `dmesg` también funciona allí.

## Revertir
`python3 parche_156_t480s_inventario.py --undo`
