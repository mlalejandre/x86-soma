/* SOMA_200: las funciones que pasan un numero a texto no pueden escribir mas de F64_STR_MAX bytes, pida lo que pida la pagina.
 * (Antes: (1e308).toString(2), (1).toExponential(100000) y toLocaleString con maximumFractionDigits enorme desbordaban un bufer
 * de 400 bytes en la pila del nucleo.) Cada llamada escribe en un bufer de F64_STR_MAX exactos: ASan detecta un byte de mas. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "web_f64.h"
static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static int checks, fails;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    static const double V[] = { 1e308, -1.7976931348623157e308, 5e-324, -5e-324, 1e21, 1e20, 123456789.123456789, 0.1, 1e-7, 1e100, 0.000001234, 9.999999999999999e22, 1.5e300, -1e-300, 4.9e-320, 0.5, 255.75, 0.0, -0.0, 1.0 / 3 };
    static const int D[] = { -1000000, -1, 0, 1, 20, 100, 101, 400, 100000, 2147483647 };
    int mx = 0;
    for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
        f64 a = bits(V[i]);
        for (int r = 2; r <= 36; r++) { char *o = malloc(F64_STR_MAX); int l = f64_to_str(a, o, r); CK(l < F64_STR_MAX && (int)strlen(o) == l, "toString(%d) de %g: %d", r, V[i], l); if (l > mx) mx = l; free(o); }
        for (unsigned k = 0; k < sizeof D / sizeof D[0]; k++) {
            char *o = malloc(F64_STR_MAX); int l;
            l = f64_to_fixed(a, D[k], o); CK(l < F64_STR_MAX && (int)strlen(o) == l, "toFixed(%d) de %g: %d", D[k], V[i], l); if (l > mx) mx = l;
            l = f64_to_exponential(a, D[k], o); CK(l < F64_STR_MAX && (int)strlen(o) == l, "toExponential(%d) de %g: %d", D[k], V[i], l); if (l > mx) mx = l;
            l = f64_to_precision(a, D[k], o); CK(l < F64_STR_MAX && (int)strlen(o) == l, "toPrecision(%d) de %g: %d", D[k], V[i], l); if (l > mx) mx = l;
            free(o);
        }
    }
    { char o[F64_STR_MAX]; f64_to_fixed(bits(1.5), 2, o); CK(!strcmp(o, "1.50"), "toFixed(2) = %s", o); f64_to_str(bits(255.0), o, 16); CK(!strcmp(o, "ff"), "toString(16) = %s", o);
      f64_to_exponential(bits(12345.0), 2, o); CK(!strcmp(o, "1.23e+4"), "toExponential(2) = %s", o); f64_to_precision(bits(0.000123), 2, o); CK(!strcmp(o, "0.00012"), "toPrecision(2) = %s", o); }
    printf("test_f64_200: %d comprobaciones, %d fallos (salida mas larga: %d bytes de %d)\n", checks, fails, mx, F64_STR_MAX);
    return fails != 0;
}
