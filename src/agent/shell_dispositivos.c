/* shell_dispositivos.c: dispositivos: ventilador, HDMI, Ethernet de la Pi, USB, discos (blk) y FAT.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
/* PARCHE 077: "fan" - ventilador de la Pi 5 */
static int cmd_fan(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
#if defined(PLATFORM_rpi5)
    while (*args == ' ') args++;
    int r = 0;
    if (args[0] == 'o' && args[1] == 'n') r = rp1_fan_set(1);
    else if (args[0] == 'o' && args[1] == 'f') r = rp1_fan_set(0);
    else if (args[0] == 'i') rp1_fan_invert();
    else if (args[0]) { kprint("uso: fan | fan on | fan off | fan inv (invierte el nivel si gira al reves)\n"); return 1; }
    if (r != 0) { kprint("fan: no hay RP1\n"); return 1; }
    kprint("fan: "); kprint(rp1_fan_state() < 0 ? "sin tocar" : rp1_fan_state() ? "en marcha" : "parado"); kprint(rp1_fan_inverted() ? " (nivel invertido)\n" : "\n");
#else
    (void)args;
    kprint("fan: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

/* PARCHE 076: "hdmi" - modo del framebuffer que dio el firmware (diagnostico en placa) */
static int cmd_hdmi(const char *args, char *out_buf, uint32_t max_out)
{
    (void)max_out;
    if (out_buf) out_buf[0] = 0;
#ifdef __aarch64__
    if (args && args[0] == 's' && args[1] == 'w') rpi_fb_toggle_swap();   /* 'hdmi swap': rojo <-> azul */
    rpi_fb_info();
#else
    kprint("hdmi: solo en aarch64\n");
#endif
    return 1;
}

/* PARCHE 073: "eth" - estado de la Ethernet del RP1 (enlace negociado) */
static int cmd_eth(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)max_out;
    if (out_buf) out_buf[0] = 0;
#if defined(PLATFORM_rpi5)
    if (!gem_present()) { kprint("eth: no hay Ethernet del RP1\n"); return 1; }
    kprint("eth: enlace ");
    if (!gem_link_up()) { kprint("abajo (sin cable o negociando)\n"); return 1; }
    kprint_dec(gem_speed_mbps()); kprint(gem_full_duplex() ? " Mb/s duplex completo" : " Mb/s semiduplex");
    { uint32_t t, r; uint64_t tb, rb; gem_stats(&t, &r, &tb, &rb);
      kprint(" | tramas tx "); kprint_dec(t); kprint(" rx "); kprint_dec(r); kprint("\n"); }
#else
    kprint("eth: solo en la Pi 5 (RP1)\n");
#endif
    return 1;
}

static int cmd_usb(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[4096];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    static struct xhci_info xi;
    static struct xhci_hid_status hs;
    int hid_on = 0, hid_off = 0, hid_st = 0;
    if (args) {                                                                  /* PARCHE 063: un subcomando desconocido ya no repite la exploracion */
        const char *w0 = args; while (*w0 == ' ') w0++;
        if (*w0 && !((w0[0] == 'h' && w0[1] == 'i' && w0[2] == 'd') || (w0[0] == 'r' && w0[1] == 'e' && w0[2] == 's') || (w0[0] == 'm' && w0[1] == 's' && w0[2] == 'c'))) {
            kprint("usb: subcomando desconocido. Uso: usb | usb rescan | usb hid [on|off|status] | usb msc\n");
            if (out_buf && max_out) { uint32_t q0 = 0; out_buf[0] = 0; soma_str_put(out_buf, max_out, &q0, "usb: subcomando desconocido"); }
            return 1;
        }
    }
    if (args && args[0] == 'h' && args[1] == 'i' && args[2] == 'd') {            /* usb hid [on | off | status] */
        const char *w = args + 3; while (*w == ' ') w++;
        if (w[0] == 'o' && w[1] == 'f') hid_off = 1; else if (w[0] == 's') hid_st = 1; else hid_on = 1;
    }
    if (args && args[0] == 'r' && args[1] == 'e' && args[2] == 's') {            /* usb rescan: vuelve a explorar y reactiva HID y disco */
        int rr = xhci_rescan();
        soma_str_put(b, sizeof b, &p, rr == 0 ? "USB: reexploracion terminada (usa 'usb' para ver los dispositivos)\n" : rr == -1 ? "USB: ya hay una reexploracion en curso\n" : "USB: la reexploracion ha fallado\n");
        goto usb_done;
    }
    if (hid_off || hid_st) {
        if (hid_off) xhci_hid_stop();
        xhci_hid_status(&hs);
        usb_hid_print(b, sizeof b, &p, &hs);
        goto usb_done;
    }
    if (args && args[0] == 'm' && args[1] == 's' && args[2] == 'c') {            /* usb msc: disco USB (almacenamiento masivo) */
        static struct xhci_msc_info mi;
        if (!xhci_msc_ready()) {
            int pr = xhci_probe(&xi);
            int mr = pr == 0 ? xhci_msc_start(&xi, &mi) : pr;
            if (mr != 0) { soma_str_put(b, sizeof b, &p, "USB MSC: FALLO - "); soma_str_put(b, sizeof b, &p, mr == -1 ? "no hay disco USB (almacenamiento masivo) conectado" : xhci_err_str(mr)); soma_str_putc(b, sizeof b, &p, '\n'); }
        }
        if (xhci_msc_ready()) {
            const struct xhci_msc_info *ui = xhci_msc_get();
            soma_str_put(b, sizeof b, &p, "USB MSC: "); soma_str_put(b, sizeof b, &p, ui->vendor); soma_str_putc(b, sizeof b, &p, ' '); soma_str_put(b, sizeof b, &p, ui->product);
            soma_str_put(b, sizeof b, &p, " | "); soma_str_put_dec(b, sizeof b, &p, ui->sectors / 2048); soma_str_put(b, sizeof b, &p, " MiB | sector 0: ");
            for (int i = 0; i < 16; i++) soma_str_putc(b, sizeof b, &p, (ui->sector0[i] >= 32 && ui->sector0[i] < 127) ? (char)ui->sector0[i] : '.');
            soma_str_put(b, sizeof b, &p, "\nUsa 'blk' para ver usb0, 'blk read usb0 <lba>' y 'blk test'.\n");
        }
        goto usb_done;
    }
    int r;
    if (xhci_hid_active() && xhci_last() && xhci_last()->ready) {               /* con el teclado en marcha no se reinicia el controlador: se muestra la ultima exploracion */
        xi = *xhci_last(); r = 0;
        soma_str_put(b, sizeof b, &p, "(ultima exploracion; 'usb rescan' para repetirla)\n");
    } else r = xhci_probe(&xi);
    if (xi.found) {
        if (xi.bus == 0xFF) soma_str_put(b, sizeof b, &p, "xHCI del RP1 (fuera de PCI)");         /* PARCHE 067 */
        else { soma_str_put(b, sizeof b, &p, "xHCI en PCI "); soma_str_put_dec(b, sizeof b, &p, xi.bus); soma_str_putc(b, sizeof b, &p, ':'); soma_str_put_dec(b, sizeof b, &p, xi.slot); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_dec(b, sizeof b, &p, xi.fn); }
        soma_str_put(b, sizeof b, &p, " BAR0 0x"); soma_str_put_hex(b, sizeof b, &p, (uint32_t)(xi.bar >> 32), 8, 1); soma_str_put_hex(b, sizeof b, &p, (uint32_t)xi.bar, 8, 1);
        soma_str_put(b, sizeof b, &p, " | version "); soma_str_put_dec(b, sizeof b, &p, xi.hciversion >> 8); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_hex(b, sizeof b, &p, xi.hciversion & 0xFF, 2, 0);
        soma_str_put(b, sizeof b, &p, "\n  ranuras "); soma_str_put_dec(b, sizeof b, &p, xi.max_slots); soma_str_put(b, sizeof b, &p, ", puertos "); soma_str_put_dec(b, sizeof b, &p, xi.max_ports);
        soma_str_put(b, sizeof b, &p, ", contextos de "); soma_str_put_dec(b, sizeof b, &p, xi.csz64 ? 64 : 32); soma_str_put(b, sizeof b, &p, " bytes, scratchpad "); soma_str_put_dec(b, sizeof b, &p, xi.scratch);
        if (xi.bios_owned) soma_str_put(b, sizeof b, &p, ", el BIOS lo tenia y lo ha cedido");
        soma_str_putc(b, sizeof b, &p, '\n');
    }
    if (r == 0) {
        soma_str_put(b, sizeof b, &p, "Puertos con dispositivo: "); soma_str_put_dec(b, sizeof b, &p, xi.n_conn); soma_str_put(b, sizeof b, &p, " de "); soma_str_put_dec(b, sizeof b, &p, xi.n_ports);
        soma_str_putc(b, sizeof b, &p, '\n');
        for (int i = 0; i < xi.n_ports && i < XHCI_MAX_PORTS_INFO; i++) {
            if (!xi.port[i].ccs) continue;
            soma_str_put(b, sizeof b, &p, "  puerto "); soma_str_put_dec(b, sizeof b, &p, xi.port[i].num); soma_str_put(b, sizeof b, &p, xi.port[i].major == 3 ? " (USB3)" : " (USB2)");
            soma_str_put(b, sizeof b, &p, " PORTSC 0x"); soma_str_put_hex(b, sizeof b, &p, xi.port[i].portsc, 8, 1); soma_str_putc(b, sizeof b, &p, '\n');
        }
        for (int i = 0; i < xi.ndev; i++) {
            const struct usb_dev_info *d = &xi.dev[i];
            soma_str_put(b, sizeof b, &p, "Puerto "); soma_str_put_dec(b, sizeof b, &p, d->root_port ? d->root_port : d->port);
            for (int lv = 0; lv < 5; lv++) { unsigned nb = (d->route >> (4 * lv)) & 0xF; if (!nb) break; soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_dec(b, sizeof b, &p, nb); }
            if (d->err != 0) { soma_str_put(b, sizeof b, &p, ": FALLO - "); soma_str_put(b, sizeof b, &p, xhci_err_str(d->err)); soma_str_putc(b, sizeof b, &p, '\n'); if (!d->vid) continue; }
            else soma_str_put(b, sizeof b, &p, ": ");
            soma_str_put(b, sizeof b, &p, xhci_speed_str(d->speed)); soma_str_put(b, sizeof b, &p, " | ranura "); soma_str_put_dec(b, sizeof b, &p, d->slot);
            soma_str_put(b, sizeof b, &p, " | ID "); soma_str_put_hex(b, sizeof b, &p, d->vid, 4, 0); soma_str_putc(b, sizeof b, &p, ':'); soma_str_put_hex(b, sizeof b, &p, d->pid, 4, 0);
            soma_str_put(b, sizeof b, &p, " | USB "); soma_str_put_dec(b, sizeof b, &p, d->bcd_usb >> 8); soma_str_putc(b, sizeof b, &p, '.'); soma_str_put_hex(b, sizeof b, &p, d->bcd_usb & 0xFF, 2, 0);
            soma_str_put(b, sizeof b, &p, " | EP0 "); soma_str_put_dec(b, sizeof b, &p, d->speed == 4 ? 512u : d->mps0); soma_str_put(b, sizeof b, &p, " B");
            if (d->is_hub) {
                soma_str_put(b, sizeof b, &p, " | HUB ");
                if (d->hub_ports) { soma_str_put_dec(b, sizeof b, &p, d->hub_ports); soma_str_put(b, sizeof b, &p, " puertos"); }
                else soma_str_put(b, sizeof b, &p, d->hub_note == 1 ? "no soportado (SuperSpeed o demasiado profundo)" : "no se pudo configurar");
            }
            soma_str_putc(b, sizeof b, &p, '\n');
            for (int k = 0; k < d->n_if && k < 4; k++) {
                const struct usb_if_info *f = &d->ifs[k];
                soma_str_put(b, sizeof b, &p, "    interfaz "); soma_str_put_dec(b, sizeof b, &p, f->num); soma_str_put(b, sizeof b, &p, ": "); soma_str_put(b, sizeof b, &p, usb_class_str(f->cls, f->sub, f->proto));
                soma_str_put(b, sizeof b, &p, " (clase "); soma_str_put_hex(b, sizeof b, &p, f->cls, 2, 0); soma_str_putc(b, sizeof b, &p, '/'); soma_str_put_hex(b, sizeof b, &p, f->sub, 2, 0); soma_str_putc(b, sizeof b, &p, '/'); soma_str_put_hex(b, sizeof b, &p, f->proto, 2, 0); soma_str_putc(b, sizeof b, &p, ')');
                for (int e = 0; e < f->n_ep; e++) {
                    soma_str_put(b, sizeof b, &p, " | EP 0x"); soma_str_put_hex(b, sizeof b, &p, f->ep[e].addr, 2, 0);
                    soma_str_put(b, sizeof b, &p, (f->ep[e].attr & 3) == 3 ? " interrupt" : (f->ep[e].attr & 3) == 2 ? " bulk" : (f->ep[e].attr & 3) == 1 ? " isoc" : " control");
                    soma_str_putc(b, sizeof b, &p, ' '); soma_str_put_dec(b, sizeof b, &p, f->ep[e].mps); soma_str_put(b, sizeof b, &p, " B");
                }
                soma_str_putc(b, sizeof b, &p, '\n');
            }
        }
        soma_str_put(b, sizeof b, &p, "xHCI: OK\n");
        if (hid_on) {
            int hr = xhci_hid_start(&xi, &hs);
            if (hr != 0) { soma_str_put(b, sizeof b, &p, "USB HID: FALLO - "); soma_str_put(b, sizeof b, &p, hr == -1 ? "no hay teclado ni raton USB HID conectado" : xhci_err_str(hr)); soma_str_putc(b, sizeof b, &p, '\n'); }
            else usb_hid_print(b, sizeof b, &p, &hs);
        } else if (!xhci_hid_active()) soma_str_put(b, sizeof b, &p, "(usa 'usb hid' para activar el teclado/raton USB)\n");
    } else {
        soma_str_put(b, sizeof b, &p, "xHCI: FALLO - "); soma_str_put(b, sizeof b, &p, xhci_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n');
    }
usb_done:;
#else
    (void)args;
    soma_str_put(b, sizeof b, &p, "usb: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 045: "blk" lista los discos (virtio, AHCI, NVMe) en una capa comun; blk read <dev> <lba>; blk copy <src> <lba> <dst> <lba> <n>; blk test. */
static const char *blk_word(const char **pp, char *w, int max)
{
    const char *s = *pp; while (*s == ' ') s++;
    int n = 0; while (*s && *s != ' ' && n < max - 1) w[n++] = *s++;
    w[n] = 0; *pp = s; return n ? w : 0;
}
static uint64_t blk_num(const char **pp)
{
    const char *s = *pp; while (*s == ' ') s++;
    uint64_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint64_t)(*s++ - '0');
    *pp = s; return v;
}

static int cmd_blk(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[3072];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    const char *a = args ? args : "";
    char w[16], d1[16], d2[16];
    const char *sub = blk_word(&a, w, sizeof w);
    int n = blk_scan();
    int is_read = sub && w[0] == 'r' && w[1] == 'e' && w[2] == 'a' && w[3] == 'd';
    int is_copy = sub && w[0] == 'c' && w[1] == 'o' && w[2] == 'p' && w[3] == 'y';
    int is_test = sub && w[0] == 't' && w[1] == 'e' && w[2] == 's' && w[3] == 't';
    if (n == 0) soma_str_put(b, sizeof b, &p, "blk: ningun dispositivo\n");
    for (int i = 0; i < n && !is_read && !is_copy && !is_test; i++) {
        const struct blkdev *d = blk_get(i);
        soma_str_put(b, sizeof b, &p, "  "); soma_str_put(b, sizeof b, &p, d->name); soma_str_put(b, sizeof b, &p, "  ["); soma_str_put(b, sizeof b, &p, d->driver); soma_str_put(b, sizeof b, &p, "]  ");
        soma_str_put_dec(b, sizeof b, &p, d->sectors / 2048); soma_str_put(b, sizeof b, &p, " MiB  "); soma_str_put(b, sizeof b, &p, d->test_disk ? "disco de PRUEBA (escritura permitida)\n" : "protegido (solo lectura)\n");
    }
    if (is_read) {
        const char *dn = blk_word(&a, d1, sizeof d1); uint64_t lba = blk_num(&a);
        static uint8_t sec[512];
        int dev = dn ? blk_find(dn) : -1;
        int r = dev >= 0 ? blk_read(dev, lba, 1, sec) : BLK_E_ARG;
        if (r != 0) { soma_str_put(b, sizeof b, &p, "blk read <dev> <lba>: "); soma_str_put(b, sizeof b, &p, blk_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n'); }
        else {
            const char hx[] = "0123456789ABCDEF";
            for (int row = 0; row < 4; row++) {
                soma_str_put(b, sizeof b, &p, "  "); soma_str_put_dec(b, sizeof b, &p, lba); soma_str_putc(b, sizeof b, &p, '+'); soma_str_put_dec(b, sizeof b, &p, (uint64_t)row * 16); soma_str_put(b, sizeof b, &p, ": ");
                for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, hx[v >> 4]); soma_str_putc(b, sizeof b, &p, hx[v & 15]); soma_str_putc(b, sizeof b, &p, ' '); }
                for (int i = 0; i < 16; i++) { uint8_t v = sec[row * 16 + i]; soma_str_putc(b, sizeof b, &p, (v >= 32 && v < 127) ? (char)v : '.'); }
                soma_str_putc(b, sizeof b, &p, '\n');
            }
        }
    } else if (is_copy) {
        const char *sn = blk_word(&a, d1, sizeof d1); uint64_t sl = blk_num(&a);
        const char *dn = blk_word(&a, d2, sizeof d2); uint64_t dl = blk_num(&a); uint64_t cnt = blk_num(&a);
        int s = sn ? blk_find(sn) : -1, d = dn ? blk_find(dn) : -1;
        int r = (s >= 0 && d >= 0) ? blk_copy(s, sl, d, dl, cnt, 1) : BLK_E_ARG;
        soma_str_put(b, sizeof b, &p, "blk copy <origen> <lba> <destino> <lba> <n>: "); soma_str_put(b, sizeof b, &p, r == 0 ? "OK (copiado y verificado)" : blk_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n');
    } else if (is_test) {
        static uint8_t orig[64 * 512], now[64 * 512], src[64 * 512];
        int dst = -1, srcd = blk_find("vda");
        for (int i = 0; i < n; i++) if (blk_get(i)->test_disk && blk_get(i)->sectors >= 4096) { dst = i; break; }
        if (dst < 0) soma_str_put(b, sizeof b, &p, "blk test: hace falta un disco de prueba (SOMA_SATA=1, SOMA_NVME=1, SOMA_USB=1 o la tarjeta SD de pi5.py)\n");
        else {
            int r = 0;
            if (srcd >= 0) r = blk_read(srcd, 0, 64, src);
            else for (int i = 0; i < 64 * 512; i++) src[i] = (uint8_t)(i * 131 + (i >> 9) * 7 + 5);   /* sin vda: patron determinista */
            if (r == 0) r = blk_read(dst, 2048, 64, orig);
            int c = r;
            if (r == 0) c = srcd >= 0 ? blk_copy(srcd, 0, dst, 2048, 64, 1) : blk_write(dst, 2048, 64, src);
            if (c == 0 && srcd < 0) { int rr = blk_read(dst, 2048, 64, now); for (int i = 0; rr == 0 && i < 64 * 512; i++) if (now[i] != src[i]) rr = BLK_E_VERIFY; c = rr; }
            int ok = (c == 0);
            if (r == 0) { int rr = blk_write(dst, 2048, 64, orig); if (rr == 0) rr = blk_read(dst, 2048, 64, now); int same = (rr == 0); for (int i = 0; same && i < 64 * 512; i++) if (now[i] != orig[i]) same = 0; if (!same) ok = 0; }
            soma_str_put(b, sizeof b, &p, srcd >= 0 ? "blk test (vda -> " : "blk test (patron -> "); soma_str_put(b, sizeof b, &p, blk_get(dst)->name); soma_str_put(b, sizeof b, &p, ", 32 KiB, verificado y restaurado): ");
            soma_str_put(b, sizeof b, &p, ok ? "OK\n" : "FALLO\n");
        }
    }
#else
    (void)args;
    soma_str_put(b, sizeof b, &p, "blk: no disponible en esta arquitectura\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

/* PARCHE 050: "fat" lee sistemas de archivos FAT12/16/32 (solo lectura) de los discos de blk: fat | fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino VFS> */
#if defined(__x86_64__) || defined(__aarch64__)
static int fat_blk_read(void *ctx, uint64_t lba, uint32_t n, void *buf) { return blk_read((int)(intptr_t)ctx, lba, n, buf); }

static int fat_open(struct fat_vol *v, const char *devname, char *b, uint32_t m, uint32_t *p)
{
    int dev = devname ? blk_find(devname) : -1;
    if (dev < 0) { soma_str_put(b, m, p, "fat: dispositivo desconocido (mira 'blk')\n"); return 0; }
    struct fat_io io = { fat_blk_read, (void *)(intptr_t)dev, blk_get(dev)->sectors };
    int r = fat_mount(v, &io);
    if (r != 0) { soma_str_put(b, m, p, devname); soma_str_put(b, m, p, ": "); soma_str_put(b, m, p, fat_err_str(r)); soma_str_putc(b, m, p, '\n'); return 0; }
    return 1;
}

static void fat_put_date(char *b, uint32_t m, uint32_t *p, uint16_t d, uint16_t t)
{
    uint32_t y = 1980u + (d >> 9), mo = (d >> 5) & 15u, da = d & 31u, h = t >> 11, mi = (t >> 5) & 63u;
    soma_str_put_dec(b, m, p, y); soma_str_putc(b, m, p, '-'); if (mo < 10) soma_str_putc(b, m, p, '0'); soma_str_put_dec(b, m, p, mo); soma_str_putc(b, m, p, '-'); if (da < 10) soma_str_putc(b, m, p, '0'); soma_str_put_dec(b, m, p, da);
    soma_str_putc(b, m, p, ' '); if (h < 10) soma_str_putc(b, m, p, '0'); soma_str_put_dec(b, m, p, h); soma_str_putc(b, m, p, ':'); if (mi < 10) soma_str_putc(b, m, p, '0'); soma_str_put_dec(b, m, p, mi);
}
#endif

static int cmd_fat(const char *args, char *out_buf, uint32_t max_out)
{
    static char b[6144];
    uint32_t p = 0;
#if defined(__x86_64__) || defined(__aarch64__)
    const char *a = args ? args : "";
    char w[16], dn[16], path[160], dest[64];
    const char *sub = blk_word(&a, w, sizeof w);
    static struct fat_vol v;
    int is_ls = sub && w[0] == 'l' && w[1] == 's' && !w[2];
    int is_cat = sub && w[0] == 'c' && w[1] == 'a' && w[2] == 't' && !w[3];
    int is_get = sub && w[0] == 'g' && w[1] == 'e' && w[2] == 't' && !w[3];
    int n = blk_scan();
    if (!sub) {
        for (int i = 0; i < n; i++) {
            const struct blkdev *d = blk_get(i);
            soma_str_put(b, sizeof b, &p, "  "); soma_str_put(b, sizeof b, &p, d->name); soma_str_put(b, sizeof b, &p, ": ");
            struct fat_io io = { fat_blk_read, (void *)(intptr_t)i, d->sectors };
            int r = fat_mount(&v, &io);
            if (r == 0) {
                soma_str_put(b, sizeof b, &p, "FAT"); soma_str_put_dec(b, sizeof b, &p, (uint32_t)v.type);
                if (v.part_index) { soma_str_put(b, sizeof b, &p, " (particion "); soma_str_put_dec(b, sizeof b, &p, (uint32_t)v.part_index); soma_str_putc(b, sizeof b, &p, ')'); }
                soma_str_put(b, sizeof b, &p, " | "); soma_str_put_dec(b, sizeof b, &p, v.part_sectors / 2048); soma_str_put(b, sizeof b, &p, " MiB | etiqueta '"); soma_str_put(b, sizeof b, &p, v.label); soma_str_put(b, sizeof b, &p, "'\n");
            } else { soma_str_put(b, sizeof b, &p, fat_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n'); }
        }
        soma_str_put(b, sizeof b, &p, "Usa: fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>\n");
    } else if (is_ls || is_cat || is_get) {
        const char *d = blk_word(&a, dn, sizeof dn);
        const char *pa = blk_word(&a, path, sizeof path);
        if (!pa && is_ls) { path[0] = 0; pa = path; }
        if (!d || !pa) soma_str_put(b, sizeof b, &p, "fat: faltan argumentos. Uso: fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>   (dev: ver fat)\n");
        else if (fat_open(&v, d, b, sizeof b, &p)) {
            if (is_ls) {
                static struct fat_dirent ents[48]; int total = 0;
                int got = fat_readdir(&v, pa, ents, 48, &total);
                if (got < 0) { soma_str_put(b, sizeof b, &p, "fat ls: "); soma_str_put(b, sizeof b, &p, fat_err_str(got)); soma_str_putc(b, sizeof b, &p, '\n'); }
                else {
                    for (int i = 0; i < got; i++) {
                        soma_str_put(b, sizeof b, &p, "  "); fat_put_date(b, sizeof b, &p, ents[i].date, ents[i].time); soma_str_put(b, sizeof b, &p, ents[i].is_dir ? "  <DIR>     " : "  ");
                        if (!ents[i].is_dir) { uint32_t pp0 = p; soma_str_put_dec(b, sizeof b, &p, ents[i].size); soma_str_put(b, sizeof b, &p, " B"); while (p - pp0 < 12 && p + 2 < sizeof b) soma_str_putc(b, sizeof b, &p, ' '); }
                        soma_str_put(b, sizeof b, &p, ents[i].name); soma_str_put(b, sizeof b, &p, ents[i].is_dir ? "/\n" : "\n");
                    }
                    soma_str_put_dec(b, sizeof b, &p, (uint32_t)total); soma_str_put(b, sizeof b, &p, " entradas"); if (total > got) soma_str_put(b, sizeof b, &p, " (se muestran las primeras 48)"); soma_str_putc(b, sizeof b, &p, '\n');
                }
            } else if (is_cat) {
                static uint8_t buf[4096];
                int got = fat_read(&v, pa, 0, buf, sizeof buf);
                if (got < 0) { soma_str_put(b, sizeof b, &p, "fat cat: "); soma_str_put(b, sizeof b, &p, fat_err_str(got)); soma_str_putc(b, sizeof b, &p, '\n'); }
                else {
                    for (int i = 0; i < got; i++) { uint8_t c = buf[i]; if (c == '\n' || c == '\t' || c >= 0x80 || (c >= 32 && c < 127)) soma_str_putc(b, sizeof b, &p, (char)c); else soma_str_putc(b, sizeof b, &p, '.'); }
                    if (got == (int)sizeof buf) soma_str_put(b, sizeof b, &p, "\n[... solo los primeros 4096 bytes]");
                    soma_str_putc(b, sizeof b, &p, '\n');
                }
            } else {
                const char *dst = blk_word(&a, dest, sizeof dest);
                struct fat_dirent st;
                char *fbuf = 0;
                int r = dst ? fat_stat(&v, pa, &st) : FAT_E_ARG;
                if (!dst) soma_str_put(b, sizeof b, &p, "fat get: falta el destino\n");
                else if (agent_write_denied(dst, "fat get", 0, 0)) soma_str_put(b, sizeof b, &p, "fat get: destino protegido (los agentes no pueden modificar /etc, /sys, /bin ni /boot)\n");
                else if (r != 0) { soma_str_put(b, sizeof b, &p, "fat get: "); soma_str_put(b, sizeof b, &p, fat_err_str(r)); soma_str_putc(b, sizeof b, &p, '\n'); }
                else if (st.is_dir) soma_str_put(b, sizeof b, &p, "fat get: es un directorio\n");
                else if (st.size > FS_FILE_MAX) { soma_str_put(b, sizeof b, &p, "fat get: "); soma_str_put_dec(b, sizeof b, &p, st.size); soma_str_put(b, sizeof b, &p, " bytes no caben en el VFS (maximo 32 MiB)\n"); }
                else if (!(fbuf = (char *)kmalloc((size_t)st.size + 1))) soma_str_put(b, sizeof b, &p, "fat get: sin memoria\n");
                else {
                    int got = fat_read(&v, pa, 0, fbuf, st.size);
                    if (got < 0) { soma_str_put(b, sizeof b, &p, "fat get: "); soma_str_put(b, sizeof b, &p, fat_err_str(got)); soma_str_putc(b, sizeof b, &p, '\n'); }
                    else if (vfs_write(dst, fbuf, (uint32_t)got) < 0) soma_str_put(b, sizeof b, &p, "fat get: no se pudo escribir en el VFS\n");
                    else { soma_str_put(b, sizeof b, &p, "copiados "); soma_str_put_dec(b, sizeof b, &p, (uint32_t)got); soma_str_put(b, sizeof b, &p, " bytes a "); soma_str_put(b, sizeof b, &p, dst); soma_str_putc(b, sizeof b, &p, '\n'); }
                }
                if (fbuf) kfree(fbuf);
            }
        }
    } else soma_str_put(b, sizeof b, &p, "uso: fat | fat ls <dev> [ruta] | fat cat <dev> <ruta> | fat get <dev> <ruta> <destino>\n");
#else
    (void)args;
    soma_str_put(b, sizeof b, &p, "fat: solo disponible en x86_64\n");
#endif
    kprint(b);
    uint32_t q = 0;
    if (out_buf && max_out) { out_buf[0] = 0; soma_str_put(out_buf, max_out, &q, b); }
    return 1;
}

