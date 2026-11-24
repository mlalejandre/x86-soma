/* shell_correo.c: SOMA_295_CORREO: la orden `correo` (la app Correo). Lo incluye src/agent/shell.c. */
#include "correo.h"
static int cmd_correo(const char *args, char *out_buf, uint32_t max_out)
{
    (void)correo_cmd(args ? args : "", out_buf, max_out);
    return 1;
}
