# Parche 175A2 · T480s: el firmware de la WiFi también dentro del pendrive

Requiere el 175A. Corrige un fallo del 175A visto en el portátil: `wifi` decía que no se cargó el firmware.

## Causa
`ejecutar.py -t t480s-usb` no usa la imagen ISO de `make t480s`. Compila `make t480s-usb`, que empaqueta GRUB, el menú y
el núcleo en un único `BOOTX64.EFI`. El 175A metía el firmware solo en la ISO, así que en el pendrive no estaba. El menú
lo pedía con `if [ -f ... ]` y, al no encontrarlo, seguía sin avisar.

## Arreglo
- `make t480s-usb` mete `firmware/iwlwifi-8265-36.ucode` dentro del `BOOTX64.EFI` (en `/boot/firmware/`) y carga el
  módulo `test` de GRUB. Avisa al compilar si falta: `BOOTX64.EFI: con el firmware WiFi ...`.
  El `BOOTX64.EFI` pasa de unos 10,4 a unos 12,9 MB. El límite de 14 MiB es del núcleo, que no cambia.
- `tools/firmware_wifi.py` busca también en `intel/iwlwifi/`, donde lo guarda la versión actual de `linux-firmware`. Por
  eso GitLab daba 404.

## Prueba
QEMU con UEFI, arrancando el `BOOTX64.EFI` desde una unidad FAT, como el pendrive: `wifi` encuentra el módulo y lo valida.
