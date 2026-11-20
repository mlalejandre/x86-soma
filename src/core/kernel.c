#include "fb.h"
#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "idt.h"
#ifdef __x86_64__
#include "ssp_init.h"
#include "memprot.h"
#endif
#include "pmm.h"
#include "vmm.h"
#include "mem.h"
#include "thread.h"
#include "sysinfo.h"
#include "pci.h"
#include "virtio_blk.h"
#include "fs.h"
#include "boot_gate.h"
#include "llm.h"
#include "net.h"
#include "shell.h"
#include "agent.h"
#include "ramstat.h"
#include "io.h"
#include "lang.h"
#include "usermode.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#include "scrcap.h"                                  /* SOMA_179_T480S */
#ifdef __x86_64__
#include "xhci.h"
#include "gui.h"                                     /* SOMA_152 */
#include "bootfb.h"                                   /* SOMA_154_T480S */
#include "i8042.h"                                     /* SOMA_155_T480S */
#include "apic.h"                                      /* SOMA_158_T480S */
#include "fwcfg.h"                                   /* PARCHE 138 */
#include "sandbox.h"
#endif
#ifdef __aarch64__
#include "arch/aarch64/rpi_fb.h"
#ifdef PLATFORM_rpi5
#include "arch/aarch64/rp1_gpio.h"
#include "arch/aarch64/wifi.h"
void xhci2_autostart(void);
#endif
#include "sdhci.h"
#include "xhci.h"
#endif
#include "smp.h"                                     /* PARCHE 082 */
#ifdef __x86_64__
#include "usbdisk.h"
#include "sysdisk.h"
#ifdef __x86_64__
#include "bootdiag.h"           /* SOMA_228_ARRANQUE */
#endif
#include "acpi_sys.h"
#include "cpupower.h"
#include "backlight.h"                               /* SOMA_168_T480S */
#endif
#include "crypto.h"

extern uint32_t multiboot_magic;
extern uint32_t multiboot_info_addr;

#define COM1 0x3F8

static void serial_init(void)
{
#ifdef __x86_64__
    console_serial_probe();        /* SOMA_155B_T480S: sin COM1 (portatil) no se usa el puerto serie ni para leer ni para escribir */
    if (!console_serial_present()) return;
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
#endif
}

#ifdef __x86_64__
/* SOMA_PARCHE_010_BOOT: la prueba HTTP/LLM contra 192.168.1.200:8087 se ejecutaba aqui, de forma
 * sincrona, antes de dar el prompt: ~7 s de espera en cada arranque aunque no hubiera servidor
 * (el kernel no sabe si en el runner elegiste "sin LLM"). Es solo informativa (llm_test_passed no
 * lo lee nadie), asi que corre en un hilo de fondo. */
static void llm_selftest_thread(void *arg)
{
    (void)arg;
    net_run_llm_test();
}

/* PARCHE 138: autopruebas pedidas por QEMU (-fw_cfg name=opt/soma/selftest,string=sbx). Salen de QEMU con el resultado:
 * qemu_exit(0x30) = todo bien, qemu_exit(0x31) = fallo (codigos de proceso 97 y 99). */
static char g_selftest[64];
static void selftest_thread(void *arg)
{
    (void)arg;
    thread_sleep(500);                                   /* deja arrancar el escritorio: la prueba corre con el sistema vivo */
    int bad = 0;
    int any = 0;
    for (const char *p = g_selftest; *p; ++p) {                         /* lista: sbx+user */
        if (p[0] == 's' && p[1] == 'b' && p[2] == 'x') { bad += sbx_selftest(); any = 1; }
        if (p[0] == 'u' && p[1] == 's' && p[2] == 'e' && p[3] == 'r') { bad += usermode_selftest(); any = 1; }
        if (p[0] == 's' && p[1] == 'r' && p[2] == 'c') { bad += sbx_srcfstest(); any = 1; }      /* PARCHE 140 */
        if ((p[0] == 'n' && p[1] == 'e' && p[2] == 't') || (p[0] == 'w' && p[1] == 'r' && p[2] == 'e' && p[3] == 'f')) {   /* PARCHE 142 */
            static char u2[200], base[200];
            if (fwcfg_read_file("opt/soma/url", u2, sizeof u2)) {
                int k = 0, sl = 0; for (; u2[k] && k < 199; ++k) { if (u2[k] == '/' && ++sl == 3) break; base[k] = u2[k]; }
                base[k] = 0;
                if (p[0] == 'n') bad += sbx_nettest(base);
                else { static char ru[220]; int j = 0; for (; base[j]; ++j) ru[j] = base[j]; const char *t = "/ref.html"; while (*t) ru[j++] = *t++; ru[j] = 0; bad += sbx_webref(ru); }
            } else { kprint("SELFTEST net/wref: falta opt/soma/url\n"); bad++; }
            any = 1;
        }
        if (p[0] == 'w' && p[1] == 'e' && p[2] == 'b') {
            static char url[200];
            if (fwcfg_read_file("opt/soma/url", url, sizeof url)) bad += sbx_webtest(url); else { kprint("SELFTEST web: falta opt/soma/url\n"); bad++; }
            any = 1;
        }
    }
    if (!any) { kprint("SELFTEST: desconocido\n"); bad = 1; }
    qemu_exit(bad ? 0x31 : 0x30);
}
#endif

#ifdef __x86_64__
/* SOMA_159_T480S: -fw_cfg name=opt/soma/cmds,string="cpus;irq" ejecuta esas ordenes con el escritorio ya en marcha y las saca
 * por la consola (serie). Solo para las pruebas en QEMU: en un PC sin fw_cfg (el portatil) no existe la entrada. */
static char g_cmds[8192];             /* SOMA_239: 8 KiB y lineas de 1 KiB (antes 2 KiB y 127: no cabian los scripts de prueba) */
static void cmds_thread(void *arg)
{
    (void)arg;
    thread_sleep(1500);
    static char line[1024], out[4096];
    const char *p = g_cmds;
    while (*p) {
        int n = 0;
        while (*p && *p != ';' && *p != '\n' && n < (int)sizeof line - 1) line[n++] = *p++;
        line[n] = 0;
        if (*p) p++;
        if (!n) continue;
        if (line[0] == '@') { uint32_t ms = 0; for (int i = 1; line[i] >= '0' && line[i] <= '9'; i++) ms = ms * 10 + (uint32_t)(line[i] - '0'); thread_sleep(ms); continue; }
        kprint("[CMD] "); kprint(line); kprint("\n");
        out[0] = 0;
        dispatch_command(line, out, sizeof out);
        if (out[0]) { kprint(out); kprint("\n"); }
        kprint("[CMD-FIN]\n");
    }
}
#endif

/* SOMA_PARCHE_021_FPU */
#ifdef __x86_64__
#define BD(n, o) bootdiag_stage(n, o)            /* SOMA_228_ARRANQUE: etapa del arranque (pantalla, panico y memoria entre arranques) */
#else
#define BD(n, o) ((void)0)
#endif

#ifdef __x86_64__
/* SOMA_228_ARRANQUE: con el escritorio ya en marcha unos segundos, el arranque se da por bueno y se guarda el informe de hardware
 * (INFORME.TXT en la particion de arranque del pendrive y /informe.txt). soma.informe=off no lo guarda solo. */
static void boot_done_thread(void *a)
{
    (void)a;
    thread_sleep(6000);
    if (!sysdisk_capacity()) {     /* SOMA_238_GUARDADO: sin disco nada se guarda; que se vea en el escritorio, no solo al arrancar */
        extern void clip_toast_ms(const char *msg, uint32_t ms);
        kprint("AVISO: no hay disco del sistema: los cambios (WiFi, IA, escritorio, archivos) se perderan al apagar\n");
        clip_toast_ms("Aviso: SOMA no puede guardar nada en este pendrive. Mira Ajustes > Sistema.", 60000);
    }
    bootdiag_complete();
    if (!bootinfo_disabled("informe")) bootdiag_report_save_async();
}
#endif

static void run_autoexec_line(const char *line)
{
    static char out[512];
    kprint("[AUTOEXEC] "); kprint(line); kprint("\n");
    dispatch_command(line, out, sizeof out);
}

/* ======================================================================== arranque
 * SOMA_230_ARRANQUE_TABLA: kernel_main es una lista de etapas. Cada etapa es una funcion con el codigo de siempre, en el mismo
 * orden; la tabla les pone nombre y la opcion de GRUB que la salta (lo que muestran la pantalla, el panico y el informe).
 * Para anadir un driver al arranque: su funcion y una linea en BOOT_STEPS. */

/* Antes de las etapas: puerto serie, excepciones, memoria fisica y virtual, y la pantalla del firmware para ver el resto. */
static void boot_early(void)
{
    serial_init();
#ifdef __x86_64__
    kprint(ssp_source == 1 ? "SSP: canario aleatorio (RDRAND)\n" : ssp_source == 2 ? "SSP: canario aleatorio (contador de ciclos)\n" : "SSP: desactivado\n");
#endif
#ifdef __aarch64__
    rpi_fb_init();                 /* PARCHE 075: consola de texto en HDMI cuanto antes (Pi 5): el arranque se ve en pantalla */
#endif
    idt_init();
#ifdef __x86_64__
    bootinfo_parse(multiboot_magic, multiboot_info_addr);   /* SOMA_154_T480S: Multiboot 1/2: memoria, pantalla, ACPI */
#endif
    pmm_init(multiboot_magic, multiboot_info_addr);
    vmm_init();
    vmm_apply_protections();
#ifdef __x86_64__
    memprot_apply();               /* SOMA_192: el resto del espacio del nucleo, no ejecutable; pagina 0 protegida */
    bootfb_console_start();        /* SOMA_154_T480S: consola en la pantalla del firmware (portatil: sin serie ni modo texto) */
    if (bootfb_use_firmware()) { kprint("PANTALLA: framebuffer del firmware "); kprint_dec(bootfb_get()->width); kprint("x"); kprint_dec(bootfb_get()->height);
        kprint(bootinfo_efi() ? " (UEFI GOP)\n" : " (VBE)\n"); }
#endif
}

static void print_tsc(void)
{
    kprint("Reloj Hardware calibrado a "); kprint_dec((uint32_t)tsc_freq_mhz);
    kprint(" MHz ("); kprint_dec((uint32_t)tsc_ticks_per_ms); kprint(" ticks/ms)\n");
}

static void step_memory(void)
{
    kheap_init();
    klog_grow();                   /* SOMA_156_T480S: registro del nucleo de 256 KiB (orden dmesg) */
}

static void step_clock(void)
{
    thread_init();
    timer_calibrate_tsc();
    timer_init(1000);
    kprint("Hardware Timer Activo a 1000 Hz\n");
    arch_interrupts_enable();
    kprint("CPU: Interrupciones configuradas\n");
#ifndef __x86_64__
    print_tsc();
#endif
}

#ifdef __x86_64__
static void step_keyboard(void)
{
    i8042_init();                  /* SOMA_155_T480S: teclado y trackpad PS/2 (portatil) con la secuencia de Linux */
    keyboard_layout_auto();        /* SOMA_155_T480S: distribucion espanola en el T480s (soma.teclado=us|es) */
}

static void step_apic(void)        /* SOMA_158_T480S: APIC + IOAPIC + reloj por el temporizador del LAPIC (vuelve solo al PIC si algo falla) */
{
    int ar = apic_system_start();
    const struct apic_sys_info *ai = apic_system_info();
    kprint("APIC: "); kprint(ai->why);
    if (ar == 0) {
        kprint(ai->x2apic ? " | x2APIC" : " | xAPIC"); kprint(", LAPIC "); kprint_dec(ai->lapic_id);
        kprint(" | reloj: "); kprint(ai->timer_lapic ? "temporizador del LAPIC 1 kHz" : "PIT por el IOAPIC");
        kprint(" | teclado GSI "); kprint_dec(ai->gsi[1]); kprint(", trackpad GSI "); kprint_dec(ai->gsi[2]);
    }
    kprint("\n");
}

static void step_acpi(void)        /* SOMA_164_T480S: interprete ACPI (uACPI): controlador embebido, apagado S5, bateria y tapa */
{
    extern void shell_poweroff_from_button(void);
    acpi_set_power_button_action(shell_poweroff_from_button);   /* SOMA_165: el boton apaga limpiamente */
    int r = acpi_sys_init();
    const struct acpi_sys_info *ai = acpi_sys_info();
    kprint("ACPI: ");
    if (r == 0) {
        kprint("interprete activo en "); kprint_dec(ai->init_ms); kprint(" ms | EC ");
        if (ai->ec_present) { kprint(ai->ec_from_ecdt ? "por la ECDT" : "en 0x62/0x66"); } else kprint("no hay");
        kprint(" | SCI "); kprint_dec(ai->sci_irq); kprint(" (sondeo)");
        if (ai->errors || ai->warnings) { kprint(" | avisos "); kprint_dec(ai->warnings); kprint(", errores "); kprint_dec(ai->errors); }
        kprint("\n");
    } else { kprint(ai->why); if (ai->status) { kprint(": "); kprint(ai->status); } kprint(" (sigue sin ACPI)\n"); }
    print_tsc();
}
#endif

static void step_disk(void)
{
    virtio_blk_init();
#ifdef __x86_64__
    /* SOMA_162_T480S: sin disco virtio (el portatil), el disco del sistema es la particion SOMADATA del pendrive */
    int ur = usbdisk_attach();
    if (ur != 0 && !sysdisk_capacity()) { kprint("DISCO USB: "); kprint(usbdisk_info()->why); kprint(" -> los archivos solo duran hasta apagar\n"); }
    bootdiag_disk_ready();         /* SOMA_228_ARRANQUE: el arranque anterior, y desde aqui las etapas al disco */
#endif
#ifdef __aarch64__
    (void)rpi_fb_init();
#endif
}

static void step_files(void)
{
    vfs_init();
    usermode_install_programs();   /* SOMA_PARCHE_016_EXEC: /bin/ *.elf */
    bigfs_sys_probe();             /* SOMA_PARCHE_018_BIGFS */
    audio_sys_init();              /* SOMA_PARCHE_020_AUDIO */
    scrcap_init();                 /* SOMA_179_T480S: captura de pantalla (imagen, video, audio interno) */
    lang_init();
}

static void step_network(void)
{
    net_init();                    /* PARCHE 047: la red debe estar viva antes del canary (ping al gateway) */
#ifdef __x86_64__
    { extern void iwl_start(void); iwl_start(); }        /* SOMA_175A_T480S: WiFi Intel 8265 (arranca su firmware en segundo plano) */
    { extern void netmgr_start(void); netmgr_start(); }   /* SOMA_170_T480S: cable / movil: interfaz con enlace y DHCP automatico */
#endif
    boot_gate_check();
#ifndef __x86_64__
    kprint("NET: Red configurada (test automatico LLM omitido en aarch64)\n");
#endif
    /* resumen del sistema y bienvenida */
    static char dummy_fb[64];
    show_somafetch(dummy_fb, sizeof(dummy_fb));
    ramstat_print();
    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n");
    kprint(" \033[1;37mSOMA 0.3 pre-beta :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m\n");
    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n\n");
}

static void step_agent(void)
{
    agent_state_load();
    agent_refresh_knowledge();     /* MAYORDOMO_V2_KNOWLEDGE */
    console_history_load_from_vfs();
    static char init_dummy[128];
    char chk_init[16];
    if (vfs_read("/etc/init.sh", chk_init, sizeof(chk_init)) > 0) {
        kprint("\033[1;33m[INIT]: Ejecutando /etc/init.sh...\033[0m\n");
        dispatch_command("source /etc/init.sh", init_dummy, sizeof(init_dummy));
        kprint("\n");
    }
    if (bigfs_sys_autoexec(run_autoexec_line)) kprint("[AUTOEXEC] terminado\n");   /* SOMA_PARCHE_021_FPU: /autoexec.cmd del almacen grande, antes de la GUI */
    vfs_killtest_boot();                                /* PARCHE 114: solo actua si el disco lleva la marca de tools/killtest.py */
#ifdef __aarch64__
    sd_autostart();                                     /* PARCHE 061: tarjeta SD (Pi 5) */
#endif
}

static void step_usb(void)
{
#if defined(__x86_64__) || defined(__aarch64__)
    xhci_autostart();                                   /* PARCHE 048: teclado/raton USB (si hay xHCI) antes de abrir el escritorio */
#endif
#ifdef PLATFORM_rpi5
    xhci2_autostart();                                  /* PARCHE 077: segundo controlador USB del RP1 (los otros puertos de la placa) */
    if (rp1_fan_set(1) == 0) kprint("FAN: ventilador en marcha (GPIO45; 'fan off' lo para, 'fan inv' si gira al reves)\n");
    board_print();                                      /* PARCHE 079: modelo de placa; 'wifi prueba' arranca el chip Wi-Fi */
#endif
}

static void step_smp(void)
{
#if defined(__aarch64__) || defined(__x86_64__)
    smp_init();                                         /* PARCHE 082 (aarch64) / SOMA_159_T480S (x86_64): nucleos secundarios para calculo */
    if (smp_cpus() > 1) crypto_parallel = smp_parallel_n;
#endif
}

#ifdef __x86_64__
static void step_power(void)
{
    {   /* SOMA_166_T480S: energia y temperatura del procesador */
        cpupower_init();
        const struct cpupower_info *ci = cpupower_info();
        kprint("ENERGIA: perfil "); kprint(cpupower_profile_name(ci->profile));
        kprint(ci->hwp_on ? " | HWP activo (turbo hasta " : " | sin HWP");
        if (ci->hwp_on) { kprint_dec(ci->hwp_highest * 100); kprint(" MHz)"); }
        kprint(ci->mwait ? " | reposo MWAIT C" : " | reposo HLT"); if (ci->mwait) kprint_dec((uint32_t)ci->deep_c);
        kprint(ci->dts ? " | sensor de temperatura" : " | sin sensor de temperatura");
        kprint(ci->rapl ? " | medidor de consumo (RAPL)" : "");
        kprint(ci->msr_fix_ok ? " | MSR seguros: OK" : " | MSR seguros: FALLO");
        kprint(ci->msr_unknown_gp ? "\n" : " (esta CPU no avisa de los MSR inexistentes)\n");
    }
    {   /* SOMA_168_T480S: brillo del panel y teclas Fn */
        int r = backlight_init();
        struct backlight_info bi; backlight_get(&bi);
        const struct acpi_sys_info *ai = acpi_sys_info();
        kprint("BRILLO: ");
        if (r == 0) { kprint(bi.why); kprint(", "); kprint_dec(bi.pct); kprint(" %"); } else kprint(bi.why);
        kprint(" | teclas: volumen por el teclado");
        if (ai->ec_node) kprint(", consultas del EC");
        if (ai->hkey) kprint(", HKEY del ThinkPad");
        if (ai->video_bcl) kprint(", avisos de video");
        kprint("\n");
        backlight_start();
    }
}
#endif

struct boot_step { const char *name, *skip; void (*run)(void); };   /* skip: opcion de GRUB que la salta (o 0) */
static const struct boot_step BOOT_STEPS[] = {
    { "memoria del nucleo",                    0,                                                step_memory },
    { "reloj y temporizador",                  0,                                                step_clock },
#ifdef __x86_64__
    { "teclado y trackpad (PS/2)",             0,                                                step_keyboard },
    { "APIC (interrupciones)",                 "soma.apic=off",                                  step_apic },
    { "ACPI (bateria, tapa, apagado)",         "soma.acpi=off",                                  step_acpi },
#endif
    { "disco (pendrive SOMADATA)",             "soma.usbdisk=off",                               step_disk },
    { "archivos y audio",                      "soma.audio=off",                                 step_files },
    { "red, Ethernet y WiFi",                  "soma.wifi=off soma.ethernet=off",                step_network },
    { "agente y configuracion (/etc/init.sh)", 0,                                                step_agent },
    { "USB (teclado y raton USB)",             0,                                                step_usb },
    { "nucleos de la CPU (SMP)",               "soma.smp=off",                                   step_smp },
#ifdef __x86_64__
    { "energia de la CPU y brillo",            "soma.hwp=off soma.mwait=off soma.brillo=off",    step_power },
#endif
};

/* La ultima etapa: hilos de fondo y el escritorio (o la consola, en la Pi 5). No vuelve. */
static void start_desktop(void)
{
#ifdef __x86_64__
    memprot_apply();               /* SOMA_192: segunda pasada, con los dispositivos ya mapeados por los drivers */
#endif
    kprint("\033[1;32m[SOMA ready]\033[0m Sistema interactivo listo en consola serie.\n\n");
    kprint("\033[1;36m[DESKTOP]: Iniciando Entorno Grafico SOMA por defecto...\033[0m\n");
    BD("escritorio", 0);
#ifdef __x86_64__
    thread_create("llmtest", llm_selftest_thread, 0);  /* los hilos arrancan cuando kernel_main ceda la CPU */
    thread_create("arranque", boot_done_thread, 0);    /* SOMA_228_ARRANQUE */
    if (fwcfg_read_file("opt/soma/cmds", g_cmds, sizeof g_cmds - 1)) thread_create("cmds", cmds_thread, 0);   /* SOMA_159 */
    if (fwcfg_read_file("opt/soma/selftest", g_selftest, sizeof g_selftest)) {   /* PARCHE 138 */
        kprint("[SELFTEST] pedido por QEMU: "); kprint(g_selftest); kprint("\n");
        thread_create("selftest", selftest_thread, 0);
    }
#endif
#ifdef PLATFORM_rpi5
    kprint("Escritorio grafico: escribe 'vsoma' (en la Pi 5 el arranque queda en la consola serie hasta que haya teclado USB real)\n");
#else
#ifdef __x86_64__
    {   /* SOMA_152_WEB_FLUIDO: -fw_cfg name=opt/soma/openurl,string=URL abre el escritorio con el navegador en esa URL
         * (lo usa la medicion automatica de rendimiento: tools/web_bench.py). Sin la entrada, arranque normal. */
        static char openurl[256];
        uint32_t n = fwcfg_read_file("opt/soma/openurl", openurl, sizeof openurl - 1);
        while (n > 0 && (openurl[n - 1] == '\n' || openurl[n - 1] == '\r' || openurl[n - 1] == ' ' || openurl[n - 1] == 0)) n--;
        openurl[n] = 0;
        if (n) { kprint("[WEB] abriendo "); kprint(openurl); kprint("\n"); gui_open_browser(openurl); }
        else cmd_vsoma("", 0, 0);
    }
#else
    cmd_vsoma("", 0, 0);
#endif
#endif
}

void kernel_main(void)
{
    boot_early();
    for (unsigned i = 0; i < sizeof BOOT_STEPS / sizeof BOOT_STEPS[0]; i++) {
        BD(BOOT_STEPS[i].name, BOOT_STEPS[i].skip);
        BOOT_STEPS[i].run();
    }
    start_desktop();
    kprint("\033[1;32m[SOMA ready]\033[0m Modo texto activado.\n\n");
    shell_run();
}
