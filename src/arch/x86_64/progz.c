/* SOMA_314_PROGZ: ver progz.h. */
#include "progz.h"
#include "progz_fmt.h"
#include "web_inflate.h"
#include "web_util.h"
#include "console.h"

int progz_abre(const uint8_t *ini, const uint8_t *fin, const char *nombre, struct progz *p)
{
    p->elf = 0; p->len = 0;
    struct progz_cab c;
    size_t zn = (size_t)(fin - ini);
    if (fin <= ini || !progz_cabecera(ini, zn, &c)) {
        kprint("[progz] "); kprint(nombre); kprint(": el programa incrustado no tiene una cabecera valida\n");
        return -1;
    }
    struct wb o = { 0, 0, 0, 0 };
    int r = wz_inflate(ini + PROGZ_CAB, (uint32_t)(zn - PROGZ_CAB), 1, &o, (uint32_t)c.tam);   /* zlib, en el servicio zdec */
    if (r != 0 || o.err || !o.p || o.len != c.tam || progz_fnv((const uint8_t *)o.p, o.len) != c.huella) {
        kprint("[progz] "); kprint(nombre); kprint(": no se pudo descomprimir (o la huella no coincide); no se ejecuta\n");
        wb_free(&o);
        return -1;
    }
    p->elf = o.p; p->len = o.len;
    return 0;
}

void progz_cierra(struct progz *p)
{
    struct wb o = { (char *)p->elf, (uint32_t)p->len, (uint32_t)p->len, 0 };
    wb_free(&o);
    p->elf = 0; p->len = 0;
}
