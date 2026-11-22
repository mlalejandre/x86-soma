#include <stdint.h>

#include "console.h"
#include "mem.h"
#include "srcfs.h"
#include "sysdisk.h"
#include "web_inflate.h"   /* PARCHE 140: SRCFS v02 comprimido (en x86_64 se descomprime en el servicio aislado zdec) */

/*
 * Formato (lo escribe ejecutar.py::pack_sources):
 *   LBA 8192..8319  indice (128 sectores), entradas de 64 bytes. La entrada 0 es la
 *                   cabecera: "SRCFSv01" + u32 count (little-endian).
 *                   Entradas 1..count: char name[48]; u32 lba; u32 size; pad[8].
 *   LBA 8320..      datos, cada archivo alineado a sector (hasta LBA 16383).
 * SOMA_PARCHE_011_SRCFS: el indice tenia 8 sectores (63 entradas) y solo se empaquetaba la
 * mitad de las fuentes; con 32 caben 255. SRCFS_INDEX_SECTORS debe coincidir con
 * SRCFS_INDEX_SECTORS de ejecutar.py y con SOMA_LBA_SRCFS_DATA - SOMA_LBA_SRCFS_INDEX
 * (python3 tools/audit.py lo comprueba).
 * PARCHE 107: con 32 sectores (255 entradas) y mas de 320 fuentes, el empaquetado cortaba por orden alfabetico y dejaba fuera net/,
 * lang/, ui/ y user/ (el agente no podia leerlas con src_cat). Ahora 64 sectores = 511 entradas.
 * SOMA_267_SRCFS: 128 sectores = 1023 entradas (543 fuentes tras los parches 265 y 267: las ultimas por orden alfabetico, user/, quedaban fuera).
 * Un disco antiguo (indice de 64 sectores) se sigue leyendo: sus entradas estan en los primeros 64 y cada una lleva su LBA.
 * PARCHE 140: "SRCFSv02": entrada = name[48], u32 lba, u32 tamano original, u32 tamano guardado, u32 flags (bit 0 = zlib). El codigo C
 * ocupa ~4 veces menos y vuelve a caber todo. Se siguen leyendo discos "SRCFSv01" (sin comprimir).
 */
#define SRCFS_LBA            8192
#define SRCFS_INDEX_SECTORS  128   /* SOMA_267_SRCFS: 1023 entradas (con el compilador de C del 265 hay mas de 511 fuentes) */
#define SRCFS_ENTRY_SIZE     64
#define SRCFS_NAME_MAX       48
#define SRCFS_PAGE           768
#define SRCFS_LINE_MAX       160

static uint8_t srcfs_index[SRCFS_INDEX_SECTORS * 512];
static uint8_t srcfs_sec[512];
static int srcfs_v2;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* PARCHE 140: contenido COMPLETO de una entrada (descomprimido si hace falta). Se guarda el ultimo en cache: paginar con src_cat
 * no vuelve a leer ni a descomprimir. Devuelve 0 o <0 (-1 E/S o memoria, -2 datos comprimidos corruptos). */
static uint8_t *cache_buf;
static uint32_t cache_lba, cache_size, cache_ok;
static int srcfs_file(const uint8_t *e, const uint8_t **data, uint32_t *size)
{
    uint32_t lba = le32(e + 48), osize = le32(e + 52);
    uint32_t csize = srcfs_v2 ? le32(e + 56) : osize, flags = srcfs_v2 ? le32(e + 60) : 0;
    if (cache_ok && cache_lba == lba && cache_size == osize) { *data = cache_buf; *size = osize; return 0; }
    if (csize > (16u << 20) || osize > (64u << 20)) return -2;
    uint8_t *raw = (uint8_t *)kmalloc(csize ? csize : 1);
    if (!raw) return -1;
    for (uint32_t p = 0; p < csize; p += 512) {
        if (sysdisk_read((uint64_t)(lba + p / 512), srcfs_sec) != 0) { kfree(raw); return -1; }
        uint32_t take = csize - p < 512 ? csize - p : 512;
        memcpy(raw + p, srcfs_sec, take);
    }
    uint8_t *buf = raw;
    if (flags & 1) {
        struct wb o; o.p = 0; o.len = o.cap = 0; o.err = 0;
        int r = wz_inflate(raw, csize, 1, &o, osize);
        kfree(raw);
        if (r < 0 || o.len != osize) { if (o.p) kfree(o.p); return o.err ? -1 : -2; }
        buf = (uint8_t *)o.p;
        if (!buf) { buf = (uint8_t *)kmalloc(1); if (!buf) return -1; }
    } else if (csize != osize) { kfree(raw); return -2; }
    if (cache_buf) kfree(cache_buf);
    cache_buf = buf; cache_lba = lba; cache_size = osize; cache_ok = 1;
    *data = buf; *size = osize;
    return 0;
}




static void out_char(char *out, uint32_t max, uint32_t *pos, char c)
{
    if (*pos + 1 < max) {
        out[*pos] = c;
        out[*pos + 1] = '\0';
        (*pos)++;
    }
}


static void out_str(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    while (*s) {
        out_char(out, max, pos, *s++);
    }
}


static void out_dec(char *out, uint32_t max, uint32_t *pos, uint32_t v)
{
    char tmp[10];
    int n = 0;

    if (v == 0) {
        out_char(out, max, pos, '0');
        return;
    }

    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }

    while (n) {
        out_char(out, max, pos, tmp[--n]);
    }
}


static void console_dump(const char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;

        if (c == '\n' || (c >= 32 && c < 127)) {
            kputc((char)c);
        } else if (c != '\r') {
            kputc('.');
        }
    }
}


static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }

    return *a == *b;
}


static const char *strip_src(const char *n)
{
    if (n[0] == '/') n++;
    if (n[0] == 's' && n[1] == 'r' && n[2] == 'c' && n[3] == '/') n += 4;
    return n;
}


static int word_is(const char *s, const char *w)
{
    while (*w) {
        if (*s != *w) return 0;
        s++;
        w++;
    }

    return *s == '\0' || *s == ' ';
}


/* Carga el indice. Devuelve el numero de archivos o <0 si no hay. */
static int srcfs_load_index(void)
{
    for (int i = 0; i < SRCFS_INDEX_SECTORS; ++i) {
        if (sysdisk_read((uint64_t)(SRCFS_LBA + i), srcfs_index + i * 512) != 0) {
            return -1;
        }
    }

    if (memcmp(srcfs_index, "SRCFSv01", 8) == 0) srcfs_v2 = 0;
    else if (memcmp(srcfs_index, "SRCFSv02", 8) == 0) srcfs_v2 = 1;
    else return -2;

    uint32_t count = le32(srcfs_index + 8);
    uint32_t cap = SRCFS_INDEX_SECTORS * 512 / SRCFS_ENTRY_SIZE - 1;

    return (int)(count > cap ? cap : count);
}


static const uint8_t *find_entry(int count, const char *name)
{
    name = strip_src(name);
    uint32_t qlen = 0;
    while (name[qlen]) qlen++;

    for (int i = 1; i <= count; ++i) {
        const uint8_t *e = srcfs_index + i * SRCFS_ENTRY_SIZE;
        const char *ename = (const char *)e;
        if (name_eq(ename, name)) return e;

        uint32_t elen = 0;
        while (ename[elen]) elen++;
        if (elen > qlen && ename[elen - qlen - 1] == '/') {
            if (name_eq(ename + (elen - qlen), name)) return e;
        }
    }

    return 0;
}


static int no_index(char *out, uint32_t max, int count)
{
    uint32_t pos = 0;

    out_str(out, max, &pos,
            count == -1 ? "Error de I/O leyendo el indice de fuentes."
                        : "Error: no hay fuentes en el disco (arranca con ejecutar.py).");
    return -1;
}


/* SOMA_PARCHE_011_SRCFS: lista con filtro opcional por prefijo ("net/", "core/mutex"). Antes el
 * listado se cortaba EN SILENCIO al llenarse el buffer de salida; ahora avisa de cuantas
 * entradas quedaron fuera y como verlas. Devuelve el numero de entradas que coinciden. */
static int has_prefix(const char *name, const char *prefix)
{
    if (!prefix) return 1;
    while (*prefix) {
        if (*name != *prefix) return 0;
        name++; prefix++;
    }
    return 1;
}

static uint32_t cstr_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

int srcfs_ls_filter(const char *prefix, char *out, uint32_t max)
{
    uint32_t pos = 0;
    int matched = 0, shown = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    if (prefix && !*prefix) prefix = 0;
    out_str(out, max, &pos, "Archivos fuente (nombre bytes):\n");

    for (int i = 1; i <= count; ++i) {
        const uint8_t *e = srcfs_index + i * SRCFS_ENTRY_SIZE;
        const char *name = (const char *)e;

        if (!has_prefix(name, prefix)) continue;
        matched++;

        /* nombre + ' ' + hasta 10 digitos + '\n', dejando sitio para el aviso final (~96 bytes) */
        if (pos + cstr_len(name) + 12 + 96 >= max) continue;

        out_str(out, max, &pos, name);
        out_char(out, max, &pos, ' ');
        out_dec(out, max, &pos, le32(e + 52));
        out_char(out, max, &pos, '\n');
        shown++;
    }

    if (shown < matched) {
        out_str(out, max, &pos, "... y ");
        out_dec(out, max, &pos, (uint32_t)(matched - shown));
        out_str(out, max, &pos, " mas: acota con src_ls <prefijo> (p. ej. src_ls net/)\n");
    }

    return matched;
}

int srcfs_ls(char *out, uint32_t max)
{
    return srcfs_ls_filter(0, out, max);
}


int srcfs_cat(const char *name, uint32_t offset, char *out, uint32_t max)
{
    uint32_t pos = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    const uint8_t *e = find_entry(count, name);

    if (!e) {
        out_str(out, max, &pos, "Error: archivo fuente no encontrado: ");
        out_str(out, max, &pos, strip_src(name));
        out_str(out, max, &pos, " (usa src_ls)");
        return -1;
    }

    const uint8_t *data; uint32_t size;
    int fr = srcfs_file(e, &data, &size);
    if (fr < 0) {
        out_str(out, max, &pos, fr == -1 ? "Error de I/O (o sin memoria) leyendo el disco." : "Error: datos de fuente corruptos en el disco.");
        return -1;
    }

    if (offset >= size) {
        out_str(out, max, &pos, "Fin del archivo (tamano ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes).");
        return 0;
    }

    uint32_t n = size - offset;

    if (n > SRCFS_PAGE) n = SRCFS_PAGE;
    if (max > 160 && n > max - 160) n = max - 160;

    for (uint32_t k = 0; k < n; ++k) {
        char c = (char)data[offset + k];
        if (c != '\r') out_char(out, max, &pos, c);
    }

    if (offset + n < size) {
        out_str(out, max, &pos, "\n--- siguiente OFFSET: ");
        out_dec(out, max, &pos, offset + n);
        out_str(out, max, &pos, " (total ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes) ---");
    } else {
        out_str(out, max, &pos, "\n--- fin del archivo (total ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes) ---");
    }

    return (int)n;
}


static int contains(const char *hay, const char *needle)
{
    if (!*needle) return 1;

    for (; *hay; ++hay) {
        const char *h = hay;
        const char *n = needle;

        while (*h && *n && *h == *n) {
            h++;
            n++;
        }

        if (!*n) return 1;
    }

    return 0;
}


int srcfs_grep(const char *name, const char *pat, char *out, uint32_t max)
{
    uint32_t pos = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    const uint8_t *e = find_entry(count, name);

    if (!e) {
        out_str(out, max, &pos, "Error: archivo fuente no encontrado: ");
        out_str(out, max, &pos, strip_src(name));
        return -1;
    }

    /* Limpiar espacios iniciales y comillas envolventes (' o ") */
    while (*pat == ' ' || *pat == '\t') pat++;

    char clean_pat[SRCFS_LINE_MAX + 1];
    uint32_t pat_len = 0;
    char quote = 0;
    if (*pat == '\'' || *pat == '"') {
        quote = *pat++;
    }

    while (*pat && pat_len < sizeof(clean_pat) - 1) {
        if (quote && *pat == quote) {
            break;
        }
        clean_pat[pat_len++] = *pat++;
    }
    clean_pat[pat_len] = '\0';

    if (!quote) {
        while (pat_len > 0 && (clean_pat[pat_len - 1] == ' ' || clean_pat[pat_len - 1] == '\t' ||
                               clean_pat[pat_len - 1] == '\r' || clean_pat[pat_len - 1] == '\n')) {
            clean_pat[--pat_len] = '\0';
        }
    }
    pat = clean_pat;

    if (!*pat) {
        out_str(out, max, &pos, "Uso: src_grep archivo.c texto");
        return -1;
    }

    const uint8_t *data; uint32_t size;
    int fr = srcfs_file(e, &data, &size);                  /* PARCHE 140 */
    if (fr < 0) {
        out_str(out, max, &pos, fr == -1 ? "Error de I/O (o sin memoria) leyendo el disco." : "Error: datos de fuente corruptos en el disco.");
        return -1;
    }

    char line[SRCFS_LINE_MAX + 1];
    uint32_t ll = 0;
    uint32_t line_start = 0;
    int hits = 0;
    int truncated = 0;

    for (uint32_t p = 0; p < size; ++p) {

        char c = (char)data[p];

        if (c != '\n' && c != '\r' && ll < SRCFS_LINE_MAX) {
            line[ll++] = c;
        }

        if (c == '\n' || p + 1 == size) {

            line[ll] = '\0';

            if (contains(line, pat)) {

                if (max > 100 && pos + ll + 16 < max - 100) {
                    out_dec(out, max, &pos, line_start);
                    out_str(out, max, &pos, ": ");
                    out_str(out, max, &pos, line);
                    out_char(out, max, &pos, '\n');
                    hits++;
                } else {
                    truncated = 1;
                }
            }

            ll = 0;
            line_start = p + 1;
        }
    }

    if (hits == 0 && !truncated) {
        out_str(out, max, &pos, "Sin coincidencias.");
    }

    if (truncated) {
        out_str(out, max, &pos, "... (hay mas coincidencias; usa un texto mas especifico)");
    }

    return hits;
}


int src_tool(const char *cmd, char *out, uint32_t max)
{
    while (*cmd == ' ') cmd++;

    if (word_is(cmd, "src_ls")) {
        const char *a = cmd + 6;
        char prefix[SRCFS_NAME_MAX];
        uint32_t k = 0;

        while (*a == ' ') a++;
        while (*a && *a != ' ' && *a != '\n' && *a != '\r' && k < sizeof(prefix) - 1) prefix[k++] = *a++;
        prefix[k] = '\0';
        srcfs_ls_filter(k ? prefix : 0, out, max);
        console_dump(out);
        kputc('\n');
        return 1;
    }

    if (word_is(cmd, "src_cat") || word_is(cmd, "src_grep")) {

        int is_cat = word_is(cmd, "src_cat");
        const char *p = cmd + (is_cat ? 7 : 8);

        while (*p == ' ') p++;

        char name[SRCFS_NAME_MAX];
        uint32_t n = 0;

        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (*p && *p != q && n < sizeof(name) - 1) {
                name[n++] = *p++;
            }
            if (*p == q) p++;
        } else {
            while (*p && *p != ' ' && n < sizeof(name) - 1) {
                name[n++] = *p++;
            }
        }

        name[n] = '\0';

        while (*p == ' ') p++;

        if (n == 0) {
            uint32_t pos = 0;

            if (max) out[0] = '\0';
            out_str(out, max, &pos,
                    is_cat ? "Uso: src_cat archivo.c [offset]"
                           : "Uso: src_grep archivo.c texto");

        } else if (is_cat) {

            uint32_t off = 0;

            while (*p >= '0' && *p <= '9') {
                off = off * 10 + (uint32_t)(*p - '0');
                p++;
            }

            srcfs_cat(name, off, out, max);

        } else {
            srcfs_grep(name, p, out, max);
        }

        console_dump(out);
        kputc('\n');
        return 1;
    }

    return 0;
}
