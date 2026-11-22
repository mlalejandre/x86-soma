/* SOMA_267_DISCOS: pruebas de src/core/exfat.c en el anfitrion.
 *   test_exfat_267                 en memoria: formatea, cientos de operaciones al azar comparadas con un modelo, volver a montar,
 *                                  llenar el disco, errores, y volumenes estropeados a proposito (nunca se sale del volumen)
 *   test_exfat_267 ops IMG SEMILLA hace operaciones al azar en una imagen (de mkfs.exfat) y escribe como deberia quedar
 *   test_exfat_267 dump IMG        lista la imagen: "D ruta" / "F ruta tamano huella" (para compararla con otra implementacion)
 *   test_exfat_267 format IMG MIB  formatea una imagen nueva con exfat_format
 * tests/test_exfat_267.py junta todo con mkfs.exfat, fsck.exfat y libexfat (otra implementacion) cuando estan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#include "exfat.h"

static uint8_t *disco; static uint64_t disco_sec; static FILE *fimg;
static uint64_t vol_ini, vol_n; static int fuera;                     /* escrituras fuera del volumen */
static int fallar_cada, callado;                                             /* fallos de E/S inyectados */
static uint32_t cuenta_io;
static int rd_mem(void *c, uint64_t lba, uint32_t n, void *b)
{
    (void)c;
    if (fallar_cada && ++cuenta_io % (uint32_t)fallar_cada == 0) return -1;
    if (lba + n > disco_sec) return -1;
    if (fimg) { fseek(fimg, (long)(lba * 512), SEEK_SET); return fread(b, 512, n, fimg) == n ? 0 : -1; }
    memcpy(b, disco + lba * 512, (size_t)n * 512); return 0;
}
static int wr_mem(void *c, uint64_t lba, uint32_t n, const void *b)
{
    (void)c;
    if (lba < vol_ini || lba + n > vol_ini + vol_n) { fuera++; return -1; }
    if (fallar_cada && ++cuenta_io % (uint32_t)fallar_cada == 0) return -1;
    if (fimg) { fseek(fimg, (long)(lba * 512), SEEK_SET); return fwrite(b, 512, n, fimg) == n ? 0 : -1; }
    memcpy(disco + lba * 512, b, (size_t)n * 512); return 0;
}
static void *al(uint32_t n) { return malloc(n); }
static uint32_t ahora(void) { return (46u << 25) | (11u << 21) | (22u << 16) | (12u << 11); }   /* 2026-11-22 12:00 */
static struct exfat_io IO = { rd_mem, wr_mem, 0, al, free, ahora };

static unsigned long long fnv(const unsigned char *p, size_t n) { unsigned long long h = 1469598103934665603ULL; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; } return h; }
static uint64_t semilla = 88172645463325252ULL;
static uint64_t azar(void) { semilla ^= semilla << 13; semilla ^= semilla >> 7; semilla ^= semilla << 17; return semilla; }

/* ---------------------------------------------------------------- el modelo: lo que deberia haber */
#define MAXF 400
struct ent { char ruta[600]; int dir; unsigned char *datos; size_t n; int usado; };
static struct ent M[MAXF];
static int busca(const char *r) { for (int i = 0; i < MAXF; i++) if (M[i].usado && !strcasecmp(M[i].ruta, r)) return i; return -1; }
static int libre(void) { for (int i = 0; i < MAXF; i++) if (!M[i].usado) return i; return -1; }
static const char *const NOMBRES[] = { "notas.txt", "Fotos", "árbol genealógico.doc", "ñandú", "Música", "README", "a", "Informe Final (versión 2).pdf",
                                       "😀 sonrisa.png", "日本語のファイル.txt", "data.bin", "Документы", "x.c", "Vídeos", "MAYÚSCULAS.TXT", "z" };
static void nombre_azar(char *o, size_t cap)
{
    if (azar() % 7 == 0) {                                          /* largo: hasta 255 caracteres */
        size_t n = 30 + azar() % 200; for (size_t i = 0; i < n && i + 1 < cap; i++) o[i] = (char)('a' + azar() % 26); o[n < cap ? n : cap - 1] = 0;
        snprintf(o + strlen(o), cap - strlen(o), "%u", (unsigned)(azar() % 1000));
    } else snprintf(o, cap, "%s%u", NOMBRES[azar() % (sizeof NOMBRES / sizeof *NOMBRES)], (unsigned)(azar() % 50));
}
static const char *carpeta_azar(void)
{
    int c[MAXF], k = 0; for (int i = 0; i < MAXF; i++) if (M[i].usado && M[i].dir) c[k++] = i;
    if (!k || azar() % 3 == 0) return "";
    return M[c[azar() % k]].ruta;
}
static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

static int verifica(struct exfat_vol *v, const char *donde)
{
    int mal = 0;
    for (int i = 0; i < MAXF; i++) {
        if (!M[i].usado) continue;
        struct exfat_dirent d; int r = exfat_stat(v, M[i].ruta, &d);
        if (r) { printf("  %s: %s no esta (%s)\n", donde, M[i].ruta, exfat_err_str(r)); mal++; continue; }
        if (d.is_dir != M[i].dir) { printf("  %s: %s tipo\n", donde, M[i].ruta); mal++; continue; }
        if (M[i].dir) continue;
        if (d.size != M[i].n) { printf("  %s: %s tamano %llu != %zu\n", donde, M[i].ruta, (unsigned long long)d.size, M[i].n); mal++; continue; }
        unsigned char *b = malloc(M[i].n + 1); size_t got = 0;
        while (got < M[i].n) {                                      /* por trozos de tamanos raros */
            uint32_t k = (uint32_t)(1 + azar() % 70000); if (k > M[i].n - got) k = (uint32_t)(M[i].n - got);
            int x = exfat_read(v, M[i].ruta, got, b + got, k); if (x <= 0) break; got += (size_t)x;
        }
        if (got != M[i].n || memcmp(b, M[i].datos, M[i].n)) { printf("  %s: %s contenido distinto\n", donde, M[i].ruta); mal++; }
        free(b);
    }
    return mal;
}
static int cuenta_listado(struct exfat_vol *v, const char *dir)
{
    int total = 0; static struct exfat_dirent lst[64];
    int r = exfat_readdir(v, dir, lst, 64, &total);
    return r < 0 ? r : total;
}
static int hijos_modelo(const char *dir)
{
    size_t L = strlen(dir); int k = 0;
    for (int i = 0; i < MAXF; i++) {
        if (!M[i].usado) continue;
        const char *r = M[i].ruta;
        if (strncasecmp(r, dir, L) || r[L] != '/') continue;
        if (!strchr(r + L + 1, '/')) k++;
    }
    return k;
}
#define printf(...) (callado ? 0 : printf(__VA_ARGS__))
static void op_azar(struct exfat_vol *v, int *errs)
{
    int t = (int)(azar() % 100);
    char nom[400], ruta[600];
    if (t < 30) {                                                   /* archivo nuevo o reescrito */
        nombre_azar(nom, sizeof nom); snprintf(ruta, sizeof ruta, "%s/%s", carpeta_azar(), nom);
        int i = busca(ruta);
        if (i >= 0 && M[i].dir) return;
        size_t sz = azar() % 4 == 0 ? azar() % 600000 : azar() % 5000;
        unsigned char *b = malloc(sz + 1); for (size_t k = 0; k < sz; k++) b[k] = (unsigned char)azar();
        int r = exfat_write(v, ruta, b, sz);
        if (r == EXFAT_E_NOSPC) { free(b); return; }
        if (r) { printf("  escribir %s: %s\n", ruta, exfat_err_str(r)); (*errs)++; free(b); return; }
        if (i < 0) { i = libre(); if (i < 0) { exfat_remove(v, ruta); free(b); return; } snprintf(M[i].ruta, sizeof M[i].ruta, "%s", ruta); M[i].usado = 1; M[i].dir = 0; }
        else free(M[i].datos);
        M[i].datos = b; M[i].n = sz;
    } else if (t < 45) {                                            /* anadir al final */
        int c[MAXF], k = 0; for (int i = 0; i < MAXF; i++) if (M[i].usado && !M[i].dir) c[k++] = i;
        if (!k) return;
        int i = c[azar() % k]; size_t add = azar() % 3 == 0 ? azar() % 200000 : azar() % 3000;
        unsigned char *b = malloc(add + 1); for (size_t q = 0; q < add; q++) b[q] = (unsigned char)azar();
        int r = exfat_append(v, M[i].ruta, b, (uint32_t)add);
        if (r == EXFAT_E_NOSPC) { free(b); return; }
        if (r) { printf("  anadir %s: %s\n", M[i].ruta, exfat_err_str(r)); (*errs)++; free(b); return; }
        M[i].datos = realloc(M[i].datos, M[i].n + add + 1); memcpy(M[i].datos + M[i].n, b, add); M[i].n += add; free(b);
    } else if (t < 57) {                                            /* carpeta */
        nombre_azar(nom, sizeof nom); snprintf(ruta, sizeof ruta, "%s/%s", carpeta_azar(), nom);
        if (strlen(ruta) > 500) return;
        int i = busca(ruta);
        int r = exfat_mkdir(v, ruta);
        if (i >= 0) { if (r != EXFAT_E_EXIST) { printf("  mkdir existente %s: %d\n", ruta, r); (*errs)++; } return; }
        if (r == EXFAT_E_NOSPC) return;
        if (r) { printf("  mkdir %s: %s\n", ruta, exfat_err_str(r)); (*errs)++; return; }
        i = libre(); if (i < 0) { exfat_remove(v, ruta); return; }      /* el modelo esta lleno */
        snprintf(M[i].ruta, sizeof M[i].ruta, "%s", ruta); M[i].usado = 1; M[i].dir = 1; M[i].datos = 0; M[i].n = 0;
    } else if (t < 75) {                                            /* borrar */
        int c[MAXF], k = 0; for (int i = 0; i < MAXF; i++) if (M[i].usado) c[k++] = i;
        if (!k) return;
        int i = c[azar() % k];
        int r = exfat_remove(v, M[i].ruta);
        if (M[i].dir && hijos_modelo(M[i].ruta)) { if (r != EXFAT_E_NOTEMPTY) { printf("  rmdir no vacia %s: %d\n", M[i].ruta, r); (*errs)++; } return; }
        if (r) { printf("  borrar %s: %s\n", M[i].ruta, exfat_err_str(r)); (*errs)++; return; }
        free(M[i].datos); M[i].datos = 0; M[i].usado = 0;
    } else if (t < 90) {                                            /* renombrar o mover */
        int c[MAXF], k = 0; for (int i = 0; i < MAXF; i++) if (M[i].usado) c[k++] = i;
        if (!k) return;
        int i = c[azar() % k];
        nombre_azar(nom, sizeof nom); const char *dst = carpeta_azar();
        snprintf(ruta, sizeof ruta, "%s/%s", dst, nom);
        size_t L = strlen(M[i].ruta);
        int dentro = M[i].dir && !strncasecmp(ruta, M[i].ruta, L) && ruta[L] == '/';
        size_t L0 = strlen(M[i].ruta);
        for (int j = 0; j < MAXF; j++)                                /* que las rutas de dentro sigan cabiendo en el modelo */
            if (M[j].usado && !strncasecmp(M[j].ruta, M[i].ruta, L0) && M[j].ruta[L0] == '/' && strlen(ruta) + strlen(M[j].ruta) - L0 >= 590) return;
        int r = exfat_rename(v, M[i].ruta, ruta);
        if (dentro) { if (r != EXFAT_E_ARG) { printf("  mover dentro de si %s -> %s: %d\n", M[i].ruta, ruta, r); (*errs)++; } return; }
        if (busca(ruta) >= 0 && strcasecmp(ruta, M[i].ruta)) { if (r != EXFAT_E_EXIST) { printf("  renombrar a existente: %d\n", r); (*errs)++; } return; }
        if (r == EXFAT_E_NOSPC) return;
        if (r) { printf("  renombrar %s -> %s: %s\n", M[i].ruta, ruta, exfat_err_str(r)); (*errs)++; return; }
        char viejo[600]; snprintf(viejo, sizeof viejo, "%s", M[i].ruta);
        for (int j = 0; j < MAXF; j++) {                            /* lo de dentro cambia de ruta */
            if (!M[j].usado) continue;
            if (j == i) { snprintf(M[j].ruta, sizeof M[j].ruta, "%s", ruta); continue; }
            if (!strncasecmp(M[j].ruta, viejo, L) && M[j].ruta[L] == '/') { size_t a1 = strlen(ruta), a2 = strlen(M[j].ruta + L); if (a1 + a2 < sizeof M[j].ruta) { char nr[sizeof M[j].ruta]; memcpy(nr, ruta, a1); memcpy(nr + a1, M[j].ruta + L, a2 + 1); memcpy(M[j].ruta, nr, a1 + a2 + 1); } }
        }
    } else {                                                        /* listar y comparar */
        const char *d = carpeta_azar();
        int got = cuenta_listado(v, d[0] ? d : "/");
        int esp = hijos_modelo(d);
        if (got != esp) { printf("  listado de '%s': %d, el modelo %d\n", d, got, esp); (*errs)++; }
    }
}
#undef printf
static void limpia_modelo(void) { for (int i = 0; i < MAXF; i++) { if (M[i].usado) free(M[i].datos); M[i].usado = 0; M[i].datos = 0; } }

static int imagen_abrir(const char *p, uint64_t *sec)
{
    fimg = fopen(p, "r+b"); if (!fimg) return -1;
    fseek(fimg, 0, SEEK_END); *sec = (uint64_t)ftell(fimg) / 512; disco_sec = *sec; vol_ini = 0; vol_n = *sec;
    return 0;
}
static void lista(struct exfat_vol *v, const char *dir)
{
    static struct exfat_dirent lst[4096]; int total = 0;
    int k = exfat_readdir(v, dir[0] ? dir : "/", lst, 4096, &total);
    if (k < 0) { printf("ERROR %s: %s\n", dir, exfat_err_str(k)); return; }
    struct exfat_dirent *copia = malloc(sizeof(struct exfat_dirent) * (size_t)(k ? k : 1)); memcpy(copia, lst, sizeof(struct exfat_dirent) * (size_t)k);
    for (int i = 0; i < k; i++) {
        char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, copia[i].name);
        if (copia[i].is_dir) { printf("D %s\n", p); lista(v, p); }
        else {
            unsigned char *b = malloc(copia[i].size + 1); uint64_t got = 0;
            while (got < copia[i].size) { int x = exfat_read(v, p, got, b + got, 1u << 20); if (x <= 0) break; got += (uint64_t)x; }
            printf("F %s %llu %016llx%s\n", p, (unsigned long long)copia[i].size, fnv(b, (size_t)got), got == copia[i].size ? "" : " ERROR-LECTURA");
            free(b);
        }
    }
    free(copia);
}

int main(int argc, char **argv)
{
    static struct exfat_vol v;
    if (argc >= 3 && !strcmp(argv[1], "dump")) {
        uint64_t s; if (imagen_abrir(argv[2], &s)) return 2;
        IO.write = 0;
        int r = exfat_mount(&v, &IO, 0, s); if (r) { printf("ERROR montar: %s\n", exfat_err_str(r)); return 1; }
        lista(&v, ""); exfat_umount(&v); return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "format")) {
        FILE *f = fopen(argv[2], "wb"); uint64_t mib = strtoull(argv[3], 0, 10); fclose(f);
        if (truncate(argv[2], (long)(mib << 20))) return 2;
        uint64_t s; if (imagen_abrir(argv[2], &s)) return 2;
        int r = exfat_format(&IO, 0, s, argc > 4 ? argv[4] : "PRUEBA", 0x12345678); printf("format: %s\n", r ? exfat_err_str(r) : "ok"); return r != 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "ops")) {
        uint64_t s; if (imagen_abrir(argv[2], &s)) return 2;
        semilla ^= strtoull(argv[3], 0, 10) * 0x9E3779B97F4A7C15ULL;
        int r = exfat_mount(&v, &IO, 0, s); if (r) { printf("ERROR montar: %s\n", exfat_err_str(r)); return 1; }
        int errs = 0, nops = argc > 4 ? atoi(argv[4]) : 400;
        for (int k = 0; k < nops; k++) { op_azar(&v, &errs); if (k % 97 == 96) { exfat_umount(&v); r = exfat_mount(&v, &IO, 0, s); if (r) { printf("ERROR remontar\n"); return 1; } } }
        errs += verifica(&v, "final");
        exfat_umount(&v); fclose(fimg);
        for (int i = 0; i < MAXF; i++) if (M[i].usado) {               /* lo esperado, para compararlo con libexfat */
            if (M[i].dir) printf("D %s\n", M[i].ruta); else printf("F %s %zu %016llx\n", M[i].ruta, M[i].n, fnv(M[i].datos, M[i].n));
        }
        fprintf(stderr, "ops: %d errores\n", errs);
        return errs != 0;
    }

    /* ---------------------------------------------------------- 1. en memoria: volumenes de varios tamanos */
    static const uint64_t TAM[] = { 8u << 20, 64u << 20, 300u << 20 };
    for (unsigned t = 0; t < 3; t++) {
        disco_sec = TAM[t] / 512 + 4096; disco = calloc(disco_sec, 512); vol_ini = 2048; vol_n = TAM[t] / 512; fuera = 0;
        int r = exfat_format(&IO, vol_ini, vol_n, "USUARIO", 0xCAFE0000u + t);
        CK(r == 0, "formatear %llu MiB: %s", (unsigned long long)(TAM[t] >> 20), exfat_err_str(r));
        r = exfat_mount(&v, &IO, vol_ini, vol_n);
        CK(r == 0 && v.writable && !strcmp(v.label, "USUARIO"), "montar lo formateado (%s, etiqueta %s)", exfat_err_str(r), v.label);
        CK(exfat_free_bytes(&v) > exfat_total_bytes(&v) * 9 / 10, "casi todo libre");
        limpia_modelo(); int errs = 0;
        int nops = t == 0 ? 600 : 1500;
        for (int k = 0; k < nops; k++) {
            op_azar(&v, &errs);
            if (k % 211 == 210) { exfat_umount(&v); r = exfat_mount(&v, &IO, vol_ini, vol_n); if (r) { errs++; break; } errs += verifica(&v, "remontado"); }
        }
        errs += verifica(&v, "final");
        CK(errs == 0, "%llu MiB: %d operaciones al azar sin diferencias con el modelo (%d errores)", (unsigned long long)(TAM[t] >> 20), nops, errs);
        CK(fuera == 0, "nunca se escribe fuera del volumen (%d)", fuera);
        /* cuentas del mapa: lo libre de memoria = lo que dice el disco al volver a montar */
        uint64_t libre_antes = exfat_free_bytes(&v); exfat_umount(&v); exfat_mount(&v, &IO, vol_ini, vol_n);
        CK(exfat_free_bytes(&v) == libre_antes, "el espacio libre cuadra al volver a montar");
        /* mayusculas y tildes: se encuentran sin distinguirlas */
        for (int i = 0; i < MAXF && exfat_free_bytes(&v) < (1u << 20); i++)   /* hacer sitio si las operaciones llenaron el disco */
            if (M[i].usado && !M[i].dir) { exfat_remove(&v, M[i].ruta); free(M[i].datos); M[i].datos = 0; M[i].usado = 0; }
        int rm_ = exfat_mkdir(&v, "/Árbol"), rw_ = exfat_write(&v, "/Árbol/Ñu.TXT", "x", 1);
        if (rm_ || rw_) printf("  mkdir %s, escribir %s, libre %llu\n", exfat_err_str(rm_), exfat_err_str(rw_), (unsigned long long)exfat_free_bytes(&v));
        struct exfat_dirent d;
        CK(exfat_stat(&v, "/árbol/ñu.txt", &d) == 0 && d.size == 1, "tildes y mayusculas (la tabla del volumen)");
        CK(exfat_write(&v, "/ÁRBOL/ñU.txt", "yy", 2) == 0 && exfat_stat(&v, "/Árbol/Ñu.TXT", &d) == 0 && d.size == 2 && cuenta_listado(&v, "/Árbol") == 1, "reescribir con otras mayusculas: el mismo archivo");
        exfat_umount(&v); free(disco); disco = 0;
    }

    /* ---------------------------------------------------------- 2. errores y limites */
    disco_sec = (16u << 20) / 512; disco = calloc(disco_sec, 512); vol_ini = 0; vol_n = disco_sec; fuera = 0;
    exfat_format(&IO, 0, vol_n, "LIMITES", 1); exfat_mount(&v, &IO, 0, vol_n); limpia_modelo();
    CK(exfat_write(&v, "/a/b.txt", "x", 1) == EXFAT_E_NOENT, "carpeta que no existe");
    CK(exfat_write(&v, "/mal:nombre", "x", 1) == EXFAT_E_NAME && exfat_write(&v, "/mal?", "x", 1) == EXFAT_E_NAME && exfat_mkdir(&v, "/..") == EXFAT_E_NAME, "nombres no validos");
    { char largo[400]; memset(largo, 'a', 257); largo[0] = '/'; largo[257] = 0; CK(exfat_write(&v, largo, "x", 1) == EXFAT_E_NAME, "nombre de mas de 255"); largo[256] = 0; CK(exfat_write(&v, largo, "x", 1) == 0, "nombre de 255"); }
    exfat_mkdir(&v, "/c"); exfat_write(&v, "/c/f", "1", 1);
    CK(exfat_remove(&v, "/c") == EXFAT_E_NOTEMPTY, "no se borra una carpeta con cosas");
    CK(exfat_write(&v, "/c", "x", 1) == EXFAT_E_ISDIR && exfat_mkdir(&v, "/c") == EXFAT_E_EXIST, "carpeta: ni escribir ni crearla otra vez");
    CK(exfat_rename(&v, "/c", "/c/dentro") == EXFAT_E_ARG, "una carpeta no se mueve dentro de si misma");
    CK(exfat_remove(&v, "/") == EXFAT_E_ARG && exfat_rename(&v, "/", "/x") == EXFAT_E_ARG, "la raiz no se toca");
    {   /* llenar: NOSPC limpio, y al borrar vuelve el sitio */
        uint64_t libre0 = exfat_free_bytes(&v); unsigned char *b = calloc(1, 1 << 20); int k = 0, r = 0; char p[64];
        for (k = 0; k < 64; k++) { snprintf(p, sizeof p, "/lleno%02d", k); r = exfat_write(&v, p, b, 1 << 20); if (r) break; }
        CK(r == EXFAT_E_NOSPC && k > 5, "disco lleno: NOSPC tras %d MiB", k);
        CK(exfat_append(&v, "/lleno00", b, 1 << 20) == EXFAT_E_NOSPC, "anadir sin sitio: NOSPC");
        struct exfat_dirent d; CK(exfat_stat(&v, "/lleno00", &d) == 0 && d.size == 1u << 20, "el archivo sigue igual tras el NOSPC");
        for (int j = 0; j < k; j++) { snprintf(p, sizeof p, "/lleno%02d", j); exfat_remove(&v, p); }
        CK(exfat_free_bytes(&v) == libre0, "borrando vuelve todo el sitio (%llu/%llu)", (unsigned long long)exfat_free_bytes(&v), (unsigned long long)libre0);
        exfat_umount(&v); exfat_mount(&v, &IO, 0, vol_n);
        CK(exfat_free_bytes(&v) == libre0, "y en el disco tambien");
        free(b);
    }
    {   /* muchos archivos en una carpeta: la carpeta crece en varios cluster */
        exfat_mkdir(&v, "/muchos"); int ok = 1; char p[64];
        for (int k = 0; k < 600 && ok; k++) { snprintf(p, sizeof p, "/muchos/archivo numero %04d con nombre largo.txt", k); ok = exfat_write(&v, p, p, (uint32_t)strlen(p)) == 0; }
        CK(ok && cuenta_listado(&v, "/muchos") == 600, "600 archivos en una carpeta (%d)", cuenta_listado(&v, "/muchos"));
        exfat_umount(&v); exfat_mount(&v, &IO, 0, vol_n);
        char b[80]; int r = exfat_read(&v, "/muchos/ARCHIVO NUMERO 0599 CON NOMBRE LARGO.TXT", 0, b, sizeof b);
        CK(r > 0 && !memcmp(b, "/muchos/archivo numero 0599", 27), "y se encuentran tras volver a montar");
        for (int k = 0; k < 600; k += 2) { snprintf(p, sizeof p, "/muchos/archivo numero %04d con nombre largo.txt", k); exfat_remove(&v, p); }
        CK(cuenta_listado(&v, "/muchos") == 300, "borrando la mitad quedan 300");
        CK(exfat_write(&v, "/muchos/nuevo en un hueco", "h", 1) == 0 && cuenta_listado(&v, "/muchos") == 301, "los huecos se reutilizan");
    }
    CK(fuera == 0, "nunca fuera del volumen");
    exfat_umount(&v);

    /* ---------------------------------------------------------- 3. solo lectura, region de reserva y fallos de E/S */
    IO.write = 0; int r = exfat_mount(&v, &IO, 0, vol_n);
    CK(r == 0 && !v.writable && exfat_write(&v, "/x", "1", 1) == EXFAT_E_RO && exfat_mkdir(&v, "/y") == EXFAT_E_RO, "sin escritura: solo lectura");
    exfat_umount(&v); IO.write = wr_mem;
    memset(disco + 100, 0x5A, 8);                                   /* region principal estropeada (suma de comprobacion) */
    r = exfat_mount(&v, &IO, 0, vol_n); struct exfat_dirent d;
    CK(r == 0 && exfat_stat(&v, "/c/f", &d) == 0, "con la region principal danada se monta con la de reserva");
    exfat_umount(&v);
    memset(disco + 12 * 512 + 100, 0x5A, 8);
    CK(exfat_mount(&v, &IO, 0, vol_n) == EXFAT_E_CORRUPT, "con las dos danadas: volumen danado");
    exfat_umount(&v);
    memset(disco, 0, 512 * 24);
    CK(exfat_mount(&v, &IO, 0, vol_n) == EXFAT_E_NOFS, "sin exFAT: NOFS");
    exfat_format(&IO, 0, vol_n, "IO", 2); exfat_mount(&v, &IO, 0, vol_n); limpia_modelo();
    {
        int errs = 0; for (int k = 0; k < 200; k++) op_azar(&v, &errs);
        fallar_cada = 37; int raros = 0; callado = 1;
        for (int k = 0; k < 300; k++) { struct exfat_dirent d2; char p[64]; snprintf(p, sizeof p, "/io%d", k % 20); int x = exfat_write(&v, p, p, 10); if (x && x != EXFAT_E_IO && x != EXFAT_E_NOSPC && x != EXFAT_E_CORRUPT) raros++; (void)exfat_stat(&v, p, &d2); }
        fallar_cada = 0; callado = 0;
        CK(raros == 0 && fuera == 0, "con fallos de E/S: errores limpios, nunca fuera del volumen");
    }
    exfat_umount(&v);

    /* ---------------------------------------------------------- 4. volumenes estropeados al azar: nada se cuelga ni se sale */
    {
        uint8_t *base = malloc(disco_sec * 512);
        exfat_format(&IO, 0, vol_n, "FUZZ", 3); exfat_mount(&v, &IO, 0, vol_n); limpia_modelo();
        int errs = 0; for (int k = 0; k < 300; k++) op_azar(&v, &errs);
        exfat_umount(&v); memcpy(base, disco, disco_sec * 512);
        int montados = 0;
        for (int it = 0; it < 400; it++) {
            memcpy(disco, base, disco_sec * 512);
            int nb = 1 + (int)(azar() % 40);
            for (int k = 0; k < nb; k++) {                            /* metadatos: arranque, FAT, mapa, directorios */
                uint64_t off = azar() % 4 == 0 ? azar() % (24 * 512) : azar() % 3 == 0 ? (2048 * 512 + azar() % (64 * 512)) : azar() % (disco_sec * 512 / 8);
                disco[off] ^= (uint8_t)(1 + azar() % 255);
            }
            if (exfat_mount(&v, &IO, 0, vol_n) != 0) continue;
            montados++;
            int e2 = 0; callado = 1;
            for (int k = 0; k < 40; k++) op_azar(&v, &e2);
            callado = 0;
            static struct exfat_dirent lst[64]; int tot; exfat_readdir(&v, "/", lst, 64, &tot);
            exfat_umount(&v);
        }
        CK(fuera == 0, "400 volumenes estropeados (%d se montaron): ninguna escritura fuera del volumen", montados);
        free(base);
    }
    free(disco); limpia_modelo();
    printf("%s exFAT: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
