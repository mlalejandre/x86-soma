/* SOMA_260_LLM_TAM: prueba en el anfitrion de los tamanos del dialogo con el modelo (src/agent/llm_limites.inc) y de la orden
 * llm-limites (src/agent/llm_limites_shell.inc).   cc -O1 -Isrc/agent -o /tmp/t tests/test_llm_tam_260.c && /tmp/t */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "llm.h"

struct tcb { int agent_mode; };
static struct tcb hilo = { 1 };
static int modo_al_leer = -1;
static struct tcb *thread_current(void) { return &hilo; }
static char disco[256]; static int disco_n = -1;
static int vfs_read(const char *ruta, char *b, uint32_t n)
{
    modo_al_leer = hilo.agent_mode;
    if (strcmp(ruta, "/etc/llm_limites.conf") || disco_n < 0) return -1;
    int k = disco_n < (int)n ? disco_n : (int)n; memcpy(b, disco, (size_t)k); return k;
}
static int vfs_write(const char *ruta, const char *b, uint32_t n) { if (strcmp(ruta, "/etc/llm_limites.conf") || n >= sizeof disco) return -1; memcpy(disco, b, n); disco_n = (int)n; return (int)n; }
static char consola[4096];
static void kprint(const char *s) { strncat(consola, s, sizeof consola - strlen(consola) - 1); }
static void soma_str_put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void soma_str_put_dec(char *o, uint32_t cap, uint32_t *p, uint64_t v) { char b[24]; snprintf(b, sizeof b, "%llu", (unsigned long long)v); soma_str_put(o, cap, p, b); }
#define AGENT_RESP_MAX (384u * 1024u)
char agent_last_response[AGENT_RESP_MAX];
#include "llm_limites.inc"
#include "llm_limites_shell.inc"

static int fallos, n;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)
static void recarga(void) { g_tam_listo = 0; }

int main(void)
{
    /* sin archivo: exactamente los tamanos de antes en lo que cambia el comportamiento del agente */
    const struct llm_tam *t = llm_tam();
    CK(t->contexto == 0 && t->salida == 8192 && !t->salida_fijada, "por defecto: contexto %u salida %u", t->contexto, t->salida);
    CK(t->prompt == 16384 && t->historial == 8192 && t->herramienta == 16384 && t->paso == 450 && t->directa == 8192, "como antes");
    CK(t->texto >= 32768 && t->http >= (1u << 20) && t->peticion >= 65536, "los techos no bajan: texto %u http %u peticion %u", t->texto, t->http, t->peticion);
    CK(modo_al_leer == 0 && hilo.agent_mode == 1, "se lee como nucleo y se deja el modo del agente");
    /* 250 000 tokens */
    CK(llm_tam_fija(250000, 0) == 0, "guardar");
    CK(strstr(disco, "contexto=250000\n") && !strstr(disco, "salida="), "archivo: %.*s", disco_n, disco);
    recarga(); t = llm_tam();
    uint64_t usado = (uint64_t)t->prompt / 3u + (uint64_t)t->salida;      /* tokens como mucho (3 caracteres por token, a la baja) */
    CK(t->contexto == 250000 && usado <= 250000 && t->prompt > 600000, "prompt %u (%llu tokens con la salida)", t->prompt, (unsigned long long)usado);
    CK(t->historial < t->prompt && t->paso > 10000 && t->paso <= t->herramienta, "historial %u paso %u herramienta %u", t->historial, t->paso, t->herramienta);
    CK(t->peticion >= t->prompt * 2u, "la peticion cabe con el JSON escapado");
    /* salida */
    CK(llm_tam_fija(250000, 32000) == 0, "salida");
    recarga(); t = llm_tam();
    CK(t->salida == 32000 && t->salida_fijada && t->texto >= 32000u * 4u && t->texto <= AGENT_RESP_MAX, "texto %u para 32000 tokens", t->texto);
    CK((uint64_t)t->prompt / 3u + t->salida <= 250000, "el prompt deja sitio a la salida");
    /* limites y valores absurdos */
    struct llm_tam x;
    x.contexto = 1; x.salida = 0; x.salida_fijada = 0; llm_tam_calcula(&x);
    CK(x.contexto == LLM_TAM_CONTEXTO_MIN && x.prompt >= 16384 && x.salida == 8192, "contexto minimo");
    x.contexto = 4000000000u; x.salida = 4000000000u; x.salida_fijada = 1; llm_tam_calcula(&x);
    CK(x.contexto == LLM_TAM_CONTEXTO_MAX && x.salida == LLM_TAM_SALIDA_MAX && x.prompt <= (8u << 20) && x.texto <= AGENT_RESP_MAX && x.peticion <= (17u << 20) && x.http <= (24u << 20), "maximos: prompt %u texto %u peticion %u http %u", x.prompt, x.texto, x.peticion, x.http);
    for (uint32_t c = 0; c <= 4000000u; c += 997u) {                      /* nunca por debajo de lo de antes */
        x.contexto = c; x.salida = (c * 7u) % 70000u; x.salida_fijada = 1; llm_tam_calcula(&x);
        if (x.prompt < 16384 || x.historial < 8192 || x.herramienta < 16384 || x.paso < 450 || x.texto < 32768 || x.peticion < 65536 || x.paso > x.herramienta || x.historial > x.prompt) { CK(0, "contexto %u: tamanos raros", c); break; }
    }
    /* archivo con basura */
    memcpy(disco, "xx\ncontexto=abc\nsalida=99999999999\n", 35); disco_n = 35; recarga(); t = llm_tam();
    CK(t->contexto == 0 && t->salida <= LLM_TAM_SALIDA_MAX, "basura: contexto %u salida %u", t->contexto, t->salida);
    memcpy(disco, "micontexto=5\n", 13); disco_n = 13; recarga(); t = llm_tam();
    CK(t->contexto == 0, "clave a medias no vale");
    /* recorte con aviso */
    { char b[300]; memset(b, 'a', sizeof b - 1); b[sizeof b - 1] = 0; b[200] = (char)0xC3; b[201] = (char)0xA1;
      consola[0] = 0; llm_recortada = 0; llm_marca_recorte(b, sizeof b);
      CK(llm_recortada && strstr(b, "respuesta recortada") && strlen(b) < sizeof b && strstr(consola, "recortado"), "recorte: %s", b);
      int ok = 1; for (size_t i = 0; b[i]; ++i) if (((unsigned char)b[i] & 0xC0) == 0xC0 && ((unsigned char)b[i + 1] & 0xC0) != 0x80) ok = 0;
      CK(ok, "sin partir letras");
      char c2[300] = "corta"; llm_recortada = 0; llm_marca_recorte(c2, sizeof c2); CK(!llm_recortada && !strcmp(c2, "corta"), "lo que cabe no se toca"); }
    /* la orden */
    { char o[2048]; disco_n = -1; recarga();
      cmd_llm_limites("", o, sizeof o); CK(strstr(o, "los de siempre") && strstr(o, "salida maxima: 8192"), "orden sin argumentos: %s", o);
      cmd_llm_limites("contexto 250k", o, sizeof o); CK(llm_tam()->contexto == 250000 && strstr(o, "250000 tokens") && strstr(disco, "contexto=250000"), "contexto 250k: %s", o);
      cmd_llm_limites("salida 16000", o, sizeof o); CK(llm_tam()->salida == 16000 && llm_tam()->contexto == 250000, "salida");
      cmd_llm_limites("contexto 10", o, sizeof o); CK(llm_tam()->contexto == LLM_TAM_CONTEXTO_MIN, "contexto diminuto");
      cmd_llm_limites("como-antes", o, sizeof o); CK(llm_tam()->contexto == 0 && !llm_tam()->salida_fijada && llm_tam()->prompt == 16384, "como antes");
      cmd_llm_limites("contexto mucho", o, sizeof o); CK(strstr(o, "Uso:") && llm_tam()->contexto == 0, "mal escrito: no cambia nada");
      cmd_llm_limites("contexto 9999999999999", o, sizeof o); CK(strstr(o, "Uso:"), "numero enorme"); }
    printf("  %s tamanos del LLM: %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
