# Parche 154 — Lenovo ThinkPad T480s: arranque y pantalla

Primer parche de la serie T480s. Objetivo: que SOMA arranque desde una memoria USB en el portátil (UEFI o BIOS
heredada/CSM) y se vea en su pantalla, desde el primer mensaje del núcleo hasta el escritorio. El teclado y el
trackpad van en el parche 155.

## Qué cambia

| Pieza | Antes (solo QEMU) | Ahora |
|---|---|---|
| Protocolo de arranque | Multiboot 1 | Multiboot 1 **y** Multiboot 2 (el mismo `soma.elf`) |
| Pantalla | Bochs BGA (solo existe en QEMU) | Framebuffer que deja el firmware: UEFI GOP o VBE (como `efifb`/`simplefb` de Linux) |
| Texto de arranque | VGA de texto en 0xB8000 (no existe en UEFI) | Consola sobre el framebuffer, fuente 8x8 a escala 2 (3 en WQHD) |
| Mapa de memoria | Multiboot 1 | También la etiqueta 6 de Multiboot 2 (mapa E820/UEFI) |
| ACPI (RSDP) | Búsqueda en 0xE0000–0xFFFFF (falla en UEFI) | Primero la etiqueta 14/15 de Multiboot 2 que entrega GRUB |
| Imagen | `build/soma.iso` | Además `build/soma-t480s.iso` (UEFI x86_64 + BIOS en la misma imagen) |

### Detalles técnicos

- **`src/arch/x86_64/bootfb.c/.h`** (nuevo). Lee la información de Multiboot 2 (línea de órdenes, cargador, mapa de
  memoria, framebuffer, RSDP, tabla de sistema EFI) y, si se arrancó con Multiboot 1, sus campos de framebuffer.
  Valida el framebuffer (RGB directo, 24/32 bpp, tamaños razonables) y lo mapea en `0xE0000000`.
- **Write-combining (PAT)**, igual que Linux: la entrada PA1 del MSR `IA32_PAT` pasa a WC, y el framebuffer se mapea
  con PWT. En la UHD 620 del T480s eso multiplica la velocidad de escritura en VRAM. Lo MMIO existente (PWT|PCD) sigue
  siendo no cacheable.
- **Consola de arranque**: rejilla de celdas en memoria, scroll por saltos de un tercio de pantalla y sin leer nunca
  la VRAM (leerla es lentísimo con WC). El texto anterior a mapear la pantalla se guarda y se repinta. Ante un pánico
  la consola vuelve a mostrarse aunque el escritorio esté activo.
- **`fb.c`**: si hay framebuffer del firmware, el escritorio usa esa resolución nativa (no se puede cambiar de modo sin
  driver i915) y convierte a 24 bpp o BGR si hiciera falta. Sin framebuffer del firmware, BGA como siempre.
- **Cintas del orbe**: el acumulador del 153B (640 KB fijos en la imagen) pasa al montón y se dimensiona según el
  radio real del orbe. A 2560x1440 ya no se recortan, y la imagen del núcleo vuelve a caber en su límite de 14 MiB.
- **`grub/grub-t480s.cfg`**: tres entradas (resolución alta, 1024x768 segura, modo nativo del firmware).
  `gfxpayload` se fija **después** de `multiboot2`, porque esa orden lo sobrescribe con lo que pide la cabecera
  del núcleo (comprobado con GRUB 2.12).
- Orden nueva en la shell: **`pantalla`** (driver, resolución, bpp, pitch, origen GOP/VBE y cargador).
- Opción de arranque `soma.fb=bga`: ignora el framebuffer del firmware (solo para pruebas en QEMU).

## Construir la memoria USB (macOS)

```sh
make clean
make docker-make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1' DOCKER_TARGET=t480s
```

La primera vez se reconstruye la imagen de Docker para incluir GRUB UEFI (`grub-efi-amd64-bin`). Con toolchain
nativo: `make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='-DSOMA_WEB_ASYNC_IMAGES=1' t480s`.

Grabar (sustituye `diskN` por la memoria, compruébalo dos veces con `diskutil list`):

```sh
diskutil list
diskutil unmountDisk /dev/diskN
sudo dd if=build/soma-t480s.iso of=/dev/rdiskN bs=4m
diskutil eject /dev/diskN
```

## Arrancar en el T480s

1. Enciende y pulsa **F1** para entrar en la BIOS.
2. *Security → Secure Boot* → **Disabled** (GRUB no está firmado).
3. *Startup → UEFI/Legacy Boot*: deja **UEFI Only** (recomendado) o **Both** si quieres probar BIOS heredada.
4. Guarda (F10), reinicia y pulsa **F12** para elegir la memoria USB.
5. En el menú: **SOMA - Lenovo ThinkPad T480s**. Si la pantalla queda negra o rara, reinicia y prueba
   *resolución segura 1024x768* o *resolución nativa del firmware*.

Qué deberías ver: el menú de GRUB, después el texto de arranque de SOMA (línea `PANTALLA: framebuffer del
firmware 1920x1080 (UEFI GOP)` o 2560x1440 en el panel WQHD) y, al final, el escritorio con el orbe.

En este parche el teclado interno puede funcionar ya (es PS/2 a través del i8042 que emula el EC), pero no está
garantizado; el trackpad no. Ambos son el objetivo del parche 155.

## Probar en QEMU como si fuera el portátil

```sh
make run-t480s-uefi     # q35 + firmware UEFI (OVMF/edk2), sin BGA
make run-t480s-bios     # q35 + BIOS heredada (VBE)
```

`make run` (Multiboot 1 + BGA) sigue funcionando exactamente igual que antes.

## Resultados en QEMU

| Arranque | Resolución recibida | Consola de arranque | Escritorio |
|---|---|---|---|
| q35 + OVMF (UEFI GOP) | 2560x1440 | sí | sí, orbe completo |
| q35 + SeaBIOS (VBE) | 2560x1440 | sí | sí |
| Entrada 1024x768 | 1024x768 | sí | sí |
| `make run` de siempre (MB1 + BGA) | 1920x1080 | VGA de texto | igual que antes |

## Limitaciones conocidas (para los siguientes parches)

- **Sin cambio de resolución**: la resolución es la que deja GRUB/el firmware. Cambiarla en caliente requiere un
  driver i915 (fuera de alcance por ahora).
- **Texto pequeño en WQHD**: el escritorio aún no escala su interfaz (la consola de arranque sí).
- **Disco**: el T480s tiene NVMe; SOMA solo conoce virtio-blk, así que arranca con disco en RAM (SOMAFS se formatea en
  memoria y no se guarda nada). Driver NVMe: parche posterior.
- **Red**: Intel I219-LM y Wireless 8265 sin driver todavía.
- **Memoria**: el PMM sigue limitado a 512 MiB.
- **Temporizadores**: se usan PIT y PIC 8259; el T480s los mantiene, pero si el reloj fuese inestable el siguiente
  paso es HPET/LAPIC.

## Revertir

`python3 parche_154_t480s_arranque_pantalla.py --undo` (antes de revertir el 153B).
