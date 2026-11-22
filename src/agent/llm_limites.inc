/* SOMA_260_LLM_TAM: los tamanos del dialogo con el modelo de lenguaje dejan de ser fijos.
 *
 * Lo incluye src/agent/llm.c. Antes: peticion 64 KB, respuesta HTTP 64-256 KB, texto de la respuesta 32-48 KB, respuesta final
 * 8 KB, prompt del agente 16 KB, historial 8 KB, y de cada orden el modelo solo veia 400 caracteres. Con un modelo de 250 000
 * tokens de contexto, SOMA no le mandaba ni el 5 % y cortaba en silencio las respuestas largas.
 *
 * /etc/llm_limites.conf (orden llm-limites):
 *   contexto=<tokens>   ventana de contexto del modelo. 0 = como antes (los tamanos de siempre, para no cambiar el comportamiento).
 *   salida=<tokens>     cuanto puede escribir el modelo (max_tokens). Sin fijar = 8192, como antes.
 * Todo lo demas se calcula de esos dos numeros (llm_tam_calcula), contando 3 caracteres por token (el espanol da unos 4: sobra
 * margen) y reservando sitio para la salida. Si no hay memoria para un tamano grande, se vuelve al de siempre. */

#define TAM_CPT        3u                        /* caracteres por token (a la baja) */
#define TAM_PROMPT_MAX (8u << 20)                /* 8 MB de prompt como mucho */
#define TAM_TEXTO_MAX  (4u << 20)

static struct llm_tam g_tam; static int g_tam_listo;
static uint32_t tam_max(uint32_t a, uint32_t b) { return a > b ? a : b; }
static uint32_t tam_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Calcula los tamanos a partir de t->contexto y t->salida (t->salida_fijada dice si la eligio el usuario). Sin efectos. */
void llm_tam_calcula(struct llm_tam *t)
{
    if (t->salida < 256u) t->salida = 8192u;
    if (t->salida > LLM_TAM_SALIDA_MAX) t->salida = LLM_TAM_SALIDA_MAX;
    if (t->contexto && t->contexto < LLM_TAM_CONTEXTO_MIN) t->contexto = LLM_TAM_CONTEXTO_MIN;
    if (t->contexto > LLM_TAM_CONTEXTO_MAX) t->contexto = LLM_TAM_CONTEXTO_MAX;
    /* texto de una respuesta: lo que cabe en 'salida' tokens (razonamiento incluido) con holgura; como poco lo de antes (32 KB) */
    t->texto = tam_min(tam_max(32768u, t->salida * 5u + 8192u), TAM_TEXTO_MAX);
    /* cuerpo HTTP: el JSON escapa los acentos (á = 6 bytes por letra) */
    t->http = tam_max(1u << 20, tam_min(t->texto * 6u, 24u << 20));
    if (!t->contexto) {                                              /* como antes */
        t->prompt = 16384u; t->historial = 8192u; t->herramienta = 16384u; t->paso = 450u; t->directa = 8192u;
    } else {
        uint64_t p = (uint64_t)t->contexto * TAM_CPT, reserva = (uint64_t)t->salida * 4u + 4096u;
        p = p > reserva ? p - reserva : 0;
        if (p < 16384u) p = 16384u;
        if (p > TAM_PROMPT_MAX) p = TAM_PROMPT_MAX;
        t->prompt = (uint32_t)p;
        t->historial = tam_max(8192u, t->prompt / 5u * 3u);          /* el historial de pasos, lo que mas crece */
        t->herramienta = tam_min(tam_max(16384u, t->prompt / 8u), 1u << 20);
        t->paso = tam_min(tam_max(450u, t->prompt / 24u), t->herramienta);   /* lo que el modelo ve de cada orden */
        t->directa = t->prompt;
    }
    /* la peticion: el prompt escapado en JSON (hasta el doble) + indicaciones del sistema */
    t->peticion = tam_max(65536u, t->prompt * 2u + 32768u);
}

static uint32_t tam_num(const char *c, const char *clave, uint32_t def)
{
    for (const char *p = c; *p; ++p) {
        const char *a = p, *b = clave; while (*b && *a == *b) { ++a; ++b; }
        if (*b || (p != c && p[-1] != '\n')) continue;
        uint32_t v = 0; int d = 0; while (*a >= '0' && *a <= '9' && v < 100000000u) { v = v * 10u + (uint32_t)(*a++ - '0'); d = 1; }
        return d ? v : def;
    }
    return def;
}
static void tam_lee(void)
{
    char c[160]; int n;
    {   /* lectura del nucleo, no del agente (como /etc/llm.conf) */
        struct tcb *th = thread_current(); int am = th ? th->agent_mode : 0;
        if (th) th->agent_mode = 0;
        n = vfs_read("/etc/llm_limites.conf", c, sizeof c - 1);
        if (th) th->agent_mode = am;
    }
    c[n > 0 ? n : 0] = 0;
    g_tam.contexto = tam_num(c, "contexto=", 0);
    g_tam.salida = tam_num(c, "salida=", 0);
    g_tam.salida_fijada = g_tam.salida != 0;
    llm_tam_calcula(&g_tam);
}
const struct llm_tam *llm_tam(void)
{
    if (!g_tam_listo) { tam_lee(); g_tam_listo = 1; }
    return &g_tam;
}
static void tam_pon(char *o, uint32_t cap, uint32_t *q, const char *s) { while (*s && *q + 1 < cap) o[(*q)++] = *s++; o[*q] = 0; }
static void tam_pon_n(char *o, uint32_t cap, uint32_t *q, uint32_t v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = (char)('0' + v % 10u); v /= 10u; } tam_pon(o, cap, q, b + i); }
/* contexto 0 = como antes; salida 0 = sin fijar (8192). 0 = guardado. */
int llm_tam_fija(uint32_t contexto, uint32_t salida)
{
    struct llm_tam t; t.contexto = contexto; t.salida = salida; t.salida_fijada = salida != 0;
    llm_tam_calcula(&t);
    char b[96]; uint32_t q = 0; b[0] = 0;
    tam_pon(b, sizeof b, &q, "contexto="); tam_pon_n(b, sizeof b, &q, t.contexto); tam_pon(b, sizeof b, &q, "\n");
    if (t.salida_fijada) { tam_pon(b, sizeof b, &q, "salida="); tam_pon_n(b, sizeof b, &q, t.salida); tam_pon(b, sizeof b, &q, "\n"); }
    g_tam = t; g_tam_listo = 1;
    return vfs_write("/etc/llm_limites.conf", b, q) < 0 ? -1 : 0;
}


/* SOMA_260_LLM_TAM: si el texto llena el sitio que le dio quien llama, se dice (antes se cortaba en silencio) */
int llm_recortada;
static void llm_marca_recorte(char *out, uint32_t max)
{
    if (!out || max < 256u) return;
    uint32_t n = 0; while (n < max && out[n]) ++n;
    if (n + 1u < max) return;
    static const char m[] = "\n\n[... respuesta recortada: no cabia entera. Mira la orden llm-limites.]";
    uint32_t L = (uint32_t)sizeof m - 1u, k = max - 1u - L;
    while (k > 0 && ((unsigned char)out[k] & 0xC0u) == 0x80u) --k;      /* sin partir una letra UTF-8 */
    for (uint32_t i = 0; i < L; ++i) out[k + i] = m[i];
    out[k + L] = 0;
    llm_recortada = 1;
    kprint("LLM: la respuesta no cabia y se ha recortado (llm-limites).\n");
}
/* SOMA_260_LLM_TAM fin */
