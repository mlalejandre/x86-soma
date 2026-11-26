/* SOMA_311_CORREO_AISLADO: el nucleo no se cree lo que devuelve el lector de correo aislado (src/apps/correo_sbx.h).
 * 1) Con estructuras de bytes al azar (lo peor que podria mandar un servicio comprometido), correo_mime_sanea y correo_msg_sanea
 *    dejan siempre cadenas terminadas, contadores en rango y solo adjuntos cuyo trozo cae dentro del mensaje.
 * 2) Con lo que da de verdad co_mime_lee (mensajes buenos), el saneado NO cambia nada.
 * Las mismas comparaciones con el servicio de verdad, en QEMU: sbx_selftest (make sbx-check). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "correo_sbx.h"

void *co_alloc(uint32_t n) { return malloc(n ? n : 1); }
void  co_free(void *p) { free(p); }

static unsigned semilla = 311u;
static unsigned azar(void) { semilla = semilla * 1103515245u + 12345u; return semilla >> 8; }
static int terminada(const char *s, size_t cap) { return memchr(s, 0, cap) != 0; }

static const char *const muestras[] = {
    "From: =?ISO-8859-1?Q?Mar=EDa?= <maria@ejemplo.es>\r\nTo: Ana <ana@ejemplo.es>\r\nSubject: =?UTF-8?B?UHJlc3VwdWVzdG8=?=\r\n"
    "Date: Tue, 24 Nov 2026 10:53:00 +0100\r\nContent-Type: multipart/mixed; boundary=\"XYZ\"\r\n\r\n"
    "--XYZ\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nHola, te env=EDo esto.\r\n"
    "--XYZ\r\nContent-Type: text/plain; name=\"a.txt\"\r\nContent-Disposition: attachment; filename=\"a.txt\"\r\n"
    "Content-Transfer-Encoding: base64\r\n\r\nQ29jaW5hOiA0LjIwMCBFVVIK\r\n--XYZ\r\nContent-Type: image/png; name=\"b.png\"\r\n"
    "Content-Disposition: inline; filename=\"b.png\"\r\nContent-Transfer-Encoding: base64\r\n\r\niVBORw0KGgo=\r\n--XYZ--\r\n",
    "From: Tienda <t@t.com>\r\nSubject: html\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<p>Hola <b>mundo</b> &amp; m\303\241s</p>\r\n",
    "From: yo@ejemplo.es\r\nSubject: nota\r\n\r\nsolo texto\r\n",
};

int main(void)
{
    /* 1) basura */
    for (int it = 0; it < 20000; ++it) {
        struct co_mime m; uint8_t *p = (uint8_t *)&m;
        for (size_t i = 0; i < sizeof m; ++i) p[i] = (uint8_t)azar();
        if (it % 3 == 0) m.nadj = (int)(azar() % 40) - 10;
        uint32_t raw_n = azar() % 5000, tmax = azar() % 3000;
        if (it % 5 == 0) { for (int k = 0; k < CO_ADJ_MAX; ++k) { m.adj[k].off = azar() % 6000; m.adj[k].len = azar() % 6000; } }
        correo_mime_sanea(&m, raw_n, tmax);
        int ok = terminada(m.asunto, sizeof m.asunto) && terminada(m.de, sizeof m.de) && terminada(m.para, sizeof m.para)
              && terminada(m.cc, sizeof m.cc) && terminada(m.responder_a, sizeof m.responder_a) && terminada(m.msgid, sizeof m.msgid)
              && terminada(m.refs, sizeof m.refs) && (m.html == 0 || m.html == 1) && (m.truncado == 0 || m.truncado == 1)
              && m.nadj >= 0 && m.nadj <= CO_ADJ_MAX && (tmax ? m.texto_n <= tmax - 1 : m.texto_n == 0);
        for (int k = 0; ok && k < m.nadj; ++k) {
            const struct co_adj *a = &m.adj[k];
            ok = a->off <= raw_n && a->len <= raw_n - a->off && terminada(a->nombre, sizeof a->nombre) && terminada(a->tipo, sizeof a->tipo)
              && a->cte >= CO_CTE_7BIT && a->cte <= CO_CTE_B64 && (a->en_linea == 0 || a->en_linea == 1) && a->tam <= a->len;
        }
        if (!ok) { CHECK(0 && "co_mime saneada con algo fuera de rango"); break; }
        struct co_msg f; p = (uint8_t *)&f;
        for (size_t i = 0; i < sizeof f; ++i) p[i] = (uint8_t)azar();
        correo_msg_sanea(&f);
        ok = terminada(f.de, sizeof f.de) && terminada(f.de_email, sizeof f.de_email) && terminada(f.para, sizeof f.para)
          && terminada(f.asunto, sizeof f.asunto) && terminada(f.extracto, sizeof f.extracto) && terminada(f.msgid, sizeof f.msgid)
          && f.adj <= 1 && f.cat <= CO_CAT_PROMO && f.prio <= 3;
        if (!ok) { CHECK(0 && "co_msg saneada con algo fuera de rango"); break; }
    }
    CHECK(1);

    /* 2) mensajes buenos: el saneado no cambia nada */
    for (int i = 0; i < 3; ++i) {
        const char *raw = muestras[i]; uint32_t n = (uint32_t)strlen(raw);
        static char tx[8192]; struct co_mime m, c;
        memset(&m, 0, sizeof m);
        co_mime_lee(raw, n, &m, tx, sizeof tx);
        c = m;
        correo_mime_sanea(&c, n, sizeof tx);
        CHECK(memcmp(&m, &c, sizeof m) == 0);
        if (i == 0) { CHECK(m.nadj == 2); CHECK_STR(m.adj[0].nombre, "a.txt"); }
        struct co_msg f; memset(&f, 0, sizeof f); f.uid = 5;
        const char *sep = strstr(raw, "\r\n\r\n");
        co_ficha(&f, raw, (uint32_t)(sep + 4 - raw), sep + 4, (uint32_t)strlen(sep + 4));
        struct co_msg g = f; correo_msg_sanea(&g);
        CHECK(memcmp(&f, &g, sizeof f) == 0);
        CHECK(f.uid == 5);
        if (i == 0) { CHECK(f.adj == 1); CHECK_STR(f.asunto, "Presupuesto"); CHECK(strstr(f.extracto, "Hola") != 0); }
    }
    /* el envoltorio en el anfitrion (WEB_HOST_TEST) llama directo y da lo mismo */
    {
        const char *raw = muestras[0]; uint32_t n = (uint32_t)strlen(raw);
        static char t1[8192], t2[8192]; struct co_mime a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        CHECK_EQ_INT(co_mime_lee(raw, n, &a, t1, sizeof t1), correo_mime_lee(raw, n, &b, t2, sizeof t2));
        CHECK(memcmp(&a, &b, sizeof a) == 0 && memcmp(t1, t2, a.texto_n) == 0);
    }
    TEST_MAIN_END("correo_sbx_311");
}
