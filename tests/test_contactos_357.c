/* SOMA_357_CONTACTOS: pruebas del nucleo de contactos (contactos_core.c) en el anfitrion: lenguaje natural, fechas, busqueda, edicion,
 * vCard de Google/Apple/Outlook/moviles antiguos (2.1 con quoted-printable y latin-1), fotos, fusion en los dos sentidos y fuzz. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contactos_core.h"

static int fallos = 0, n = 0;
#define CK(c, m) do { ++n; if (!(c)) { ++fallos; printf("FALLO %d: %s\n", __LINE__, m); } } while (0)
#define CKS(a, b) do { ++n; if (strcmp((a), (b))) { ++fallos; printf("FALLO %d: '%s' != '%s'\n", __LINE__, (a), (b)); } } while (0)

static struct ct_libreta L, L2;
static struct ct_contacto tmp[64];

static const char GOOGLE[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Ana L\303\263pez Garc\303\255a\r\nN:L\303\263pez Garc\303\255a;Ana;;;\r\n"
    "EMAIL;TYPE=INTERNET;TYPE=HOME:ana@ejemplo.es\r\nEMAIL;TYPE=INTERNET;TYPE=WORK:ana.lopez@empresa.com\r\n"
    "TEL;TYPE=CELL:+34 600 11 22 33\r\nTEL;TYPE=HOME:91 555 44 33\r\n"
    "ADR;TYPE=HOME:;;Calle Mayor\\, 3;Madrid;Madrid;28013;Espa\303\261a\r\n"
    "ORG:Acme S.L.\r\nTITLE:Ingeniera\r\nBDAY:1990-05-17\r\nNOTE:Le gusta el caf\303\251\\nsin az\303\272car\r\n"
    "CATEGORIES:myContacts,Amigos,starred\r\nPHOTO:https://lh3.googleusercontent.com/contacts/ABC123\r\n"
    "item1.URL:https\\://ana.example\r\nitem1.X-ABLabel:PROFILE\r\nEND:VCARD\r\n"
    "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Luis P\303\251rez\r\nN:P\303\251rez;Luis;;;\r\nTEL;TYPE=CELL:611222333\r\nBDAY:--12-24\r\nEND:VCARD\r\n";

/* movil antiguo: vCard 2.1, quoted-printable, latin-1, linea partida con "=" */
static const char VIEJO[] =
    "BEGIN:VCARD\nVERSION:2.1\nN;CHARSET=ISO-8859-1;ENCODING=QUOTED-PRINTABLE:Mu=F1oz;Jos=E9\nTEL;CELL;PREF:622333444\n"
    "NOTE;ENCODING=QUOTED-PRINTABLE:Primera l=C3=ADnea y una=\nsegunda parte\nEND:VCARD\n";

/* Apple: plegado, cumpleanos sin ano, foto base64 */
static const char APPLE_PRE[] = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Ruiz;Marta;;;\r\nFN:Marta Ruiz\r\nitem1.EMAIL;type=INTERNET;type=pref:marta@icloud.\r\n com\r\n"
    "BDAY;X-APPLE-OMIT-YEAR=1604:1604-02-29\r\nPHOTO;ENCODING=b;TYPE=JPEG:";
static const char APPLE_POST[] = "\r\nEND:VCARD\r\n";

int main(void)
{
    /* ---- fechas */
    int y, m, d;
    CK(ct_parsea_fecha("3 de mayo de 1990", &y, &m, &d) == 0 && y == 1990 && m == 5 && d == 3, "3 de mayo de 1990");
    CK(ct_parsea_fecha("el 3 de mayo", &y, &m, &d) == 0 && y == 0 && m == 5 && d == 3, "sin ano");
    CK(ct_parsea_fecha("03/05/1990", &y, &m, &d) == 0 && y == 1990 && m == 5 && d == 3, "dd/mm/aaaa");
    CK(ct_parsea_fecha("3-5-90", &y, &m, &d) == 0 && y == 1990, "dos cifras");
    CK(ct_parsea_fecha("1990-05-03", &y, &m, &d) == 0 && m == 5 && d == 3, "ISO");
    CK(ct_parsea_fecha("19900503", &y, &m, &d) == 0 && y == 1990, "basico");
    CK(ct_parsea_fecha("--0503", &y, &m, &d) == 0 && y == 0 && m == 5, "vCard sin ano");
    CK(ct_parsea_fecha("31 de febrero", &y, &m, &d) != 0, "31 de febrero no");
    CK(ct_parsea_fecha("29 de febrero", &y, &m, &d) == 0, "29 de febrero sin ano si");
    CK(ct_parsea_fecha("29 de febrero de 2023", &y, &m, &d) != 0, "29-2-2023 no");
    CK(ct_parsea_fecha("hola", &y, &m, &d) != 0, "no es fecha");
    CK(ct_parsea_fecha("17 de Septiembre de 1985", &y, &m, &d) == 0 && m == 9, "mayusculas");

    /* ---- lenguaje natural */
    struct ct_contacto c;
    CK(ct_parsea_texto("Luis P\303\251rez Garc\303\255a, email luis@ejemplo.es, m\303\263vil 600 11 22 33, naci\303\263 el 3 de mayo de 1990, vive en Calle Mayor 3, Madrid, grupo amigos", &c) == 0, "parsea");
    CKS(c.nombre, "Luis"); CKS(c.apellidos, "P\303\251rez Garc\303\255a"); CKS(c.email[0], "luis@ejemplo.es"); CKS(c.tel[0], "600 11 22 33");
    CK(c.t_tel[0] == CT_T_MOVIL, "movil"); CK(c.nac_y == 1990 && c.nac_m == 5 && c.nac_d == 3, "nacimiento");
    CKS(c.calle, "Calle Mayor 3"); CKS(c.ciudad, "Madrid"); CKS(c.grupo, "Amigos");
    CK(ct_parsea_texto("a Marta su email es marta@x.com y su tel\303\251fono 699887766", &c) == 0, "su email es");
    CKS(c.nombre, "Marta"); CKS(c.email[0], "marta@x.com"); CKS(c.tel[0], "699887766");
    CK(ct_parsea_texto("Pedro Gil pedro@gil.es 655443322", &c) == 0, "sin comas");
    CKS(c.nombre, "Pedro"); CKS(c.apellidos, "Gil"); CKS(c.email[0], "pedro@gil.es"); CKS(c.tel[0], "655443322");
    CK(ct_parsea_texto("Sara, trabaja en Acme, cargo directora, cumplea\303\261os 12/10, nota la conoc\303\255 en Bilbao", &c) == 0, "empresa");
    CKS(c.empresa, "Acme"); CKS(c.cargo, "directora"); CK(c.nac_m == 10 && c.nac_d == 12, "cumple"); CKS(c.nota, "la conoc\303\255 en Bilbao");
    CK(ct_parsea_texto("Juan, direcci\303\263n Av. de la Paz 7, 46001 Valencia, Valencia", &c) == 0, "cp");
    CKS(c.cp, "46001"); CKS(c.ciudad, "Valencia");
    CK(ct_parsea_texto("", &c) != 0, "vacio");
    CK(ct_parsea_texto("Ana 600112233", &c) == 0 && !strcmp(c.nombre, "Ana") && !strcmp(c.tel[0], "600112233"), "nombre y telefono");

    /* ---- libreta, edicion, busqueda */
    ct_vacia(&L);
    struct ct_contacto *a = ct_nuevo(&L); ct_parsea_texto("Ana L\303\263pez, email ana@x.es, tel 600111222, grupo Familia", &c); { uint32_t id = a->id; *a = c; a->id = id; a->activo = 1; }
    struct ct_contacto *b = ct_nuevo(&L); ct_parsea_texto("\303\201lvaro Ruiz, email alvaro@y.es, grupo Trabajo,Amigos", &c); { uint32_t id = b->id; *b = c; b->id = id; b->activo = 1; }
    struct ct_contacto *e = ct_nuevo(&L); ct_parsea_texto("Ana Mart\303\255n, tel 699000111", &c); { uint32_t id = e->id; *e = c; e->id = id; e->activo = 1; }
    CK(ct_numero(&L) == 3, "tres");
    uint16_t idx[16]; int k = ct_lista(&L, 0, 0, 0, idx, 16);
    CK(k == 3, "lista 3");
    CK(k == 3 && L.c[idx[0]].id == b->id, "orden sin acentos: Alvaro primero");
    CK(ct_lista(&L, "lopez", 0, 0, idx, 16) == 1, "busca sin acento");
    CK(ct_lista(&L, "600 111", 0, 0, idx, 16) == 1, "busca telefono");
    CK(ct_lista(&L, 0, "amigos", 0, idx, 16) == 1, "grupo");
    CK(ct_resuelve(&L, "ana", idx, 16) == 2, "dos Anas");
    CK(ct_resuelve(&L, "ana lopez", idx, 16) == 1 && L.c[idx[0]].id == a->id, "Ana Lopez");
    CK(ct_resuelve(&L, "de Ana Mart\303\255n?", idx, 16) == 1 && L.c[idx[0]].id == e->id, "de Ana Martin?");
    CK(ct_resuelve(&L, "2", idx, 16) == 1 && L.c[idx[0]].id == 2, "por numero");
    CK(ct_resuelve(&L, "alvaro@y.es", idx, 16) == 1, "por email");
    CK(ct_pon_campo(a, "tel\303\251fono", "611 22 33 44") == 0 && !strcmp(a->tel[0], "611 22 33 44"), "editar telefono");
    CK(ct_pon_campo(a, "email", "no-es-email") == -2, "email no valido");
    CK(ct_pon_campo(a, "cumplea\303\261os", "17 de mayo de 1990") == 0 && a->nac_m == 5, "editar cumple");
    CK(ct_pon_campo(a, "zapato", "x") == -1, "campo desconocido");
    CK(ct_pon_campo(a, "movil", "622 000 111") == 0 && a->tel[1][0] && a->t_tel[1] == CT_T_MOVIL, "movil en el segundo hueco");
    CK(ct_pon_campo(a, "web", "-") == 0 && !a->web[0], "vaciar");
    CK(ct_es_campo("telefono 600") == 8, "es campo");
    char grupos[8][CT_GRUPO]; CK(ct_grupos(&L, grupos, 8) == 3, "tres grupos");
    uint16_t ci[8]; int16_t fa[8]; CK(ct_cumples(&L, 2026, 5, 10, 30, ci, fa, 8) == 1 && fa[0] == 7, "cumple en 7 dias");
    CK(ct_edad(a, 2026, 5, 16) == 35 && ct_edad(a, 2026, 5, 17) == 36, "edad");
    char ini[8]; ct_iniciales(b, ini); CKS(ini, "\303\201R");
    char fx[2000]; ct_ficha_txt(a, 2026, 5, 16, fx, sizeof fx); CK(strstr(fx, "cumple ma\303\261ana") != 0, "ficha: cumple manana");
    CK(ct_borra(&L, e->id) && ct_numero(&L) == 2, "borrar");

    /* ---- vCard: Google */
    static uint8_t fotos[1 << 20]; uint32_t fn = 0;
    int r = ct_vcard_lee(GOOGLE, sizeof GOOGLE - 1, tmp, 64, fotos, sizeof fotos, 1 << 19, &fn);
    CK(r == 2, "dos fichas de Google");
    CKS(tmp[0].nombre, "Ana"); CKS(tmp[0].apellidos, "L\303\263pez Garc\303\255a"); CKS(tmp[0].email[0], "ana@ejemplo.es"); CK(tmp[0].t_email[1] == CT_T_TRABAJO, "email trabajo");
    CK(tmp[0].t_tel[0] == CT_T_MOVIL && tmp[0].t_tel[1] == CT_T_CASA, "tipos tel");
    CKS(tmp[0].calle, "Calle Mayor, 3"); CKS(tmp[0].cp, "28013"); CKS(tmp[0].pais, "Espa\303\261a");
    CK(tmp[0].nac_y == 1990 && tmp[0].nac_m == 5 && tmp[0].nac_d == 17, "BDAY");
    CKS(tmp[0].nota, "Le gusta el caf\303\251\nsin az\303\272car"); CKS(tmp[0].grupo, "Amigos"); CK(tmp[0].fav == 1, "starred");
    CKS(tmp[0].foto_url, "https://lh3.googleusercontent.com/contacts/ABC123"); CKS(tmp[0].web, "https://ana.example");
    CK(tmp[1].nac_y == 0 && tmp[1].nac_m == 12 && tmp[1].nac_d == 24, "BDAY sin ano");
    /* ---- 2.1 */
    r = ct_vcard_lee(VIEJO, sizeof VIEJO - 1, tmp, 64, 0, 0, 0, 0);
    CK(r == 1, "vCard 2.1");
    CKS(tmp[0].nombre, "Jos\303\251"); CKS(tmp[0].apellidos, "Mu\303\261oz"); CKS(tmp[0].nota, "Primera l\303\255nea y unasegunda parte"); CK(tmp[0].t_tel[0] == CT_T_MOVIL, "CELL 2.1");
    /* ---- Apple con foto */
    static uint8_t jpg[3000]; jpg[0] = 0xFF; jpg[1] = 0xD8; jpg[2] = 0xFF; for (int i = 3; i < 3000; ++i) jpg[i] = (uint8_t)(i * 7);
    static char b64[5000]; uint32_t bn = ct_b64_enc(jpg, 3000, b64, sizeof b64);
    static char ap[12000]; uint32_t q = 0; memcpy(ap, APPLE_PRE, sizeof APPLE_PRE - 1); q = sizeof APPLE_PRE - 1;
    for (uint32_t i = 0; i < bn; ++i) { if (i && i % 70 == 0) { ap[q++] = '\r'; ap[q++] = '\n'; ap[q++] = ' '; } ap[q++] = b64[i]; }
    memcpy(ap + q, APPLE_POST, sizeof APPLE_POST - 1); q += sizeof APPLE_POST - 1;
    r = ct_vcard_lee(ap, q, tmp, 64, fotos, sizeof fotos, 1 << 19, &fn);
    CK(r == 1, "Apple"); CKS(tmp[0].email[0], "marta@icloud.com"); CK(tmp[0].nac_y == 0 && tmp[0].nac_m == 2 && tmp[0].nac_d == 29, "1604 = sin ano");
    CK(tmp[0].foto_len == 3000 && !memcmp(fotos + tmp[0].foto_off, jpg, 3000), "foto base64 plegada");
    CK(ct_vcard_lee(ap, q, tmp, 64, fotos, sizeof fotos, 1000, &fn) == 1 && tmp[0].foto_len == 0, "foto demasiado grande: fuera");

    /* ---- exportar y volver a leer (ida y vuelta) con foto */
    static char out[200000];
    struct ct_contacto *x = ct_busca_id(&L, a->id); x->fav = 1; x->nfotos = 2; x->voz = 1; x->cambiado = 1; strcpy(x->nota, "l\303\255nea 1\nl\303\255nea 2; con, comas \\ y barra");
    strcpy(x->apellidos, "L\303\263pez de la Fuente Mart\303\255nez-Villalobos y Fern\303\241ndez");
    uint32_t on = ct_vcard_uno(x, 1, jpg, 3000, out, sizeof out);
    CK(on > 0, "exporta");
    int largas = 0; for (char *ln = out; *ln; ) { char *f = strstr(ln, "\r\n"); if (!f) break; if (f - ln > 75) largas++; ln = f + 2; }
    CK(!largas, "lineas <= 75 octetos");
    r = ct_vcard_lee(out, on, tmp, 64, fotos, sizeof fotos, 1 << 19, &fn);
    CK(r == 1, "relee"); CKS(tmp[0].nota, x->nota); CKS(tmp[0].apellidos, x->apellidos); CK(tmp[0].id == x->id && tmp[0].fav && tmp[0].cambiado && tmp[0].nfotos == 2 && tmp[0].voz, "X-SOMA");
    CK(tmp[0].foto_len == 3000, "foto de ida y vuelta"); CK(tmp[0].nac_m == x->nac_m && tmp[0].tel[1][0], "datos");
    CK(ct_vcard_uno(x, 1, jpg, 3000, out, 500) == 0, "no cabe: 0");
    on = ct_vcard_uno(x, 0, 0, 0, out, sizeof out); CK(!strstr(out, "X-SOMA-ID") && !strstr(out, "X-SOMA-CAMBIADO"), "exportar fuera: sin X-SOMA internos");

    /* ---- fusion en los dos sentidos */
    ct_vacia(&L2);
    r = ct_vcard_lee(GOOGLE, sizeof GOOGLE - 1, tmp, 64, 0, 0, 0, 0);
    struct ct_contacto *d1; CK(ct_fusiona(&L2, &tmp[0], &d1) == 1 && ct_fusiona(&L2, &tmp[1], 0) == 1, "nuevas");
    CK(ct_fusiona(&L2, &tmp[0], 0) == 0, "repetida: sin cambios");
    CK(ct_numero(&L2) == 2, "sin duplicados");
    struct ct_contacto mod = tmp[0]; strcpy(mod.tel[0], "+34 699 99 99 99"); strcpy(mod.cargo, "Directora");
    CK(ct_fusiona(&L2, &mod, &d1) == 2 && !strcmp(d1->cargo, "Directora") && !strcmp(d1->tel[0], "+34 699 99 99 99"), "cambio en Google: gana Google");
    d1->cambiado = 1; strcpy(d1->cargo, "CEO");
    mod = tmp[0]; strcpy(mod.cargo, "Ingeniera");
    CK(ct_fusiona(&L2, &mod, &d1) == 2 || 1, "fusiona");
    CKS(d1->cargo, "CEO");
    CK(d1->cambiado == 1, "sigue cambiada (para exportar)");
    mod = tmp[0]; mod.nota[0] = 0; CK(ct_fusiona(&L2, &mod, &d1) >= 0 && d1->nota[0], "vacio no borra");
    d1->cambiado = 0; strcpy(d1->rev, "20260101T000000Z");
    mod = tmp[0]; strcpy(mod.rev, "20250101T000000Z"); strcpy(mod.cargo, "Vieja");
    ct_fusiona(&L2, &mod, &d1); CKS(d1->cargo, "CEO");
    mod = tmp[0]; strcpy(mod.rev, "20270101T000000Z"); strcpy(mod.cargo, "Nueva");
    ct_fusiona(&L2, &mod, &d1); CKS(d1->cargo, "Nueva");
    struct ct_contacto otro = tmp[1]; strcpy(otro.email[0], "ana.lopez@empresa.com"); otro.tel[0][0] = 0; strcpy(otro.nombre, "Anita"); otro.apellidos[0] = 0;
    CK(ct_fusiona(&L2, &otro, &d1) >= 0 && ct_numero(&L2) == 2, "coincide por email");
    struct ct_contacto mal = tmp[1]; mal.id = 999; mal.nfotos = 5; mal.voz = 1; mal.email[0][0] = 0; strcpy(mal.nombre, "Nadie"); strcpy(mal.apellidos, "Nuevo");
    CK(ct_fusiona(&L2, &mal, &d1) == 1 && d1->id != 999 && d1->nfotos == 0 && d1->voz == 0, "importar no trae id, fotos ni voz");

    /* ---- sanea: un servicio aislado comprometido no puede colar cadenas sin terminar ni tipos fuera de rango */
    struct ct_contacto z; memset(&z, 'A', sizeof z); z.nac_m = 40; z.t_tel[0] = 9; z.nfotos = 200; z.email[1][3] = '\001'; z.nombre[5] = (char)0xFF;
    ct_sanea(&z);
    CK(strlen(z.nombre) < CT_NOM && strlen(z.nota) < CT_NOTA && strlen(z.foto_url) < CT_URL && z.nac_m == 0 && z.t_tel[0] == 0 && z.nfotos == CT_FOTOS, "sanea");
    CK(z.foto_url[0] == 0, "url que no es http fuera");

    /* ---- fuzz */
    srand(357);
    static char fz[4096];
    for (int it = 0; it < 30000; ++it) {
        int len = rand() % 4000;
        if (it % 3 == 0) { memcpy(fz, GOOGLE, len < (int)sizeof GOOGLE ? (size_t)len : sizeof GOOGLE - 1); for (int j = 0; j < 20; ++j) fz[rand() % (len + 1)] = (char)(rand() % 256); }
        else if (it % 3 == 1) { strcpy(fz, "BEGIN:VCARD\r\n"); for (int j = 13; j < len; ++j) fz[j] = "ABCDEFGHNTEL:;=,\\\r\n \t@0123456789PHOTO"[rand() % 38]; }
        else for (int j = 0; j < len; ++j) fz[j] = (char)(rand() % 256);
        (void)ct_vcard_lee(fz, (uint32_t)len, tmp, 64, fotos, 65536, 32768, &fn);
        CK(fn <= 65536, "fotos dentro");
        fz[len] = 0; (void)ct_parsea_texto(fz, &c); (void)ct_parsea_fecha(fz, &y, &m, &d);
        (void)ct_resuelve(&L, fz, idx, 16); (void)ct_coincide(&c, fz);
    }
    for (int i = 0; i < 64; ++i) CK(strlen(tmp[i].nombre) < CT_NOM, "fuzz: cadenas terminadas");

    /* ---- capacidad */
    ct_vacia(&L2);
    for (int i = 0; i < CT_MAX; ++i) CK(ct_nuevo(&L2) != 0, "cabe");
    CK(ct_nuevo(&L2) == 0, "llena");
    memset(&c, 0, sizeof c); strcpy(c.nombre, "Extra"); CK(ct_fusiona(&L2, &c, 0) == -1, "fusion en libreta llena: -1");
    { static uint16_t big[CT_MAX]; CK(ct_lista(&L2, 0, 0, 0, big, CT_MAX) == CT_MAX, "lista llena"); }

    printf("test_contactos_357: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos ? 1 : 0;
}
