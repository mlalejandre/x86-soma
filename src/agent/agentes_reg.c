/* SOMA_338_AGENTES: registro de actividad de los agentes. Ver agentes_reg.h. */
#include "agentes_reg.h"

static void cpy(char *d, const char *s, uint32_t cap)          /* una linea: saltos como espacios, sin cortar un caracter */
{
    uint32_t n = 0;
    if (!cap) return;
    for (; s && *s && n + 1 < cap; s++) { char c = *s; if (c == '\n' || c == '\r' || c == '\t') c = ' '; if (c == ' ' && n && d[n - 1] == ' ') continue; if ((unsigned char)c < 32) continue; d[n++] = c; }
    if (n) {                                                   /* un caracter UTF-8 cortado al final: fuera */
        uint32_t k = n - 1; while (k && ((unsigned char)d[k] & 0xC0) == 0x80) k--;
        unsigned char c = (unsigned char)d[k]; uint32_t L = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (k + L > n) n = k;
    }
    while (n && d[n - 1] == ' ') n--;
    d[n] = 0;
}
static int ig(const char *a, const char *b) { while (*a && *b && (*a | 32) == (*b | 32)) { a++; b++; } return !*b && (*a == 0 || *a == ' '); }
static int pre(const char *a, const char *b) { while (*b && *a == *b) { a++; b++; } return !*b; }

void agr_inicia(struct agr *r)
{
    uint8_t *z = (uint8_t *)r; for (uint32_t i = 0; i < sizeof *r; i++) z[i] = 0;
}
int agr_esp_de(const char *f)
{
    if (!f || !*f) return -1;
    if (ig(f, "lcm")) return AGE_LCM;
    if (ig(f, "mayordomo")) return AGE_MAYORDOMO;
    if (ig(f, "buscar") || ig(f, "buscador") || ig(f, "web") || ig(f, "internet") || ig(f, "investigador")) return AGE_INVESTIGADOR;
    if (ig(f, "documento") || ig(f, "maquetador")) return AGE_MAQUETADOR;
    if (ig(f, "programador") || ig(f, "coder")) return AGE_PROGRAMADOR;
    if (ig(f, "pensar") || ig(f, "escribir")) return AGE_MODELO;
    if (ig(f, "voz")) return AGE_VOZ;
    if (ig(f, "hecho") || ig(f, "soma")) return -1;
    return AGE_HERRAMIENTAS;                                  /* "orden", "agente" y cualquier otro agente */
}
int agr_esp_de_orden(const char *l)
{
    if (!l) return AGE_HERRAMIENTAS;
    while (*l == ' ') l++;
    static const char *const INV[] = { "search", "investiga", "leer", "descargar", "navegador", "fetch", "web", "noticias", "tema", "youtube", 0 };
    static const char *const PRO[] = { "studio", "ejecutar", "cc", "python", "js", "coder", "programa", 0 };
    static const char *const DOC[] = { "documento", "latex", "maquetador", 0 };
    for (int i = 0; INV[i]; i++) if (ig(l, INV[i])) return AGE_INVESTIGADOR;
    for (int i = 0; PRO[i]; i++) if (ig(l, PRO[i])) return AGE_PROGRAMADOR;
    for (int i = 0; DOC[i]; i++) if (ig(l, DOC[i])) return AGE_MAQUETADOR;
    return AGE_HERRAMIENTAS;
}
static const char *const NOM[AGE_N] = { "\303\223rdenes al instante (LCM)", "Mayordomo", "Investigador", "Programador", "Maquetador",
    "Agente con herramientas", "Modelo de lenguaje", "Voz" };
static const char *const DESC[AGE_N] = {
    "Entiende lo que pides y lo hace al momento, sin el modelo: abrir apps, el volumen, la hora...",
    "Operaciones del sistema sin el modelo: memoria, disco, archivos y carpetas.",
    "Busca y lee en Internet (Wikipedia, DuckDuckGo, prensa y fuentes por tema) y escribe un informe con citas.",
    "Escribe, repara y prueba programas en SOMA-LANG, Python, C, JavaScript y HTML.",
    "Compone documentos (PDF, LaTeX) con su maquetacion.",
    "El modelo usa las ordenes de SOMA paso a paso para lo que necesita varias acciones.",
    "Piensa y redacta la respuesta.",
    "Lee las respuestas en voz alta y escucha con el micr\303\263fono.",
};
static const char *const EJ[AGE_N] = { "abre la agenda", "cuanta memoria libre tengo", "investiga que sintomas tiene la gripe",
    "haz un programa en python que calcule primos", "hazme un documento sobre el sistema solar", "crea una carpeta proyectos y un archivo notas.txt dentro",
    "explicame la fotosintesis en tres frases", "lee en voz alta que hora es" };
const char *agr_nombre(int e) { return e >= 0 && e < AGE_N ? NOM[e] : ""; }
const char *agr_desc(int e) { return e >= 0 && e < AGE_N ? DESC[e] : ""; }
const char *agr_ejemplo(int e) { return e >= 0 && e < AGE_N ? EJ[e] : ""; }

static struct agr_mision *abierta(struct agr *r) { return r->en_curso && r->nmis ? &r->mis[(r->imis + AGR_MIS - 1) % AGR_MIS] : 0; }
static void usa(struct agr *r, struct agr_mision *m, int e, uint64_t ms)
{
    if (e < 0 || e >= AGE_N) return;
    r->ult_ms[e] = ms ? ms : 1;
    if (m) {
        if (!(m->esp & (1u << e))) { m->esp |= 1u << e; r->usos[e]++; }
        if (!m->npasos || m->paso[m->npasos - 1] != e) {
            if (m->npasos == AGR_PASOS) { for (int i = 1; i < AGR_PASOS; i++) { m->paso[i - 1] = m->paso[i]; m->paso_ms[i - 1] = m->paso_ms[i]; } m->npasos--; }
            m->paso[m->npasos] = (uint8_t)e; m->paso_ms[m->npasos] = (uint32_t)(ms - m->ini_ms); m->npasos++;
        }
    } else r->usos[e]++;
}
void agr_mision(struct agr *r, const char *texto, uint64_t ms, int64_t min)
{
    struct agr_mision *a = abierta(r);
    if (a) { a->estado = AGF_DETENIDA; a->fin_ms = ms; }          /* la anterior no cerro: se da por cortada */
    struct agr_mision *m = &r->mis[r->imis];
    uint8_t *z = (uint8_t *)m; for (uint32_t i = 0; i < sizeof *m; i++) z[i] = 0;
    cpy(m->texto, texto, sizeof m->texto);
    m->ini_ms = ms; m->ini_min = min; m->estado = AGF_EN_CURSO;
    r->imis = (r->imis + 1) % AGR_MIS; if (r->nmis < AGR_MIS) r->nmis++;
    r->en_curso = 1; r->ver++;
}
void agr_fase(struct agr *r, const char *fase, uint64_t ms)
{
    int e = agr_esp_de(fase);
    if (e < 0) return;
    usa(r, abierta(r), e, ms); r->ver++;
}
void agr_orden(struct agr *r, const char *linea, uint64_t ms, int64_t min)
{
    if (!linea || !*linea) return;
    int e = agr_esp_de_orden(linea);
    struct agr_act *x = &r->act[r->iact];
    x->ms = ms; x->min = min; x->esp = (uint8_t)e; cpy(x->txt, linea, sizeof x->txt);
    r->iact = (r->iact + 1) % AGR_ACT; if (r->nact < AGR_ACT) r->nact++;
    usa(r, abierta(r), e, ms); r->ver++;
}
void agr_fin(struct agr *r, const char *resp, uint64_t ms)
{
    struct agr_mision *m = abierta(r);
    if (!m) return;
    m->fin_ms = ms;
    const char *s = resp ? resp : "";
    while (*s == ' ' || *s == '\n') s++;
    if (pre(s, "[Razonamiento]:")) { const char *d = s; for (; *d; d++) if (pre(d, "[Dictamen]:") || pre(d, "[Respuesta]:")) { s = d; while (*s && *s != ':') s++; if (*s) s++; break; } }
    while (*s == ' ' || *s == '\n') s++;
    cpy(m->respuesta, s, sizeof m->respuesta);
    if (pre(s, "Mision detenida")) m->estado = AGF_DETENIDA;
    else if (pre(s, "Error") || pre(s, "[ERROR]") || pre(s, "ERROR") || pre(s, "LLM: ")) m->estado = AGF_ERROR;
    else m->estado = AGF_BIEN;
    r->en_curso = 0; r->ver++;
}
const struct agr_mision *agr_mision_i(const struct agr *r, int i)
{
    if (i < 0 || i >= r->nmis) return 0;
    return &r->mis[(r->imis + AGR_MIS - 1 - i) % AGR_MIS];
}
const struct agr_act *agr_act_i(const struct agr *r, int i)
{
    if (i < 0 || i >= r->nact) return 0;
    return &r->act[(r->iact + AGR_ACT - 1 - i) % AGR_ACT];
}
int agr_activo(const struct agr *r)
{
    if (!r->en_curso || !r->nmis) return -1;
    const struct agr_mision *m = &r->mis[(r->imis + AGR_MIS - 1) % AGR_MIS];
    return m->npasos ? m->paso[m->npasos - 1] : -1;
}
static void put(char *o, uint32_t cap, uint32_t *p, const char *s) { while (*s && *p + 1 < cap) o[(*p)++] = *s++; if (cap) o[*p < cap ? *p : cap - 1] = 0; }
static void putd(char *o, uint32_t cap, uint32_t *p, uint64_t v, int dos) { char b[24]; int k = 0; do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 23); if (dos && k < 2) b[k++] = '0'; while (k) { char c[2] = { b[--k], 0 }; put(o, cap, p, c); } }
void agr_dura(uint64_t ms, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    if (ms < 1000) { putd(o, cap, &p, ms, 0); put(o, cap, &p, " ms"); return; }
    uint64_t s = ms / 1000;
    if (s < 60) { putd(o, cap, &p, s, 0); if (s < 10) { put(o, cap, &p, ","); putd(o, cap, &p, ms / 100 % 10, 0); } put(o, cap, &p, " s"); return; }
    if (s < 3600) { putd(o, cap, &p, s / 60, 0); put(o, cap, &p, " min "); putd(o, cap, &p, s % 60, 1); put(o, cap, &p, " s"); return; }
    putd(o, cap, &p, s / 3600, 0); put(o, cap, &p, " h "); putd(o, cap, &p, s / 60 % 60, 1); put(o, cap, &p, " min");
}

#ifndef AGR_SOLO_PURO
/* ---------------------------------------------------------------- en SOMA */
#include "mutex.h"
#include "idt.h"
#include "hora.h"
#include "thread.h"
static struct agr g_r; static struct kmutex g_lk; static volatile int g_ok;
static uint32_t g_tid; static int g_anid;                  /* hilo de la mision abierta y cuantas agent_run anidadas lleva */
static void lk(void) { if (!g_ok) { kmutex_init(&g_lk, "agentes"); agr_inicia(&g_r); g_ok = 1; } kmutex_lock(&g_lk); }
static void ulk(void) { kmutex_unlock(&g_lk); }
static uint32_t tid(void) { struct tcb *t = thread_current(); return t ? t->tid + 1u : 1u; }
/* una mision por peticion: las agent_run de dentro (delegaciones, el Mayordomo) cuentan en la de fuera */
void agentes_reg_mision(const char *t)
{
    uint64_t ms = timer_get_uptime_ms(); int64_t mi = hora_utc_min(); uint32_t me = tid();
    lk();
    if (g_r.en_curso && g_tid == me) g_anid++;
    else { agr_mision(&g_r, t, ms, mi); g_tid = me; g_anid = 1; }
    ulk();
}
void agentes_reg_fase(const char *f) { uint64_t ms = timer_get_uptime_ms(); lk(); agr_fase(&g_r, f, ms); ulk(); }
void agentes_reg_orden(const char *l) { uint64_t ms = timer_get_uptime_ms(); int64_t mi = hora_utc_min(); lk(); agr_orden(&g_r, l, ms, mi); ulk(); }
/* desde el hilo de la mision cierra la de fuera; desde otro (la GUI, si el hilo se detuvo) la cierra siempre */
void agentes_reg_fin(const char *r)
{
    uint64_t ms = timer_get_uptime_ms(); uint32_t me = tid();
    lk();
    if (g_r.en_curso && g_tid == me && --g_anid > 0) { ulk(); return; }
    agr_fin(&g_r, r, ms); g_anid = 0; g_tid = 0;
    ulk();
}
struct agr *agentes_reg_entra(void) { lk(); return &g_r; }
void agentes_reg_sale(void) { ulk(); }
uint32_t agentes_reg_version(void) { return g_ok ? g_r.ver : 0; }

static void hhmm(int64_t min, char *o, uint32_t cap)
{
    uint32_t p = 0; if (cap) o[0] = 0;
    if (min <= 0) return;
    int64_t l = min + hora_desfase_en(min); int64_t d = l % 1440; if (d < 0) d += 1440;
    putd(o, cap, &p, (uint64_t)(d / 60), 1); put(o, cap, &p, ":"); putd(o, cap, &p, (uint64_t)(d % 60), 1);
}
int agentes_cmd(const char *a, char *o, uint32_t cap)
{
    (void)a;
    uint32_t p = 0; if (cap) o[0] = 0;
    struct agr *r = agentes_reg_entra();
    uint64_t ya = timer_get_uptime_ms();
    int act = agr_activo(r);
    const struct agr_mision *m0 = agr_mision_i(r, 0);
    if (r->en_curso && m0) {
        char d[24]; agr_dura(ya - m0->ini_ms, d, sizeof d);
        put(o, cap, &p, "Ahora: \""); put(o, cap, &p, m0->texto); put(o, cap, &p, "\" ("); put(o, cap, &p, d);
        if (act >= 0) { put(o, cap, &p, ", "); put(o, cap, &p, agr_nombre(act)); }
        put(o, cap, &p, ")\n");
    } else put(o, cap, &p, "Ahora: en reposo\n");
    put(o, cap, &p, "Especialistas (usos en esta sesion):\n");
    for (int e = 0; e < AGE_N; e++) { put(o, cap, &p, "  "); put(o, cap, &p, agr_nombre(e)); put(o, cap, &p, ": "); putd(o, cap, &p, r->usos[e], 0); put(o, cap, &p, "\n"); }
    put(o, cap, &p, "Misiones recientes:\n");
    for (int i = 0; i < r->nmis && i < 10; i++) {
        const struct agr_mision *m = agr_mision_i(r, i); char h[8], d[24];
        hhmm(m->ini_min, h, sizeof h); agr_dura((m->fin_ms ? m->fin_ms : ya) - m->ini_ms, d, sizeof d);
        put(o, cap, &p, "  "); put(o, cap, &p, h); put(o, cap, &p, "  ");
        put(o, cap, &p, m->estado == AGF_EN_CURSO ? "[en curso] " : m->estado == AGF_BIEN ? "[bien] " : m->estado == AGF_ERROR ? "[error] " : "[detenida] ");
        put(o, cap, &p, m->texto); put(o, cap, &p, "  ("); put(o, cap, &p, d); put(o, cap, &p, ";");
        for (int e = 0; e < AGE_N; e++) if (m->esp & (1u << e)) { put(o, cap, &p, " "); put(o, cap, &p, agr_nombre(e)); }
        put(o, cap, &p, ")\n");
    }
    if (!r->nmis) put(o, cap, &p, "  (ninguna todavia)\n");
    put(o, cap, &p, "Actividad reciente:\n");
    for (int i = 0; i < r->nact && i < 12; i++) { const struct agr_act *x = agr_act_i(r, i); char h[8]; hhmm(x->min, h, sizeof h);
        put(o, cap, &p, "  "); put(o, cap, &p, h); put(o, cap, &p, "  "); put(o, cap, &p, x->txt); put(o, cap, &p, "\n"); }
    if (!r->nact) put(o, cap, &p, "  (nada todavia)\n");
    agentes_reg_sale();
    return 1;
}
#endif
