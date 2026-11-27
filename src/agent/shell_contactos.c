/* SOMA_357_CONTACTOS: la orden `contactos` (libreta de direcciones). Se incluye desde shell.c. La logica esta en src/apps/contactos.c. */
#include "contactos.h"
#include "contactos_gui.h"

static int cmd_contactos(const char *args, char *out_buf, uint32_t max_out)
{
    const char *a = args ? args : "";
    while (*a == ' ') ++a;
    if (str_eq(a, "abrir") || str_eq(a, "ventana") || str_eq(a, "agenda")) {
        uint32_t p = 0; gui_open_contactos(); soma_str_put(out_buf, max_out, &p, "Contactos abiertos."); return 1;
    }
    if ((a[0] == 'a' && a[1] == 'b' && a[2] == 'r' && a[3] == 'i' && a[4] == 'r' && a[5] == ' ') && a[6]) {   /* contactos abrir Ana: su ficha */
        struct ct_libreta *l = contactos_libreta(); uint16_t idx[2];
        if (l && ct_resuelve(l, a + 6, idx, 2) == 1) { uint32_t p = 0; contactos_gui_ver(l->c[idx[0]].id); gui_open_contactos(); soma_str_put(out_buf, max_out, &p, "Ficha abierta."); return 1; }
    }
    return contactos_cmd(a, out_buf, max_out);
}
