ARCH ?= x86_64
# SOMA_328_COMPILA_RAPIDO: "make" a secas (o all, t480s-usb) usa todos los procesadores si no se ha dado -j. Las pruebas (test, ci,
# fuzz...) siguen en serie salvo que se pida -j: algunas abren puertos y no deben correr a la vez. Con -j1 se desactiva.
SOMA_NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
ifeq ($(filter-out all t480s t480s-usb docker-make,$(or $(MAKECMDGOALS),all)),)
MAKEFLAGS += -j$(SOMA_NPROC)
endif
endif
# SOMA_197: el nucleo hace lo que cualquier nucleo (reinterpreta bufferes de red y de disco como estructuras, hace aritmetica que
# da la vuelta, toca direcciones bajas). Sin estas opciones el optimizador puede dar por imposible ese codigo y eliminarlo o
# reordenarlo: fallos que solo aparecen al cambiar de compilador o de nivel de optimizacion. Son las mismas que usa Linux.
SAFE_CFLAGS = -fno-strict-aliasing -fno-delete-null-pointer-checks -fwrapv

ifeq ($(ARCH),x86_64)
	CC = gcc
	LD = ld
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -m64 -march=x86-64 -ffreestanding -O2 -Wall -Wextra $(if $(WERROR),-Werror) -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mgeneral-regs-only -fno-omit-frame-pointer $(SAFE_CFLAGS)
else
	CC = aarch64-linux-gnu-gcc
	LD = aarch64-linux-gnu-ld
	OBJCOPY_A64 = aarch64-linux-gnu-objcopy
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -march=armv8-a -ffreestanding -O2 -Wall -Wextra $(if $(WERROR),-Werror) -fno-stack-protector -fno-pie -fno-pic -mgeneral-regs-only -mstrict-align -mno-outline-atomics -fno-omit-frame-pointer $(SAFE_CFLAGS)
endif

# SOMA_152: compilar en macOS. El "gcc" de macOS es Apple clang para el propio Mac (en Apple Silicon, arm64): no acepta
# -march=x86-64 ni genera ELF freestanding ("clang: error: unsupported argument 'x86-64' to option '-march='").
#   a) toolchain cruzado de Homebrew:  brew install x86_64-elf-gcc x86_64-elf-binutils x86_64-elf-grub xorriso
#      -> se detecta solo (x86_64-elf-gcc / x86_64-elf-ld / x86_64-elf-grub-mkrescue).
#   b) sin instalar nada mas que Docker:  make docker-make ARCH=x86_64 PLATFORM=qemu_virt EXTRA_CFLAGS='...'
GRUB_MKRESCUE ?= grub-mkrescue
GRUB_FILE ?= grub-file
SOMA_HOST_OS := $(shell uname -s 2>/dev/null)
ifeq ($(SOMA_HOST_OS),Darwin)
ifeq ($(origin CC),file)
ifeq ($(ARCH),x86_64)
ifneq ($(shell command -v x86_64-elf-gcc 2>/dev/null),)
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
else
SOMA_MAC_NO_TOOLCHAIN = 1
endif
endif
endif
ifneq ($(shell command -v x86_64-elf-grub-mkrescue 2>/dev/null),)
GRUB_MKRESCUE = x86_64-elf-grub-mkrescue
GRUB_FILE = x86_64-elf-grub-file
else ifneq ($(shell command -v i686-elf-grub-mkrescue 2>/dev/null),)
GRUB_MKRESCUE = i686-elf-grub-mkrescue
GRUB_FILE = i686-elf-grub-file
endif
endif
ifdef SOMA_MAC_NO_TOOLCHAIN
# SOMA_263_ARREGLO: + pruebas-lcm (pruebas del anfitrion: no necesitan el compilador cruzado; make test la llama). SOMA_265_CC: + pruebas-cc
ifneq ($(filter-out clean test ci audit fuzz fuzz-% docker-make docker-image web152-test browser-boundary-test pruebas-lcm pruebas-lang pruebas-cc pruebas-js pruebas-memoria pruebas-chat pruebas-discos run-t480s-uefi run-t480s-bios size-check,$(or $(MAKECMDGOALS),all)),)
$(error macOS: el compilador del sistema (Apple clang) no puede generar el nucleo x86_64. Opcion 1: brew install x86_64-elf-gcc x86_64-elf-binutils x86_64-elf-grub xorriso  (y repetir make). Opcion 2 (solo Docker): make docker-make ARCH=$(ARCH) PLATFORM=$(PLATFORM) EXTRA_CFLAGS='$(EXTRA_CFLAGS)')
endif
endif
DOCKER_IMAGE ?= soma-toolchain
docker-image:
	@if docker image inspect $(DOCKER_IMAGE) >/dev/null 2>&1 && docker run --rm --platform linux/amd64 $(DOCKER_IMAGE) test -d /usr/lib/grub/x86_64-efi; then :; \
	 else echo "Construyendo $(DOCKER_IMAGE) (con GRUB para UEFI)"; docker build --platform linux/amd64 -t $(DOCKER_IMAGE) .; fi
docker-make: docker-image
	docker run --rm --platform linux/amd64 -u $$(id -u):$$(id -g) -v "$(CURDIR)":/soma -w /soma $(DOCKER_IMAGE) make ARCH=$(ARCH) PLATFORM=$(PLATFORM) EXTRA_CFLAGS='$(EXTRA_CFLAGS)' $(DOCKER_TARGET)
.PHONY: docker-image docker-make

# SOMA_143A_BROWSER_NET: prueba opt-in del transporte aislado (NO aisla HTML/CSS/JS).
BROWSER_NET_RING3 ?= 0
ifneq ($(filter $(BROWSER_NET_RING3),0 1),$(BROWSER_NET_RING3))
$(error BROWSER_NET_RING3 debe ser 0 o 1)
endif
ifneq ($(ARCH),x86_64)
ifeq ($(BROWSER_NET_RING3),1)
$(error BROWSER_NET_RING3=1 requiere x86_64)
endif
endif
CFLAGS += -DSOMA_BROWSER_NET_RING3=$(BROWSER_NET_RING3)

BUILD = build/$(ARCH)
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS += -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/$(ARCH)

OBJS_ARCH = \
	$(BUILD)/boot.o \
	$(BUILD)/switch.o \
	$(BUILD)/idt.o \
	$(BUILD)/pci.o \
	$(BUILD)/rtc.o \
	$(BUILD)/sysinfo.o \
	$(BUILD)/pmm.o \
	$(BUILD)/vmm.o \
	$(BUILD)/usermode.o \
	$(BUILD)/hda.o \
	$(BUILD)/smp.o \
	$(BUILD)/hwmon.o

ifeq ($(ARCH),aarch64)
OBJS_ARCH += $(BUILD)/rpi_fb.o $(BUILD)/mmu.o $(BUILD)/gic.o $(BUILD)/sdhci.o $(BUILD)/blk_glue.o $(BUILD)/pcie.o $(BUILD)/rp1.o $(BUILD)/rp1_gpio.o $(BUILD)/gem.o $(BUILD)/xhci.o $(BUILD)/xhci2.o $(BUILD)/wifi.o      # PARCHE 060 / 061 / 062 / 077 / 079
endif
ifeq ($(ARCH),x86_64)
OBJS_ARCH += $(BUILD)/ssp_init.o $(BUILD)/usermode_tests.o $(BUILD)/apic.o $(BUILD)/ahci.o $(BUILD)/nvme.o $(BUILD)/blk_glue.o $(BUILD)/xhci.o      # PARCHE 037 / 040 / 043 / 045 / 046
OBJS_ARCH += $(BUILD)/bootfb.o      # SOMA_154_T480S: Multiboot 2, pantalla del firmware, consola de texto
OBJS_ARCH += $(BUILD)/i8042.o       # SOMA_155_T480S: teclado y trackpad PS/2 (controlador i8042, Synaptics)
OBJS_ARCH += $(BUILD)/hwscan.o      # SOMA_156_T480S: inventario del hardware (orden hw), tablas del firmware sobre 1 GiB
OBJS_ARCH += $(BUILD)/bootdiag.o    # SOMA_228_ARRANQUE: etapas del arranque, arranque anterior fallido e informe de hardware
OBJS_ARCH += $(BUILD)/usbdisk.o     # SOMA_162_T480S: disco del sistema en la particion SOMADATA del pendrive
OBJS_ARCH += $(BUILD)/cpupower.o    # SOMA_166_T480S: energia, temperatura, HWP y reposo MWAIT
OBJS_ARCH += $(BUILD)/syn_gest.o    # SOMA_167_T480S: gestos del trackpad Synaptics (modo absoluto)
OBJS_ARCH += $(BUILD)/backlight.o   # SOMA_168_T480S: brillo del panel (PWM del chipset) y teclas del ThinkPad
OBJS_ARCH += $(BUILD)/e1000e.o $(BUILD)/netmgr.o   # SOMA_170_T480S: Ethernet Intel (I219) y gestor de red (DHCP automatico)
OBJS_ARCH += $(BUILD)/iwl.o $(BUILD)/iwl_fw.o      # SOMA_175A_T480S: WiFi Intel 8265 (firmware como modulo de GRUB)
OBJS_ARCH += $(BUILD)/memprot.o     # SOMA_192: W^X en todo el espacio del nucleo y pagina 0 protegida
# SOMA_164_T480S: interprete ACPI (uACPI, third_party/uacpi, MIT) y su capa de adaptacion (acpi_sys.c)
UACPI_DEFS = -Ithird_party/uacpi/include -DUACPI_USE_BUILTIN_STRING -DUACPI_NATIVE_ALLOC_ZEROED -DUACPI_SIZED_FREES
UACPI_OBJS = $(patsubst third_party/uacpi/source/%.c,$(BUILD)/uacpi_%.o,$(wildcard third_party/uacpi/source/*.c))
OBJS_ARCH += $(BUILD)/acpi_sys.o $(UACPI_OBJS)
endif

OBJS_CORE = \
	$(BUILD)/thread.o \
	$(BUILD)/mutex.o \
	$(BUILD)/panic.o \
	$(BUILD)/kernel.o \
	$(BUILD)/mem.o \
	$(BUILD)/ramstat.o \
	$(BUILD)/console.o \
	$(BUILD)/virtio_net.o \
	$(BUILD)/net.o \
	$(BUILD)/netif.o \
	$(BUILD)/usbnet_proto.o \
	$(BUILD)/dhcp.o \
	$(BUILD)/dhcp_proto.o \
	$(BUILD)/ip.o \
	$(BUILD)/udp.o \
	$(BUILD)/dns.o \
	$(BUILD)/tcp.o \
	$(BUILD)/http.o \
	$(BUILD)/llm.o \
	$(BUILD)/fs.o \
	$(BUILD)/virtio_blk.o \
	$(BUILD)/srcfs.o \
	$(BUILD)/bigfs.o \
	$(BUILD)/bigfs_glue.o \
	$(BUILD)/boot_gate.o \
	$(BUILD)/nano.o \
	$(BUILD)/agent.o \
	$(BUILD)/agent_net.o \
	$(BUILD)/llm_proto.o \
	$(BUILD)/docagent.o $(BUILD)/docgen.o $(BUILD)/pdfw.o \
	$(BUILD)/agent_ruta.o $(BUILD)/busca_web.o $(BUILD)/inv_tema.o $(BUILD)/web_lee.o $(BUILD)/web_aislado.o $(BUILD)/prog_voz.o \
	$(BUILD)/memoria.o $(BUILD)/memoria_core.o \
	$(BUILD)/shell.o \
	$(BUILD)/httpd.o \
	$(BUILD)/fb.o \
	$(BUILD)/gui_font.o $(BUILD)/soma_fonts.o \
	$(BUILD)/mouse.o \
	$(BUILD)/gui.o \
	$(BUILD)/browser.o $(BUILD)/web_visor.o \
	$(BUILD)/image.o \
	$(BUILD)/imgview.o \
	$(BUILD)/fm.o \
	$(BUILD)/notepad.o \
	$(BUILD)/memview.o \
	$(BUILD)/chat.o $(BUILD)/chat_core.o $(BUILD)/chat_md.o \
	$(BUILD)/correo.o $(BUILD)/correo_core.o $(BUILD)/correo_red.o $(BUILD)/correo_sbx.o $(BUILD)/tun_tls.o \
	$(BUILD)/layout.o $(BUILD)/tex.o $(BUILD)/settings.o $(BUILD)/savedlg.o $(BUILD)/codeedit.o $(BUILD)/textfield.o $(BUILD)/coder_core.o $(BUILD)/coder_demo.o $(BUILD)/pdf_sbx.o $(BUILD)/pdfview.o $(BUILD)/sysmon.o $(BUILD)/zona.o $(BUILD)/hora.o $(BUILD)/noticias_rss.o $(BUILD)/noticias.o $(BUILD)/noticias_gui.o $(BUILD)/agentes_reg.o $(BUILD)/agentes_gui.o $(BUILD)/discos_core.o $(BUILD)/discos.o $(BUILD)/discos_gui.o $(BUILD)/agenda_core.o $(BUILD)/agenda.o $(BUILD)/agenda_gui.o $(BUILD)/recorder.o $(BUILD)/camapp.o $(BUILD)/capapp.o $(BUILD)/sysicon.o \
	$(BUILD)/paint.o $(BUILD)/paint_svg.o \
	$(BUILD)/coder_app.o $(BUILD)/coder_run.o \
	$(BUILD)/vm.o \
	$(BUILD)/lexer.o \
	$(BUILD)/lang.o

.DEFAULT_GOAL := all

# SOMA_PARCHE_016_EXEC: programas de usuario (src/user/*.c) -> ELF -> objeto enlazado en el kernel (solo x86_64)
ifeq ($(ARCH),x86_64)
USER_PROGS = hello fault ticker sumfile wavplay ssetest fbtest sysfuzz      # se enlazan en el kernel y se copian a /bin/ (caben en el VFS de 16 KiB)
USER_PROGS_BIG = bigprog vidplay                    # SOMA_PARCHE_019_FILES: demasiado grandes para el VFS; se construyen aparte y viven en el almacen BIGFS
# SOMA_328_COMPILA_RAPIDO: los programas de ring 3 se compilan en paralelo y solo lo que cambio (tools/lote_cc.py)
# HEADERS va aqui arriba: las listas de requisitos se expanden al leer cada regla, y websvc.elf (mas abajo) la usaba antes de
# definirla, asi que le llegaba vacia y el navegador aislado no se recompilaba al cambiar una cabecera (lo tapaba el borrado de
# build/ que hacia ejecutar.py en cada ciclo). Los programas de ring 3 que compila tools/lote_cc.py vuelven a enlazarse si cambia
# cualquier cabecera; el lote ya sabe cuales de sus archivos la incluyen.
HEADERS = $(wildcard src/*.h) $(wildcard src/*/*.h) $(wildcard src/cursors/*.h) $(wildcard src/fonts/*.h) $(wildcard src/icons/*.h) $(wildcard src/arch/$(ARCH)/*.h)
HEADERS_R3 = $(HEADERS) $(wildcard src/*/*/*.h) $(wildcard src/*/*.inc) $(wildcard src/*/*/*.inc)
LOTE = python3 tools/lote_cc.py
USER_CFLAGS = -m64 -march=x86-64 -ffreestanding -O2 -Wall -Wextra $(SAFE_CFLAGS) -fno-stack-protector -fPIE -mno-red-zone -mgeneral-regs-only -nostdlib -static -Isrc/user -Isrc/core
OBJS_ARCH += $(BUILD)/userprogs.o

# SOMA_PARCHE_021_FPU: ssetest usa SSE2/x87: sin -mgeneral-regs-only (el resto de programas sigue sin SSE)
$(BUILD)/user/ssetest.elf: USER_CFLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -msse2 -mfpmath=sse -fno-tree-vectorize

$(BUILD)/user/%.elf: src/user/%.c src/user/ulib.h src/user/user.ld $(wildcard src/core/wav.*) $(wildcard src/core/jpeg.*) $(wildcard src/core/avi.*) | $(BUILD)
	mkdir -p $(BUILD)/user
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $< -o $@

all: $(foreach p,$(USER_PROGS_BIG),$(BUILD)/user/$(p).elf)

$(BUILD)/userprogs.o: $(foreach p,$(USER_PROGS),$(BUILD)/user/$(p).elf)
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../userprogs.o $(foreach p,$(USER_PROGS),$(p).elf)
# PARCHE 138: servicios aislados (solo SYS_EXIT; ver src/core/sbx_abi.h). Se enlazan en el kernel, no se instalan en /bin.
OBJS_ARCH += $(BUILD)/sandbox.o $(BUILD)/sandbox_test.o $(BUILD)/fwcfg.o $(BUILD)/sbxprogs.o
# SOMA_314_PROGZ: los programas grandes de ring 3 van comprimidos en la imagen (tools/progz.py) y se descomprimen al lanzarlos
OBJS_ARCH += $(BUILD)/progz.o $(BUILD)/websvcprog.o
$(BUILD)/user/%.elf.z: $(BUILD)/user/%.elf tools/progz.py
	python3 tools/progz.py $< $@
.SECONDARY: $(BUILD)/user/websvc.elf.z $(BUILD)/user/vplay.elf.z $(BUILD)/user/python.elf.z $(BUILD)/user/jsrun.elf.z $(BUILD)/user/cc.elf.z $(BUILD)/user/voz.elf.z $(BUILD)/user/ia.elf.z
IMGDEC_SRCS = src/user/imgdec.c src/apps/web_img.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c src/apps/bmp_dec.c
$(BUILD)/user/imgdec.elf: $(IMGDEC_SRCS) src/apps/simplewebp.h src/apps/web_svg.inc src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld $(wildcard src/apps/web_*.h) src/apps/image.h src/core/jpeg.h | $(BUILD)
	mkdir -p $(BUILD)/user/imgdec
	+@$(LOTE) $(BUILD)/user/imgdec imgdec_srcs - base $(CC) $(USER_CFLAGS) -Isrc/apps ::: $(IMGDEC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/imgdec/*.o -o $@
$(BUILD)/user/sbxevil.elf: src/user/sbxrt.h src/core/sbx_abi.h
ZDEC_SRCS = src/user/zdec.c src/apps/web_inflate.c       # PARCHE 139
$(BUILD)/user/zdec.elf: $(ZDEC_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld src/apps/web_inflate.h src/apps/web_util.h | $(BUILD)
	mkdir -p $(BUILD)/user/zdec
	+@$(LOTE) $(BUILD)/user/zdec zdec_srcs - base $(CC) $(USER_CFLAGS) -Isrc/apps ::: $(ZDEC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/zdec/*.o -o $@
PDFSVC_SRCS = src/user/pdfsvc.c src/apps/pdf.c src/apps/web_img.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c src/apps/bmp_dec.c   # PARCHE 141
$(BUILD)/user/pdfsvc.elf: $(PDFSVC_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld src/apps/pdf.h $(wildcard src/apps/web_*.h) | $(BUILD)
	mkdir -p $(BUILD)/user/pdfsvc
	+@$(LOTE) $(BUILD)/user/pdfsvc pdfsvc_srcs - base $(CC) $(USER_CFLAGS) -DWI_NO_WEB_FORMATS -Isrc/apps ::: $(PDFSVC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/pdfsvc/*.o -o $@
NETSVC_SRCS = src/user/netsvc.c src/user/unet.c src/apps/web_net.c src/apps/web_inflate.c   # PARCHE 142
NETSVC_SRCS += src/core/tls13.c src/core/tlscrypto.c src/core/crypto.c src/core/x509.c src/core/tls_roots.c   # SOMA_316_RED_AISLADA: TLS fuera del nucleo
$(BUILD)/user/netsvc.elf: $(NETSVC_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld src/apps/web_net.h src/arch/x86_64/usermode.h src/core/tls13.h src/core/x509.h src/core/tlscrypto.h src/core/tls_roots.h | $(BUILD)
	mkdir -p $(BUILD)/user/netsvc
	+@$(LOTE) $(BUILD)/user/netsvc netsvc_srcs - base $(CC) $(USER_CFLAGS) -DSOMA_NETSVC=1 -Isrc/apps -Isrc/net -Isrc/arch/x86_64 ::: $(NETSVC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/netsvc/*.o -o $@
# SOMA_205: el proceso de ring 3 no tiene hilos del nucleo: la descarga asincrona de imagenes/scripts (SOMA_WEB_ASYNC_IMAGES=1, la que usa ejecutar.py)
# se fuerza a 0 aqui; en el proceso las descargas son sincronas. El motor que sigue en el nucleo conserva la bandera.
# SOMA_202: el NAVEGADOR AISLADO: el motor web entero como proceso de ring 3 (src/user/websvc.c, src/core/web_r3_abi.h).
WEBSVC_SRCS = src/user/websvc.c src/user/unet.c src/apps/web_page.c src/apps/web_html.c src/apps/web_css.c src/apps/web_layout.c $(wildcard src/apps/web_js_*.c) \
	src/apps/web_f64.c src/apps/web_regex.c src/apps/web_font.c src/apps/web_net.c src/apps/web_inflate.c src/apps/web_img.c src/apps/web_jpeg.c src/core/jpeg.c \
	src/apps/bmp_dec.c src/drivers/fb.c src/ui/gui_font.c src/ui/soma_fonts.c src/core/tls13.c src/core/tlscrypto.c src/core/crypto.c src/core/x509.c src/core/tls_roots.c
WEBSVC_INC = -Isrc/apps -Isrc/net -Isrc/arch/x86_64 -Isrc/drivers -Isrc/ui -Isrc/fs -Isrc/fonts -Isrc/icons -Isrc/lang -Isrc/agent -Isrc
$(BUILD)/user/websvc.elf: $(WEBSVC_SRCS) $(HEADERS) src/user/user.ld $(wildcard src/apps/*.ttf) $(wildcard src/apps/*.inc) | $(BUILD)
	mkdir -p $(BUILD)/user/websvc
	+@$(LOTE) $(BUILD)/user/websvc websvc_srcs - base $(CC) $(USER_CFLAGS) $(EXTRA_CFLAGS) -w -USOMA_WEB_ASYNC_IMAGES -DSOMA_WEB_ASYNC_IMAGES=0 -DSOMA_WEBSVC=1 $(WEBSVC_INC) ::: $(WEBSVC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/websvc/*.o -o $@
OBJS_ARCH += $(BUILD)/web_r3.o
# SOMA_311_CORREO_AISLADO: mimesvc, el lector de correo aislado (el mismo correo_core.c que el nucleo usa para IMAP y SMTP)
MIMESVC_SRCS = src/user/mimesvc.c src/apps/correo_core.c
$(BUILD)/user/mimesvc.elf: $(MIMESVC_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld src/apps/correo_core.h | $(BUILD)
	mkdir -p $(BUILD)/user/mimesvc
	+@$(LOTE) $(BUILD)/user/mimesvc mimesvc_srcs - base $(CC) $(USER_CFLAGS) -Isrc/apps ::: $(MIMESVC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/mimesvc/*.o -o $@
# SOMA_312_WEB_AISLADA: leesvc, el lector aislado de lo que el agente trae de Internet
LEESVC_SRCS = src/user/leesvc.c src/agent/web_lee.c src/agent/busca_web.c src/apps/noticias_rss.c   # SOMA_337_NOTICIAS: + canales RSS
$(BUILD)/user/leesvc.elf: $(LEESVC_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/user/user.ld src/agent/web_lee.h src/agent/busca_web.h src/apps/yt_inner.h src/apps/yt_proto.h src/apps/noticias_rss.h | $(BUILD)
	mkdir -p $(BUILD)/user/leesvc
	+@$(LOTE) $(BUILD)/user/leesvc leesvc_srcs - base $(CC) $(USER_CFLAGS) -Isrc/agent ::: $(LEESVC_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/leesvc/*.o -o $@
# SOMA_326_TUNEL_TLS: tlstun, el tunel TLS de ring 3 (una conexion por proceso) para el correo y YouTube nativo
TLSTUN_SRCS = src/user/tlstun.c src/user/unet.c src/core/tls13.c src/core/tlscrypto.c src/core/crypto.c src/core/x509.c src/core/tls_roots.c
$(BUILD)/user/tlstun.elf: $(TLSTUN_SRCS) src/user/sbxrt.h src/core/sbx_abi.h src/core/tun_abi.h src/user/user.ld src/arch/x86_64/usermode.h src/core/tls13.h src/core/x509.h src/core/tlscrypto.h src/core/tls_roots.h | $(BUILD)
	mkdir -p $(BUILD)/user/tlstun
	+@$(LOTE) $(BUILD)/user/tlstun tlstun_srcs - base $(CC) $(USER_CFLAGS) -DSOMA_NETSVC=1 -Isrc/net -Isrc/arch/x86_64 ::: $(TLSTUN_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none $(BUILD)/user/tlstun/*.o -o $@
$(BUILD)/user/imgdec.elf $(BUILD)/user/zdec.elf $(BUILD)/user/pdfsvc.elf $(BUILD)/user/netsvc.elf $(BUILD)/user/mimesvc.elf: $(HEADERS_R3)
$(BUILD)/user/leesvc.elf $(BUILD)/user/tlstun.elf $(BUILD)/user/jsrun.elf: $(HEADERS_R3)
$(BUILD)/sbxprogs.o: $(BUILD)/user/imgdec.elf $(BUILD)/user/sbxevil.elf $(BUILD)/user/zdec.elf $(BUILD)/user/pdfsvc.elf $(BUILD)/user/netsvc.elf $(BUILD)/user/mimesvc.elf $(BUILD)/user/leesvc.elf $(BUILD)/user/tlstun.elf
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../sbxprogs.o imgdec.elf sbxevil.elf zdec.elf pdfsvc.elf netsvc.elf mimesvc.elf leesvc.elf tlstun.elf
$(BUILD)/websvcprog.o: $(BUILD)/user/websvc.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../websvcprog.o websvc.elf.z
$(BUILD)/sandbox_test.o: src/arch/x86_64/sbx_testimg.h
# SOMA_237_PYTHON: interprete de Python (MicroPython, third_party/micropython, MIT) como programa de ring 3 con SSE (como ssetest).
# Va dentro del nucleo (orden "python"), no en /bin. Sin -Isrc/core: ulib.h debe ver el errno.h de la biblioteca del sistema.
PY_DIR = third_party/micropython
PY_SRCS = $(wildcard $(PY_DIR)/py/*.c) $(wildcard $(PY_DIR)/extmod/*.c) $(PY_DIR)/shared/runtime/gchelper_generic.c $(PY_DIR)/libc/string0.c
PY_LIBM = $(wildcard $(PY_DIR)/libm_dbl/*.c)
PY_CFLAGS = $(filter-out -mgeneral-regs-only -Isrc/core,$(USER_CFLAGS)) -msse2 -mfpmath=sse -fno-tree-vectorize -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
	-DNDEBUG -Isrc/user -isystem src/user/python -isystem $(PY_DIR)
$(BUILD)/user/python.elf: $(PY_SRCS) $(PY_LIBM) src/user/python.c src/user/python/mpconfigport.h src/user/ulib.h src/user/user.ld \
		$(wildcard $(PY_DIR)/py/*.h) $(wildcard $(PY_DIR)/genhdr/*.h) $(wildcard $(PY_DIR)/lib/re1.5/*) $(wildcard $(PY_DIR)/libm_dbl/*.h) $(wildcard $(PY_DIR)/libm_dbl/include/*.h) | $(BUILD)
	mkdir -p $(BUILD)/user/python
	+@$(LOTE) $(BUILD)/user/python py_srcs - base $(CC) $(PY_CFLAGS) -w ::: $(PY_SRCS)
	+@$(LOTE) $(BUILD)/user/python m m_ base $(CC) $(PY_CFLAGS) -w -I$(PY_DIR)/libm_dbl/include ::: $(PY_LIBM)
	$(CC) $(PY_CFLAGS) -c src/user/python.c -o $(BUILD)/user/python/soma_python.o
	$(CC) $(PY_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/python/*.o -lgcc -o $@
# SOMA_252_VIDEO_NATIVO: reproductor de video nativo (MP4: H.264 + AAC) como proceso de ring 3. Decodificadores de terceros con
# licencia Apache 2.0: third_party/libavc (H.264, el de Android) y third_party/libxaac (AAC). Va dentro del nucleo (vplay_sys.c).
VP_AVC = third_party/libavc
VP_XAAC = third_party/libxaac
VP_AVC_SRCS = $(wildcard $(VP_AVC)/common/*.c) $(wildcard $(VP_AVC)/decoder/*.c)
VP_XAAC_SRCS = $(wildcard $(VP_XAAC)/common/*.c) $(wildcard $(VP_XAAC)/decoder/*.c) $(wildcard $(VP_XAAC)/decoder/drc_src/*.c) $(wildcard $(VP_XAAC)/decoder/generic/*.c)
VP_CFLAGS = $(filter-out -mgeneral-regs-only -Isrc/core -Wall -Wextra,$(USER_CFLAGS)) -msse2 -mfpmath=sse -fno-math-errno -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DNDEBUG
VP_AVC_INC = -I$(VP_AVC)/common -I$(VP_AVC)/decoder -DX86
VP_XAAC_INC = -I$(VP_XAAC)/common -I$(VP_XAAC)/decoder -I$(VP_XAAC)/decoder/drc_src
$(BUILD)/user/vplay.elf: $(VP_AVC_SRCS) $(VP_XAAC_SRCS) $(wildcard $(VP_AVC)/*/*.h) $(wildcard $(VP_XAAC)/*/*.h) $(wildcard $(VP_XAAC)/decoder/*/*.h) $(wildcard src/user/vplay/*) \
		src/user/ulib.h src/user/user.ld $(PY_LIBM) | $(BUILD)
	mkdir -p $(BUILD)/user/vplay
	+@$(LOTE) $(BUILD)/user/vplay avc avc_ base $(CC) $(VP_CFLAGS) $(VP_AVC_INC) -w ::: $(VP_AVC_SRCS)
	+@$(LOTE) $(BUILD)/user/vplay xa xa_ base $(CC) $(VP_CFLAGS) $(VP_XAAC_INC) -w -Wno-incompatible-pointer-types ::: $(VP_XAAC_SRCS)   # SOMA_300_CI: libxaac mezcla WORD16/WORD32 en punteros a funcion; gcc 14 lo da como error aunque haya -w
	+@$(LOTE) $(BUILD)/user/vplay m m_ base $(CC) $(VP_CFLAGS) -w -I$(PY_DIR)/libm_dbl/include ::: $(PY_LIBM)
	$(CC) $(VP_CFLAGS) -Wall -Wextra $(VP_AVC_INC) -c src/user/vplay/vp_vdec.c -o $(BUILD)/user/vplay/vp_vdec.o
	$(CC) $(VP_CFLAGS) -Wall -Wextra $(VP_XAAC_INC) -c src/user/vplay/vp_adec.c -o $(BUILD)/user/vplay/vp_adec.o
	$(CC) $(VP_CFLAGS) -Wall -Wextra -Werror -c src/user/vplay/vp_core.c -o $(BUILD)/user/vplay/vp_core.o
	$(CC) $(VP_CFLAGS) -Wall -Wextra -Werror -fno-builtin -c src/user/vplay/vp_libc.c -o $(BUILD)/user/vplay/vp_libc.o
	$(CC) $(VP_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/vplay/*.o -lgcc -o $@
$(BUILD)/vplayprog.o: $(BUILD)/user/vplay.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../vplayprog.o vplay.elf.z
OBJS_ARCH += $(BUILD)/vplayprog.o $(BUILD)/vplay_sys.o
$(BUILD)/pyprog.o: $(BUILD)/user/python.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../pyprog.o python.elf.z
OBJS_ARCH += $(BUILD)/pyprog.o
# SOMA_274_JS: JavaScript para aplicaciones: el motor del navegador (src/apps/web_js_*.c) sin pagina, como programa de ring 3 (orden "js").
# Sin red ni maquetacion: solo el interprete, el DOM minimo (document vacio), console, fs, win, temporizadores y promesas.
JSRUN_SRCS = src/user/jsrun.c src/apps/jsrun_core.c src/apps/web_html.c src/apps/web_css.c $(wildcard src/apps/web_js_*.c) src/apps/web_f64.c src/apps/web_regex.c
$(BUILD)/user/jsrun.elf: $(JSRUN_SRCS) src/apps/jsrun_core.h src/apps/web_js.h src/apps/web_js_i.h src/user/ulib.h src/user/user.ld $(wildcard src/apps/*.h) | $(BUILD)
	mkdir -p $(BUILD)/user/jsrun
	+@$(LOTE) $(BUILD)/user/jsrun jsrun_srcs - base $(CC) $(USER_CFLAGS) $(EXTRA_CFLAGS) -w -USOMA_WEB_ASYNC_IMAGES -DSOMA_WEB_ASYNC_IMAGES=0 -DSOMA_WEBSVC=1 $(WEBSVC_INC) -Isrc/user ::: $(JSRUN_SRCS)
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/jsrun/*.o -o $@
$(BUILD)/jsprog.o: $(BUILD)/user/jsrun.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../jsprog.o jsrun.elf.z
OBJS_ARCH += $(BUILD)/jsprog.o
# SOMA_265_CC: compilador de C (TinyCC, third_party/tinycc, LGPL 2.1) con la biblioteca de C de SOMA (src/user/cc) como programa
# de ring 3 (orden "cc"), como Python. Lleva dentro sus cabeceras y la biblioteca, que compila con cada programa (cc_fs.inc).
TCC_DIR = third_party/tinycc
CCC_DIR = src/user/cc
CCC_CFLAGS = $(filter-out -mgeneral-regs-only -Isrc/core,$(USER_CFLAGS)) -msse2 -mfpmath=sse -fno-tree-vectorize -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
	-isystem $(CCC_DIR)/include -I$(CCC_DIR) -I$(TCC_DIR) -I$(BUILD)/user/cc
TCC_DEFS = -DONE_SOURCE=1 -DTCC_TARGET_X86_64 -DCONFIG_TCC_NO_NATIVE -DCONFIG_TCC_STATIC -DCONFIG_TCC_BACKTRACE=0 -DCONFIG_TCC_BCHECK=0 \
	-DCONFIG_TCC_SEMLOCK=0 -DCONFIG_TCC_PREDEFS=1
$(BUILD)/user/cc.elf: $(wildcard $(TCC_DIR)/*.c $(TCC_DIR)/*.h $(TCC_DIR)/*.def $(TCC_DIR)/include/*.h $(TCC_DIR)/lib/*) \
		$(wildcard $(CCC_DIR)/*.c $(CCC_DIR)/*.h $(CCC_DIR)/*.py $(CCC_DIR)/include/*.h $(CCC_DIR)/include/sys/*.h) src/user/user.ld | $(BUILD)
	mkdir -p $(BUILD)/user/cc
	python3 $(CCC_DIR)/cc_fs.py . $(BUILD)/user/cc/cc_fs.inc
	$(CC) $(CCC_CFLAGS) $(TCC_DEFS) -w -c $(TCC_DIR)/libtcc.c -o $(BUILD)/user/cc/libtcc.o
	$(CC) $(CCC_CFLAGS) "-DSOMA_HEAP_SIZE=(10u << 20)" -c $(CCC_DIR)/libc.c -o $(BUILD)/user/cc/libc.o
	$(CC) $(CCC_CFLAGS) -c $(CCC_DIR)/cc.c -o $(BUILD)/user/cc/cc.o
	$(CC) $(CCC_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/cc/*.o -lgcc -o $@
$(BUILD)/ccprog.o: $(BUILD)/user/cc.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../ccprog.o cc.elf.z
OBJS_ARCH += $(BUILD)/ccprog.o
# SOMA_241_VOZ: voz a texto con whisper.cpp (third_party/whisper, MIT) como programa de ring 3 con AVX2 (orden "voz").
# C++17 sin excepciones ni RTTI, con las cabeceras de libstdc++ y una libc propia (src/user/voz/voz_libc.c). De libstdc++.a solo
# se enlazan los arboles de std::map y la politica de las tablas hash (src/user/voz/cxxinc desactiva las extern template).
VOZ_W = third_party/whisper
VOZ_SRCS = $(shell find $(VOZ_W) -name '*.c' -o -name '*.cpp' | grep -v aneforge | sort)
VOZ_BASE = -m64 -O2 $(SAFE_CFLAGS) -fno-stack-protector -fPIE -mno-red-zone -nostdlib -static -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-math-errno -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DNDEBUG -msse4.2 -mf16c -mfma -mbmi2 -mavx -mavx2
VOZ_DEFS = -D_GNU_SOURCE -DGGML_SOMA -DGGML_USE_CPU -DGGML_USE_CPU_REPACK -DGGML_SCHED_MAX_COPIES=4 -DGGML_AVX -DGGML_AVX2 -DGGML_BMI2 \
	-DGGML_F16C -DGGML_FMA -DGGML_SSE42 '-DWHISPER_VERSION="1.9.6-dev"'
VOZ_INC = -Isrc/user/voz/cxxinc -Isrc/user/voz -I$(VOZ_W)/include -I$(VOZ_W)/ggml/include -I$(VOZ_W)/ggml/src -I$(VOZ_W)/ggml/src/ggml-cpu -I$(VOZ_W)/src
VOZ_CXXF = $(VOZ_BASE) $(VOZ_DEFS) $(VOZ_INC) -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics
VOZ_CF = $(VOZ_BASE) $(VOZ_DEFS) $(VOZ_INC) -std=gnu11
VOZ_LIBSTDCXX = $(shell $(CXX) -print-file-name=libstdc++.a)
ifeq ($(shell command -v $(CXX) 2>/dev/null),)
$(error Falta el compilador de C++ ($(CXX)), necesario desde el parche 241 (voz a texto). Con Docker: borra la imagen vieja con "docker rmi soma-toolchain" y vuelve a ejecutar ejecutar.py (la reconstruye con g++). En Debian/Ubuntu: sudo apt install g++)
endif
$(BUILD)/user/voz.elf: $(VOZ_SRCS) $(shell find $(VOZ_W) -name '*.h') $(wildcard src/user/voz/*) src/user/voz/cxxinc/bits/c++config.h $(PY_LIBM) | $(BUILD)
	mkdir -p $(BUILD)/user/voz
	+@$(LOTE) $(BUILD)/user/voz voz_cpp - ruta $(CXX) $(VOZ_CXXF) -w ::: $(filter %.cpp,$(VOZ_SRCS))
	+@$(LOTE) $(BUILD)/user/voz voz_c - ruta $(CC) $(VOZ_CF) -w ::: $(filter-out %.cpp,$(VOZ_SRCS))
	+@$(LOTE) $(BUILD)/user/voz m m_ base $(CC) $(VOZ_BASE) -w -I$(PY_DIR)/libm_dbl/include ::: $(PY_LIBM)
	$(CXX) $(VOZ_CXXF) -Wall -Wextra -c src/user/voz/voz.cpp -o $(BUILD)/user/voz/voz.o
	$(CXX) $(VOZ_CXXF) -Wall -Wextra -c src/user/voz/voz_cxx.cpp -o $(BUILD)/user/voz/voz_cxx.o
	$(CXX) $(VOZ_CXXF) -O3 -Wall -Wextra -c src/user/voz/habla.cpp -o $(BUILD)/user/voz/habla.o
	$(CC) $(VOZ_BASE) -std=gnu11 -fno-builtin -Wall -Wextra -Werror -c src/user/voz/voz_libc.c -o $(BUILD)/user/voz/voz_libc.o
	$(CXX) $(VOZ_BASE) -Wl,-T,src/user/voz/voz.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax $(BUILD)/user/voz/*.o \
		-Wl,--start-group $(VOZ_LIBSTDCXX) -lgcc -Wl,--end-group -o $@
# Variante para el anfitrion Linux (mismo codigo; las llamadas al sistema de SOMA se traducen a Linux): sirve para probar el
# reconocimiento y su velocidad sin QEMU (que emula AVX2 muy despacio). Rutas del almacen bajo ./voz_raiz:
#   make voz-host ARCH=x86_64 && mkdir -p voz_raiz/modelos && cp ggml-base-q5_1.bin voz_raiz/modelos/ && build/x86_64/voz_host -H 1400 /x.wav
voz-host: $(BUILD)/user/voz.elf
	$(CC) $(VOZ_BASE) -std=gnu11 -fno-builtin -DVOZ_HOST -Wall -Wextra -Werror -c src/user/voz/voz_libc.c -o $(BUILD)/voz_libc_host.o
	$(CXX) $(VOZ_BASE) -Wl,-T,src/user/voz/voz.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax \
		$(filter-out %/voz_libc.o,$(wildcard $(BUILD)/user/voz/*.o)) $(BUILD)/voz_libc_host.o \
		-Wl,--start-group $(VOZ_LIBSTDCXX) -lgcc -Wl,--end-group -o $(BUILD)/voz_host
.PHONY: voz-host
$(BUILD)/vozprog.o: $(BUILD)/user/voz.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../vozprog.o voz.elf.z
OBJS_ARCH += $(BUILD)/vozprog.o $(BUILD)/voz_sys.o
# SOMA_329_IA_LOCAL: motor de IA local (llama.cpp + ggml, third_party/llama, MIT) como proceso de ring 3 con AVX2: embeddings para la
# memoria y chat con modelos pequenos sin servidor externo. La misma libc que la voz (voz_libc.c) mas src/user/ia/ia_libc2.c, y con
# EXCEPCIONES de C++ (llama.cpp avisa asi de los errores al cargar un modelo): libsupc++ y el desenrollador de libgcc_eh.
IA_L = third_party/llama
IA_SRCS_CPP = $(shell find $(IA_L) -name '*.cpp' | sort)
IA_SRCS_C = $(shell find $(IA_L) -name '*.c' | sort)
IA_BASE = $(filter-out -fno-asynchronous-unwind-tables -fno-unwind-tables,$(VOZ_BASE))
IA_DEFS = -D_GNU_SOURCE -DGGML_SOMA -DGGML_USE_CPU -DGGML_USE_LLAMAFILE -DGGML_SCHED_MAX_COPIES=4 -DGGML_AVX -DGGML_AVX2 -DGGML_BMI2 \
	-DGGML_F16C -DGGML_FMA -DGGML_SSE42
IA_INC = -Isrc/user/voz/cxxinc -I$(IA_L)/include -I$(IA_L)/ggml/include -I$(IA_L)/ggml/src -I$(IA_L)/ggml/src/ggml-cpu -I$(IA_L)/src
IA_CXXF = $(IA_BASE) $(IA_DEFS) $(IA_INC) -std=gnu++17 -fno-threadsafe-statics
IA_CF = $(VOZ_BASE) $(IA_DEFS) $(IA_INC) -std=gnu11
IA_LIBGCC_EH = $(shell $(CC) -print-file-name=libgcc_eh.a)
IA_LIBGCC = $(shell $(CC) -print-file-name=libgcc.a)
IA_PROPIOS = src/user/ia/ia.cpp src/user/ia/ia_rt.cpp src/user/ia/ia_extra.c src/user/ia/ia_libc2.c src/user/ia/ia.ld src/core/ia_abi.h src/user/voz/voz_libc.c
$(BUILD)/user/ia.elf: $(IA_SRCS_CPP) $(IA_SRCS_C) $(shell find $(IA_L) -name '*.h') $(IA_PROPIOS) $(PY_LIBM) | $(BUILD)
	mkdir -p $(BUILD)/user/ia
	+@$(LOTE) $(BUILD)/user/ia ia_cpp - ruta $(CXX) $(IA_CXXF) -w ::: $(IA_SRCS_CPP)
	+@$(LOTE) $(BUILD)/user/ia ia_c - ruta $(CC) $(IA_CF) -w ::: $(IA_SRCS_C)
	+@$(LOTE) $(BUILD)/user/ia m m_ base $(CC) $(VOZ_BASE) -w -I$(PY_DIR)/libm_dbl/include ::: $(PY_LIBM)
	$(CC) $(VOZ_BASE) -std=gnu11 -fno-builtin -DIA_EXC -Wall -Wextra -Werror -c src/user/voz/voz_libc.c -o $(BUILD)/user/ia/voz_libc.o
	$(CC) $(VOZ_BASE) -std=gnu11 -Wall -Wextra -Werror -c src/user/ia/ia_extra.c -o $(BUILD)/user/ia/ia_extra.o
	$(CC) $(VOZ_BASE) -std=gnu11 -fno-builtin -Wall -Wextra -Werror -c src/user/ia/ia_libc2.c -o $(BUILD)/user/ia/ia_libc2.o
	$(CXX) $(IA_CXXF) -Wall -Wextra -Werror -Wno-deprecated-declarations -c src/user/ia/ia.cpp -o $(BUILD)/user/ia/ia.o
	$(CXX) $(IA_CXXF) -Wall -Wextra -Werror -c src/user/ia/ia_rt.cpp -o $(BUILD)/user/ia/ia_rt.o
	$(CXX) $(IA_BASE) -Wl,-T,src/user/ia/ia.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax -Wl,--gc-sections \
		$(BUILD)/user/ia/*.o -Wl,--start-group $(VOZ_LIBSTDCXX) $(IA_LIBGCC) $(IA_LIBGCC_EH) -Wl,--end-group -o $@
# Variante para el anfitrion Linux (pruebas sin QEMU): ia_host -m /modelos/x.gguf (-e texto | -c json | -k texto | -E/-C/-K archivo);
# las rutas del almacen se buscan bajo ./voz_raiz (como voz-host)
ia-host: $(BUILD)/user/ia.elf
	$(CC) $(VOZ_BASE) -std=gnu11 -fno-builtin -DIA_EXC -DVOZ_HOST -Wall -Wextra -Werror -c src/user/voz/voz_libc.c -o $(BUILD)/ia_h_libc.o
	$(CC) $(VOZ_BASE) -std=gnu11 -DIA_HOST -Wall -Wextra -Werror -c src/user/ia/ia_extra.c -o $(BUILD)/ia_h_extra.o
	$(CXX) $(IA_CXXF) -DIA_HOST -Wall -Wextra -Werror -Wno-deprecated-declarations -c src/user/ia/ia.cpp -o $(BUILD)/ia_h_ia.o
	$(CXX) $(IA_CXXF) -DIA_HOST -Wall -Wextra -Werror -c src/user/ia/ia_rt.cpp -o $(BUILD)/ia_h_rt.o
	$(CXX) $(IA_BASE) -Wl,-T,src/user/ia/ia.ld -Wl,-z,max-page-size=4096 -Wl,--build-id=none -Wl,--no-relax -Wl,--gc-sections \
		$(filter-out %/voz_libc.o %/ia_extra.o %/ia.o %/ia_rt.o,$(wildcard $(BUILD)/user/ia/*.o)) $(BUILD)/ia_h_libc.o $(BUILD)/ia_h_extra.o \
		$(BUILD)/ia_h_ia.o $(BUILD)/ia_h_rt.o -Wl,--start-group $(VOZ_LIBSTDCXX) $(IA_LIBGCC) $(IA_LIBGCC_EH) -Wl,--end-group -o $(BUILD)/ia_host
.PHONY: ia-host
$(BUILD)/iaprog.o: $(BUILD)/user/ia.elf.z
	cd $(BUILD)/user && $(LD) -z noexecstack -r -b binary -o ../iaprog.o ia.elf.z
OBJS_ARCH += $(BUILD)/iaprog.o $(BUILD)/ia_sys.o
endif

OBJS_CORE += $(BUILD)/elf.o $(BUILD)/audio.o $(BUILD)/wav.o $(BUILD)/audio_sys.o $(BUILD)/micrec.o $(BUILD)/uvc.o $(BUILD)/avi_wr.o $(BUILD)/camera.o $(BUILD)/jpeg_enc.o $(BUILD)/scrcap_dsp.o $(BUILD)/scrcap.o
# SOMA_PARCHE_023_MEDIA: reproductor multimedia de la GUI
OBJS_CORE += $(BUILD)/avi.o $(BUILD)/media.o      # PARCHE 139: jpeg.o solo en aarch64 (en x86_64 vive en imgdec.elf)
OBJS_CORE += $(BUILD)/yt_https.o   # SOMA_252_VIDEO_NATIVO: cliente HTTPS que lee el video segun llega
OBJS_CORE += $(BUILD)/youtube.o                   # SOMA_250_YOUTUBE: aplicacion YouTube (el video lo convierte tools/yt_service.py)
OBJS_CORE += $(BUILD)/yt_aislado.o                # SOMA_315_YOUTUBE_AISLADO: lo que responde YouTube se lee en leesvc (ring 3)
# SOMA_PARCHE_024_TERM: emulador de terminal de la GUI
OBJS_CORE += $(BUILD)/term_core.o $(BUILD)/term_ui.o
# SOMA_PARCHE_PORTAPAPELES: portapapeles global del sistema
OBJS_CORE += $(BUILD)/clipboard.o
# SOMA_PARCHE_NAVEGADOR: motor web (HTML5/CSS3/JavaScript) para el navegador
# SOMA_275_HTML: comprobador/empaquetador de webs (orden html y Programador)
OBJS_CORE += $(BUILD)/htmlproj.o $(BUILD)/web_regex.o $(BUILD)/web_js_regex.o $(BUILD)/web_js_run.o $(BUILD)/web_js_co.o $(BUILD)/web_js_u16.o $(BUILD)/web_js_bin.o $(BUILD)/web_js_proxy.o $(BUILD)/web_js_big.o $(BUILD)/web_js_gc.o $(BUILD)/web_js_module.o $(BUILD)/web_js_lib.o $(BUILD)/web_js_lib2.o $(BUILD)/web_js_lib3.o $(BUILD)/web_js_lib4.o $(BUILD)/web_js_parse.o $(BUILD)/web_js_api.o $(BUILD)/web_js_dom.o $(BUILD)/web_f64.o $(BUILD)/web_net.o $(BUILD)/web_html.o $(BUILD)/web_css.o $(BUILD)/web_layout.o $(BUILD)/web_font.o $(BUILD)/web_img_sbx.o $(BUILD)/web_inflate_sbx.o $(BUILD)/web_net_sbx.o $(BUILD)/web_page.o
$(BUILD)/web_css.o: src/apps/web_color.inc
# PARCHE 138: en x86_64 los decodificadores de imagen (web_img, web_jpeg, bmp_dec) NO se enlazan en el kernel: viven en el servicio
# aislado imgdec.elf (ring 3). En aarch64 (sin EL0 todavia) se enlazan y web_img_sbx.c los llama directamente.
ifneq ($(ARCH),x86_64)
OBJS_CORE += $(BUILD)/web_img.o $(BUILD)/web_jpeg.o $(BUILD)/bmp_dec.o $(BUILD)/web_inflate.o $(BUILD)/jpeg.o   # PARCHE 139
OBJS_CORE += $(BUILD)/pdf.o   # PARCHE 141: en x86_64 el analizador PDF vive en pdfsvc.elf
endif
$(BUILD)/web_font.o: src/apps/web_dejavu.ttf src/apps/web_libsans.ttf src/apps/web_libsansb.ttf src/fonts/vfont.h

# PARCHE 030: protector de pila opcional (make SSP=1). Detecta desbordamientos que pisan la direccion de retorno y
# detiene el sistema con un mensaje en lugar de seguir con la pila corrupta.
# (activado por defecto en x86_64; make SSP=0 lo desactiva. En aarch64 sigue desactivado hasta poder probarlo.)
ifeq ($(ARCH),x86_64)
SSP ?= 1
else
SSP ?= 0
endif
# SOMA_199: en x86_64 cada funcion y cada dato van a su propia seccion y el enlazador descarta lo que nadie referencia (unas 680
# secciones: los modulos *_proto de la Raspberry Pi, partes de uACPI sin usar, funciones de diagnostico sin llamar...). No se pierde
# nada que se use: solo sale del binario lo inalcanzable. make GC=0 lo desactiva.
ifeq ($(ARCH),x86_64)
GC ?= 1
else
GC ?= 0
endif
ifeq ($(GC),1)
CFLAGS += -ffunction-sections -fdata-sections
LDEXTRA += --gc-sections -z nostart-stop-gc
endif
ifeq ($(SSP),1)
CFLAGS += -fstack-protector-strong -mstack-protector-guard=global
OBJS_CORE += $(BUILD)/ssp.o
endif
# SOMA_306_PILAS: cada hilo del nucleo tiene 60 KiB de pila y UNA pagina de guarda de 4 KiB debajo. Una funcion con un marco
# mayor de 4 KiB (hay mas de 50) bajaba el puntero de pila de golpe y podia saltarse la guarda: escribia en la pila del hilo
# vecino sin que nada fallara. Con -fstack-clash-protection el compilador toca cada pagina al reservar el marco, asi que un
# desbordamiento cae siempre en la guarda (#PF con su traza) en vez de corromper otro hilo. Coste: una escritura por cada
# 4 KiB en las funciones grandes. make CLASH=0 lo quita. En aarch64 sigue desactivado hasta poder probarlo en la Pi.
ifeq ($(ARCH),x86_64)
CLASH ?= 1
else
CLASH ?= 0
endif
ifeq ($(CLASH),1)
CFLAGS += -fstack-clash-protection
endif
# SOMA_306_PILAS: ninguna funcion del nucleo puede reservar mas de KFRAME_MAX bytes de pila de una vez (con WERROR=1, como en
# la CI, es un error). Las mayores de hoy fuera del navegador: 9,4 KiB en x86_64 y 14 KiB en aarch64 (jpeg.c). El motor web se salta el limite: corre en pilas
# propias de 256-512 KiB (web_call_on_stack) y sus marcos llegan a 84 KiB (URL de 8 KiB).
KFRAME_MAX ?= 16384
KFRAME_FLAG = -Wframe-larger-than=$(KFRAME_MAX)
CFLAGS += $(KFRAME_FLAG)
$(BUILD)/web_net.o $(BUILD)/web_page.o $(BUILD)/web_layout.o $(BUILD)/web_js_dom.o: KFRAME_FLAG =

OBJS_CORE += $(BUILD)/rng.o         # SOMA_192: generador aleatorio unico del nucleo (ChaCha20)
OBJS_CORE += $(BUILD)/x509.o $(BUILD)/tls_roots.o $(BUILD)/tls_trust.o   # SOMA_194: validacion de certificados del TLS propio

# PARCHE 032: descripcion del hardware (ACPI / FDT)
OBJS_CORE += $(BUILD)/hwinfo.o $(BUILD)/nvme_proto.o $(BUILD)/blk.o $(BUILD)/xhci_proto.o $(BUILD)/hid_proto.o $(BUILD)/msc_proto.o $(BUILD)/fat.o $(BUILD)/crypto.o $(BUILD)/tlscrypto.o $(BUILD)/tls13.o $(BUILD)/wpa.o $(BUILD)/ccmp.o $(BUILD)/png_enc.o $(BUILD)/conc_test.o
# SOMA_308_EFICIENCIA: los protocolos de la Raspberry Pi 5 (tarjeta SD, PCIe, mailbox, RP1, Ethernet GEM, GPIO, Wi-Fi SDIO/Broadcom
# y su reserva de DMA) solo se compilan en aarch64. En x86_64 se compilaban y el enlazador los tiraba enteros: nada los usa.
# Sus pruebas del anfitrion (make test) no cambian.
ifeq ($(ARCH),aarch64)
OBJS_CORE += $(BUILD)/sd_proto.o $(BUILD)/pcie_proto.o $(BUILD)/mbox_proto.o $(BUILD)/rp1_proto.o $(BUILD)/gem_proto.o $(BUILD)/dma_pool.o $(BUILD)/gpio_proto.o $(BUILD)/sdio_proto.o $(BUILD)/brcm_proto.o
endif

# SOMA_267_DISCOS: exFAT (MODELOS y USUARIO del pendrive) y las unidades /usuario y /modelos
OBJS_CORE += $(BUILD)/exfat.o $(BUILD)/unidades.o
HOST_MACHINE := $(shell uname -m)
HOST_SO := $(shell uname -s)
# Las pruebas del anfitrion se exigen en Linux x86_64 (el equipo de referencia). En otro (macOS, ARM) las que no COMPILAN alli
# (ensamblador x86, directivas ELF) se omiten diciendolo; las que compilan se ejecutan y tienen que pasar igual.
HOST_REF := $(if $(filter Linux,$(HOST_SO)),$(filter x86_64 amd64,$(HOST_MACHINE)),)
FS_UNIDADES = src/core/unidades.c src/core/exfat.c      # fs.c las usa: toda prueba que compile fs.c las enlaza tambien
$(BUILD)/exfat.o: src/core/exfat_upcase.inc

OBJS = $(OBJS_ARCH) $(OBJS_CORE)

all: $(BUILD)/soma.iso

# SOMA_328_COMPILA_RAPIDO: cada objeto del nucleo depende de las cabeceras que de verdad incluye (gcc -MMD, archivos .d), no de
# TODAS: antes tocar un .h recompilaba el nucleo entero. Y de $(BUILD)/.cflags, que solo cambia si cambian el compilador o las
# opciones (EXTRA_CFLAGS, PLATFORM, WERROR...): asi cambiar de opciones recompila lo necesario sin tener que borrar build/.
CFLAGS += -MMD -MP
KDEPS = $(BUILD)/.cflags
SOMA_KFLAGS := $(CC) $(CFLAGS) $(UACPI_DEFS)
ifneq ($(strip $(SOMA_KFLAGS)),$(strip $(file < $(BUILD)/.cflags)))
$(shell mkdir -p $(BUILD))
$(file > $(BUILD)/.cflags,$(SOMA_KFLAGS))
endif
$(BUILD)/.cflags: | $(BUILD)
	@touch $@
-include $(wildcard $(BUILD)/*.d)
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/arch/$(ARCH)/boot.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/switch.o: src/arch/$(ARCH)/switch.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/arch/$(ARCH)/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# SOMA_164_T480S: uACPI (codigo de terceros: sin -Werror) y la capa de adaptacion
$(BUILD)/uacpi_%.o: third_party/uacpi/source/%.c | $(BUILD)
	$(CC) $(filter-out -Werror,$(CFLAGS)) $(UACPI_DEFS) -c $< -o $@
$(BUILD)/acpi_sys.o: src/arch/x86_64/acpi_sys.c src/arch/x86_64/acpi_sys.h $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(UACPI_DEFS) -c $< -o $@

# PARCHE 062: el controlador xHCI es el mismo en x86_64 y aarch64 (vive en src/arch/x86_64; lo propio de cada arquitectura va en xhci_arch.h)
ifeq ($(ARCH),aarch64)
$(BUILD)/xhci.o: src/arch/x86_64/xhci.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
endif

$(BUILD)/%.o: src/core/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/drivers/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/fs/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/net/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/ui/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/apps/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/lang/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/youtube.o: src/apps/youtube_ia.c src/apps/youtube_nativo.c src/apps/yt_proto.h src/apps/yt_inner.h src/apps/yt_aislado.h src/user/vplay/vp_abi.h   # youtube.c incluye la parte de IA (251) y la nativa (253)
$(BUILD)/gui.o: src/ui/gui_vortice.c src/ui/gui_cosmos.c src/ui/gui_iconos.c src/ui/gui_ventanas.c src/ui/gui_agente.c src/ui/gui_orbe_fx.c src/ui/gui_fotograma.c src/ui/gui_minimizar.c src/ui/gui_lang.c src/ui/gui_abrir.c src/ui/gui_orbe_frente.c src/ui/gui_prompter.c src/ui/gui_voz.c src/ui/gui_ayuda.c   # SOMA_294_AYUDA (+ gui_voz.c, gui_ayuda.c); SOMA_234_GUI_PARTIDO: gui.c los incluye (SOMA_259_ORBE: + gui_prompter.c)
$(BUILD)/shell.o: $(wildcard src/agent/shell_*.c)    # SOMA_233_SHELL_PARTIDO: shell.c incluye sus ordenes por temas
$(BUILD)/agent.o: $(wildcard src/agent/lcm_*.inc)    # SOMA_257_LCM: agent.c incluye el LCM (modelo y pegamento en .inc)
$(BUILD)/agenda_core.o: src/apps/agenda_nl.inc src/apps/agenda_ics.inc    # SOMA_272_AGENDA
$(BUILD)/shell.o: src/agent/lcm_ordenes_shell.inc    # SOMA_258_LCM: shell.c incluye cerrar y contiene
$(BUILD)/shell.o: src/agent/llm_limites_shell.inc    # SOMA_260_LLM_TAM
$(BUILD)/llm.o: src/agent/llm_limites.inc            # SOMA_260_LLM_TAM
$(BUILD)/%.o: src/agent/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c $(KDEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(ARCH)-$(PLATFORM),aarch64-rpi5)
LDEXTRA = --defsym=SOMA_BASE=0x80000 --build-id=none
endif
$(BUILD)/soma.elf: $(OBJS) linker_$(ARCH).ld
	$(LD) $(LDEXTRA) -T linker_$(ARCH).ld -o $@ $(OBJS)
ifeq ($(ARCH)-$(PLATFORM),aarch64-rpi5)
	$(OBJCOPY_A64) -O binary $@ $(BUILD)/kernel_2712.img          # PARCHE 058: imagen para la tarjeta SD de la Pi 5 (se carga en 0x80000)
endif

$(BUILD)/soma.iso: $(BUILD)/soma.elf
ifeq ($(ARCH),x86_64)
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(BUILD)/soma.elf $(BUILD)/isodir/boot/soma.elf
	cp grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/isodir
	$(GRUB_FILE) --is-x86-multiboot $(BUILD)/soma.elf
	cp $@ build/soma.iso
	cp $(BUILD)/soma.elf build/soma.elf
else
	@echo "ISO not applicable for aarch64. Target is bare ELF."
	touch $@
	cp $(BUILD)/soma.elf build/soma.elf
endif

# SOMA_154_T480S: imagen para el Lenovo ThinkPad T480s (memoria USB). Arranca en UEFI y en BIOS heredada (CSM).
# Necesita grub-efi-amd64-bin ademas de grub-pc-bin (incluidos en el Dockerfile). Escribir en la memoria USB:
#   macOS:  diskutil list; diskutil unmountDisk /dev/diskN; sudo dd if=build/soma-t480s.iso of=/dev/rdiskN bs=4m
t480s: $(BUILD)/soma.elf
	mkdir -p $(BUILD)/isodir-t480s/boot/grub
	cp $(BUILD)/soma.elf $(BUILD)/isodir-t480s/boot/soma.elf
	cp grub/grub-t480s.cfg $(BUILD)/isodir-t480s/boot/grub/grub.cfg
	@# SOMA_175A_T480S: firmware de la WiFi (lo descarga el parche 175A en firmware/); GRUB lo carga como modulo
	@rm -rf $(BUILD)/isodir-t480s/boot/firmware; if [ -f firmware/iwlwifi-8265-36.ucode ]; then mkdir -p $(BUILD)/isodir-t480s/boot/firmware && cp firmware/iwlwifi-8265-36.ucode $(BUILD)/isodir-t480s/boot/firmware/ && echo "soma-t480s.iso: con el firmware WiFi iwlwifi-8265-36.ucode"; else echo "AVISO: sin firmware/iwlwifi-8265-36.ucode: la imagen no tendra WiFi (aplica el parche 175A o ejecuta: python3 tools/firmware_wifi.py)"; fi
	$(GRUB_MKRESCUE) -o build/soma-t480s.iso $(BUILD)/isodir-t480s
	@if [ -d /usr/lib/grub/x86_64-efi ] || [ -d /opt/homebrew/lib/grub/x86_64-efi ] || [ -d /usr/local/lib/grub/x86_64-efi ]; then echo "soma-t480s.iso: arranque UEFI + BIOS"; else echo "AVISO: sin grub x86_64-efi la imagen solo arranca en BIOS heredada (activa CSM en el T480s)"; fi
# Pruebas en QEMU lo mas parecidas al portatil: q35 (chipset moderno), sin puerto serie para la pantalla, firmware real.
OVMF_CODE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/qemu/OVMF.fd /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/local/share/qemu/edk2-x86_64-code.fd))
run-t480s-uefi:
	qemu-system-x86_64 -machine q35 -m 1024M -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -cdrom build/soma-t480s.iso -serial stdio -vga std
run-t480s-bios:
	qemu-system-x86_64 -machine q35 -m 1024M -cdrom build/soma-t480s.iso -serial stdio -vga std
# SOMA_155_T480S: memoria USB en FAT32 (lo mas compatible con el firmware de Lenovo). Un unico archivo EFI/BOOT/BOOTX64.EFI
# con GRUB, el menu y el nucleo dentro. Copia build/usb/EFI en la raiz de un pendrive formateado en FAT32 (ver docs).
GRUB_MKSTANDALONE ?= $(firstword $(shell command -v grub-mkstandalone x86_64-elf-grub-mkstandalone 2>/dev/null) grub-mkstandalone)
t480s-usb: $(BUILD)/soma.elf
	mkdir -p build/usb/EFI/BOOT
	@# SOMA_175B: el firmware de la WiFi tambien va dentro del BOOTX64.EFI (antes solo iba en la ISO y el pendrive no lo tenia)
	$(GRUB_MKSTANDALONE) -O x86_64-efi -o build/usb/EFI/BOOT/BOOTX64.EFI \
	  --modules="part_gpt part_msdos fat multiboot2 all_video gfxterm efi_gop normal test" \
	  "boot/grub/grub.cfg=grub/grub-t480s.cfg" "boot/soma.elf=$(BUILD)/soma.elf" \
	  $$(if [ -f firmware/iwlwifi-8265-36.ucode ]; then echo "boot/firmware/iwlwifi-8265-36.ucode=firmware/iwlwifi-8265-36.ucode"; fi)
	@if [ -f firmware/iwlwifi-8265-36.ucode ]; then echo "BOOTX64.EFI: con el firmware WiFi iwlwifi-8265-36.ucode"; else echo "AVISO: sin firmware/iwlwifi-8265-36.ucode: el pendrive no tendra WiFi (python3 tools/firmware_wifi.py)"; fi
	@echo "build/usb/EFI/BOOT/BOOTX64.EFI listo: copia la carpeta build/usb/EFI a la raiz de un pendrive FAT32"
.PHONY: t480s t480s-usb run-t480s-uefi run-t480s-bios

clean:
	rm -rf build

# SOMA_PARCHE_002_HOST_TESTS: pruebas en host (no afectan a la build del kernel)
HOST_CC ?= cc
HOST_CXX ?= c++
TEST_INC = -Itests -Itests/host_inc -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64
TEST_WARN = -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-address -Wno-unused-function -Wno-unused-variable
TEST_CFLAGS = -std=gnu11 $(TEST_WARN) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer $(TEST_INC) $(TEST_EXTRA)
ifeq ($(STRICT),1)
TEST_CFLAGS += -Werror
endif

# PARCHE 032: pruebas y fuzzing (ASAN+UBSAN) de los analizadores ACPI/FDT
fuzz-nvme:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_nvme.c src/core/nvme_proto.c -o build/fuzz/test_nvme
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_nvme

fuzz-xhci:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_xhci.c src/core/xhci_proto.c -o build/fuzz/test_xhci
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_xhci

fuzz-hid:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_hid.c src/core/hid_proto.c src/core/xhci_proto.c -o build/fuzz/test_hid
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_hid

fuzz-msc:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_msc.c src/core/msc_proto.c -o build/fuzz/test_msc
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_msc

fuzz-fat:
	@mkdir -p build/fuzz/fat
	python3 tools/mkfat.py build/fuzz/fat
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_fat.c src/core/fat.c -o build/fuzz/test_fat
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_fat build/fuzz/fat 6000

fuzz-hw:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_hwinfo.c src/core/hwinfo.c -o build/fuzz/test_hwinfo
	./build/fuzz/test_hwinfo 50000

test:
	$(MAKE) browser-boundary-test
	$(MAKE) pruebas-lcm   # SOMA_262_PUBLICAR
	$(MAKE) pruebas-cc    # SOMA_265_CC
	$(MAKE) pruebas-js    # SOMA_274_JS
	$(MAKE) pruebas-memoria   # SOMA_277_MEMORIA
	$(MAKE) pruebas-chat      # SOMA_285_CHAT
	$(MAKE) pruebas-discos   # SOMA_267_DISCOS
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lexer.c src/lang/lexer.c -o build/tests/test_lexer
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sbuf.c -o build/tests/test_sbuf
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/core tests/test_zona_332.c src/core/zona.c -o build/tests/test_zona_332 && ./build/tests/test_zona_332   # SOMA_332_HORA
	$(HOST_CC) $(TEST_CFLAGS) -DCRUN_SOLO_FORMATO tests/test_coder_run_333.c src/apps/coder_run.c -o build/tests/test_coder_run_333 && ./build/tests/test_coder_run_333   # SOMA_333_SALIDA
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lang.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_lang
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lang_264.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_lang_264   # SOMA_264_LANG
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_sim.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_sim
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_235.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_235
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_py_239.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_py_239
	$(HOST_CC) $(TEST_CFLAGS) tests/test_fs.c tests/stubs_common.c tests/stubs_fs.c src/fs/fs.c $(FS_UNIDADES) -o build/tests/test_fs
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_heap_cfg.h tests/test_heap.c tests/stubs_common.c tests/stubs_heap.c tests/stubs_vmm.c src/core/mem.c -o build/tests/test_heap
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_sched_cfg.h tests/test_sched.c src/core/thread.c src/core/panic.c src/core/mutex.c tests/stubs_common.c tests/stubs_vmm.c tests/stubs_sched.c -o build/tests/test_sched
	$(HOST_CC) $(TEST_CFLAGS) -DSOMA_HOST_PANIC=1 tests/test_panic.c src/core/panic.c tests/stubs_common.c tests/stubs_vmm.c -o build/tests/test_panic
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_sched_cfg.h tests/test_mutex.c src/core/mutex.c src/core/thread.c src/core/panic.c tests/stubs_common.c tests/stubs_vmm.c tests/stubs_sched.c -o build/tests/test_mutex
	$(HOST_CC) $(TEST_CFLAGS) tests/test_srcfs.c src/fs/srcfs.c src/apps/web_inflate.c tests/stubs_common.c tests/stubs_fs.c -o build/tests/test_srcfs
	$(HOST_CC) $(TEST_CFLAGS) tests/test_elf.c src/core/elf.c -o build/tests/test_elf
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/arch/x86_64 tests/test_uaccess.c -o build/tests/test_uaccess
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/arch/x86_64 tests/test_pic_irq.c -o build/tests/test_pic_irq
	$(HOST_CC) $(TEST_CFLAGS) tests/test_agent_policy.c -o build/tests/test_agent_policy
	$(HOST_CC) $(TEST_CFLAGS) tests/test_user_path.c -o build/tests/test_user_path
	$(HOST_CC) $(TEST_CFLAGS) tests/test_pmm_scan.c -o build/tests/test_pmm_scan
	$(HOST_CC) $(TEST_CFLAGS) tests/test_pila_canario_306.c -o build/tests/test_pila_canario_306      # SOMA_306_PILAS
	$(HOST_CC) $(TEST_CFLAGS) tests/test_agent_protegidos_307.c -o build/tests/test_agent_protegidos_307      # SOMA_307_SEGURIDAD
	$(HOST_CC) -std=gnu11 -O2 $(TEST_WARN) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(if $(filter 1,$(STRICT)),-Werror) tests/test_memfn_gen_308.c -o build/tests/test_memfn_gen_308      # SOMA_308_EFICIENCIA
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_INC) tests/test_tcp_reap.c tests/stubs_netif.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/tests/test_tcp_reap
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_INC) tests/test_tcp_send.c tests/stubs_netif.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/tests/test_tcp_send
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_INC) tests/test_tcp_rx.c tests/stubs_netif.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/tests/test_tcp_rx
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DARP_CHAL_MS=30 $(TEST_INC) tests/test_arp_191.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/tests/test_arp_191
	$(HOST_CC) $(TEST_CFLAGS) tests/test_rng_192.c src/core/rng.c src/core/tlscrypto.c src/core/crypto.c -o build/tests/test_rng_192
	$(HOST_CC) $(TEST_CFLAGS) tests/test_agent_net_193.c src/agent/agent_net.c -o build/tests/test_agent_net_193
	$(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_x509_194.c src/core/x509.c src/core/tls_roots.c src/core/tlscrypto.c src/core/crypto.c -o build/tests/test_x509_194
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_llm_proto_195.c src/agent/llm_proto.c -o build/tests/test_llm_proto_195
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_llm_vision_248.c src/agent/llm_proto.c -o build/tests/test_llm_vision_248   # SOMA_248_DOC
	@if [ "$(HOST_MACHINE)" = x86_64 ] || [ "$(HOST_MACHINE)" = amd64 ]; then echo "$(HOST_CC) -O2 -g -Wall -Wextra -fsanitize=address,undefined tests/test_memfn_199.c -o build/tests/test_memfn_199"; $(HOST_CC) -O2 -g -Wall -Wextra -fsanitize=address,undefined tests/test_memfn_199.c -o build/tests/test_memfn_199; else echo "test_memfn_199: memfn.h es ensamblador x86 y este equipo es $(HOST_MACHINE): se omite (se prueba en Linux x86_64 y en QEMU)"; fi   # SOMA_267_DISCOS
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -DWEB_HOST_TEST -Isrc/apps -Isrc/core tests/test_f64_200.c src/apps/web_f64.c -o build/tests/test_f64_200
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fwrapv -fno-sanitize=signed-integer-overflow,shift,nonnull-attribute -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Itests tests/fuzz_http_201.c src/apps/web_net.c src/apps/web_inflate.c -o build/tests/fuzz_http_201
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_sbx_ref.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c src/apps/pdf.c -o build/tests/test_sbx_ref
	$(HOST_CC) $(TEST_CFLAGS) tests/test_disk_layout.c -o build/tests/test_disk_layout
	$(HOST_CC) $(TEST_CFLAGS) tests/test_httpd_policy.c -o build/tests/test_httpd_policy
	$(HOST_CC) $(TEST_CFLAGS) tests/test_errno.c -o build/tests/test_errno
	$(HOST_CC) $(TEST_CFLAGS) tests/test_slotio.c -o build/tests/test_slotio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_ckptio.c -o build/tests/test_ckptio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_spillio.c src/fs/bigfs.c -o build/tests/test_spillio
	$(HOST_CC) $(TEST_CFLAGS) -DVMM_HOST_TEST tests/test_vmm.c tests/stubs_common.c src/arch/x86_64/vmm.c -o build/tests/test_vmm
	$(HOST_CC) $(TEST_CFLAGS) tests/test_bigfs.c src/fs/bigfs.c -o build/tests/test_bigfs
	$(HOST_CC) $(TEST_CFLAGS) tests/test_audio.c src/core/audio.c -o build/tests/test_audio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_wav.c src/core/wav.c -o build/tests/test_wav
	$(HOST_CC) $(TEST_CFLAGS) tests/test_jpeg.c src/core/jpeg.c -o build/tests/test_jpeg
	$(HOST_CC) $(TEST_CFLAGS) tests/test_avi.c src/core/avi.c -o build/tests/test_avi
	$(HOST_CC) $(TEST_CFLAGS) tests/test_termcore.c src/apps/term_core.c -o build/tests/test_termcore
	$(HOST_CC) $(TEST_CFLAGS) tests/test_youtube_250.c -o build/tests/test_youtube_250   # SOMA_250_YOUTUBE
	$(HOST_CC) $(TEST_CFLAGS) tests/test_youtube_ia_251.c -o build/tests/test_youtube_ia_251   # SOMA_251_YOUTUBE_IA
	$(HOST_CC) $(TEST_CFLAGS) tests/test_yt_busquedas_335.c -o build/tests/test_yt_busquedas_335   # SOMA_335_YT_BUSQUEDAS
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/agent tests/test_inv_tema_336.c src/agent/inv_tema.c -o build/tests/test_inv_tema_336   # SOMA_336_INVESTIGA_TEMAS
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/apps tests/test_noticias_337.c src/apps/noticias_rss.c -o build/tests/test_noticias_337   # SOMA_337_NOTICIAS
	$(HOST_CC) $(TEST_CFLAGS) -DAGR_SOLO_PURO -Isrc/agent tests/test_agentes_338.c src/agent/agentes_reg.c -o build/tests/test_agentes_338   # SOMA_338_AGENTES
	$(HOST_CC) $(TEST_CFLAGS) tests/test_mp4_252.c -o build/tests/test_mp4_252   # SOMA_252_VIDEO_NATIVO
	$(HOST_CC) $(TEST_CFLAGS) -DYT_HTTPS_PURE_ONLY tests/test_yt_inner_252.c src/apps/yt_https.c -o build/tests/test_yt_inner_252
	./build/tests/test_lexer
	./build/tests/test_sbuf
	./build/tests/test_lang
	./build/tests/test_lang_264
	./build/tests/test_coder_sim
	./build/tests/test_coder_235
	./build/tests/test_coder_py_239
	./build/tests/test_fs
	./build/tests/test_fs persist-write build/tests/disk.img
	./build/tests/test_fs persist-check build/tests/disk.img
	./build/tests/test_heap
	$(HOST_CC) $(TEST_CFLAGS) tests/test_hwinfo.c src/core/hwinfo.c -o build/tests/test_hwinfo
	./build/tests/test_hwinfo 2000
	$(HOST_CC) $(TEST_CFLAGS) tests/test_nvme.c src/core/nvme_proto.c -o build/tests/test_nvme
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_nvme
	$(HOST_CC) $(TEST_CFLAGS) tests/test_blk.c src/core/blk.c -o build/tests/test_blk
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_blk
	$(HOST_CC) $(TEST_CFLAGS) tests/test_xhci.c src/core/xhci_proto.c -o build/tests/test_xhci
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_xhci
	$(HOST_CC) $(TEST_CFLAGS) tests/test_hid.c src/core/hid_proto.c src/core/xhci_proto.c -o build/tests/test_hid
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_hid
	$(HOST_CC) $(TEST_CFLAGS) tests/test_msc.c src/core/msc_proto.c -o build/tests/test_msc
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_msc
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sd.c src/core/sd_proto.c -o build/tests/test_sd
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_sd
	$(HOST_CC) $(TEST_CFLAGS) tests/test_pcie.c src/core/pcie_proto.c -o build/tests/test_pcie
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_pcie
	$(HOST_CC) $(TEST_CFLAGS) tests/test_mbox.c src/core/mbox_proto.c -o build/tests/test_mbox
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_mbox
	$(HOST_CC) $(TEST_CFLAGS) tests/test_rp1.c src/core/rp1_proto.c -o build/tests/test_rp1
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_rp1
	$(HOST_CC) $(TEST_CFLAGS) tests/test_gem.c src/core/gem_proto.c -o build/tests/test_gem
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_gem
	$(HOST_CC) $(TEST_CFLAGS) tests/test_gpio.c src/core/gpio_proto.c -o build/tests/test_gpio
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_gpio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sdio.c src/core/sdio_proto.c -o build/tests/test_sdio
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_sdio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_wpa.c src/core/crypto.c src/core/wpa.c src/net/netif.c -o build/tests/test_wpa
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_wpa
	$(HOST_CC) $(TEST_CFLAGS) tests/test_ccmp_175.c src/core/ccmp.c src/core/crypto.c -o build/tests/test_ccmp_175
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_ccmp_175
	$(HOST_CC) $(TEST_CFLAGS) tests/test_tls_176.c src/core/tlscrypto.c src/core/crypto.c -o build/tests/test_tls_176
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_tls_176
	@if [ "$(HOST_MACHINE)" = x86_64 ] || [ "$(HOST_MACHINE)" = amd64 ]; then set -ex; $(HOST_CC) $(TEST_CFLAGS) -DHDA_HOST_TEST tests/test_mic_177.c -o build/tests/test_mic_177; ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_mic_177; else echo "test_mic_177: hda.c usa ensamblador x86 (rdtsc) y este equipo es $(HOST_MACHINE): se omite"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_micrec_177"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_micrec_177.c -lm -o build/tests/test_micrec_177; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_micrec_177; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_micrec_177: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_uvc_178"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_uvc_178.c src/core/uvc.c -o build/tests/test_uvc_178; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_uvc_178; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_uvc_178: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_avi_wr_178"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_avi_wr_178.c src/core/avi_wr.c src/core/avi.c -o build/tests/test_avi_wr_178; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_avi_wr_178; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_avi_wr_178: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_jpeg_enc_179"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_jpeg_enc_179.c src/core/jpeg_enc.c src/core/uvc.c src/core/jpeg.c -lm -o build/tests/test_jpeg_enc_179; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_jpeg_enc_179; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_jpeg_enc_179: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_scrcap_179"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_scrcap_179.c src/core/scrcap_dsp.c -lm -o build/tests/test_scrcap_179; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_scrcap_179; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_scrcap_179: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_img_180"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Itests tests/test_web_img_180.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -o build/tests/test_web_img_180; then ./build/tests/test_web_img_180; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_img_180: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_paint_ia_292"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/agent -Itests tests/test_paint_ia_292.c src/apps/paint_svg.c src/agent/agent_ruta.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -o build/tests/test_paint_ia_292; then ./build/tests/test_paint_ia_292; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_paint_ia_292: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_292_PAINT_IA
	@echo "[prueba] test_paint_ia_maquina_292"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Itests -Itests/host_inc -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 tests/test_paint_ia_maquina_292.c src/apps/paint_svg.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -o build/tests/test_paint_ia_maquina_292; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_paint_ia_maquina_292; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_paint_ia_maquina_292: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_292_PAINT_IA
	@echo "[prueba] test_web_layout_181"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_layout_181.c tests/layhost/fbhost.c src/apps/web_layout.c src/apps/web_css.c src/apps/web_html.c src/apps/web_font.c src/ui/soma_fonts.c -o build/tests/test_web_layout_181; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_web_layout_181; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_layout_181: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_font_182"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_font_182.c tests/layhost/fbhost.c src/apps/web_font.c src/ui/soma_fonts.c src/apps/web_css.c src/apps/web_html.c -o build/tests/test_web_font_182; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_web_font_182; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_font_182: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_183"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_183.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_183; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/tests/test_web_js_183 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_183: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_185"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_185.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_185; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/tests/test_web_js_185 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_185: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_186"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_186.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_186; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/tests/test_web_js_186 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_186: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_187"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_187.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_187; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 ./build/tests/test_web_js_187 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_187: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_187a"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_187a.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_187a; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 ./build/tests/test_web_js_187a 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_187a: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_web_js_188"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_web_js_188.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_web_js_188; then JSCO_OFF=1 ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=1 ./build/tests/test_web_js_188 2>/dev/null; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_web_js_188: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_font_184"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -DSOMA_FONT_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_font_184.c src/ui/gui_font.c src/apps/web_font.c src/ui/soma_fonts.c -o build/tests/test_font_184; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_font_184; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_font_184: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_brcm"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_brcm.c src/core/brcm_proto.c src/net/dhcp_proto.c -o build/tests/test_brcm; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_brcm; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_brcm: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_png"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_png.c src/core/png_enc.c -o build/tests/test_png; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_png; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_png: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	python3 tests/check_png.py build/tests
	@echo "[prueba] test_tex"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_tex.c src/apps/tex.c -o build/tests/test_tex; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_tex; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_tex: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_docgen_248"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/agent -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_docgen_248.c tests/layhost/fbhost.c src/apps/docgen.c src/agent/docagent.c src/core/pdfw.c src/core/png_enc.c src/apps/tex.c src/apps/web_layout.c src/apps/web_css.c src/apps/web_html.c src/apps/web_font.c src/ui/soma_fonts.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -lm -o build/tests/test_docgen_248; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_docgen_248; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_docgen_248: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_pdf.c src/apps/pdf.c src/apps/web_inflate.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/core/jpeg.c -o build/tests/test_pdf
	python3 tests/pdf_seeds.py build/tests/pdf
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_pdf --check build/tests/pdf/clasico.pdf build/tests/pdf/xrefstm.pdf
	@echo "[prueba] test_dma_pool"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_dma_pool.c src/core/dma_pool.c -o build/tests/test_dma_pool; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_dma_pool; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_dma_pool: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@mkdir -p build/tests/fat
	python3 tools/mkfat.py build/tests/fat
	@echo "[prueba] test_fat"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_fat.c src/core/fat.c -o build/tests/test_fat; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_fat build/tests/fat 800; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_fat: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	python3 tests/mk_informe_228.py build/tests/informe_228.img
	@echo "[prueba] test_arranque_228"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_arranque_228.c src/core/fat.c -o build/tests/test_arranque_228; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_arranque_228 build/tests/informe_228.img; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_arranque_228: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_guardado_238"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_guardado_238.c src/core/fat.c -o build/tests/test_guardado_238; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_guardado_238 build/tests/informe_228.img; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_guardado_238: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_texto_229"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_texto_229.c -o build/tests/test_texto_229; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_texto_229 200000; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_texto_229: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_iconos_240"; if $(HOST_CC) $(TEST_CFLAGS) tests/test_iconos_240.c src/ui/sysicon.c -o build/tests/test_iconos_240; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_iconos_240; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_iconos_240: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_voz_241"; if $(HOST_CXX) -std=gnu++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ithird_party/whisper/src tests/test_voz_241.cpp -o build/tests/test_voz_241; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_voz_241; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_voz_241: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_voz_245"; if $(HOST_CXX) -std=gnu++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_voz_245.cpp -o build/tests/test_voz_245; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_voz_245; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_voz_245: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_tts_250"; if $(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/core tests/test_tts_250.c -o build/tests/test_tts_250; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_tts_250; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_tts_250: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_habla_247"; if $(HOST_CXX) -std=gnu++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined tests/test_habla_247.cpp -o build/tests/test_habla_247; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_habla_247; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_habla_247: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	./build/tests/test_sched
	./build/tests/test_panic
	./build/tests/test_mutex
	./build/tests/test_uaccess
	./build/tests/test_pic_irq
	./build/tests/test_agent_policy
	./build/tests/test_user_path
	./build/tests/test_pmm_scan
	./build/tests/test_pila_canario_306
	./build/tests/test_agent_protegidos_307
	./build/tests/test_memfn_gen_308
	./build/tests/test_tcp_reap
	./build/tests/test_tcp_send
	./build/tests/test_tcp_rx
	./build/tests/test_arp_191
	./build/tests/test_rng_192
	./build/tests/test_agent_net_193
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_x509_194
	./build/tests/test_llm_proto_195
	./build/tests/test_llm_vision_248
	@echo "[prueba] test_rutas_251"; if $(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_rutas_251.c src/agent/agent_ruta.c -o build/tests/test_rutas_251; then ./build/tests/test_rutas_251; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_rutas_251: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_busca_251"; if $(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_busca_251.c src/agent/busca_web.c -o build/tests/test_busca_251; then ./build/tests/test_busca_251; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_busca_251: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	@echo "[prueba] test_investiga_288"; if $(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent -Isrc/core tests/test_investiga_288.c src/agent/web_lee.c src/agent/agent_ruta.c -o build/tests/test_investiga_288; then ./build/tests/test_investiga_288; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_investiga_288: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_288_INVESTIGA
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_web_lee_313.c src/agent/web_lee.c -o build/tests/test_web_lee_313 && ./build/tests/test_web_lee_313      # SOMA_313_LECTOR_WEB
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/core tests/test_progz_314.c -o build/tests/test_progz_314 && ./build/tests/test_progz_314      # SOMA_314_PROGZ
	python3 tools/progz.py --prueba      # SOMA_314_PROGZ
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/apps -Isrc/agent tests/test_correo_proto_319.c src/apps/correo_core.c src/agent/agent_ruta.c -o build/tests/test_correo_proto_319 && ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_correo_proto_319 100000      # SOMA_319_CORREO_PROTO
	$(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_wpa_hostil_320.c src/core/crypto.c src/core/wpa.c -o build/tests/test_wpa_hostil_320 && ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_wpa_hostil_320      # SOMA_320_ENTRADAS_HOSTILES
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_llm_hostil_320.c src/agent/llm_proto.c -o build/tests/test_llm_hostil_320 && ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_llm_hostil_320      # SOMA_320_ENTRADAS_HOSTILES
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/apps tests/test_chat_md_hostil_321.c src/apps/chat_md.c src/apps/tex.c -o build/tests/test_chat_md_hostil_321 && ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_chat_md_hostil_321 20000      # SOMA_321_CHAT_MD_HOSTIL
	$(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_tex_hostil_322.c src/apps/tex.c -o build/tests/test_tex_hostil_322 && ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_tex_hostil_322 20000      # SOMA_322_TEX_HOSTIL
	$(HOST_CC) $(TEST_CFLAGS) -O1 -DWEB_HOST_TEST -Isrc/apps tests/test_yt_aislado_315.c src/apps/yt_aislado.c -o build/tests/test_yt_aislado_315 && ./build/tests/test_yt_aislado_315      # SOMA_315_YOUTUBE_AISLADO
	@echo "[prueba] test_prog_voz_290"; if $(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/agent tests/test_prog_voz_290.c src/agent/prog_voz.c -o build/tests/test_prog_voz_290; then ./build/tests/test_prog_voz_290; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_prog_voz_290: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_290_VOZ_CODIGO
	@echo "[prueba] test_ayuda_294"; if $(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_ayuda_294.c -o build/tests/test_ayuda_294; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_ayuda_294; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_ayuda_294: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_294_AYUDA
	@echo "[prueba] test_ayuda_maquina_294"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_ayuda_maquina_294.c tests/layhost/fbhost.c src/apps/web_font.c src/ui/soma_fonts.c -o build/tests/test_ayuda_maquina_294; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_ayuda_maquina_294; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_ayuda_maquina_294: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_294_AYUDA
	@echo "[prueba] test_correo_295"; if $(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/apps -Isrc/agent tests/test_correo_295.c src/apps/correo_core.c src/agent/agent_ruta.c -o build/tests/test_correo_295; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_correo_295; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_correo_295: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_295_CORREO
	$(HOST_CC) $(TEST_CFLAGS) -O1 -DWEB_HOST_TEST -Isrc/apps tests/test_correo_sbx_311.c src/apps/correo_core.c src/apps/correo_sbx.c -o build/tests/test_correo_sbx_311 && ./build/tests/test_correo_sbx_311      # SOMA_311_CORREO_AISLADO
	$(HOST_CC) $(TEST_CFLAGS) -O1 -DWEB_HOST_TEST -Isrc/agent tests/test_web_aislado_312.c src/agent/web_aislado.c src/agent/web_lee.c src/agent/busca_web.c -o build/tests/test_web_aislado_312 && ./build/tests/test_web_aislado_312      # SOMA_312_WEB_AISLADA
	@echo "[prueba] test_correo_red_295 (IMAP/SMTP con TLS 1.3 y la app entera)"; if command -v openssl >/dev/null 2>&1 && python3 -c "import ssl, sys; sys.exit(0 if ssl.HAS_TLSv1_3 else 1)" >/dev/null 2>&1 && $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DPLATFORM_X86 -DWEB_HOST_TEST -Itests -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 -Isrc/fonts -Isrc/icons tests/test_correo_red_295.c src/apps/correo_red.c src/apps/correo_sbx.c src/apps/correo_core.c src/core/tls13.c src/core/tlscrypto.c src/core/crypto.c src/core/x509.c -o build/tests/test_correo_red_295 && $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DPLATFORM_X86 -DWEB_HOST_TEST -Itests -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 -Isrc/fonts -Isrc/icons tests/test_correoapp_295.c src/apps/correo_core.c src/apps/correo_red.c src/apps/correo_sbx.c src/apps/agenda_core.c src/ui/textfield.c tests/layhost/fbhost.c src/apps/web_font.c src/ui/soma_fonts.c src/core/png_enc.c src/core/tls13.c src/core/tlscrypto.c src/core/crypto.c src/core/x509.c -lm -o build/tests/test_correoapp_295; then ASAN_OPTIONS=detect_leaks=0 python3 tests/test_correo_red_295.py build/tests/test_correo_red_295 build/tests/test_correoapp_295; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_correo_red_295: no compila, o falta openssl o un python3 con TLS 1.3, en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_295_CORREO
	@echo "[prueba] test_visor_289"; if $(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_visor_289.c -o build/tests/test_visor_289; then ./build/tests/test_visor_289; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_visor_289: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_289_VISOR
	@echo "[prueba] test_visor_maquina_289"; if $(HOST_CC) $(TEST_CFLAGS) -O1 tests/test_visor_maquina_289.c src/apps/tex.c -o build/tests/test_visor_maquina_289; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_visor_maquina_289; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_visor_maquina_289: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_289_VISOR
	@if [ "$(HOST_MACHINE)" = x86_64 ] || [ "$(HOST_MACHINE)" = amd64 ]; then ./build/tests/test_memfn_199; fi
	./build/tests/test_f64_200
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/fuzz_http_201 3000 1
	./build/tests/test_sbx_ref
	./build/tests/test_disk_layout
	./build/tests/test_httpd_policy
	./build/tests/test_errno
	./build/tests/test_slotio
	./build/tests/test_ckptio
	./build/tests/test_spillio
	./build/tests/test_vmm
	./build/tests/test_elf $(wildcard build/x86_64/user/*.elf)
	./build/tests/test_bigfs
	python3 tests/test_bigfs_xcheck.py build/tests/test_bigfs
	./build/tests/test_audio
	./build/tests/test_wav
	python3 tests/test_video.py
	./build/tests/test_termcore
	./build/tests/test_youtube_250
	$(HOST_CC) -O2 -Wall -Wextra -Isrc/core tests/avicheck_250.c src/core/avi.c src/core/jpeg.c -o build/tests/avicheck_250
	python3 tests/test_yt_service_250.py build/tests/avicheck_250
	./build/tests/test_youtube_ia_251
	./build/tests/test_yt_busquedas_335
	./build/tests/test_inv_tema_336
	./build/tests/test_noticias_337
	./build/tests/test_agentes_338
	python3 tests/test_yt_service_251.py
	./build/tests/test_mp4_252
	./build/tests/test_yt_inner_252
	@echo "[prueba] test_vplay_252"; if HOST_CC="$(HOST_CC)" python3 tests/test_vplay_252.py; then :; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_vplay_252: libavc (intrinsecas SSE) no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)); solo se exige en Linux x86_64: SE OMITE"; fi   # SOMA_267_DISCOS
	python3 tests/test_wavcheck.py
	python3 tests/test_fpcheck.py
	python3 tests/test_srcfs_pack.py
	python3 tests/test_size_check_310.py      # SOMA_310_PRESUPUESTO
	python3 tests/test_srcfs_pack.py --emit build/tests/srcfs.img build/tests/srcfs.manifest
	./build/tests/test_srcfs build/tests/srcfs.img build/tests/srcfs.manifest
	@echo "TESTS OK"

audit:
	python3 tools/audit.py

# PARCHE 031: fuzzing de la pila de red (ARP/IP/ICMP/UDP/TCP) con ASAN+UBSAN
# SOMA_200: fuzzing del motor del navegador (HTML/CSS/maquetacion/pintado y JavaScript/DOM) con ASan y UBSan.
#   make fuzz-web FUZZ_ITERS=20000 FUZZ_SEED=7     make fuzz-js FUZZ_ITERS=20000 FUZZ_SEED=7
# Si un caso falla queda en /tmp/fuzz_web_ultimo.html o /tmp/fuzz_js_ultimo.js; se reproduce con el archivo como tercer argumento.
fuzz-web:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fwrapv -fno-sanitize=signed-integer-overflow,shift,nonnull-attribute -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/fuzz_web_200.c tests/layhost/fbhost.c src/apps/web_layout.c src/apps/web_css.c src/apps/web_html.c src/apps/web_font.c src/ui/soma_fonts.c -o build/fuzz/fuzz_web_200
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/fuzz_web_200 $(or $(FUZZ_ITERS),3000) $(or $(FUZZ_SEED),1)

fuzz-js:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fwrapv -fno-sanitize=signed-integer-overflow,shift,nonnull-attribute -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/fuzz_js_200.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/fuzz/fuzz_js_200
	ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/fuzz/fuzz_js_200 $(or $(FUZZ_ITERS),3000) $(or $(FUZZ_SEED),1)

# SOMA_201: fuzzing del cliente HTTP del navegador (con un servidor hostil simulado) y de la maquina de estados del TLS 1.3.
fuzz-http:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fwrapv -fno-sanitize=signed-integer-overflow,shift,nonnull-attribute -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Itests tests/fuzz_http_201.c src/apps/web_net.c src/apps/web_inflate.c -o build/fuzz/fuzz_http_201
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/fuzz_http_201 $(or $(FUZZ_ITERS),20000) $(or $(FUZZ_SEED),1)

fuzz-tls:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -fwrapv -fno-sanitize=signed-integer-overflow,shift,nonnull-attribute -Isrc -Isrc/core -Isrc/net -Isrc/arch/x86_64 tests/fuzz_tls_201.c src/core/tls13.c -o build/fuzz/fuzz_tls_201
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/fuzz_tls_201 $(or $(FUZZ_ITERS),20000) $(or $(FUZZ_SEED),1)

fuzz-net:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_INC) tests/fuzz_net.c tests/stubs_netif.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/fuzz/fuzz_net
	./build/fuzz/fuzz_net $(or $(FUZZ_ITERS),20000) $(or $(FUZZ_SEED),1)
	@echo "FUZZ-NET OK"

# PARCHE 088: fuzzing del lector de PDF
fuzz-pdf:
	@mkdir -p build/fuzz
	python3 tests/pdf_seeds.py build/fuzz/pdf
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_pdf.c src/apps/pdf.c src/apps/web_inflate.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/core/jpeg.c -o build/fuzz/test_pdf
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_pdf --fuzz $(or $(FUZZ_ITERS),3000) $(or $(FUZZ_SEED),1) build/fuzz/pdf/*.pdf

# PARCHE 029: fuzzing de decodificadores con ASAN+UBSAN (necesita Pillow para crear las semillas)
fuzz:
	@mkdir -p build/fuzz
	python3 tests/fuzz_seeds.py build/fuzz/seeds
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang tests/fuzz_decoders.c src/apps/web_img.c src/apps/bmp_dec.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -o build/fuzz/fuzz_decoders
	./build/fuzz/fuzz_decoders $(or $(FUZZ_ITERS),2000) $(or $(FUZZ_SEED),1) build/fuzz/seeds/*
	@echo "FUZZ OK"

# SOMA_PARCHE_004_CI: lo que ejecuta la integracion continua
ci:
	$(MAKE) test STRICT=1
	python3 tools/audit.py --strict

# SOMA_152: navegador fluido (anillo, desplazamiento suave, :hover selectivo, Bloom de clases, arena estable)
web152-test:
	@mkdir -p build/tests
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_web_fluido_152.c src/apps/web_css.c src/apps/web_html.c -o build/tests/test_web_fluido_152
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_web_fluido_152

# Requiere la imagen compilada (make ARCH=...) y QEMU instalado
# SOMA_300_CI: limite de tiempo del arranque de prueba (en x86_64). En tu equipo, 6 s; la CI de GitHub (QEMU sin KVM y
# maquinas compartidas) usa BOOT_MAX_S=30.
BOOT_MAX_S ?= 6
boot-smoke:
	python3 tools/boot_smoke.py --arch $(ARCH) $(if $(filter x86_64,$(ARCH)),--max-seconds $(BOOT_MAX_S))

# PARCHE 138: servicios aislados en ring 3 (ataques al aislamiento, imagenes vs referencia, fuzzing, concurrencia) + pruebas de ring 3, en QEMU
sbx-check:
	python3 tools/sbx_check.py --web --srcfs

# Arranca en QEMU con varias CPU (qemu64 sin RDRAND, Nehalem, max) y comprueba que no hay panico y que HTTP responde (x86_64)
cpu-matrix:
	python3 tools/cpu_matrix.py

# SOMA_310_PRESUPUESTO: cuanto ocupa el nucleo frente a su limite de 46 MiB (linker_x86_64.ld) y que es lo que mas ocupa.
# Avisa desde el 85 %; la CI falla desde el 90 % (SIZE_FALLAR=90), antes de que el enlazador corte de golpe al 100 %.
SIZE_FALLAR ?= 100
size-check:
	python3 tools/size_check.py build/x86_64/soma.elf --fallar-en $(SIZE_FALLAR)

# Falla si el SO vuelve a consumir CPU en reposo (solo Linux + QEMU, x86_64)
idle-check:
	python3 tools/idle_check.py

# Provoca un panic y un cuelgue en QEMU y comprueba backtrace + vigilante (x86_64)
fault-check:
	python3 tools/fault_check.py

.PHONY: web152-test all clean test fuzz fuzz-web fuzz-js fuzz-http fuzz-tls fuzz-net fuzz-hw fuzz-nvme fuzz-xhci fuzz-hid fuzz-msc fuzz-fat audit ci boot-smoke cpu-matrix sbx-check idle-check fault-check

# SOMA_143A_BROWSER_NET: 2 modos, sin sockets ni QEMU, fail-closed.
.PHONY: browser-boundary-test
# SOMA_267_DISCOS: -D_FORTIFY_SOURCE=0: en macOS, con -O1, el SDK convierte memcpy/memmove/memset en macros y src/core/mem.h no compilaba
BB_FORTIFY = -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
browser-boundary-test:
	@mkdir -p build/tests
	$(HOST_CC) -std=gnu11 -O1 $(BB_FORTIFY) -Wall -Wextra -Werror -DSOMA_BROWSER_NET_TEST=1 -DSOMA_BROWSER_NET_RING3=0 -Isrc -Isrc/core -Isrc/apps tests/test_browser_boundary.c -o build/tests/test_browser_boundary_0
	./build/tests/test_browser_boundary_0
	$(HOST_CC) -std=gnu11 -O1 $(BB_FORTIFY) -Wall -Wextra -Werror -DSOMA_BROWSER_NET_TEST=1 -DSOMA_BROWSER_NET_RING3=1 -Isrc -Isrc/core -Isrc/apps tests/test_browser_boundary.c -o build/tests/test_browser_boundary_1
	./build/tests/test_browser_boundary_1

# SOMA_262_PUBLICAR: pruebas en el anfitrion del LCM (253-258), el orbe (259) y los tamanos del LLM (260). Unos segundos y sin
# Docker; las pasa subir_github.py antes de subir y forman parte de make test (y de la CI).
LCM_TEST_CFLAGS = -std=gnu11 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined $(if $(filter 1,$(STRICT)),-Werror)
pruebas-lcm:
	@mkdir -p build/tests
	python3 tests/test_lcm_ajuste_276.py   # SOMA_276_LCM
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O2 -Isrc/agent tests/test_lcm_ordenes_253.c -o build/tests/test_lcm_ordenes_253
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/agent tests/test_lcm_257.c -o build/tests/test_lcm_257
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/agent tests/test_lcm_258_ordenes.c -o build/tests/test_lcm_258_ordenes
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/ui tests/test_orbe_259.c -o build/tests/test_orbe_259
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/agent tests/test_llm_tam_260.c -o build/tests/test_llm_tam_260
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/agent tests/test_aviso_266.c -o build/tests/test_aviso_266   # SOMA_266_AVISO
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/ui tests/test_orbe_266.c -o build/tests/test_orbe_266
	$(HOST_CC) $(LCM_TEST_CFLAGS) -O1 -Isrc/agent tests/test_lcm_286.c -o build/tests/test_lcm_286   # SOMA_286_YOUTUBE
	$(HOST_CC) -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wno-unused-function -Isrc/agent -c tests/test_lcm_257_nucleo.c -o build/tests/lcm_nucleo_257.o
	$(HOST_CC) -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Wno-unused-function -Isrc/ui -Isrc/apps -c tests/test_orbe_259_nucleo.c -o build/tests/orbe_nucleo_259.o
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lcm_ordenes_253
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lcm_257
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lcm_258_ordenes
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_orbe_259
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_llm_tam_260
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_aviso_266
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_orbe_266
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lcm_286

# SOMA_265_CC: el compilador de C en el anfitrion (solo Linux x86_64: ejecuta los .elf de SOMA con tests/somaemu_265.c; en macOS
# avisa y no hace nada) y el programador en modo C. En QEMU: python3 tests/prueba_cc_265.py
pruebas-cc:
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_c_265.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_c_265
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_coder_c_265
	HOST_CC="$(HOST_CC)" python3 tests/test_cc_265.py $(if $(filter 1,$(RAPIDO)),--rapido)

# SOMA_274_JS: la orden js. El nucleo de js (consola, input, fs, ventana, temporizadores) con ASan/UBSan y el programa de ring 3 real
# (src/user/jsrun.c) ejecutado en el anfitrion Linux x86_64 con el motor compilado como en SOMA. Sin Docker ni compilador cruzado.
pruebas-js:
	@mkdir -p build/tests
	@echo "[prueba] test_jsrun_274"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_jsrun_274.c src/apps/jsrun_core.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_jsrun_274 2>/dev/null; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/tests/test_jsrun_274; else echo "== test_jsrun_274: no compila en este equipo: SE OMITE"; fi
	CC="$(HOST_CC)" sh tests/jsrun_host_274/run.sh
	python3 tests/test_js_manifiesto_274.py
	python3 tests/test_html_manifiesto_275.py
	@echo "[prueba] test_html_275"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang -Isrc/ui -Isrc/net -Isrc/fonts -Isrc/icons -Itests tests/test_html_275.c src/apps/htmlproj.c src/apps/web_js_*.c src/apps/web_f64.c src/apps/web_regex.c src/apps/web_html.c src/apps/web_css.c src/apps/web_net.c -Wl,--unresolved-symbols=ignore-all -o build/tests/test_html_275 2>/dev/null; then ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0 ./build/tests/test_html_275; else echo "== test_html_275: no compila en este equipo: SE OMITE"; fi
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_js_275.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_js_275
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_coder_js_275
	$(HOST_CC) $(TEST_CFLAGS) tests/test_web_html_290.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_web_html_290   # SOMA_290_VOZ_CODIGO
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_web_html_290

# SOMA_277_MEMORIA: el nucleo de la memoria (memorIA portada a C) con ASan/UBSan: fechas, vectores int8, busqueda, sustitucion temporal,
# canal lexical, lectura de las respuestas del LLM, contexto del prompt y guardado por fragmentos con CRC (corrupcion y truncado). Sin Docker.
# SOMA_281_ARREGLOS: LeakSanitizer no existe en macOS (ASAN_OPTIONS=detect_leaks=1 aborta alli); como el resto de pruebas, detect_leaks=0
# SOMA_285_CHAT: la aplicacion de chat (historial, conversacion de varios turnos con el modelo, Markdown + LaTeX, y la aplicacion entera
# dibujada en el anfitrion). Capturas: ./build/tests/test_chatapp_285 CARPETA
pruebas-chat:
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) -O1 -Isrc/apps -Isrc/agent tests/test_chat_285.c src/apps/chat_core.c src/apps/chat_md.c src/apps/tex.c src/agent/llm_proto.c -o build/tests/test_chat_285
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_chat_285
	@echo "[prueba] test_chatapp_285"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DPLATFORM_X86 -DWEB_HOST_TEST -Itests -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 -Isrc/fonts -Isrc/icons tests/test_chatapp_285.c src/apps/chat_core.c src/apps/chat_md.c src/apps/tex.c src/ui/textfield.c tests/layhost/fbhost.c src/apps/web_font.c src/ui/soma_fonts.c src/agent/llm_proto.c src/core/png_enc.c -lm -o build/tests/test_chatapp_285; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_chatapp_285; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_chatapp_285: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)): SE OMITE"; fi

pruebas-memoria:
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/agent tests/test_memoria_277.c src/agent/memoria_core.c -o build/tests/test_memoria_277
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memoria_277
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/agent tests/test_llm_proto_278.c src/agent/llm_proto.c -o build/tests/test_llm_proto_278   # SOMA_278_EMBEDDINGS
	./build/tests/test_llm_proto_278
	python3 tests/test_lanzador_emb_278.py
	$(HOST_CC) $(TEST_CFLAGS) -DPLATFORM_X86 -Isrc/agent tests/test_memoria_279.c src/agent/memoria_core.c -lm -o build/tests/test_memoria_279   # SOMA_279_SUENO
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memoria_279
	python3 tests/test_exporta_280.py build/tests/f280   # SOMA_280_IMPORTAR
	$(HOST_CC) $(TEST_CFLAGS) -DPLATFORM_X86 -Isrc/agent tests/test_memoria_280.c src/agent/memoria_core.c -lm -o build/tests/test_memoria_280
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memoria_280 build/tests/f280
	$(HOST_CC) $(TEST_CFLAGS) -DPLATFORM_X86 -Isrc/agent tests/test_memoria_283.c src/agent/memoria_core.c -lm -o build/tests/test_memoria_283   # SOMA_283_MEMORIA_APP
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memoria_283
	@echo "[prueba] test_memview_283"; if $(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DPLATFORM_X86 -DWEB_HOST_TEST -Itests -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64 -Isrc/fonts -Isrc/icons tests/test_memview_283.c src/ui/textfield.c tests/layhost/fbhost.c src/apps/web_font.c src/ui/soma_fonts.c src/agent/memoria_core.c src/agent/llm_proto.c src/apps/agenda_core.c src/core/png_enc.c -lm -o build/tests/test_memview_283; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memview_283; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_memview_283: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)): SE OMITE"; fi
	@echo "[prueba] test_memoria_glue_279"; if $(HOST_CC) $(TEST_CFLAGS) -DPLATFORM_X86 tests/test_memoria_glue_279.c src/agent/memoria_core.c src/agent/llm_proto.c src/apps/agenda_core.c -o build/tests/test_memoria_glue_279; then ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_memoria_glue_279 build/tests/f280; elif [ -n "$(HOST_REF)" ]; then exit 1; else echo "== test_memoria_glue_279: no compila en este equipo ($(HOST_SO) $(HOST_MACHINE)): SE OMITE"; fi

# SOMA_267_DISCOS: el pendrive en cuatro particiones. El exFAT del nucleo frente a mkfs.exfat/fsck.exfat (exfatprogs) y a una segunda
# implementacion (EXREF=), las herramientas de tools/ (soma_disco.py, somafs.py, datos.py) frente al codigo C real del nucleo, la
# migracion de pendrives antiguos y el agrandado del primer arranque con cortes de luz. Sin Docker; RAPIDO=1 para la tanda corta.
# Lo que necesita herramientas que no estan (fsck.exfat, fsck.fat, pyfatfs) se dice y se salta.
pruebas-discos:
	@mkdir -p build/tests
	$(HOST_CC) -std=gnu11 -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Isrc/core -c src/core/exfat.c -o build/tests/exfat_nucleo.o
	$(HOST_CC) -std=gnu11 -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Isrc/core -c src/core/unidades.c -o build/tests/unidades_nucleo.o
	$(HOST_CC) -std=gnu11 -ffreestanding -mgeneral-regs-only -O2 -Wall -Wextra -Werror -Isrc/core -c src/core/discos_core.c -o build/tests/discos_nucleo.o   # SOMA_331_DISCOS
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/core tests/test_discos_331.c src/core/discos_core.c -o build/tests/test_discos_331 && ./build/tests/test_discos_331
	CC="$(HOST_CC)" python3 tests/test_exfat_267.py $(if $(filter 1,$(RAPIDO)),--rapido)
	CC="$(HOST_CC)" python3 tests/test_datos_267.py $(if $(filter 1,$(RAPIDO)),--rapido)
	CC="$(HOST_CC)" python3 tests/test_unidades_267.py $(if $(filter 1,$(RAPIDO)),--rapido)
	python3 tests/test_soma_disco_267.py $(if $(filter 1,$(RAPIDO)),--rapido)

# SOMA_264_LANG: las pruebas de SOMA-LANG y del programador sueltas (las usa el parche 264 al comprobar; make test ya las pasa)
pruebas-lang:
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lang.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_lang
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lang_264.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_lang_264
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_sim.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_sim
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_235.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_235
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_py_239.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_py_239
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lang
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_lang_264
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_coder_sim
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_coder_235
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_coder_py_239
