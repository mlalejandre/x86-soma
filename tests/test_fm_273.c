/* SOMA_273_ARCHIVOS: edicion de texto de los dialogos del explorador y validacion de nombres (host):
 * cc -std=gnu11 -g -Wall -Wextra -fsanitize=address,undefined -Isrc/apps tests/test_fm_273.c -o /tmp/tf */
#include <stdio.h>
#include <string.h>
#include "fm_edit.h"
static int fallos = 0, n = 0;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO (l.%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void)
{
    char b[16]; struct fm_ed e;
    fm_ed_set(&e, b, sizeof b, "foto.png");
    CK(e.len == 8 && e.caret == 8 && !fm_ed_has_sel(&e), "set");
    fm_ed_select_base(&e, 0);
    CK(e.s0 == 0 && e.s1 == 4 && e.caret == 4, "base sin extension: %u %u", e.s0, e.s1);
    fm_ed_insert(&e, 'a'); fm_ed_insert(&e, 'b');
    CK(!strcmp(b, "ab.png") && e.caret == 2 && !fm_ed_has_sel(&e), "teclear sustituye la seleccion y conserva .png: %s", b);
    fm_ed_back(&e); CK(!strcmp(b, "a.png") && e.caret == 1, "retroceso");
    fm_ed_supr(&e); CK(!strcmp(b, "apng") && e.caret == 1, "supr");
    fm_ed_home(&e); fm_ed_insert(&e, 'X'); CK(!strcmp(b, "Xapng") && e.caret == 1, "inicio+insertar");
    fm_ed_end(&e); CK(e.caret == 5, "fin");
    fm_ed_left(&e); fm_ed_left(&e); CK(e.caret == 3, "izquierda");
    fm_ed_right(&e); CK(e.caret == 4, "derecha");
    fm_ed_select_all(&e); fm_ed_insert(&e, 'z'); CK(!strcmp(b, "z") && e.caret == 1, "seleccionar todo y teclear");
    fm_ed_back(&e); fm_ed_back(&e); fm_ed_supr(&e); CK(e.len == 0 && e.caret == 0, "vacio: retroceso y supr no rompen");
    /* carpeta: se selecciona todo, aunque lleve punto */
    fm_ed_set(&e, b, sizeof b, "v1.2"); fm_ed_select_base(&e, 1); CK(e.s1 == 4, "carpeta con punto, todo");
    /* sin extension y archivo oculto */
    fm_ed_set(&e, b, sizeof b, "README"); fm_ed_select_base(&e, 0); CK(e.s1 == 6, "sin extension");
    fm_ed_set(&e, b, sizeof b, ".bashrc"); fm_ed_select_base(&e, 0); CK(e.s1 == 7, "oculto");
    /* UTF-8: la n con virgulilla y la tilde se borran enteras */
    fm_ed_set(&e, b, sizeof b, "ni\xC3\xB1" "o");
    fm_ed_left(&e); CK(e.caret == 4, "izquierda salta la o");
    fm_ed_left(&e); CK(e.caret == 2, "izquierda salta la n con virgulilla (2 bytes): %u", e.caret);
    fm_ed_supr(&e); CK(!strcmp(b, "nio") && e.caret == 2, "supr borra el caracter completo: %s", b);
    fm_ed_set(&e, b, sizeof b, "ni\xC3\xB1" "o"); fm_ed_back(&e); fm_ed_back(&e); CK(!strcmp(b, "ni") && e.caret == 2, "retroceso entero: %s", b);
    /* lleno: no desborda */
    fm_ed_set(&e, b, 6, "abcde"); CK(fm_ed_insert(&e, 'f') == 0 && e.len == 5, "lleno");
    char g[8]; fm_ed_set(&e, g, sizeof g, "0123456"); fm_ed_select_all(&e); CK(fm_ed_insert(&e, 'k') == 1 && !strcmp(g, "k"), "lleno pero con seleccion");
    /* nombres */
    CK(fm_ed_name_ok("Mis fotos") && fm_ed_name_ok("a.txt") && fm_ed_name_ok("ni\xC3\xB1" "o") && fm_ed_name_ok("a b.c"), "validos");
    CK(!fm_ed_name_ok("") && !fm_ed_name_ok(".oculto") && !fm_ed_name_ok("a/b") && !fm_ed_name_ok("a:b") && !fm_ed_name_ok("a*b") && !fm_ed_name_ok("a?b")
       && !fm_ed_name_ok("a\"b") && !fm_ed_name_ok("a<b") && !fm_ed_name_ok("a>b") && !fm_ed_name_ok("a|b") && !fm_ed_name_ok("a\\b"), "prohibidos");
    CK(!fm_ed_name_ok("   ") && !fm_ed_name_ok("a ") && !fm_ed_name_ok("a."), "espacios y punto final");
    /* confinamiento a Mi unidad */
    CK(fm_ed_ruta_ok("/usuario") && fm_ed_ruta_ok("/usuario/Fotos") && fm_ed_ruta_ok("/usuario/a/b/c") && fm_ed_ruta_ok("/usuario/..hola") && fm_ed_ruta_ok("/usuario/a..b"), "rutas de Mi unidad");
    CK(!fm_ed_ruta_ok("/") && !fm_ed_ruta_ok("") && !fm_ed_ruta_ok(0) && !fm_ed_ruta_ok("/etc") && !fm_ed_ruta_ok("/workspace") && !fm_ed_ruta_ok("/big/x") && !fm_ed_ruta_ok("@Biblioteca")
       && !fm_ed_ruta_ok("/usuarios") && !fm_ed_ruta_ok("/usuario2/x") && !fm_ed_ruta_ok("/usuari"), "fuera de Mi unidad");
    CK(!fm_ed_ruta_ok("/usuario/..") && !fm_ed_ruta_ok("/usuario/../etc") && !fm_ed_ruta_ok("/usuario/a/../../etc") && !fm_ed_ruta_ok("/usuario/a/.."), "sin escapar con ..");
    /* aleatorio: nunca se sale del buffer ni deja de terminar en 0 */
    unsigned s = 12345; char r[12]; fm_ed_set(&e, r, sizeof r, "");
    for (int i = 0; i < 200000; ++i) {
        s = s * 1103515245u + 12345u;
        switch ((s >> 16) % 9) { case 0: fm_ed_insert(&e, (char)(32 + (s >> 8) % 90)); break; case 1: fm_ed_insert(&e, (char)(0xC3)); fm_ed_insert(&e, (char)(0xB1)); break; case 2: fm_ed_back(&e); break;
          case 3: fm_ed_supr(&e); break; case 4: fm_ed_left(&e); break; case 5: fm_ed_right(&e); break; case 6: fm_ed_home(&e); break; case 7: fm_ed_end(&e); break; default: fm_ed_select_all(&e); break; }
        if (e.len >= sizeof r || r[e.len] != 0 || e.caret > e.len || e.s1 > e.len) { CK(0, "invariante rota en %d", i); break; }
    }
    CK(1, "aleatorio");
    printf("%s edicion del explorador (273): %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
