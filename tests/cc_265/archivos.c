#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
int main(void) {
    FILE *f = fopen("datos.txt", "w");
    if (!f) { perror("fopen"); return 1; }
    for (int i = 1; i <= 5; ++i) fprintf(f, "linea %d %.2f\n", i, i * 1.5);
    fclose(f);
    f = fopen("datos.txt", "a"); fputs("extra\n", f); fclose(f);
    f = fopen("/workspace/datos.txt", "r");
    char b[100]; int n = 0;
    while (fgets(b, sizeof b, f)) { n++; if (n == 2 || n == 6) printf("leida: %s", b); }
    printf("lineas=%d\n", n);
    fseek(f, 0, SEEK_END); printf("tam=%ld\n", ftell(f));
    rewind(f); int k; double d; char w[20];
    if (fscanf(f, "%19s %d %lf", w, &k, &d) == 3) printf("fscanf: %s %d %.2f\n", w, k, d);
    fclose(f);
    unsigned char bin[256]; for (int i = 0; i < 256; ++i) bin[i] = (unsigned char)i;
    f = fopen("bin.dat", "wb"); fwrite(bin, 1, 256, f); fclose(f);
    unsigned char r[256] = { 0 }; f = fopen("bin.dat", "rb"); size_t m = fread(r, 1, 300, f); fclose(f);
    printf("binario: %zu bytes, iguales=%d\n", m, !memcmp(bin, r, 256));
    errno = 0; f = fopen("/etc/prueba.txt", "w"); if (f) fclose(f);
    FILE *g = f ? 0 : fopen("/etc/prueba.txt", "w");
    printf("escribir fuera de /workspace: %s\n", f ? "PERMITIDO (mal)" : "denegado");
    (void)g;
    errno = 0; f = fopen("no_existe.txt", "r"); printf("abrir inexistente: %s (%s)\n", f ? "abierto (mal)" : "falla", strerror(errno));
    char rp[128]; realpath("x/y.c", rp); printf("realpath: %s\n", rp);
    return 0;
}
