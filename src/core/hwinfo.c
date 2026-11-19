/* PARCHE 032: ver hwinfo.h. Todo acceso esta acotado: ninguna longitud de tabla se acepta sin comprobarla. */
#include "hwinfo.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int sum_ok(const uint8_t *p, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); return s == 0; }
static int meq(const uint8_t *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != (uint8_t)b[i]) return 0; return 1; }

#define ACPI_MAX_TABLE (1u << 20)
#define ACPI_HDR 36

/* Devuelve el puntero a una tabla completa (cabecera validada: longitud y suma de comprobacion) o 0. */
static const uint8_t *acpi_table(uint64_t phys, hw_map_fn map, void *ctx, uint32_t *len)
{
    const uint8_t *h = map(phys, ACPI_HDR, ctx);
    if (!h) return 0;
    uint32_t l = le32(h + 4);
    if (l < ACPI_HDR || l > ACPI_MAX_TABLE) return 0;
    const uint8_t *t = map(phys, l, ctx);
    if (!t || !sum_ok(t, l)) return 0;
    *len = l; return t;
}

static void parse_madt(const uint8_t *t, uint32_t len, struct hw_acpi *o)
{
    if (len < ACPI_HDR + 8) return;
    o->has_madt = 1; o->lapic_addr = le32(t + ACPI_HDR);
    o->nmi_lint = -1;
    uint32_t p = ACPI_HDR + 8;
    while (p + 2 <= len) {
        uint8_t ty = t[p], l = t[p + 1];
        if (l < 2 || p + l > len) break;
        if ((ty == 0 && l >= 8) || (ty == 9 && l >= 16)) {                                   /* LAPIC / x2APIC local */
            uint32_t fl = le32(t + p + (ty == 0 ? 4 : 8));
            uint32_t id = ty == 0 ? t[p + 3] : le32(t + p + 4);
            uint32_t uid = ty == 0 ? t[p + 2] : le32(t + p + 12);
            int dup = 0;                                                                      /* el mismo nucleo puede salir en 0 y en 9 */
            for (int k = 0; k < o->nlapic; k++) if (o->lapic[k].apic_id == id) dup = 1;
            if (!dup && (fl & 3)) o->ncpu++;                                                  /* habilitado o capaz de habilitarse */
            if (!dup && o->nlapic < HW_MAX_CPU) {
                int k = o->nlapic++;
                o->lapic[k].apic_id = id; o->lapic[k].uid = uid;
                o->lapic[k].enabled = (uint8_t)(fl & 1); o->lapic[k].online_capable = (uint8_t)((fl >> 1) & 1);
            }
        }
        else if (ty == 1 && l >= 12 && o->nioapic < HW_MAX_IOAPIC) {
            int k = o->nioapic++;
            o->ioapic_id[k] = t[p + 2]; o->ioapic_addr[k] = le32(t + p + 4); o->ioapic_gsi[k] = le32(t + p + 8);
        }
        else if ((ty == 4 && l >= 6) || (ty == 0x0A && l >= 12)) {                            /* NMI del LAPIC (todos o uno) */
            o->nmi_flags = (uint16_t)(t[p + 3] | t[p + 4] << 8);
            o->nmi_lint = ty == 4 ? (t[p + 5] & 1) : (t[p + 10] & 1);
        }
        else if (ty == 2 && l >= 10 && o->nirq_ovr < 16) { int i = o->nirq_ovr++; o->irq_ovr[i].src = t[p + 3]; o->irq_ovr[i].gsi = le32(t + p + 4); o->irq_ovr[i].flags = (uint16_t)(t[p + 8] | t[p + 9] << 8); }
        else if (ty == 5 && l >= 12) o->lapic_addr = (uint32_t)le64(t + p + 4);           /* sustituto de 64 bits (se guarda truncado) */
        p += l;
    }
}

void hw_isa_irq_route(const struct hw_acpi *o, uint8_t irq, uint32_t *gsi, int *low, int *level)
{
    *gsi = irq; *low = 0; *level = 0;                                 /* ISA: flanco, activa alta */
    for (int i = 0; o && i < o->nirq_ovr && i < 16; i++) {
        if (o->irq_ovr[i].src != irq) continue;
        uint16_t f = o->irq_ovr[i].flags;
        *gsi = o->irq_ovr[i].gsi;
        if ((f & 3) == 3) *low = 1;                                   /* polaridad: 00 = la del bus (ISA alta), 01 alta, 11 baja */
        if (((f >> 2) & 3) == 3) *level = 1;                          /* disparo: 00 = el del bus (ISA flanco), 01 flanco, 11 nivel */
        return;
    }
}
int hw_ioapic_for_gsi(const struct hw_acpi *o, uint32_t gsi)
{
    int best = -1;
    for (int i = 0; o && i < o->nioapic && i < HW_MAX_IOAPIC; i++)
        if (o->ioapic_gsi[i] <= gsi && (best < 0 || o->ioapic_gsi[i] > o->ioapic_gsi[best])) best = i;
    return best;
}

static void parse_mcfg(const uint8_t *t, uint32_t len, struct hw_acpi *o)
{
    for (uint32_t p = ACPI_HDR + 8; p + 16 <= len && o->nmcfg < HW_MAX_MCFG; p += 16) {
        int i = o->nmcfg++;
        o->mcfg[i].base = le64(t + p); o->mcfg[i].seg = (uint16_t)(t[p + 8] | t[p + 9] << 8);
        o->mcfg[i].bus0 = t[p + 10]; o->mcfg[i].bus1 = t[p + 11];
    }
}

int hw_acpi_parse(uint64_t rsdp_phys, hw_map_fn map, void *ctx, struct hw_acpi *o)
{
    for (size_t i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    const uint8_t *r = map(rsdp_phys, 20, ctx);
    if (!r || !meq(r, "RSD PTR ", 8) || !sum_ok(r, 20)) return -1;
    o->revision = r[15];
    for (int i = 0; i < 6; i++) o->oem[i] = (r[9 + i] >= 32 && r[9 + i] < 127) ? (char)r[9 + i] : '?';
    uint64_t root; int esz;
    if (r[15] >= 2) {
        r = map(rsdp_phys, 36, ctx);
        if (!r) return -1;
        uint32_t l = le32(r + 20);
        if (l < 36 || l > 4096 || !(r = map(rsdp_phys, l, ctx)) || !sum_ok(r, l)) return -1;
        root = le64(r + 24); esz = 8;
    } else { root = le32(r + 16); esz = 4; }
    uint32_t rl; const uint8_t *rt = acpi_table(root, map, ctx, &rl);
    if (!rt || !(meq(rt, "XSDT", 4) || meq(rt, "RSDT", 4))) return -1;
    uint32_t n = (rl - ACPI_HDR) / (uint32_t)esz;
    if (n > 256) n = 256;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t a = esz == 8 ? le64(rt + ACPI_HDR + i * 8) : le32(rt + ACPI_HDR + i * 4);
        uint32_t tl; const uint8_t *t = acpi_table(a, map, ctx, &tl);
        if (!t) continue;
        if (o->ntables < 32) { for (int k = 0; k < 4; k++) o->sig[o->ntables][k] = (t[k] >= 32 && t[k] < 127) ? (char)t[k] : '?'; o->sig[o->ntables][4] = 0; }
        o->ntables++;
        if (meq(t, "APIC", 4)) parse_madt(t, tl, o);
        else if (meq(t, "MCFG", 4)) parse_mcfg(t, tl, o);
        else if (meq(t, "HPET", 4)) o->has_hpet = 1;
        else if (meq(t, "FACP", 4)) o->has_fadt = 1;
    }
    o->ok = o->ntables > 0;
    return o->ok ? 0 : -1;
}

uint64_t hw_acpi_find_rsdp(uint64_t lo, uint64_t hi, hw_map_fn map, void *ctx)
{
    for (uint64_t a = (lo + 15) & ~15ULL; a + 20 <= hi; a += 16) {
        const uint8_t *p = map(a, 20, ctx);
        if (p && meq(p, "RSD PTR ", 8) && sum_ok(p, 20)) return a;
    }
    return 0;
}

/* ------------------------------ FDT ------------------------------ */
#define FDT_BEGIN 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_NOP 4
#define FDT_END 9
#define FDT_MAX_DEPTH 24

static int cstr_len(const uint8_t *s, size_t max) { size_t i = 0; while (i < max && s[i]) i++; return i < max ? (int)i : -1; }
static int str_has(const uint8_t *list, size_t n, const char *w)     /* `w` es una de las cadenas de una lista de NUL concatenadas */
{
    size_t i = 0;
    while (i < n) {
        int l = cstr_len(list + i, n - i); if (l < 0) return 0;
        int j = 0; while (w[j] && list[i + j] == (uint8_t)w[j]) j++;
        if (!w[j] && j == l) return 1;
        i += (size_t)l + 1;
    }
    return 0;
}
static void copy_str(char *d, size_t dmax, const uint8_t *s, size_t n)
{
    size_t i = 0; for (; i + 1 < dmax && i < n && s[i]; i++) d[i] = (s[i] >= 32 && s[i] < 127) ? (char)s[i] : '?';
    d[i] = 0;
}

struct fdt_it { const uint8_t *b; size_t off_struct, size_struct, off_str, size_str; size_t pos; };

static int fdt_open(const uint8_t *f, size_t avail, struct fdt_it *it, uint32_t *total, uint32_t *ver)
{
    if (!f || avail < 40 || be32(f) != 0xD00DFEEDu) return -1;
    uint32_t ts = be32(f + 4), os = be32(f + 8), ostr = be32(f + 12), v = be32(f + 20), lc = be32(f + 24), sts = be32(f + 32), ss = be32(f + 36);
    if (ts < 40 || ts > avail || v < 16 || lc > 17) return -1;
    if ((os & 3) || os > ts || ss > ts - os || (ss & 3) || ostr > ts || sts > ts - ostr) return -1;
    it->b = f; it->off_struct = os; it->size_struct = ss; it->off_str = ostr; it->size_str = sts; it->pos = 0;
    if (total) *total = ts;
    if (ver) *ver = v;
    return 0;
}

/* Evento siguiente: 1 = nodo (name), 2 = fin de nodo, 3 = propiedad (name, val, vlen), 0 = fin, -1 = error */
static int fdt_next(struct fdt_it *it, const uint8_t **name, const uint8_t **val, uint32_t *vlen, size_t *namemax)
{
    for (;;) {
        if (it->pos + 4 > it->size_struct) return -1;
        const uint8_t *s = it->b + it->off_struct;
        uint32_t tok = be32(s + it->pos); it->pos += 4;
        if (tok == FDT_NOP) continue;
        if (tok == FDT_END) return 0;
        if (tok == FDT_END_NODE) return 2;
        if (tok == FDT_BEGIN) {
            size_t max = it->size_struct - it->pos; int l = cstr_len(s + it->pos, max); if (l < 0) return -1;
            *name = s + it->pos; *namemax = (size_t)l; it->pos = (it->pos + (size_t)l + 1 + 3) & ~(size_t)3;
            return it->pos <= it->size_struct ? 1 : -1;
        }
        if (tok == FDT_PROP) {
            if (it->pos + 8 > it->size_struct) return -1;
            uint32_t len = be32(s + it->pos), nameoff = be32(s + it->pos + 4); it->pos += 8;
            if (len > it->size_struct - it->pos || nameoff >= it->size_str) return -1;
            int l = cstr_len(it->b + it->off_str + nameoff, it->size_str - nameoff); if (l < 0) return -1;
            *name = it->b + it->off_str + nameoff; *namemax = (size_t)l; *val = s + it->pos; *vlen = len;
            it->pos = (it->pos + len + 3) & ~(size_t)3;
            return 3;
        }
        return -1;
    }
}

static uint64_t cells(const uint8_t *p, int n) { uint64_t v = 0; for (int i = 0; i < n; i++) v = (v << 32) | be32(p + 4 * i); return v; }
static int pstr(const uint8_t *nm, size_t l, const char *w) { size_t i = 0; while (w[i] && i < l && nm[i] == (uint8_t)w[i]) i++; return !w[i] && i == l; }

int hw_fdt_parse(const uint8_t *fdt, size_t avail, struct hw_fdt *o)
{
    for (size_t i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    struct fdt_it it;
    if (fdt_open(fdt, avail, &it, &o->totalsize, &o->version) != 0) return -1;
    int depth = 0, ac = 2, sc = 1; /* #address-cells / #size-cells de la raiz (valores por defecto de la especificacion) */
    int is_mem[FDT_MAX_DEPTH + 1], is_cpu[FDT_MAX_DEPTH + 1], is_chosen[FDT_MAX_DEPTH + 1];
    const uint8_t *nm, *val; uint32_t vl; size_t nl; int ev, seen_end = 0;
    while ((ev = fdt_next(&it, &nm, &val, &vl, &nl)) > 0) {
        if (ev == 1) {
            if (depth >= FDT_MAX_DEPTH) return -1;
            depth++; o->nnodes++;
            is_mem[depth] = nl >= 6 && pstr(nm, 6, "memory") && (nl == 6 || nm[6] == '@');
            is_cpu[depth] = depth == 3 && nl >= 4 && pstr(nm, 3, "cpu") && nm[3] == '@';
            is_chosen[depth] = depth == 2 && pstr(nm, nl, "chosen");
        } else if (ev == 2) {
            if (depth == 0) return -1;
            depth--;
        } else if (ev == 3) {
            if (depth == 1) {
                if (pstr(nm, nl, "model")) copy_str(o->model, sizeof o->model, val, vl);
                else if (pstr(nm, nl, "compatible")) copy_str(o->compat, sizeof o->compat, val, vl);
                else if (pstr(nm, nl, "#address-cells") && vl == 4) { ac = (int)be32(val); if (ac < 1 || ac > 2) ac = 2; }
                else if (pstr(nm, nl, "#size-cells") && vl == 4) { sc = (int)be32(val); if (sc < 0 || sc > 2) sc = 1; }
            } else if (depth >= 1 && is_chosen[depth] && pstr(nm, nl, "stdout-path")) copy_str(o->stdout_path, sizeof o->stdout_path, val, vl);
            else if (depth >= 1 && is_mem[depth] && pstr(nm, nl, "reg")) {
                uint32_t ent = (uint32_t)(ac + sc) * 4;
                for (uint32_t p = 0; ent && p + ent <= vl && o->nmem < HW_FDT_MAX_MEM; p += ent) {
                    o->mem_base[o->nmem] = cells(val + p, ac); o->mem_size[o->nmem] = cells(val + p + 4 * ac, sc); o->nmem++;
                }
            } else if (depth >= 1 && is_cpu[depth] && pstr(nm, nl, "device_type") && str_has(val, vl, "cpu")) { o->ncpu++; is_cpu[depth] = 0; }
        }
    }
    if (ev < 0 || depth != 0) return -1;
    seen_end = 1; (void)seen_end;
    o->ok = 1;
    return 0;
}

int hw_fdt_find_reg(const uint8_t *fdt, size_t avail, const char *compat, uint64_t *base, uint64_t *size)
{
    struct fdt_it it; if (fdt_open(fdt, avail, &it, 0, 0) != 0) return -1;
    int acs[FDT_MAX_DEPTH + 1], scs[FDT_MAX_DEPTH + 1], match[FDT_MAX_DEPTH + 1], depth = 0;
    uint64_t rb[FDT_MAX_DEPTH + 1], rs[FDT_MAX_DEPTH + 1]; int have[FDT_MAX_DEPTH + 1];
    const uint8_t *nm, *val; uint32_t vl; size_t nl; int ev;
    acs[0] = 2; scs[0] = 1;
    while ((ev = fdt_next(&it, &nm, &val, &vl, &nl)) > 0) {
        if (ev == 1) {
            if (depth >= FDT_MAX_DEPTH) return -1;
            depth++; acs[depth] = 2; scs[depth] = 1; match[depth] = 0; have[depth] = 0; rb[depth] = rs[depth] = 0;
            /* #address-cells/#size-cells del nodo se aplican a sus hijos (por defecto 2 y 1) */
        } else if (ev == 2) {
            if (depth == 0) return -1;
            if (match[depth] && have[depth]) { *base = rb[depth]; *size = rs[depth]; return 0; }
            depth--;
        } else if (ev == 3 && depth >= 1) {
            if (pstr(nm, nl, "compatible") && str_has(val, vl, compat)) match[depth] = 1;
            else if (pstr(nm, nl, "reg") && !have[depth]) {
                int a = acs[depth - 1], s = scs[depth - 1]; uint32_t ent = (uint32_t)(a + s) * 4;
                if (ent && vl >= ent) { rb[depth] = cells(val, a); rs[depth] = cells(val + 4 * a, s); have[depth] = 1; }
            }
            else if (pstr(nm, nl, "#address-cells") && vl == 4) { int c = (int)be32(val); if (c >= 1 && c <= 2) acs[depth] = c; }
            else if (pstr(nm, nl, "#size-cells") && vl == 4) { int c = (int)be32(val); if (c >= 0 && c <= 2) scs[depth] = c; }
        }
    }
    return -1;
}

/* ------------------------------ virtio-pci ------------------------------ */
int hw_virtio_caps(const uint8_t cfg[256], struct hw_virtio_cap *out, int max)
{
    int n = 0;
    if (!(cfg[6] & 0x10)) return 0;                                  /* estado bit 4: lista de capacidades */
    unsigned ptr = cfg[0x34] & 0xFCu;
    for (int guard = 0; guard < 48 && ptr >= 0x40 && ptr + 2 <= 256; guard++) {
        unsigned next = cfg[ptr + 1] & 0xFCu, len = cfg[ptr + 2];
        if (cfg[ptr] == 0x09 && len >= 16 && ptr + len <= 256 && n < max) {
            out[n].type = cfg[ptr + 3]; out[n].bar = cfg[ptr + 4];
            out[n].offset = le32(cfg + ptr + 8); out[n].length = le32(cfg + ptr + 12);
            out[n].notify_mult = (out[n].type == HW_VIRTIO_NOTIFY && len >= 20 && ptr + 20 <= 256) ? le32(cfg + ptr + 16) : 0;
            if (out[n].type >= 1 && out[n].type <= 5 && out[n].bar < 6) n++;
        }
        ptr = next;
    }
    return n;
}

/* ===================================================================== SOMA_156_T480S: SMBIOS
 * Especificacion DMTF DSP0134. Cada estructura: tipo (1), longitud de la parte fija (1), asa (2), campos; despues las
 * cadenas, terminadas cada una en NUL y el conjunto en un NUL extra. Los campos de cadena son indices (1 = primera). */
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
int hw_smbios_entry(const uint8_t *ep, size_t avail, uint64_t *table_phys, uint32_t *table_len, uint8_t *major, uint8_t *minor)
{
    if (!ep) return -1;
    if (avail >= 24 && meq(ep, "_SM3_", 5)) {                     /* SMBIOS 3.x: tabla de 64 bits */
        uint8_t l = ep[6];
        if (l < 24 || l > avail || !sum_ok(ep, l)) return -1;
        *major = ep[7]; *minor = ep[8];
        *table_len = le32(ep + 12); *table_phys = le64(ep + 16);
        return *table_phys && *table_len ? 0 : -1;
    }
    if (avail >= 31 && meq(ep, "_SM_", 4)) {                      /* SMBIOS 2.x */
        uint8_t l = ep[5];
        if (l < 31 || l > avail || !sum_ok(ep, l) || !meq(ep + 16, "_DMI_", 5)) return -1;
        *major = ep[6]; *minor = ep[7];
        *table_len = le16(ep + 22); *table_phys = le32(ep + 24);
        return *table_phys && *table_len ? 0 : -1;
    }
    return -1;
}
/* Cadena numero `idx` del conjunto que empieza en `s` (hasta `end`). Copia saneada (solo ASCII imprimible, sin espacios
 * sobrantes al final). Indice 0 o fuera de rango: cadena vacia. */
static void smb_str(const uint8_t *s, const uint8_t *end, uint8_t idx, char *d, size_t dmax)
{
    d[0] = 0;
    if (!idx) return;
    for (uint8_t k = 1; s < end && *s; k++) {
        const uint8_t *e = s;
        while (e < end && *e) e++;
        if (e >= end) return;
        if (k == idx) {
            size_t n = 0;
            for (const uint8_t *c = s; c < e && n + 1 < dmax; c++) d[n++] = (*c >= 32 && *c < 127) ? (char)*c : '?';
            while (n && d[n - 1] == ' ') n--;
            d[n] = 0;
            return;
        }
        s = e + 1;
    }
}
const char *hw_smbios_memtype(uint8_t t)
{
    switch (t) {
    case 0x12: return "DDR"; case 0x13: return "DDR2"; case 0x18: return "DDR3"; case 0x1A: return "DDR4";
    case 0x1B: return "LPDDR"; case 0x1C: return "LPDDR2"; case 0x1D: return "LPDDR3"; case 0x1E: return "LPDDR4";
    case 0x22: return "DDR5"; case 0x23: return "LPDDR5";
    default: return "?";
    }
}
int hw_smbios_parse(const uint8_t *t, size_t len, uint8_t major, uint8_t minor, struct hw_smbios *o)
{
    for (size_t i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    o->major = major; o->minor = minor;
    if (!t) return -1;
    const uint8_t *p = t, *end = t + len;
    while (p + 4 <= end) {
        uint8_t type = p[0], fl = p[1];
        if (fl < 4 || p + fl > end) break;                        /* estructura truncada o corrupta: se para */
        const uint8_t *str = p + fl, *q = str;                     /* fin del conjunto de cadenas: doble NUL */
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        if (q + 1 >= end) break;
        const uint8_t *next = q + 2;
        o->nstructs++;
        switch (type) {
        case 0:
            if (fl >= 9) { smb_str(str, q + 1, p[4], o->bios_vendor, sizeof o->bios_vendor);
                           smb_str(str, q + 1, p[5], o->bios_version, sizeof o->bios_version);
                           smb_str(str, q + 1, p[8], o->bios_date, sizeof o->bios_date); }
            break;
        case 1:
            if (fl >= 8) { smb_str(str, q + 1, p[4], o->sys_vendor, sizeof o->sys_vendor);
                           smb_str(str, q + 1, p[5], o->sys_product, sizeof o->sys_product);
                           smb_str(str, q + 1, p[6], o->sys_version, sizeof o->sys_version); }
            if (fl >= 0x1B) smb_str(str, q + 1, p[0x1A], o->sys_family, sizeof o->sys_family);
            break;
        case 2:
            if (fl >= 6 && !o->board_product[0]) smb_str(str, q + 1, p[5], o->board_product, sizeof o->board_product);
            break;
        case 4:
            if (fl >= 0x1A && !o->cpu_name[0]) {
                smb_str(str, q + 1, p[0x10], o->cpu_name, sizeof o->cpu_name);
                o->cpu_max_mhz = le16(p + 0x14); o->cpu_cur_mhz = le16(p + 0x16);
                if (fl >= 0x26) { o->cpu_cores = p[0x23]; o->cpu_threads = p[0x25]; }
            }
            break;
        case 16:
            if (fl >= 0x0B) {
                uint32_t kb = le32(p + 7);
                uint64_t mb = (kb == 0x80000000u && fl >= 0x17) ? le64(p + 0x0F) >> 20 : (uint64_t)kb >> 10;
                o->mem_max_mb += mb;
            }
            break;
        case 17:
            if (fl >= 0x15 && o->nmem < HW_SMBIOS_MAX_MEM) {
                int k = o->nmem++;
                uint16_t sz = le16(p + 0x0C);
                uint32_t mb = 0;
                if (sz == 0x7FFF && fl >= 0x20) mb = le32(p + 0x1C) & 0x7FFFFFFFu;
                else if (sz != 0xFFFF) mb = (sz & 0x8000) ? (uint32_t)(sz & 0x7FFF) / 1024u : sz;
                o->mem[k].size_mb = mb;
                o->mem[k].type = p[0x12];
                smb_str(str, q + 1, p[0x10], o->mem[k].locator, sizeof o->mem[k].locator);
                o->mem[k].mhz = le16(p + 0x15);
                if (fl >= 0x22 && le16(p + 0x20)) o->mem[k].mhz = le16(p + 0x20);   /* velocidad configurada (la real) */
                if (fl >= 0x1B) smb_str(str, q + 1, p[0x1A], o->mem[k].part, sizeof o->mem[k].part);
            }
            break;
        case 127:
            o->ok = o->nstructs > 0;
            return o->ok ? 0 : -1;
        }
        p = next;
    }
    o->ok = o->nstructs > 0;
    return o->ok ? 0 : -1;
}
