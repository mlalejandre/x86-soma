# SOMA 0.3-prebeta — imagen lista para el pendrive (opción rápida)

Esta carpeta trae SOMA **ya compilado** como imagen de disco. Solo tienes que grabarla en un pendrive y arrancar el PC desde él.
Si prefieres compilarlo tú mismo, sigue las instrucciones del [README principal](../README.md#-novedad-de-la-v03-arrancar-soma-desde-un-pendrive-en-un-pc-x86_64).

| Archivo | Qué es |
|---|---|
| `soma-0.3-prebeta-x86_64-usb.img.xz` | La imagen comprimida (17.4 MB; ocupa 5 GiB al grabarla) |

El pendrive queda con cuatro particiones: **SOMA** (el sistema, 1 GiB), **DATOS** (configuración y memoria de SOMA, 2 GiB; Windows y
macOS no la abren, es normal), **MODELOS** (modelos de IA) y **USUARIO** (tus archivos: Documentos, Imágenes, Audio...). MODELOS y
USUARIO se ven desde cualquier ordenador (exFAT). En un pendrive de más de 8 GB, SOMA las agranda solo en el primer arranque.
| `soma-0.3-prebeta-x86_64-usb.img.xz.sha256` | Su huella SHA-256, para comprobar que se descargó bien |

Generada el 28/11/2026 00:10 a partir del código de este repositorio con `python3 construyeimagen.py --carpeta`.

**Qué necesitas:** un pendrive de **8 GB o más** (se borra entero; con 32 GB o más, MODELOS tiene sus 16 GiB) y un PC x86_64 con **UEFI**. Probado en un Lenovo ThinkPad T480s.

> ⚠️ Grabar la imagen **borra el pendrive entero**. Comprueba dos veces que eliges el pendrive y no el disco del ordenador.

## La forma más sencilla (Windows, macOS y Linux): balenaEtcher

1. Descarga **balenaEtcher** de https://etcher.balena.io e instálalo.
2. *Flash from file* → elige `soma-0.3-prebeta-x86_64-usb.img.xz` (no hace falta descomprimirlo).
3. *Select target* → tu pendrive. Etcher solo muestra discos externos.
4. *Flash!* y espera a que termine la verificación.

## Windows: Rufus

1. Descarga **Rufus** de https://rufus.ie.
2. *Dispositivo*: tu pendrive. *Elección de arranque*: **SELECCIONAR** → `soma-0.3-prebeta-x86_64-usb.img.xz`.
3. **EMPEZAR**. Si pregunta el modo, elige **Escribir en modo Imagen DD**.
4. Si después Windows ofrece «formatear» alguna unidad del pendrive, pulsa **Cancelar**.

## macOS: Terminal

```
diskutil list external              # localiza tu pendrive, por ejemplo disk4
diskutil unmountDisk /dev/disk4
xzcat soma-0.3-prebeta-x86_64-usb.img.xz | sudo dd of=/dev/rdisk4 bs=4m
diskutil eject /dev/disk4
```

Si macOS dice que no puede leer un disco, es la partición DATOS de SOMA: pulsa **Ignorar**, nunca «Inicializar».

Para **actualizar** SOMA más adelante sin perder nada, usa `python3 construyeimagen.py --grabar` desde el repositorio: solo
reescribe la partición del sistema. (Grabar esta imagen otra vez con Etcher o `dd` borra el pendrive entero.)

## Linux: Terminal

```
lsblk -o NAME,SIZE,TRAN,MODEL       # localiza tu pendrive (TRAN = usb), por ejemplo sdb
sudo umount /dev/sdb?*
xzcat soma-0.3-prebeta-x86_64-usb.img.xz | sudo dd of=/dev/sdb bs=4M status=progress conv=fsync
```

## Comprobar la descarga (opcional)

- Windows (PowerShell): `Get-FileHash soma-0.3-prebeta-x86_64-usb.img.xz -Algorithm SHA256`
- macOS: `shasum -a 256 soma-0.3-prebeta-x86_64-usb.img.xz`
- Linux: `sha256sum -c soma-0.3-prebeta-x86_64-usb.img.xz.sha256`

El resultado tiene que coincidir con el de `soma-0.3-prebeta-x86_64-usb.img.xz.sha256`.

## Arrancar

1. En la BIOS/UEFI del PC **desactiva Secure Boot** (SOMA no está firmado). En el ThinkPad T480s: `F1` al encender → *Security → Secure Boot → Disabled*.
2. Enchufa el pendrive, abre el menú de arranque (`F12` en Lenovo; `F11`, `F8` o `Esc` en otras marcas) y elige el USB.
3. En el menú de GRUB elige **SOMA** (o *resolución segura 1024x768* si la pantalla se queda en negro).
4. En el primer arranque SOMA prepara la partición `DATOS` y, si el pendrive es de más de 8 GB, agranda `MODELOS` (hasta 16 GiB) y `USUARIO` (el resto) mientras están vacías: desde ahí lo que guardes en SOMA se conserva.

¿Tu teclado no es español? Elige **SOMA - otro PC con teclado inglés (US)** en el menú de GRUB.

¿No arranca? Prueba otro puerto USB y comprueba que el PC está en modo **UEFI** (no «Legacy/CSM»).

## ¿Algo no funciona en tu equipo? Comparte el informe

SOMA está probado a fondo en un ThinkPad T480s; en otros equipos puede faltar algún driver. Para que se pueda arreglar:

- **Si arranca:** SOMA guarda solo, en cada arranque, un informe en el archivo **`INFORME.TXT`** de la unidad *SOMA* del
  pendrive. Enchufa el pendrive en tu ordenador de siempre, abre ese archivo y compártelo (en los *issues* del repositorio o
  en los comentarios del vídeo). Dice qué equipo tienes, qué dispositivos tienen driver en SOMA y cuáles no, y en qué paso del
  arranque hubo problemas. No contiene contraseñas ni claves. También puedes generarlo en *Ajustes → Sistema → Guardar informe*.
- **Si se queda colgado:** la última línea `[ARRANQUE n/13] ...` de la pantalla dice en qué paso. Haz una foto. Si sale una
  pantalla de error, dice qué opción de GRUB salta ese paso (pulsa `e` en el menú, añádela al final de la línea `multiboot2` y
  pulsa `F10`). En el siguiente arranque que llegue al escritorio, el informe recordará dónde se quedó el anterior.

## Licencias

SOMA es software libre con licencia MIT (archivo `LICENSE` del repositorio), de Marcos Luis Álvarez Alejandre.
La imagen incluye el firmware WiFi de Intel (`iwlwifi-8265-36.ucode`, licencia de redistribución de Intel, ver
`firmware/LICENCE.iwlwifi_firmware`), GRUB (GPLv3, código en https://www.gnu.org/software/grub/) y las fuentes DejaVu
y Liberation Sans (licencias libres). Más detalles en SOMA → Ajustes → Acerca de.
