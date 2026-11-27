/* SOMA_363_ACCESO: ver acceso.h */
#include <stdint.h>
#include "acceso.h"
#include "unidades.h"
#include "fs.h"
#include "agent_policy.h"

static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void ap(char *o, uint32_t cap, uint32_t *p, const char *s) { while (s && *s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p] = 0; }

int acceso_usuario(void) { return unid_activa(UNID_USUARIO); }
int acceso_carpeta(int tipo, char *out, uint32_t cap)
{
    uint32_t p = 0;
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!acceso_usuario() || tipo < 0 || tipo >= ACC_N) { ap(out, cap, &p, "/tmp"); return -1; }
    /* las subcarpetas: dentro de una de las 6 de serie */
    static const struct { int padre; const char *nombre; } SUB[] = {
        { ACC_IMAGENES, "Fotos" }, { ACC_IMAGENES, "Dibujos" }, { ACC_IMAGENES, "Capturas" }, { ACC_DOCUMENTOS, "Hojas de c\xC3\xA1lculo" },
    };
    int base = tipo >= ACC_FOTOS ? SUB[tipo - ACC_FOTOS].padre : tipo;
    ap(out, cap, &p, "/usuario/"); ap(out, cap, &p, UNID_CARPETAS_USUARIO[base]);
    uint64_t t; int d = 0;
    if (unid_tam(out, &t, &d) != 0 || !d) (void)unid_mkdir(out, 0);
    if (tipo >= ACC_FOTOS) {
        ap(out, cap, &p, "/"); ap(out, cap, &p, SUB[tipo - ACC_FOTOS].nombre);
        d = 0;
        if (unid_tam(out, &t, &d) != 0 || !d) (void)unid_mkdir(out, 0);
    }
    return 0;
}
int acceso_libre(const char *dir, const char *pre, const char *ext, char *out, uint32_t cap)
{
    for (uint32_t i = 1; i < 10000; ++i) {
        uint32_t p = 0; char n[8]; int k = 0;
        uint32_t v = i; char t[8]; int m = 0; do { t[m++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (m < 3) t[m++] = '0';
        while (m) n[k++] = t[--m];
        n[k] = 0;
        ap(out, cap, &p, dir); if (p && out[p - 1] != '/') ap(out, cap, &p, "/");
        ap(out, cap, &p, pre); ap(out, cap, &p, n); ap(out, cap, &p, ext);
        if (p + 1 >= cap) return -1;
        if (vfs_size(out) < 0) { uint64_t t2; int d = 0; if (unid_tam(out, &t2, &d) != 0) return 0; }
    }
    return -1;
}
int acceso_sec_abre(struct acc_sec *s, const char *ruta)
{
    if (!s || !ruta || sl(ruta) + 1 > sizeof s->ruta) return -1;
    uint32_t p = 0; ap(s->ruta, sizeof s->ruta, &p, ruta);
    s->pos = 0; s->err = 0;
    if (unid_ruta(ruta, 0) >= 0) s->err = unid_escribir(ruta, "", 0, 0) != 0;
    else s->err = vfs_write(ruta, "", 0) < 0;
    return s->err ? -1 : 0;
}
int acceso_sec_pon(void *u, uint64_t off, const void *b, uint32_t n)
{
    struct acc_sec *s = (struct acc_sec *)u;
    if (!s || s->err || off != s->pos) { if (s) s->err = 1; return -1; }
    if (!n) return 0;
    if (unid_ruta(s->ruta, 0) >= 0) {
        const uint8_t *q = (const uint8_t *)b;
        while (n) { uint32_t k = n > (1u << 20) ? (1u << 20) : n; if (unid_anadir(s->ruta, q, k, 0) != 0) { s->err = 1; return -1; } q += k; n -= k; s->pos += k; }
        return 0;
    }
    s->err = 1;                                                        /* /tmp: en RAM no se escriben archivos grandes por trozos */
    return -1;
}
int acceso_sec_fin(struct acc_sec *s, int ok)
{
    if (!s) return -1;
    if (!ok || s->err) { if (unid_ruta(s->ruta, 0) >= 0) (void)unid_borrar(s->ruta, 0); else (void)vfs_delete(s->ruta); return -1; }
    return 0;
}
int acceso_ruta_ok(const char *r)
{
    char c[AGENT_POLICY_PATH_MAX + 1];
    if (!r || !agent_path_canon(r, c, sizeof c)) return 0;
    return agent_path_has_prefix(c, "/usuario") || agent_path_has_prefix(c, "/usb");
}
