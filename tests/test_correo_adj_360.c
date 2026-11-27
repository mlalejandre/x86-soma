/* SOMA_360_HOJA_GRAFICOS: correo con un archivo adjunto (co_compone en multipart/mixed). Se vuelve a leer con el lector MIME de SOMA:
 * el texto y el adjunto (nombre, tipo y bytes exactos). Tambien con un nombre con acentos y adjuntos de varios tamanos. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "correo_core.h"

void *co_alloc(uint32_t n) { return malloc(n ? n : 1); }
void co_free(void *p) { free(p); }
static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)

static void prueba(uint32_t tam, const char *nombre, const char *cuerpo)
{
    uint8_t *d = malloc(tam + 1); for (uint32_t i = 0; i < tam; ++i) d[i] = (uint8_t)(i * 131u + (i >> 7) * 7u);
    struct co_envio e; memset(&e, 0, sizeof e);
    e.de_nombre = "Ana"; e.de_email = "ana@x.es"; e.para = "luis@y.es"; e.asunto = "La hoja de gastos"; e.cuerpo = cuerpo;
    e.utc_min = 29000000; e.off_min = 60; e.azar = 0x12345678u;
    e.adj_nombre = nombre; e.adj = d; e.adj_n = tam;
    uint32_t cap = tam * 2 + 65536; char *out = malloc(cap);
    uint32_t k = co_compone(&e, out, cap);
    CK(k > tam, "compone");
    CK(strstr(out, "multipart/mixed; boundary=") != 0, "multipart");
    CK(strstr(out, "Content-Transfer-Encoding: base64") != 0, "base64");
    /* ninguna linea de mas de 998 bytes (RFC 5322) */
    { uint32_t col = 0, mx = 0; for (uint32_t i = 0; i < k; ++i) { if (out[i] == '\n') { if (col > mx) mx = col; col = 0; } else ++col; } CK(mx <= 998, "lineas cortas"); }
    struct co_mime *m = calloc(1, sizeof *m); char *texto = malloc(65536);
    CK(co_mime_lee(out, k, m, texto, 65536) >= 0, "se lee");
    CK(strstr(texto, "Te paso la hoja") != 0, "el texto");
    CK(m->nadj == 1, "un adjunto");
    if (m->nadj == 1) {
        CK(!strcmp(m->adj[0].nombre, nombre), "nombre del adjunto");
        CK(strstr(m->adj[0].tipo, strstr(nombre, ".xlsx") ? "spreadsheetml" : "csv") != 0, "tipo");
        uint8_t *v = malloc(tam + 16); uint32_t r = co_adj_saca(out, k, &m->adj[0], v, tam + 16);
        CK(r == tam && !memcmp(v, d, tam), "bytes exactos");
        free(v);
    }
    /* sin adjunto: como antes */
    e.adj = 0; e.adj_n = 0; k = co_compone(&e, out, cap);
    CK(k > 0 && strstr(out, "multipart") == 0 && strstr(out, "Content-Type: text/plain; charset=UTF-8") != 0, "sin adjunto, texto plano");
    /* no cabe: 0, nunca se pasa */
    e.adj = d; e.adj_n = tam; CK(co_compone(&e, out, 3000) == 0 || tam < 1000, "no cabe");
    free(m); free(texto); free(out); free(d);
}
int main(void)
{
    prueba(1, "a.xlsx", "Te paso la hoja.");
    prueba(57, "gastos.xlsx", "Te paso la hoja.\nUn saludo");
    prueba(58, "gastos.csv", "Te paso la hoja.");
    prueba(100000, "Presupuesto a\303\261o 2026.xlsx", "Te paso la hoja con acentos: \303\241\303\251\303\255.");
    prueba(1500000, "grande.xlsx", "Te paso la hoja.");
    CK(!strcmp(co_tipo_mime("x.ODS"), "application/vnd.oasis.opendocument.spreadsheet"), "ods");
    CK(!strcmp(co_tipo_mime("x.bin"), "application/octet-stream"), "otro");
    printf("test_correo_adj_360: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos != 0;
}
