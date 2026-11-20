/* Pruebas en host del validador ELF real (src/core/elf.c): imagenes correctas, cada tipo de error y
 * fuzzing (cada byte mutado, cada truncado) con ASan: nunca lee fuera del buffer y, si acepta, los
 * invariantes se cumplen. Con un argumento, valida ademas ese ELF real (p. ej. build/x86_64/user/hello.elf). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "soma_test.h"
#include "elf.h"

static const struct elf_limits LIM = { 0x100000000ULL, 0x10000ULL, 16 };

static void w16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void w64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }

/* ELF de 2 segmentos: texto R+X en base y datos R+W en base+0x1000 */
#define IMG_LEN 0x200
static void build(uint8_t *b)
{
    memset(b, 0, IMG_LEN);
    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; b[4] = 2; b[5] = 1; b[6] = 1;
    w16(b + 16, 2); w16(b + 18, 62); w32(b + 20, 1);
    w64(b + 24, 0x100000000ULL + 0x10);          /* entrada */
    w64(b + 32, 64);                             /* phoff */
    w16(b + 52, 64); w16(b + 54, 56); w16(b + 56, 2);
    uint8_t *p0 = b + 64, *p1 = b + 64 + 56;
    w32(p0, 1); w32(p0 + 4, ELF_PF_R | ELF_PF_X); w64(p0 + 8, 0x100); w64(p0 + 16, 0x100000000ULL);
    w64(p0 + 32, 0x40); w64(p0 + 40, 0x40);
    w32(p1, 1); w32(p1 + 4, ELF_PF_R | ELF_PF_W); w64(p1 + 8, 0x140); w64(p1 + 16, 0x100001000ULL);
    w64(p1 + 32, 0x20); w64(p1 + 40, 0x2000);    /* 8 KiB en memoria, 32 bytes en archivo (bss) */
}

static int parse(const uint8_t *b, size_t n, struct elf_image *o) { return elf_parse(b, n, &LIM, o); }

static void test_valido(void)
{
    uint8_t b[IMG_LEN]; struct elf_image o;
    build(b);
    CHECK_EQ_INT(parse(b, IMG_LEN, &o), ELF_OK);
    CHECK_EQ_INT(o.nsegs, 2);
    CHECK_EQ_INT(o.entry, 0x100000010ULL);
    CHECK_EQ_INT(o.seg[1].memsz, 0x2000);
    CHECK_EQ_INT(o.seg[1].filesz, 0x20);
}

#define EXPECT(code, stmt) do { uint8_t b[IMG_LEN]; struct elf_image o; build(b); stmt; CHECK_EQ_INT(parse(b, IMG_LEN, &o), code); } while (0)

/* SOMA_PARCHE_019_FILES: programas grandes (la region real admite 4 MiB / 1024 paginas) */
static void test_programa_grande(void)
{
    static const struct elf_limits G = { 0x8000000000ULL, 0x400000ULL, 512 };
    uint8_t b[IMG_LEN]; struct elf_image o;
    build(b);
    w64(b + 24, 0x8000000000ULL + 0x10); w64(b + 64 + 16, 0x8000000000ULL);   /* texto en la base */
    w64(b + 64 + 56 + 16, 0x8000000000ULL + 0x1000);                          /* datos: base + 4 KiB */
    w64(b + 64 + 56 + 40, 0x1FF000);                                          /* 511 paginas + 1 de texto = 512: justo */
    CHECK_EQ_INT(elf_parse(b, IMG_LEN, &G, &o), ELF_OK);
    w64(b + 64 + 56 + 40, 0x1FF001);                                          /* una pagina de mas */
    CHECK_EQ_INT(elf_parse(b, IMG_LEN, &G, &o), ELF_E_TOOBIG);
    w64(b + 64 + 56 + 40, 0x3FF001);                                          /* se sale de la ventana de 4 MiB */
    CHECK_EQ_INT(elf_parse(b, IMG_LEN, &G, &o), ELF_E_SEGMENT);
    static const struct elf_limits R = { 0x8000000000ULL, 0x400000ULL, 1024 };
    w64(b + 64 + 56 + 40, 0x3FF000);                                          /* 1023 + 1 = 1024: justo, ocupa toda la ventana */
    CHECK_EQ_INT(elf_parse(b, IMG_LEN, &R, &o), ELF_OK);
}

static void test_segmento_vacio(void)
{
    uint8_t b[IMG_LEN]; struct elf_image o;
    build(b);
    w64(b + 64 + 56 + 32, 0); w64(b + 64 + 56 + 40, 0); w64(b + 64 + 56 + 16, 0);   /* 2o PT_LOAD vacio, vaddr 0 (como emite ld) */
    CHECK_EQ_INT(parse(b, IMG_LEN, &o), ELF_OK);
    CHECK_EQ_INT(o.nsegs, 1);                             /* se ignora */
    build(b);
    w64(b + 64 + 56 + 40, 0);                             /* memsz 0 pero filesz 0x20: invalido */
    CHECK_EQ_INT(parse(b, IMG_LEN, &o), ELF_E_SEGMENT);
}

static void test_errores(void)
{
    uint8_t b[IMG_LEN]; struct elf_image o;
    build(b);
    CHECK_EQ_INT(parse(b, 63, &o), ELF_E_SIZE);
    CHECK_EQ_INT(parse(0, 100, &o), ELF_E_SIZE);
    EXPECT(ELF_E_MAGIC, b[1] = 'X');
    EXPECT(ELF_E_FORMAT, b[4] = 1);                       /* ELF32 */
    EXPECT(ELF_E_FORMAT, b[5] = 2);                       /* big endian */
    EXPECT(ELF_E_FORMAT, w16(b + 16, 3));                 /* ET_DYN */
    EXPECT(ELF_E_FORMAT, w16(b + 18, 183));               /* AArch64 */
    EXPECT(ELF_E_PHDR, w16(b + 54, 32));                  /* phentsize incorrecto */
    EXPECT(ELF_E_PHDR, w16(b + 56, 0));
    EXPECT(ELF_E_PHDR, w64(b + 32, 0xFFFFFFFFFFFFFFF0ULL)); /* phoff enorme: sin desborde */
    EXPECT(ELF_E_PHDR, w64(b + 32, IMG_LEN - 10));
    EXPECT(ELF_E_UNSUPPORTED, w32(b + 64, 3));            /* PT_INTERP */
    EXPECT(ELF_E_UNSUPPORTED, w32(b + 64 + 56, 2));       /* PT_DYNAMIC */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 40, 0));           /* memsz 0 */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 32, 0x41));        /* filesz > memsz */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 8, IMG_LEN - 4));  /* datos fuera del archivo */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 8, 0xFFFFFFFFFFFFFFF0ULL));   /* offset enorme: sin desborde */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 32, 0xFFFFFFFFFFFFFFF0ULL));
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 16, 0x1000));      /* direccion del kernel */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 16, 0xFFFFFFFFFFFFF000ULL));  /* vaddr+memsz desborda */
    EXPECT(ELF_E_SEGMENT, w64(b + 64 + 56 + 40, 0x20000));          /* fuera de la ventana */
    EXPECT(ELF_E_WX, w32(b + 64 + 4, ELF_PF_R | ELF_PF_W | ELF_PF_X));
    EXPECT(ELF_E_OVERLAP, w64(b + 64 + 56 + 16, 0x100000000ULL + 0x800));  /* misma pagina que el texto */
    EXPECT(ELF_E_ENTRY, w64(b + 24, 0x100001000ULL));     /* entrada en el segmento de datos (no X) */
    EXPECT(ELF_E_ENTRY, w64(b + 24, 0x100000000ULL + 0x40));              /* justo pasado el final del texto */
    EXPECT(ELF_E_NOLOAD, (w32(b + 64, 4), w32(b + 64 + 56, 4)));          /* solo PT_NOTE */
    { uint8_t c[IMG_LEN]; build(c); struct elf_limits l = LIM; l.max_pages = 2; CHECK_EQ_INT(elf_parse(c, IMG_LEN, &l, &o), ELF_E_TOOBIG); }   /* 1 + 2 paginas > 2 */
    CHECK(elf_strerror(ELF_E_WX) != 0 && strlen(elf_strerror(-99)) > 0);
}

static void test_demasiados_segmentos(void)
{
    uint8_t b[64 + 56 * 6 + 0x40]; struct elf_image o;
    memset(b, 0, sizeof(b));
    b[0] = 0x7F; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; b[4] = 2; b[5] = 1; b[6] = 1;
    w16(b + 16, 2); w16(b + 18, 62); w32(b + 20, 1);
    w64(b + 24, 0x100000000ULL); w64(b + 32, 64); w16(b + 54, 56); w16(b + 56, 6);
    for (int i = 0; i < 6; ++i) {
        uint8_t *p = b + 64 + 56 * i;
        w32(p, 1); w32(p + 4, ELF_PF_R | ELF_PF_X); w64(p + 16, 0x100000000ULL + 0x1000ULL * i);
        w64(p + 32, 0); w64(p + 40, 16);
    }
    CHECK_EQ_INT(elf_parse(b, sizeof(b), &LIM, &o), ELF_E_TOOBIG);   /* mas de ELF_MAX_SEGS */
}

/* Invariantes que debe cumplir cualquier imagen aceptada */
static void check_invariantes(const uint8_t *b, size_t n, const struct elf_image *o)
{
    int exec = 0;
    CHECK(o->nsegs >= 1 && o->nsegs <= ELF_MAX_SEGS);
    for (int i = 0; i < o->nsegs; ++i) {
        const struct elf_seg *s = &o->seg[i];
        CHECK(s->filesz <= s->memsz);
        CHECK(s->offset <= n && s->filesz <= n - s->offset);
        CHECK(s->vaddr >= LIM.base && s->vaddr + s->memsz <= LIM.base + LIM.area);
        CHECK(!((s->flags & ELF_PF_W) && (s->flags & ELF_PF_X)));
        if ((s->flags & ELF_PF_X) && o->entry >= s->vaddr && o->entry < s->vaddr + s->memsz) exec = 1;
    }
    CHECK(exec);
    (void)b;
}

static void test_fuzz(void)
{
    uint8_t base[IMG_LEN]; struct elf_image o;
    build(base);
    int aceptadas = 0, rechazadas = 0;
    /* 1) cada byte de la cabecera y tablas, con 256 valores, sobre un buffer del tamano EXACTO (ASan vigila) */
    for (size_t pos = 0; pos < 64 + 2 * 56; ++pos) {
        for (int v = 0; v < 256; ++v) {
            uint8_t *b = malloc(IMG_LEN);
            memcpy(b, base, IMG_LEN); b[pos] = (uint8_t)v;
            int r = parse(b, IMG_LEN, &o);
            if (r == ELF_OK) { aceptadas++; check_invariantes(b, IMG_LEN, &o); } else rechazadas++;
            free(b);
        }
    }
    /* 2) cada longitud truncada */
    for (size_t n = 0; n <= IMG_LEN; ++n) {
        uint8_t *b = malloc(n ? n : 1);
        memcpy(b, base, n);
        int r = parse(b, n, &o);
        if (r == ELF_OK) { aceptadas++; check_invariantes(b, n, &o); } else rechazadas++;
        free(b);
    }
    /* 3) basura pseudoaleatoria con cabecera valida */
    unsigned s = 12345;
    for (int it = 0; it < 20000; ++it) {
        uint8_t *b = malloc(IMG_LEN);
        memcpy(b, base, IMG_LEN);
        for (int k = 0; k < 6; ++k) { s = s * 1103515245u + 12345u; b[(s >> 8) % (64 + 112)] = (uint8_t)(s >> 16); }
        int r = parse(b, IMG_LEN, &o);
        if (r == ELF_OK) { aceptadas++; check_invariantes(b, IMG_LEN, &o); } else rechazadas++;
        free(b);
    }
    CHECK(aceptadas > 0 && rechazadas > 0);
    printf("  fuzz: %d aceptadas (con invariantes), %d rechazadas\n", aceptadas, rechazadas);
}

static void test_real(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  (sin %s: se omite la prueba del ELF real)\n", path); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    CHECK(fread(b, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    struct elf_image o;
    static const struct elf_limits REAL = { 0x8000000000ULL, 0xC00000ULL, 3072 };   /* region de usuario real (usermode.h; 12 MiB desde SOMA_202, python.elf ocupa 6.4 MiB: SOMA_237) */
    int r = elf_parse(b, (size_t)n, &REAL, &o);
    if (r != ELF_OK && n >= 32) {
        /* un .elf compilado con otra direccion base (antes de un cambio de user.ld) no es un fallo del validador:
         * es un artefacto de build obsoleto. Se omite y se avisa; ejecutar.py lo recompila. */
        uint64_t entry = 0;
        for (int i = 0; i < 8; ++i) entry |= (uint64_t)b[24 + i] << (8 * i);
        if (entry < REAL.base) {
            printf("  %s: compilado con otra direccion base (obsoleto): se omite; recompila con 'python3 ejecutar.py'\n", path);
            free(b);
            return;
        }
    }
    if (r != ELF_OK) printf("  %s: %s\n", path, elf_strerror(r));
    CHECK_EQ_INT(r, ELF_OK);
    if (r == ELF_OK) {
        CHECK(o.entry >= REAL.base && o.entry < REAL.base + REAL.area);
        CHECK(o.nsegs >= 1);
    }
    free(b);
}

int main(int argc, char **argv)
{
    test_valido();
    test_segmento_vacio();
    test_programa_grande();
    test_errores();
    test_demasiados_segmentos();
    test_fuzz();
    for (int i = 1; i < argc; ++i) test_real(argv[i]);
    TEST_MAIN_END("elf");
}
