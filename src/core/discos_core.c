/* SOMA_331_DISCOS: tablas de particiones (MBR y GPT), reconocimiento de sistemas de archivos y formato FAT32. Ver discos_core.h.
 * Todo lo que viene del disco se trata como hostil: firmas, CRC32, tamanos, limites y solapes se comprueban antes de usarse. */
#include "discos_core.h"

static void cero(void *d, uint32_t n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = 0; }
static void copia(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; while (n--) *a++ = *b++; }
static int igual(const void *a, const void *b, uint32_t n) { const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b; while (n--) if (*x++ != *y++) return 0; return 1; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }
static void pon16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void pon32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void pon64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void scopia(char *d, uint32_t cap, const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } if (cap) d[i] = 0; }

uint32_t dc_crc32(const void *d, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)d; uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}

/* GUID de tipo en el orden del disco */
static const uint8_t G_DATOS[16] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
static const uint8_t G_EFI[16]   = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
static const uint8_t G_LINUX[16] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };
static const uint8_t G_MSR[16]   = { 0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D, 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE };
static const uint8_t G_WINRE[16] = { 0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D, 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC };
static const uint8_t G_CERO[16]  = { 0 };

/* ---------------------------------------------------------------- lectura */
static void anade_leida(struct dc_tabla *t) { if (t->n < DC_PART_MAX) t->n++; }
/* comprueba los limites de una particion leida; 0 = se descarta */
static int dentro(const struct dc_tabla *t, uint64_t lba, uint64_t n)
{
    return n && lba >= 1 && lba < t->sectores && n <= t->sectores - lba;
}
static void aviso(struct dc_tabla *t, const char *s) { if (!t->aviso[0]) scopia(t->aviso, sizeof t->aviso, s); }

static int lee_gpt_en(dc_leer_fn rd, void *ctx, struct dc_tabla *t, uint64_t hlba, uint8_t *buf)
{
    if (rd(ctx, hlba, 1, buf)) return -1;
    if (!igual(buf, "EFI PART", 8)) return 1;
    uint32_t hsz = le32(buf + 12);
    if (hsz < 92 || hsz > DC_SECTOR) return 1;
    uint32_t crc = le32(buf + 16);
    pon32(buf + 16, 0);
    if (dc_crc32(buf, hsz) != crc) return 1;
    uint64_t mylba = le64(buf + 24), first = le64(buf + 40), last = le64(buf + 48), elba = le64(buf + 72);
    uint32_t ne = le32(buf + 80), esz = le32(buf + 84), ecrc = le32(buf + 88);
    if (mylba != hlba || esz < 128 || esz > DC_SECTOR || (esz & 7) || ne == 0 || ne > 1024) return 1;
    if (first > last || last >= t->sectores || first < 2) return 1;
    uint64_t ebytes = (uint64_t)ne * esz, esec = (ebytes + DC_SECTOR - 1) / DC_SECTOR;
    if (elba < 2 || elba >= t->sectores || esec > t->sectores - elba || esec > 32) return 1;
    copia(t->disco_guid, buf + 56, 16);
    /* las entradas: hasta 32 sectores (16 KiB, 128 entradas de 128 bytes) en buf + 512 */
    uint8_t *e = buf + DC_SECTOR;
    if (rd(ctx, elba, (uint32_t)esec, e)) return -1;
    if (dc_crc32(e, (uint32_t)ebytes) != ecrc) return 1;
    t->esquema = DC_GPT; t->usable_ini = first; t->usable_fin = last; t->n = 0;
    for (uint32_t i = 0; i < ne; i++) {
        const uint8_t *q = e + (uint64_t)i * esz;
        if (igual(q, G_CERO, 16)) continue;
        uint64_t a = le64(q + 32), b = le64(q + 40);
        if (b < a || a < first || b > last) { aviso(t, "una entrada de la GPT se sale del disco: no se muestra"); continue; }
        if (t->n >= DC_PART_MAX) { aviso(t, "hay mas particiones de las que se muestran"); break; }
        struct dc_part *p = &t->p[t->n];
        cero(p, sizeof *p);
        p->lba = a; p->n = b - a + 1;
        copia(p->tipo, q, 16); copia(p->uuid, q + 16, 16); p->attrs = le64(q + 48);
        uint32_t o = 0;                                    /* nombre: UTF-16LE (36 caracteres) -> UTF-8 */
        for (int k = 0; k < 36; k++) {
            uint32_t c = le16(q + 56 + 2 * k);
            if (!c) break;
            if (c >= 0xD800 && c < 0xE000) c = '?';
            if (c < 0x80) { if (o + 1 < sizeof p->nombre) p->nombre[o++] = (char)(c < 32 ? '?' : c); }
            else if (c < 0x800) { if (o + 2 < sizeof p->nombre) { p->nombre[o++] = (char)(0xC0 | (c >> 6)); p->nombre[o++] = (char)(0x80 | (c & 63)); } }
            else if (o + 3 < sizeof p->nombre) { p->nombre[o++] = (char)(0xE0 | (c >> 12)); p->nombre[o++] = (char)(0x80 | ((c >> 6) & 63)); p->nombre[o++] = (char)(0x80 | (c & 63)); }
        }
        p->nombre[o] = 0;
        anade_leida(t);
    }
    return 0;
}

static void lee_mbr_entradas(dc_leer_fn rd, void *ctx, struct dc_tabla *t, const uint8_t *s0, uint8_t *buf)
{
    t->esquema = DC_MBR; t->mbr_firma = le32(s0 + 440); t->n = 0;
    t->usable_ini = 1; t->usable_fin = t->sectores ? t->sectores - 1 : 0;
    uint64_t ext_ini = 0, ext_n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = s0 + 446 + 16 * i;
        uint8_t tipo = e[4]; uint64_t a = le32(e + 8), n = le32(e + 12);
        if (!tipo || !n) continue;
        if (!dentro(t, a, n)) { aviso(t, "una entrada del MBR se sale del disco: no se muestra"); continue; }
        if (t->n >= DC_PART_MAX) break;
        struct dc_part *p = &t->p[t->n];
        cero(p, sizeof *p);
        p->lba = a; p->n = n; p->mbr_tipo = tipo; p->activa = e[0] == 0x80;
        anade_leida(t);
        if ((tipo == 0x05 || tipo == 0x0F || tipo == 0x85) && !ext_n) { ext_ini = a; ext_n = n; }
    }
    /* unidades logicas: cadena de EBR dentro de la extendida (como mucho 12 y sin volver atras) */
    uint64_t ebr = ext_ini, prev = 0;
    for (int k = 0; ext_n && k < 12 && t->n < DC_PART_MAX; k++) {
        if (ebr < ext_ini || ebr >= ext_ini + ext_n || ebr <= prev) break;
        if (rd(ctx, ebr, 1, buf) || buf[510] != 0x55 || buf[511] != 0xAA) break;
        const uint8_t *e = buf + 446;
        uint64_t a = ebr + le32(e + 8), n = le32(e + 12);
        if (e[4] && n && dentro(t, a, n) && a >= ext_ini && a + n <= ext_ini + ext_n) {
            struct dc_part *p = &t->p[t->n];
            cero(p, sizeof *p);
            p->lba = a; p->n = n; p->mbr_tipo = e[4]; p->logica = 1;
            anade_leida(t);
        }
        uint64_t sig = le32(e + 16 + 8);
        if (!e[16 + 4] || !sig) break;
        prev = ebr; ebr = ext_ini + sig;
    }
}

int dc_lee(dc_leer_fn rd, void *ctx, uint64_t sectores, struct dc_tabla *t, uint8_t *buf)
{
    cero(t, sizeof *t);
    t->sectores = sectores;
    if (sectores < 2) return 0;
    if (rd(ctx, 0, 1, buf)) return -1;
    uint8_t s0[DC_SECTOR]; copia(s0, buf, DC_SECTOR);
    int hay_mbr = s0[510] == 0x55 && s0[511] == 0xAA;
    int protector = 0;
    if (hay_mbr) for (int i = 0; i < 4; i++) if (s0[446 + 16 * i + 4] == 0xEE) protector = 1;
    if (protector || !hay_mbr) {                     /* GPT (tambien sin MBR: algunos discos lo tienen borrado) */
        int r = lee_gpt_en(rd, ctx, t, 1, buf);
        if (r < 0) return -1;
        if (r == 0) return 0;
        r = lee_gpt_en(rd, ctx, t, sectores - 1, buf);   /* la copia del final */
        if (r < 0) return -1;
        if (r == 0) { t->gpt_respaldo = 1; aviso(t, "la GPT principal esta danada: se ha leido la copia del final"); return 0; }
        cero(t, sizeof *t); t->sectores = sectores;
        if (protector) { aviso(t, "el disco dice tener GPT pero esta danada"); return 0; }
    }
    if (hay_mbr) {
        /* un sector de arranque de un sistema de archivos (disco sin tablas) tambien acaba en 55 AA: no es un MBR */
        if (igual(s0 + 3, "EXFAT   ", 8) || igual(s0 + 3, "NTFS    ", 8) || igual(s0 + 82, "FAT32   ", 8) || igual(s0 + 54, "FAT1", 4)) return 0;
        lee_mbr_entradas(rd, ctx, t, s0, buf);
    }
    return 0;
}

/* ---------------------------------------------------------------- tablas nuevas */
static uint32_t g_azar;
static uint32_t azar(void) { g_azar = g_azar * 1664525u + 1013904223u; return g_azar ^ (g_azar >> 16); }
static void guid_nuevo(uint8_t *g, uint32_t semilla)
{
    g_azar ^= semilla;
    for (int i = 0; i < 16; i += 4) pon32(g + i, azar());
    g[7] = (uint8_t)((g[7] & 0x0F) | 0x40);           /* version 4 (el byte alto de time_hi, en el orden del disco) */
    g[8] = (uint8_t)((g[8] & 0x3F) | 0x80);
}
void dc_nueva(struct dc_tabla *t, int esquema, uint64_t sectores, uint32_t semilla)
{
    cero(t, sizeof *t);
    t->sectores = sectores; t->esquema = esquema;
    g_azar = semilla * 2654435761u + 7;
    if (esquema == DC_GPT) {
        t->usable_ini = 34; t->usable_fin = sectores > 34 ? sectores - 34 : 0;
        guid_nuevo(t->disco_guid, semilla);
    } else {
        t->usable_ini = 1; t->usable_fin = sectores ? sectores - 1 : 0;
        t->mbr_firma = azar() | 1;
    }
}

int dc_valida(const struct dc_tabla *t, const char **why)
{
    static const char *d; if (!why) why = &d;
    *why = "";
    if (t->esquema != DC_MBR && t->esquema != DC_GPT) { *why = "sin tabla de particiones"; return -1; }
    if (t->n < 0 || t->n > DC_PART_MAX || (t->esquema == DC_MBR && t->n > 4)) { *why = "demasiadas particiones"; return -1; }
    for (int i = 0; i < t->n; i++) {
        const struct dc_part *p = &t->p[i];
        if (p->logica) { *why = "no se pueden escribir unidades logicas (MBR extendida)"; return -1; }
        if (!p->n || p->lba < t->usable_ini || p->lba > t->usable_fin || p->n > t->usable_fin - p->lba + 1) { *why = "una particion se sale del disco"; return -1; }
        if (t->esquema == DC_MBR && (p->lba > 0xFFFFFFFFull || p->n > 0xFFFFFFFFull)) { *why = "el disco es demasiado grande para MBR (usa GPT)"; return -1; }
        for (int j = 0; j < i; j++) {
            const struct dc_part *q = &t->p[j];
            if (p->lba < q->lba + q->n && q->lba < p->lba + p->n) { *why = "hay particiones que se solapan"; return -1; }
        }
    }
    return 0;
}

int dc_hueco(const struct dc_tabla *t, uint64_t *ini, uint64_t *n)
{
    /* las particiones ordenadas por su inicio; los huecos estan entre ellas (alineados a 1 MiB) */
    int ord[DC_PART_MAX], k = t->n < DC_PART_MAX ? t->n : DC_PART_MAX;
    for (int i = 0; i < k; i++) ord[i] = i;
    for (int i = 1; i < k; i++) for (int j = i; j > 0 && t->p[ord[j]].lba < t->p[ord[j - 1]].lba; j--) { int x = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = x; }
    uint64_t mejor_i = 0, mejor_n = 0, desde = t->usable_ini;
    for (int i = 0; i <= k; i++) {
        uint64_t hasta = i < k ? t->p[ord[i]].lba : t->usable_fin + 1;     /* fin exclusivo del hueco */
        uint64_t a = (desde + DC_ALINEA - 1) / DC_ALINEA * DC_ALINEA;
        if (a < hasta && hasta - a > mejor_n) { mejor_n = hasta - a; mejor_i = a; }
        if (i < k) { uint64_t f = t->p[ord[i]].lba + t->p[ord[i]].n; if (f > desde) desde = f; }
    }
    if (mejor_n < DC_ALINEA) return -1;
    *ini = mejor_i; *n = mejor_n / DC_ALINEA * DC_ALINEA;      /* tamano en MiB enteros */
    return 0;
}
int dc_anade(struct dc_tabla *t, uint64_t n, int tipo, const char *nombre, uint32_t semilla)
{
    if ((t->esquema == DC_MBR && t->n >= 4) || t->n >= DC_PART_MAX) return -1;
    uint64_t ini, libre;
    if (dc_hueco(t, &ini, &libre) != 0) return -2;
    if (!n || n > libre) n = libre;
    n = n / DC_ALINEA * DC_ALINEA;
    if (!n) return -2;
    struct dc_part *p = &t->p[t->n];
    cero(p, sizeof *p);
    p->lba = ini; p->n = n;
    if (t->esquema == DC_GPT) {
        copia(p->tipo, tipo == DC_TIPO_EFI ? G_EFI : tipo == DC_TIPO_LINUX ? G_LINUX : G_DATOS, 16);
        g_azar ^= semilla + (uint32_t)t->n * 977u;
        guid_nuevo(p->uuid, semilla + (uint32_t)t->n);
        scopia(p->nombre, sizeof p->nombre, nombre ? nombre : "");
    } else p->mbr_tipo = tipo == DC_TIPO_EFI ? 0xEF : tipo == DC_TIPO_LINUX ? 0x83 : 0x07;   /* 0x07: exFAT/NTFS; FAT32 pone 0x0C al formatear */
    const char *why;
    t->n++;
    if (dc_valida(t, &why) != 0) { t->n--; return -3; }
    return t->n - 1;
}
int dc_quita(struct dc_tabla *t, int i)
{
    if (i < 0 || i >= t->n) return -1;
    for (int k = i; k + 1 < t->n; k++) t->p[k] = t->p[k + 1];
    t->n--;
    return 0;
}

/* ---------------------------------------------------------------- escritura */
static void chs(uint8_t *o, uint64_t lba) { (void)lba; o[0] = 0xFE; o[1] = 0xFF; o[2] = 0xFF; }   /* LBA moderno: CHS "fuera de rango" */
static void entrada_mbr(uint8_t *e, uint8_t tipo, uint64_t lba, uint64_t n, int activa)
{
    e[0] = activa ? 0x80 : 0;
    chs(e + 1, lba); e[4] = tipo; chs(e + 5, lba + n - 1);
    pon32(e + 8, (uint32_t)lba); pon32(e + 12, (uint32_t)(n > 0xFFFFFFFFull ? 0xFFFFFFFFull : n));
}
static int escribe_gpt_copia(dc_escribir_fn wr, void *ctx, const struct dc_tabla *t, uint8_t *buf, int principal)
{
    uint8_t *h = buf, *e = buf + DC_SECTOR;          /* cabecera + 32 sectores de entradas (128 de 128 bytes) */
    cero(e, 32 * DC_SECTOR);
    for (int i = 0; i < t->n; i++) {
        uint8_t *q = e + 128 * i; const struct dc_part *p = &t->p[i];
        copia(q, p->tipo, 16); copia(q + 16, p->uuid, 16);
        pon64(q + 32, p->lba); pon64(q + 40, p->lba + p->n - 1); pon64(q + 48, p->attrs);
        for (int k = 0, o = 0; k < 36 && p->nombre[o]; k++) {     /* UTF-8 -> UTF-16LE (BMP) */
            uint32_t c = (uint8_t)p->nombre[o++];
            if (c >= 0xE0 && p->nombre[o] && p->nombre[o + 1]) { c = ((c & 15) << 12) | (((uint8_t)p->nombre[o] & 63) << 6) | ((uint8_t)p->nombre[o + 1] & 63); o += 2; }
            else if (c >= 0xC0 && p->nombre[o]) { c = ((c & 31) << 6) | ((uint8_t)p->nombre[o] & 63); o += 1; }
            else if (c >= 0x80) c = '?';
            pon16(q + 56 + 2 * k, (uint16_t)c);
        }
    }
    uint32_t ecrc = dc_crc32(e, 128 * 128);
    uint64_t ultimo = t->sectores - 1;
    uint64_t hlba = principal ? 1 : ultimo, otra = principal ? ultimo : 1, elba = principal ? 2 : ultimo - 32;
    cero(h, DC_SECTOR);
    copia(h, "EFI PART", 8); pon32(h + 8, 0x00010000u); pon32(h + 12, 92);
    pon64(h + 24, hlba); pon64(h + 32, otra); pon64(h + 40, t->usable_ini); pon64(h + 48, t->usable_fin);
    copia(h + 56, t->disco_guid, 16); pon64(h + 72, elba); pon32(h + 80, 128); pon32(h + 84, 128); pon32(h + 88, ecrc);
    pon32(h + 16, dc_crc32(h, 92));
    if (wr(ctx, elba, 32, e)) return -1;
    if (wr(ctx, hlba, 1, h)) return -1;
    return 0;
}
int dc_escribe(dc_escribir_fn wr, void *ctx, const struct dc_tabla *t, uint8_t *buf)
{
    const char *why;
    if (dc_valida(t, &why) != 0) return -2;
    if (t->esquema == DC_GPT && (t->sectores < 34 * 2 + DC_ALINEA || t->n > 128)) return -2;
    uint8_t s0[DC_SECTOR]; cero(s0, sizeof s0);
    if (t->esquema == DC_GPT) {
        uint64_t n = t->sectores - 1;
        entrada_mbr(s0 + 446, 0xEE, 1, n > 0xFFFFFFFFull ? 0xFFFFFFFFull : n, 0);
        s0[446 + 1] = 0; s0[446 + 2] = 2; s0[446 + 3] = 0;   /* CHS de inicio 0/0/2 como pide la especificacion */
        if (escribe_gpt_copia(wr, ctx, t, buf, 0) || escribe_gpt_copia(wr, ctx, t, buf, 1)) return -1;   /* primero la copia del final */
    } else {
        pon32(s0 + 440, t->mbr_firma);
        for (int i = 0; i < t->n; i++) entrada_mbr(s0 + 446 + 16 * i, t->p[i].mbr_tipo, t->p[i].lba, t->p[i].n, t->p[i].activa);
    }
    s0[510] = 0x55; s0[511] = 0xAA;
    return wr(ctx, 0, 1, s0) ? -1 : 0;
}

/* ---------------------------------------------------------------- sistemas de archivos */
static void etiqueta_ascii(char *o, uint32_t cap, const uint8_t *s, uint32_t n)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n && k + 1 < cap; i++) { uint8_t c = s[i]; o[k++] = (char)(c >= 32 && c < 127 ? c : '?'); }
    while (k && o[k - 1] == ' ') k--;
    if (cap) o[k] = 0;
}
int dc_fs(dc_leer_fn rd, void *ctx, uint64_t lba, uint64_t n, char *etiqueta, uint32_t cap, uint8_t *buf4k)
{
    if (cap) etiqueta[0] = 0;
    if (n < 8 || rd(ctx, lba, 8, buf4k)) return DC_FS_DESCONOCIDO;
    const uint8_t *b = buf4k;
    int vacio = 1; for (uint32_t i = 0; i < 4096 && vacio; i++) if (b[i]) vacio = 0;
    if (igual(b + 3, "EXFAT   ", 8)) {
        /* la etiqueta esta en el directorio raiz (entrada 0x83): se lee el primer cluster de la raiz */
        uint32_t fat_off = le32(b + 80), heap = le32(b + 88), raiz = le32(b + 96); uint8_t bps = b[108], spc = b[109];
        (void)fat_off;
        if (bps == 9 && spc < 16 && raiz >= 2 && heap < n) {
            uint64_t s = (uint64_t)heap + ((uint64_t)(raiz - 2) << spc);
            if (s + 8 <= n && rd(ctx, lba + s, 8, buf4k) == 0) {
                for (int i = 0; i < 128; i++) {
                    const uint8_t *e = buf4k + 32 * i;
                    if (e[0] == 0x83 && e[1] <= 11 && cap) {
                        uint32_t k = 0;
                        for (int j = 0; j < e[1] && k + 2 < cap; j++) { uint16_t c = le16(e + 2 + 2 * j); if (c < 0x80) etiqueta[k++] = (char)(c >= 32 ? c : '?'); else etiqueta[k++] = '?'; }
                        etiqueta[k] = 0; break;
                    }
                    if (!e[0]) break;
                }
            }
        }
        return DC_FS_EXFAT;
    }
    if (igual(b + 3, "NTFS    ", 8)) return DC_FS_NTFS;
    if (b[510] == 0x55 && b[511] == 0xAA && le16(b + 11) == 512) {
        if (igual(b + 82, "FAT32   ", 8)) { if (b[66] == 0x29) etiqueta_ascii(etiqueta, cap, b + 71, 11); return DC_FS_FAT32; }
        if (igual(b + 54, "FAT16   ", 8) || igual(b + 54, "FAT12   ", 8)) {
            if (b[38] == 0x29) etiqueta_ascii(etiqueta, cap, b + 43, 11);
            return b[58] == '6' ? DC_FS_FAT16 : DC_FS_FAT12;
        }
    }
    if (le16(b + 1024 + 56) == 0xEF53) { etiqueta_ascii(etiqueta, cap, b + 1024 + 120, 16); for (uint32_t i = 0; i < cap && etiqueta[i]; i++) if (etiqueta[i] == '?') { etiqueta[i] = 0; break; } return DC_FS_EXT; }
    if (igual(b, "BIGFS", 5) || igual(b, "SOMABIGF", 8)) return DC_FS_BIGFS;
    if (n >= 17 * 4 && rd(ctx, lba + 64, 4, buf4k) == 0 && buf4k[1] == 'C' && igual(buf4k + 1, "CD001", 5)) { etiqueta_ascii(etiqueta, cap, buf4k + 40, 32); return DC_FS_ISO9660; }
    return vacio ? DC_FS_VACIO : DC_FS_DESCONOCIDO;
}
const char *dc_fs_nombre(int fs)
{
    static const char *const N[DC_FS_N] = { "desconocido", "vacio", "exFAT", "FAT12", "FAT16", "FAT32", "NTFS", "ext2/3/4", "BIGFS (SOMA)", "ISO 9660" };
    return fs >= 0 && fs < DC_FS_N ? N[fs] : "?";
}
const char *dc_tipo_nombre(const struct dc_tabla *t, const struct dc_part *p)
{
    if (t->esquema == DC_GPT) {
        if (igual(p->tipo, G_DATOS, 16)) return "Datos";
        if (igual(p->tipo, G_EFI, 16)) return "Sistema EFI";
        if (igual(p->tipo, G_LINUX, 16)) return "Linux";
        if (igual(p->tipo, G_MSR, 16)) return "Reservada de Microsoft";
        if (igual(p->tipo, G_WINRE, 16)) return "Recuperacion de Windows";
        return "Otro tipo";
    }
    switch (p->mbr_tipo) {
        case 0x01: case 0x04: case 0x06: case 0x0E: return "FAT";
        case 0x07: return "exFAT/NTFS";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x05: case 0x0F: case 0x85: return "Extendida";
        case 0x82: return "Intercambio Linux";
        case 0x83: return "Linux";
        case 0xEF: return "Sistema EFI";
        case 0xDA: return "Datos (SOMA)";
        default: return "Otro tipo";
    }
}

/* ---------------------------------------------------------------- FAT32 */
int dc_fat32(dc_escribir_fn wr, void *ctx, uint64_t lba, uint64_t n, const char *etiqueta, uint32_t serie, uint8_t *buf)
{
    if (n < DC_FAT32_MIN || n > 0xFFFFFFFFull) return -2;
    uint32_t total = (uint32_t)n;
    uint32_t spc = n <= 532480ull ? 1 : n <= 16777216ull ? 8 : n <= 33554432ull ? 16 : n <= 67108864ull ? 32 : 64;   /* 260 MiB: 512 B; 8 GiB: 4 KiB; 16: 8 KiB; 32: 16 KiB; mas: 32 KiB */
    const uint32_t reservados = 32, nfat = 2;
    uint32_t fatsz, clusters;
    for (;;) {
        /* tamano de cada FAT (formula de Microsoft): suficiente para todos los clusters */
        uint32_t t1 = total - reservados, t2 = (256u * spc + nfat) / 2;   /* FAT32: entradas de 4 bytes */
        fatsz = (t1 + t2 - 1) / t2;
        clusters = (total - reservados - nfat * fatsz) / spc;
        if (clusters >= 65525 || spc == 1) break;
        spc /= 2;                                   /* pocos clusters: mas pequenos (FAT32 exige al menos 65525) */
    }
    if (clusters < 65525 || clusters > 0x0FFFFFF5u) return -2;
    cero(buf, 4096);
    uint8_t *b = buf;
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90; copia(b + 3, "MSWIN4.1", 8);
    pon16(b + 11, 512); b[13] = (uint8_t)spc; pon16(b + 14, (uint16_t)reservados); b[16] = (uint8_t)nfat;
    b[21] = 0xF8; pon16(b + 24, 63); pon16(b + 26, 255); pon32(b + 28, (uint32_t)(lba > 0xFFFFFFFFull ? 0 : lba)); pon32(b + 32, total);
    pon32(b + 36, fatsz); pon32(b + 44, 2); pon16(b + 48, 1); pon16(b + 50, 6);
    b[64] = 0x80; b[66] = 0x29; pon32(b + 67, serie);
    char et[11]; for (int i = 0; i < 11; i++) et[i] = ' ';
    for (int i = 0; etiqueta && etiqueta[i] && i < 11; i++) { char c = etiqueta[i]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); et[i] = (c >= 32 && c < 127 && c != '"' && c != '*' && c != '/' && c != ':' && c != '<' && c != '>' && c != '?' && c != '\\' && c != '|') ? c : '_'; }
    copia(b + 71, et, 11); copia(b + 82, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t *fsi = buf + 512;                       /* FSInfo */
    pon32(fsi, 0x41615252u); pon32(fsi + 484, 0x61417272u); pon32(fsi + 488, clusters - 1); pon32(fsi + 492, 3); pon32(fsi + 508, 0xAA550000u);
    if (wr(ctx, lba, 2, buf) || wr(ctx, lba + 6, 2, buf)) return -1;   /* arranque y FSInfo, y su copia en el sector 6 */
    /* reservados restantes a cero (2..5 y 8..31) */
    cero(buf, 65536);
    if (wr(ctx, lba + 2, 4, buf) || wr(ctx, lba + 8, reservados - 8, buf)) return -1;
    /* las dos FAT: a cero salvo las tres primeras entradas (medio, fin de cadena y la raiz en el cluster 2) */
    for (uint32_t f = 0; f < nfat; f++) {
        uint64_t s = lba + reservados + (uint64_t)f * fatsz;
        for (uint32_t k = 0; k < fatsz; k += 128) { uint32_t c = fatsz - k < 128 ? fatsz - k : 128; if (wr(ctx, s + k, c, buf)) return -1; }
    }
    pon32(buf, 0x0FFFFFF8u); pon32(buf + 4, 0x0FFFFFFFu); pon32(buf + 8, 0x0FFFFFFFu);
    for (uint32_t f = 0; f < nfat; f++) if (wr(ctx, lba + reservados + (uint64_t)f * fatsz, 1, buf)) return -1;
    cero(buf, 16);
    /* la raiz (cluster 2) a cero, con la etiqueta como primera entrada */
    uint64_t raiz = lba + reservados + (uint64_t)nfat * fatsz;
    copia(buf, et, 11); buf[11] = 0x08;
    if (wr(ctx, raiz, spc, buf)) return -1;
    return 0;
}
