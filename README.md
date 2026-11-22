# 🧠 SOMA v0.3 pre-beta — AI-Native Operating System with Cosmic GUI

SOMA (Sistema Operativo Multi-Agente) es un sistema operativo experimental de 64 bits para arquitecturas x86_64 y AArch64 desarrollado desde cero en C freestanding y ensamblador, sin Linux, sin glibc ni dependencias externas.

> **🍓 Primera versión que arranca en una Raspberry Pi 5 real.** Probada en placa: consola de texto y escritorio gráfico por HDMI a la resolución del monitor, teclado y ratón USB en los cuatro puertos y ventilador activo. También se puede probar sin placa en una Pi 5 emulada (QEMU `raspi5b`).

Diseñado desde el hardware como entorno nativo para agentes de Inteligencia Artificial, SOMA integra un escritorio gráfico TrueColor acelerado en Ring 0, multitarea multiventana, pila de red TCP/IP y almacenamiento persistente.

> **🧪 Fase pre-beta (v0.3.0-prebeta).** El sistema se usa a diario en QEMU y en un ThinkPad T480s real, pero sigue siendo
> experimental: guarda copia de tus datos y no lo instales como único sistema. Los cambios de esta fase están en
> [CHANGELOG.md](CHANGELOG.md). Para informar de un fallo, abre una *issue* con lo que hiciste, el equipo y, si puedes, la
> salida de la consola serie (las líneas `[ESCRITORIO]`, `[WATCHDOG]` o el informe de pánico).

---

## 💽 Novedad de la v0.3: arrancar SOMA desde un pendrive en un PC x86_64

La principal novedad de esta versión es que SOMA arranca **en un PC real desde un pendrive USB**, sin instalar nada en el
disco del ordenador. Está probado a fondo en un **Lenovo ThinkPad T480s** (pantalla, teclado, trackpad, USB, Ethernet, WiFi
Intel 8265, audio, cámara, batería y brillo); en otros PC x86_64 con UEFI debería arrancar, aunque el soporte de su
hardware puede ser parcial.

**Qué necesitas:** un pendrive de **8 GB o más** (32 GB o más para que MODELOS tenga sus 16 GiB), un PC x86_64 con **UEFI** y, para compilar,
**Docker** (Docker Desktop en Windows y Mac) y **Python 3**.

<!-- SOMA_267_DISCOS -->
El pendrive lleva **cuatro particiones** en una tabla **MBR** (las prepara `construyeimagen.py`, en Python, sin herramientas extra):

| Partición | Formato | Etiqueta | Tamaño | Para qué |
|---|---|---|---|---|
| 1 | FAT32 | `SOMA` | 1 GiB | El **sistema**: `EFI/BOOT/BOOTX64.EFI` (GRUB + núcleo + firmware WiFi) e `INFORME.TXT`. Es lo único que cambia al actualizar |
| 2 | propio de SOMA | `DATOS` | 2 GiB | **Configuración, memoria y programas instalados**. No cambia al actualizar. Dentro de SOMA solo la tocan sus programas; desde fuera, `python3 tools/datos.py` (Windows y macOS no la abren: es normal) |
| 3 | exFAT | `MODELOS` | 16 GiB | **Modelos de IA** (`voz/`, `llm/`). En SOMA se ve en `/modelos`, de solo lectura. En pendrives de menos de 32 GB, menos |
| 4 | exFAT | `USUARIO` | el resto | **Tus archivos**: `Documentos`, `Imágenes`, `Audio`, `Vídeos`, `Descargas`, `Proyectos`. En SOMA es `/usuario`; se abre desde cualquier ordenador |

Al grabar, `construyeimagen.py` **mira primero lo que tiene el pendrive**: si ya es de SOMA **solo reescribe el sistema**
(DATOS, MODELOS y USUARIO quedan igual); si es de una versión anterior (SOMA + SOMADATA, con o sin la partición DATOS FAT32 de
macOS, o con el fichero `SOMADATA.IMG`) **conserva todo y lo pasa a cuatro particiones** (lo que cambia de sitio se copia antes
al ordenador; si no cabe, no toca nada); cualquier otro pendrive se prepara de cero pidiendo escribir `BORRAR`. Los modelos de
voz que pongas en la carpeta `modelos/` del proyecto (whisper.cpp `ggml-*.bin`, voces de Piper `.onnx`) se copian a `MODELOS/voz`.

### ⚡ Opción rápida: imagen ya compilada

En la carpeta [`imagen/`](imagen/LEEME.md) del repositorio hay una imagen de SOMA lista para grabar en el pendrive con
**balenaEtcher** o **Rufus**, sin compilar nada; sus instrucciones para Windows, macOS y Linux están en `imagen/LEEME.md`.

Si prefieres compilarla tú, usa `python3 construyeimagen.py`: compila y **graba la imagen en el pendrive** (Linux y macOS)
o **genera la carpeta `imagen/`** (imagen comprimida + instrucciones) para publicarla. También puedes seguir los pasos de abajo.

> ⚠️ Los pasos de abajo **borran el pendrive entero**. Comprueba dos veces que eliges el pendrive y no el disco del ordenador.

### Paso 1 · Compilar el archivo de arranque (igual en los tres sistemas)

Desde la carpeta del repositorio, con Docker abierto:

    docker build --platform linux/amd64 -t soma-toolchain .
    docker run --rm --platform linux/amd64 -v "$PWD":/soma -w /soma soma-toolchain make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS=-DSOMA_WEB_ASYNC_IMAGES=1 t480s-usb

En Windows (PowerShell) cambia `"$PWD"` por `"${PWD}"`; en el `cmd` clásico, por `"%cd%"`.
El resultado es **`build/usb/EFI/BOOT/BOOTX64.EFI`** (unos 5 MB).

### Paso 2 · Preparar el pendrive

#### 🍎 macOS y 🐧 Linux (automático)

    python3 construyeimagen.py --grabar

(o `python3 ejecutar.py -t t480s-usb`, que hace lo mismo). Compila, lista **solo discos externos** y deja el pendrive con las
cuatro particiones; se pedirá tu contraseña (sudo) para escribir en él. Opciones: `--disco disk4` o `--disco /dev/sdb` para no
preguntar, `--formatear` para borrarlo entero aunque ya sea de SOMA, `--temporal CARPETA` si al migrar un pendrive antiguo no hay
sitio en el disco del sistema para la copia temporal.
Si macOS dice que no puede leer un disco, es la partición `DATOS`: pulsa **Ignorar**, nunca «Inicializar».

#### 🪟 Windows

Graba la imagen de la carpeta [`imagen/`](imagen/LEEME.md) con **balenaEtcher** o **Rufus** (modo imagen DD). Para actualizar
después sin perder nada, hazlo desde Linux o macOS con `construyeimagen.py --grabar`.

Un pendrive preparado a mano con las dos particiones antiguas (`SOMA` y `SOMADATA`) sigue arrancando y guardando, pero sin
`/usuario` ni `/modelos`: `construyeimagen.py --grabar` lo pasa a cuatro particiones conservando lo que tenga.

### Paso 3 · Arrancar el PC desde el pendrive

1. En la BIOS/UEFI del PC: **desactiva Secure Boot** (SOMA no está firmado) y deja activado el arranque UEFI desde USB.
   En el ThinkPad T480s: `F1` al encender → *Security → Secure Boot → Disabled*.
2. Enchufa el pendrive y abre el menú de arranque (`F12` en Lenovo; `F11`, `F8` o `Esc` en otras marcas) y elige el USB.
3. En el menú de GRUB elige **SOMA** (o *resolución segura 1024x768* si la pantalla se queda en negro).
4. En el primer arranque SOMA prepara `DATOS` y, si grabaste la imagen del repositorio en un pendrive de más de 8 GB, agranda
   `MODELOS` y `USUARIO` hasta el final mientras están vacías (`soma.agrandar=off` lo evita). Desde ahí lo que guardes se conserva.
5. ¿Tu teclado no es español? Elige **SOMA - otro PC con teclado inglés (US)** en el menú de GRUB.

**Si algo falla:** prueba otro puerto USB, comprueba que Secure Boot está desactivado y que el PC arranca en modo UEFI
(no «Legacy/CSM»).

#### 💾 Si SOMA no guarda la configuración (WiFi, IA, escritorio...)

SOMA guarda su configuración en la partición `DATOS` del pendrive (`SOMADATA` en los pendrives antiguos). Si no la encuentra, trabaja solo en memoria: al arrancar sale un
aviso en el escritorio y *Ajustes → Sistema → SOMADATA* dice el motivo (también la orden `disco`). Lo habitual es un pendrive
preparado con una versión antigua, que solo tiene la unidad *SOMA*. Hay dos arreglos, sin y con borrado:

- **Sin borrar nada:** crea en la unidad *SOMA* un archivo `SOMADATA.IMG` de 64 MB a 4 GB (vacío, en un pendrive con sitio
  libre) y SOMA lo usará como disco desde el siguiente arranque. `ejecutar.py -t t480s-usb` lo ofrece solo. A mano:
  - macOS: `mkfile 2g /Volumes/SOMA/SOMADATA.IMG`
  - Linux: `dd if=/dev/zero of=/media/$USER/SOMA/SOMADATA.IMG bs=1M count=2048 && sync`
  - Windows (como administrador): `fsutil file createnew E:\SOMADATA.IMG 2147483648` (cambia `E:` por la unidad SOMA)
- **Pasándolo a cuatro particiones** con `python3 construyeimagen.py --grabar`: conserva lo que tenga (también ese
  `SOMADATA.IMG`).

#### 📂 Tus archivos, los modelos y DATOS (SOMA_267)

- Dentro de SOMA: `ls /usuario`, `ls /usuario/Documentos`, `cat /usuario/Documentos/nota.txt`, `write`, `cp`, `rm`, `mkdir`,
  `rmdir` funcionan en `/usuario`. `/modelos` se lee pero no se escribe (solo los programas del sistema). La orden `disco`
  dice qué unidades hay, su tamaño y lo libre.
- Los modelos de voz de `MODELOS/voz` se usan **directamente** desde allí (ya no hace falta `voz instalar` ni copiarlos a DATOS).
- Desde fuera, `DATOS` se lee y se edita con `tools/datos.py` (siguiendo los mismos protocolos a prueba de cortes que el núcleo):

        python3 tools/datos.py /dev/sdb listar
        python3 tools/datos.py /dev/sdb leer /etc/hostname
        python3 tools/datos.py /dev/sdb escribir /etc/hostname mi_nombre.txt
        python3 tools/datos.py /dev/sdb exportar copia_de_seguridad

  En macOS, el pendrive es `/dev/rdiskN` (con `sudo`, tras `diskutil unmountDisk`). `exportar` no copia claves ni `.conf` salvo
  con `--con-secretos`. `--crudo build/hdd.img` sirve para el disco de QEMU.

#### 🩺 Diagnóstico del arranque e informe de hardware

SOMA está probado a fondo en el ThinkPad T480s. En otros equipos puede faltar algún driver, y para arreglarlo hace falta
saber qué hardware hay. SOMA ayuda de tres formas:

- **Etapas en pantalla.** El arranque muestra `[ARRANQUE 1/13] ...` hasta `[ARRANQUE 13/13] escritorio`. Si el equipo se
  cuelga, la última línea dice en qué paso.
- **Pantalla de error útil.** Si el núcleo falla durante el arranque, la pantalla dice la etapa y qué opción de GRUB la salta
  (por ejemplo `soma.acpi=off`): en el menú de GRUB pulsa `e`, añádela al final de la línea `multiboot2` y pulsa `F10`.
- **`INFORME.TXT`.** En cada arranque, SOMA escribe un informe en ese archivo de la unidad *SOMA* del pendrive, que se abre
  desde Windows, macOS o Linux. Dice el modelo del equipo, las etapas y sus tiempos, si el arranque anterior se quedó a
  medias y dónde, y la lista de dispositivos PCI con lo que SOMA hace con cada uno (*SI: driver*, *no: aún no*, *no hace
  falta*). No contiene contraseñas ni claves. Compártelo en una *issue*: es lo que permite añadir el driver que falta.
  También se genera con la orden `informe` o en *Ajustes → Sistema → Guardar informe*, y se guarda en `/informe.txt`.

SOMA escribe el informe **en el sitio** del `INFORME.TXT` que ya trae el pendrive (256 KiB, lo crean `construyeimagen.py`
y `ejecutar.py`): no crea archivos ni toca la tabla FAT, así que la partición de arranque no corre ningún riesgo.

---

## 🌌 Entorno de Escritorio Gráfico (Her-Aura Desktop)

SOMA incorpora un servidor visual con doble búfer, recorte por tijera (clipping), optimización por rectángulos de daño y mezcla alfa con soporte para resoluciones HD (1280×720) y Full HD (1920×1080) a 32 bpp TrueColor.

    +------------------------------------------------------------------------+
    |                        SOMA HER-AURA DESKTOP                           |
    |                                                                        |
    |        [Browser]          [Notepad]          [Files]         [Paint]   |
    |             \                 |                 |            /         |
    |              v                 v                 v          v          |
    |        +------------------------------------------------------+        |
    |        |        ANILLO ORBITAL DE ACRECIÓN Y HORIZONTE        |        |
    |        |            * Vórtice Central Cósmico (30 FPS)        |        |
    |        |      (Entrada Reactiva en Vivo / Voz y dictado)      |        |
    |        +------------------------------------------------------+        |
    |              ^                 ^                 ^          ^          |
    |             /                 |                 |           \          |
    |        [Terminal]         [MemView]         [Layout]     [Agentes]     |
    |                                                                        |
    |  soma> analiza el estado del sistema y genera un informe_              |
    +------------------------------------------------------------------------+

- Escritorio Interactivo: Vórtice procedural de partículas a 30 FPS con renderizado de acreción y pulsos luminosos reactivos al razonamiento del LLM.
- Dock Orbital Dinámico: Los iconos orbitan alrededor del vórtice central y pueden ser absorbidos hacia el horizonte de sucesos mediante física en coma fija (16.8).
- Gestor Multiventana Flotante: Mueve, redimensiona interactivamente por los bordes (drag & resize), apila en profundidad Z y conmuta entre ventanas abiertas.
- Motor Tipográfico e Iconográfico Vectorial: Renderizado de curvas de Bézier y máscaras antialiasing de 8 sub-muestras por píxel (formato TrueType TTF sin librerías externas), junto a una colección de iconos vectoriales procedurales.
- Respuestas del orbe como un teleprompter: zona fija bajo el orbe con letra fija (Ajustes → LCM), que sube sola mientras
  se escribe y se recorre moviendo el ratón arriba y abajo o con la rueda; negrita, cursiva, subrayado, código, listas y
  fórmulas (`$...$`). Entre el orbe y la respuesta, una barra de fases con iconos dice qué está haciendo SOMA.

---

## 🖥️ Suite de Aplicaciones Gráficas Nativas

- SOMA Browser: Navegador visual con motor de renderizado HTML/CSS, imágenes locales/remotas BMP/PPM y adaptador para JavaScript reactivo.
- SOMA Paint: Editor bitmap sobre lienzo de 80×60 con lápiz, pincel, figuras geométricas, relleno por inundación (flood fill), paleta y guardado BMP a disco.
- SOMA File Manager: Explorador de archivos para SOMAFS02 con clasificación por tipo, tamaño, metadatos y apertura directa.
- SOMA Notepad: Editor de texto gráfico con seguimiento en vivo de los archivos creados por los agentes y lectura en voz alta.
- SOMA Memory View: Visor en árbol del grafo de conocimiento persistente, la pizarra compartida (Blackboard) y la memoria episódica.
- Maquetador: documentos HTML y LaTeX paginados como en papel (A4, A5, carta, 16:9, email), exportación a PDF/HTML/PNG y revisión con IA (ver *Documentos con acabado profesional*).
- Terminal Gráfica: Consola interactiva con emulación ANSI, historial persistente, autocompletado y tuberías (| , > , && , ||).
- SOMA Studio: editor de código para SOMA-LANG y Python con explorador, verificación, ejecución y peticiones a la IA; pestañas *Crear con IA*, *Proyectos* y *Registro*.
- SOMA Media: reproductor de vídeo AVI (MJPG + PCM) y audio WAV del almacén BIGFS, con biblioteca, visualización del sonido y controles con nombre.

Todas usan los **iconos vectoriales del sistema** (`src/ui/sysicon.h`): se dibujan con antialiasing a cualquier tamaño y se
pueden ver todos en *Ajustes → Iconos*. Para añadir uno: una entrada nueva en `enum si_icon` (antes de `SI_COUNT`), su dibujo en
`si_icon()` sobre una rejilla de 48×48 con las primitivas `L` (trazo), `D` (disco), `O` (aro), `B` (caja redondeada) y `P`
(polígono), y su nombre en la página de iconos de `settings.c`. La prueba `tests/test_iconos_240.c` comprueba que cada icono
pinta algo, cabe en su caja y no repite el dibujo de otro.

---

## 🤖 Jerarquía y Orquestación Multi-Agente

SOMA elige el camino más corto para cada petición (ver *Cómo decide SOMA el camino de una petición*). Agentes:

- soma: orquestador. Responde directamente con una sola llamada al modelo cuando no hacen falta herramientas; si las necesita,
  usa un bucle ReAct (Reason + Act) con herramientas y delegación (como mucho dos niveles, nunca en sí mismo).
- LCM: traductor de frases a órdenes dentro del núcleo, sin el modelo de lenguaje (ver *LCM*); también la charla corta.
- mayordomo: agente operativo determinista y sin modelo: RAM, disco, archivos, carpetas y órdenes del sistema.
- programador: crea y modifica programas SOMA-LANG y Python (compila, comprueba y, con modelos multimodales, revisa la ventana).
- buscador: investigación web (Wikipedia y DuckDuckGo por HTTPS) y búsqueda en la memoria interna (BM25).
- maquetador: documentos con acabado profesional y PDF.
- Pizarra (/agent/blackboard.txt): contexto compartido solo entre los agentes de una misma delegación.

---

## 🌐 Red y Servicios Web en Ring 0

- VirtIO-NET: Controlador de red virtualizado para entornos QEMU (PCI legacy en x86_64, MMIO en AArch64).
- Pila TCP/IP Nativa: Ethernet, ARP dinámico, IPv4, ICMP (Ping), UDP y TCP de flujo confiable con retransmisión por temporizadores.
- Servidor HTTPD: Servidor web integrado en Ring 0 sirviendo un Dashboard interactivo y API REST en el puerto 80 (mapeado a http://localhost:8090/).

---

## 💾 Almacenamiento Persistente (SOMAFS02)

- VirtIO-BLK: Controlador de bloques de bajo nivel sobre disco físico virtual de 16 MiB.
- SOMAFS02: Sistema de archivos con 64 inodos, hasta 16 KiB por archivo, sincronización transaccional (dirty tracking) y partición de fábrica.
- SRCFS: Empaquetador que almacena el código fuente del kernel en sectores reservados para auto-inspección en tiempo de ejecución.
- Singularity Loop: Mecanismo donde el agente puede proponer parches de código C (SEARCH / REPLACE), reiniciar en modo prueba canaria y validar o revertir el parche automáticamente.

---

## 🍓 Raspberry Pi 5 (BCM2712 + RP1)

| Componente | Estado | Probado en placa |
|---|---|---|
| Arranque (EL2 → EL1, MMU y cachés, GIC-400, temporizador 1 kHz) | ✅ | Sí |
| HDMI: consola de texto desde el arranque y escritorio `vsoma` a la resolución del monitor | ✅ | Sí |
| USB (los dos xHCI del RP1): teclado, ratón, hubs, discos USB | ✅ | Sí |
| Ventilador (GPIO45 del RP1) | ✅ (al 100 %, sin regulación) | Sí |
| Tarjeta SD (SDHCI) con FAT16/FAT32 | ✅ | Sí |
| PCIe (Broadcom STB), enlace con el RP1 | ✅ | Sí |
| Ethernet (Cadence GEM del RP1) con negociación del PHY | ✅ en emulador | No |
| GPIO 0..27 (comando `gpio`) | ✅ en emulador | No |
| LED verde de actividad (latido) | ⚠️ dirección sin verificar | No |

Detalles técnicos y la historia de cada corrección en [`docs/ESTABILIDAD.md`](docs/ESTABILIDAD.md) (parches 058-078). Nota sobre el vídeo: el firmware de la Pi 5 da una pantalla de 16 bits (RGB565) y contesta con el modo pedido aunque no lo aplique; SOMA calcula la geometría real con la pitch y el tamaño del buffer.

---

## 🚀 Puesta en Marcha

Requisitos en el equipo anfitrión: Python 3, Docker y QEMU (`qemu-system-x86_64` y/o `qemu-system-aarch64`). La compilación se hace en contenedores Docker (`soma-toolchain`), no hace falta instalar compiladores.

1. Clonar el repositorio:

        git clone https://github.com/mlalejandre/x86-soma.git
        cd x86-soma

2. Elegir destino con el menú o con `-t`:

        python3 ejecutar.py                  # menú interactivo
        python3 ejecutar.py -t x86_64        # PC x86_64 en QEMU (por defecto)
        python3 ejecutar.py -t virt          # ARM64 en QEMU virt

   | Destino | Qué hace |
   |---|---|
   | `x86_64` | Compila y arranca en QEMU x86_64 |
   | `virt` | Compila y arranca en QEMU ARM64 `virt` |
   | `pi5-qemu` | Construye la Pi 5 emulada (imagen Docker, una vez, ~20-40 min) |
   | `pi5` | Arranca en la Pi 5 emulada (USB en PCIe1) |
   | `pi5-rp1` | Pi 5 emulada con RP1 simulado: USB, Ethernet y GPIO como en la placa |
   | `pi5-img` | Solo compila `build/aarch64/kernel_2712.img` |
   | `pi5-sd` | Genera `soma_pi5_sd.img` para una Pi 5 real |

   `-a x86_64|aarch64` sigue funcionando por compatibilidad.

3. Dashboard web (x86_64 y `pi5-rp1`): http://localhost:8090/

4. Controles básicos de la interfaz:
    - Escribir en cualquier momento: el orquestador captura el texto directamente.
    - Tecla X: alterna la visibilidad del dock orbital de iconos.
    - ESC: cierra o minimiza la ventana activa hacia el escritorio cósmico.

### En una Raspberry Pi 5 real

1. Generar la imagen (descarga el DTB oficial de la Pi 5 la primera vez):

        python3 ejecutar.py -t pi5-sd

2. Grabar `soma_pi5_sd.img` en una microSD con Raspberry Pi Imager ("Usar personalizada") o balenaEtcher.
3. Conectar monitor por micro-HDMI, teclado y ratón USB, y encender. El arranque se ve en pantalla.
4. En la consola, `vsoma` abre el escritorio gráfico.

Comandos útiles en la placa:

| Comando | Para qué |
|---|---|
| `vsoma` | Abrir el escritorio gráfico |
| `hdmi` / `hdmi swap` | Ver el modo de pantalla / intercambiar rojo y azul |
| `usb`, `blk`, `fat ls sd0` | Dispositivos USB, discos y ficheros de la SD |
| `fan on`, `fan off`, `fan inv` | Ventilador |
| `placa` | Modelo, memoria y revisión de la Raspberry |
| `wifi prueba` | Wi-Fi (fase 1, en pruebas): enciende el chip, lo identifica y carga su firmware desde `/wifi` de la SD |
| `wifi scan`, `wifi conectar <red> <clave>` | Redes Wi-Fi cercanas y conexión WPA2 (escrito, pendiente de probar en placa) |
| `dhcp` | Pide dirección IP al router |
| `cpus`, `cpus prueba` | Núcleos del procesador y prueba de cálculo en paralelo |
| `netif`, `netif usar wlan0` | Interfaces de red (Ethernet, Wi-Fi) y cambio entre ellas |
| `wpa`, `wpa psk <ssid> <clave>` | Autoprueba de la criptografía WPA2 y clave de una red |
| `eth`, `ping 10.0.2.2` | Ethernet (verificada en emulador) |
| `gpio`, `gpio N out`, `gpio N 1` | Pines GPIO del RP1 |
| `pci`, `threads` | Bus PCIe y planificador |

Consola serie opcional: conector UART de depuración, 115200 8N1.

### Pi 5 emulada (sin placa)

    python3 ejecutar.py -t pi5-qemu      # una vez
    python3 ejecutar.py -t pi5-rp1

La pantalla se abre en el navegador (http://localhost:6080/) y la consola serie queda en el terminal (salir: Ctrl+A y X). Variables: `SOMA_PI5_RAM`, `SOMA_PI5_CPUS`, `SOMA_PI5_GUI=0`, `SOMA_PI5_USB=0|kbd`, `SOMA_PI5_NET=0`.

---

## 📂 Estructura del Proyecto

    soma/
    ├── Dockerfile          # Toolchain de compilación cruzada reproducible
    ├── Makefile            # Reglas de construcción para x86_64 y AArch64
    ├── ejecutar.py         # Runner maestro con soporte multi-arquitectura
    ├── grub/               # Configuración de arranque GRUB Multiboot
    ├── src/
    │   ├── arch/           # HAL: arranque, IDT/GIC, MMU, PMM; aarch64: PCIe, RP1, xHCI, GEM, SDHCI, HDMI
    │   ├── core/           # Kernel, planificador KThreads, mutex, heap
    │   ├── drivers/        # VirtIO-NET, VirtIO-BLK, Framebuffer BGA, Ratón
    │   ├── fs/             # SOMAFS02, VFS virtual, BM25 y SRCFS
    │   ├── net/            # Pila IP, TCP, UDP, ARP, DNS, cliente HTTP y HTTPD
    │   ├── ui/             # Escritorio Her-Aura, compositor 2D y consola
    │   ├── apps/           # Browser, Paint, Notepad, File Manager, MemView, Layout
    │   ├── fonts/          # Rasterizador vectorial TrueType e iconos
    │   └── agent/          # Agentes ReAct, integración LLM, shell y LCM (lcm_*.inc)
    ├── tools/pi5/          # Pi 5 emulada (QEMU raspi5b + RP1 simulado) y generador de la SD
    ├── tools/lcm_ordenes/  # LCM: entrenar, medir y exportar el traductor
    ├── tools/exporta_para_claude.py   # .txt para preparar un parche con Claude
    ├── tests/              # Pruebas en el host (ASAN/UBSAN) de las capas puras
    └── docs/               # ESTABILIDAD.md (historial de parches), navegador, diseño

---

## 🐍 Python en SOMA

SOMA trae **Python 3** (MicroPython 1.30, en `third_party/micropython`, licencia MIT) como un programa más de ring 3: un error
o un bucle infinito en un script nunca tumba el sistema. Solo en x86_64 (en aarch64 aún no hay procesos de usuario).

```
python                      # modo interactivo (>>>); exit() para salir
python hola.py uno dos      # ejecuta /workspace/hola.py con sys.argv = ['hola.py', 'uno', 'dos']
python -c "print(2**100)"   # una línea de código
```

En el **Terminal** del escritorio la salida aparece al momento, lo que escribes va a `input()` y **Esc** detiene el script.

**Ventanas, ratón y teclado** con `soma.win`, la misma ventana que usan los programas SOMA-LANG:

```python
from soma import win
import time

win.create("Hola", 320, 200)
n = 0
while win.active():                      # False cuando se cierra (también con Esc)
    m = win.mouse()                      # m.x, m.y, m.clicked, m.button, m.inside
    if m.clicked: n += 1
    if win.key() == win.UP: n += 10      # win.UP/DOWN/LEFT/RIGHT, 10 Intro, 32 espacio, ord('a')...
    win.clear(0x101828)
    win.text(20, 30, "Clics: " + str(n), 0xFFFFFF)
    win.rect(20, 80, 120, 40, 0x2563EB); win.circle(220, 100, 30, win.rgb(220, 38, 38))
    win.present()
    time.sleep_ms(16)
```

También `win.line`, `win.pixel`, `win.size()` y `win.close()`. **SOMA Studio → Crear con IA** puede escribir el programa
en Python (botón *Python* junto a *Lenguaje*, o «en Python» en la descripción): el programador lo comprueba con el
intérprete de verdad antes de guardarlo y *Probar* lo lanza en el Terminal. Lo mismo con la orden `programa archivo.py ...`.

Qué hay: enteros de precisión ilimitada, `float` doble, cadenas, listas, diccionarios, clases, excepciones, generadores,
`import` de tus `.py` de `/workspace`, `open()` (texto; lee el VFS y escribe solo en `/workspace`), `input()`, y los módulos
`math`, `cmath`, `random`, `json`, `re`, `time`, `struct`, `collections`, `heapq`, `binascii`, `array`, `io`, `sys`, `gc`,
`errno` y `soma` (`leer`, `escribir`, `existe`, `reloj`, `ms`, `dormir`, `win`). No hay `os`, red ni hilos; las rutas privadas
(`/etc`, `/agent`, `/mem`...) no se pueden leer. Memoria del intérprete: 6 MiB. Plazo: 10 minutos por script (8 horas en modo
interactivo). La prueba automática está en `tests/prueba_python_237.py`.

<!-- SOMA_265_CC -->
## ⚙️ C en SOMA (TinyCC)

SOMA trae un **compilador de C**: TinyCC (en `third_party/tinycc`, licencia LGPL 2.1) con una biblioteca de C propia, como un
programa más de ring 3, igual que Python. Solo en x86_64.

```
cc hola.c                   # compila /workspace/hola.c -> /workspace/hola.elf
cc -run hola.c uno dos      # compila y ejecuta con argumentos
cc -run hola.elf            # ejecuta uno ya compilado
cc a.c b.c -o juego.elf     # varios archivos (-c, -w, -Dnombre=valor, -Iruta, -heap MiB)
```

Ventanas con `#include <soma.h>` (`soma_win_create`, `soma_win_clear`, `soma_win_rect`, `soma_win_text`, `soma_win_mouse`,
`soma_win_key`, `soma_win_present`...), la misma ventana que SOMA-LANG y Python. La biblioteca tiene `stdio` (con archivos del
VFS: se escribe solo en `/workspace`), `stdlib`, `string`, `ctype`, `math`, `time`, `setjmp`, `wchar` y atómicos; `printf` y
`strtod` dan exactamente lo mismo que glibc. Cada programa tiene 12 MiB (8 de memoria dinámica y 1 de pila). En el Terminal,
**Esc** lo para. **SOMA Studio → Crear con IA** también escribe C (botón *C*, o «en C» en la descripción) y lo comprueba con el
compilador de verdad. Detalles en `docs/PARCHE_265_CC.md`; pruebas: `make pruebas-cc` y `tests/prueba_cc_265.py` (QEMU).

## 🎙️ Voz a texto (whisper.cpp)

SOMA transcribe audio en español **sin conexión**, con [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT) como
proceso de usuario con AVX2. Solo en x86_64 (el T480s y QEMU con `-cpu max`).

1. **Descarga el modelo** `ggml-base-q5_1.bin` (60 MB, multilingüe) de <https://huggingface.co/ggerganov/whisper.cpp>.
   Más preciso pero unas 3 veces más lento: `ggml-small-q5_1.bin` (181 MB). Los `.en` solo entienden inglés.
2. **Cópialo al pendrive**, en una carpeta `MODELOS` de la unidad *SOMA* (o deja el archivo en la carpeta `modelos/` del
   proyecto y `ejecutar.py -t t480s-usb` lo copia solo).
3. **En SOMA**, una sola vez: `voz instalar` (lo copia al almacén, en `/modelos`).
4. **Transcribe**:
   - en la *Grabadora*: elige una grabación y pulsa **Transcribir** (o la tecla `T`). El texto sale en pantalla, se puede
     copiar y se guarda junto al audio (`/grabaciones/grab_N.txt`);
   - en el terminal: `voz ultima` (la última grabación) o `voz /grabaciones/grab_3.wav`. Opciones: `-T` marcas de tiempo,
     `-l en` otro idioma, `-C` ventana completa de 30 s (algo más preciso en audios cortos, más lento), `-v` detalles.

     `-b 1` decodificación voraz (más rápida; por defecto búsqueda en haz de 5), `-V` sin recortar silencios.

**Dictado en todo el sistema (pulsar para hablar):** mantén pulsada la tecla **Alt** izquierda mientras hablas y suéltala al
terminar. El texto se escribe donde esté el cursor, igual que si lo tecleases: el orbe, el terminal, las notas, el editor de
programas o cualquier campo de texto. En el orbe, además, se envía a SOMA. Mientras se graba aparece abajo "Escuchando" con el
nivel del micrófono y, al soltar, "Transcribiendo...".
- Un toque corto de Alt sigue abriendo el orbe; Alt + otra tecla es un atajo normal (no se graba nada).
- **Esc** cancela mientras graba. Máximo 2 minutos por dictado.

**Elegir el modelo (Ajustes > Voz, o `voz modelo rapido|preciso|maximo`):**

| Nivel | Archivo (whisper.cpp) | Tamaño | Velocidad |
|---|---|---|---|
| Rápido | `ggml-base-q5_1.bin` | ~60 MB | la de siempre |
| Preciso (recomendado) | `ggml-small-q5_1.bin` | ~190 MB | ~3 veces más lento |
| Máxima precisión | `ggml-large-v3-turbo-q5_0.bin` | ~575 MB | ~8-10 veces más lento |

Se descargan de `huggingface.co/ggerganov/whisper.cpp` y se copian en la carpeta `MODELOS` del pendrive, junto al de
siempre. En Ajustes > Voz se ve cuál está instalado o en el pendrive; **Instalar desde el pendrive** lo copia al almacén (si
no, se instala solo la primera vez que se dicta). Si el elegido no está, se usa el instalado más parecido. La elección se
guarda en `/etc/voz.conf`. El nivel se deduce del nombre (base/tiny, small/medium, large/turbo), así que sirven otras
variantes (`q5_0`, `q8_0`...).

**Contra las alucinaciones de whisper** (inventar frases con silencio o ruido, "Subtítulos por la comunidad de Amara.org",
"Gracias por ver", bucles que repiten lo mismo):
- un detector de voz por energía con umbral adaptado al ruido: sin voz no se reconoce nada, y con voz se recortan los silencios
  del principio y del final y se acortan las pausas largas;
- decodificación conservadora: búsqueda en haz, sin texto anterior como contexto, sin tokens de "no habla" (música, aplausos),
  y los umbrales de OpenAI para repetir con más temperatura o descartar un tramo sin voz;
- cada tramo se descarta si es probable que no haya voz y su confianza es baja, si es una frase típica de subtítulos, o si es
  una frase corta dudosa ("Gracias.", "Música."); se quitan las anotaciones (`[Música]`, `(risas)`, ♪) y las repeticiones en bucle.
- contra los bucles de repetición (la misma frase escrita varias veces): un bloque de 4 o más palabras repetido seguido se deja
  una sola vez, también entre tramos y con una copia a medias al final; hay un tope de tokens según lo que dura el audio; y si el
  resultado huele a bucle (repetía, o más de 5,5 palabras por segundo), se vuelve a reconocer con la ventana de audio completa,
  que es donde whisper entra menos en bucle.

El reconocimiento usa varios núcleos (hasta 4 hilos de cálculo en los núcleos libres). Con el modelo base, 7 segundos de voz se transcriben en ~1,5 s con un hilo en el ordenador de pruebas; con 4 hilos, bastante menos. Acepta WAV PCM de
8/16/24/32 bits o float, mono o estéreo, a cualquier frecuencia (se pasa a 16 kHz con un filtro sinc). En QEMU funciona pero
es muy lento, porque emula AVX2: para probar en el ordenador está `make voz-host ARCH=x86_64` (ver
`third_party/whisper/LEEME_SOMA.md`).

## 🔊 Texto a voz (voces de Piper)

**Notas → Leer en voz alta** lee el documento (o lo seleccionado) con una voz neuronal, sin conexión. El botón pasa a
**Parar lectura** mientras habla. También desde el terminal: `habla Hola, ¿qué tal?`, `habla parar`, `habla` (estado).

1. Descarga una voz de [Piper](https://huggingface.co/rhasspy/piper-voices/tree/main/es): los **dos** archivos, el `.onnx` y
   su `.onnx.json`. Para español de España, `es_ES-davefx-medium` (hombre, ~63 MB); de América, `es_MX-claude-high` o
   `es_AR-daniela-high` (mujer). Las calidades `x_low`, `low`, `medium` y `high` funcionan; `medium` es el mejor equilibrio.
2. Cópialos en la carpeta `MODELOS` del pendrive (junto al modelo de whisper). La primera vez que se lee algo, la voz se copia
   sola a `/voces` del almacén (o ahora con `habla instalar`).

Cómo funciona:
- **Sin onnxruntime**: SOMA implementa la red VITS de Piper (codificador de texto con atención relativa, duraciones
  estocásticas, flujo y generador HiFi-GAN) leyendo los pesos directamente del `.onnx`. Va en el mismo programa que `voz`
  (`src/user/voz/habla*.{h,cpp}`) y usa los núcleos libres. En el ordenador de pruebas, 3 s de voz se calculan en 0,6 s con
  un núcleo; la frase siguiente se calcula mientras suena la anterior.
- **Pronunciación propia** (`habla_g2p_es.h`): el texto en español pasa a fonemas con reglas (números a palabras, c/z, ll,
  g/j, r/rr, diptongos e hiatos, acento, alófonos de b/d/g, siglas...), imitando a espeak-ng, que es con lo que se entrenan
  las voces (espeak-ng no se usa: es GPL). En un corpus de 380 frases coincide en el 95 % de las palabras. Con voces de
  América (`es-419`, `es-mx`...) aplica seseo y yeísmo.
- Con una voz de otro idioma lee igualmente, pero con la pronunciación española.

**El orbe habla** (por defecto): las respuestas del orbe se leen en voz alta con la voz elegida. Pulsar **Alt** (para dictar) o
hacer otra pregunta corta la lectura. Al leer se quitan los adornos de markdown, el código, las rutas largas (de
`/workspace/docs/informe.pdf` se lee «informe») y los enlaces («el enlace»). Todo se ajusta en **Ajustes → Voz → Lectura en voz
alta**: activar o desactivar, **voz** (lista desplegable con todas las de `/voces`: copia más en `MODELOS` del pendrive y pulsa
**Instalar del pendrive**; se instalan todas las que falten), **velocidad** y, en voces con varios hablantes, cuál. **Probar voz**
lee una frase de ejemplo. Se guarda en `/etc/habla.conf`.

**Ajustes → Sonido**: volumen de la salida (el mismo que las teclas del portátil), silencio y **Probar sonido**; micrófono
(entrada en una lista desplegable y ganancia). Se guarda en `/etc/sonido.conf` y se aplica al arrancar. También `ajustes sonido`.


## 📰 Documentos con acabado profesional (Maquetador)

Pide a SOMA un documento con formato y lo redacta, lo **pagina como en papel**, lo **revisa** y lo deja en **PDF**:

```
documento Redacta un informe trimestral de ventas con gráficos, en PDF
documento prepara una presentación de 8 diapositivas sobre energía solar para el comité
soma redacta un paper sobre redes neuronales con ecuaciones (en LaTeX)
```

También desde el orbe ("redacta un informe... con formato profesional") o desde el **Maquetador**: escribe la petición abajo y
pulsa **Generar** (trabaja en el hilo del agente; el orbe enseña el progreso). Tipos: **informe**, **paper** (HTML o LaTeX),
**revista**, **presentación 16:9**, **carta formal**, **currículum**, **email** y **libro A5**. Se guardan en `/workspace/docs/`
(la fuente `.html`/`.tex` y el `.pdf`).

Cómo funciona:
- **El modelo escribe HTML con la hoja de estilo de la casa** (`dg_house_css`: tipografía en puntos, portada, resumen,
  indicadores, tablas con cifras alineadas, recuadros, columnas, gráficos SVG, diapositivas, email, carta, libro) y elige el
  color cambiando seis variables. Para papers con matemáticas escribe LaTeX (motor `tex.c`).
- **Paginación de verdad** (`src/apps/docgen.c`): formatos A4, A4 apaisado, A5, carta, 16:9 y email con
  `<meta name="soma-pagina" content="A4; margen=20; pie={titulo} · {n}; cabecera=...">`. Los cortes se buscan en blanco entre
  bloques, sin dejar un título solo al pie; `.salto-pagina` y cada `.diapositiva` fuerzan página. Texto justificado de verdad.
- **Revisión automática**: lo que se sale de la página, diapositivas desbordadas, imágenes que faltan, última página casi vacía.
- **Revisión de director de arte con IA**: si el modelo es **multimodal** (ve imágenes), recibe las páginas renderizadas tal y
  como saldrán en el PDF y devuelve el documento corregido; si no, revisa con el informe de la maquetación. Hasta dos rondas;
  la corrección solo se acepta si no empeora. El informe queda en `/agent/doc_revision.txt`. Funciona con los cuatro
  proveedores (local con `mmproj`, ChatGPT, Claude y Gemini); si el modelo no admite imágenes, SOMA lo recuerda.
- **Exportar**: PDF (páginas a 192 ppp, 1920×1080 en presentaciones; se abre en cualquier lector y en el de SOMA), **HTML
  autónomo** (con la hoja de estilo incrustada y `@page`, se ve igual en cualquier navegador) y **PNG** por página:
  `documento exportar <ruta> pdf|html|png`, o los botones **PDF** y **HTML** del Maquetador. **Revisar IA** revisa el
  documento abierto (guarda la versión anterior como `-ant`).
- **Programas también**: cuando el programador termina una aplicación gráfica y el modelo ve imágenes, SOMA la abre, captura
  su ventana y un "diseñador de interfaces" la critica; si propone cambios, el programador hace una ronda de mejora (solo se
  queda si compila). La captura queda en `/agent/coder_captura.png`.

<!-- SOMA_262_PUBLICAR: LCM y llm-limites -->
## 🗣️ LCM: SOMA entiende lo que le pides (sin el modelo de lenguaje)

El **LCM** es un traductor pequeño que vive dentro del núcleo: convierte lo que dices o escribes al orbe en una orden del terminal,
en milisegundos, sin conexión y sin gastar el modelo de lenguaje. Es una red BiLSTM en enteros (dos modelos que votan) entrenada
para SOMA; el entrenamiento y las medidas están en `tools/lcm_ordenes/` y en [github.com/mlalejandre/LCM](https://github.com/mlalejandre/LCM).

```
soma borra el archivo notas.txt                     → rm notas.txt (pregunta antes)
soma ¿serías tan amable de decirme qué archivos contienen la palabra casa?   → contiene casa
soma cierra el navegador                            → cerrar navegador
soma qué hora es                                    → «Son las cinco y diez.»
soma hola, ¿cómo estás?                             → charla, sin el modelo de lenguaje
```

- **98 órdenes** y muchas formas de pedirlas (directas, indirectas o educadas). En un examen a ciegas: 79 % exactas,
  2 % equivocadas y ninguna peligrosa. Lo que **borra, mueve, mata o apaga siempre pregunta** antes («sí» / «no»).
- **Lo que no entiende sigue su camino de siempre** (mayordomo o modelo de lenguaje); si duda, también.
- **Respuestas para la voz**: una frase natural («Son las cinco y diez») en vez de la salida del terminal; se puede
  cambiar a técnicas.
- **Aprende de tus correcciones**: si se equivoca, dile «no era eso» y escríbele la orden buena.
- **Ajustes → LCM**: encender o apagar, voz natural o técnica, umbral de seguridad, probar una frase sin ejecutarla, lo aprendido
  y la letra de las respuestas del orbe. En el terminal: `lcm <frase>` (qué haría, sin ejecutar), `lcm estado`, `lcm apagar`.
- <!-- SOMA_266_AVISO --> **Avisa antes de usar el modelo de lenguaje**: si la petición va al modelo (una pregunta, un programa, un
  documento, una búsqueda), el LCM dice al momento lo que se va a hacer y cuánto suele tardar, con lo que tardaron las anteriores
  del mismo tipo («Voy a programarlo en C con el modelo de IA: primero planifico el programa, después escribo el código y lo
  compruebo, y al final lo pruebo y corrijo lo que falle. Suele tardar unos 2 minutos, ten un poco de paciencia»). Sale en el orbe,
  en la conversación, en el terminal y en voz alta si el orbe lee sus respuestas; la respuesta de verdad lo sustituye.
  Se apaga en **Ajustes → LCM**, donde también se ve lo que suele tardar cada tipo.

## 📏 Modelos con contexto grande: `llm-limites`

Por defecto SOMA habla con el modelo con tamaños prudentes (prompt del agente de 16 KB, de cada orden el modelo ve 450
caracteres). Si tu modelo admite un contexto grande, díselo y SOMA lo aprovecha:

```
llm-limites contexto 250000     la ventana de contexto de tu modelo (con llama.cpp, como mucho el -c del servidor)
llm-limites salida 16000        cuánto puede escribir de una vez (no pases del máximo del modelo)
llm-limites                     enseña los tamaños
llm-limites como-antes          vuelve a los de siempre
```

Con 250 000 tokens el agente le manda hasta 700 KB de prompt y 29 000 caracteres de cada orden ejecutada. Las respuestas finales
pueden ocupar 384 KB, y si algo no cabe termina con «[... respuesta recortada ...]» en vez de cortarse en silencio. El maquetador
y el programador también lo usan. Se guarda en `/etc/llm_limites.conf`.

## ⚡ Cómo decide SOMA el camino de una petición

1. **Documento con formato** (PDF, maquetar, presentación, revista, paper...) → Maquetador.
2. **Programa** (crea/haz + app, programa, juego, calculadora...) → Programador.
3. **Una orden o una charla corta dicha con palabras** («borra notas.txt», «qué hora es», «hola») → **LCM**, sin el
   modelo de lenguaje (ver *LCM*). Lo que borra, mueve o apaga pregunta antes; si duda, sigue al paso siguiente.
4. **Operación del sistema** (RAM, disco, archivos, carpetas...) → Mayordomo, sin el modelo.
5. **Conversación, redacción, explicación, traducción** → **respuesta directa**: una sola llamada al modelo.
6. **Lo demás** (internet, datos actuales, archivos, ejecutar) → agente con herramientas (`search` busca en Wikipedia y DuckDuckGo).

Las palabras se comparan completas, sin mayúsculas ni tildes. Si pides guardar («guárdalo», «en notas.txt»), SOMA guarda el
texto ya escrito. `traza` enseña el tiempo y los tokens de las últimas llamadas al modelo (`/agent/traza.txt`): si algo tarda,
ahí se ve si es el modelo (y si razona en oculto) o el camino.

Mientras trabaja, el orbe enseña una **barra de fases** con iconos (LCM, mayordomo, búsqueda, documento, programador,
orden, pensando, escribiendo, voz, hecho): se ve por qué camino ha ido cada petición.

## 🧾 Exportar el proyecto en texto

`exportar_txt.py` genera archivos `.txt` con el código del proyecto, listos para pegarlos en una conversación con un asistente de IA
(cada archivo entre marcas con su ruta, sus líneas y su huella; nunca binarios ni claves de API, que se sustituyen por `[CLAVE OCULTA]`).
El proyecto completo son unos 6 MB de código, así que lo práctico es ir por partes:

```
python3 exportar_txt.py --indice              # mapa del proyecto (~30 000 tokens): qué hay en cada archivo
python3 exportar_txt.py --tema escritorio     # un tema completo (python3 exportar_txt.py --temas los lista)
python3 exportar_txt.py src/ui/gui.c src/apps/settings.c     # archivos o carpetas concretos
python3 exportar_txt.py                       # todo el código propio, en partes de ~400 KB
python3 exportar_txt.py --cambiados           # solo lo que cambió desde la última exportación completa
```

Los archivos quedan en `exportacion/` (no se sube al repositorio).

Para preparar un parche con Claude hay otro exportador, más corto y centrado en una conversación:

```
python3 tools/exporta_para_claude.py                 # lo cambiado desde el último commit
python3 tools/exporta_para_claude.py --tema lcm      # un tema (--temas los lista: lcm, voz, orbe, llm, maquetador...)
python3 tools/exporta_para_claude.py --funcion lang_compile src/ui/gui.c
```

Crea `para_claude_FECHA.txt` con el estado del repositorio, los parches aplicados y los archivos pedidos (sin claves ni binarios).

## 📄 Licencia

Distribuido bajo licencia MIT. Consulta el archivo LICENSE para más detalles.

## 👤 Autor

SOMA está diseñado y desarrollado por **Marcos Luis Álvarez Alejandre**.

- Correo: mlalejandre@gmail.com
- YouTube: [Mente de Von Neumann](https://youtube.com/@mentedevonneumann)
- GitHub: [mlalejandre/x86-soma](https://github.com/mlalejandre/x86-soma)

Estos datos también aparecen dentro del sistema, en **Ajustes → Acerca de**.
