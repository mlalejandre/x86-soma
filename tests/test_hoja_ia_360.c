/* SOMA_360_HOJA_GRAFICOS: separar la respuesta del modelo en texto, formula y tabla (hoja_ia_separa). */
#include <stdio.h>
#include <string.h>
#include "hoja_ia.h"
static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)
static void caso(const char *in, const char *t, const char *f, const char *tb)
{
    char b[4096]; strcpy(b, in); char *x, *y, *z; hoja_ia_separa(b, &x, &y, &z);
    CK(!strcmp(x, t), x); CK(!strcmp(y, f), y); CK(!strcmp(z, tb), z);
}
int main(void)
{
    caso("La suma de los gastos es 67,4.", "La suma de los gastos es 67,4.", "", "");
    caso("Usa esta f\303\263rmula:\nFORMULA: =SUMA(B2:B9)", "Usa esta f\303\263rmula:", "=SUMA(B2:B9)", "");
    caso("Para el IVA:\n**F\303\263rmula:** `=B2*21%`\n", "Para el IVA:", "=B2*21%", "");
    caso("formula: SUMA(A1:A3)", "", "=SUMA(A1:A3)", "");
    caso("Aqu\303\255 tienes:\nTABLA:\n```\nMes\tVentas\nEnero\t10\nFebrero\t12\n```\n\n", "Aqu\303\255 tienes:", "", "Mes\tVentas\nEnero\t10\nFebrero\t12");
    caso("Texto\nFORMULA: =PROMEDIO(C2:C5)\nTABLA:\nA\tB\n1\t2", "Texto", "=PROMEDIO(C2:C5)", "A\tB\n1\t2");
    caso("", "", "", "");
    caso("FORMULA:", "", "", "");
    caso("TABLA:", "", "", "");
    printf("test_hoja_ia_360: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos != 0;
}
