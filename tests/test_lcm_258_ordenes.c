/* SOMA_258_LCM: las ordenes nuevas 'cerrar' y 'contiene' (src/agent/lcm_ordenes_shell.inc) con imitaciones de la GUI y del VFS.
 * cc -O1 -Wall -Isrc/agent -o /tmp/t tests/test_lcm_258_ordenes.c && /tmp/t */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FS_NAME_MAX 64
static void soma_str_put(char *o, uint32_t m, uint32_t *p, const char *s) { while (*s && *p + 1 < m) o[(*p)++] = *s++; o[*p] = 0; }
static void soma_str_putc(char *o, uint32_t m, uint32_t *p, char c) { if (*p + 1 < m) { o[(*p)++] = c; o[*p] = 0; } }
static void soma_str_put_dec(char *o, uint32_t m, uint32_t *p, uint64_t v) { char b[24]; snprintf(b, sizeof b, "%llu", (unsigned long long)v); soma_str_put(o, m, p, b); }
static void kprint(const char *s) { (void)s; }
static void shell_emit(const char *s, char *o, uint32_t m) { uint32_t p = 0; o[0] = 0; soma_str_put(o, m, &p, s); }
static void shell_copy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static const char *shell_next_arg(const char *s, char *d, uint32_t cap) { while (*s == ' ') s++; uint32_t i = 0; while (*s && *s != ' ') { if (i + 1 < cap) d[i++] = *s; s++; } d[i] = 0; return s; }
static const char *shell_list_start(const char *l) { return l; }
static const char *shell_list_next(const char *p, char *item, uint32_t cap) { uint32_t i = 0; while (*p && *p != '\n') { if (i + 1 < cap) item[i++] = *p; p++; } item[i] = 0; if (*p) p++; return p; }
static void *kmalloc(uint32_t n) { return malloc(n); }
static void kfree(void *p) { free(p); }
static const char *F[][2] = { { "/mis_archivos/compra.txt", "Pan, leche y CASA rural" }, { "/mis_archivos/viaje.md", "Hotel en Roma" }, { "/workspace/notas.txt", "la casa de Ana" }, { 0, 0 } };
static void vfs_format_list_ext(int all, const char *root, char *out, uint32_t max) { (void)all; (void)root; uint32_t p = 0; out[0] = 0; for (int i = 0; F[i][0]; i++) { soma_str_put(out, max, &p, F[i][0]); soma_str_putc(out, max, &p, '\n'); } soma_str_put(out, max, &p, "/mis_archivos/fotos/\n"); }
static char *vfs_read_alloc(const char *path, uint32_t *len) { for (int i = 0; F[i][0]; i++) if (!strcmp(F[i][0], path)) { *len = (uint32_t)strlen(F[i][1]); char *b = malloc(*len + 1); memcpy(b, F[i][1], *len + 1); return b; } return 0; }
static int abiertas[8] = { 2, 3, 10, 20 }, nab = 4;
static const char *T[32] = { "Escritorio", "SOMA (agente)", "Terminal", "Navegador", "Imagenes", "Archivos", "Bloc de notas", "Memoria de agentes", "Maquetador", "Agentes", "Paint", "Coder", "Media",
                             "Programa SOMA", "Ajustes", "Lector PDF", "Monitor del sistema", "Grabadora", "Camara", "Captura", "YouTube" };
const char *gui_app_title(int a) { return a >= 0 && a < 21 ? T[a] : "Aplicacion"; }
int gui_app_list(int *apps, int max) { int n = 0; for (int i = 0; i < nab && n < max; i++) apps[n++] = abiertas[i]; return n; }
int gui_close_app(int app) { for (int i = 0; i < nab; i++) if (abiertas[i] == app) { for (int k = i + 1; k < nab; k++) abiertas[k - 1] = abiertas[k]; nab--; return 1; } return 0; }
int gui_enter(void) { return 0; }
void gui_leave(int t) { (void)t; }
#include "lcm_ordenes_shell.inc"   /* -Isrc/agent */

static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    char o[600];
    cmd_cerrar("el Paint", o, sizeof o);        CK(!strcmp(o, "Cerrada: Paint") && nab == 3, "cerrar paint: %s", o);
    cmd_cerrar("", o, sizeof o);                CK(!strcmp(o, "Cerrada: YouTube") && nab == 2, "cerrar (la de delante): %s", o);
    cmd_cerrar("internet", o, sizeof o);        CK(!strcmp(o, "Cerrada: Navegador"), "sinonimo: %s", o);
    cmd_cerrar("cámara", o, sizeof o);          CK(strstr(o, "No hay ninguna") != 0, "no abierta: %s", o);
    cmd_cerrar("la calculadora", o, sizeof o);  CK(strstr(o, "no se que aplicacion") != 0, "desconocida: %s", o);
    cmd_cerrar("todo", o, sizeof o);            CK(nab == 0 && strstr(o, "Cerradas"), "todo: %s", o);
    cmd_cerrar("", o, sizeof o);                CK(strstr(o, "No hay ninguna ventana abierta") != 0, "sin ventanas: %s", o);
    cmd_contiene("casa", o, sizeof o);          CK(strstr(o, "compra.txt") && strstr(o, "notas.txt") && !strstr(o, "viaje") && strstr(o, "2 archivo"), "contiene casa: %s", o);
    cmd_contiene("Roma", o, sizeof o);          CK(strstr(o, "viaje.md") && strstr(o, "1 archivo"), "contiene Roma: %s", o);
    cmd_contiene("zzz", o, sizeof o);           CK(strstr(o, "ningun archivo") != 0, "contiene nada: %s", o);
    cmd_contiene("", o, sizeof o);              CK(strstr(o, "Uso: contiene") != 0, "uso: %s", o);
    printf("  %s ordenes cerrar y contiene: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
