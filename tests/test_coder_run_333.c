/* SOMA_333_SALIDA: la salida de los programas del programador, partida en lineas (sin nucleo). */
#include <stdio.h>
#include <string.h>
#include "coder_run.h"

static int g_n, g_mal;
#define CK(c) do { g_n++; if (!(c)) { g_mal++; printf("FALLO linea %d: %s\n", __LINE__, #c); } } while (0)
static struct crun_buf B;
static void mete(const char *s) { crun_buf_mete(&B, s, (uint32_t)strlen(s)); }

int main(void)
{
    crun_buf_vacia(&B);
    mete("hola\nmundo\n");
    CK(B.n == 2 && !strcmp(crun_buf_linea(&B, 0), "hola") && !strcmp(crun_buf_linea(&B, 1), "mundo"));
    mete("sin salto");                                  /* queda pendiente hasta el salto o el cierre */
    CK(B.n == 2);
    mete(" todavia\r\n");
    CK(B.n == 3 && !strcmp(crun_buf_linea(&B, 2), "sin salto todavia"));
    mete("\033[1;32mverde\033[0m y normal\n");          /* colores ANSI fuera, tambien partidos entre dos llamadas */
    CK(!strcmp(crun_buf_linea(&B, 3), "verde y normal"));
    mete("a\033["); mete("31"); mete("mb\n");
    CK(!strcmp(crun_buf_linea(&B, 4), "ab"));
    mete("t\tx\n");
    CK(!strcmp(crun_buf_linea(&B, 5), "t   x"));
    mete("ctrl\001\002ok\n");
    CK(!strcmp(crun_buf_linea(&B, 6), "ctrlok"));
    mete("ultima");
    crun_buf_cierra(&B);
    CK(B.n == 8 && !strcmp(crun_buf_linea(&B, 7), "ultima"));
    crun_buf_cierra(&B);
    CK(B.n == 8);
    CK(!strcmp(crun_buf_linea(&B, -1), "") && !strcmp(crun_buf_linea(&B, 99), ""));

    /* una linea muy larga se parte sin romper un caracter UTF-8 */
    crun_buf_vacia(&B);
    char largo[1000]; int k = 0;
    for (int i = 0; i < 300; i++) { largo[k++] = (char)0xC3; largo[k++] = (char)0xB1; }   /* 300 enes: 600 bytes */
    largo[k++] = '\n'; largo[k] = 0;
    mete(largo);
    CK(B.n >= 4);
    int total = 0, bien = 1;
    for (int i = 0; i < B.n; i++) {
        const char *l = crun_buf_linea(&B, i); int n = (int)strlen(l);
        total += n;
        if (n >= CRUN_LW || n % 2) bien = 0;
        if (n && ((unsigned char)l[0] & 0xC0) == 0x80) bien = 0;
    }
    CK(total == 600 && bien);

    /* llena: se conservan las CRUN_NL ultimas, en orden */
    crun_buf_vacia(&B);
    char l[32];
    for (int i = 0; i < CRUN_NL + 123; i++) { snprintf(l, sizeof l, "linea %d\n", i); mete(l); }
    CK(B.n == CRUN_NL);
    snprintf(l, sizeof l, "linea %d", 123); CK(!strcmp(crun_buf_linea(&B, 0), l));
    snprintf(l, sizeof l, "linea %d", CRUN_NL + 122); CK(!strcmp(crun_buf_linea(&B, CRUN_NL - 1), l));

    /* basura: nada se sale */
    crun_buf_vacia(&B);
    unsigned x = 12345;
    for (int r = 0; r < 20000; r++) {
        char b[64]; int n = (int)(x % 64);
        for (int i = 0; i < n; i++) { x = x * 1103515245u + 12345u; b[i] = (char)(x >> 16); }
        crun_buf_mete(&B, b, (uint32_t)n);
        if (B.n < 0 || B.n > CRUN_NL || B.pn < 0 || B.pn >= CRUN_LW) { CK(0); break; }
    }
    crun_buf_cierra(&B);
    for (int i = 0; i < B.n; i++) if (strlen(crun_buf_linea(&B, i)) >= CRUN_LW) { CK(0); break; }

    printf("test_coder_run_333: %d comprobaciones, %d fallos\n", g_n, g_mal);
    return g_mal != 0;
}
