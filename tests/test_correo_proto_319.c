/* SOMA_319_CORREO_PROTO: lo que el nucleo lee del SERVIDOR de correo (src/apps/correo_core.c: respuestas IMAP con literales {n},
 * FETCH, LIST, SEARCH, numeros, capacidades, etiquetas; respuestas SMTP y EHLO) con datos hostiles. El servidor lo configura el
 * usuario, pero la conexion puede ser de cualquiera (un servidor comprometido, una red con STARTTLS mal usado).
 * Respuestas reales mutadas y cortadas, cada una en un bufer de su tamano exacto SIN terminador: ASan ve cualquier lectura de mas.
 * Ademas: lo "completo" nunca pasa del bufer, FETCH apunta dentro del bufer, LIST deja cadenas terminadas, SEARCH respeta max. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "correo_core.h"

void *co_alloc(uint32_t n) { return malloc(n ? n : 1); }       /* las da quien enlaza (correo_core.h) */
void co_free(void *p) { free(p); }

static uint64_t semilla = 319;
static uint32_t rnd(void) { semilla = semilla * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(semilla >> 33); }

static const char *const IMAP[] = {
    "* 12 FETCH (UID 345 RFC822.SIZE 2048 FLAGS (\\Seen \\Flagged) BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {62}\r\nFrom: Ana <ana@x.es>\r\nSubject: Hola\r\nDate: Mon, 1 Jan 2026\r\n\r\n)\r\n",
    "* 9 FETCH (UID 9 FLAGS () BODY[] {5}\r\nHola!)\r\n",
    "* 10 FETCH (UID 10 INTERNALDATE \"01-Jan-2026 10:00:00 +0000\" BODY[]<0> \"Hola!\")\r\n",
    "* LIST (\\HasNoChildren \\Sent) \"/\" \"[Gmail]/Enviados\"\r\n",
    "* LIST (\\HasNoChildren) \".\" \"INBOX.Elementos &AOk-liminados\"\r\n",
    "* LIST () \"/\" \"Con \\\"comillas\\\"\"\r\n",
    "* LIST (\\Noselect) \"/\" {7}\r\nCarpeta\r\n",
    "* SEARCH 4 8 15 16 23 42\r\n",
    "* 23 EXISTS\r\n",
    "* OK [UIDNEXT 4567] Predicted next UID\r\n",
    "* CAPABILITY IMAP4rev1 MOVE UIDPLUS X-GM-EXT-1 AUTH=PLAIN\r\n",
    "A7 OK [READ-WRITE] SELECT completado\r\n",
    "A7 NO [AUTHENTICATIONFAILED] Invalid credentials\r\n",
    "* STATUS \"INBOX\" (MESSAGES 231 UNSEEN 3)\r\n",
};
static const char *const SMTP[] = {
    "250-smtp.x.es Hola\r\n250-PIPELINING\r\n250-SIZE 35882577\r\n250-STARTTLS\r\n250-AUTH LOGIN PLAIN XOAUTH2\r\n250 SMTPUTF8\r\n",
    "220 smtp.x.es ESMTP listo\r\n",
    "535-5.7.8 Usuario o clave\r\n535 5.7.8 no aceptados\r\n",
    "354 Adelante\r\n",
};

static const char RAROS[] = "{}0123456789\"\\()[]<> \r\n*+-~";

/* copia mutada de s en un bufer de su tamano exacto (sin NUL) */
static char *mutada(const char *s, uint32_t *n)
{
    uint32_t l = (uint32_t)strlen(s), cap = l + 64;
    char *t = (char *)malloc(cap);
    memcpy(t, s, l);
    int cambios = (int)(rnd() % 8);
    for (int c = 0; c < cambios && l; ++c) {
        uint32_t i = rnd() % l, k = rnd() % 4;
        if (k == 0) t[i] = RAROS[rnd() % (sizeof RAROS - 1)];
        else if (k == 1) t[i] = (char)rnd();
        else if (k == 2 && l + 8 < cap) {                              /* inserta un literal o un numero enorme */
            static const char *const ins[] = { "{4294967295}", "{99}", "{0}\r\n", "99999999999", "\"", "\\", ")" };
            const char *x = ins[rnd() % 7]; uint32_t xl = (uint32_t)strlen(x);
            if (l + xl < cap) { memmove(t + i + xl, t + i, l - i); memcpy(t + i, x, xl); l += xl; }
        } else if (l > 1) { memmove(t + i, t + i + 1, l - i - 1); l--; }    /* quita un byte */
    }
    if (rnd() % 4 == 0 && l) l = rnd() % (l + 1);                     /* cortada */
    char *e = (char *)malloc(l ? l : 1);
    memcpy(e, t, l);
    free(t);
    *n = l;
    return e;
}

static int termina(const char *s, size_t cap) { return memchr(s, 0, cap) != 0; }

int main(int argc, char **argv)
{
    int iter = argc > 1 ? atoi(argv[1]) : 200000;
    int malos = 0;
    uint32_t uids[8];
    for (int it = 0; it < iter; ++it) {
        int imap = (rnd() % 3) != 0;
        const char *base = imap ? IMAP[rnd() % (sizeof IMAP / sizeof IMAP[0])] : SMTP[rnd() % (sizeof SMTP / sizeof SMTP[0])];
        uint32_t n; char *b = mutada(base, &n);
        if (imap) {
            uint32_t c = co_imap_completa(b, n);
            if (c > n) malos++;
            struct co_fetch f; memset(&f, 0, sizeof f);
            if (co_imap_fetch(b, n, &f)) {
                if (f.cab && (f.cab < b || f.cab_n > n || f.cab + f.cab_n > b + n)) malos++;
                if (f.cuerpo && (f.cuerpo < b || f.cuerpo_n > n || f.cuerpo + f.cuerpo_n > b + n)) malos++;
            }
            struct co_carpeta k; memset(&k, 'Z', sizeof k);
            if (co_imap_list(b, n, &k) && (!termina(k.nombre, sizeof k.nombre) || !termina(k.imap, sizeof k.imap))) malos++;
            uint32_t m = 1 + rnd() % 8;
            if (co_imap_search(b, n, uids, m) > m) malos++;
            uint32_t v;
            (void)co_imap_num(b, n, "EXISTS", &v); (void)co_imap_num(b, n, "UIDNEXT", &v); (void)co_imap_num(b, n, "UNSEEN", &v);
            (void)co_imap_cap(b, n, "MOVE");
            char t[64]; memset(t, 'Z', sizeof t);
            if (co_imap_etiqueta(b, n, "A7", t, sizeof t) != 0 && !termina(t, sizeof t)) malos++;
        } else {
            int cod = 0; uint32_t usado = 0;
            if (co_smtp_resp(b, n, &cod, &usado) && usado > n) malos++;
            (void)co_smtp_caps(b, n);
        }
        free(b);
    }
    CHECK_EQ_INT(malos, 0);
    /* los literales enormes no se dan por completos (no hay tantos datos) */
    static const char g[] = "* 1 FETCH (BODY[] {4294967295}\r\nab)\r\n";
    CHECK(co_imap_completa(g, (uint32_t)sizeof g - 1) == 0);
    /* el fallo que encontro esta prueba: un literal {4294967295} en medio de la linea desbordaba i + v y el cuerpo salia de 4 GB
     * (el nucleo copiaba despues hasta su capacidad desde un bufer de 44 bytes) */
    static const char h[] = "* 1 FETCH (UID 5 BODY[] {4294967295}x\r\nab)\r\n";
    uint32_t hn = co_imap_completa(h, (uint32_t)sizeof h - 1);
    struct co_fetch f; memset(&f, 0, sizeof f);
    CHECK(hn > 0 && co_imap_fetch(h, hn, &f) == 1 && f.uid == 5);
    CHECK(f.cuerpo && f.cuerpo >= h && f.cuerpo + f.cuerpo_n <= h + hn);
    printf("[correo_proto_319] %d respuestas mutadas\n", iter);
    TEST_MAIN_END("correo_proto_319");
}
