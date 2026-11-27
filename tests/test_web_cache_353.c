/* SOMA_353_CACHE_HTTP: que se guarda (wn_cache_secs, con bloques de cabeceras reales) y como se sirve (web_hcache.c: frescura,
 * tope de memoria, la menos usada fuera, clave URL + binario, limite del que pide). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "web_net.h"
#include "web_hcache.h"
void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void kfree(void *p) { free(p); }
static uint64_t reloj = 1000000;
uint64_t web_now_ms(void) { return reloj; }
void web_yield(void) {}
uint32_t web_stack_left(void) { return 1024u * 1024u; }
static int checks, fails;
#define CK(c, m) do { checks++; if (!(c)) { fails++; printf("FALLO: %s (linea %d)\n", m, __LINE__); } } while (0)
static int32_t secs(const char *h, int status) { return wn_cache_secs(h, (uint32_t)strlen(h), status); }
static struct wn_resp resp(const char *body, int32_t cache_s)
{
    struct wn_resp r; memset(&r, 0, sizeof r); r.status = 200; r.len = (uint32_t)strlen(body); r.body = (char *)malloc(r.len + 1); memcpy(r.body, body, r.len + 1);
    strcpy(r.ctype, "text/css"); strcpy(r.charset, "utf-8"); strcpy(r.final_url, "https://cdn.example/final.css"); r.cache_s = cache_s; return r;
}
int main(void)
{
    /* --- que se puede guardar --- */
    CK(secs("HTTP/1.1 200 OK\r\ncontent-type: application/javascript\r\ncache-control: public, max-age=31536000, immutable\r\nvary: Accept-Encoding\r\nage: 158543\r\n\r\n", 200) == 31536000 - 158543,
       "20minutos global.bundle.js: un ano menos Age");
    CK(secs("HTTP/1.1 200 OK\r\nCache-Control: max-age=7200, public\r\nAge: 1422\r\n\r\n", 200) == 7200 - 1422, "loader.js: 2 h menos Age");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: public, max-age=0, s-maxage=15, stale-while-revalidate=5\r\nvary: Accept-Encoding\r\n\r\n", 200) == 0, "max-age=0 (la portada): no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: public, s-maxage=600\r\n\r\n", 200) == 0, "solo s-maxage (cache compartida): no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nset-cookie: a=b; Path=/\r\n\r\n", 200) == 0, "con Set-Cookie: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nX-Soma-Set-Cookie: a=b\r\n\r\n", 200) == 0, "con cookie de la pasarela: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600, no-store\r\n\r\n", 200) == 0, "no-store: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: no-cache, max-age=600\r\n\r\n", 200) == 0, "no-cache (habria que revalidar): no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: no-cache=\"set-cookie\", max-age=600\r\n\r\n", 200) == 0, "no-cache con campos: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nvary: Accept-Encoding, User-Agent\r\n\r\n", 200) == 0, "Vary: User-Agent: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nvary: accept\r\n\r\n", 200) == 0, "Vary: Accept (imagenes negociadas): no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nVary: Accept-Encoding\r\n\r\n", 200) == 600, "Vary: Accept-Encoding: si");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=600\r\nage: 700\r\n\r\n", 200) == 0, "ya caducada en la CDN: no");
    CK(secs("HTTP/1.1 404 Not Found\r\ncache-control: max-age=600\r\n\r\n", 404) == 0, "404: no");
    CK(secs("HTTP/1.1 200 OK\r\ncontent-type: text/css\r\n\r\n", 200) == 0, "sin Cache-Control: no");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: no-store\r\nX-Soma-Status: 200\r\n\r\n", 200) == 0, "respuesta de la pasarela (no-store): no");
    CK(secs("HTTP/1.1 200 OK\r\nCACHE-CONTROL: MAX-AGE=60\r\n\r\n", 200) == 60, "mayusculas");
    CK(secs("HTTP/1.1 200 OK\r\ncache-control: max-age=99999999999999\r\n\r\n", 200) > 0, "max-age enorme: sin desbordar");

    /* --- la cache --- */
    whc_set_budget(64u << 10);
    struct wn_resp a = resp("body{color:red}", 600), r;
    whc_put("https://cdn.example/a.css", 0, &a);
    CK(whc_count() == 1, "guardada");
    CK(whc_get("https://cdn.example/a.css", 0, 1u << 20, &r) == 1 && r.status == 200 && r.len == a.len && !memcmp(r.body, a.body, a.len) && r.body != a.body, "servida: copia nueva con el mismo cuerpo");
    CK(!strcmp(r.ctype, "text/css") && !strcmp(r.charset, "utf-8") && !strcmp(r.final_url, "https://cdn.example/final.css") && r.cache_s == 0, "tipo, juego de caracteres y URL final");
    free(r.body);
    a.body[0] = 'X'; CK(whc_get("https://cdn.example/a.css", 0, 0, &r) == 1 && r.body[0] == 'b', "la cache tiene su propia copia"); free(r.body);
    CK(whc_get("https://cdn.example/a.css", 1, 0, &r) == 0, "pedida como binario: otra clave");
    CK(whc_get("https://cdn.example/a.css", 0, 4, &r) == 0, "quien pide acepta menos bytes: a la red");
    CK(whc_get("https://cdn.example/b.css", 0, 0, &r) == 0, "otra URL: no");
    struct wn_resp nc = resp("x", 0); whc_put("https://cdn.example/n.css", 0, &nc);
    CK(whc_get("https://cdn.example/n.css", 0, 0, &r) == 0, "sin cache_s no se guarda");
    struct wn_resp e404 = resp("x", 600); e404.status = 404; whc_put("https://cdn.example/e.css", 0, &e404);
    CK(whc_get("https://cdn.example/e.css", 0, 0, &r) == 0, "un 404 no se guarda");
    reloj += 599000; CK(whc_get("https://cdn.example/a.css", 0, 0, &r) == 1, "fresca a los 599 s"); free(r.body);
    reloj += 2000; CK(whc_get("https://cdn.example/a.css", 0, 0, &r) == 0 && whc_count() == 0, "caducada a los 601 s: fuera");
    /* tope de memoria: la menos usada sale primero */
    char big[12000]; memset(big, 'z', sizeof big - 1); big[sizeof big - 1] = 0;
    char u[64];
    for (int i = 0; i < 4; i++) { struct wn_resp b = resp(big, 3600); snprintf(u, sizeof u, "https://cdn.example/g%d.js", i); whc_put(u, 0, &b); free(b.body); reloj += 10; }
    CK(whc_count() == 4, "cuatro de 12 KB en 64 KB");
    reloj += 10; CK(whc_get("https://cdn.example/g0.js", 0, 0, &r) == 1, "g0 usada otra vez"); free(r.body);
    for (int i = 4; i < 6; i++) { struct wn_resp b = resp(big, 3600); snprintf(u, sizeof u, "https://cdn.example/g%d.js", i); whc_put(u, 0, &b); free(b.body); reloj += 10; }
    CK(whc_bytes() <= (64u << 10), "nunca por encima del tope");
    CK(whc_get("https://cdn.example/g0.js", 0, 0, &r) == 1, "la usada hace poco sigue"); free(r.body);
    CK(whc_get("https://cdn.example/g1.js", 0, 0, &r) == 0, "la menos usada salio");
    struct wn_resp huge = resp(big, 3600); whc_set_budget(32u << 10); whc_put("https://cdn.example/huge.js", 0, &huge);
    CK(whc_get("https://cdn.example/huge.js", 0, 0, &r) == 0, "mas de un cuarto del tope: no se guarda");
    CK(whc_bytes() <= (64u << 10), "sigue dentro");
    struct wn_resp v1 = resp("uno", 600), v2 = resp("dos", 600);
    whc_put("https://cdn.example/v.js", 0, &v1); whc_put("https://cdn.example/v.js", 0, &v2);
    CK(whc_get("https://cdn.example/v.js", 0, 0, &r) == 1 && !strcmp(r.body, "dos"), "la ultima respuesta sustituye a la anterior"); free(r.body);
    whc_clear(); CK(whc_count() == 0 && whc_bytes() == 0, "vaciar");
    free(a.body); free(nc.body); free(e404.body); free(huge.body); free(v1.body); free(v2.body);
    printf("test_web_cache_353: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
