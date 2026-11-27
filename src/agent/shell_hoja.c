/* SOMA_358_HOJA: la orden `hoja` (hoja de calculo). Se incluye desde shell.c. La logica esta en src/apps/hoja.c.
 * SOMA_359_HOJA_VENTANA: `hoja ventana [archivo]` abre la ventana (con ese archivo). Las demas ordenes, con el turno de la GUI. */
#include "hoja.h"
#include "gui.h"
#include "hoja_integra.h"

static int cmd_hoja(const char *args, char *out_buf, uint32_t max_out)
{
    const char *a = args ? args : "";
    while (*a == ' ') ++a;
    if (a[0] == 'v' && a[1] == 'e' && a[2] == 'n' && a[3] == 't' && a[4] == 'a' && a[5] == 'n' && a[6] == 'a' && (a[7] == 0 || a[7] == ' ')) {
        const char *r = a + 7; while (*r == ' ') ++r;
        uint32_t p = 0;
        if (*r) { char m[200]; int tk = gui_turn_begin(); int k = hoja_abre(r, m, sizeof m); gui_turn_end(tk); if (k < 0) { soma_str_put(out_buf, max_out, &p, m); return 1; } hoja_pon_actual(k); }
        gui_open_hoja(0); soma_str_put(out_buf, max_out, &p, "Hoja de c\303\241lculo abierta."); return 1;
    }
    /* SOMA_360_HOJA_GRAFICOS: desde Contactos, el Calendario y hacia el Correo */
    if ((a[0] == 'c' && !hj_slen(a + 9) && a[1] == 'o' && a[2] == 'n' && a[3] == 't' && a[4] == 'a' && a[5] == 'c' && a[6] == 't' && a[7] == 'o' && a[8] == 's') ||
        (a[0] == 'a' && a[1] == 'g' && a[2] == 'e' && a[3] == 'n' && a[4] == 'd' && a[5] == 'a' && (a[6] == 0 || a[6] == ' ')) ||
        (a[0] == 'c' && a[1] == 'a' && a[2] == 'l' && a[3] == 'e' && a[4] == 'n' && a[5] == 'd' && a[6] == 'a' && a[7] == 'r' && a[8] == 'i' && a[9] == 'o' && (a[10] == 0 || a[10] == ' '))) {
        char m[300]; uint32_t p = 0; int tk = gui_turn_begin();
        int k = a[0] == 'c' && a[1] == 'o' ? hoja_desde_contactos(m, sizeof m) : hoja_desde_agenda(a + (a[1] == 'g' ? 6 : 10), m, sizeof m);
        gui_turn_end(tk);
        soma_str_put(out_buf, max_out, &p, m); if (k >= 0) soma_str_put(out_buf, max_out, &p, " (hoja ventana para verlo; hoja guardar para guardarlo)");
        return 1;
    }
    if (a[0] == 'e' && a[1] == 'n' && a[2] == 'v' && a[3] == 'i' && a[4] == 'a' && a[5] == 'r' && (a[6] == 0 || a[6] == ' ')) {
        const char *para = a + 6; while (*para == ' ') ++para;
        char m[300]; uint32_t p = 0; int tk = gui_turn_begin(); int r = hoja_envia_correo(hoja_actual(), para, m, sizeof m); gui_turn_end(tk);
        if (!r) gui_open_correo();
        soma_str_put(out_buf, max_out, &p, m); return 1;
    }
    /* los libros los toca tambien la ventana: la orden se ejecuta con el turno de la GUI (desde el agente o un hilo de fondo) */
    int tok = gui_turn_begin();
    int r = hoja_cmd(a, out_buf, max_out);
    gui_turn_end(tok);
    return r;
}
