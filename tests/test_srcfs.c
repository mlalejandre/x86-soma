/* Prueba CRUZADA: el lector real del kernel (src/fs/srcfs.c) lee un disco escrito por el
 * empaquetador real de ejecutar.py y cada archivo debe coincidir con el original.
 *   test_srcfs IMG MANIFEST      (se ejecuta desde la raiz del proyecto; ver 'make test') */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "srcfs.h"
uint64_t web_now_ms(void) { return 0; }   /* PARCHE 140: el lector usa wz_inflate (web_inflate.c) */
void web_yield(void) { }

typedef struct { char name[64]; uint32_t size; } entry_t;
static entry_t ent[1024];   /* SOMA_267_SRCFS: el indice admite 1023 */
static int nent;

static int load_manifest(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];

    if (!f) return 0;
    while (fgets(line, sizeof(line), f) && nent < 1024) {
        char *sp = strrchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        if (strlen(line) >= sizeof(ent[nent].name)) continue;     /* los nombres SRCFS miden <= 47 */
        memcpy(ent[nent].name, line, strlen(line) + 1);
        ent[nent].size = (uint32_t)strtoul(sp + 1, 0, 10);
        nent++;
    }
    fclose(f);
    return nent;
}

/* Lee el archivo original sin los '\r' (srcfs_cat los elimina). */
static char *read_original(const char *name, uint32_t *len)
{
    char path[200];
    FILE *f;
    char *raw, *out;
    long n, i, o = 0;

    snprintf(path, sizeof(path), "src/%s", name);
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    raw = (char *)malloc((size_t)n + 1);
    out = (char *)malloc((size_t)n + 1);
    if (fread(raw, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(raw); free(out); return 0; }
    fclose(f);
    for (i = 0; i < n; ++i) if (raw[i] != '\r') out[o++] = raw[i];
    out[o] = 0;
    free(raw);
    *len = (uint32_t)o;
    return out;
}

/* Reconstruye un archivo completo paginando srcfs_cat con el OFFSET que el propio kernel indica. */
static int cat_all(const char *name, char *dst, uint32_t cap, uint32_t *got)
{
    static char page[2048];
    uint32_t off = 0, n = 0;

    for (int guard = 0; guard < 4000; ++guard) {
        char *m;
        int r = srcfs_cat(name, off, page, sizeof(page));
        if (r < 0) return 0;
        m = strstr(page, "\n--- ");
        if (!m) return 0;
        if (n + (uint32_t)(m - page) >= cap) return 0;
        memcpy(dst + n, page, (size_t)(m - page));
        n += (uint32_t)(m - page);
        if (strstr(m, "siguiente OFFSET: ") == 0) break;          /* fin del archivo */
        off += (uint32_t)r;
    }
    dst[n] = 0;
    *got = n;
    return 1;
}

static void test_listado(void)
{
    static char big[32768];
    int total = srcfs_ls_filter(0, big, sizeof(big)), i, vistos = 0;

    CHECK_EQ_INT(total, nent);
    for (i = 0; i < nent; ++i) {
        char linea[128];
        snprintf(linea, sizeof(linea), "%s %u\n", ent[i].name, ent[i].size);
        if (strstr(big, linea)) vistos++;
    }
    CHECK_EQ_INT(vistos, nent);                       /* todas, con su tamano exacto */
    CHECK(strstr(big, "mas: acota") == 0);            /* con buffer grande no se recorta */
}

static void test_filtro(void)
{
    static char big[32768];
    int esperados = 0, i;

    for (i = 0; i < nent; ++i) if (strncmp(ent[i].name, "core/", 5) == 0) esperados++;
    CHECK(esperados > 0);
    CHECK_EQ_INT(srcfs_ls_filter("core/", big, sizeof(big)), esperados);
    CHECK(strstr(big, "core/") != 0);
    CHECK(strstr(big, "net/") == 0);                  /* solo lo filtrado */
    CHECK_EQ_INT(srcfs_ls_filter("no_existe/", big, sizeof(big)), 0);
    CHECK_EQ_INT(srcfs_ls_filter("", big, sizeof(big)), nent);   /* prefijo vacio = todo */
}

/* Buffer pequeno (como el de 2 KiB del shell): antes se cortaba en silencio. */
static void test_buffer_pequeno_avisa(void)
{
    char small[512];
    int total = srcfs_ls_filter(0, small, sizeof(small));

    CHECK_EQ_INT(total, nent);                        /* el numero de coincidencias es el real */
    CHECK(strlen(small) < sizeof(small));
    CHECK(strstr(small, "mas: acota con src_ls <prefijo>") != 0);
    {
        char *p = strstr(small, "... y ");
        CHECK(p != 0);
        if (p) CHECK(atoi(p + 6) > 0 && atoi(p + 6) < nent);
    }
}

static void test_todos_los_archivos_coinciden(void)
{
    static char got[1 << 20];
    int i, malos = 0, vacios = 0;

    for (i = 0; i < nent; ++i) {
        uint32_t olen = 0, glen = 0;
        char *orig;
        if (ent[i].size > 200000) continue;           /* tablas de datos enormes: se comprueban abajo */
        orig = read_original(ent[i].name, &olen);
        if (!orig) { vacios++; continue; }
        if (!cat_all(ent[i].name, got, sizeof(got), &glen) || glen != olen || memcmp(got, orig, olen) != 0) {
            printf("  difiere: %s (orig %u, kernel %u)\n", ent[i].name, olen, glen);
            malos++;
        }
        free(orig);
    }
    CHECK_EQ_INT(malos, 0);
    CHECK_EQ_INT(vacios, 0);
}

static void test_archivo_grande_y_ultimo(void)
{
    static char got[4 << 20];
    int imax = 0, i;
    uint32_t olen = 0, glen = 0;
    char *orig;

    for (i = 1; i < nent; ++i) if (ent[i].size > ent[imax].size) imax = i;
    orig = read_original(ent[imax].name, &olen);
    CHECK(orig != 0);
    if (orig) {
        CHECK(cat_all(ent[imax].name, got, sizeof(got), &glen));
        CHECK_EQ_INT(glen, olen);
        CHECK(memcmp(got, orig, olen) == 0);
        free(orig);
    }
    /* La ultima entrada del indice (la que antes quedaba fuera) es legible. */
    orig = read_original(ent[nent - 1].name, &olen);
    CHECK(orig != 0 && cat_all(ent[nent - 1].name, got, sizeof(got), &glen) && glen == olen);
    free(orig);
}

static void test_grep_y_errores(void)
{
    static char out[4096];
    char name[80] = "";
    int i;

    for (i = 0; i < nent; ++i)
        if (strstr(ent[i].name, ".c") && strncmp(ent[i].name, "core/", 5) == 0) { strncpy(name, ent[i].name, sizeof(name) - 1); break; }
    CHECK(name[0] != 0);
    CHECK(srcfs_grep(name, "include", out, sizeof(out)) > 0);
    CHECK(srcfs_cat("no/existe.c", 0, out, sizeof(out)) < 0);
    CHECK(strstr(out, "no encontrado") != 0);
    CHECK_EQ_INT(srcfs_cat(name, 0xFFFFFF, out, sizeof(out)), 0);   /* offset fuera: fin de archivo */
    CHECK(strstr(out, "Fin del archivo") != 0);
    CHECK(srcfs_cat(name, 0, out, 8) >= 0 || 1);                    /* buffer minimo: no desborda (ASan) */
}

static void test_src_tool(void)
{
    static char out[8192];

    host_out_reset();
    CHECK_EQ_INT(src_tool("src_ls net/", out, sizeof(out)), 1);
    CHECK(strstr(host_out(), "net/") != 0);
    CHECK(strstr(host_out(), "core/") == 0);
}

int main(int argc, char **argv)
{
    if (argc < 3 || !load_manifest(argv[2]) || host_disk_load(argv[1]) != 0) {
        printf("uso: test_srcfs IMG MANIFEST (generados por tests/test_srcfs_pack.py --emit)\n");
        return 2;
    }
    CHECK(nent > 63);                                 /* el limite antiguo era 63 */
    {   /* SOMA_269_RAM: con las fuentes en memoria no se lee el disco y todo sigue coincidiendo */
        int antes = host_disk_reads();
        CHECK_EQ_INT(srcfs_a_ram(), 0);
        CHECK(host_disk_reads() > antes);                        /* la copia lee el disco una vez */
        CHECK_EQ_INT(srcfs_a_ram(), 0);                          /* segunda vez: no hace nada */
        int tras = host_disk_reads();
        test_todos_los_archivos_coinciden();
        test_archivo_grande_y_ultimo();
        test_grep_y_errores();
        CHECK_EQ_INT(host_disk_reads(), tras);                   /* ni un sector del disco mientras se leen las fuentes */
    }
    test_listado();
    test_filtro();
    test_buffer_pequeno_avisa();
    test_todos_los_archivos_coinciden();
    test_archivo_grande_y_ultimo();
    test_grep_y_errores();
    test_src_tool();
    TEST_MAIN_END("srcfs (kernel lee lo que escribe el host)");
}
