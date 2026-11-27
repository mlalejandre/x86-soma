/* SOMA_360_HOJA_GRAFICOS: ver hoja_ia.h. El contexto lo prepara la ventana (en su hilo) y se copia: el hilo de la IA no toca el libro. */
#include <stdint.h>
#include <stddef.h>
#include "hoja_ia.h"
#ifndef WEB_HOST_TEST
#include "mem.h"
#include "thread.h"
#include "llm.h"
#include "inv_tema.h"
#endif

#define IA_PREG 1200
#define IA_CTX  (24u * 1024u)
#define IA_RESP (24u * 1024u)
static volatile int g_estado, g_hilo;
static volatile uint32_t g_ver = 1;
static char g_preg[IA_PREG], *g_ctx, *g_resp;
static char *g_texto = (char *)"", *g_formula = (char *)"", *g_tabla = (char *)"";

static const char SYS[] =
    "Eres el asistente de la hoja de c\303\241lculo de SOMA. Te paso la hoja (cada fila con sus celdas: direcci\303\263n=valor; las f\303\263rmulas "
    "van entre par\303\251ntesis) y lo que pide el usuario. Responde en castellano, breve y concreto.\n"
    "- Si lo que conviene es una f\303\263rmula para la celda activa, a\303\261ade al final UNA l\303\255nea: FORMULA: =... con las funciones en "
    "castellano (SUMA, PROMEDIO, SI, BUSCARV, CONTAR.SI, SUMAR.SI.CONJUNTO, TEXTO...), los argumentos separados por ';' y coma decimal. "
    "Usa referencias a las celdas que existen.\n"
    "- Si te piden crear, completar o reorganizar datos, a\303\261ade al final una l\303\255nea TABLA: y debajo las filas, con las columnas "
    "separadas por tabuladores (la primera fila, los t\303\255tulos; 200 filas como mucho). N\303\272meros con coma decimal.\n"
    "- Si es una pregunta sobre los datos, contesta con el resultado y una frase; no inventes datos que no est\303\251n en la hoja.";

static uint32_t sl(const char *s) { uint32_t n = 0; while (s && s[n]) ++n; return n; }
static void cpy(char *d, uint32_t cap, const char *s) { uint32_t i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; ++i; } if (cap) d[i] = 0; }
static char baja(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int empieza_ci(const char *s, const char *p) { while (*p) { if (baja(*s) != *p) return 0; ++s; ++p; } return 1; }

/* "FORMULA: =..." (o "F\303\263rmula:"), "TABLA:" y las vallas ``` que algunos modelos ponen */
void hoja_ia_separa(char *r, char **texto, char **formula, char **tabla)
{
    *texto = r; *formula = (char *)""; *tabla = (char *)"";
    char *tab = 0;
    for (char *l = r; l && *l; ) {
        char *fin = l; while (*fin && *fin != '\n') ++fin;
        char *q = l; while (*q == ' ' || *q == '*' || *q == '-' || *q == '`') ++q;
        int esf = empieza_ci(q, "formula:") || empieza_ci(q, "f\303\263rmula:");
        int est = empieza_ci(q, "tabla:");
        if (esf && !(*formula)[0]) {
            char *v = q; while (*v && *v != ':') ++v;
            if (*v) ++v;
            while (*v == ' ' || *v == '`' || *v == '*') ++v;
            char *e = fin; while (e > v && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '`' || e[-1] == '*')) --e;
            char *sig = *fin ? fin + 1 : fin;
            *e = 0;
            if (*v == '=' && v[1]) *formula = v;
            else if (*v && v > q) { v[-1] = '='; *formula = v - 1; }   /* sin '=': se le pone (pisa el ':' o el espacio de delante) */
            l[0] = 0;                                                    /* el texto acaba antes de la formula */
            l = sig;
            continue;
        }
        if (est && !tab) { l[0] = 0; tab = *fin ? fin + 1 : fin; break; }
        l = *fin ? fin + 1 : fin;
    }
    if (tab) {
        /* fuera las vallas ``` y las lineas en blanco del final */
        char *w = tab; for (char *l = tab; *l; ) {
            char *fin = l; while (*fin && *fin != '\n') ++fin;
            char *q = l; while (*q == ' ') ++q;
            int valla = q[0] == '`' && q[1] == '`' && q[2] == '`';
            if (!valla) { while (l < fin) *w++ = *l++; *w++ = '\n'; }
            l = *fin ? fin + 1 : fin;
        }
        *w = 0; while (w > tab && (w[-1] == '\n' || w[-1] == ' ' || w[-1] == '\r')) *--w = 0;
        *tabla = tab;
    }
    /* el texto sin espacios ni saltos al final */
    { uint32_t n = sl(r); while (n && (r[n - 1] == '\n' || r[n - 1] == ' ' || r[n - 1] == '\r')) r[--n] = 0; }
}

#ifndef WEB_HOST_TEST
static void hilo(void *a)
{
    (void)a;
    char *pr = (char *)kmalloc(IA_CTX + IA_PREG + 256), *r = (char *)kmalloc(IA_RESP);
    if (!pr || !r) { if (pr) kfree(pr); if (r) kfree(r); g_texto = (char *)"Sin memoria para preguntar a la IA."; g_estado = HIA_ERROR; ++g_ver; g_hilo = 0; return; }
    uint32_t p = 0; const char *s;
    for (s = g_ctx; s && *s && p + 1 < IA_CTX; ) pr[p++] = *s++;
    for (s = "\n\nLo que pide el usuario: "; *s; ) pr[p++] = *s++;
    for (s = g_preg; *s && p + 2 < IA_CTX + IA_PREG + 200; ) pr[p++] = *s++;
    pr[p] = 0; r[0] = 0;
    llm_trace_tag("hoja");
    int ok = llm_chat_ex(SYS, pr, 0, 0, 1500, r, IA_RESP, 240000u);
    kfree(pr);
    if (ok == 1 && it_sin_razonamiento(r) == 0) ok = 0;
    if (ok != 1) { cpy(r, IA_RESP, llm_last_error[0] ? llm_last_error : "El modelo de lenguaje no ha contestado (mira Ajustes > Inteligencia artificial)."); }
    g_resp = r;                                                       /* la anterior la solto hoja_ia_pide (en el hilo de la ventana) */
    char *t, *f, *tb;
    if (ok == 1) hoja_ia_separa(r, &t, &f, &tb); else { t = r; f = (char *)""; tb = (char *)""; }
    g_texto = t; g_formula = f; g_tabla = tb;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    g_estado = ok == 1 ? HIA_LISTA : HIA_ERROR;
    ++g_ver; g_hilo = 0;
}
int hoja_ia_pide(const char *pregunta, const char *contexto)
{
    if (g_estado == HIA_PENSANDO || __atomic_exchange_n(&g_hilo, 1, __ATOMIC_ACQ_REL)) return -1;
    if (!g_ctx) g_ctx = (char *)kmalloc(IA_CTX);
    if (!g_ctx) { g_hilo = 0; return -1; }
    cpy(g_preg, sizeof g_preg, pregunta); cpy(g_ctx, IA_CTX, contexto);
    g_texto = (char *)""; g_formula = (char *)""; g_tabla = (char *)"";
    if (g_resp) { kfree(g_resp); g_resp = 0; }                          /* nadie la mira: la ventana pide desde su hilo */
    g_estado = HIA_PENSANDO; ++g_ver;
    if (!thread_create("hoja-ia", hilo, 0)) { g_estado = HIA_NADA; g_hilo = 0; return -1; }
    return 0;
}
#endif
int hoja_ia_estado(void) { return g_estado; }
const char *hoja_ia_texto(void) { return g_estado == HIA_LISTA || g_estado == HIA_ERROR ? g_texto : ""; }
const char *hoja_ia_formula(void) { return g_estado == HIA_LISTA ? g_formula : ""; }
const char *hoja_ia_tabla(void) { return g_estado == HIA_LISTA ? g_tabla : ""; }
void hoja_ia_olvida(void) { if (g_estado != HIA_PENSANDO) { g_estado = HIA_NADA; ++g_ver; } }
uint32_t hoja_ia_version(void) { return g_ver; }
