/* SOMA_251_RUTAS: caminos de los agentes por palabras completas */
#include <stdio.h>
#include <string.h>
#include "agent_ruta.h"
static int f, n;
#define CK(c, m) do { n++; if (!(c)) { f++; printf("FALLO: %s\n", m); } } while (0)
int main(void)
{
    char p[128];
    /* palabras completas */
    CK(!ar_word("crea un programa que cuente hasta diez", "ram"), "programa no contiene la palabra ram");
    CK(ar_word("¿Cuánta RAM libre hay?", "ram"), "RAM");
    CK(ar_word("Redacción sobre la guerra", "redaccion"), "sin tildes ni mayusculas");
    CK(ar_word("estoy redactando", "redact*"), "prefijo");
    CK(ar_word("dime el estado del sistema, por favor", "estado del sistema"), "frase");
    CK(!ar_word("describe la Primera Guerra Mundial", "escribe"), "describe no es escribe");
    CK(!ar_word("la creación del universo", "crea"), "creacion no es crea");
    /* guardar */
    CK(!ar_wants_save("escribe una redacción de 20 frases sobre la Primera Guerra Mundial", p, sizeof p), "escribir no es guardar");
    CK(!ar_wants_save("describe la Primera Guerra Mundial en cinco frases", p, sizeof p), "describir no es guardar");
    CK(ar_wants_save("escribe un poema y guárdalo", p, sizeof p) && !p[0], "guardalo sin ruta");
    CK(ar_wants_save("redacta una nota en /workspace/nota.txt", p, sizeof p) && !strcmp(p, "/workspace/nota.txt"), "ruta absoluta");
    CK(ar_wants_save("haz una lista de la compra en compra.txt", p, sizeof p) && !strcmp(p, "/workspace/compra.txt"), "nombre relativo");
    CK(!ar_wants_save("resume https://example.com/a.html por favor", p, sizeof p), "un enlace no es un archivo");
    CK(ar_wants_save("escribe la redacción en un archivo", p, sizeof p), "en un archivo");
    /* mayordomo */
    CK(!ar_mayordomo("crea un programa que cuente hasta diez"), "programa no va al mayordomo");
    CK(!ar_mayordomo("dime un resumen del programa Erasmus"), "programa Erasmus no va al mayordomo");
    CK(ar_mayordomo("dime cuánta RAM libre hay"), "RAM libre si");
    CK(ar_mayordomo("muestra los archivos de /workspace/"), "listar archivos si");
    CK(ar_mayordomo("lista las carpetas"), "carpetas si");
    CK(!ar_mayordomo("redacta un texto sobre la memoria histórica"), "redaccion no");
    /* respuesta directa */
    CK(ar_direct_ok("escribe una redacción de 20 frases sobre la Primera Guerra Mundial"), "redaccion directa");
    CK(ar_direct_ok("hola, ¿qué tal?"), "saludo directo");
    CK(ar_direct_ok("explícame el sistema solar"), "explicacion directa");
    CK(ar_direct_ok("traduce al inglés: buenos días"), "traduccion directa");
    CK(!ar_direct_ok("busca las noticias de hoy sobre Valencia"), "noticias: herramientas");
    CK(!ar_direct_ok("¿cuánta RAM libre hay?"), "ram: herramientas");
    CK(!ar_direct_ok("lee /workspace/nota.txt"), "ruta: herramientas");
    CK(!ar_direct_ok("¿quién es el presidente actual de Francia?"), "actual: herramientas");
    CK(!ar_direct_ok("abre el navegador"), "abrir: herramientas");
    printf("rutas 251: %d/%d correctas\n", n - f, n);
    return f ? 1 : 0;
}
