/* SOMA_363_ACCESO: los medios USB externos como unidades (/usb/<nombre>) y las reglas de acceso de la persona.
 * src/core/unidades.c con tres discos en memoria (USUARIO, un pendrive "KINGSTON" y otro "FOTOS"), la politica de escritura
 * (agent_policy.h) y la del explorador (fm_edit.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "unidades.h"
#include "exfat.h"
#include "agent_policy.h"
#include "fm_edit.h"
#include "acceso.h"
/* lo que acceso.c usa del VFS: aqui solo cuenta /usuario (unidades.c) */
int vfs_size(const char *n) { uint64_t t = 0; int d = 0; return unid_tam(n, &t, &d) == 0 && !d ? (int)t : -1; }
int vfs_write(const char *n, const char *d, uint32_t l) { return unid_escribir(n, d, l, 0) == 0 ? (int)l : -1; }
int vfs_delete(const char *n) { return unid_borrar(n, 0); }

#define SEC 131072u                                        /* 64 MiB por disco */
struct disco { uint8_t *d; };
static int d_rd(void *c, uint64_t lba, uint32_t n, void *b) { struct disco *k = c; if (lba + n > SEC) return -1; memcpy(b, k->d + lba * 512, (size_t)n * 512); return 0; }
static int d_wr(void *c, uint64_t lba, uint32_t n, const void *b) { struct disco *k = c; if (lba + n > SEC) return -1; memcpy(k->d + lba * 512, b, (size_t)n * 512); return 0; }
static void *al(uint32_t n) { return malloc(n); }
static uint32_t ahora(void) { return (46u << 25) | (11u << 21) | (27u << 16); }

static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    static struct disco dk[3]; struct exfat_io io[3];
    const char *et[3] = { "USUARIO", "KINGSTON", "FOTOS" };
    for (int i = 0; i < 3; ++i) {
        dk[i].d = calloc(SEC, 512);
        struct exfat_io x = { d_rd, d_wr, &dk[i], al, free, ahora }; io[i] = x;
        CK(exfat_format(&io[i], 0, SEC, et[i], 0x1234u + (uint32_t)i) == 0, "formatear %s", et[i]);
    }
    CK(unid_montar(UNID_USUARIO, &io[0], SEC) == 0, "montar USUARIO");
    CK(unid_montar_ext(UNID_EXT0, &io[1], SEC, "KINGSTON") == 0, "montar /usb/KINGSTON");
    CK(unid_montar_ext(UNID_EXT0 + 1, &io[2], SEC, "FO/TOS") == 0, "montar /usb/FO_TOS (sin '/')");
    CK(unid_montar_ext(UNID_USUARIO, &io[2], SEC, "X") != 0, "unid_montar_ext no puede montar sobre USUARIO");
    CK(unid_montar_ext(UNID_N, &io[2], SEC, "X") != 0, "unidad fuera de rango");
    CK(unid_montar_ext(UNID_EXT0 + 2, &io[2], SEC, "") != 0, "nombre vacio");

    /* rutas */
    const char *resto = 0;
    CK(unid_ruta("/usb/KINGSTON", &resto) == UNID_EXT0 && !strcmp(resto, "/"), "/usb/KINGSTON es la unidad externa 0");
    CK(unid_ruta("/usb/KINGSTON/a/b.txt", &resto) == UNID_EXT0 && !strcmp(resto, "/a/b.txt"), "resto de la ruta");
    CK(unid_ruta("/usb/KINGSTONX/a", 0) < 0, "/usb/KINGSTONX no es KINGSTON");
    CK(unid_ruta("/usb/KING", 0) < 0, "prefijo incompleto");
    CK(unid_ruta("/usb", 0) < 0 && unid_ruta("/usb/", 0) < 0, "/usb no es una unidad (es la lista de medios)");
    CK(unid_ruta("/usb/FO_TOS/x", 0) == UNID_EXT0 + 1, "/usb/FO_TOS");
    CK(unid_ruta("/usuario/Documentos", 0) == UNID_USUARIO, "/usuario sigue igual");
    CK(!strcmp(unid_nombre(UNID_EXT0), "usb/KINGSTON"), "nombre de la unidad externa: %s", unid_nombre(UNID_EXT0));

    /* lectura y escritura (sin privilegios: las externas son de la persona) */
    CK(unid_mkdir("/usb/KINGSTON/Fotos", 0) == 0, "mkdir en el USB");
    CK(unid_escribir("/usb/KINGSTON/Fotos/a.txt", "hola", 4, 0) == 0, "escribir en el USB");
    CK(unid_anadir("/usb/KINGSTON/Fotos/a.txt", " mundo", 6, 0) == 0, "anadir en el USB");
    char b[32] = { 0 };
    CK(unid_leer("/usb/KINGSTON/Fotos/a.txt", 0, b, sizeof b - 1) == 10 && !memcmp(b, "hola mundo", 10), "leer del USB");
    CK(unid_leer("/usb/FO_TOS/Fotos/a.txt", 0, b, 4) < 0, "cada medio es suyo (no se mezclan)");
    CK(unid_escribir("/usb/KINGSTON/../usuario/x", "x", 1, 0) != 0, "'..' no sale del medio");
    CK(unid_escribir("/modelos/x", "x", 1, 0) != 0, "MODELOS sigue de solo lectura (no esta montada)");
    struct exfat_dirent d[8]; int tot = 0;
    CK(unid_listar("/usb/KINGSTON", d, 8, &tot) == 1 && d[0].is_dir && !strcmp(d[0].name, "Fotos"), "listar el USB");
    CK(unid_renombrar("/usb/KINGSTON/Fotos/a.txt", "/usb/KINGSTON/Fotos/b.txt", 0) == 0, "renombrar dentro del USB");
    CK(unid_renombrar("/usb/KINGSTON/Fotos/b.txt", "/usuario/b.txt", 0) != 0, "renombrar no salta de un medio a otro");

    /* desmontar (quitar el pendrive) */
    unid_desmontar(UNID_EXT0);
    CK(unid_ruta("/usb/KINGSTON/Fotos/b.txt", 0) < 0, "tras quitarlo, la ruta ya no es de ninguna unidad");
    CK(unid_leer("/usb/KINGSTON/Fotos/b.txt", 0, b, 4) < 0, "ni se lee");
    CK(unid_ruta("/usb/FO_TOS", 0) == UNID_EXT0 + 1, "el otro sigue");
    CK(unid_montar_ext(UNID_EXT0, &io[1], SEC, "KINGSTON") == 0 && unid_leer("/usb/KINGSTON/Fotos/b.txt", 0, b, 10) == 10, "al volver a enchufarlo, ahi esta");

    /* SOMA_363_ACCESO: las carpetas de cada programa (las 6 de serie y sus subcarpetas), creadas al pedirlas */
    {
        char c[96]; uint64_t tm = 0; int dd = 0;
        CK(acceso_usuario() == 1, "hay unidad del usuario");
        CK(acceso_carpeta(ACC_FOTOS, c, sizeof c) == 0 && !strcmp(c, "/usuario/Im\xC3\xA1genes/Fotos"), "fotos: %s", c);
        CK(unid_tam(c, &tm, &dd) == 0 && dd, "Imagenes/Fotos existe");
        CK(acceso_carpeta(ACC_DIBUJOS, c, sizeof c) == 0 && !strcmp(c, "/usuario/Im\xC3\xA1genes/Dibujos"), "dibujos: %s", c);
        CK(acceso_carpeta(ACC_CAPTURAS, c, sizeof c) == 0 && !strcmp(c, "/usuario/Im\xC3\xA1genes/Capturas"), "capturas: %s", c);
        CK(acceso_carpeta(ACC_HOJAS, c, sizeof c) == 0 && !strcmp(c, "/usuario/Documentos/Hojas de c\xC3\xA1lculo"), "hojas: %s", c);
        dd = 0; CK(unid_tam(c, &tm, &dd) == 0 && dd, "Documentos/Hojas de calculo existe");
        CK(acceso_carpeta(ACC_DESCARGAS, c, sizeof c) == 0 && !strcmp(c, "/usuario/Descargas"), "descargas: %s", c);
        CK(acceso_carpeta(ACC_N, c, sizeof c) != 0 && !strcmp(c, "/tmp"), "tipo fuera de rango: /tmp");
        char nm[128];
        CK(acceso_libre("/usuario/Im\xC3\xA1genes/Fotos", "foto_", ".jpg", nm, sizeof nm) == 0 && strstr(nm, "foto_001.jpg"), "primer nombre libre: %s", nm);
        CK(unid_escribir(nm, "x", 1, 0) == 0 && acceso_libre("/usuario/Im\xC3\xA1genes/Fotos", "foto_", ".jpg", nm, sizeof nm) == 0 && strstr(nm, "foto_002.jpg"), "el siguiente: %s", nm);
        CK(acceso_ruta_ok("/usuario/Documentos") && acceso_ruta_ok("/usb/FO_TOS/a") && !acceso_ruta_ok("/workspace/a") && !acceso_ruta_ok("/usuario/../etc"), "acceso_ruta_ok");
    }

    /* politica: donde puede escribir la persona */
    CK(usuario_ruta_escribible("/usuario/Im\303\241genes/a.png"), "/usuario");
    CK(usuario_ruta_escribible("/usb/KINGSTON/a.png"), "/usb");
    CK(usuario_ruta_escribible("/tmp/a"), "/tmp");
    CK(!usuario_ruta_escribible("/workspace/a"), "/workspace no es de la persona");
    CK(!usuario_ruta_escribible("/usbx/a"), "/usbx no es /usb");
    CK(!usuario_ruta_escribible("/usb/../etc/passwd"), "/usb/.. no escapa");
    CK(!usuario_ruta_escribible("/etc/x"), "/etc");
    /* el explorador */
    CK(fm_ed_ruta_ok("/usuario") && fm_ed_ruta_ok("/usuario/Documentos"), "explorador: /usuario");
    CK(fm_ed_ruta_ok("/usb") && fm_ed_ruta_ok("/usb/KINGSTON/Fotos"), "explorador: /usb");
    CK(!fm_ed_ruta_ok("/usbx") && !fm_ed_ruta_ok("/usuariox") && !fm_ed_ruta_ok("/"), "explorador: nada mas");
    CK(!fm_ed_ruta_ok("/usb/KINGSTON/../../etc"), "explorador: sin '..'");
    CK(!fm_ed_ruta_ok("/workspace") && !fm_ed_ruta_ok("/etc"), "explorador: ni /workspace ni /etc");

    for (int i = 0; i < 3; ++i) free(dk[i].d);
    printf("test_acceso_363: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos ? 1 : 0;
}
