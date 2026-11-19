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

**Qué necesitas:** un pendrive de **8 GB o más** (se borra entero), un PC x86_64 con **UEFI** y, para compilar,
**Docker** (Docker Desktop en Windows y Mac) y **Python 3**.

El pendrive lleva dos particiones en una tabla **MBR**:

| Partición | Formato | Etiqueta | Tamaño | Para qué |
|---|---|---|---|---|
| 1 | FAT32 | `SOMA` | 1 GB | El arranque: `EFI/BOOT/BOOTX64.EFI` (GRUB + núcleo + firmware WiFi en un solo archivo) |
| 2 | FAT32 | `SOMADATA` | 4 GB | El disco de SOMA. Al primer arranque SOMA la reclama y la formatea con su propio sistema de archivos |

> ⚠️ Los pasos de abajo **borran el pendrive entero**. Comprueba dos veces que eliges el pendrive y no el disco del ordenador.

### Paso 1 · Compilar el archivo de arranque (igual en los tres sistemas)

Desde la carpeta del repositorio, con Docker abierto:

    docker build --platform linux/amd64 -t soma-toolchain .
    docker run --rm --platform linux/amd64 -v "$PWD":/soma -w /soma soma-toolchain make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS=-DSOMA_WEB_ASYNC_IMAGES=1 t480s-usb

En Windows (PowerShell) cambia `"$PWD"` por `"${PWD}"`; en el `cmd` clásico, por `"%cd%"`.
El resultado es **`build/usb/EFI/BOOT/BOOTX64.EFI`** (unos 5 MB).

### Paso 2 · Preparar el pendrive

#### 🍎 macOS (automático)

    python3 ejecutar.py -t t480s-usb

Compila, lista **solo discos externos**, pide escribir `BORRAR` y deja el pendrive listo (incluida una tercera partición
`DATOS` para ti). Si el pendrive ya estaba preparado, solo copia el arranque y **conserva tus archivos de SOMA**.
Si macOS dice que no puede leer un disco, es `SOMADATA`: pulsa **Ignorar**, nunca «Inicializar».

#### 🐧 Linux

1. Localiza el pendrive (fíjate en el tamaño y en `TRAN usb`):

        lsblk -o NAME,SIZE,TRAN,MODEL

2. Particiona y formatea (cambia `/dev/sdX` por tu pendrive, **sin número al final**):

        sudo umount /dev/sdX?* 2>/dev/null
        sudo parted -s /dev/sdX mklabel msdos \
             mkpart primary fat32 1MiB 1GiB \
             mkpart primary fat32 1GiB 5GiB
        sudo mkfs.vfat -F 32 -n SOMA     /dev/sdX1
        sudo mkfs.vfat -F 32 -n SOMADATA /dev/sdX2

3. Copia el arranque:

        sudo mkdir -p /mnt/soma && sudo mount /dev/sdX1 /mnt/soma
        sudo cp -r build/usb/EFI /mnt/soma/
        sudo umount /mnt/soma && sync

#### 🪟 Windows

1. Abre **PowerShell como administrador** y ejecuta `diskpart`.
2. Dentro de `diskpart` (cambia `N` por el número de tu pendrive; compruébalo por el tamaño en `list disk`):

        list disk
        select disk N
        clean
        convert mbr
        create partition primary size=1024
        format fs=fat32 quick label=SOMA
        assign letter=S
        create partition primary size=4096
        format fs=fat32 quick label=SOMADATA
        exit

3. Copia la carpeta `EFI` a la raíz de la unidad `S:`:

        Copy-Item -Recurse build\usb\EFI S:\

   Si Windows ofrece «formatear» la segunda unidad tras el primer arranque de SOMA, pulsa **Cancelar**.

En los tres casos el pendrive debe quedar así: `S:\EFI\BOOT\BOOTX64.EFI` (o `/EFI/BOOT/BOOTX64.EFI`) en la partición `SOMA`.

### Paso 3 · Arrancar el PC desde el pendrive

1. En la BIOS/UEFI del PC: **desactiva Secure Boot** (SOMA no está firmado) y deja activado el arranque UEFI desde USB.
   En el ThinkPad T480s: `F1` al encender → *Security → Secure Boot → Disabled*.
2. Enchufa el pendrive y abre el menú de arranque (`F12` en Lenovo; `F11`, `F8` o `Esc` en otras marcas) y elige el USB.
3. En el menú de GRUB elige **SOMA** (o *resolución segura 1024x768* si la pantalla se queda en negro).
4. El primer arranque reclama `SOMADATA`: a partir de ahí lo que guardes en SOMA se conserva entre arranques.

**Si algo falla:** prueba otro puerto USB, comprueba que Secure Boot está desactivado y que el PC arranca en modo UEFI
(no «Legacy/CSM»). Para informar de un fallo abre una *issue* con la marca y el modelo del PC.

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
    |        |     (Entrada Reactiva en Vivo / Streaming de LLM)    |        |
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

---

## 🖥️ Suite de Aplicaciones Gráficas Nativas

- SOMA Browser: Navegador visual con motor de renderizado HTML/CSS, imágenes locales/remotas BMP/PPM y adaptador para JavaScript reactivo.
- SOMA Paint: Editor bitmap sobre lienzo de 80×60 con lápiz, pincel, figuras geométricas, relleno por inundación (flood fill), paleta y guardado BMP a disco.
- SOMA File Manager: Explorador de archivos para SOMAFS02 con clasificación por tipo, tamaño, metadatos y apertura directa.
- SOMA Notepad: Editor de texto gráfico con seguimiento y volcado en vivo (live streaming) de los archivos creados por los agentes.
- SOMA Memory View: Visor en árbol del grafo de conocimiento persistente, la pizarra compartida (Blackboard) y la memoria episódica.
- Document Layout: Motor de maquetación editorial para previsualizar documentos estructurados bajo el estándar #SOMA-LAYOUT v1.
- Terminal Gráfica: Consola interactiva con emulación ANSI, historial persistente, autocompletado y tuberías (| , > , && , ||).

---

## 🤖 Jerarquía y Orquestación Multi-Agente

SOMA ejecuta un bucle cognitivo ReAct (Reason + Act) con agentes especializados:

- soma: Orquestador superior. Descompone objetivos complejos, delega tareas especializadas y sintetiza dictámenes finales.
- mayordomo: Agente operativo determinista y de baja latencia. Ejecuta consultas de memoria, operaciones de VFS, búsquedas y archivos sin consumir tokens LLM.
- buscador: Especialista en investigación externa web (DuckDuckGo / proxy HTTP) y recuperación semántica interna mediante algoritmo BM25 / TF-IDF.
- maquetador: Especialista editorial. Transforma síntesis e informes en documentos maquetados con jerarquía visual estructurada.
- Blackboard IPC: Pizarra persistente compartida (/agent/blackboard.txt) para consenso e intercambio de contexto entre agentes.

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
    │   └── agent/          # Agentes ReAct, integración LLM y shell
    ├── tools/pi5/          # Pi 5 emulada (QEMU raspi5b + RP1 simulado) y generador de la SD
    ├── tests/              # Pruebas en el host (ASAN/UBSAN) de las capas puras
    └── docs/               # ESTABILIDAD.md (historial de parches), navegador, diseño

---

## 📄 Licencia

Distribuido bajo licencia MIT. Consulta el archivo LICENSE para más detalles.
