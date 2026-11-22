/* SOMA_267_DISCOS: src/core/unidades.c en el anfitrion, sobre pendrives creados con tools/soma_disco.py (tests/test_unidades_267.py).
 *   test_unidades_267 plan TOTAL DATOS          imprime el plan (para compararlo con el de Python)
 *   test_unidades_267 agrandar IMG [CORTE]      agranda MODELOS y USUARIO; con CORTE, la escritura numero CORTE (y las siguientes)
 *                                               fallan, como un corte de luz. Aborta si se escribe en SISTEMA o DATOS.
 *   test_unidades_267 ops IMG                   monta las unidades y prueba rutas, lectura/escritura, permisos y archivos abiertos */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "unidades.h"

static FILE *img; static uint64_t nsec; static uint64_t prohib_ini, prohib_fin; static long corte = -1, nescr;
static int d_rd(void *c, uint64_t lba, uint32_t n, void *b) { (void)c; if (lba + n > nsec) return -1; fseek(img, (long)(lba * 512), SEEK_SET); return fread(b, 512, n, img) == n ? 0 : -1; }
static int d_wr(void *c, uint64_t lba, uint32_t n, const void *b)
{
    (void)c;
    if (lba + n > nsec) return -1;
    if (lba + n > prohib_ini && lba < prohib_fin) { printf("ESCRITURA PROHIBIDA en %llu (SISTEMA/DATOS)\n", (unsigned long long)lba); exit(3); }
    if (corte >= 0 && nescr++ >= corte) return -1;
    fseek(img, (long)(lba * 512), SEEK_SET); return fwrite(b, 512, n, img) == n ? 0 : -1;
}
static int d_sync(void *c) { (void)c; fflush(img); return 0; }
static void *al(uint32_t n) { return malloc(n); }
static uint32_t ahora(void) { return (46u << 25) | (11u << 21) | (22u << 16); }

struct part_io { uint64_t lba, n; };
static int p_rd(void *c, uint64_t lba, uint32_t n, void *b) { struct part_io *p = c; if (lba + n > p->n) return -1; return d_rd(0, p->lba + lba, n, b); }
static int p_wr(void *c, uint64_t lba, uint32_t n, const void *b) { struct part_io *p = c; if (lba + n > p->n) return -1; return d_wr(0, p->lba + lba, n, b); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static int nlk;
static void lock(void) { nlk++; }
static void unlock(void) { nlk--; }

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "plan")) {
        struct unid_part p[4];
        if (unid_plan(strtoull(argv[2], 0, 10), (uint32_t)strtoul(argv[3], 0, 10), p)) { printf("NO\n"); return 0; }
        for (int i = 0; i < 4; i++) printf("%u %u %u\n", p[i].tipo, p[i].lba, p[i].n);
        return 0;
    }
    if (argc < 3) return 2;
    img = fopen(argv[2], "r+b"); if (!img) return 2;
    fseek(img, 0, SEEK_END); nsec = (uint64_t)ftell(img) / 512;
    uint8_t m[512]; d_rd(0, 0, 1, m);
    prohib_ini = 1; prohib_fin = le32(m + 0x1BE + 16 * 2 + 8);                     /* de LBA 1 hasta MODELOS: SISTEMA y DATOS */
    if (!strcmp(argv[1], "agrandar")) {
        if (argc > 3) corte = atol(argv[3]);
        struct unid_disco d = { d_rd, d_wr, d_sync, 0, nsec, al, free, ahora };
        const char *why; uint32_t a = 0, b = 0;
        int r = unid_agrandar(&d, &why, &a, &b);
        printf("%d %s %u %u escrituras=%ld\n", r, why, a, b, nescr);
        return 0;
    }
    if (!strcmp(argv[1], "ops")) {
        unid_set_lock(lock, unlock);
        static struct part_io pio[2]; struct exfat_io io[2];
        for (int u = 0; u < 2; u++) {
            const uint8_t *e = m + 0x1BE + 16 * (u == UNID_USUARIO ? 3 : 2);
            pio[u].lba = le32(e + 8); pio[u].n = le32(e + 12);
            io[u] = (struct exfat_io){ p_rd, p_wr, &pio[u], al, free, ahora };
            CK(unid_montar(u, &io[u], pio[u].n) == 0 && unid_activa(u), "montar %s", unid_nombre(u));
        }
        const char *r;
        CK(unid_ruta("/usuario", &r) == UNID_USUARIO && !strcmp(r, "/") && unid_ruta("/usuario/Documentos/a.txt", &r) == UNID_USUARIO && !strcmp(r, "/Documentos/a.txt"), "rutas de USUARIO");
        CK(unid_ruta("/modelos/voz/x.bin", &r) == UNID_MODELOS && unid_ruta("/usuarios/x", &r) < 0 && unid_ruta("/workspace/x", &r) < 0 && unid_ruta("usuario/x", &r) < 0 && unid_ruta("/modelosX", &r) < 0, "rutas que no son unidades");
        const char *t = "Hola desde SOMA";
        CK(unid_escribir("/usuario/Documentos/hola.txt", t, strlen(t), 0) == 0, "escribir en USUARIO");
        char b[64] = { 0 }; uint64_t sz = 0; int dir = 1;
        CK(unid_leer("/usuario/documentos/HOLA.TXT", 0, b, sizeof b - 1) == (int)strlen(t) && !strcmp(b, t) && unid_tam("/usuario/Documentos/hola.txt", &sz, &dir) == 0 && sz == strlen(t) && !dir, "leer (sin distinguir mayusculas) y tamano");
        CK(unid_anadir("/usuario/Documentos/hola.txt", "!", 1, 0) == 0 && unid_tam("/usuario/Documentos/hola.txt", &sz, 0) == 0 && sz == strlen(t) + 1, "anadir");
        CK(unid_escribir("/modelos/voz/x.bin", "x", 1, 0) == EXFAT_E_RO && unid_mkdir("/modelos/nueva", 0) == EXFAT_E_RO && unid_borrar("/modelos/voz", 0) == EXFAT_E_RO, "MODELOS es de solo lectura desde el VFS");
        CK(unid_escribir("/modelos/voz/ggml-prueba.bin", "lmgg1234", 8, 1) == 0, "un programa del sistema si puede escribir en MODELOS");
        CK(unid_leer("/usuario/../modelos/voz/ggml-prueba.bin", 0, b, 8) == EXFAT_E_NAME && unid_escribir("/usuario/./x", "x", 1, 0) == EXFAT_E_NAME, "nada de . ni .. dentro de una unidad");
        CK(unid_renombrar("/usuario/Documentos/hola.txt", "/modelos/hola.txt", 1) == EXFAT_E_ARG, "no se mueve de una unidad a otra");
        CK(unid_mkdir("/usuario/Proyectos/juego", 0) == 0 && unid_renombrar("/usuario/Documentos/hola.txt", "/usuario/Proyectos/juego/hola.txt", 0) == 0, "carpetas y mover");
        static struct exfat_dirent lst[16]; int tot = 0;
        int k = unid_listar("/usuario", lst, 16, &tot);
        CK(k == 6 && tot == 6, "USUARIO lista sus 6 carpetas (%d)", k);
        struct unid_fd fd; static uint8_t grande[300000], vuelta[300000];
        for (uint32_t i = 0; i < sizeof grande; i++) grande[i] = (uint8_t)(i * 31 + 7);
        CK(unid_escribir("/modelos/voz/ggml-grande.bin", grande, sizeof grande, 1) == 0 && unid_abrir("/modelos/voz/ggml-grande.bin", &fd) == 0 && unid_fd_tam(&fd) == (int64_t)sizeof grande, "abrir un archivo de MODELOS");
        uint32_t got = 0; while (got < sizeof vuelta) { int x = unid_pread(&fd, got, vuelta + got, 16384); if (x <= 0) break; got += (uint32_t)x; }
        CK(got == sizeof grande && !memcmp(grande, vuelta, sizeof grande), "leer por trozos un archivo abierto (%u)", got);
        unid_desmontar(UNID_MODELOS);
        CK(unid_pread(&fd, 0, vuelta, 10) == EXFAT_E_NOENT && unid_leer("/modelos/voz/ggml-grande.bin", 0, vuelta, 10) < 0, "desmontada: ni el archivo abierto ni las rutas valen");
        CK(unid_montar(UNID_MODELOS, &io[UNID_MODELOS], pio[UNID_MODELOS].n) == 0 && unid_pread(&fd, 0, vuelta, 10) == EXFAT_E_NOENT, "un archivo abierto antes de volver a montar ya no vale");
        uint64_t li, to; char et[16];
        CK(unid_info(UNID_USUARIO, &li, &to, et, sizeof et) == 0 && !strcmp(et, "USUARIO") && li < to && to > (500ull << 20), "info de USUARIO");
        CK(nlk == 0, "el cerrojo queda equilibrado (%d)", nlk);
        printf("%s unidades: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
        return fallos != 0;
    }
    return 2;
}
