/* SOMA_295_CORREO: pruebas en el anfitrion de la logica pura del correo (src/apps/correo_core.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "correo_core.h"
#include "agent_ruta.h"

void *co_alloc(uint32_t n) { return malloc(n ? n : 1); }
void co_free(void *p) { free(p); }

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }
static int eq(const char *a, const char *b) { return !strcmp(a, b); }
static int tiene(const char *s, const char *p) { return strstr(s, p) != 0; }
static int64_t utc(int y, int mo, int d, int h, int mi)          /* minutos UTC, para comprobar */
{
    int64_t yy = y - (mo <= 2); int64_t era = (yy >= 0 ? yy : yy - 399) / 400; int64_t yoe = yy - era * 400;
    int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 1440 + h * 60 + mi;
}

static void codificaciones(void)
{
    char b[4096]; uint8_t u[4096];
    const char *t = "Hola, \303\201lvaro: el caf\303\251 cuesta 3 \342\202\254.";
    uint32_t n = co_b64_enc((const uint8_t *)t, (uint32_t)strlen(t), b, sizeof b);
    uint32_t m = co_b64_dec(b, n, u, sizeof u); u[m] = 0;
    chk(m == strlen(t) && eq((char *)u, t), "base64 ida y vuelta");
    m = co_b64_dec("SG9s\r\nYQ==", 10, u, sizeof u); u[m] = 0; chk(eq((char *)u, "Hola"), "base64 con saltos de linea");
    chk(co_b64_enc((const uint8_t *)"abcdef", 6, b, 4) == 0, "base64 respeta el tope");
    m = co_qp_dec("Caf=E9 con le=\r\nche =3D bien", 28, u, sizeof u, 0); u[m] = 0; chk(eq((char *)u, "Caf\351 con leche = bien"), "quoted-printable: =XX y salto suave");
    m = co_qp_dec("Hola_mundo", 10, u, sizeof u, 1); u[m] = 0; chk(eq((char *)u, "Hola mundo"), "quoted-printable de cabecera: _ = espacio");
    /* QP: lineas largas con tildes, ida y vuelta, y ninguna linea de mas de 76 */
    char largo[2000] = ""; for (int i = 0; i < 30; i++) strcat(largo, "Programaci\303\263n en espa\303\261ol = bien. ");
    strcat(largo, "\nfin con espacio \n.punto");
    char q[8000]; uint32_t qn = co_qp_enc(largo, (uint32_t)strlen(largo), q, sizeof q);
    int maxl = 0, cur = 0; for (uint32_t i = 0; i < qn; i++) { if (q[i] == '\n') { if (cur > maxl) maxl = cur; cur = 0; } else if (q[i] != '\r') cur++; }
    chk(qn > 0 && maxl <= 76, "quoted-printable: lineas de 76 como mucho");
    chk(tiene(q, "espacio=20\r\n"), "quoted-printable: espacio al final de linea codificado");
    m = co_qp_dec(q, qn, u, sizeof u, 0); u[m] = 0;
    { char esp[2100]; uint32_t k = 0; for (const char *p = largo; *p; p++) { if (*p == '\n') esp[k++] = '\r'; esp[k++] = *p; } esp[k] = 0; chk(eq((char *)u, esp), "quoted-printable ida y vuelta (con CRLF)"); }
    /* juegos de caracteres */
    co_a_utf8((const uint8_t *)"Espa\361a", 6, "iso-8859-1", b, sizeof b); chk(eq(b, "Espa\303\261a"), "Latin-1 a UTF-8");
    co_a_utf8((const uint8_t *)"\200 \223hola\224", 8, "windows-1252", b, sizeof b); chk(eq(b, "\342\202\254 \342\200\234hola\342\200\235"), "Windows-1252: euro y comillas");
    co_a_utf8((const uint8_t *)"\244", 1, "ISO-8859-15", b, sizeof b); chk(eq(b, "\342\202\254"), "Latin-9: euro");
    co_a_utf8((const uint8_t *)"caf\351", 4, "utf-8", b, sizeof b); chk(eq(b, "caf\303\251"), "dice UTF-8 pero no lo es: se lee como Windows-1252");
    co_a_utf8((const uint8_t *)"\346\227\245\346\234\254", 6, "utf-8", b, sizeof b); chk(eq(b, "\346\227\245\346\234\254"), "UTF-8 de verdad: tal cual");
    co_a_utf8((const uint8_t *)"\346\227\245", 3, "gb2312", b, sizeof b); chk(eq(b, "\346\227\245"), "juego desconocido que ya es UTF-8: tal cual");
    /* cabeceras */
    const char *h1 = "=?UTF-8?B?w4FsdmFybyBHYXJjw61h?= <alvaro@x.es>";
    co_cab_dec(h1, (uint32_t)strlen(h1), b, sizeof b); chk(eq(b, "\303\201lvaro Garc\303\255a <alvaro@x.es>"), "palabra codificada B");
    const char *h2 = "=?iso-8859-1?Q?Caf=E9_con?=\r\n =?iso-8859-1?Q?_leche?= y m\303\241s";
    co_cab_dec(h2, (uint32_t)strlen(h2), b, sizeof b); chk(eq(b, "Caf\303\251 con leche y m\303\241s"), "palabras Q seguidas se juntan; lineas desplegadas; 8 bits tal cual");
    co_cab_dec("Re: =?utf-8?q?Reuni=C3=B3n?= ma\361ana", 35, b, sizeof b); chk(eq(b, "Re: Reuni\303\263n ma\303\261ana"), "mezcla de texto normal, codificado y Latin-1 crudo");
    co_cab_dec("=?UTF-8?B?bWFs", 14, b, sizeof b); chk(eq(b, "=?UTF-8?B?bWFs"), "palabra codificada rota: se deja tal cual");
    char enc[1000]; const char *largo_as = "Presupuesto de la reforma de la cocina y el ba\303\261o: \303\272ltima versi\303\263n revisada por el arquitecto";
    co_cab_enc(largo_as, enc, sizeof enc);
    { int ml = 0, c2 = 0; for (const char *p = enc; *p; p++) { if (*p == '\n') { if (c2 > ml) ml = c2; c2 = 0; } else if (*p != '\r') c2++; } if (c2 > ml) ml = c2;
      chk(ml <= 76 && tiene(enc, "=?UTF-8?B?"), "asunto largo con tildes: palabras codificadas de 75 como mucho"); }
    co_cab_dec(enc, (uint32_t)strlen(enc), b, sizeof b); chk(eq(b, largo_as), "asunto codificado ida y vuelta");
    co_cab_enc("Hola", enc, sizeof enc); chk(eq(enc, "Hola"), "ASCII: sin codificar");
    /* UTF-7 de IMAP */
    co_utf7_dec("Elementos &AOk-nviados &- otros", b, sizeof b); chk(eq(b, "Elementos \303\251nviados & otros"), "UTF-7 modificado a UTF-8");
    co_utf7_dec("&ZeVnLIqe-", b, sizeof b); chk(eq(b, "\346\227\245\346\234\254\350\252\236"), "UTF-7 con caracteres de 3 bytes");
    co_utf7_enc("Papelera & m\303\241s \346\227\245\346\234\254", b, sizeof b); { char d[200]; co_utf7_dec(b, d, sizeof d); chk(eq(d, "Papelera & m\303\241s \346\227\245\346\234\254") && !strchr(b, 0xC3), "UTF-7 ida y vuelta"); }
    /* HTML */
    const char *html = "<html><head><style>p{color:red}</style><title>t</title></head><body><p>Hola&nbsp;<b>Ana</b>,</p>"
                       "<p>Te env&iacute;o el <a href=\"http://x\">presupuesto</a>.<br>Un saludo<br><br>Pepe</p><ul><li>uno</li><li>dos &amp; tres</li></ul>"
                       "<!-- comentario --><script>alert(1)</script>&#8364; &#x41;</body></html>";
    co_html_texto(html, (uint32_t)strlen(html), b, sizeof b);
    chk(tiene(b, "Hola Ana,\n\nTe env\303\255o el presupuesto.\nUn saludo\n\nPepe"), "HTML: parrafos, saltos y entidades");
    chk(tiene(b, "\342\200\242 uno\n\342\200\242 dos & tres") && tiene(b, "\342\202\254 A"), "HTML: listas y entidades numericas");
    chk(!tiene(b, "color") && !tiene(b, "alert") && !tiene(b, "comentario") && !tiene(b, "<"), "HTML: sin estilos, guiones ni etiquetas");
    /* normalizar */
    co_norm("\302\277Qu\303\251 TAL, \303\221and\303\272?", b, sizeof b); chk(eq(b, "que tal nandu"), "normalizar: minusculas, sin tildes ni signos");
}

static void cabeceras_y_fechas(void)
{
    const char *msg = "Return-Path: <x@y>\r\nSubject: Una cabecera\r\n\tplegada\r\nContent-Type: multipart/mixed;\r\n boundary=\"=_frontera\"; charset=utf-8\r\n"
                      "Content-Disposition: attachment; filename*=UTF-8''presupuesto%20ba%C3%B1o.pdf\r\n\r\nSubject: esto es el cuerpo\r\n";
    char v[300];
    chk(co_cabecera(msg, (uint32_t)strlen(msg), "subject", v, sizeof v) > 0 && eq(v, "Una cabecera\tplegada"), "cabecera plegada (sin mirar el cuerpo)");
    chk(co_cabecera(msg, (uint32_t)strlen(msg), "X-No", v, sizeof v) == -1, "cabecera que no esta");
    co_cabecera(msg, (uint32_t)strlen(msg), "Content-Type", v, sizeof v);
    char p[200]; chk(co_param(v, "boundary", p, sizeof p) > 0 && eq(p, "=_frontera"), "parametro entre comillas");
    chk(co_param(v, "charset", p, sizeof p) > 0 && eq(p, "utf-8"), "parametro sin comillas");
    co_cabecera(msg, (uint32_t)strlen(msg), "Content-Disposition", v, sizeof v);
    chk(co_param(v, "filename", p, sizeof p) > 0 && eq(p, "presupuesto ba\303\261o.pdf"), "nombre de archivo RFC 2231");
    chk(co_param("attachment; filename*0=\"parte\"; filename*1=\"uno.txt\"", "filename", p, sizeof p) > 0 && eq(p, "parteuno.txt"), "RFC 2231 en trozos");
    chk(co_param("text/plain; name=\"=?UTF-8?B?w7FhbmR1LnR4dA==?=\"", "name", p, sizeof p) > 0 && eq(p, "\303\261andu.txt"), "nombre con palabra codificada");
    struct co_dir d[8];
    int n = co_dirs("\"Garc\303\255a, Ana\" <ana@x.com>, Juan P\303\251rez <juan@y.es>, pepe@z.org (Pepe Z), =?UTF-8?B?w4FsdmFybw==?= <al@v.es>, nada, grupo: a@b.cc, c@d.ee;", d, 8);
    chk(n == 6, "seis direcciones (la que no es correo no cuenta)");
    chk(eq(d[0].nombre, "Garc\303\255a, Ana") && eq(d[0].email, "ana@x.com"), "nombre entre comillas con coma");
    chk(eq(d[1].nombre, "Juan P\303\251rez") && eq(d[2].email, "pepe@z.org") && eq(d[2].nombre, "Pepe Z"), "nombre suelto y comentario");
    chk(eq(d[3].nombre, "\303\201lvaro") && eq(d[4].email, "a@b.cc") && eq(d[5].email, "c@d.ee"), "nombre codificado y grupo");
    char t[200]; co_dir_txt(&d[1], t, sizeof t); chk(eq(t, "Juan P\303\251rez <juan@y.es>"), "direccion en texto");
    chk(co_email_valido("a.b@c.es") && !co_email_valido("a@b") && !co_email_valido("a b@c.es") && !co_email_valido("@c.es"), "correo valido");
    /* fechas */
    chk(co_fecha("Tue, 24 Nov 2026 10:53:00 +0100") == utc(2026, 11, 24, 9, 53), "fecha RFC 5322");
    chk(co_fecha("24 Nov 2026 10:53 -0500 (EST)") == utc(2026, 11, 24, 15, 53), "sin dia de la semana, sin segundos, zona negativa");
    chk(co_fecha("24-Nov-2026 10:53:00 +0100") == utc(2026, 11, 24, 9, 53), "INTERNALDATE de IMAP");
    chk(co_fecha("Mon, 3 Feb 97 08:00:00 GMT") == utc(1997, 2, 3, 8, 0), "anio de dos cifras");
    chk(co_fecha("no es una fecha") == 0 && co_fecha("") == 0, "fecha mala: 0");
    chk(co_desfase(utc(2026, 7, 1, 12, 0)) == 120 && co_desfase(utc(2026, 1, 15, 12, 0)) == 60, "horario de verano y de invierno");
    chk(co_desfase(utc(2026, 3, 29, 0, 59)) == 60 && co_desfase(utc(2026, 3, 29, 1, 0)) == 120, "cambio de marzo (29 a la 1:00 UTC)");
    chk(co_desfase(utc(2026, 10, 25, 0, 59)) == 120 && co_desfase(utc(2026, 10, 25, 1, 0)) == 60, "cambio de octubre (25 a la 1:00 UTC)");
    char f[64]; co_fecha_rfc(utc(2026, 11, 24, 9, 53), 60, f, sizeof f); chk(eq(f, "Tue, 24 Nov 2026 10:53:00 +0100"), "fecha para el Date:");
    co_fecha_rfc(utc(2026, 7, 1, 22, 30), 120, f, sizeof f); chk(eq(f, "Thu, 2 Jul 2026 00:30:00 +0200"), "fecha que cambia de dia");
    chk(co_fecha(f) == utc(2026, 7, 1, 22, 30), "y se vuelve a leer igual");
}

static const char MIX[] =
    "From: =?UTF-8?Q?Mar=C3=ADa_L=C3=B3pez?= <maria@ejemplo.es>\r\n"
    "To: Pepe <pepe@soma.es>, ana@ejemplo.es\r\n"
    "Subject: =?UTF-8?Q?Presupuesto_reforma_ba=C3=B1o?=\r\n"
    "Date: Mon, 23 Nov 2026 18:05:00 +0100\r\n"
    "Message-ID: <abc123@ejemplo.es>\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: multipart/mixed; boundary=\"MIX\"\r\n\r\n"
    "Esto es un mensaje MIME.\r\n"
    "--MIX\r\n"
    "Content-Type: multipart/alternative; boundary=\"ALT\"\r\n\r\n"
    "--ALT\r\n"
    "Content-Type: text/plain; charset=iso-8859-1\r\n"
    "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
    "Hola Pepe,\r\n\r\nTe adjunto el presupuesto del ba=F1o: 4.300 =80 con IVA.\r\n"
    "\r\n> lo que me preguntaste\r\n--\r\nMar=EDa\r\n"
    "--ALT\r\n"
    "Content-Type: text/html; charset=utf-8\r\n\r\n"
    "<p>Hola Pepe,</p><p>Te adjunto el presupuesto</p>\r\n"
    "--ALT--\r\n"
    "--MIX\r\n"
    "Content-Type: application/pdf; name=\"presupuesto.pdf\"\r\n"
    "Content-Disposition: attachment; filename=\"presupuesto.pdf\"\r\n"
    "Content-Transfer-Encoding: base64\r\n\r\n"
    "JVBERi0xLjQKJcOkw7zDtsOfCjIgMCBvYmoKPDwvTGVuZ3RoIDMgMCBSPj4Kc3RyZWFtCg==\r\n"
    "--MIX\r\n"
    "Content-Type: image/png\r\n"
    "Content-Disposition: inline\r\n"
    "Content-ID: <logo>\r\n"
    "Content-Transfer-Encoding: base64\r\n\r\n"
    "iVBORw0KGgo=\r\n"
    "--MIX--\r\n";

static void mime(void)
{
    static struct co_mime m; static char tx[20000];
    chk(co_mime_lee(MIX, sizeof MIX - 1, &m, tx, sizeof tx), "mensaje mixto");
    chk(eq(m.asunto, "Presupuesto reforma ba\303\261o") && tiene(m.de, "maria@ejemplo.es") && tiene(m.para, "ana@ejemplo.es"), "cabeceras");
    chk(m.fecha == utc(2026, 11, 23, 17, 5) && eq(m.msgid, "<abc123@ejemplo.es>"), "fecha y Message-ID");
    chk(!m.html && tiene(tx, "Te adjunto el presupuesto del ba\303\261o: 4.300 \342\202\254 con IVA."), "texto de la parte text/plain (Latin-1 y QP)");
    chk(!tiene(tx, "\r") && !tiene(tx, "<p>"), "sin CR ni HTML");
    chk(m.nadj == 2 && eq(m.adj[0].nombre, "presupuesto.pdf") && eq(m.adj[0].tipo, "application/pdf"), "adjunto PDF");
    chk(m.adj[1].en_linea && eq(m.adj[1].nombre, "imagen.png"), "imagen en linea (con nombre inventado)");
    uint8_t pdf[200]; uint32_t pn = co_adj_saca(MIX, sizeof MIX - 1, &m.adj[0], pdf, sizeof pdf);
    chk(pn == 52 && !memcmp(pdf, "%PDF-1.4", 8) && m.adj[0].tam >= 52 && m.adj[0].tam <= 54, "el adjunto se saca entero (y su tamano se estima bien)");
    char ex[200]; co_extracto(tx, m.texto_n, ex, sizeof ex);
    chk(eq(ex, "Hola Pepe, Te adjunto el presupuesto del ba\303\261o: 4.300 \342\202\254 con IVA."), "extracto sin cita ni firma");
    /* solo HTML */
    const char *h = "Subject: Oferta\r\nContent-Type: text/html; charset=\"windows-1252\"\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n<div>S=F3lo hoy: <b>50%</b> =80</div>\r\n";
    co_mime_lee(h, (uint32_t)strlen(h), &m, tx, sizeof tx);
    chk(m.html && eq(tx, "S\303\263lo hoy: 50% \342\202\254"), "solo HTML: se convierte a texto");
    /* texto de relleno + HTML de verdad: gana el HTML */
    const char *st = "Content-Type: multipart/alternative; boundary=b\r\n\r\n--b\r\nContent-Type: text/plain\r\n\r\nTu programa no muestra HTML.\r\n--b\r\nContent-Type: text/html\r\n\r\n"
                     "<p>Tu pedido 112-4456 ha salido del almac&eacute;n y llegar&aacute; el jueves. Puedes seguirlo desde la web en cualquier momento con el n&uacute;mero de seguimiento.</p>"
                     "<p>Gracias por comprar con nosotros. Si tienes cualquier duda, responde a este correo y te ayudamos.</p>\r\n--b--\r\n";
    co_mime_lee(st, (uint32_t)strlen(st), &m, tx, sizeof tx);
    chk(m.html && tiene(tx, "llegar\303\241 el jueves"), "texto de relleno: se usa el HTML");
    /* sin Content-Type y LF solos */
    const char *simple = "From: a@b.es\nSubject: hola\n\nl\303\255nea 1\nl\303\255nea 2\n";
    co_mime_lee(simple, (uint32_t)strlen(simple), &m, tx, sizeof tx);
    chk(eq(tx, "l\303\255nea 1\nl\303\255nea 2") && eq(m.asunto, "hola"), "mensaje sencillo con saltos LF");
    /* cortado a la mitad (se pidio solo el principio) */
    char cortado[700]; memcpy(cortado, MIX, 600); cortado[600] = 0;
    co_mime_lee(cortado, 600, &m, tx, sizeof tx);
    chk(m.truncado && tiene(tx, "Hola Pepe"), "mensaje cortado: se lee lo que hay y se sabe");
    /* bufer de texto pequeno */
    co_mime_lee(MIX, sizeof MIX - 1, &m, tx, 20);
    chk(strlen(tx) < 20, "respeta el tope del texto");
}

static void imap(void)
{
    const char *r = "* 12 FETCH (UID 345 RFC822.SIZE 2048 FLAGS (\\Seen \\Flagged) BODY[HEADER.FIELDS (FROM SUBJECT)] {44}\r\n"
                    "From: Ana <ana@x.es>\r\nSubject: Hola (1)\r\n\r\n)\r\n* 13 FETCH (UID 346)\r\n";
    uint32_t n = (uint32_t)strlen(r);
    uint32_t c = co_imap_completa(r, n);
    chk(c > 0 && r[c - 1] == '\n' && !strncmp(r + c, "* 13", 4), "respuesta completa con literal");
    chk(co_imap_completa(r, 60) == 0, "literal sin llegar entero: aun no");
    struct co_fetch f;
    chk(co_imap_fetch(r, c, &f) && f.seq == 12 && f.uid == 345 && f.tam == 2048 && f.flags == (CO_F_VISTO | CO_F_DESTACADO), "FETCH: UID, tamano y banderas");
    chk(f.cab && f.cab_n == 44 && !strncmp(f.cab, "From: Ana", 9), "FETCH: cabeceras del literal");
    const char *r2 = "* 3 FETCH (FLAGS () UID 9 BODY[] {5}\r\nHola!)\r\n";
    chk(co_imap_fetch(r2, (uint32_t)strlen(r2), &f) && f.uid == 9 && f.flags == 0 && f.tiene_flags && f.cuerpo_n == 5 && !strncmp(f.cuerpo, "Hola!", 5), "FETCH: BODY[] y banderas vacias");
    const char *r3 = "* 4 FETCH (X-GM-LABELS (\"\\\\Important\" trabajo) UID 10 BODY[]<0> \"corto\" INTERNALDATE \"24-Nov-2026 10:00:00 +0100\")\r\n";
    chk(co_imap_fetch(r3, (uint32_t)strlen(r3), &f) && f.uid == 10 && f.cuerpo_n == 5, "FETCH: se saltan los datos que no se usan; cuerpo entre comillas");
    char t[100];
    chk(co_imap_etiqueta("A7 OK [READ-WRITE] SELECT completado\r\n", 38, "A7", t, sizeof t) == 1 && eq(t, "[READ-WRITE] SELECT completado"), "etiqueta OK");
    chk(co_imap_etiqueta("A7 NO [AUTHENTICATIONFAILED] Invalid credentials\r\n", 49, "A7", t, sizeof t) == -1 && tiene(t, "Invalid"), "etiqueta NO con el motivo");
    chk(co_imap_etiqueta("A71 OK x\r\n", 10, "A7", t, sizeof t) == 0, "otra etiqueta no confunde");
    struct co_carpeta k;
    chk(co_imap_list("* LIST (\\HasNoChildren \\Sent) \"/\" \"[Gmail]/Enviados\"\r\n", 54, &k) && k.uso == CO_U_ENVIADOS && eq(k.nombre, "Enviados") && eq(k.imap, "[Gmail]/Enviados"), "LIST con uso especial");
    chk(co_imap_list("* LIST (\\HasNoChildren) \".\" \"INBOX.Elementos &AOk-liminados\"\r\n", 63, &k) && k.uso == CO_U_PAPELERA, "LIST: papelera por el nombre (en UTF-7)");
    chk(co_imap_list("* LIST (\\HasNoChildren) \"/\" \"Facturas/2026 &AOk-t&AOk-\"\r\n", 57, &k) && k.uso == CO_U_NADA && eq(k.nombre, "2026 \303\251t\303\251"), "LIST: carpeta del usuario (ultimo trozo)");
    chk(co_imap_list("* LIST (\\Noselect \\HasChildren) \"/\" \"[Gmail]\"\r\n", 46, &k) && k.noselect, "LIST: no se puede abrir");
    chk(co_imap_list("* LIST () \"/\" INBOX\r\n", 21, &k) && k.uso == CO_U_ENTRADA, "LIST: INBOX sin comillas");
    chk(co_imap_list("* LIST () \"/\" \"Con \\\"comillas\\\"\"\r\n", 34, &k) && eq(k.imap, "Con \"comillas\""), "LIST: comillas escapadas en el nombre");
    uint32_t u[10];
    chk(co_imap_search("* SEARCH 4 8 15 16\r\n", 20, u, 10) == 4 && u[3] == 16, "SEARCH");
    chk(co_imap_search("* SEARCH\r\n", 10, u, 10) == 0, "SEARCH vacio");
    uint32_t v = 0;
    chk(co_imap_num("* 23 EXISTS\r\n", 13, "EXISTS", &v) && v == 23, "EXISTS");
    chk(co_imap_num("* OK [UIDNEXT 4567] Predicted\r\n", 31, "UIDNEXT", &v) && v == 4567, "UIDNEXT");
    chk(!co_imap_num("* OK [UIDNEXTX 1]\r\n", 19, "UIDNEXT", &v), "UIDNEXT no confunde");
    chk(co_imap_cap("* CAPABILITY IMAP4rev1 MOVE UIDPLUS X-GM-EXT-1 AUTH=PLAIN\r\n", 59, "MOVE") && !co_imap_cap("* CAPABILITY IMAP4rev1 MOVEX\r\n", 30, "MOVE"), "capacidades");
    chk(co_imap_cadena("cla\"ve\\x", t, sizeof t) && eq(t, "\"cla\\\"ve\\\\x\""), "cadena con comillas y barras");
    chk(co_imap_cadena("contrase\303\261a", t, sizeof t) == 0 && co_imap_cadena("a\r\nb", t, sizeof t) == 0, "literal para 8 bits o saltos");
}

static void smtp_y_composicion(void)
{
    int c; uint32_t u;
    const char *e = "250-smtp.ejemplo.es saluda\r\n250-SIZE 35882577\r\n250-8BITMIME\r\n250-AUTH LOGIN PLAIN XOAUTH2\r\n250-STARTTLS\r\n250 SMTPUTF8\r\n";
    chk(co_smtp_resp(e, (uint32_t)strlen(e), &c, &u) && c == 250 && u == strlen(e), "respuesta multilinea completa");
    chk(!co_smtp_resp(e, 50, &c, &u), "multilinea a medias: aun no");
    uint32_t caps = co_smtp_caps(e, (uint32_t)strlen(e));
    chk(caps == (CO_S_STARTTLS | CO_S_PLAIN | CO_S_LOGIN | CO_S_8BIT | CO_S_UTF8 | CO_S_XOAUTH), "capacidades de EHLO");
    char b[200]; co_smtp_plain("pepe@soma.es", "cl@ve", b, sizeof b);
    uint8_t d[100]; uint32_t dn = co_b64_dec(b, (uint32_t)strlen(b), d, sizeof d);
    chk(dn == 19 && d[0] == 0 && !memcmp(d + 1, "pepe@soma.es", 12) && d[13] == 0 && !memcmp(d + 14, "cl@ve", 5), "AUTH PLAIN");
    static char out[20000];
    struct co_envio en; memset(&en, 0, sizeof en);
    en.de_nombre = "Jos\303\251 Ram\303\263n"; en.de_email = "jose@soma.es";
    en.para = "Mar\303\255a L\303\263pez <maria@ejemplo.es>, \"Garc\303\255a, Ana\" <ana@x.com>, juan@y.es";
    en.cc = "\"Equipo, Obra\" <obra@x.com>"; en.cco = "oculto@x.com, maria@ejemplo.es";
    en.asunto = "Re: Presupuesto reforma ba\303\261o (versi\303\263n 2)";
    en.cuerpo = "Hola Mar\303\255a:\n\nPerfecto, adelante con el presupuesto.\n.l\303\255nea que empieza por punto\nUn saludo";
    en.en_respuesta_a = "<abc123@ejemplo.es>"; en.referencias = "<prev@x>";
    en.utc_min = utc(2026, 11, 24, 9, 53); en.off_min = 60; en.azar = 0xBEEF;
    uint32_t n = co_compone(&en, out, sizeof out);
    chk(n > 0 && tiene(out, "Date: Tue, 24 Nov 2026 10:53:00 +0100\r\n") && tiene(out, "\r\nMIME-Version: 1.0\r\n"), "mensaje: fecha y MIME");
    chk(tiene(out, "Content-Transfer-Encoding: quoted-printable") && tiene(out, "charset=UTF-8"), "UTF-8 en quoted-printable");
    chk(tiene(out, "Message-ID: <0000beef.") && tiene(out, ".soma@soma.es>") && tiene(en.msgid, "@soma.es>"), "Message-ID");
    chk(tiene(out, "In-Reply-To: <abc123@ejemplo.es>\r\nReferences: <prev@x>\r\n <abc123@ejemplo.es>\r\n"), "respuesta en el hilo");
    chk(tiene(out, "\"Equipo, Obra\" <obra@x.com>") && !tiene(out, "oculto@x.com"), "nombre con coma entre comillas; Cco no aparece");
    for (const char *p = out; *p; p++) if ((unsigned char)*p >= 0x80) { chk(0, "el mensaje es ASCII de 7 bits"); break; }
    /* volver a leerlo */
    static struct co_mime m; static char tx[4000];
    co_mime_lee(out, n, &m, tx, sizeof tx);
    chk(eq(m.asunto, en.asunto), "el asunto se vuelve a leer igual");
    chk(eq(tx, en.cuerpo), "el cuerpo se vuelve a leer igual");
    struct co_dir dd[8]; int nd = co_dirs(m.para, dd, 8);
    chk(nd == 3 && eq(dd[0].nombre, "Mar\303\255a L\303\263pez") && eq(dd[1].nombre, "Garc\303\255a, Ana") && eq(dd[2].email, "juan@y.es"), "los destinatarios se vuelven a leer igual");
    co_dirs(m.de, dd, 1); chk(eq(dd[0].nombre, "Jos\303\251 Ram\303\263n") && eq(dd[0].email, "jose@soma.es"), "el remitente tambien");
    { int ml = 0, cur = 0; for (const char *p = out; *p; p++) { if (*p == '\n') { if (cur > ml) ml = cur; cur = 0; } else if (*p != '\r') cur++; } chk(ml <= 78, "ninguna linea de mas de 78"); }
    nd = co_destinatarios(&en, dd, 8);
    chk(nd == 5, "destinatarios: Para + Cc + Cco sin repetir");
    static char pt[30000]; uint32_t pn = co_puntea(out, n, pt, sizeof pt);
    chk(pn > n && tiene(pt, "\r\n..l=C3=ADnea") && !strcmp(pt + pn - 5, "\r\n.\r\n"), "DATA: punto doblado y final");
    pn = co_puntea("a\nb", 3, pt, sizeof pt); chk(eq(pt, "a\r\nb\r\n.\r\n"), "DATA: saltos LF pasan a CRLF");
    en.cuerpo = "Hola, todo ASCII.\n"; en.asunto = "Hola"; en.en_respuesta_a = 0; en.cc = 0;
    co_compone(&en, out, sizeof out); chk(tiene(out, "Content-Transfer-Encoding: 7bit") && tiene(out, "Subject: Hola\r\n") && !tiene(out, "In-Reply-To"), "ASCII: 7bit, sin hilo");
    chk(co_compone(&en, out, 100) == 0, "mensaje que no cabe: 0");
    co_asunto_resp("RE: Re:  Fwd: hola", 0, b, sizeof b); chk(eq(b, "Re: hola"), "Re: sin repetir");
    co_asunto_resp("RV: hola", 1, b, sizeof b); chk(eq(b, "Fwd: hola"), "Fwd:");
    char ci[600]; co_cita("Ana <ana@x>", 29000000LL * 1 + 600, "uno\n\ndos", 8, 0, "a", "b", ci, sizeof ci);
    chk(tiene(ci, "escribi\303\263:\n> uno\n>\n> dos"), "cita para responder");
    co_cita("Ana <ana@x>", 29000000LL, "texto", 5, 1, "Asunto X", "pepe@x", ci, sizeof ci);
    chk(tiene(ci, "Mensaje reenviado") && tiene(ci, "Asunto: Asunto X") && tiene(ci, "\n\ntexto"), "cita para reenviar");
}

static void guardado_y_busqueda(void)
{
    static struct co_msg v[8], w[8];
    memset(v, 0, sizeof v);
    const char *cabs[6] = {
        "From: Amazon.es <envios@amazon.es>\r\nTo: Pepe <pepe@soma.es>\r\nSubject: Tu pedido de auriculares ha salido\r\nDate: Mon, 23 Nov 2026 10:00:00 +0100\r\n\r\n",
        "From: =?UTF-8?Q?Mar=C3=ADa_L=C3=B3pez?= <maria@ejemplo.es>\r\nTo: pepe@soma.es\r\nSubject: Presupuesto reforma\r\nDate: Tue, 24 Nov 2026 08:00:00 +0100\r\nContent-Type: multipart/mixed; boundary=x\r\n\r\n",
        "From: Iberdrola <facturas@iberdrola.es>\r\nTo: pepe@soma.es\r\nSubject: Factura de la luz de noviembre\r\nDate: Tue, 24 Nov 2026 09:00:00 +0100\r\nContent-Type: multipart/mixed; boundary=x\r\n\r\n",
        "From: Pepe <pepe@soma.es>\r\nTo: Mar\303\255a L\303\263pez <maria@ejemplo.es>, Juan Garc\303\255a <juan@obra.es>\r\nSubject: Re: Presupuesto reforma\r\nDate: Tue, 24 Nov 2026 09:30:00 +0100\r\n\r\n",
        "From: Juan Garc\303\255a <juan@obra.es>\r\nTo: pepe@soma.es\r\nSubject: Fotos de la obra\r\nDate: Wed, 18 Nov 2026 12:00:00 +0100\r\n\r\n",
        "From: Newsletter <no-reply@tienda.es>\r\nTo: pepe@soma.es\r\nSubject: Ofertas del Black Friday\r\nDate: Fri, 20 Nov 2026 07:00:00 +0100\r\n\r\n" };
    for (int i = 0; i < 6; i++) { v[i].uid = 100 + (uint32_t)i; co_msg_de_cab(&v[i], cabs[i], (uint32_t)strlen(cabs[i])); }
    v[0].flags = CO_F_VISTO; v[1].flags = 0; v[2].flags = CO_F_DESTACADO | CO_F_VISTO; v[3].flags = CO_F_VISTO; v[4].flags = CO_F_VISTO; v[5].flags = 0;
    strcpy(v[1].extracto, "Te adjunto el presupuesto del ba\303\261o y la cocina: 4.300 euros");
    strcpy(v[4].extracto, "Van las fotos del avance de la obra"); v[5].cat = CO_CAT_PROMO; v[1].cat = CO_CAT_IMPORTANTE;
    chk(eq(v[1].de, "Mar\303\255a L\303\263pez") && eq(v[1].de_email, "maria@ejemplo.es") && v[1].adj && !v[0].adj, "lista: remitente decodificado y si hay adjuntos");
    chk(eq(v[0].de, "Amazon.es") && eq(v[3].para, "Mar\303\255a L\303\263pez <maria@ejemplo.es>, Juan Garc\303\255a <juan@obra.es>"), "lista: destinatarios");
    chk(v[2].fecha == utc(2026, 11, 24, 8, 0), "lista: fecha");
    static char ser[20000]; uint32_t sn = co_msgs_serializa(v, 6, ser, sizeof ser);
    chk(sn > 0 && co_msgs_carga(ser, sn, w, 8) == 6 && !memcmp(&w[1], &v[1], sizeof w[1]) && !memcmp(&w[5], &v[5], sizeof w[5]), "guardar y cargar la lista");
    chk(co_msgs_serializa(v, 6, ser, 100) == 0, "lista que no cabe: 0");
    struct co_filtro f; int idx[8];
    int64_t ahora = utc(2026, 11, 24, 10, 0) + 60;       /* local */
    co_filtro_texto("de:maria asunto:\"presupuesto reforma\" tiene:adjunto no leidos hoy cocina", ahora, &f);
    chk(eq(f.de, "maria") && eq(f.asunto, "presupuesto reforma") && f.adjuntos && f.no_leidos && f.desde == utc(2026, 11, 23, 23, 0) && eq(f.palabras, "cocina"), "filtro escrito con operadores");
    chk(co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 1, "encuentra el de Maria");
    co_filtro_texto("facturas", ahora, &f); chk(co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 2, "plural: facturas -> factura");
    co_filtro_texto("el correo de amazon", ahora, &f); chk(eq(f.palabras, "amazon") && co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 0, "palabras vacias fuera");
    co_filtro_texto("presupuesto", ahora, &f); { int k = co_busca(v, 6, &f, idx, 8); chk(k == 2 && idx[0] == 1 && idx[1] == 3, "por puntos (asunto y extracto antes que solo asunto)"); }
    co_filtro_texto("reforma", ahora, &f); { int k = co_busca(v, 6, &f, idx, 8); chk(k == 2 && idx[0] == 3 && idx[1] == 1, "a igualdad de puntos, lo mas nuevo"); }
    co_filtro_texto("obra", ahora, &f); chk(co_busca(v, 6, &f, idx, 8) == 2, "tambien en el extracto y en los destinatarios");
    co_filtro_texto("esta semana destacados", ahora, &f); chk(co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 2, "esta semana y destacados");
    co_filtro_texto("ayer", ahora, &f); chk(co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 0, "ayer");
    co_filtro_texto("importantes", ahora, &f); chk(f.cat == CO_CAT_IMPORTANTE && co_busca(v, 6, &f, idx, 8) == 1, "los importantes (de la IA)");
    co_filtro_texto("zzz", ahora, &f); chk(co_busca(v, 6, &f, idx, 8) == 0, "nada");
    chk(co_filtro_json("Claro: {\"de\": \"Mar\\u00eda\", \"palabras\": \"presupuesto de la cocina\", \"desde\": \"2026-11-20\", \"hasta\":\"2026-11-24\", \"adjuntos\": true, \"no_leidos\": false}", &f)
        && eq(f.de, "maria") && eq(f.palabras, "presupuesto cocina") && f.adjuntos && !f.no_leidos && f.desde == utc(2026, 11, 19, 23, 0) && f.hasta == utc(2026, 11, 24, 23, 0), "filtro del modelo (JSON)");
    chk(co_busca(v, 6, &f, idx, 8) == 1 && idx[0] == 1, "y encuentra lo que toca");
    char im[400]; co_filtro_texto("de:maria no leidos presupuesto cocina", ahora, &f); co_filtro_imap(&f, im, sizeof im);
    chk(eq(im, "UNSEEN FROM \"maria\" TEXT \"presupuesto\" TEXT \"cocina\""), "criterios para el servidor");
    co_filtro_texto("hoy", ahora, &f); co_filtro_imap(&f, im, sizeof im); chk(eq(im, "SINCE 24-Nov-2026"), "SINCE con la fecha local");
    memset(&f, 0, sizeof f); co_filtro_imap(&f, im, sizeof im); chk(eq(im, "ALL") && co_filtro_vacio(&f), "sin filtro: ALL");
    /* contactos */
    struct co_dir c[10]; int ve[10];
    int nc = co_contactos(v, 6, "pepe@soma.es", c, ve, 10);
    chk(nc == 4 && eq(c[0].email, "maria@ejemplo.es") && ve[0] == 2, "contactos: Maria la primera (2 veces); sin el propio ni no-reply");
    struct co_dir r;
    chk(co_resuelve(v, 6, "pepe@soma.es", "Mar\303\255a", &r) && eq(r.email, "maria@ejemplo.es"), "resolver 'Maria'");
    chk(co_resuelve(v, 6, "pepe@soma.es", "juan garcia", &r) && eq(r.email, "juan@obra.es"), "resolver 'juan garcia' (sin tilde)");
    chk(co_resuelve(v, 6, "pepe@soma.es", "facturas", &r) && eq(r.email, "facturas@iberdrola.es"), "resolver por el principio del correo");
    chk(!co_resuelve(v, 6, "pepe@soma.es", "Roberto", &r), "nadie con ese nombre");
    chk(co_resuelve(v, 6, "pepe@soma.es", "nuevo@dominio.com", &r) && eq(r.email, "nuevo@dominio.com"), "una direccion escrita: tal cual");
}

static void modelo_y_cuentas(void)
{
    char pa[200], as[200], cu[2000];
    chk(co_ia_borrador("<think>pienso</think>\n**Para:** Mar\303\255a L\303\263pez <maria@ejemplo.es>\n**Asunto:** Llego tarde\n**Cuerpo:**\nHola Mar\303\255a:\n\nLlego 10 minutos tarde.\n\nUn saludo\n---",
                       pa, sizeof pa, as, sizeof as, cu, sizeof cu)
        && eq(pa, "Mar\303\255a L\303\263pez <maria@ejemplo.es>") && eq(as, "Llego tarde") && eq(cu, "Hola Mar\303\255a:\n\nLlego 10 minutos tarde.\n\nUn saludo"), "borrador con marcas en negrita");
    chk(co_ia_borrador("ASUNTO: Reuni\303\263n\nHola equipo,\nnos vemos a las 5.", pa, sizeof pa, as, sizeof as, cu, sizeof cu) && !pa[0] && eq(as, "Reuni\303\263n") && eq(cu, "Hola equipo,\nnos vemos a las 5."), "sin Cuerpo: lo que sigue al asunto");
    chk(co_ia_borrador("```\nGracias por todo.\n```", pa, sizeof pa, as, sizeof as, cu, sizeof cu) && eq(cu, "Gracias por todo.") && !as[0], "solo el cuerpo (sin bloques de codigo)");
    chk(co_ia_borrador("Subject: Hi\nBody: Thanks a lot", pa, sizeof pa, as, sizeof as, cu, sizeof cu) && eq(as, "Hi") && eq(cu, "Thanks a lot"), "en ingles y el cuerpo en la misma linea");
    chk(!co_ia_borrador("<think>solo pienso", pa, sizeof pa, as, sizeof as, cu, sizeof cu), "solo pensamiento: nada");
    uint32_t ids[8]; uint8_t cats[8];
    chk(co_ia_ids("Los mas relevantes son... IDS: 12, 7, 12, 3\nporque...", ids, 8) == 3 && ids[0] == 12 && ids[2] == 3, "ids (sin repetir; hasta el salto)");
    chk(co_ia_ids("IDS: ninguno", ids, 8) == 0, "ids: ninguno");
    chk(co_ia_cats("12: importante\n- 13: Promoci\303\263n\n14 = notificaci\303\263n autom\303\241tica\n15: trabajo\n16: algo raro\n", ids, cats, 8) == 4 && cats[0] == CO_CAT_IMPORTANTE && cats[1] == CO_CAT_PROMO && cats[2] == CO_CAT_NOTIF && ids[3] == 15, "categorias");
    char l[100]; co_ia_limpia("  \n<think>x</think>\n\nHola\n\n", l, sizeof l); chk(eq(l, "Hola"), "limpiar la respuesta");
    struct co_cuenta c, d; const char *av;
    memset(&c, 0, sizeof c); strcpy(c.email, "pepe@gmail.com");
    chk(co_cuenta_auto(&c, &av) && eq(c.imap, "imap.gmail.com") && c.imap_p == 993 && c.smtp_p == 465 && c.gmail && tiene(av, "aplicaci\303\263n") && eq(c.usuario, "pepe@gmail.com"), "Gmail");
    memset(&c, 0, sizeof c); strcpy(c.email, "pepe@icloud.com");
    chk(co_cuenta_auto(&c, &av) && eq(c.smtp, "smtp.mail.me.com") && c.smtp_p == 587 && c.smtp_tls == CO_TLS_STARTTLS, "iCloud: SMTP con STARTTLS");
    memset(&c, 0, sizeof c); strcpy(c.email, "yo@miempresa.es");
    chk(!co_cuenta_auto(&c, &av) && eq(c.imap, "imap.miempresa.es") && eq(c.smtp, "smtp.miempresa.es") && !av, "dominio propio: imap. y smtp.");
    strcpy(c.clave, "cl@ve con\nsalto y = raro"); strcpy(c.nombre, "Jos\303\251");
    char t[1000]; co_cuenta_serializa(&c, t, sizeof t);
    chk(!tiene(t, "cl@ve") && co_cuenta_carga(&d, t, (uint32_t)strlen(t)) && !memcmp(&c, &d, sizeof c), "cuenta: guardar y cargar (la clave no va en claro)");
    chk(!co_cuenta_carga(&d, "nada=1\n", 7), "cuenta vacia");
}

static void rutas(void)
{
    static const struct { const char *m; int r; } C[] = {
        { "escribe un correo a Mar\303\255a dici\303\251ndole que llego tarde", AR_CO_REDACTA },
        { "M\303\241ndale un email a Juan para quedar el martes", AR_CO_REDACTA },
        { "redacta un mail formal para mi jefa pidiendo vacaciones", AR_CO_REDACTA },
        { "responde al correo de Ana dici\303\251ndole que s\303\255", AR_CO_REDACTA },
        { "env\303\255alo", AR_CO_ENVIA }, { "S\303\255, env\303\255alo", AR_CO_ENVIA }, { "vale, m\303\241ndalo ya", AR_CO_ENVIA }, { "env\303\255a el correo", AR_CO_ENVIA },
        { "\302\277Tengo correos nuevos?", AR_CO_LEE }, { "resume mis correos de hoy", AR_CO_LEE }, { "l\303\251eme el correo", AR_CO_LEE },
        { "\302\277me ha llegado alg\303\272n email de Amazon?", AR_CO_LEE }, { "revisa mi bandeja de entrada", AR_CO_LEE },
        { "busca el correo de Amazon del pedido", AR_CO_BUSCA }, { "encu\303\251ntrame el email donde me mandaron el contrato", AR_CO_BUSCA },
        { "abre el correo", AR_CO_ABRE }, { "abre mi email", AR_CO_ABRE },
        /* no son del correo */
        { "\302\277qu\303\251 es un correo electr\303\263nico?", 0 }, { "expl\303\255came c\303\263mo funciona el correo", 0 },
        { "busca en internet c\303\263mo configurar el correo", 0 }, { "haz un programa que mande correos", 0 },
        { "env\303\255a los archivos de la carpeta al servidor y despu\303\251s comprueba que han llegado todos bien", 0 },
        { "dibuja un faro", 0 }, { "abre el navegador", 0 }, { "escribe un poema", 0 }, { "", 0 } };
    int bien = 1;
    for (int i = 0; i < (int)(sizeof C / sizeof C[0]); i++) { int r = ar_correo(C[i].m); if (r != C[i].r) { bien = 0; printf("  ruta: '%s' -> %d (esperaba %d)\n", C[i].m, r, C[i].r); } }
    chk(bien, "el orbe sabe que es del correo (y que no)");
}

int main(void)
{
    rutas();
    codificaciones();
    cabeceras_y_fechas();
    mime();
    imap();
    smtp_y_composicion();
    guardado_y_busqueda();
    modelo_y_cuentas();
    printf("== test_correo_295: %d/%d\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
