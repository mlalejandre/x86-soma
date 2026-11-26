/* shell_sistema.c: informacion del sistema y del hardware: somafetch, status, hw, apic, ahci, nvme, gpio, wifi, tls, placa, cpus, dhcp, red, netif.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
static int cmd_somafetch(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    show_somafetch(out_buf, max_out);
    return 1;
}

static int cmd_creador(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("SOMA (Sistema Operativo Multi-Agente) - Creado por Marcos\n");
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "SOMA - Creado por Marcos");
    return 1;
}

/* PARCHE_FEEDBACK_V1: status con datos al agente */
static int cmd_status(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    kprint("ARCH: "); kprint(MYOS_ARCH_NAME);
    kprint(" (HAL v1) | IP: "); kprint_ip(net_ip);
    kprint(" | GW: "); kprint_ip(net_gateway);
    kprint(" | MAC: "); kprint_mac(net_mac);
    kprint("\n");
    uint32_t p = 0;
    soma_str_put(out_buf, max_out, &p, "ARCH: ");
    soma_str_put(out_buf, max_out, &p, MYOS_ARCH_NAME);
    soma_str_put(out_buf, max_out, &p, " | Red: "); soma_str_put(out_buf, max_out, &p, net_describe()); soma_str_put(out_buf, max_out, &p, " | Disco: "); soma_str_put(out_buf, max_out, &p, sysdisk_describe());
    return 1;
}

/* PARCHE_FEEDBACK_V1: uptime real al agente */
/* PARCHE 032: "hw" describe el hardware (ACPI en x86, PCI en ambos). Solo lectura. */
/* SOMA_156_T480S: el acceso a memoria fisica (antes hw_map_identity, solo el primer GiB) esta en hwscan.c: hw_map_phys */

__attribute__((unused)) static const char *hw_pci_class(uint8_t c, uint8_t sc)
{
    switch (c) {
    case 0x01: return sc == 0x06 ? "SATA" : sc == 0x08 ? "NVMe" : sc == 0x00 ? "SCSI/virtio-blk" : "almacenamiento";
    case 0x02: return "red";
    case 0x03: return "video";
    case 0x04: return sc == 0x03 ? "audio HDA" : "multimedia";
    case 0x06: return sc == 0x00 ? "host bridge" : sc == 0x01 ? "ISA bridge" : "bridge";
    case 0x0C: return sc == 0x03 ? "USB" : "bus serie";
    default:   return "otro";
    }
}

#if defined(__aarch64__)
extern uint64_t soma_dtb_addr;                 /* boot.s: x0 al arrancar */
static int hw_dtb_ok(const uint8_t *c)
{
    if (!(c[0] == 0xD0 && c[1] == 0x0D && c[2] == 0xFE && c[3] == 0xED)) return 0;
    uint32_t ts = (uint32_t)c[4] << 24 | (uint32_t)c[5] << 16 | (uint32_t)c[6] << 8 | c[7];
    uint32_t ver = (uint32_t)c[20] << 24 | (uint32_t)c[21] << 16 | (uint32_t)c[22] << 8 | c[23];
    return ts >= 64 && ts <= 0x100000 && ver >= 16 && ver <= 17;
}

/* x0 del cargador; si QEMU no lo entrega al arrancar un ELF, el DTB queda en la RAM (normalmente en su base):
 * se busca a 4 KiB de paso en los primeros 192 MiB (RAM de QEMU virt, accesible con la MMU apagada). */
static const uint8_t *hw_dtb(void)
{
    const uint8_t *c = (const uint8_t *)(uintptr_t)soma_dtb_addr;
    if (soma_dtb_addr >= 0x40000000ULL && soma_dtb_addr < 0x40000000ULL + (192ULL << 20) && hw_dtb_ok(c)) return c;
    for (uint64_t a = 0x40000000ULL; a < 0x40000000ULL + (192ULL << 20); a += 4096) {
        c = (const uint8_t *)(uintptr_t)a;
        if (hw_dtb_ok(c)) return c;
    }
    return 0;
}
#endif

/* Capacidades PCI: PM, MSI, MSI-X, PCIe (lista enlazada en el espacio de configuracion; recorrido acotado). */
__attribute__((unused)) static void hw_pci_caps(char *b, uint32_t max, uint32_t *p, uint8_t bus, uint8_t slot, uint8_t fn)
{
    if (!(pci_config_read32(bus, slot, fn, 0x04) & 0x00100000u)) return;      /* estado bit 4: hay lista de capacidades */
    uint8_t ptr = (uint8_t)(pci_config_read32(bus, slot, fn, 0x34) & 0xFC);
    soma_str_put(b, max, p, "       caps:");
    for (int i = 0; i < 24 && ptr >= 0x40; i++) {
        uint32_t h = pci_config_read32(bus, slot, fn, ptr);
        uint8_t id = (uint8_t)h;
        const char *n = id == 0x01 ? "PM" : id == 0x05 ? "MSI" : id == 0x09 ? "vendor" : id == 0x10 ? "PCIe" : id == 0x11 ? "MSI-X" : 0;
        soma_str_putc(b, max, p, ' ');
        if (n) soma_str_put(b, max, p, n); else { soma_str_put(b, max, p, "0x"); soma_str_put_hex(b, max, p, id, 4, 1); }
        ptr = (uint8_t)((h >> 8) & 0xFC);
    }
    soma_str_putc(b, max, p, '\n');
    if (pci_vendor_id(bus, slot, fn) == 0x1AF4) {                           /* PARCHE 034: estructuras virtio-pci */
        uint8_t cfg[256];
        for (int i = 0; i < 64; i++) { uint32_t w = pci_config_read32(bus, slot, fn, (uint8_t)(i * 4)); cfg[i * 4] = (uint8_t)w; cfg[i * 4 + 1] = (uint8_t)(w >> 8); cfg[i * 4 + 2] = (uint8_t)(w >> 16); cfg[i * 4 + 3] = (uint8_t)(w >> 24); }
        struct hw_virtio_cap vc[8]; int nv = hw_virtio_caps(cfg, vc, 8);
        static const char *const vn[] = { "?", "common", "notify", "isr", "device", "pcicfg" };
        for (int i = 0; i < nv; i++) {
            soma_str_put(b, max, p, "       virtio "); soma_str_put(b, max, p, vn[vc[i].type]); soma_str_put(b, max, p, ": BAR"); soma_str_put_dec(b, max, p, vc[i].bar);
            soma_str_put(b, max, p, " +0x"); soma_str_put_hex(b, max, p, vc[i].offset, 8, 1); soma_str_put(b, max, p, " len "); soma_str_put_dec(b, max, p, vc[i].length);
            if (vc[i].type == HW_VIRTIO_NOTIFY) { soma_str_put(b, max, p, " mult "); soma_str_put_dec(b, max, p, vc[i].notify_mult); }
            soma_str_putc(b, max, p, '\n');
        }
    }
}

static int cmd_hw(const char *args, char *out_buf, uint32_t max_out)
{
    while (args && *args == ' ') args++;
    int verbose = args && args[0] == '-' && args[1] == 'v';
    (void)verbose;
    static char b[6144];
    uint32_t p = 0;
#if defined(__x86_64__)
    /* SOMA_156_T480S: secciones para fotografiar en el portatil: hw cpu | mem [-v] | smbios | acpi | pci [-v] | todo.
     * Sin argumentos: resumen (equipo, CPU, RAM, pantalla) + ACPI + PCI, como antes. */
    int only_acpi = args && args[0] == 'a' && args[1] == 'c';
    int only_pci = args && args[0] == 'p' && args[1] == 'c';
    if (only_pci && args[2] == 'i' && args[3] == ' ' && args[4] == '-' && args[5] == 'v') verbose = 1;
    if (args && *args && !only_acpi && !only_pci && !verbose) {
        static char sb[8192];
        int todo = args[0] == 't' && args[1] == 'o' && args[2] == 'd';
        if (todo) {
            uint32_t q = 0; const char *secs[] = { "smbios", "cpu", "mem" };
            for (int i = 0; i < 3; i++) { hw_section_text(secs[i], sb + q, sizeof sb - q); while (sb[q]) q++; }
        } else if (!hw_section_text(args, sb, sizeof sb)) {
            kprint("Uso: hw [cpu | mem [-v] | smbios | acpi | pci [-v] | todo]\n");
            return 1;
        }
        kprint(sb);
        if (!todo) { if (out_buf && max_out) out_buf[0] = 0; return 1; }
        only_acpi = 0; only_pci = 0;
    } else if (!only_acpi && !only_pci) {
        static char sb[2048];
        hw_section_text("resumen", sb, sizeof sb);
        soma_str_put(b, sizeof b, &p, sb);
    }
    uint64_t rsdp = hw_find_rsdp();    /* SOMA_156_T480S: tablas por encima de 1 GiB accesibles (UEFI) */
    static struct hw_acpi a;
    if (only_pci) { /* nada */ } else
    if (rsdp && hw_acpi_parse(rsdp, hw_map_phys, 0, &a) == 0) {
        soma_str_put(b, sizeof b, &p, "ACPI: rev "); soma_str_put_dec(b, sizeof b, &p, a.revision);
        soma_str_put(b, sizeof b, &p, ", OEM "); soma_str_put(b, sizeof b, &p, a.oem);
        soma_str_put(b, sizeof b, &p, ", tablas:");
        for (int i = 0; i < a.ntables && i < 32; i++) { soma_str_putc(b, sizeof b, &p, ' '); soma_str_put(b, sizeof b, &p, a.sig[i]); }
        soma_str_put(b, sizeof b, &p, "\nCPU: "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)a.ncpu);
        soma_str_put(b, sizeof b, &p, " | LAPIC 0x"); soma_str_put_hex(b, sizeof b, &p, a.lapic_addr, 8, 1);
        for (int i = 0; i < a.nioapic; i++) { soma_str_put(b, sizeof b, &p, " | IOAPIC 0x"); soma_str_put_hex(b, sizeof b, &p, a.ioapic_addr[i], 8, 1); }
        for (int i = 0; i < a.nmcfg; i++) {
            soma_str_put(b, sizeof b, &p, "\nPCIe ECAM 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(a.mcfg[i].base >> 32), 8, 1);
            soma_str_put_hex(b, sizeof b, &p, (uint32_t)a.mcfg[i].base, 8, 1);
            soma_str_put(b, sizeof b, &p, " buses "); soma_str_put_dec(b, sizeof b, &p, a.mcfg[i].bus0); soma_str_putc(b, sizeof b, &p, '-'); soma_str_put_dec(b, sizeof b, &p, a.mcfg[i].bus1);
        }
        for (int i = 0; i < a.nirq_ovr; i++) {
            soma_str_put(b, sizeof b, &p, "\nIRQ "); soma_str_put_dec(b, sizeof b, &p, a.irq_ovr[i].src); soma_str_put(b, sizeof b, &p, " -> GSI "); soma_str_put_dec(b, sizeof b, &p, a.irq_ovr[i].gsi);
        }
        soma_str_put(b, sizeof b, &p, a.has_hpet ? "\nHPET: si\n" : "\nHPET: no\n");
    } else {
        soma_str_put(b, sizeof b, &p, "ACPI: no disponible (arranque sin tablas ACPI)\n");
    }
    if (only_acpi) goto hw_done;
#else
    {
        const uint8_t *dtb = hw_dtb();
        static struct hw_fdt f;
        if (dtb && hw_fdt_parse(dtb, 0x100000, &f) == 0) {
            soma_str_put(b, sizeof b, &p, "FDT: "); soma_str_put(b, sizeof b, &p, f.model[0] ? f.model : "(sin modelo)");
            soma_str_put(b, sizeof b, &p, " [");  soma_str_put(b, sizeof b, &p, f.compat); soma_str_put(b, sizeof b, &p, "]\nCPU: ");
            soma_str_put_dec(b, sizeof b, &p, (uint64_t)f.ncpu); soma_str_put(b, sizeof b, &p, " | nodos: "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)f.nnodes);
            for (int i = 0; i < f.nmem; i++) {
                soma_str_put(b, sizeof b, &p, "\nRAM 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(f.mem_base[i] >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)f.mem_base[i], 8, 1);
                soma_str_put(b, sizeof b, &p, " + "); soma_str_put_dec(b, sizeof b, &p, f.mem_size[i] >> 20); soma_str_put(b, sizeof b, &p, " MiB");
            }
            static const char *const dev[][2] = { { "arm,pl011", "UART PL011" }, { "arm,pl031", "RTC PL031" }, { "arm,cortex-a15-gic", "GICv2" }, { "arm,gic-400", "GICv2" }, { "arm,gic-v3", "GICv3" }, { "virtio,mmio", "virtio-mmio" }, { "pci-host-ecam-generic", "PCIe ECAM" } };
            for (unsigned i = 0; i < sizeof dev / sizeof dev[0]; i++) {
                uint64_t base, size;
                if (hw_fdt_find_reg(dtb, 0x100000, dev[i][0], &base, &size) == 0) {
                    soma_str_putc(b, sizeof b, &p, '\n'); soma_str_put(b, sizeof b, &p, dev[i][1]); soma_str_put(b, sizeof b, &p, " 0x");
                    soma_str_put_hex(b, sizeof b, &p, (uint32_t)(base >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)base, 8, 1);
                }
            }
            soma_str_putc(b, sizeof b, &p, '\n');
        } else {
            soma_str_put(b, sizeof b, &p, "FDT: no se encontro un arbol de dispositivos valido (x0 = 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(soma_dtb_addr >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)soma_dtb_addr, 8, 1); soma_str_put(b, sizeof b, &p, ")\n");
        }
    }
#endif
#if defined(__x86_64__)
    soma_str_put(b, sizeof b, &p, "PCI:\n");
    int n = 0;
    for (int bus = 0; bus < 256 && n < 40; bus++)
        for (int slot = 0; slot < 32 && n < 40; slot++) {
            uint16_t v0 = pci_vendor_id((uint8_t)bus, (uint8_t)slot, 0);
            if (v0 == 0xFFFF) continue;
            int multi = (int)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0x80);
            for (int fn = 0; fn < (multi ? 8 : 1) && n < 40; fn++) {
                uint16_t v = pci_vendor_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
                if (v == 0xFFFF) continue;
                uint32_t cc = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)fn, 0x08);
                soma_str_put(b, sizeof b, &p, "  "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)bus); soma_str_putc(b, sizeof b, &p, ':');
                soma_str_put_dec(b, sizeof b, &p, (uint64_t)slot); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_dec(b, sizeof b, &p, (uint64_t)fn);
                soma_str_putc(b, sizeof b, &p, ' '); soma_str_put_hex(b, sizeof b, &p, v, 4, 1); soma_str_putc(b, sizeof b, &p, ':');
                soma_str_put_hex(b, sizeof b, &p, pci_device_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn), 4, 1);
                soma_str_putc(b, sizeof b, &p, ' '); soma_str_put(b, sizeof b, &p, hw_pci_class((uint8_t)(cc >> 24), (uint8_t)(cc >> 16)));
                {   /* SOMA_156_T480S: fabricante y nombre de lo que lleva el portatil */
                    uint16_t dv = pci_device_id((uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
                    const char *vn = hw_vendor_name(v), *dn = hw_pci_name(v, dv);
                    if (vn || dn) { soma_str_put(b, sizeof b, &p, " - "); if (vn) soma_str_put(b, sizeof b, &p, vn); if (dn) { if (vn) soma_str_putc(b, sizeof b, &p, ' '); soma_str_put(b, sizeof b, &p, dn); } }
                }
                soma_str_putc(b, sizeof b, &p, '\n'); n++;
                if (verbose) hw_pci_caps(b, sizeof b, &p, (uint8_t)bus, (uint8_t)slot, (uint8_t)fn);
            }
        }
    if (n == 0) soma_str_put(b, sizeof b, &p, "  (ningun dispositivo)\n");
hw_done:
#else
    soma_str_put(b, sizeof b, &p, "PCI: no usado en aarch64 (los dispositivos son virtio-mmio)\n");     /* su ECAM no esta mapeada en QEMU virt */
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 037: "apic" muestra el estado del LAPIC/IOAPIC (solo lectura; no habilita ni enruta nada). */
static int cmd_apic(const char *args, char *out_buf, uint32_t max_out)
{
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    int do_route = 0, route_test = 0, route_off = 0;                  /* PARCHE 039 */
    (void)do_test; (void)do_route; (void)route_test; (void)route_off;      /* en aarch64 no hay APIC */
    if (args && args[0] == 'r' && args[1] == 'o' && args[2] == 'u' && args[3] == 't' && args[4] == 'e') {
        const char *w = args + 5; while (*w == ' ') w++;
        do_route = 1; route_test = (w[0] == 't'); route_off = (w[0] == 'o' && w[1] == 'f');
    }
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__)
    uint64_t rsdp = hw_find_rsdp();    /* SOMA_156_T480S (antes solo el primer GiB) */
    static struct hw_acpi a; static struct apic_info ai;
    if (!rsdp || hw_acpi_parse(rsdp, hw_map_phys, 0, &a) != 0 || !a.has_madt) {
        soma_str_put(b, sizeof b, &p, "APIC: sin tabla MADT\n");
    } else if (apic_probe(a.lapic_addr, a.nioapic ? a.ioapic_addr[0] : 0, &ai) != 0) {
        soma_str_put(b, sizeof b, &p, "APIC: no se pudo leer el LAPIC\n");
    } else {
        soma_str_put(b, sizeof b, &p, "IA32_APIC_BASE 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(ai.msr_base >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)ai.msr_base, 8, 1);
        soma_str_put(b, sizeof b, &p, (ai.msr_base & 0x800) ? " (habilitado)" : " (DESHABILITADO)"); soma_str_put(b, sizeof b, &p, (ai.msr_base & 0x100) ? " BSP\n" : "\n");
        soma_str_put(b, sizeof b, &p, "LAPIC id "); soma_str_put_dec(b, sizeof b, &p, ai.lapic_id); soma_str_put(b, sizeof b, &p, " ver 0x"); soma_str_put_hex(b, sizeof b, &p, (uint16_t)ai.lapic_ver, 4, 1);
        soma_str_put(b, sizeof b, &p, " SVR 0x"); soma_str_put_hex(b, sizeof b, &p, (uint16_t)ai.lapic_svr, 4, 1); soma_str_put(b, sizeof b, &p, (ai.lapic_svr & 0x100) ? " (software: ON)" : " (software: OFF)");
        soma_str_put(b, sizeof b, &p, " TPR "); soma_str_put_dec(b, sizeof b, &p, ai.lapic_tpr & 0xFF); soma_str_putc(b, sizeof b, &p, '\n');
        if (ai.ioapic_ok) {
            soma_str_put(b, sizeof b, &p, "IOAPIC id "); soma_str_put_dec(b, sizeof b, &p, ai.io_id); soma_str_put(b, sizeof b, &p, " ver 0x"); soma_str_put_hex(b, sizeof b, &p, (uint16_t)ai.io_ver, 4, 1);
            soma_str_put(b, sizeof b, &p, ", "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)ai.nredir); soma_str_put(b, sizeof b, &p, " entradas\n");
            for (int i = 0; i < ai.nredir; i++) {
                uint32_t lo = (uint32_t)ai.redir[i];
                soma_str_put(b, sizeof b, &p, "  GSI "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)i); soma_str_put(b, sizeof b, &p, ": vec 0x"); soma_str_put_hex(b, sizeof b, &p, (uint16_t)(lo & 0xFF), 4, 1);
                soma_str_put(b, sizeof b, &p, (lo & 0x10000) ? " enmascarada\n" : " ACTIVA\n");
            }
        } else soma_str_put(b, sizeof b, &p, "IOAPIC: no leido\n");
        if (do_test) {
            static struct apic_test at;
            int r = apic_timer_selftest(&at);
            soma_str_put(b, sizeof b, &p, "Autotest temporizador LAPIC: ");
            soma_str_put(b, sizeof b, &p, r == 0 ? "OK" : r == -2 ? "FALLO (interrupciones desactivadas)" : r == -3 ? "FALLO (el PIT no avanza)" : r == -4 ? "FALLO (calibracion inverosimil)" : r == -5 ? "FALLO (la interrupcion no llego)" : "FALLO");
            soma_str_put(b, sizeof b, &p, " | ticks/ms (div 16): "); soma_str_put_dec(b, sizeof b, &p, at.ticks_per_ms);
            soma_str_put(b, sizeof b, &p, " | espera: "); soma_str_put_dec(b, sizeof b, &p, at.irq_wait_ms); soma_str_put(b, sizeof b, &p, " ms | IRQ: "); soma_str_put_dec(b, sizeof b, &p, at.irqs); soma_str_putc(b, sizeof b, &p, '\n');
        } else if (do_route) {
            static struct apic_route_cfg rc;
            rc.lapic_id = (int)ai.lapic_id;
            for (int i = 0; i < 2; i++) {
                rc.gsi[i] = i; rc.low[i] = 0; rc.level[i] = 0;                 /* ISA por defecto: GSI = IRQ, flanco, activo alto */
                for (int k = 0; k < a.nirq_ovr; k++)
                    if (a.irq_ovr[k].src == i) { rc.gsi[i] = (int)a.irq_ovr[k].gsi; rc.low[i] = (a.irq_ovr[k].flags & 3) == 3; rc.level[i] = ((a.irq_ovr[k].flags >> 2) & 3) == 3; }
                if (rc.gsi[i] >= ai.nredir) rc.gsi[i] = -1;
            }
            if (rc.gsi[0] < 0 || rc.gsi[1] < 0) soma_str_put(b, sizeof b, &p, "route: GSI fuera de la tabla del IOAPIC\n");
            else if (route_off) { apic_route_off(); soma_str_put(b, sizeof b, &p, "route: desactivado (IRQ0/IRQ1 vuelven al PIC)\n"); }
            else {
                int r = apic_route_on(&rc);
                if (r == -2) soma_str_put(b, sizeof b, &p, "route: ya estaba activo (usa 'apic route off')\n");
                else if (r != 0) soma_str_put(b, sizeof b, &p, "route: fallo al activar\n");
                else if (route_test) {
                    uint64_t t0 = timer_get_uptime_ms(); unsigned long guard = 400000000UL;
                    while (timer_get_uptime_ms() - t0 < 50 && --guard) __asm__ volatile ("pause");
                    uint32_t eoi = apic_eoi_count(); uint64_t adv = timer_get_uptime_ms() - t0;
                    apic_route_off();
                    soma_str_put(b, sizeof b, &p, "Autotest enrutado IOAPIC (PIT por GSI "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)rc.gsi[0]); soma_str_put(b, sizeof b, &p, "): ");
                    soma_str_put(b, sizeof b, &p, (eoi >= 30 && adv >= 30) ? "OK" : "FALLO"); soma_str_put(b, sizeof b, &p, " | ticks en 50 ms de espera: "); soma_str_put_dec(b, sizeof b, &p, adv);
                    soma_str_put(b, sizeof b, &p, " | IRQ entregadas por el LAPIC: "); soma_str_put_dec(b, sizeof b, &p, eoi); soma_str_put(b, sizeof b, &p, " | revertido al PIC\n");
                } else soma_str_put(b, sizeof b, &p, "route: ACTIVO (PIT y teclado por IOAPIC). 'apic route off' lo revierte\n");
            }
        } else soma_str_put(b, sizeof b, &p, "(usa 'apic test' | 'apic route test|on|off')\n");
    }
#else
    soma_str_put(b, sizeof b, &p, "apic: solo disponible en x86_64 (aarch64 usa GIC)\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 040: "ahci" detecta el controlador SATA, identifica el primer disco y lee su sector 0 (solo lectura, opt-in). */
static int cmd_ahci(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    (void)do_test;
    int do_read = args && args[0] == 'r' && args[1] == 'e' && args[2] == 'a' && args[3] == 'd';
    uint64_t rd_lba = 0;
    if (do_read) { const char *w = args + 4; while (*w == ' ') w++; while (*w >= '0' && *w <= '9') rd_lba = rd_lba * 10 + (uint64_t)(*w++ - '0'); }
    uint32_t p = 0;
#if defined(__x86_64__)
    static struct ahci_info ai;
    int r = ahci_probe(&ai);
    if (ai.found) {
        soma_str_put(b, sizeof b, &p, "AHCI en PCI "); soma_str_put_dec(b, sizeof b, &p, ai.bus); soma_str_putc(b, sizeof b, &p, ':'); soma_str_put_dec(b, sizeof b, &p, ai.slot); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_dec(b, sizeof b, &p, ai.fn);
        soma_str_put(b, sizeof b, &p, " ABAR 0x"); soma_str_put_hex(b, sizeof b, &p, ai.abar, 8, 1); soma_str_put(b, sizeof b, &p, " version 0x"); soma_str_put_hex(b, sizeof b, &p, ai.vs, 8, 1);
        soma_str_put(b, sizeof b, &p, " puertos implementados: "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)ai.nports_impl); soma_str_put(b, sizeof b, &p, " | CAP: ranuras "); soma_str_put_dec(b, sizeof b, &p, ((ai.cap >> 8) & 0x1F) + 1);
        soma_str_put(b, sizeof b, &p, (ai.cap >> 31) ? ", 64 bits\n" : ", 32 bits\n");
        for (int n = 0; n < 32; n++)
            if (ai.pi & (1u << n)) { soma_str_put(b, sizeof b, &p, "  puerto "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)n); soma_str_put(b, sizeof b, &p, ": "); soma_str_put(b, sizeof b, &p, ai.port_state[n] == 1 ? "disco ATA" : ai.port_state[n] == 2 ? "ATAPI (optico)" : ai.port_state[n] == 3 ? "otro" : "sin dispositivo"); soma_str_putc(b, sizeof b, &p, '\n'); }
    }
    if (r == 0) {
        soma_str_put(b, sizeof b, &p, "Disco (puerto "); soma_str_put_dec(b, sizeof b, &p, (uint64_t)ai.port); soma_str_put(b, sizeof b, &p, "): "); soma_str_put(b, sizeof b, &p, ai.model);
        soma_str_put(b, sizeof b, &p, " | "); soma_str_put_dec(b, sizeof b, &p, ai.sectors / 2048); soma_str_put(b, sizeof b, &p, " MiB"); soma_str_put(b, sizeof b, &p, ai.lba48 ? " | LBA48" : " | LBA28");
        soma_str_put(b, sizeof b, &p, "\nSector 0 leido por DMA: ");
        for (int i = 0; i < 16; i++) soma_str_putc(b, sizeof b, &p, (ai.sector0[i] >= 32 && ai.sector0[i] < 127) ? (char)ai.sector0[i] : '.');
        soma_str_put(b, sizeof b, &p, "\nAHCI: OK\n");
        if (do_test) {
            static struct ahci_selftest st;
            int tr = ahci_selftest(&st);
            soma_str_put(b, sizeof b, &p, "Autotest E/S: ");
            if (!st.allowed) soma_str_put(b, sizeof b, &p, "OMITIDO (solo se escribe en el disco de prueba QEMU con firma SOMASATA)\n");
            else {
                soma_str_put(b, sizeof b, &p, tr == 0 ? "OK" : "FALLO");
                soma_str_put(b, sizeof b, &p, " | escritura "); soma_str_put(b, sizeof b, &p, st.wrote_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", verificacion "); soma_str_put(b, sizeof b, &p, st.verify_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", restauracion "); soma_str_put(b, sizeof b, &p, st.restore_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", sector 0 intacto "); soma_str_put(b, sizeof b, &p, st.sector0_ok ? "si" : "NO");
                soma_str_put(b, sizeof b, &p, "\nLectura secuencial: "); soma_str_put_dec(b, sizeof b, &p, st.read_kib); soma_str_put(b, sizeof b, &p, " KiB en "); soma_str_put_dec(b, sizeof b, &p, st.read_ms); soma_str_put(b, sizeof b, &p, " ms");
                if (tr != 0) { soma_str_put(b, sizeof b, &p, " | "); soma_str_put(b, sizeof b, &p, ahci_err_str(st.err)); }
                soma_str_putc(b, sizeof b, &p, '\n');
            }
        } else if (do_read) {
            static uint8_t sec[512];
            int rr = ahci_read(rd_lba, 1, sec);
            if (rr != 0) { soma_str_put(b, sizeof b, &p, "read: "); soma_str_put(b, sizeof b, &p, ahci_err_str(rr)); soma_str_putc(b, sizeof b, &p, '\n'); }
            else {
                const char hx[] = "0123456789ABCDEF";
                for (int row = 0; row < 4; row++) {
                    soma_str_put(b, sizeof b, &p, "  LBA "); soma_str_put_dec(b, sizeof b, &p, rd_lba); soma_str_putc(b, sizeof b, &p, '+'); soma_str_put_dec(b, sizeof b, &p, (uint64_t)row * 16); soma_str_put(b, sizeof b, &p, ": ");
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, hx[v >> 4]); soma_str_putc(b, sizeof b, &p, hx[v & 15]); soma_str_putc(b, sizeof b, &p, ' '); }
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                    soma_str_putc(b, sizeof b, &p, '\n');
                }
            }
        } else soma_str_put(b, sizeof b, &p, "(usa 'ahci test' | 'ahci read <lba>')\n");
    } else {
        soma_str_put(b, sizeof b, &p, "AHCI: FALLO - "); soma_str_put(b, sizeof b, &p, ahci_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n');
    }
#else
    soma_str_put(b, sizeof b, &p, "ahci: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 043: "nvme" inicializa el controlador NVMe, identifica el disco y lee su sector 0; nvme test | nvme read <lba>. */
static int cmd_nvme(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__)
    int do_test = args && args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't';
    int do_read = args && args[0] == 'r' && args[1] == 'e' && args[2] == 'a' && args[3] == 'd';
    uint64_t rd_lba = 0;
    if (do_read) { const char *w = args + 4; while (*w == ' ') w++; while (*w >= '0' && *w <= '9') rd_lba = rd_lba * 10 + (uint64_t)(*w++ - '0'); }
    static struct nvme_info ni;
    int r = nvme_probe(&ni);
    if (ni.found) {
        soma_str_put(b, sizeof b, &p, "NVMe en PCI "); soma_str_put_dec(b, sizeof b, &p, ni.bus); soma_str_putc(b, sizeof b, &p, ':'); soma_str_put_dec(b, sizeof b, &p, ni.slot); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_dec(b, sizeof b, &p, ni.fn);
        soma_str_put(b, sizeof b, &p, " BAR0 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(ni.bar >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)ni.bar, 8, 1);
        soma_str_put(b, sizeof b, &p, " version 0x"); soma_str_put_hex(b, sizeof b, &p, ni.vs, 8, 1); soma_str_put(b, sizeof b, &p, " | colas hasta "); soma_str_put_dec(b, sizeof b, &p, ni.mqes); soma_str_putc(b, sizeof b, &p, '\n');
    }
    if (r == 0) {
        soma_str_put(b, sizeof b, &p, "Disco: "); soma_str_put(b, sizeof b, &p, ni.model); soma_str_put(b, sizeof b, &p, " (serie "); soma_str_put(b, sizeof b, &p, ni.serial); soma_str_put(b, sizeof b, &p, ") | ");
        soma_str_put_dec(b, sizeof b, &p, ni.sectors / 2048); soma_str_put(b, sizeof b, &p, " MiB | sector de "); soma_str_put_dec(b, sizeof b, &p, ni.lba_size);
        soma_str_put(b, sizeof b, &p, "\nSector 0 leido por DMA: ");
        for (int i = 0; i < 16; i++) soma_str_putc(b, sizeof b, &p, (ni.sector0[i] >= 32 && ni.sector0[i] < 127) ? (char)ni.sector0[i] : '.');
        soma_str_put(b, sizeof b, &p, "\nNVMe: OK\n");
        if (do_test) {
            static struct nvme_selftest st;
            int tr = nvme_selftest(&st);
            soma_str_put(b, sizeof b, &p, "Autotest E/S: ");
            if (!st.allowed) soma_str_put(b, sizeof b, &p, "OMITIDO (solo se escribe en el disco de prueba QEMU con firma SOMANVME)\n");
            else {
                soma_str_put(b, sizeof b, &p, tr == 0 ? "OK" : "FALLO");
                soma_str_put(b, sizeof b, &p, " | escritura "); soma_str_put(b, sizeof b, &p, st.wrote_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", verificacion "); soma_str_put(b, sizeof b, &p, st.verify_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", restauracion "); soma_str_put(b, sizeof b, &p, st.restore_ok ? "ok" : "NO");
                soma_str_put(b, sizeof b, &p, ", sector 0 intacto "); soma_str_put(b, sizeof b, &p, st.sector0_ok ? "si" : "NO");
                soma_str_put(b, sizeof b, &p, "\nLectura secuencial: "); soma_str_put_dec(b, sizeof b, &p, st.read_kib); soma_str_put(b, sizeof b, &p, " KiB en "); soma_str_put_dec(b, sizeof b, &p, st.read_ms); soma_str_put(b, sizeof b, &p, " ms");
                if (tr != 0) { soma_str_put(b, sizeof b, &p, " | "); soma_str_put(b, sizeof b, &p, nvme_err_str(st.err)); }
                soma_str_putc(b, sizeof b, &p, '\n');
            }
        } else if (do_read) {
            static uint8_t sec[512];
            int rr = nvme_read(rd_lba, 1, sec);
            if (rr != 0) { soma_str_put(b, sizeof b, &p, "read: "); soma_str_put(b, sizeof b, &p, nvme_err_str(rr)); soma_str_putc(b, sizeof b, &p, '\n'); }
            else {
                const char hx[] = "0123456789ABCDEF";
                for (int row = 0; row < 4; row++) {
                    soma_str_put(b, sizeof b, &p, "  LBA "); soma_str_put_dec(b, sizeof b, &p, rd_lba); soma_str_putc(b, sizeof b, &p, '+'); soma_str_put_dec(b, sizeof b, &p, (uint64_t)row * 16); soma_str_put(b, sizeof b, &p, ": ");
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, hx[v >> 4]); soma_str_putc(b, sizeof b, &p, hx[v & 15]); soma_str_putc(b, sizeof b, &p, ' '); }
                    for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                    soma_str_putc(b, sizeof b, &p, '\n');
                }
            }
        } else soma_str_put(b, sizeof b, &p, "(usa 'nvme test' | 'nvme read <lba>')\n");
    } else {
        soma_str_put(b, sizeof b, &p, "NVMe: FALLO - "); soma_str_put(b, sizeof b, &p, nvme_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n');
    }
#else
    (void)args;
    soma_str_put(b, sizeof b, &p, "nvme: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 046: "usb" inicializa el controlador xHCI (USB 3), reinicia los puertos con dispositivo y lee sus descriptores. */
#if defined(__x86_64__) || defined(__aarch64__)
static void usb_hid_print(char *b, uint32_t m, uint32_t *p, const struct xhci_hid_status *h)
{
    soma_str_put(b, m, p, "USB HID: "); soma_str_put(b, m, p, h->active ? "ACTIVO" : "inactivo");
    soma_str_put(b, m, p, " | teclados "); soma_str_put_dec(b, m, p, h->nkbd); soma_str_put(b, m, p, " | ratones "); soma_str_put_dec(b, m, p, h->nmouse);
    soma_str_put(b, m, p, " | informes "); soma_str_put_dec(b, m, p, h->reports); soma_str_put(b, m, p, " | scancodes "); soma_str_put_dec(b, m, p, h->keys);
    soma_str_put(b, m, p, " | movimientos de raton "); soma_str_put_dec(b, m, p, h->mouse_ev); soma_str_put(b, m, p, " | errores "); soma_str_put_dec(b, m, p, h->errors); soma_str_put(b, m, p, " | recuperados "); soma_str_put_dec(b, m, p, h->recovered);
    soma_str_putc(b, m, p, '\n');
}
#endif

/* PARCHE 071: "gpio" - pines del RP1 (Pi 5) */
static int cmd_gpio(const char *args, char *out_buf, uint32_t max_out)
{
#if defined(PLATFORM_rpi5)
    (void)rp1_gpio_run(args, out_buf, max_out);
    kprint(out_buf);
#else
    (void)args; (void)max_out;
    if (out_buf) { out_buf[0] = 0; }
    kprint("gpio: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

/* PARCHE 079: "wifi" (Wi-Fi de la Pi 5, fase 1: bus SDIO y firmware) y "placa" */
static int cmd_wifi(const char *args, char *out_buf, uint32_t max_out)
{
    if (out_buf) out_buf[0] = 0;
    if (agent_dispatch_mode) {                    /* SOMA_193: un agente consulta la WiFi, pero no cambia de red (podria llevar el equipo al punto de acceso de un atacante) */
        const char *a = args ? args : ""; while (*a == ' ') a++;
        if (*a && !shell_starts_with(a, "buscar") && !shell_starts_with(a, "guardadas") && !shell_starts_with(a, "registro") && !shell_starts_with(a, "log") && !shell_starts_with(a, "firmware")) {
            kprint("[SEGURIDAD AGENTE] wifi: los agentes solo pueden consultar (wifi, wifi buscar, wifi guardadas, wifi registro).\n");
            if (out_buf && max_out) { uint32_t pp = 0; soma_str_put(out_buf, max_out, &pp, "AGENT_DENIED: los agentes no pueden cambiar la conexion WiFi"); }
            return 1;
        }
    }
#ifdef __aarch64__
    wifi_command(args ? args : "");
#else
    { extern void iwl_command(const char *args); iwl_command(args ? args : ""); }   /* SOMA_175A_T480S: Intel 8265 */
#endif
    return 1;
}
/* SOMA_176A: "tls <url>": prueba el HTTPS directo (TLS 1.3 propio) sin navegador */
static int cmd_tls(const char *args, char *out_buf, uint32_t max_out)
{
    if (agent_net_denied("tls", args, out_buf, max_out)) return 1;   /* SOMA_193 */
    (void)max_out; if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (!args || !*args || shell_starts_with(args, "estado")) {           /* SOMA_194 */
        struct tls_trust_info ti; tls_trust_get_info(&ti);
        kprint("TLS 1.3 del nucleo: verificacion de certificados "); kprint(ti.verify ? "ACTIVA" : "DESACTIVADA (soma.tlsverificar=off): cifrado sin comprobar con quien se habla");
        kprint("\n  autoridades de confianza: "); kprint_dec(ti.roots); kprint(" de fabrica (Mozilla) + "); kprint_dec((uint32_t)ti.user); kprint(" del usuario (/etc/tls_confianza.pem)");
        kprint("\n  reloj: "); if (ti.now) { kprint("correcto (se comprueban las fechas de los certificados)"); } else kprint("SIN PONER: no se comprueban las fechas de los certificados");
        kprint("\nuso: tls <url>   (ej. tls https://example.com/)   HTTPS directo; dice por que falla un certificado\n");
        return 1;
    }
    static struct wn_req rq; static struct wn_resp rs;
    memset(&rq, 0, sizeof rq); memset(&rs, 0, sizeof rs);
    rq.url = args; rq.timeout_ms = 20000; rq.max_body = 300000;
    kprint("tls: pidiendo "); kprint(args); kprint(wn_https_unverified() ? " (HTTPS directo, certificado SIN verificar: soma.tlsverificar=off)\n" : " (HTTPS directo con certificado verificado, o por la pasarela del anfitrion en QEMU)\n");
    int r = wn_fetch_isolated(&rq, &rs);           /* SOMA_317_DESCARGAS_AISLADAS: el mismo TLS que el agente y "descargar" (netsvc, ring 3) */
    if (r != 0) {
        kprint("tls: fallo: "); kprint(rs.err);
#ifndef __x86_64__
        { const char *e = wn_tls_last_error(); if (e[0]) { kprint(" | "); kprint(e); } }      /* aarch64: el cliente del nucleo */
#endif
        kprint("\n"); wn_free_isolated(&rs); return 1;
    }
    kprint("tls: HTTP "); kprint_dec((uint32_t)rs.status); kprint(" | "); kprint(rs.ctype); kprint(" | "); kprint_dec(rs.len); kprint(" bytes | redirecciones "); kprint_dec((uint32_t)rs.redirects); kprint(rs.via_gateway ? " | por la pasarela\n" : " | directo\n");
    char t[161]; uint32_t k = 0; for (uint32_t i = 0; i < rs.len && k < 160; i++) { char c = rs.body[i]; t[k++] = (c >= 32 && c < 127) ? c : ' '; } t[k] = 0;
    kprint("tls: "); kprint(t); kprint("\n");
    wn_free_isolated(&rs);
    return 1;
}
static int cmd_placa(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
#ifdef __aarch64__
    board_print();
#else
    kprint("placa: PC x86_64 (sin firmware de Raspberry Pi)\n");
#endif
    return 1;
}

/* PARCHE 082: "cpus" - nucleos del procesador y prueba de calculo en paralelo */
#define CPUS_BUF (256 * 1024)
static uint8_t *cpus_buf;            /* SOMA_175C: en el montón al usarse (256 KiB menos en la imagen del kernel, que no puede pasar de 14 MiB) */
struct cpus_job { uint32_t rounds; uint8_t out[16][20]; };   /* SOMA_159_T480S: 16 (antes 4, el maximo de la Pi 5: con 8 nucleos se desbordaba) */
static void cpus_part(int idx, int n, void *arg)
{
    struct cpus_job *j = (struct cpus_job *)arg;
    /* trabajo total fijo (rounds pasadas de SHA-1 sobre el buffer), repartido por trozos del buffer */
    uint32_t a = (uint32_t)((uint64_t)CPUS_BUF * (uint32_t)idx / (uint32_t)n), b = (uint32_t)((uint64_t)CPUS_BUF * (uint32_t)(idx + 1) / (uint32_t)n);
    uint8_t h[20] = { 0 };
    for (uint32_t r = 0; r < j->rounds; r++) { struct sha1_ctx c; sha1_init(&c); sha1_update(&c, h, 20); sha1_update(&c, cpus_buf + a, b - a); sha1_final(&c, h); }
    for (int i = 0; i < 20; i++) j->out[idx][i] = h[i];
}
extern uint64_t fb_parallel_rects(void);
static int cmd_cpus(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == '1' && !args[1]) { smp_set_enabled(0); kprint("cpus: solo el nucleo 0 (vuelve con 'cpus todos')\n"); return 1; }
    if (args && args[0] == 't') { smp_set_enabled(1); kprint("cpus: todos los nucleos disponibles\n"); }
    if (args && args[0] == 'p') {
        if (!cpus_buf) cpus_buf = (uint8_t *)kmalloc(CPUS_BUF);
        if (!cpus_buf) { kprint("cpus: sin memoria para la prueba\n"); return 1; }
        for (uint32_t i = 0; i < CPUS_BUF; i++) cpus_buf[i] = (uint8_t)(i * 2654435761u >> 24);
        struct cpus_job j; j.rounds = 24;
        uint32_t t1 = 0, tn = 0;
        for (int pass = 0; pass < 2; pass++) {
            int n = pass ? smp_cpus() : 1;
            if (pass && n == 1) break;
            uint64_t t0 = timer_ticks;
            int used = smp_parallel_n(cpus_part, &j, n);
            uint32_t t = (uint32_t)(timer_ticks - t0);
            if (pass) tn = t; else t1 = t;
            kprint("  "); kprint_dec((uint32_t)used); kprint(used == 1 ? " nucleo:  " : " nucleos: "); kprint_dec(t); kprint(" ms  (SHA-1 de ");
            kprint_dec(CPUS_BUF / 1024 * j.rounds / 1024); kprint(" MiB)\n");
        }
        if (smp_cpus() > 1 && tn) { uint32_t x10 = t1 * 10 / tn; kprint("  aceleracion: x"); kprint_dec(x10 / 10); kprint(","); kprint_dec(x10 % 10); kprint("\n"); }
        else if (smp_cpus() == 1) kprint("  (solo hay un nucleo disponible)\n");
        return 1;
    }
    kprint("Nucleos: "); kprint_dec((uint32_t)smp_cpus()); kprint(" en uso | "); kprint(smp_method()); kprint(smp_enabled() ? "\n" : " | reparto apagado ('cpus todos')\n");
#ifdef __x86_64__
    /* SOMA_159_T480S: hasta 16 nucleos; el identificador es el del LAPIC (en el T480s: 0..7, dos hilos por nucleo fisico) */
    kprint("  el firmware declara "); kprint_dec((uint32_t)smp_found()); kprint(" procesadores");
    if (smp_cpus() > 1) { kprint(" | trampolin 0x"); kprint_hex32(smp_tramp_addr()); }
    else { kprint(" | "); kprint(smp_why()); }
    kprint("\n");
    for (int i = 0; i < 16; i++) {
        struct smp_core_info c;
        if (smp_core_info(i, &c) != 0) break;
        if (i > 0 && !c.online) continue;
        kprint("  nucleo "); if (i < 10) kprint(" "); kprint_dec((uint32_t)i); kprint(i == 0 ? "  sistema + calculo" : "  calculo          ");
        kprint("  APIC "); kprint_dec(c.mpidr); kprint("  trabajos "); kprint_dec((uint32_t)c.jobs); kprint("  ocupado "); kprint_dec((uint32_t)c.busy_ticks); kprint(" ms");
        if (i > 0) { kprint("  despertares "); kprint_dec((uint32_t)smp_core_wakes(i)); kprint("  arranque "); kprint_dec(smp_core_boot_ms(i)); kprint(" ms"); }
        kprint("\n");
    }
#else
    for (int i = 0; i < 4; i++) {
        struct smp_core_info c;
        if (smp_core_info(i, &c) != 0) break;
        if (i > 0 && !c.online) continue;
        kprint("  nucleo "); kprint_dec((uint32_t)i); kprint(i == 0 ? "  sistema + calculo" : "  calculo          ");
        kprint("  MPIDR 0x"); kprint_hex32(c.mpidr); kprint("  trabajos "); kprint_dec((uint32_t)c.jobs); kprint("  ocupado "); kprint_dec((uint32_t)c.busy_ticks); kprint(" ms\n");
    }
#endif
    kprint("  volcados de pantalla repartidos: "); kprint_dec((uint32_t)fb_parallel_rects()); kprint("\n");
    kprint("uso: cpus | cpus prueba | cpus 1 | cpus todos\n");
    return 1;
}

/* PARCHE 081: "dhcp" - pide direccion al router (Wi-Fi o Ethernet de una red real) */
static int cmd_dhcp(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    if (!net_init()) { kprint("dhcp: sin red\n"); return 1; }
    dhcp_run(1);
    return 1;
}

/* SOMA_170_T480S: "red": estado de la conexion (cable, movil por USB), direccion y tarjetas */
#ifdef __x86_64__
#include "e1000e.h"
#include "netmgr.h"
#endif
static int cmd_red(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
    const struct netif_ops *act = netif_active();
#ifdef __x86_64__
    kprint("Red: "); kprint(netmgr_state());
#else
    kprint("Red:");
#endif
    kprint(" | interfaz "); kprint(act ? act->name : "ninguna");
    kprint(" | IP "); kprint_ip(net_ip); kprint(" puerta "); kprint_ip(net_gateway); kprint(" DNS "); kprint_ip(net_dns); kprint("\n");
#ifdef __x86_64__
    { uint32_t sw, ok, fl; netmgr_counts(&sw, &ok, &fl); kprint("  cambios de interfaz "); kprint_dec(sw); kprint(", DHCP bien "); kprint_dec(ok); kprint(", fallidos "); kprint_dec(fl); kprint("\n"); }
    e1000e_info();
    { extern void usbnet_info(void) __attribute__((weak)); if (usbnet_info) usbnet_info(); }
#endif
    kprint("uso: red | netif (interfaces) | netif usar <nombre> | dhcp | ping <ip>\n");
    return 1;
}

/* PARCHE 080: "netif" (interfaces de red) y "wpa" (criptografia de WPA2) */
static int cmd_netif(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
    while (args && *args == ' ') args++;
    if (args && args[0] == 'u' && args[1] == 's' && args[2] == 'a' && args[3] == 'r') {
        const char *n = args + 4; while (*n == ' ') n++;
        if (netif_use(n)) { memcpy(net_mac, netif_mac(), 6); kprint("netif: ahora se usa "); kprint(n); kprint(" (la IP no cambia; 'dhcp' si hace falta)\n"); }
        else { kprint("netif: "); kprint(n); kprint(" no existe o no arranca\n"); }
        return 1;
    }
    const struct netif_ops *act = netif_active();
    for (int i = 0; i < netif_count(); i++) {
        const struct netif_ops *o = netif_get(i);
        kprint(o == act ? " * " : "   "); kprint(o->name); kprint("  "); kprint(o->desc);
        kprint(netif_is_up(i) ? "  [operativa" : "  [sin arrancar");
        if (netif_is_up(i) && o->link) kprint(o->link() ? ", enlace arriba]" : ", sin enlace]"); else kprint("]");
        if (netif_is_up(i) && o->mac) { kprint("  MAC "); kprint_mac(o->mac()); }
        kprint("\n");
    }
    if (!netif_count()) kprint("netif: ninguna interfaz registrada (la red no se ha iniciado)\n");
    kprint("uso: netif | netif usar <nombre>\n");
    return 1;
}

