/* SOMA_250_TTS: limpieza del texto que el orbe lee en voz alta */
#include <stdio.h>
#include <string.h>
#include "habla_texto.h"
static int f, n;
static char o[4096];
static void ck(const char *in, const char *want, unsigned max)
{
    n++; habla_limpia(in, o, sizeof o, max);
    if (strcmp(o, want)) { f++; printf("FALLO:\n  entrada: %s\n  sale:    '%s'\n  espero:  '%s'\n", in, o, want); }
}
int main(void)
{
    ck("Hola, ¿qué tal?", "Hola, ¿qué tal?", 0);
    ck("**Hecho**: el archivo `notas.txt` está listo.", "Hecho: el archivo notas.txt está listo.", 0);
    ck("# Resumen\n- uno\n- dos\n1. tres", "Resumen. uno. dos. tres", 0);
    ck("Mira https://example.com/a?b=1 para más.", "Mira el enlace para más.", 0);
    ck("Puedes verlo en https://example.com. ¿Algo más?", "Puedes verlo en el enlace. ¿Algo más?", 0);
    ck("Documento listo: Informe T3\n  Fuente: /workspace/docs/informe_trimestral.html\n  PDF: /workspace/docs/informe_trimestral.pdf (2 paginas)",
       "Documento listo: Informe T3. Fuente: informe trimestral. PDF: informe trimestral (2 paginas)", 0);
    ck("Antes\n```soma\nwin.create(1);\n```\nDespues", "Antes. Despues", 0);
    ck("\033[1;35m[MAQUETADOR]\033[0m hola", "[MAQUETADOR] hola", 0);
    ck("variable_de_ejemplo y _cursiva_", "variable_de_ejemplo y cursiva", 0);
    ck("Uno. Dos. Tres. Cuatro cinco seis siete ocho", "Uno. Dos.", 12);
    ck("", "", 0);
    ck("50 % y 3/4 de 10", "50 % y 3/4 de 10", 0);
    printf("tts 250: %d/%d correctas\n", n - f, n);
    return f ? 1 : 0;
}
