/* Banco de pruebas en el anfitrion: lee el bloque del modelo de un archivo y traduce una peticion por linea de stdin.
 * Salida por linea: estado \t intent \t p_mil \t confirmar \t orden|falta */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcm_ordenes.inc"   /* -Isrc/agent */

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: lcm_host modelo.bin < peticiones\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static unsigned char buf[8 << 20]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (!lcm_carga(buf, (uint32_t)n)) { fprintf(stderr, "modelo no valido\n"); return 3; }
    static char ln[4096]; struct lcm_res r;
    while (fgets(ln, sizeof ln, stdin)) {
        size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
        lcm_traduce(ln, &r);
        printf("%d\t%s\t%u\t%d\t%s\n", r.estado, r.intent_nombre, r.p_mil, r.confirmar, r.estado == LCM_FALTA ? r.falta : r.orden);
    }
    return 0;
}
