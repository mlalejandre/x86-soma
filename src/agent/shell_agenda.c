/* SOMA_272_AGENDA: la orden `agenda` (calendario). Se incluye desde shell.c. La logica esta en src/apps/agenda.c. */
#include "agenda.h"
#include "agenda_gui.h"

static int cmd_agenda(const char *args, char *out_buf, uint32_t max_out)
{
    const char *a = args ? args : "";
    while (*a == ' ') ++a;
    if (!*a || str_eq(a, "abrir") || str_eq(a, "ventana") || str_eq(a, "calendario")) {
        if (*a) { uint32_t p = 0; gui_open_agenda(); soma_str_put(out_buf, max_out, &p, "Calendario abierto."); return 1; }
        return agenda_cmd(args, out_buf, max_out);                /* sin argumentos: hoy y los proximos dias, en texto */
    }
    if (str_eq(a, "avisos probar") || str_eq(a, "avisos prueba")) {
        agenda_gui_probar();
        return agenda_cmd(args, out_buf, max_out);
    }
    return agenda_cmd(args, out_buf, max_out);
}
