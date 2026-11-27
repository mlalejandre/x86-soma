/* SOMA_CODER_IDE_V2
 * SOMA Studio :: Coder IDE -- programador agentico multipaso.
 *
 * Pipeline: 1 Plan (LLM) -> 2 Codigo (agente Coder) -> 3 Verificar (compile + lint)
 *           -> 4 Reparar (hasta 3 rondas) -> 5 Revision -> 6 Entrega.
 * Copia de seguridad previa y rollback automatico si el resultado final no compila.
 */
#include "acceso.h"      /* SOMA_363_ACCESO */
#include "lang.h"
#include "coder_app.h"
#include "fb.h"
#include "gui_font.h"
#include "fs.h"
#include "mem.h"
#include "agent.h"
#include "shell.h"
#include "thread.h"
#include "console.h"
#include "mouse.h"
#include "notepad.h"
#include "arch.h"
#include "soma_str.h"
#include "lang_diag.h"
#include "llm.h"
#include "gui.h"
#include "codeedit.h"
#include "coder_core.h"
#include "clipboard.h"
#include "web_font.h"
#include "textfield.h"
#include "sysicon.h"
#include "png_enc.h"
#include "docagent.h"   /* SOMA_248_DOC */
#include "coder_run.h"  /* SOMA_333_SALIDA */

/* PARCHE 094: toda la interfaz del Coder con la fuente antialiasada del sistema (antes: mapa de bits 8x8) */
static int cdr_wf(void) { static int v = -1; if (v < 0) v = wf_init() ? 1 : 0; return v; }
static int cdr_is_caps(const char *s) { int n = 0, up = 0; for (; s[n]; n++) { if (s[n] >= 'a' && s[n] <= 'z') return 0; if (s[n] >= 'A' && s[n] <= 'Z') up++; } return up >= 4 && n < 48; }
static int cdr_tw(const char *s)
{
    uint32_t n = 0; while (s[n]) n++;
    if (!cdr_wf()) return (int)n * 8;
    return cdr_is_caps(s) ? wf_width(s, n, 11, WF_BOLD) + (int)n : wf_width(s, n, 13, 0);
}
static void cdr_txt(uint32_t x, uint32_t y, const char *s, uint32_t col, int scale)
{
    uint32_t n = 0; while (s[n]) n++;
    if (!cdr_wf()) { font_draw_string(x, y, s, col, scale); return; }
    if (cdr_is_caps(s)) {                               /* rotulos en mayusculas: versalitas espaciadas */
        int xx = (int)x;
        for (uint32_t i = 0; i < n; i++) { wf_draw(xx, (int)y + 9, s + i, 1, 11, col, WF_BOLD, 255); xx += wf_width(s + i, 1, 11, WF_BOLD) + 1; }
        return;
    }
    wf_draw((int)x, (int)y + 9, s, n, 13, col, 0, 255);
}
#define font_draw_string(x, y, s, c, sc) cdr_txt((x), (y), (s), (c), (sc))

extern uint64_t timer_get_uptime_ms(void);

#define CDR_MAX_PROJECTS 64
#define CDR_SRC_MAX      65536   /* SOMA_262_PUBLICAR + SOMA_264_LANG: como CC_SRC_MAX (antes 16 KB, luego 32 KB) */
#define CDR_LOG_N        40   /* PARCHE 099 */
#define CDR_LOG_LEN      120
#define CDR_LINT_N       6
#define CDR_LINT_LEN     96
#define CDR_MISSION_MAX  1024
#define CDR_FULL_MAX     1536
#define CDR_PLAN_MAX     480
#define CDR_THOUGHT_MAX  384
#define CDR_MAX_REPAIRS  3
#define CDR_PHASES       6
#define CDR_BAK_FILE     "/tmp/coder_prev.bak"
#define CDR_REPORT_FILE  "/agent/coder_log.txt"

#define CDR_C_BG       0x000A0F1Cu
#define CDR_C_PANEL    0x00111827u
#define CDR_C_DARK     0x00080C17u
#define CDR_C_BORDER   0x001B2436u
#define CDR_C_BORDER2  0x00334155u
#define CDR_C_GREEN    0x0034D399u
#define CDR_C_CYAN     0x0038BDF8u
#define CDR_C_TXT      0x00E2E8F0u
#define CDR_C_SOFT     0x00CBD5E1u
#define CDR_C_GRAY     0x0094A3B8u
#define CDR_C_DIM      0x0064748Bu
#define CDR_C_AMBER    0x00FBBF24u
#define CDR_C_RED      0x00F43F5Eu
#define CDR_C_WHITE    0x00FFFFFFu
#define CDR_C_KW       0x0038BDF8u
#define CDR_C_DECL     0x00F472B6u
#define CDR_C_STR      0x00FBBF24u
#define CDR_C_NUM      0x00A78BFAu
#define CDR_C_NS       0x0034D399u
#define CDR_C_COMMENT  0x0064748Bu

enum cdr_tab {
    CDR_TAB_NEW = 0,
    CDR_TAB_MODIFY = 1,
    CDR_TAB_PROJECTS = 2,
    CDR_TAB_LOG = 3,
    CDR_TAB_EDITOR = 4      /* PARCHE 093: editor profesional */
};

enum coder_job_state {
    CODER_JOB_IDLE = 0,
    CODER_JOB_WORKING,
    CODER_JOB_SUCCESS,
    CODER_JOB_ERROR
};

struct cdr_proj {
    char     name[48];
    char     path[80];
    uint32_t size;
    uint32_t lines;
    uint8_t  is_gui;
};

/* ------------------------------------------------------------------------
 * Utilidades de cadena internas (seguras en Ring 0)
 * ------------------------------------------------------------------------ */

static uint32_t cdr_len(const char *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

/* Hash ligero para detectar si el agente dejó intacto el archivo original. */
static uint32_t cdr_hash_bytes(const char *data, uint32_t len)
{
    uint32_t i, h = 2166136261u;
    for (i = 0; data && i < len; ++i) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static void cdr_copy(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void cdr_cat(char *dst, uint32_t cap, const char *src)
{
    uint32_t n, i = 0;
    if (!dst || cap == 0) return;
    n = cdr_len(dst);
    if (n + 1 >= cap) return;
    while (src && src[i] && n + 1 < cap) dst[n++] = src[i++];
    dst[n] = '\0';
}

/* Añade como máximo max_add caracteres y siempre conserva el terminador NUL. */

static void cdr_cat_u32(char *dst, uint32_t cap, uint32_t v)
{
    char tmp[12];
    char out[12];
    int n = 0, i;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
    cdr_cat(dst, cap, out);
}


static int cdr_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int cdr_ends_with(const char *s, const char *suf)
{
    uint32_t ls = cdr_len(s), lf = cdr_len(suf);
    if (lf > ls) return 0;
    return cdr_eq(s + (ls - lf), suf);
}

static const char *cdr_find(const char *hay, const char *needle)
{
    if (!hay || !needle) return 0;
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}



/* SOMA_239_PYTHON_GRAFICO: el programador escribe SOMA-LANG (.soma) o Python (.py). cdr_lang es la eleccion de la pestana Nuevo;
 * para modificar manda la extension del archivo, y una tarea que dice "en Python" elige Python. */
static int cdr_lang;                                   /* CC_LANG_SOMA, CC_LANG_PYTHON o CC_LANG_C (SOMA_265_CC) */
static int cdr_is_py(const char *p) { return cdr_ends_with(p, ".py"); }
static int cdr_is_c(const char *p) { return cdr_ends_with(p, ".c"); }           /* SOMA_265_CC */
static int cdr_is_js(const char *p) { return cdr_ends_with(p, ".js"); }         /* SOMA_275_HTML */
static int cdr_is_html(const char *p) { return cdr_ends_with(p, ".html"); }     /* SOMA_275_HTML */
static int cdr_is_prog(const char *p) { return cdr_ends_with(p, ".soma") || cdr_ends_with(p, ".py") || cdr_ends_with(p, ".c") || cdr_is_js(p) || cdr_is_html(p); }
static int cdr_is_ext(const char *p) { return cdr_is_py(p) || cdr_is_c(p) || cdr_is_js(p) || cdr_is_html(p); }   /* SOMA_275_HTML: los que se comprueban con un proceso/comprobador externo */
static const char *cdr_ext_of(const char *p) { return cdr_is_html(p) ? ".html" : cdr_is_js(p) ? ".js" : cdr_is_c(p) ? ".c" : cdr_is_py(p) ? ".py" : ".soma"; }
static const char *cdr_lang_ext(int l) { return l == CC_LANG_HTML ? ".html" : l == CC_LANG_JS ? ".js" : l == CC_LANG_C ? ".c" : l == CC_LANG_PYTHON ? ".py" : ".soma"; }
static const char *cdr_ext_for(const char *mission)
{
    if (cdr_lang == CC_LANG_HTML) return ".html";
    if (cdr_lang == CC_LANG_JS) return ".js";
    if (cdr_lang == CC_LANG_C) return ".c";
    if (cdr_lang == CC_LANG_PYTHON || cc_mission_wants_python(mission)) return ".py";
    if (cc_mission_wants_html(mission)) return ".html";
    if (cc_mission_wants_js(mission)) return ".js";
    return cc_mission_wants_c(mission) ? ".c" : ".soma";
}
static void cdr_strip_ext(char *p) { uint32_t n = cdr_len(p); if (cdr_ends_with(p, ".soma")) p[n - 5] = 0; else if (cdr_is_html(p)) p[n - 5] = 0; else if (cdr_ends_with(p, ".py") || cdr_is_js(p)) p[n - 3] = 0; else if (cdr_ends_with(p, ".c")) p[n - 2] = 0; }
/* SOMA_275_HTML: comprobar/ejecutar un archivo del lenguaje que sea y leer su error, por la extension */
static const char *cdr_lang_label(const char *p) { return cdr_is_html(p) ? "HTML" : cdr_is_js(p) ? "JavaScript" : cdr_is_c(p) ? "C" : cdr_is_py(p) ? "Python" : "SOMA-LANG"; }
static uint32_t cdr_lang_color(const char *p) { return cdr_is_html(p) ? 0x00FB923Cu : cdr_is_js(p) ? 0x00F7DF1Eu : cdr_is_py(p) ? CDR_C_AMBER : cdr_is_c(p) ? CDR_C_NUM : CDR_C_CYAN; }
static int cdr_ext_src(const char *p, const char *src, uint32_t n, char *o, uint32_t cap)
{
    if (cdr_is_html(p)) return shell_html_source(1, src, n, "", o, cap, 8000);
    if (cdr_is_js(p)) return shell_js_source(1, src, n, "", o, cap, 8000);
    if (cdr_is_c(p)) return shell_c_source(1, src, n, "", o, cap, 8000);
    return shell_python_source(1, src, n, "", o, cap, 8000);
}
static void cdr_ext_err(const char *p, const char *o, uint32_t *ln, char *m, uint32_t cap)
{
    if (cdr_is_js(p)) cc_js_error(o, ln, m, cap);
    else if (cdr_is_c(p) || cdr_is_html(p)) cc_c_error(o, ln, m, cap);
    else cc_py_error(o, ln, m, cap);
}

static void cdr_clean_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t n = 0;
    int prev_sp = 1;
    if (!dst || cap == 0) return;
    while (src && *src && n + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c < 32 || c >= 127) continue;
        if (c == ' ') {
            if (prev_sp) continue;
            prev_sp = 1;
        } else {
            prev_sp = 0;
        }
        dst[n++] = (char)c;
    }
    while (n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

static void cdr_fmt_size(char *dst, uint32_t cap, uint32_t bytes)
{
    dst[0] = '\0';
    if (bytes < 1024u) {
        cdr_cat_u32(dst, cap, bytes);
        cdr_cat(dst, cap, " B");
        return;
    }
    cdr_cat_u32(dst, cap, bytes / 1024u);
    cdr_cat(dst, cap, ".");
    cdr_cat_u32(dst, cap, ((bytes % 1024u) * 10u) / 1024u);
    cdr_cat(dst, cap, " KB");
}

/* ------------------------------------------------------------------------
 * Bloqueo ligero (Spinlock Ring 0)
 * ------------------------------------------------------------------------ */
typedef struct {
    volatile int v;
} cdr_lock_t;

static inline void cdr_lock(cdr_lock_t *l)
{
    while (__atomic_test_and_set(&l->v, __ATOMIC_ACQUIRE)) {
        arch_pause();
    }
}

static inline void cdr_unlock(cdr_lock_t *l)
{
    __atomic_clear(&l->v, __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------------
 * Estado sincronizado del trabajo (GUI <-> Hilo Worker)
 * ------------------------------------------------------------------------ */
struct cdr_job {
    cdr_lock_t           lock;
    enum coder_job_state state;
    volatile int         cancel;
    int                  phase;
    int                  attempt;
    int                  lint_n;
    int                  lint_hard;
    int                  log_n;
    uint32_t             started_ms;
    uint32_t             ended_ms;
    char                 phase_desc[128];
    char                 live_thought[CDR_THOUGHT_MAX];
    char                 target_file[96];
    char                 mission[CDR_MISSION_MAX];
    char                 plan[CDR_PLAN_MAX];
    char                 lint[CDR_LINT_N][CDR_LINT_LEN];
    char                 log[CDR_LOG_N][CDR_LOG_LEN];
};

static struct cdr_job g_job = {
    .lock = {0},
    .state = CODER_JOB_IDLE,
    .phase = 0,
    .attempt = 0,
    .lint_n = 0,
    .lint_hard = 0,
    .log_n = 0,
    .started_ms = 0,
    .ended_ms = 0,
    .phase_desc = "Listo para crear o modificar aplicaciones (.soma o .py)",
    .live_thought = "",
    .target_file = "",
    .mission = "",
    .plan = ""
};

struct cdr_snap {
    enum coder_job_state state;
    int      phase;
    int      attempt;
    int      lint_n;
    int      lint_hard;
    int      log_n;
    uint32_t elapsed_s;
    char     phase_desc[128];
    char     live_thought[CDR_THOUGHT_MAX];
    char     target_file[96];
    char     plan[CDR_PLAN_MAX];
    char     lint[CDR_LINT_N][CDR_LINT_LEN];
    char     log[CDR_LOG_N][CDR_LOG_LEN];
};

static void cdr_snapshot(struct cdr_snap *s)
{
    int i;
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    cdr_lock(&g_job.lock);
    s->state = g_job.state;
    s->phase = g_job.phase;
    s->attempt = g_job.attempt;
    s->lint_n = g_job.lint_n;
    s->lint_hard = g_job.lint_hard;
    s->log_n = g_job.log_n;
    if (g_job.started_ms) {
        uint32_t end = (g_job.state == CODER_JOB_WORKING || g_job.ended_ms == 0) ? now : g_job.ended_ms;
        s->elapsed_s = (end >= g_job.started_ms) ? (end - g_job.started_ms) / 1000u : 0u;
    } else {
        s->elapsed_s = 0;
    }
    cdr_copy(s->phase_desc, sizeof(s->phase_desc), g_job.phase_desc);
    cdr_copy(s->live_thought, sizeof(s->live_thought), g_job.live_thought);
    cdr_copy(s->target_file, sizeof(s->target_file), g_job.target_file);
    cdr_copy(s->plan, sizeof(s->plan), g_job.plan);
    for (i = 0; i < CDR_LINT_N; ++i) cdr_copy(s->lint[i], CDR_LINT_LEN, g_job.lint[i]);
    for (i = 0; i < CDR_LOG_N; ++i) cdr_copy(s->log[i], CDR_LOG_LEN, g_job.log[i]);
    cdr_unlock(&g_job.lock);
}

static void cdr_set_state(enum coder_job_state st, int phase, const char *desc, const char *thought)
{
    cdr_lock(&g_job.lock);
    g_job.state = st;
    g_job.phase = phase;
    if (desc) cdr_copy(g_job.phase_desc, sizeof(g_job.phase_desc), desc);
    if (thought) cdr_copy(g_job.live_thought, sizeof(g_job.live_thought), thought);
    if (st != CODER_JOB_WORKING) g_job.ended_ms = (uint32_t)timer_get_uptime_ms();
    cdr_unlock(&g_job.lock);
}

static void cdr_log(const char *msg)
{
    char line[CDR_LOG_LEN];
    uint32_t i;
    line[0] = '\0';
    if (g_job.state == CODER_JOB_WORKING && g_job.started_ms) {
        uint32_t el = ((uint32_t)timer_get_uptime_ms() - g_job.started_ms) / 1000u;
        cdr_copy(line, sizeof(line), "+");
        cdr_cat_u32(line, sizeof(line), el);
        cdr_cat(line, sizeof(line), "s ");
    }
    cdr_cat(line, sizeof(line), msg);
    cdr_lock(&g_job.lock);
    if (g_job.log_n >= CDR_LOG_N) {
        for (i = 0; i + 1 < CDR_LOG_N; ++i) cdr_copy(g_job.log[i], CDR_LOG_LEN, g_job.log[i + 1]);
        g_job.log_n = CDR_LOG_N - 1;
    }
    cdr_copy(g_job.log[g_job.log_n], CDR_LOG_LEN, line);
    g_job.log_n++;
    cdr_unlock(&g_job.lock);
}

/* ------------------------------------------------------------------------
 * Estado de la interfaz de usuario
 * ------------------------------------------------------------------------ */
static int      cdr_inited = 0;
static enum cdr_tab cdr_tab_cur = CDR_TAB_EDITOR;
static const struct { const char *t; uint32_t w; enum cdr_tab tab; int icon; } cdr_tabs[] = {   /* PARCHE 093; SOMA_240_ICONOS: con icono */
    { "EDITOR", 114, CDR_TAB_EDITOR, SI_CODE }, { "CREAR CON IA", 146, CDR_TAB_NEW, SI_AUTO }, { "PROYECTOS", 126, CDR_TAB_PROJECTS, SI_GRID },
    { "REGISTRO", 118, CDR_TAB_LOG, SI_LIST } };
#define CDR_NTABS 4
static void cdr_open_in_editor(const char *path);
static int ide_inited;
static int coder_job_busy(void);

static char     input_create_prompt[384] = "";
static uint32_t input_create_len = 0;
static char     input_create_file[96] = "";
static uint32_t input_create_file_len = 0;

static char     input_mod_prompt[384] = "Anade un boton de reset y cambia el color del fondo";
static uint32_t input_mod_len = 0;
static char     mod_current_file[80] = "/workspace/calc.soma";
static char     mod_code[CDR_SRC_MAX];
static int      mod_loaded = 0;
static int      mod_scroll = 0;
static int      mod_total_lines = 0;
static int      mod_err_line = 0;
static uint32_t mod_size = 0;

static char     cdr_check_msg[160];
static uint32_t cdr_check_col = 0x0094A3B8u;

static struct cdr_proj projects[CDR_MAX_PROJECTS];
static int      project_count = 0;
static int      project_selected = -1;
static int      proj_del_armed = -1;

static volatile int g_need_rescan = 0;
static volatile int g_need_reload = 0;

/* Buffers de trabajo estaticos */
static char     cdr_src[CDR_SRC_MAX];
#define CDR_BC_MAX 262144u               /* SOMA_264_LANG: antes 32 KB. SOMA_323_BSS: se reserva al compilar y se libera */
static uint32_t cdr_bc_len = 0;
static uint32_t cdr_orig_len = 0;
static uint32_t cdr_orig_hash = 0;
static int cdr_target_unchanged = 0;
static int cdr_target_preexisted = 0;
static struct soma_lint_report cdr_rep;
static struct soma_compile_diag cdr_diag;
static int      cdr_diag_valid = 0;

static char     cdr_list_buf[8192];
static char     cdr_probe[CDR_SRC_MAX];

static char     cdr_w_target[96];
static char     cdr_w_mission[CDR_MISSION_MAX];
static char     cdr_plan_w[CDR_PLAN_MAX];
static char     cdr_report[2048];

static void cdr_sync_inputs(void)
{
    input_create_len = cdr_len(input_create_prompt);
    input_create_file_len = cdr_len(input_create_file);
    input_mod_len = cdr_len(input_mod_prompt);
}

static void cdr_ensure_init(void)
{
    if (cdr_inited) return;
    cdr_inited = 1;
    cdr_sync_inputs();
}

static void cdr_set_check(const char *msg, uint32_t color)
{
    cdr_copy(cdr_check_msg, sizeof(cdr_check_msg), msg);
    cdr_check_col = color;
}

/* ------------------------------------------------------------------------
 * Verificación (compilador + lint), copia de seguridad y seguridad de rutas
 * ------------------------------------------------------------------------ */
static int cdr_verify(const char *path)
{
    int r;
    uint32_t bl = 0;
    cdr_diag_valid = 0;
    cdr_rep.count = 0;
    cdr_rep.hard = 0;
    cdr_bc_len = 0;
    r = vfs_read(path, cdr_src, CDR_SRC_MAX - 1);
    if (r <= 0) return -1;
    cdr_src[r] = '\0';
    if (cdr_is_ext(path)) {                                  /* SOMA_239: Python se compila de verdad en su proceso (python -s); SOMA_265_CC: C con cc -s; SOMA_275_HTML: JS (js -s) y HTML (html comprobar) */
        static char o[1024];
        int pr = cdr_ext_src(path, cdr_src, (uint32_t)r, o, sizeof o);
        if (pr == 0) { cdr_bc_len = (uint32_t)r; return 1; }
        uint32_t ln = 0; char m[96]; m[0] = 0;
        cdr_ext_err(path, o, &ln, m, sizeof m);
        cdr_diag.line = ln; cdr_copy(cdr_diag.msg, sizeof cdr_diag.msg, m[0] ? m : "no se pudo comprobar con el compilador del lenguaje");
        cdr_diag_valid = 1;
        return 0;
    }
    lang_lint(cdr_src, (uint32_t)r, &cdr_rep);
    uint8_t *cdr_bc = (uint8_t *)kmalloc(CDR_BC_MAX);
    if (!cdr_bc) { cdr_diag.line = 0; cdr_copy(cdr_diag.msg, sizeof cdr_diag.msg, "sin memoria para compilar"); cdr_diag_valid = 1; return 0; }
    int crc = lang_compile(cdr_src, (uint32_t)r, cdr_bc, CDR_BC_MAX, &bl);
    kfree(cdr_bc);
    if (crc != 0) {
        lang_get_diagnostic(&cdr_diag);
        cdr_diag_valid = 1;
        return 0;
    }
    cdr_bc_len = bl;
    /* Un bytecode compilable no basta si el lint detecta errores graves. */
    if (cdr_rep.hard > 0) return 0;
    return 1;
}

static void cdr_publish_lint(void)
{
    int i;
    cdr_lock(&g_job.lock);
    g_job.lint_n = (cdr_rep.count > CDR_LINT_N) ? CDR_LINT_N : cdr_rep.count;
    g_job.lint_hard = cdr_rep.hard;
    for (i = 0; i < g_job.lint_n; ++i) cdr_copy(g_job.lint[i], CDR_LINT_LEN, cdr_rep.msg[i]);
    cdr_unlock(&g_job.lock);
}

static void cdr_diag_text(char *dst, uint32_t cap)
{
    int i;
    dst[0] = '\0';
    if (cdr_diag_valid) {
        cdr_copy(dst, cap, "L");
        cdr_cat_u32(dst, cap, (uint32_t)cdr_diag.line);
        cdr_cat(dst, cap, ": ");
        cdr_cat(dst, cap, cdr_diag.msg);
        return;
    }
    if (cdr_rep.hard > 0) {
        for (i = 0; i < (int)cdr_rep.count; ++i) {
            if (cdr_rep.msg[i][0] == 'E') {
                cdr_copy(dst, cap, cdr_rep.msg[i]);
                return;
            }
        }
        cdr_copy(dst, cap, "El lint ha detectado errores graves.");
        return;
    }
    cdr_copy(dst, cap, "error de compilacion");
}

static int cdr_backup_save(const char *target)
{
    char hdr[112];
    uint32_t hl, i;
    int r;
    cdr_orig_len = 0;
    cdr_orig_hash = 0;
    cdr_target_unchanged = 0;
    cdr_target_preexisted = 0;
    hdr[0] = '\0';
    cdr_copy(hdr, sizeof(hdr), "//BAK:");
    cdr_cat(hdr, sizeof(hdr), target);
    cdr_cat(hdr, sizeof(hdr), "\n");
    hl = cdr_len(hdr);
    r = vfs_read(target, cdr_src + 128, CDR_SRC_MAX - 129);
    if (r < 0) return 0; /* No existe: un archivo nuevo podrá eliminarse en rollback. */
    cdr_target_preexisted = 1;
    if (r == 0) {
        /* También respaldamos archivos vacíos; el encabezado por sí solo es válido. */
        if (vfs_write(CDR_BAK_FILE, hdr, hl) < 0) return -1;
        return 1;
    }
    if (r >= (int)(CDR_SRC_MAX - 129)) return -1;
    cdr_src[128 + r] = '\0';
    cdr_orig_len = (uint32_t)r;
    cdr_orig_hash = cdr_hash_bytes(cdr_src + 128, (uint32_t)r);
    for (i = 0; i < hl; ++i) cdr_src[128 - hl + i] = hdr[i];
    if (vfs_write(CDR_BAK_FILE, cdr_src + 128 - hl, hl + (uint32_t)r) < 0) return -1;
    return 1;
}

static int cdr_backup_restore(const char *target)
{
    char hdr[112];
    uint32_t hl, i;
    int r;
    hdr[0] = '\0';
    cdr_copy(hdr, sizeof(hdr), "//BAK:");
    cdr_cat(hdr, sizeof(hdr), target);
    cdr_cat(hdr, sizeof(hdr), "\n");
    hl = cdr_len(hdr);
    r = vfs_read(CDR_BAK_FILE, cdr_src, CDR_SRC_MAX - 1);
    if (r < (int)hl) return 0;
    for (i = 0; i < hl; ++i) {
        if (cdr_src[i] != hdr[i]) return 0;
    }
    return (vfs_write(target, cdr_src + hl, (uint32_t)r - hl) >= 0) ? 1 : 0;
}

static int cdr_target_matches_backup(const char *target)
{
    int r;
    if (!target || !*target || cdr_orig_len == 0) return 0;
    r = vfs_read(target, cdr_src, CDR_SRC_MAX - 1);
    if (r < 0) return 0;
    cdr_src[r] = '\0';
    return ((uint32_t)r == cdr_orig_len &&
            cdr_hash_bytes(cdr_src, (uint32_t)r) == cdr_orig_hash);
}

static int cdr_verify_after_change(const char *target)
{
    int i;
    if (cdr_orig_len > 0 && cdr_target_matches_backup(target)) {
        cdr_target_unchanged = 1;
        cdr_diag_valid = 0;
        cdr_bc_len = 0;
        cdr_rep.count = 0;
        cdr_rep.hard = 0;
        for (i = 0; i < 8; ++i) cdr_rep.msg[i][0] = '\0';
        cdr_log("El archivo objetivo sigue idéntico a la versión inicial; se solicitará otra reparación.");
        return 0;
    }
    cdr_target_unchanged = 0;
    return cdr_verify(target);
}

static int cdr_path_allowed(const char *p)
{
    static const char *const deny[] = { "/etc/", "/agent/", "/sys/", "/proc/", "/dev/", "/tmp/", "/mem/", 0 };
    const char *seg, *q;
    int i;
    if (!p || p[0] != '/' || p[1] == '\0') return 0;
    for (i = 0; deny[i]; ++i) {
        uint32_t k = 0;
        while (deny[i][k] && p[k] == deny[i][k]) k++;
        if (!deny[i][k]) return 0;
    }
    /* Evita traversal, segmentos vacíos y separadores alternativos en rutas VFS. */
    seg = p + 1;
    for (q = seg; ; ++q) {
        if (*q == '\\') return 0;
        if (*q == '/' || *q == '\0') {
            uint32_t sl = (uint32_t)(q - seg);
            if (sl == 0 || (sl == 1 && seg[0] == '.') ||
                (sl == 2 && seg[0] == '.' && seg[1] == '.')) return 0;
            if (*q == '\0') break;
            seg = q + 1;
        }
    }
    return 1;
}

/* SOMA_363_ACCESO: los programas nuevos van a /usuario/Proyectos (sin unidad del usuario, a /workspace como antes) */
static const char *cdr_base(void)
{
    static char b[64];
    if (!acceso_usuario() || acceso_carpeta(ACC_PROYECTOS, b, sizeof b - 1) != 0) return "/workspace/";
    uint32_t n = 0; while (b[n]) n++;
    b[n] = '/'; b[n + 1] = 0;
    return b;
}
static int cdr_normalize_target(const char *in, char *out, uint32_t cap)
{
    uint32_t i = 0, n;
    while (in[i] == ' ') i++;
    if (!in[i]) return -1;
    out[0] = '\0';
    if (in[i] != '/') cdr_copy(out, cap, cdr_base());
    n = cdr_len(out);
    while (in[i] && n + 1 < cap) {
        out[n++] = (in[i] == ' ') ? '_' : in[i];
        i++;
    }
    out[n] = '\0';
    while (n > 0 && out[n - 1] == '_') out[--n] = '\0';
    if (!cdr_is_prog(out)) {
        const char *x = cdr_ext_for("");
        if (cdr_len(out) + cdr_len(x) >= cap) return -2;
        cdr_cat(out, cap, x);
    }
    if (!cdr_is_prog(out)) return -2;
    if (cdr_len(out) >= (uint32_t)FS_NAME_MAX) return -2;
    if (!cdr_path_allowed(out)) return -3;
    return 1;
}

/* ------------------------------------------------------------------------
 * Proyectos y explorador
 * ------------------------------------------------------------------------ */
static void cdr_scan_projects(void)
{
    const char *p;
    int len;
    project_count = 0;
    len = vfs_format_list_ext(1, 0, cdr_list_buf, sizeof(cdr_list_buf) - 1);
    if (len <= 0) return;
    if (len >= (int)sizeof(cdr_list_buf)) len = (int)sizeof(cdr_list_buf) - 1;
    cdr_list_buf[len] = '\0';

    p = cdr_list_buf;
    if (p[0] == 'A' && p[1] == 'r') {
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
    }

    while (*p && project_count < CDR_MAX_PROJECTS) {
        char path[80];
        uint32_t pl = 0;
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        while (*p && *p != ',' && *p != ' ' && pl + 1 < sizeof(path)) path[pl++] = *p++;
        path[pl] = '\0';
        while (*p && *p != ',' && *p != ' ') p++;

        int dup = 0;                                     /* PARCHE 094: el listado puede repetir archivos (RamFS + disco) */
        for (int q = 0; q < project_count; q++) if (cdr_eq(projects[q].path, path)) dup = 1;
        if (!dup && cdr_is_prog(path)) {                     /* SOMA_239: tambien .py */
            struct cdr_proj *it = &projects[project_count];
            const char *last = path;
            const char *q = path;
            int r;
            uint32_t lc = 0;
            int k;

            while (*q) { if (*q == '/') last = q + 1; q++; }
            cdr_copy(it->path, sizeof(it->path), path);
            cdr_copy(it->name, sizeof(it->name), last);
            r = vfs_read(path, cdr_probe, CDR_SRC_MAX - 1);
            if (r >= 0) {
                cdr_probe[r] = '\0';
                it->size = (uint32_t)r;
                for (k = 0; k < r; ++k) if (cdr_probe[k] == '\n') lc++;
                if (r > 0 && cdr_probe[r - 1] != '\n') lc++;
                it->lines = lc;
                it->is_gui = (cdr_find(cdr_probe, "win.") != 0 || cdr_find(cdr_probe, "fb.") != 0 || cdr_find(cdr_probe, "soma_win_") != 0) ? 1 : 0;
            } else {
                it->size = 0;
                it->lines = 0;
                it->is_gui = 0;
            }
            project_count++;
        }
    }
    if (project_selected >= project_count) project_selected = project_count - 1;
}

static void cdr_load_mod_file(const char *path)
{
    int r;
    uint32_t k;
    if (!path || !*path) return;
    cdr_copy(mod_current_file, sizeof(mod_current_file), path);
    r = vfs_read(path, mod_code, CDR_SRC_MAX - 1);
    mod_total_lines = 0;
    mod_size = 0;
    if (r >= 0) {
        mod_code[r] = '\0';
        mod_loaded = 1;
        mod_size = (uint32_t)r;
        for (k = 0; k < (uint32_t)r; ++k) if (mod_code[k] == '\n') mod_total_lines++;
        if (r > 0 && mod_code[r - 1] != '\n') mod_total_lines++;
    } else {
        cdr_copy(mod_code, sizeof(mod_code), "// No se pudo cargar el archivo");
        mod_loaded = 0;
        mod_total_lines = 1;
    }
    mod_scroll = 0;
    mod_err_line = 0;
}

static void cdr_poll_pending(void)
{
    if (g_need_rescan) {
        g_need_rescan = 0;
        cdr_scan_projects();
    }
    if (g_need_reload) {
        char t[96];
        g_need_reload = 0;
        cdr_lock(&g_job.lock);
        cdr_copy(t, sizeof(t), g_job.target_file);
        cdr_unlock(&g_job.lock);
        cdr_load_mod_file(t);
    }
}

/* ------------------------------------------------------------------------
 * Pipeline agéntico multipaso (Hilo trabajador en segundo plano)
 * ------------------------------------------------------------------------ */
static int cdr_cancelled(void)
{
    return g_job.cancel ? 1 : 0;
}




static void cdr_write_report(const char *target, int ok, int repairs, const char *summary)
{
    int i;
    cdr_report[0] = '\0';
    cdr_copy(cdr_report, sizeof(cdr_report), "SOMA CODER :: INFORME DEL ULTIMO TRABAJO\nArchivo: ");
    cdr_cat(cdr_report, sizeof(cdr_report), target);
    cdr_cat(cdr_report, sizeof(cdr_report), "\nResultado: ");
    cdr_cat(cdr_report, sizeof(cdr_report), ok ? "OK" : "ERROR");
    cdr_cat(cdr_report, sizeof(cdr_report), "\nReparaciones: ");
    cdr_cat_u32(cdr_report, sizeof(cdr_report), (uint32_t)repairs);
    cdr_cat(cdr_report, sizeof(cdr_report), "\nResumen: ");
    cdr_cat(cdr_report, sizeof(cdr_report), summary);
    cdr_cat(cdr_report, sizeof(cdr_report), "\nPlan: ");
    cdr_cat(cdr_report, sizeof(cdr_report), cdr_plan_w[0] ? cdr_plan_w : "(sin plan)");
    cdr_cat(cdr_report, sizeof(cdr_report), "\nRevision estatica:\n");
    for (i = 0; i < (int)cdr_rep.count; ++i) {
        cdr_cat(cdr_report, sizeof(cdr_report), "  ");
        cdr_cat(cdr_report, sizeof(cdr_report), cdr_rep.msg[i]);
        cdr_cat(cdr_report, sizeof(cdr_report), "\n");
    }
    if (cdr_rep.count == 0) cdr_cat(cdr_report, sizeof(cdr_report), "  (sin avisos)\n");
    vfs_write(CDR_REPORT_FILE, cdr_report, cdr_len(cdr_report));
}

static int cdr_force_new;
/* PARCHE 096: adaptadores del entorno del nucleo agentico */
static int cdr_env_llm(const char *prompt, char *out, uint32_t max, uint32_t to) { llm_trace_tag("programador"); return llm_chat(prompt, out, max, to); }   /* SOMA_251: traza */
static int cdr_env_read(const char *path, char *buf, uint32_t max) { return vfs_read(path, buf, max); }
static int cdr_env_write(const char *path, const char *buf, uint32_t len) { return vfs_write(path, buf, len); }
static void cdr_env_status(int phase, const char *desc, const char *thought) { cdr_set_state(CODER_JOB_WORKING, phase, desc, thought); }

/* SOMA_239_PYTHON_GRAFICO: Python se comprueba y se prueba en un proceso de ring 3 (python -s / -x), sin escribir el archivo */
static int cdr_env_py_check(const char *src, uint32_t n, char *out, uint32_t max) { return shell_python_source(1, src, n, "", out, max, 8000); }
static int cdr_env_py_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max) { return shell_python_source(0, src, n, input, out, max, 5000); }
/* SOMA_265_CC: C igual, con el compilador de C (cc -s / cc -x) */
static int cdr_env_c_check(const char *src, uint32_t n, char *out, uint32_t max) { return shell_c_source(1, src, n, "", out, max, 8000); }
static int cdr_env_c_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max) { return shell_c_source(0, src, n, input, out, max, 5000); }
/* SOMA_275_HTML: JavaScript (js -s / js -x) y HTML (comprobar; no hay nada que ejecutar) */
static int cdr_env_js_check(const char *src, uint32_t n, char *out, uint32_t max) { return shell_js_source(1, src, n, "", out, max, 8000); }
static int cdr_env_js_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max) { return shell_js_source(0, src, n, input, out, max, 5000); }
static int cdr_env_html_check(const char *src, uint32_t n, char *out, uint32_t max) { return shell_html_source(1, src, n, "", out, max, 8000); }
static int cdr_env_html_run(const char *src, uint32_t n, const char *input, char *out, uint32_t max) { return shell_html_source(0, src, n, input, out, max, 5000); }
static void cdr_env_python(struct cc_env *env, const char *target)
{
    if (cdr_is_html(target)) { env->lang = CC_LANG_HTML; env->check_src = cdr_env_html_check; env->run_src = cdr_env_html_run; return; }
    if (cdr_is_js(target)) { env->lang = CC_LANG_JS; env->check_src = cdr_env_js_check; env->run_src = cdr_env_js_run; return; }
    if (cdr_is_c(target)) { env->lang = CC_LANG_C; env->check_src = cdr_env_c_check; env->run_src = cdr_env_c_run; return; }
    if (!cdr_is_py(target)) return;
    env->lang = CC_LANG_PYTHON; env->check_src = cdr_env_py_check; env->run_src = cdr_env_py_run;
}

static void coder_worker_thread(void *arg)
{
    int ok = 0, cancelled = 0, vr = -1, attempt = 0, backup_state;
    char desc[128];
    char tmp[96];
    (void)arg;

    cdr_lock(&g_job.lock);
    cdr_copy(cdr_w_target, sizeof(cdr_w_target), g_job.target_file);
    cdr_copy(cdr_w_mission, sizeof(cdr_w_mission), g_job.mission);
    g_job.cancel = 0;
    g_job.attempt = 0;
    g_job.started_ms = (uint32_t)timer_get_uptime_ms();
    g_job.ended_ms = 0;
    g_job.lint_n = 0;
    g_job.lint_hard = 0;
    g_job.plan[0] = '\0';
    cdr_unlock(&g_job.lock);

    backup_state = cdr_backup_save(cdr_w_target);
    if (backup_state < 0) {
        cdr_set_state(CODER_JOB_ERROR, 0,
                      "No se pudo crear una copia de seguridad; operación detenida.",
                      "No se modificó el archivo porque no hay rollback seguro.");
        cdr_log("Copia de seguridad fallida; el pipeline no modificará el archivo.");
        cdr_write_report(cdr_w_target, 0, 0, "Copia de seguridad fallida; operación detenida sin escribir código.");
        g_need_rescan = 1;
        g_need_reload = 1;
        return;
    }
    if (backup_state > 0) cdr_log("Copia de seguridad creada en /tmp/coder_prev.bak");

    /* PARCHE 096: nucleo agentico nuevo (coder_core.c): generar -> normalizar -> compilar/lint -> reparar con el diagnostico,
     * mejor candidato, revision critica y una sola escritura final. El archivo no se toca hasta tener un programa correcto. */
    {
        static struct cc_result cr;
        struct cc_env env;
        /* PARCHE 100: la estructura se pone a cero ENTERA. env.trace quedaba sin inicializar (basura de la pila) y el nucleo
         * saltaba a una direccion invalida: KERNEL PANIC con RIP=0xCCCCCCCD en el hilo coder_bg. */
        for (uint32_t zi = 0; zi < sizeof env; zi++) ((uint8_t *)&env)[zi] = 0;
        env.force_new = cdr_force_new;
        env.llm = cdr_env_llm; env.read = cdr_env_read; env.write = cdr_env_write;
        env.log = cdr_log; env.status = cdr_env_status; env.cancelled = cdr_cancelled; env.max_llm_calls = 7;
        cdr_env_python(&env, cdr_w_target);                                    /* SOMA_239 */
        cc_run(&env, cdr_w_target, cdr_w_mission, &cr);
        attempt = cr.repairs + cr.regenerations;
        cdr_lock(&g_job.lock);
        cdr_copy(g_job.plan, CDR_PLAN_MAX, cr.plan);
        g_job.attempt = attempt;
        cdr_unlock(&g_job.lock);
        cdr_copy(cdr_plan_w, sizeof(cdr_plan_w), cr.plan);
        {
            char m[CDR_LOG_LEN]; m[0] = 0;
            cdr_cat(m, sizeof m, "Metricas: "); cdr_cat_u32(m, sizeof m, (uint32_t)cr.llm_calls); cdr_cat(m, sizeof m, " llamadas, ");
            cdr_cat_u32(m, sizeof m, (uint32_t)cr.repairs); cdr_cat(m, sizeof m, " reparaciones, ");
            cdr_cat_u32(m, sizeof m, (uint32_t)cr.auto_fixes); cdr_cat(m, sizeof m, " correcciones automaticas");
            cdr_log(m);
        }
        if (cr.cancelled) { cancelled = 1; goto done; }
        vr = cdr_verify_after_change(cdr_w_target);
        cdr_publish_lint();
        ok = cr.ok && vr > 0;
        if (!ok && cr.wrote && backup_state > 0) {            /* no deberia pasar: escrito pero no verifica en el kernel */
            cdr_log("La verificacion final no coincide: se restaura la copia.");
            cdr_backup_restore(cdr_w_target);
        }
        if (!ok && !cr.wrote) {
            cdr_copy(cdr_check_msg, sizeof(cdr_check_msg), cr.last_error);
            cdr_lock(&g_job.lock);
            cdr_copy(g_job.phase_desc, sizeof(g_job.phase_desc), cr.last_error[0] ? cr.last_error : cr.summary);
            cdr_unlock(&g_job.lock);
        }
        if (!ok) {
            cdr_set_state(CODER_JOB_ERROR, 6, cr.last_error[0] ? cr.last_error : cr.summary, cr.summary);
            cdr_write_report(cdr_w_target, 0, attempt, cr.summary);
            g_need_rescan = 1; g_need_reload = 1;
            return;
        }
    }

done:
    /* FASE 6: Entrega final */
    if (cancelled) {
        if (backup_state > 0) {
            if (cdr_backup_restore(cdr_w_target)) cdr_log("Rollback aplicado tras la cancelación.");
            else cdr_log("No se pudo restaurar la copia tras la cancelación.");
        } else if (!cdr_target_preexisted) {
            if (vfs_delete(cdr_w_target) == 0) cdr_log("Archivo nuevo eliminado tras la cancelación.");
        }
        cdr_set_state(CODER_JOB_ERROR, 6, "Cancelado por el usuario.", "Operación abortada; rollback intentado.");
        cdr_log("Trabajo cancelado por el usuario.");
        cdr_write_report(cdr_w_target, 0, attempt, "Cancelado.");
    } else if (ok) {
        desc[0] = '\0';
        if (cdr_is_py(cdr_w_target)) cdr_copy(desc, sizeof(desc), "Fase 6/6: Programa Python verificado con exito (compila en SOMA, 0 errores).");
        else if (cdr_is_c(cdr_w_target)) cdr_copy(desc, sizeof(desc), "Fase 6/6: Programa en C verificado con exito (compila en SOMA, 0 errores).");
        else if (cdr_is_js(cdr_w_target)) cdr_copy(desc, sizeof(desc), "Fase 6/6: Programa en JavaScript verificado con exito (sin errores de sintaxis).");
        else if (cdr_is_html(cdr_w_target)) cdr_copy(desc, sizeof(desc), "Fase 6/6: Pagina web verificada con exito (etiquetas, enlaces y scripts correctos).");
        else {
        cdr_copy(desc, sizeof(desc), "Fase 6/6: Programa verificado con exito (");
        cdr_cat_u32(desc, sizeof(desc), cdr_bc_len);
        cdr_cat(desc, sizeof(desc), " B bytecode, 0 errores).");
        }
        cdr_set_state(CODER_JOB_SUCCESS, 6, desc,
                      "Codigo valido en Ring 0. Listo para ejecutar con '> PROBAR AHORA'.");
        cdr_log("Programa compilado y listo para produccion.");
        cdr_write_report(cdr_w_target, 1, attempt, "Compilado sin errores.");
    } else {
        tmp[0] = '\0';
        cdr_diag_text(tmp, sizeof(tmp));
        cdr_set_state(CODER_JOB_ERROR, 6, tmp,
                      "No se pudo compilar el resultado tras las rondas de reparacion.");
        cdr_log("El programa no compila.");
        cdr_write_report(cdr_w_target, 0, attempt, tmp);
    }

    g_need_rescan = 1;
    g_need_reload = 1;
}

/* PARCHE 098: las apps se lanzan SIEMPRE en su propio hilo (con buffer de salida, el despachador las pone en segundo plano);
 * antes se ejecutaban dentro del hilo del escritorio y un bucle de juego lo congelaba todo. */
static void ide_say(const char *m, uint32_t col);
static int  ide_focus;           /* 0 editor, 1 campo IA, 3 entrada del panel Salida (SOMA_333_SALIDA) */
static char ide_out_in[160]; static int ide_out_in_len, ide_out_scroll;   /* SOMA_333_SALIDA: lo que se escribe para el programa */
static char ide_out_path[128];                                               /* lo ultimo que se ejecuto ("Otra vez") */
static int  ide_out_y, ide_out_h, ide_out_x, ide_out_w, ide_out_rows;
static void cdr_run_app(const char *path)
{
    static char out[512]; char cmd[160];
    if (cdr_is_html(path)) {                                  /* SOMA_275_HTML: la pagina se abre en el navegador; SOMA_333_SALIDA: sin Terminal */
        cdr_copy(cmd, sizeof cmd, "html abrir "); cdr_cat(cmd, sizeof cmd, path);
        out[0] = 0; dispatch_command(cmd, out, sizeof out);
        return;
    }
    if (cdr_is_ext(path)) {                                   /* SOMA_333_SALIDA: Python, C y JavaScript en el panel Salida del programador (antes, el Terminal) */
        cdr_copy(cmd, sizeof cmd, cdr_is_js(path) ? "js " : cdr_is_c(path) ? "cc -run " : "python "); cdr_cat(cmd, sizeof cmd, path);
        const char *nm = path; for (const char *q = path; *q; q++) if (*q == '/') nm = q + 1;
        cdr_copy(ide_out_path, sizeof ide_out_path, path);
        int r = crun_start(cmd, nm);
        if (r == -1) ide_say("Ya hay un programa en marcha: paralo (Esc o Parar en el panel Salida) antes de lanzar otro.", CDR_C_AMBER);
        else if (r < 0) ide_say("No se puede ejecutar ahora: el Terminal tiene una orden en marcha.", CDR_C_RED);
        else { ide_focus = 3; ide_out_in_len = 0; ide_out_in[0] = 0; ide_out_scroll = 0; }
        return;
    }
    cdr_copy(cmd, sizeof cmd, "app "); cdr_cat(cmd, sizeof cmd, path);
    out[0] = 0;
    dispatch_command(cmd, out, sizeof out);
}
static int cdr_has_ci(const char *h, const char *n);
static void cdr_launch_job(const char *target, const char *mission)
{
    char norm[96];
    int r;
    struct tcb *t;

    r = cdr_normalize_target(target, norm, sizeof(norm));
    if (r == -1) { cdr_set_check("Ruta de archivo vacia.", CDR_C_RED); return; }
    if (r == -2) { cdr_set_check("Ruta demasiado larga para VFS.", CDR_C_RED); return; }
    if (r == -3) { cdr_set_check("Ruta protegida del sistema.", CDR_C_RED); return; }

    cdr_lock(&g_job.lock);
    if (g_job.state == CODER_JOB_WORKING) {
        cdr_unlock(&g_job.lock);
        return;
    }
    cdr_copy(g_job.target_file, sizeof(g_job.target_file), norm);
    cdr_copy(g_job.mission, sizeof(g_job.mission), mission);
    g_job.state = CODER_JOB_WORKING;
    g_job.phase = 0;
    g_job.cancel = 0;
    g_job.log_n = 0;
    cdr_unlock(&g_job.lock);

    cdr_log("Iniciando nuevo pipeline del Coder...");
    t = thread_create("coder_bg", coder_worker_thread, 0);
    if (!t) {
        cdr_set_state(CODER_JOB_ERROR, 0,
                      "Error: no se pudo instanciar hilo de ejecucion.",
                      "Tabla de hilos saturada.");
    }
}

static void coder_start_creation(void)
{
    cdr_sync_inputs();
    if (input_create_len == 0) return;
    /* PARCHE 098: si la ruta es la de ejemplo (calc.soma) o ya existe otro programa con ese nombre, el nombre sale de la mision
     * y nunca se pisa un programa existente: crear es crear, no modificar la calculadora. */
    {
        int deflt = !input_create_file[0] || (cdr_eq(input_create_file, "/workspace/calc.soma") && !cdr_has_ci(input_create_prompt, "calc"));
        if (input_create_file[0] && input_create_file[0] != '/') { char t[96]; cdr_copy(t, sizeof t, cdr_base()); cdr_cat(t, sizeof t, input_create_file); cdr_copy(input_create_file, sizeof input_create_file, t); }
        if (!deflt && !cdr_is_prog(input_create_file)) cdr_cat(input_create_file, sizeof input_create_file, cdr_ext_for(input_create_prompt));
        if (deflt || vfs_size(input_create_file) > 0) {
            char t[96], base[96];
            if (deflt) coder_pick_target(input_create_prompt, base, sizeof base); else cdr_copy(base, sizeof base, input_create_file);
            cdr_copy(t, sizeof t, base);
            for (uint32_t k = 2; vfs_size(t) > 0 && k < 100; k++) {
                const char *x = cdr_ext_of(base);
                cdr_copy(t, sizeof t, base); cdr_strip_ext(t);
                cdr_cat(t, sizeof t, "_"); cdr_cat_u32(t, sizeof t, k); cdr_cat(t, sizeof t, x);
            }
            cdr_copy(input_create_file, sizeof input_create_file, t);
            input_create_file_len = cdr_len(input_create_file);
            char m[128]; cdr_copy(m, sizeof m, "Archivo nuevo: "); cdr_cat(m, sizeof m, t); cdr_log(m);
        }
    }
    cdr_force_new = 1;
    cdr_launch_job(input_create_file, input_create_prompt);
}

static void coder_start_modification(void)
{
    cdr_force_new = 0;
    cdr_sync_inputs();
    if (input_mod_len == 0) return;
    cdr_launch_job(mod_current_file, input_mod_prompt);
}

static void coder_cancel_job(void)
{
    cdr_lock(&g_job.lock);
    if (g_job.state == CODER_JOB_WORKING) {
        g_job.cancel = 1;
    }
    cdr_unlock(&g_job.lock);
}

void coder_app_notify_thought(const char *txt)
{
    if (!txt || !*txt) return;
    cdr_lock(&g_job.lock);
    if (g_job.state == CODER_JOB_WORKING) {
        cdr_clean_text(g_job.live_thought, sizeof(g_job.live_thought), txt);
    }
    cdr_unlock(&g_job.lock);
}

void coder_app_load_project(const char *path)
{
    cdr_load_mod_file(path);
    cdr_open_in_editor(path);
}

void coder_app_init(void)
{
    cdr_ensure_init();
    cdr_tab_cur = CDR_TAB_EDITOR;                /* PARCHE 093 */
    cdr_scan_projects();
    if (ide_inited) return;                      /* conserva el archivo abierto y sus cambios */
    if (project_count > 0 && !mod_loaded) {
        cdr_load_mod_file(projects[0].path);
    } else {
        cdr_load_mod_file("/workspace/calc.soma");
    }
}

/* ------------------------------------------------------------------------
 * Renderizado de controles y vistas
 * ------------------------------------------------------------------------ */
/* SOMA_240_ICONOS: los botones llevan un icono vectorial del sistema (sysicon.h) delante del texto, o solo el icono */
#define CDR_ICO 15
static void cdr_draw_btn_i(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int icon,
                           const char *lbl, uint32_t bg, uint32_t brd, uint32_t txt, int hover)
{   /* PARCHE 094: botones planos redondeados; los neutros sin borde, los de accion con tinte y contorno suave */
    int neutral = (brd == CDR_C_BORDER2 || brd == CDR_C_BORDER);
    if (neutral) { bg = 0x00182132u; txt = (txt == CDR_C_SOFT || txt == CDR_C_GRAY) ? 0x00D5DCE8u : txt; }
    if (hover) bg = si_mix(bg, 0x00FFFFFFu, 18);
    fb_fill_rrect_aa((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 7, bg);
    if (!neutral) fb_stroke_rrect_aa((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 7, 0x90000000u | brd);
    else fb_fill_rect(x + 7, y, w > 14 ? w - 14 : 1, 1, hover ? 0x00334155u : 0x00222C40u);
    int tw = lbl && lbl[0] ? cdr_tw(lbl) : 0;
    int tot = (icon ? CDR_ICO : 0) + (icon && tw ? 7 : 0) + tw;
    int cx = (int)x + ((int)w - tot) / 2; if (cx < (int)x + 6) cx = (int)x + 6;
    uint32_t ic = neutral ? si_mix(txt, 0x0060A5FAu, 70) : txt;
    if (icon) { si_icon(icon, cx + CDR_ICO / 2, (int)y + (int)h / 2, tw ? CDR_ICO : CDR_ICO + 3, ic); cx += CDR_ICO + (tw ? 7 : 0); }
    if (tw) font_draw_string((uint32_t)cx, y + (h - 9) / 2, lbl, txt, 1);
}

static void cdr_draw_tab_btn(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             const char *title, int active, int icon)
{   /* PARCHE 094: pestanas de tipo subrayado; SOMA_240_ICONOS: con icono */
    int tw = cdr_tw(title), tot = tw + (icon ? CDR_ICO + 8 : 0);
    if (active) {
        fb_fill_rrect_aa((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 7, 0x00162036u);
        fb_fill_rrect_aa((int32_t)(x + 12), (int32_t)(y + h - 3), (int32_t)(w - 24), 3, 1, CDR_C_GREEN);
    }
    int cx = (int)x + ((int)w - tot) / 2;
    if (icon) { si_icon(icon, cx + CDR_ICO / 2, (int)y + (int)h / 2 - 1, CDR_ICO, active ? CDR_C_GREEN : 0x0064748Bu); cx += CDR_ICO + 8; }
    font_draw_string((uint32_t)cx, y + (h - 9) / 2 - 1, title, active ? CDR_C_WHITE : CDR_C_DIM, 1);
}






/* ========================================================================
 * PARCHE 093: SOMA Studio -- editor profesional (pestana EDITOR)
 * El usuario ve y modifica el codigo a mano y, cuando quiere, pide a SOMA un cambio sobre el mismo archivo;
 * el resultado del agente entra como una edicion mas (Ctrl+Z la deshace).
 * ======================================================================== */
extern int keyboard_ctrl_held(void);
extern int keyboard_shift_held(void);

struct ide_btn { int x, y, w, h; int id; const char *tip; };   /* SOMA_240_ICONOS: tip = nombre si el boton es solo icono */
static int ide_hover_id;          /* boton de la barra bajo el raton (0 = ninguno) */
enum { IB_NEW = 1, IB_OPEN, IB_SAVE, IB_SAVEAS, IB_UNDO, IB_REDO, IB_FIND, IB_REPL, IB_GOTO, IB_COMMENT, IB_VERIFY, IB_RUN, IB_IA, IB_IA_FIELD, IB_IA_CANCEL,
       IB_OUT_STOP, IB_OUT_CLOSE, IB_OUT_IN, IB_OUT_AGAIN };   /* SOMA_333_SALIDA */
static struct ide_btn ide_btns[24]; static int ide_nb;
static char ide_ia[384]; static int ide_ia_len;
static char ide_msg[160]; static uint32_t ide_msg_col = CDR_C_GRAY;
static char ide_prob[4][112]; static int ide_prob_line[4], ide_prob_n, ide_prob_ok = -1;
static int  ide_was_working, ide_job_mine;
static char ide_job_target[96];
static int  ide_ex_x, ide_ex_y, ide_ex_w, ide_ex_rows, ide_ex_scroll, ide_ex_armed = -1, ide_ex_hover = -1;
static int  ide_ed_x, ide_ed_y, ide_ed_w, ide_ed_h, ide_pr_y;
static struct soma_lint_report ide_rep;
/* ---- campos ---- */
static struct tfield tf_cprompt, tf_cfile, tf_search, tf_rename, tf_ia;
static char ui_search[64], ui_renbuf[96];
static struct tfield *ui_focus;                 /* campo con el foco en CREAR/PROYECTOS (0 = ninguno) */
static int ui_tf_ready;
static void ui_tf_init(void)
{
    if (ui_tf_ready) return;
    ui_tf_ready = 1;
    tf_init(&tf_cprompt, input_create_prompt, sizeof input_create_prompt, 1);
    tf_init(&tf_cfile, input_create_file, sizeof input_create_file, 0);
    tf_init(&tf_search, ui_search, sizeof ui_search, 0);
    tf_init(&tf_rename, ui_renbuf, sizeof ui_renbuf, 0);
    tf_init(&tf_ia, ide_ia, sizeof ide_ia, 0);
    ui_focus = &tf_cprompt;
}



static void ide_say(const char *m, uint32_t col) { cdr_copy(ide_msg, sizeof ide_msg, m); ide_msg_col = col; }

static void ide_open(const char *path)
{
    int r = ce_load(path);
    ide_prob_n = 0; ide_prob_ok = -1; ce_set_error(0, 0);
    if (r < 0) { ide_say("Archivo demasiado grande para el editor (maximo 256 KiB).", CDR_C_RED); return; }
    char m[160]; cdr_copy(m, sizeof m, r ? "Abierto " : "Nuevo archivo (aun no existe): "); cdr_cat(m, sizeof m, path);
    ide_say(m, r ? CDR_C_GRAY : CDR_C_AMBER);
    cdr_copy(mod_current_file, sizeof mod_current_file, path);
    ide_focus = 0;
}
static void cdr_open_in_editor(const char *path) { if (path && *path) { ide_open(path); ide_inited = 1; } cdr_tab_cur = CDR_TAB_EDITOR; }

static int ide_verify(void)
{
    uint32_t len = 0; const char *t = ce_text(&len);
    struct cdr_snap sn; cdr_snapshot(&sn);
    if (cdr_is_ext(ce_path())) {                             /* SOMA_239: Python se comprueba con el interprete de verdad; SOMA_265_CC: C con cc; SOMA_275_HTML: JS y HTML */
        static char o[1024];
        ide_prob_n = 0; ce_set_error(0, 0);
        int pr = cdr_ext_src(ce_path(), t, len, o, sizeof o);
        ide_prob_ok = pr == 0;
        if (ide_prob_ok) { static char okm[48]; cdr_copy(okm, sizeof okm, cdr_lang_label(ce_path())); cdr_cat(okm, sizeof okm, cdr_is_html(ce_path()) ? ": la pagina es correcta." : ": sin errores."); ide_say(okm, CDR_C_GREEN); return 1; }
        uint32_t ln = 0; char m[96]; m[0] = 0; cdr_ext_err(ce_path(), o, &ln, m, sizeof m);
        char mm[112]; cdr_copy(mm, sizeof mm, "Linea "); cdr_cat_u32(mm, sizeof mm, ln); cdr_cat(mm, sizeof mm, ": "); cdr_cat(mm, sizeof mm, m[0] ? m : "no se pudo comprobar");
        cdr_copy(ide_prob[0], sizeof ide_prob[0], mm); ide_prob_line[0] = (int)ln; ide_prob_n = 1;
        ce_set_error((int)ln, m);
        if (ln) ce_goto_line((int)ln);
        { static char em[96]; const char *lb = cdr_lang_label(ce_path());   /* SOMA_275_HTML */
          if (pr < 0) { cdr_copy(em, sizeof em, "No se pudo comprobar "); cdr_cat(em, sizeof em, lb); cdr_cat(em, sizeof em, " ahora (otra orden en marcha); prueba de nuevo."); }
          else { cdr_copy(em, sizeof em, "Error de "); cdr_cat(em, sizeof em, lb); cdr_cat(em, sizeof em, ": revisa la linea marcada."); }
          ide_say(em, CDR_C_RED); }
        return 0;
    }
    if (sn.state == CODER_JOB_WORKING) { ide_say("El compilador esta ocupado con la tarea de SOMA; espera a que termine.", CDR_C_AMBER); return -1; }
    uint8_t *bc = (uint8_t *)kmalloc(262144); uint32_t bl = 0;   /* SOMA_264_LANG */
    if (!bc) { ide_say("Sin memoria para compilar.", CDR_C_RED); return -1; }
    ide_rep.count = ide_rep.hard = 0;
    lang_lint(t, len, &ide_rep);
    int bad = lang_compile(t, len, bc, 262144, &bl) != 0;
    kfree(bc);
    ide_prob_n = 0; ce_set_error(0, 0);
    if (bad) {
        struct soma_compile_diag d; lang_get_diagnostic(&d);
        char m[112]; cdr_copy(m, sizeof m, "Linea "); cdr_cat_u32(m, sizeof m, d.line); cdr_cat(m, sizeof m, ": "); cdr_cat(m, sizeof m, d.msg);
        cdr_copy(ide_prob[0], sizeof ide_prob[0], m); ide_prob_line[0] = (int)d.line; ide_prob_n = 1;
        ce_set_error((int)d.line, d.msg);
        if (d.line) ce_goto_line((int)d.line);
    }
    for (uint32_t i = 0; i < ide_rep.count && ide_prob_n < 4; i++) { cdr_copy(ide_prob[ide_prob_n], sizeof ide_prob[0], ide_rep.msg[i]); ide_prob_line[ide_prob_n++] = 0; }
    ide_prob_ok = (!bad && ide_rep.hard == 0);
    if (ide_prob_ok) { char m[96]; cdr_copy(m, sizeof m, "Compilacion correcta: "); cdr_cat_u32(m, sizeof m, bl); cdr_cat(m, sizeof m, " bytes de bytecode, 0 errores."); ide_say(m, CDR_C_GREEN); }
    else ide_say(bad ? "Error de compilacion: revisa la linea marcada." : "El analisis estatico encontro errores graves.", CDR_C_RED);
    return ide_prob_ok;
}
static int ide_save(void)
{
    if (!ce_save()) { ide_say("No se pudo guardar (ruta protegida o sin espacio).", CDR_C_RED); return 0; }
    char m[160]; cdr_copy(m, sizeof m, "Guardado "); cdr_cat(m, sizeof m, ce_path()); ide_say(m, CDR_C_GREEN);
    cdr_copy(mod_current_file, sizeof mod_current_file, ce_path());
    cdr_scan_projects();
    return 1;
}
static void ide_run(void)
{
    if (ce_dirty() && !ide_save()) return;
    if (ide_verify() != 1) return;
    cdr_run_app(ce_path());
    ide_say("Programa lanzado.", CDR_C_CYAN);
}
static void ide_ask_soma(void)
{
    struct cdr_snap sn; cdr_snapshot(&sn);
    if (sn.state == CODER_JOB_WORKING) { ide_say("SOMA ya esta trabajando en otra tarea.", CDR_C_AMBER); return; }
    if (!ide_ia_len) { ide_focus = 1; ide_say("Escribe que quieres que cambie SOMA en este archivo.", CDR_C_AMBER); return; }
    if (ce_dirty() && !ide_save()) return;
    cdr_copy(mod_current_file, sizeof mod_current_file, ce_path());
    cdr_copy(input_mod_prompt, sizeof input_mod_prompt, ide_ia);
    coder_start_modification();
    cdr_snapshot(&sn);
    if (sn.state == CODER_JOB_WORKING) { ide_job_mine = 1; cdr_copy(ide_job_target, sizeof ide_job_target, sn.target_file); ide_say("SOMA esta modificando el archivo...", CDR_C_CYAN); }
}
static void ide_track_job(const struct cdr_snap *sn)
{
    int working = sn->state == CODER_JOB_WORKING;
    int same = cdr_eq(sn->target_file, ce_path());
    if (working && same) ce_set_readonly(1, "SOMA esta modificando este archivo: se puede leer y copiar, pero no editar.");
    if (ide_was_working && !working) {
        ce_set_readonly(0, 0);
        if (same) {
            if (sn->state == CODER_JOB_SUCCESS) {
                if (ce_reload_keep_undo()) { ide_say("Cambios de SOMA aplicados y verificados (compila). Ctrl+Z los deshace.", CDR_C_GREEN); ide_prob_ok = 1; }
                else ide_say("SOMA termino sin cambiar el archivo.", CDR_C_AMBER);
                ide_ia_len = 0; tf_set(&tf_ia, ""); ide_prob_n = 0; ce_set_error(0, 0);
            } else ide_say("SOMA no pudo completar el cambio; el archivo queda como estaba.", CDR_C_RED);
        }
        ide_job_mine = 0;
    }
    ide_was_working = working;
}
static int ide_btn_w(const char *t, int icon, int compact) { return compact ? 32 : cdr_tw(t) + 24 + (icon ? CDR_ICO + 7 : 0); }
static void ide_add_btn_i(int *x, int y, const char *t, int id, int icon, int compact, uint32_t bg, uint32_t br, uint32_t tx)
{
    int w = ide_btn_w(t, icon, compact);
    cdr_draw_btn_i((uint32_t)*x, (uint32_t)y, (uint32_t)w, 28, icon, compact ? 0 : t, bg, br, tx, ide_hover_id == id);
    if (ide_nb < 24) { ide_btns[ide_nb].x = *x; ide_btns[ide_nb].y = y; ide_btns[ide_nb].w = w; ide_btns[ide_nb].h = 28; ide_btns[ide_nb].id = id; ide_btns[ide_nb].tip = compact ? t : 0; ide_nb++; }
    *x += w + 6;
}
static void ide_sep(int *x, int y) { fb_fill_rect((uint32_t)(*x + 3), (uint32_t)(y + 6), 1, 16, 0x00263043u); *x += 10; }

/* PARCHE 094: tarjeta redondeada con contorno suave */
static void ide_card(int x, int y, int w, int h, uint32_t bg, uint32_t edge)
{
    fb_fill_rounded_rect(x, y, w, h, 10, bg);
    fb_stroke_rrect_aa(x, y, w, h, 10, 0xFF000000u | edge);
}
static const char *ide_base(const char *p) { const char *n = p; for (; *p; p++) if (*p == '/') n = p + 1; return n; }

static uint32_t cdr_file_col(const struct cdr_proj *p, int strong)
{
    uint32_t c = cdr_is_ext(p->path) ? cdr_lang_color(p->path) : p->is_gui ? CDR_C_GREEN : CDR_C_CYAN;   /* SOMA_265_CC: C violeta; SOMA_275_HTML: JS amarillo, HTML naranja */
    return strong ? c : si_mix(0x000C1220u, c, 200);
}
static void ide_render(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, const struct cdr_snap *sn)
{
    int x0 = (int)win_x + 12, w = (int)win_w - 24, y0 = (int)win_y + 54, y1 = (int)(win_y + win_h) - 10;
    int working = sn->state == CODER_JOB_WORKING;
    ide_nb = 0;
    ui_tf_init();
    if (!ide_inited) { ide_inited = 1; ide_open(mod_current_file); }
    ide_track_job(sn);

    /* ---- barra de herramientas ---- */
    int bx = x0, by = y0;
    uint32_t pb = CDR_C_PANEL, pr = CDR_C_BORDER2, pt = CDR_C_SOFT;
    {   /* SOMA_240_ICONOS: cada boton con su icono; si no cabe todo, los de edicion pasan a ser solo icono (con su nombre al pasar el raton) */
        static const struct { const char *t; int id, icon, grp; } tb[] = {
            { "Nuevo", IB_NEW, SI_NEW, 0 }, { "Abrir", IB_OPEN, SI_FOLDER, 0 }, { "Guardar", IB_SAVE, SI_DISK, 0 }, { "Guardar como", IB_SAVEAS, SI_SAVE, 2 },
            { 0, 0, 0, 0 }, { "Deshacer", IB_UNDO, SI_UNDO, 1 }, { "Rehacer", IB_REDO, SI_REDO, 1 },
            { 0, 0, 0, 0 }, { "Buscar", IB_FIND, SI_SEARCH, 2 }, { "Reemplazar", IB_REPL, SI_REPLACE, 2 }, { "Ir a la linea", IB_GOTO, SI_GOTO, 1 }, { "Comentar", IB_COMMENT, SI_COMMENT, 1 } };
        int ntb = (int)(sizeof tb / sizeof tb[0]);
        int rw = ide_btn_w("Verificar", SI_CHECK, 0) + 6 + ide_btn_w("Ejecutar", SI_PLAY, 0);
        int avail = w - rw - 16, level;
        for (level = 0; level < 3; level++) {                   /* 0 todo con texto, 1 grupo 1 solo icono, 2 grupos 1 y 2 solo icono */
            int tot = 0;
            for (int i = 0; i < ntb; i++) tot += tb[i].t ? ide_btn_w(tb[i].t, tb[i].icon, tb[i].grp && tb[i].grp <= level) + 6 : 10;
            if (tot <= avail) break;
        }
        for (int i = 0; i < ntb; i++) {
            if (!tb[i].t) { ide_sep(&bx, by); continue; }
            int compact = tb[i].grp && tb[i].grp <= level;
            if (bx + ide_btn_w(tb[i].t, tb[i].icon, compact) > x0 + avail) continue;     /* ventana muy estrecha: se omite */
            int dirty = tb[i].id == IB_SAVE && ce_dirty();
            ide_add_btn_i(&bx, by, tb[i].t, tb[i].id, tb[i].icon, compact, dirty ? 0x000E2F26u : pb, dirty ? CDR_C_GREEN : pr, dirty ? 0x00A7F3D0u : pt);
        }
        /* acciones principales alineadas a la derecha */
        int rx = x0 + w - rw; if (rx < bx + 8) rx = bx + 8;
        ide_add_btn_i(&rx, by, "Verificar", IB_VERIFY, SI_CHECK, 0, 0x00241C0Au, CDR_C_AMBER, 0x00FCD34Du);
        ide_add_btn_i(&rx, by, "Ejecutar", IB_RUN, SI_PLAY, 0, 0x00173A7Au, 0x0060A5FAu, CDR_C_WHITE);
    }

    int body_y = y0 + 36;
    int status_y = y1 - 24, ia_y = status_y - 48;
    int prob_h = ide_prob_n ? 30 + ide_prob_n * 18 : 0;
    int out_h = crun_visible() ? (ia_y - body_y > 520 ? 200 : 150) : 0;     /* SOMA_333_SALIDA: panel con lo que imprime el programa */
    int ed_bottom = ia_y - 8 - (prob_h ? prob_h + 8 : 0) - (out_h ? out_h + 8 : 0);

    /* ---- explorador ---- */
    int ex_w = (w >= 800) ? 220 : 0;
    ide_ex_w = ex_w;
    if (ex_w) {
        ide_ex_x = x0; ide_ex_y = body_y;
        ide_card(x0, body_y, ex_w, ed_bottom - body_y, 0x000C1220u, 0x001A2335u);
        si_icon(SI_FOLDER, x0 + 21, body_y + 16, 14, 0x0060A5FAu);
        font_draw_string((uint32_t)(x0 + 34), (uint32_t)(body_y + 11), "EXPLORADOR", CDR_C_DIM, 1);
        { char n[12]; n[0] = 0; cdr_cat_u32(n, sizeof n, (uint32_t)project_count); int nw = cdr_tw(n) + 14;
          fb_fill_rounded_rect(x0 + ex_w - nw - 12, body_y + 8, nw, 18, 9, 0x001A2335u); font_draw_string((uint32_t)(x0 + ex_w - nw - 5), (uint32_t)(body_y + 12), n, CDR_C_GRAY, 1); }
        int ry = body_y + 34; ide_ex_rows = (ed_bottom - ry - 6) / 26;
        if (ide_ex_scroll > project_count - 1) ide_ex_scroll = 0;
        for (int i = ide_ex_scroll; i < project_count && i - ide_ex_scroll < ide_ex_rows; i++) {
            int open = cdr_eq(projects[i].path, ce_path());
            if (open) { fb_fill_rounded_rect(x0 + 6, ry, ex_w - 12, 24, 7, 0x00172A4Au); fb_fill_rounded_rect(x0 + 6, ry + 5, 3, 14, 1, CDR_C_CYAN); }
            if (!open && ide_ex_hover == i) fb_fill_rounded_rect(x0 + 6, ry, ex_w - 12, 24, 7, 0x00121C30u);   /* PARCHE 101: resaltado al pasar el raton */
            if (ide_ex_armed == i) fb_fill_rounded_rect(x0 + 6, ry, ex_w - 12, 24, 7, 0x003A1520u);
            fb_set_clip(x0 + 8, ry, ex_w - 16, 24);
            /* SOMA_240_ICONOS: icono vectorial de codigo fuente; color segun lenguaje y tipo (Python ambar, ventana verde, consola cian) */
            si_icon(SI_FILE_CODE, x0 + 24, ry + 12, 18, cdr_file_col(&projects[i], open));
            font_draw_string((uint32_t)(x0 + 38), (uint32_t)(ry + 7), projects[i].name, open ? CDR_C_WHITE : 0x00AEB9CCu, 1);
            {   /* PARCHE 094: mismo nombre en otra carpeta: muestra la carpeta */
                int twin = 0; for (int q = 0; q < project_count; q++) if (q != i && cdr_eq(projects[q].name, projects[i].name)) twin = 1;
                if (twin) { char d[64]; cdr_copy(d, sizeof d, projects[i].path); int L = (int)cdr_len(d) - (int)cdr_len(projects[i].name) - 1; if (L <= 0) cdr_copy(d, sizeof d, "/"); else d[L] = 0;
                    font_draw_string((uint32_t)(x0 + 44 + cdr_tw(projects[i].name)), (uint32_t)(ry + 7), d, 0x00566479u, 1); }
            }
            if (open && ce_dirty()) fb_fill_rounded_rect(x0 + ex_w - 22, ry + 9, 7, 7, 3, CDR_C_AMBER);
            fb_reset_clip();
            ry += 26;
        }
        if (project_count == 0) font_draw_string((uint32_t)(x0 + 16), (uint32_t)(body_y + 40), "Sin archivos .soma todavia", CDR_C_DIM, 1);
        if (ide_ex_armed >= 0) {
            fb_fill_rounded_rect(x0 + 8, ed_bottom - 50, ex_w - 16, 42, 8, 0x002A1F0Au);
            font_draw_string((uint32_t)(x0 + 16), (uint32_t)(ed_bottom - 44), "Hay cambios sin guardar.", CDR_C_AMBER, 1);
            font_draw_string((uint32_t)(x0 + 16), (uint32_t)(ed_bottom - 26), "Clic otra vez: descartar", 0x00D6B36Au, 1);
        }
    }

    /* ---- editor con su pestana ---- */
    int ex = x0 + (ex_w ? ex_w + 8 : 0), ew = w - (ex_w ? ex_w + 8 : 0);
    ide_card(ex, body_y, ew, ed_bottom - body_y, 0x000B1120u, 0x001A2335u);
    {
        const char *pth = ce_path(), *nm = ide_base(pth);
        int tw = cdr_tw(nm) + 46;
        fb_fill_rounded_rect(ex + 1, body_y + 1, ew - 2, 32, 10, 0x000D1426u);
        fb_fill_rect((uint32_t)(ex + 1), (uint32_t)(body_y + 20), (uint32_t)(ew - 2), 13, 0x000D1426u);
        fb_fill_rounded_rect(ex + 6, body_y + 6, tw, 27, 7, 0x000B1120u);
        fb_fill_rect((uint32_t)(ex + 6), (uint32_t)(body_y + 26), (uint32_t)tw, 8, 0x000B1120u);
        fb_fill_rounded_rect(ex + 14, body_y + 6, tw - 16, 2, 1, CDR_C_CYAN);
        si_icon(SI_FILE_CODE, ex + 21, body_y + 19, 16, cdr_lang_color(pth));
        font_draw_string((uint32_t)(ex + 32), (uint32_t)(body_y + 14), nm, CDR_C_WHITE, 1);
        if (ce_dirty()) fb_fill_rounded_rect(ex + tw - 6, body_y + 16, 8, 8, 4, CDR_C_AMBER);
        fb_set_clip(ex + tw + 14, body_y, ew - tw - 24, 33);
        font_draw_string((uint32_t)(ex + tw + 18), (uint32_t)(body_y + 14), pth, CDR_C_DIM, 1);
        fb_reset_clip();
        fb_fill_rect((uint32_t)(ex + 1), (uint32_t)(body_y + 33), (uint32_t)(ew - 2), 1, 0x001A2335u);
    }
    ide_ed_x = ex + 1; ide_ed_y = body_y + 34; ide_ed_w = ew - 2; ide_ed_h = ed_bottom - body_y - 40;
    ce_render(ide_ed_x, ide_ed_y, ide_ed_w, ide_ed_h, ide_focus == 0);

    /* ---- problemas ---- */
    ide_pr_y = ed_bottom + 8 + 26;
    if (ide_prob_n) {
        int py0 = ed_bottom + 8;
        ide_card(x0, py0, w, prob_h, 0x00140B12u, 0x003F1A26u);
        si_icon(SI_SEARCH, x0 + 21, py0 + 14, 13, 0x00FDA4AFu);
        font_draw_string((uint32_t)(x0 + 34), (uint32_t)(py0 + 9), "PROBLEMAS", 0x00FDA4AFu, 1);
        { char n[12]; n[0] = 0; cdr_cat_u32(n, sizeof n, (uint32_t)ide_prob_n); int nw = cdr_tw(n) + 14;
          fb_fill_rounded_rect(x0 + 124, py0 + 6, nw, 18, 9, 0x004C1D2Bu); font_draw_string((uint32_t)(x0 + 131), (uint32_t)(py0 + 10), n, 0x00FECDD3u, 1); }
        for (int i = 0; i < ide_prob_n; i++) {
            int py = ide_pr_y + 2 + i * 18;
            uint32_t col = (ide_prob[i][0] == 'W') ? CDR_C_AMBER : 0x00FB7185u;
            fb_fill_rounded_rect(x0 + 16, py + 3, 9, 9, 4, col);
            fb_set_clip(x0 + 30, py, w - 44, 18);
            font_draw_string((uint32_t)(x0 + 34), (uint32_t)(py + 2), ide_prob[i], ide_prob_line[i] ? 0x00FECDD3u : col, 1);
            fb_reset_clip();
        }
    }

    /* ---- SOMA_333_SALIDA: salida del programa en marcha (antes se abria el Terminal) ---- */
    ide_out_h = out_h; ide_out_rows = 0;
    if (out_h) {
        int oy = ia_y - 8 - out_h, run = crun_estado() == CRUN_MARCHA, in_h = run ? 34 : 0;
        ide_out_x = x0; ide_out_y = oy; ide_out_w = w;
        ide_card(x0, oy, w, out_h, 0x00070C14u, run ? 0x001E40AFu : 0x001A2335u);
        si_icon(SI_TERMINAL, x0 + 21, oy + 17, 14, run ? 0x0060A5FAu : CDR_C_GRAY);
        font_draw_string((uint32_t)(x0 + 34), (uint32_t)(oy + 11), "SALIDA", CDR_C_DIM, 1);
        int hx = x0 + 34 + cdr_tw("SALIDA") + 12;
        font_draw_string((uint32_t)hx, (uint32_t)(oy + 11), crun_nombre(), CDR_C_WHITE, 1);
        hx += cdr_tw(crun_nombre()) + 12;
        {   char st[64]; uint32_t ms = crun_ms(); st[0] = 0;
            if (run) cdr_cat(st, sizeof st, "en marcha  ");
            else cdr_cat(st, sizeof st, crun_parado() ? "parado a los " : crun_fallo() ? "fallo a los " : "termino en ");
            cdr_cat_u32(st, sizeof st, ms / 1000); cdr_cat(st, sizeof st, ","); cdr_cat_u32(st, sizeof st, ms / 100 % 10); cdr_cat(st, sizeof st, " s");
            int sw = cdr_tw(st) + 26;
            int fallo = !run && crun_fallo();
            uint32_t cbg = run ? 0x00132A4Au : crun_parado() ? 0x002A1F0Au : fallo ? 0x004C1D2Bu : 0x000F3B2Eu,
                     cdot = run ? ((timer_get_uptime_ms() / 400) % 2 ? 0x0060A5FAu : 0x001D4ED8u) : crun_parado() ? CDR_C_AMBER : fallo ? CDR_C_RED : CDR_C_GREEN;
            fb_fill_rounded_rect(hx, oy + 7, sw, 20, 10, cbg);
            fb_fill_rounded_rect(hx + 8, oy + 14, 7, 7, 3, cdot);
            font_draw_string((uint32_t)(hx + 20), (uint32_t)(oy + 11), st, 0x00CBD5E1u, 1);
        }
        int bx2 = x0 + w - 8;
        if (run) { bx2 -= ide_btn_w("Parar", SI_STOP, 0); int b = bx2; ide_add_btn_i(&b, oy + 4, "Parar", IB_OUT_STOP, SI_STOP, 0, 0x003A121Du, CDR_C_RED, 0x00FECDD3u); }
        else {
            bx2 -= ide_btn_w("Cerrar", SI_CLOSE, 0); int b = bx2; ide_add_btn_i(&b, oy + 4, "Cerrar", IB_OUT_CLOSE, SI_CLOSE, 0, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT);
            bx2 -= 6 + ide_btn_w("Otra vez", SI_PLAY, 0); b = bx2; ide_add_btn_i(&b, oy + 4, "Otra vez", IB_OUT_AGAIN, SI_PLAY, 0, 0x00173A7Au, 0x0060A5FAu, CDR_C_WHITE);
        }
        fb_fill_rect((uint32_t)(x0 + 1), (uint32_t)(oy + 36), (uint32_t)(w - 2), 1, 0x001A2335u);
        int ty = oy + 42, rows = (out_h - 48 - in_h) / 17; if (rows < 1) rows = 1;
        ide_out_rows = rows;
        int n = crun_lineas(), mxs = n - rows; if (mxs < 0) mxs = 0;
        if (ide_out_scroll > mxs) ide_out_scroll = mxs;
        if (ide_out_scroll < 0) ide_out_scroll = 0;
        int first = n - rows - ide_out_scroll; if (first < 0) first = 0;
        fb_set_clip(x0 + 10, ty, w - 20, rows * 17);
        for (int i = first; i < n && i < first + rows; i++)
            font_draw_string((uint32_t)(x0 + 14), (uint32_t)(ty + (i - first) * 17), crun_linea(i), 0x00D7DEE8u, 1);
        if (!n) font_draw_string((uint32_t)(x0 + 14), (uint32_t)ty, run ? "(todavia no ha escrito nada)" : "(no escribio nada)", 0x00566479u, 1);
        fb_reset_clip();
        if (ide_out_scroll) { const char *m = "rueda: mas abajo"; font_draw_string((uint32_t)(x0 + w - 20 - cdr_tw(m)), (uint32_t)(ty), m, 0x00566479u, 1); }
        if (run) {                                                       /* lo que se escribe para el programa (input, scanf) */
            int iy = oy + out_h - in_h - 2;
            fb_fill_rounded_rect(x0 + 8, iy, w - 16, 28, 8, ide_focus == 3 ? 0x000B1A33u : 0x00090F1Bu);
            if (ide_focus == 3) fb_stroke_rrect_aa(x0 + 8, iy, w - 16, 28, 8, 0xFF3B82F6u);
            if (ide_nb < 24) { ide_btns[ide_nb].x = x0 + 8; ide_btns[ide_nb].y = iy; ide_btns[ide_nb].w = w - 16; ide_btns[ide_nb].h = 28; ide_btns[ide_nb].id = IB_OUT_IN; ide_btns[ide_nb].tip = 0; ide_nb++; }
            fb_set_clip(x0 + 12, iy, w - 24, 28);
            int ix = x0 + 16; const char *pr = crun_prompt();
            if (pr[0]) { font_draw_string((uint32_t)ix, (uint32_t)(iy + 8), pr, 0x0093C5FDu, 1); ix += cdr_tw(pr) + 4; }
            if (ide_out_in_len) { font_draw_string((uint32_t)ix, (uint32_t)(iy + 8), ide_out_in, CDR_C_WHITE, 1); ix += cdr_tw(ide_out_in); }
            else if (!pr[0]) font_draw_string((uint32_t)ix, (uint32_t)(iy + 8), ide_focus == 3 ? "" : "Haz clic aqui para escribirle al programa (Intro envia la linea, Esc lo para)", 0x00566479u, 1);
            if (ide_focus == 3 && (timer_get_uptime_ms() / 500) % 2 == 0) fb_fill_rect((uint32_t)(ix + 1), (uint32_t)(iy + 6), 2, 16, 0x0093C5FDu);
            fb_reset_clip();
        }
    }

    /* ---- barra de SOMA IA ---- */
    {
        uint32_t edge = ide_focus == 1 ? CDR_C_GREEN : (working ? 0x002563EBu : 0x001F3A33u);
        ide_card(x0, ia_y, w, 40, 0x000C1A1Au, edge);
        /* insignia */
        fb_fill_rounded_rect(x0 + 8, ia_y + 8, 86, 24, 12, working ? 0x00132A4Au : 0x000F3B2Eu);
        int pulse = working ? (int)((timer_get_uptime_ms() / 120) % 6) : 0;
        si_icon(SI_AUTO, x0 + 22, ia_y + 20, 14, working ? (pulse < 3 ? 0x0060A5FAu : 0x003B82F6u) : CDR_C_GREEN);
        font_draw_string((uint32_t)(x0 + 31), (uint32_t)(ia_y + 14), "SOMA IA", working ? 0x00BFDBFEu : 0x00A7F3D0u, 1);
        int fx = x0 + 100, bw = working ? ide_btn_w("Cancelar", SI_CLOSE, 0) : ide_btn_w("Aplicar con IA", SI_AUTO, 0), fw = w - 100 - bw - 18;
        if (ide_nb < 24) { ide_btns[ide_nb].x = fx; ide_btns[ide_nb].y = ia_y + 7; ide_btns[ide_nb].w = fw; ide_btns[ide_nb].h = 26; ide_btns[ide_nb].id = IB_IA_FIELD; ide_nb++; }
        if (working) {
            fb_fill_rounded_rect(fx, ia_y + 7, fw, 26, 8, 0x00070D16u);
            fb_set_clip(fx + 6, ia_y + 7, fw - 12, 26);
            font_draw_string((uint32_t)(fx + 12), (uint32_t)(ia_y + 15), sn->live_thought[0] ? sn->live_thought : sn->phase_desc, 0x0093C5FDu, 1);
            fb_reset_clip();
        } else tf_render(&tf_ia, fx, ia_y + 7, fw, 26, ide_focus == 1, "Pide un cambio sobre este archivo: anade un boton de reset, explica y comenta el codigo...", CDR_C_WHITE);
        int bxx = fx + fw + 8;
        if (working) ide_add_btn_i(&bxx, ia_y + 7, "Cancelar", IB_IA_CANCEL, SI_CLOSE, 0, 0x003A121Du, CDR_C_RED, 0x00FECDD3u);
        else ide_add_btn_i(&bxx, ia_y + 7, "Aplicar con IA", IB_IA, SI_AUTO, 0, 0x00065F46u, CDR_C_GREEN, CDR_C_WHITE);
    }

    /* ---- barra de estado ---- */
    {
        uint32_t sbg = working ? 0x00122B57u : 0x000D1426u;
        fb_fill_rounded_rect(x0, status_y, w, 24, 8, sbg);
        int ln, col, lines, sc; ce_status(&ln, &col, &lines, &sc);
        char st[128]; st[0] = 0;
        cdr_cat(st, sizeof st, "Ln "); cdr_cat_u32(st, sizeof st, (uint32_t)ln);
        cdr_cat(st, sizeof st, ", Col "); cdr_cat_u32(st, sizeof st, (uint32_t)col);
        if (sc) { cdr_cat(st, sizeof st, "  ("); cdr_cat_u32(st, sizeof st, (uint32_t)sc); cdr_cat(st, sizeof st, " sel.)"); }
        cdr_cat(st, sizeof st, "   \xC2\xB7   "); cdr_cat_u32(st, sizeof st, (uint32_t)lines);
        cdr_cat(st, sizeof st, " lineas   \xC2\xB7   UTF-8   \xC2\xB7   "); cdr_cat(st, sizeof st, cdr_lang_label(ce_path()));   /* SOMA_275_HTML */
        int sw = cdr_tw(st);
        font_draw_string((uint32_t)(x0 + w - sw - 14), (uint32_t)(status_y + 7), st, 0x008391A7u, 1);
        uint32_t dot = ide_prob_ok == 1 ? CDR_C_GREEN : ide_prob_ok == 0 ? CDR_C_RED : 0x00475569u;
        const char *dl = ide_prob_ok == 1 ? "Sin errores" : ide_prob_ok == 0 ? "Con errores" : "Sin verificar";
        int dw = cdr_tw(dl) + 28;
        fb_fill_rounded_rect(x0 + 4, status_y + 3, dw, 18, 9, ide_prob_ok == 1 ? 0x000F3B2Eu : ide_prob_ok == 0 ? 0x004C1D2Bu : 0x001A2335u);
        if (ide_prob_ok == 1) si_icon(SI_CHECK, x0 + 14, status_y + 12, 11, dot);
        else if (ide_prob_ok == 0) si_icon(SI_CLOSE, x0 + 14, status_y + 12, 11, dot);
        else fb_fill_rounded_rect(x0 + 11, status_y + 9, 7, 7, 3, dot);
        font_draw_string((uint32_t)(x0 + 22), (uint32_t)(status_y + 7), dl, ide_prob_ok == 0 ? 0x00FECDD3u : 0x00CBD5E1u, 1);
        fb_set_clip(x0 + dw + 14, status_y, w - sw - dw - 40, 24);
        font_draw_string((uint32_t)(x0 + dw + 16), (uint32_t)(status_y + 7), ide_msg[0] ? ide_msg : "Ctrl+S guardar  \xC2\xB7  Ctrl+F buscar  \xC2\xB7  Ctrl+Z deshacer  \xC2\xB7  Ctrl+E ejecutar", ide_msg[0] ? ide_msg_col : 0x0064748Bu, 1);
        fb_reset_clip();
    }

    /* SOMA_240_ICONOS: nombre del boton de solo icono bajo el raton */
    for (int i = 0; i < ide_nb; i++) {
        if (ide_btns[i].id != ide_hover_id || !ide_btns[i].tip) continue;
        int tw = cdr_tw(ide_btns[i].tip) + 20, tx = ide_btns[i].x + ide_btns[i].w / 2 - tw / 2, ty = ide_btns[i].y + ide_btns[i].h + 6;
        if (tx < x0) tx = x0;
        fb_fill_rrect_aa(tx, ty, tw, 24, 6, 0x00020617u);
        fb_stroke_rrect_aa(tx, ty, tw, 24, 6, 0xFF334155u);
        font_draw_string((uint32_t)(tx + 10), (uint32_t)(ty + 7), ide_btns[i].tip, CDR_C_WHITE, 1);
        break;
    }
}

static void ide_action(int act)
{
    switch (act) {
    case CE_ACT_SAVE: ide_save(); break;
    case CE_ACT_RUN: ide_run(); break;
    case CE_ACT_VERIFY: ide_verify(); break;
    case CE_ACT_NEW: { char t[96]; cdr_copy(t, sizeof t, cdr_base()); cdr_cat(t, sizeof t, "nuevo.soma"); ce_new(t); } ce_prompt(CE_PR_SAVEAS); ide_say("Archivo nuevo: escribe la ruta y pulsa Intro (o Esc y guardalo mas tarde).", CDR_C_AMBER); break;
    case CE_ACT_OPEN: { char p[128]; cdr_copy(p, sizeof p, ce_prompt_value()); if (p[0] != '/') { ide_say("La ruta debe empezar por /", CDR_C_RED); break; } ide_open(p); break; }
    case CE_ACT_SAVEAS: {
        char p[128]; cdr_copy(p, sizeof p, ce_prompt_value());
        if (p[0] != '/' || p[cdr_len(p) - 1] == '/') { ide_say("Escribe una ruta completa, por ejemplo /workspace/mi_app.soma", CDR_C_RED); break; }
        if (ce_save_as(p)) { char m[160]; cdr_copy(m, sizeof m, "Guardado como "); cdr_cat(m, sizeof m, p); ide_say(m, CDR_C_GREEN); cdr_copy(mod_current_file, sizeof mod_current_file, p); cdr_scan_projects(); }
        else ide_say("No se pudo guardar en esa ruta.", CDR_C_RED);
        break; }
    default: break;
    }
}
static int ide_click(int mx, int my)
{
    for (int i = 0; i < ide_nb; i++) {
        struct ide_btn *b = &ide_btns[i];
        if (mx < b->x || mx >= b->x + b->w || my < b->y || my >= b->y + b->h) continue;
        switch (b->id) {
        case IB_NEW: ide_action(CE_ACT_NEW); break;
        case IB_OPEN: ce_prompt(CE_PR_OPEN); ide_focus = 0; break;
        case IB_SAVE: ide_save(); break;
        case IB_SAVEAS: ce_prompt(CE_PR_SAVEAS); ide_focus = 0; break;
        case IB_UNDO: ce_undo(); break;
        case IB_REDO: ce_redo(); break;
        case IB_FIND: ce_prompt(CE_PR_FIND); ide_focus = 0; break;
        case IB_REPL: ce_prompt(CE_PR_REPLACE); ide_focus = 0; break;
        case IB_GOTO: ce_prompt(CE_PR_GOTO); ide_focus = 0; break;
        case IB_COMMENT: ce_toggle_comment(); break;
        case IB_VERIFY: ide_verify(); break;
        case IB_RUN: ide_run(); break;
        case IB_IA: ide_ask_soma(); break;
        case IB_IA_FIELD: { extern int keyboard_shift_held(void); ide_focus = 1; ui_tf_init(); tf_press(&tf_ia, mx, my, keyboard_shift_held()); } break;
        case IB_IA_CANCEL: coder_cancel_job(); ide_say("Cancelando la tarea de SOMA...", CDR_C_AMBER); break;
        case IB_OUT_STOP: crun_parar(); break;                                   /* SOMA_333_SALIDA */
        case IB_OUT_CLOSE: crun_cerrar(); if (ide_focus == 3) ide_focus = 0; break;
        case IB_OUT_AGAIN: if (ide_out_path[0]) cdr_run_app(ide_out_path); break;
        case IB_OUT_IN: ide_focus = 3; break;
        }
        return 1;
    }
    if (ide_ex_w && mx >= ide_ex_x && mx < ide_ex_x + ide_ex_w && my >= ide_ex_y + 34 && my < ide_ex_y + 34 + ide_ex_rows * 26) {
        int i = ide_ex_scroll + (my - ide_ex_y - 34) / 26;          /* PARCHE 101: misma geometria que el dibujo (34 + 26 por fila) */
        if (i >= 0 && i < project_count) {
            if (cdr_eq(projects[i].path, ce_path())) { ide_ex_armed = -1; return 1; }
            if (ce_dirty() && ide_ex_armed != i) { ide_ex_armed = i; ide_say("Hay cambios sin guardar. Pulsa Guardar o haz clic otra vez en el archivo para descartarlos.", CDR_C_AMBER); return 1; }
            ide_ex_armed = -1; ide_open(projects[i].path);
        }
        return 1;
    }
    if (ide_ex_w && mx >= ide_ex_x && mx < ide_ex_x + ide_ex_w && my >= ide_ex_y && my < ide_ex_y + 34) return 1;
    for (int i = 0; i < ide_prob_n; i++)
        if (ide_prob_line[i] && my >= ide_pr_y + 2 + i * 18 && my < ide_pr_y + 20 + i * 18) { ce_goto_line(ide_prob_line[i]); ide_focus = 0; return 1; }
    if (ce_press(mx, my)) { ide_focus = 0; ide_ex_armed = -1; return 1; }
    return 0;
}
static void ide_key(char c)
{
    int k = (uint8_t)c;
    if (ide_focus == 3) {                                  /* SOMA_333_SALIDA: escribiendo para el programa */
        if (crun_estado() != CRUN_MARCHA) { ide_focus = 0; }
        else {
            if (k == '\n' || k == '\r') { (void)crun_feed(ide_out_in); ide_out_in_len = 0; ide_out_in[0] = 0; ide_out_scroll = 0; }
            else if (k == 8 || k == 127) { while (ide_out_in_len > 0 && ((unsigned char)ide_out_in[ide_out_in_len - 1] & 0xC0) == 0x80) ide_out_in_len--; if (ide_out_in_len > 0) ide_out_in_len--; ide_out_in[ide_out_in_len] = 0; }
            else if (k == 9) ide_focus = 0;
            else if (k >= 32 && ide_out_in_len < (int)sizeof ide_out_in - 1) { ide_out_in[ide_out_in_len++] = c; ide_out_in[ide_out_in_len] = 0; }
            return;
        }
    }
    if (ide_focus == 1) {                                  /* PARCHE 099: campo de texto completo */
        extern int keyboard_last_mods(void);
        int md = keyboard_last_mods();
        ui_tf_init();
        int r = tf_key(&tf_ia, c, (md & 1) || keyboard_ctrl_held(), (md & 2) || keyboard_shift_held());
        ide_ia_len = (int)cdr_len(ide_ia);
        if (r == TF_ESC || r == TF_TAB) ide_focus = 0;
        else if (r == TF_ENTER) ide_ask_soma();
        (void)k;
        return;
    }
    extern int keyboard_last_mods(void);
    int md = keyboard_last_mods();                    /* PARCHE 094: Ctrl/Mayus guardados con la propia tecla */
    int act = ce_key(c, (md & 1) || keyboard_ctrl_held(), (md & 2) || keyboard_shift_held());
    if (act >= CE_ACT_SAVE) ide_action(act);
}


/* ========================================================================
 * PARCHE 099: pestanas CREAR CON IA, PROYECTOS y REGISTRO rehechas.
 * - Campos de texto de verdad (textfield.c): cursor, seleccion con raton y teclado, doble clic, Ctrl+A, portapapeles.
 * - Registro de zonas pulsables: cada boton se registra al pintarlo, asi el clic cae SIEMPRE donde se ve.
 * ======================================================================== */
enum { UB_GEN = 1, UB_CANCEL, UB_RUN, UB_EDIT, UB_TPL0, UB_TPL_END = UB_TPL0 + 8, UB_AUTO, UB_LSOMA, UB_LPY, UB_LC, UB_LJS, UB_LHTML,
       UB_PRUN, UB_PEDIT, UB_PNOTES, UB_PDUP, UB_PREN, UB_PDEL, UB_PDELOK, UB_PROW0 = 100, UB_PROW_END = 400,
       UB_LCOPY = 500, UB_LTRACE, UB_LCLEAR };
struct ui_hit { int x, y, w, h, id; };
static struct ui_hit ui_hits[96]; static int ui_nh;
static void ui_reg(int x, int y, int w, int h, int id) { if (ui_nh < 96) { ui_hits[ui_nh].x = x; ui_hits[ui_nh].y = y; ui_hits[ui_nh].w = w; ui_hits[ui_nh].h = h; ui_hits[ui_nh].id = id; ui_nh++; } }
static int ui_btn_w(const char *t, int icon) { return cdr_tw(t) + 28 + (icon ? CDR_ICO + 7 : 0); }
static int ui_btn_i(int x, int y, int icon, const char *t, int id, uint32_t bg, uint32_t br, uint32_t tx)
{   /* SOMA_240_ICONOS: boton con icono vectorial */
    int w = ui_btn_w(t, icon);
    cdr_draw_btn_i((uint32_t)x, (uint32_t)y, (uint32_t)w, 32, icon, t, bg, br, tx, 0);
    if (id) ui_reg(x, y, w, 32, id);
    return w;
}
/* texto con ajuste de linea por palabras; devuelve la y final */
static int ui_wrap(int x, int y, int w, const char *t, uint32_t col, int max_lines, int lh)
{
    char line[256]; int n = 0, lines = 0;
    const char *p = t;
    while (*p && lines < max_lines) {
        line[0] = 0; n = 0;
        const char *best = p; int bn = 0;
        const char *q = p;
        while (*q && *q != '\n' && n < 250) {
            line[n++] = *q; line[n] = 0;
            if (cdr_tw(line) > w) { n--; line[n] = 0; break; }
            q++;
            if (*q == ' ' || *q == 0 || *q == '\n') { best = q; bn = n; }
        }
        if (*q && *q != '\n' && bn > 0 && n < 250) { line[bn] = 0; q = best; }
        font_draw_string((uint32_t)x, (uint32_t)y, line, col, 1);
        y += lh; lines++;
        p = q; while (*p == ' ' || *p == '\n') p++;
        if (q == p && !*p) break;
    }
    return y;
}
static void ui_card_i(int x, int y, int w, int h, int icon, uint32_t accent, const char *title)
{   /* SOMA_240_ICONOS: tarjeta con icono en una pastilla delante del titulo */
    fb_fill_rrect_aa(x, y, w, h, 12, 0x000C1220u);
    fb_stroke_rrect_aa(x, y, w, h, 12, 0xFF1A2335u);
    int tx = x + 16;
    if (icon) {
        fb_fill_rrect_aa(x + 12, y + 9, 22, 22, 7, si_mix(0x000C1220u, accent, 40));
        si_icon(icon, x + 23, y + 20, 14, accent);
        tx = x + 42;
    }
    if (title) font_draw_string((uint32_t)tx, (uint32_t)(y + 14), title, CDR_C_DIM, 1);
}

/* ---- CREAR CON IA ---- */
static const char *const ui_tpl_name[] = { "Calculadora", "Snake", "Pong", "Contador", "Reloj", "Lista de tareas", "Paint", "Conversor" };
static const char *const ui_tpl_text[] = {
    "Una calculadora con ventana, display grande y botones para numeros, + - * / y C, que se usan con el raton",
    "El juego Snake: la serpiente se mueve con W A S D, come manzanas, crece, cuenta puntos y termina al chocar consigo misma",
    "Pong para un jugador contra el ordenador: pala con W y S, pelota que rebota y marcador",
    "Un contador grafico con botones +1, -1 y Reset, con el numero bien grande en el centro",
    "Un reloj y cronometro en ventana: muestra el tiempo desde el arranque y tiene botones Iniciar, Parar y Reiniciar",
    "Una lista de tareas por consola: anadir, listar, marcar como hecha y borrar tareas, guardandolas en /workspace/tareas.txt",
    "Un programa de dibujo: pintar con el raton en la ventana, paleta de 6 colores y boton para borrar el lienzo",
    "Un conversor de unidades por consola: kilometros a millas, grados Celsius a Fahrenheit y kilos a libras" };
#define UI_NTPL 8

static void ui_stepper(int x, int y, int w, int phase, int state)
{
    static const char *const lab[6] = { "Analisis", "Codigo", "Verificar", "Reparar", "Revision", "Entrega" };
    int step = w / 6;
    for (int i = 0; i < 6; i++) {
        int cx = x + step * i + step / 2, cy = y + 12;
        int done = (state == CODER_JOB_SUCCESS) || (i + 1 < phase);
        int cur = (state == CODER_JOB_WORKING && i + 1 == phase);
        int err = (state == CODER_JOB_ERROR && i + 1 == phase);
        if (i < 5) fb_fill_rect((uint32_t)(cx + 12), (uint32_t)cy, (uint32_t)(step - 24), 2, done ? CDR_C_GREEN : 0x00263043u);
        uint32_t c = err ? CDR_C_RED : done ? CDR_C_GREEN : cur ? CDR_C_CYAN : 0x00263043u;
        /* SOMA_240_ICONOS: cada paso con su icono; los hechos con una marca */
        static const int sic[6] = { SI_SEARCH, SI_CODE, SI_CHECK, SI_EDIT, SI_LIST, SI_SAVE };
        if (cur) fb_fill_rrect_aa(cx - 16, cy - 16, 32, 32, 16, si_mix(0x000C1220u, c, 60));
        fb_fill_rrect_aa(cx - 12, cy - 12, 24, 24, 12, (done || cur || err) ? c : 0x00172131u);
        if (!(done || cur || err)) fb_stroke_rrect_aa(cx - 12, cy - 12, 24, 24, 12, 0xFF263043u);
        si_icon(err ? SI_CLOSE : done ? SI_CHECK : sic[i], cx, cy, 13, (done || cur || err) ? 0x00081018u : CDR_C_GRAY);
        int lw = cdr_tw(lab[i]);
        font_draw_string((uint32_t)(cx - lw / 2), (uint32_t)(cy + 18), lab[i], cur ? CDR_C_WHITE : done ? 0x00A7F3D0u : CDR_C_DIM, 1);
    }
}

static void ui_render_new(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, const struct cdr_snap *sn)
{
    int x0 = (int)wx + 12, w = (int)ww - 24, y0 = (int)wy + 54, y1 = (int)(wy + wh) - 10;
    int running = sn->state == CODER_JOB_WORKING;
    ui_tf_init(); ui_nh = 0;
    int wide = w >= 980;
    int lw = wide ? (w * 52) / 100 : w;
    /* tarjeta de la orden */
    int ch = 336;                                       /* SOMA_239: +36 por la fila del lenguaje */
    ui_card_i(x0, y0, lw, ch, SI_EDIT, CDR_C_GREEN, "DESCRIBE LA APLICACION");
    tf_render(&tf_cprompt, x0 + 16, y0 + 36, lw - 32, 104, ui_focus == &tf_cprompt,
              "Ejemplo: un juego de Snake con puntuacion que se maneja con W A S D...", CDR_C_WHITE);
    /* plantillas */
    int cx = x0 + 16, cy = y0 + 152;
    si_icon(SI_AUTO, cx + 6, cy + 6, 13, CDR_C_AMBER);
    font_draw_string((uint32_t)cx + 18, (uint32_t)(cy + 2), "Ideas:", CDR_C_DIM, 1);
    cx += 18 + cdr_tw("Ideas:") + 10;
    for (int i = 0; i < UI_NTPL; i++) {
        int tw = cdr_tw(ui_tpl_name[i]) + 22;
        if (cx + tw > x0 + lw - 16) { cx = x0 + 16 + 18 + cdr_tw("Ideas:") + 10; cy += 30; }
        fb_fill_rrect_aa(cx, cy - 4, tw, 24, 12, 0x00162036u);
        fb_stroke_rrect_aa(cx, cy - 4, tw, 24, 12, 0xFF1F2A3Du);
        font_draw_string((uint32_t)(cx + 11), (uint32_t)(cy + 2), ui_tpl_name[i], 0x00BFD3EAu, 1);
        ui_reg(cx, cy - 4, tw, 24, UB_TPL0 + i);
        cx += tw + 6;
    }
    int fy = cy + 34;
    {   /* SOMA_239_PYTHON_GRAFICO: lenguaje del programa nuevo */
        font_draw_string((uint32_t)(x0 + 16), (uint32_t)(fy + 12), "Lenguaje", CDR_C_DIM, 1);
        int lx = x0 + 86, sp = cdr_lang == CC_LANG_PYTHON, sc = cdr_lang == CC_LANG_C, sj = cdr_lang == CC_LANG_JS, sh = cdr_lang == CC_LANG_HTML, ss = !sp && !sc && !sj && !sh;   /* SOMA_265_CC: + C; SOMA_275_HTML: + JS y HTML */
        lx += ui_btn_i(lx, fy, ss ? SI_CHECK : SI_CODE, "SOMA-LANG", UB_LSOMA, ss ? 0x00173A7Au : CDR_C_PANEL, ss ? 0x0060A5FAu : CDR_C_BORDER2, ss ? CDR_C_WHITE : CDR_C_SOFT) + 8;
        lx += ui_btn_i(lx, fy, sp ? SI_CHECK : SI_TERMINAL, "Python", UB_LPY, sp ? 0x003A2A0Au : CDR_C_PANEL, sp ? CDR_C_AMBER : CDR_C_BORDER2, sp ? 0x00FDE68Au : CDR_C_SOFT) + 8;
        lx += ui_btn_i(lx, fy, sc ? SI_CHECK : SI_FILE_CODE, "C", UB_LC, sc ? 0x002A1F4Au : CDR_C_PANEL, sc ? CDR_C_NUM : CDR_C_BORDER2, sc ? 0x00DDD6FEu : CDR_C_SOFT) + 8;
        lx += ui_btn_i(lx, fy, sj ? SI_CHECK : SI_FILE_CODE, "JavaScript", UB_LJS, sj ? 0x00332B05u : CDR_C_PANEL, sj ? 0x00F7DF1Eu : CDR_C_BORDER2, sj ? 0x00FEF08Au : CDR_C_SOFT) + 8;
        lx += ui_btn_i(lx, fy, sh ? SI_CHECK : SI_FILE_CODE, "HTML", UB_LHTML, sh ? 0x00391A07u : CDR_C_PANEL, sh ? 0x00FB923Cu : CDR_C_BORDER2, sh ? 0x00FED7AAu : CDR_C_SOFT) + 10;
        font_draw_string((uint32_t)lx, (uint32_t)(fy + 12), sp ? ".py: ventanas con soma.win" : sc ? ".c: TinyCC; ventanas con soma.h" : sj ? ".js: ventanas con win.*" : sh ? ".html: pagina web de un archivo" : ".soma: el lenguaje de SOMA", 0x00475569u, 1);
        fy += 36;
    }
    font_draw_string((uint32_t)(x0 + 16), (uint32_t)(fy + 11), "Archivo", CDR_C_DIM, 1);
    tf_render(&tf_cfile, x0 + 86, fy, lw - 102 - 70, 30, ui_focus == &tf_cfile, "automatico (se deduce de la descripcion)", CDR_C_AMBER);
    { int bw = 64; cdr_draw_btn_i((uint32_t)(x0 + lw - 16 - bw), (uint32_t)fy, (uint32_t)bw, 30, SI_RESTART, "Auto", CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT, 0); ui_reg(x0 + lw - 16 - bw, fy, bw, 30, UB_AUTO); }
    int by = fy + 44, bx = x0 + 16;
    if (running) {
        bx += ui_btn_i(bx, by, SI_TIMER, "Generando...", 0, 0x00122B57u, 0x0060A5FAu, 0x00BFDBFEu) + 8;
        bx += ui_btn_i(bx, by, SI_CLOSE, "Cancelar", UB_CANCEL, 0x003A121Du, CDR_C_RED, 0x00FECDD3u) + 8;
    } else {
        bx += ui_btn_i(bx, by, SI_AUTO, "Generar con IA", UB_GEN, 0x00065F46u, CDR_C_GREEN, CDR_C_WHITE) + 8;
        if (sn->state == CODER_JOB_SUCCESS) {
            bx += ui_btn_i(bx, by, SI_PLAY, "Probar", UB_RUN, 0x00173A7Au, 0x0060A5FAu, CDR_C_WHITE) + 8;
            bx += ui_btn_i(bx, by, SI_EDIT, "Abrir en el editor", UB_EDIT, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT) + 8;
        }
    }
    font_draw_string((uint32_t)bx + 6, (uint32_t)(by + 10), running ? "" : "Ctrl+Intro tambien genera", 0x00475569u, 1);
    if (by + 44 > y0 + ch) ch = by + 44 - y0;   /* (la tarjeta ya se pinto con altura fija; los botones caben) */

    if (wide && y1 - (y0 + ch + 10) > 150) {                     /* consejos */
        int ty = y0 + ch + 10;
        ui_card_i(x0, ty, lw, y1 - ty, SI_COMMENT, CDR_C_CYAN, "COMO PEDIRLO BIEN");
        static const char *const tips[] = {
            "Di QUE hace y COMO se usa: teclas, botones, que se ve en pantalla.",
            "Si es un juego, explica cuando se gana o se pierde y que puntua.",
            "Puedes dejar el archivo en blanco: se elige un nombre que no pisa ningun programa.",
            "Al terminar: Probar lo ejecuta en su ventana y Abrir en el editor te deja retocarlo.",
            "Atajos: Tab cambia de campo, Ctrl+Intro genera, doble clic selecciona una palabra." };
        int yy = ty + 42;
        for (int i = 0; i < 5 && yy + 18 < y1; i++) {
            si_icon(i == 4 ? SI_AUTO : SI_CHECK, x0 + 22, yy + 7, 12, i == 4 ? CDR_C_CYAN : CDR_C_GREEN);
            yy = ui_wrap(x0 + 34, yy, lw - 52, tips[i], 0x00AEB9CCu, 2, 17) + 8;
        }
    }
    /* tarjeta de progreso */
    int px = wide ? x0 + lw + 10 : x0, pw = wide ? w - lw - 10 : w;
    int py = wide ? y0 : y0 + ch + 10, ph = y1 - py;
    if (ph < 120) return;
    ui_card_i(px, py, pw, ph, SI_GAUGE, 0x0060A5FAu, "PROGRESO DEL PROGRAMADOR");
    if (sn->target_file[0]) { int tw = cdr_tw(sn->target_file); font_draw_string((uint32_t)(px + pw - tw - 16), (uint32_t)(py + 14), sn->target_file, CDR_C_AMBER, 1); }
    ui_stepper(px + 10, py + 42, pw - 20, sn->phase, sn->state);
    int ty = py + 96;
    uint32_t dc = sn->state == CODER_JOB_SUCCESS ? CDR_C_GREEN : sn->state == CODER_JOB_ERROR ? 0x00FB7185u : CDR_C_WHITE;
    ty = ui_wrap(px + 16, ty, pw - 32, sn->phase_desc[0] ? sn->phase_desc : "Escribe que quieres crear y pulsa Generar con IA.", dc, 2, 18) + 6;
    if (sn->plan[0]) {
        font_draw_string((uint32_t)(px + 16), (uint32_t)ty, "PLAN", CDR_C_DIM, 1); ty += 18;
        ty = ui_wrap(px + 16, ty, pw - 32, sn->plan, 0x00CBD5E1u, 4, 17) + 6;
    }
    if (running && sn->live_thought[0]) {
        fb_fill_rounded_rect(px + 12, ty, pw - 24, 44, 8, 0x00101C33u);
        ui_wrap(px + 24, ty + 6, pw - 48, sn->live_thought, 0x0093C5FDu, 2, 16);
        ty += 52;
    }
    /* bitacora reciente */
    if (ty + 40 < py + ph) {
        font_draw_string((uint32_t)(px + 16), (uint32_t)ty, "BITACORA", CDR_C_DIM, 1); ty += 20;
        int rows = (py + ph - 10 - ty) / 17;
        int start = sn->log_n - rows; if (start < 0) start = 0;
        fb_set_clip(px + 12, ty, pw - 24, py + ph - 8 - ty);
        for (int i = start; i < sn->log_n; i++) {
            const char *l = sn->log[i];
            uint32_t c = (cdr_has_ci(l, "error") || cdr_has_ci(l, "fall") || cdr_has_ci(l, "no se")) ? 0x00FDA4AFu :
                         (cdr_has_ci(l, "correct") || cdr_has_ci(l, "verificado") || cdr_has_ci(l, "guardado")) ? 0x0086EFACu : 0x0094A3B8u;
            font_draw_string((uint32_t)(px + 16), (uint32_t)ty, l, c, 1); ty += 17;
        }
        fb_reset_clip();
    }
}

/* ---- PROYECTOS ---- */
static int ui_proj_scroll, ui_proj_rows, ui_list_x, ui_list_y, ui_list_w, ui_list_h;
static char ui_pmsg[128]; static uint32_t ui_pmsg_col;
static int ui_proj_visible(int i) { return !ui_search[0] || cdr_has_ci(projects[i].path, ui_search); }
static void ui_select_project(int i)
{
    project_selected = i; proj_del_armed = -1; ui_pmsg[0] = 0;
    if (i >= 0 && i < project_count) { tf_set(&tf_rename, projects[i].path); cdr_load_mod_file(projects[i].path); }
}
static void ui_render_projects(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh)
{
    int x0 = (int)wx + 12, w = (int)ww - 24, y0 = (int)wy + 54, y1 = (int)(wy + wh) - 10;
    ui_tf_init(); ui_nh = 0;
    int lw = w >= 760 ? 360 : w;
    ui_card_i(x0, y0, lw, y1 - y0, SI_GRID, CDR_C_CYAN, "PROGRAMAS (SOMA-LANG Y PYTHON)");
    { char n[12]; n[0] = 0; cdr_cat_u32(n, sizeof n, (uint32_t)project_count); int nw = cdr_tw(n) + 14;
      fb_fill_rounded_rect(x0 + lw - nw - 14, y0 + 10, nw, 18, 9, 0x001A2335u); font_draw_string((uint32_t)(x0 + lw - nw - 7), (uint32_t)(y0 + 14), n, CDR_C_GRAY, 1); }
    tf_render(&tf_search, x0 + 12, y0 + 38, lw - 24, 30, ui_focus == &tf_search, "Buscar por nombre o carpeta...", CDR_C_WHITE);
    ui_list_x = x0 + 8; ui_list_y = y0 + 78; ui_list_w = lw - 16; ui_list_h = y1 - 8 - ui_list_y;
    ui_proj_rows = ui_list_h / 40;
    int vis = 0; for (int i = 0; i < project_count; i++) if (ui_proj_visible(i)) vis++;
    if (ui_proj_scroll > vis - ui_proj_rows) ui_proj_scroll = vis - ui_proj_rows;
    if (ui_proj_scroll < 0) ui_proj_scroll = 0;
    int ry = ui_list_y, k = 0;
    for (int i = 0; i < project_count; i++) {
        if (!ui_proj_visible(i)) continue;
        if (k++ < ui_proj_scroll) continue;
        if (ry + 38 > ui_list_y + ui_list_h) break;
        int sel = i == project_selected;
        if (sel) { fb_fill_rounded_rect(ui_list_x, ry, ui_list_w, 36, 8, 0x00172A4Au); fb_fill_rounded_rect(ui_list_x, ry + 8, 3, 20, 1, CDR_C_CYAN); }
        si_icon(SI_FILE_CODE, ui_list_x + 19, ry + 18, 22, cdr_file_col(&projects[i], sel));
        fb_set_clip(ui_list_x + 34, ry, ui_list_w - 40, 36);
        font_draw_string((uint32_t)(ui_list_x + 36), (uint32_t)(ry + 5), projects[i].name, sel ? CDR_C_WHITE : 0x00D5DCE8u, 1);
        font_draw_string((uint32_t)(ui_list_x + 36), (uint32_t)(ry + 21), projects[i].path, 0x00566479u, 1);
        fb_reset_clip();
        ui_reg(ui_list_x, ry, ui_list_w, 36, UB_PROW0 + i);
        ry += 40;
    }
    if (!vis) font_draw_string((uint32_t)(ui_list_x + 12), (uint32_t)(ui_list_y + 8), ui_search[0] ? "Ningun programa coincide." : "Aun no hay programas .soma, .py, .c, .js ni .html", CDR_C_DIM, 1);
    if (w < 760) return;
    /* detalle */
    int dx = x0 + lw + 10, dw = w - lw - 10;
    ui_card_i(dx, y0, dw, y1 - y0, SI_FILE, 0x0060A5FAu, "DETALLE");
    if (project_selected < 0 || project_selected >= project_count) {      /* SOMA_240_ICONOS: estado vacio con icono */
        int cy = y0 + (y1 - y0) / 2 - 40;
        si_icon(SI_FILE_CODE, dx + dw / 2, cy, 56, 0x00334155u);
        const char *t = "Selecciona un programa de la lista."; font_draw_string((uint32_t)(dx + dw / 2 - cdr_tw(t) / 2), (uint32_t)(cy + 44), t, CDR_C_DIM, 1);
        t = "Desde aqui puedes ejecutarlo, abrirlo en el editor, duplicarlo, moverlo o borrarlo.";
        font_draw_string((uint32_t)(dx + dw / 2 - cdr_tw(t) / 2), (uint32_t)(cy + 64), t, 0x00475569u, 1);
        return; }
    struct cdr_proj *it = &projects[project_selected];
    int ty = y0 + 44;
    si_badge(dx + 16, ty, 48, cdr_is_ext(it->path) ? SI_TERMINAL : it->is_gui ? SI_SCREEN : SI_FILE_CODE, cdr_file_col(it, 1));
    font_draw_string((uint32_t)(dx + 80), (uint32_t)(ty + 10), it->name, CDR_C_WHITE, 1);
    { char inf[96]; inf[0] = 0; cdr_cat(inf, sizeof inf, cdr_lang_label(it->path)); cdr_cat(inf, sizeof inf, "  \xC2\xB7  ");
      cdr_cat(inf, sizeof inf, cdr_is_html(it->path) ? "Pagina web" : it->is_gui ? "Aplicacion con ventana" : "Programa de consola");
      cdr_cat(inf, sizeof inf, "  \xC2\xB7  "); cdr_cat_u32(inf, sizeof inf, it->lines); cdr_cat(inf, sizeof inf, " lineas  \xC2\xB7  ");
      cdr_fmt_size(inf + cdr_len(inf), sizeof inf - cdr_len(inf), it->size);
      font_draw_string((uint32_t)(dx + 80), (uint32_t)(ty + 30), inf, CDR_C_GRAY, 1); }
    ty += 70;
    int bx = dx + 16;
    bx += ui_btn_i(bx, ty, SI_PLAY, "Ejecutar", UB_PRUN, 0x00173A7Au, 0x0060A5FAu, CDR_C_WHITE) + 8;
    bx += ui_btn_i(bx, ty, SI_EDIT, "Abrir en el editor", UB_PEDIT, 0x00065F46u, CDR_C_GREEN, CDR_C_WHITE) + 8;
    bx += ui_btn_i(bx, ty, SI_FILE, "Abrir en Notas", UB_PNOTES, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT) + 8;
    bx += ui_btn_i(bx, ty, SI_COPY, "Duplicar", UB_PDUP, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT) + 8;
    ty += 52;
    font_draw_string((uint32_t)(dx + 16), (uint32_t)ty, "NOMBRE Y RUTA", CDR_C_DIM, 1); ty += 20;
    { int bw = ui_btn_w("Renombrar / mover", SI_EDIT);
      tf_render(&tf_rename, dx + 16, ty, dw - 32 - bw - 8, 32, ui_focus == &tf_rename, "/workspace/nombre.soma", CDR_C_AMBER);
      ui_btn_i(dx + dw - 16 - bw, ty, SI_EDIT, "Renombrar / mover", UB_PREN, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT); }
    ty += 44;
    font_draw_string((uint32_t)(dx + 16), (uint32_t)ty, "Edita la ruta (doble clic selecciona una palabra) y pulsa Intro o el boton.", 0x00475569u, 1);
    ty += 30;
    if (proj_del_armed == project_selected) {
        int w1 = ui_btn_i(dx + 16, ty, SI_TRASH, "Si, borrar definitivamente", UB_PDELOK, 0x007F1D1Du, CDR_C_RED, CDR_C_WHITE);
        ui_btn_i(dx + 24 + w1, ty, SI_CLOSE, "No", UB_PDEL, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT);
    } else ui_btn_i(dx + 16, ty, SI_TRASH, "Borrar", UB_PDEL, 0x002A1015u, 0x009F1239u, 0x00FDA4AFu);
    ty += 46;
    if (ui_pmsg[0]) ui_wrap(dx + 16, ty, dw - 32, ui_pmsg, ui_pmsg_col, 3, 17);
}

/* ---- REGISTRO ---- */
static int ui_log_scroll;
static void ui_render_log(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, const struct cdr_snap *sn)
{
    int x0 = (int)wx + 12, w = (int)ww - 24, y0 = (int)wy + 54, y1 = (int)(wy + wh) - 10;
    ui_tf_init(); ui_nh = 0;
    int lh = (y1 - y0) * 62 / 100;
    ui_card_i(x0, y0, w, lh, SI_LIST, 0x0060A5FAu, "BITACORA DEL ULTIMO TRABAJO");
    int bx = x0 + w - 16;
    { int bw = ui_btn_w("Copiar", SI_COPY); bx -= bw; ui_btn_i(bx, y0 + 6, SI_COPY, "Copiar", UB_LCOPY, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT); }
    { int bw = ui_btn_w("Traza completa", SI_LIST); bx -= bw + 8; ui_btn_i(bx, y0 + 6, SI_LIST, "Traza completa", UB_LTRACE, CDR_C_PANEL, CDR_C_BORDER2, CDR_C_SOFT); }
    int rows = (lh - 56) / 18;
    if (ui_log_scroll > sn->log_n - rows) ui_log_scroll = sn->log_n - rows;
    if (ui_log_scroll < 0) ui_log_scroll = 0;
    int ty = y0 + 46;
    fb_set_clip(x0 + 8, ty, w - 16, lh - 52);
    for (int i = ui_log_scroll; i < sn->log_n && i < ui_log_scroll + rows; i++) {
        const char *l = sn->log[i];
        uint32_t c = (cdr_has_ci(l, "error") || cdr_has_ci(l, "fall") || cdr_has_ci(l, "no se")) ? 0x00FDA4AFu :
                     (cdr_has_ci(l, "correct") || cdr_has_ci(l, "verificado") || cdr_has_ci(l, "guardado")) ? 0x0086EFACu :
                     (cdr_has_ci(l, "normaliz") || cdr_has_ci(l, "metrica")) ? 0x0093C5FDu : 0x00CBD5E1u;
        fb_fill_rounded_rect(x0 + 18, ty + 6, 5, 5, 2, c);
        font_draw_string((uint32_t)(x0 + 30), (uint32_t)ty, l, c, 1);
        ty += 18;
    }
    fb_reset_clip();
    if (!sn->log_n) {
        int cy = y0 + lh / 2 - 20;
        si_icon(SI_LIST, x0 + w / 2, cy, 44, 0x00334155u);
        const char *t = "Todavia no hay ningun trabajo registrado."; font_draw_string((uint32_t)(x0 + w / 2 - cdr_tw(t) / 2), (uint32_t)(cy + 36), t, CDR_C_DIM, 1);
        t = "Aqui aparece paso a paso lo que hace SOMA al crear o modificar un programa.";
        font_draw_string((uint32_t)(x0 + w / 2 - cdr_tw(t) / 2), (uint32_t)(cy + 56), t, 0x00475569u, 1);
    }
    if (sn->log_n > rows) { int th = (lh - 56) * rows / sn->log_n, tyy = y0 + 46 + (lh - 56 - th) * ui_log_scroll / (sn->log_n - rows); fb_fill_rounded_rect(x0 + w - 10, tyy, 3, th, 1, 0x00334155u); }
    /* lint */
    int ly = y0 + lh + 10;
    ui_card_i(x0, ly, w, y1 - ly, SI_SEARCH, CDR_C_AMBER, "ANALISIS ESTATICO DEL ULTIMO PROGRAMA");
    int yy = ly + 42;
    if (!sn->lint_n) { si_icon(SI_CHECK, x0 + 24, yy + 4, 13, 0x0086EFACu); font_draw_string((uint32_t)(x0 + 38), (uint32_t)yy, "Sin avisos: codigo limpio.", 0x0086EFACu, 1); }
    for (int i = 0; i < sn->lint_n && yy + 18 < y1; i++) {
        uint32_t c = sn->lint[i][0] == 'E' ? 0x00FDA4AFu : CDR_C_AMBER;
        fb_fill_rounded_rect(x0 + 18, yy + 5, 7, 7, 3, c);
        font_draw_string((uint32_t)(x0 + 32), (uint32_t)yy, sn->lint[i], c, 1);
        yy += 20;
    }
}

/* ---- eventos ---- */
static void ui_gen(void)
{
    cdr_sync_inputs();
    if (!input_create_prompt[0]) { ui_focus = &tf_cprompt; return; }
    coder_start_creation();
    tf_set(&tf_cfile, input_create_file);
}
static void ui_do_rename(void)
{
    if (project_selected < 0 || project_selected >= project_count) return;
    char np[96]; cdr_copy(np, sizeof np, ui_renbuf);
    if (np[0] != '/') { char t[96]; cdr_copy(t, sizeof t, cdr_base()); cdr_cat(t, sizeof t, np); cdr_copy(np, sizeof np, t); }
    const char *op = projects[project_selected].path;
    if (!cdr_is_prog(np)) cdr_cat(np, sizeof np, cdr_ext_of(op));
    if (cdr_eq(np, op)) { cdr_copy(ui_pmsg, sizeof ui_pmsg, "Es la misma ruta."); ui_pmsg_col = CDR_C_AMBER; return; }
    if (vfs_size(np) > 0) { cdr_copy(ui_pmsg, sizeof ui_pmsg, "Ya existe un archivo con esa ruta; elige otro nombre."); ui_pmsg_col = 0x00FB7185u; return; }
    uint32_t len = 0; char *d = vfs_read_alloc(op, &len);
    if (!d) { cdr_copy(ui_pmsg, sizeof ui_pmsg, "No se pudo leer el archivo."); ui_pmsg_col = 0x00FB7185u; return; }
    int w = vfs_write(np, d, len); kfree(d);
    if (w < 0) { cdr_copy(ui_pmsg, sizeof ui_pmsg, "No se pudo escribir en esa ruta (carpeta protegida o sin espacio)."); ui_pmsg_col = 0x00FB7185u; return; }
    vfs_delete(op);
    if (cdr_eq(ce_path(), op)) ce_load(np);
    cdr_scan_projects();
    for (int i = 0; i < project_count; i++) if (cdr_eq(projects[i].path, np)) ui_select_project(i);
    cdr_copy(ui_pmsg, sizeof ui_pmsg, "Renombrado a "); cdr_cat(ui_pmsg, sizeof ui_pmsg, np); ui_pmsg_col = 0x0086EFACu;
}
static int ui_click(int mx, int my, const struct cdr_snap *sn)
{
    extern int keyboard_shift_held(void);
    ui_tf_init();
    struct tfield *fs[3]; int nf = 0;
    if (cdr_tab_cur == CDR_TAB_NEW) { fs[nf++] = &tf_cprompt; fs[nf++] = &tf_cfile; }
    if (cdr_tab_cur == CDR_TAB_PROJECTS) { fs[nf++] = &tf_search; fs[nf++] = &tf_rename; }
    for (int i = 0; i < nf; i++) if (tf_hit(fs[i], mx, my)) { ui_focus = fs[i]; tf_press(fs[i], mx, my, keyboard_shift_held()); return 1; }
    for (int i = 0; i < ui_nh; i++) {
        struct ui_hit *h = &ui_hits[i];
        if (mx < h->x || mx >= h->x + h->w || my < h->y || my >= h->y + h->h) continue;
        int id = h->id;
        if (id >= UB_TPL0 && id < UB_TPL0 + UI_NTPL) { tf_set(&tf_cprompt, ui_tpl_text[id - UB_TPL0]); tf_set(&tf_cfile, ""); ui_focus = &tf_cprompt; cdr_sync_inputs(); return 1; }
        if (id >= UB_PROW0 && id < UB_PROW_END) { ui_select_project(id - UB_PROW0); return 1; }
        const char *pp = (project_selected >= 0 && project_selected < project_count) ? projects[project_selected].path : 0;
        switch (id) {
        case UB_GEN: if (sn->state != CODER_JOB_WORKING) ui_gen(); return 1;
        case UB_CANCEL: coder_cancel_job(); return 1;
        case UB_RUN: cdr_run_app(sn->target_file); return 1;
        case UB_EDIT: cdr_open_in_editor(sn->target_file); return 1;
        case UB_LSOMA: case UB_LPY: case UB_LC: case UB_LJS: case UB_LHTML: {   /* SOMA_239: cambia el lenguaje (y la extension) */
            cdr_lang = id == UB_LPY ? CC_LANG_PYTHON : id == UB_LC ? CC_LANG_C : id == UB_LJS ? CC_LANG_JS : id == UB_LHTML ? CC_LANG_HTML : CC_LANG_SOMA;
            cdr_sync_inputs();
            if (input_create_file[0] && cdr_is_prog(input_create_file)) {
                char t[96]; cdr_copy(t, sizeof t, input_create_file); cdr_strip_ext(t); cdr_cat(t, sizeof t, cdr_lang_ext(cdr_lang)); tf_set(&tf_cfile, t); cdr_sync_inputs();
            }
            return 1;
        }
        case UB_AUTO: { char t[96]; cdr_sync_inputs(); coder_pick_target(input_create_prompt[0] ? input_create_prompt : "app", t, sizeof t); tf_set(&tf_cfile, t); ui_focus = &tf_cfile; return 1; }
        case UB_PRUN: if (pp) cdr_run_app(pp); return 1;
        case UB_PEDIT: if (pp) cdr_open_in_editor(pp); return 1;
        case UB_PNOTES: if (pp) gui_open_notepad(pp); return 1;
        case UB_PDUP: if (pp) {
            char dup[96]; uint32_t len = 0; cdr_copy(dup, sizeof dup, pp); cdr_strip_ext(dup);
            cdr_cat(dup, sizeof dup, "_copia"); cdr_cat(dup, sizeof dup, cdr_ext_of(pp));
            char *d = vfs_read_alloc(pp, &len); if (d) { vfs_write(dup, d, len); kfree(d); }
            cdr_scan_projects(); for (int k = 0; k < project_count; k++) if (cdr_eq(projects[k].path, dup)) ui_select_project(k);
            cdr_copy(ui_pmsg, sizeof ui_pmsg, "Copia creada: "); cdr_cat(ui_pmsg, sizeof ui_pmsg, dup); ui_pmsg_col = 0x0086EFACu; }
            return 1;
        case UB_PREN: ui_do_rename(); return 1;
        case UB_PDEL: proj_del_armed = (proj_del_armed == project_selected) ? -1 : project_selected; return 1;
        case UB_PDELOK: if (pp) { vfs_delete(pp); proj_del_armed = -1; project_selected = -1; cdr_scan_projects(); cdr_copy(ui_pmsg, sizeof ui_pmsg, "Borrado."); ui_pmsg_col = CDR_C_AMBER; } return 1;
        case UB_LCOPY: { static char all[CDR_LOG_N * (CDR_LOG_LEN + 1)]; all[0] = 0; for (int k = 0; k < sn->log_n; k++) { cdr_cat(all, sizeof all, sn->log[k]); cdr_cat(all, sizeof all, "\n"); } clip_set_text(all, cdr_len(all)); return 1; }
        case UB_LTRACE: gui_open_notepad("/agent/coder_ultimo.txt"); return 1;
        default: return 1;
        }
    }
    ui_focus = 0;
    return 1;
}
static void ui_key(char c)
{
    extern int keyboard_last_mods(void);
    int md = keyboard_last_mods(), ctrl = (md & 1) || keyboard_ctrl_held(), shift = (md & 2) || keyboard_shift_held();
    ui_tf_init();
    if (cdr_tab_cur == CDR_TAB_LOG) {
        if (c == 16 && ui_log_scroll > 0) ui_log_scroll--;
        if (c == 14) ui_log_scroll++;
        return;
    }
    if (cdr_tab_cur == CDR_TAB_PROJECTS && ui_focus != &tf_search && ui_focus != &tf_rename) {
        int vis[CDR_MAX_PROJECTS], nv = 0, cur = -1;
        for (int i = 0; i < project_count; i++) if (ui_proj_visible(i)) { if (i == project_selected) cur = nv; vis[nv++] = i; }
        if (c == 16 && cur > 0) { ui_select_project(vis[cur - 1]); if (cur - 1 < ui_proj_scroll) ui_proj_scroll--; return; }
        if (c == 14 && cur + 1 < nv) { ui_select_project(vis[cur + 1]); if (cur + 1 >= ui_proj_scroll + ui_proj_rows) ui_proj_scroll++; return; }
        if ((c == '\r' || c == '\n') && project_selected >= 0) { cdr_run_app(projects[project_selected].path); return; }
        if ((uint8_t)c >= 32) { ui_focus = &tf_search; }
        else return;
    }
    if (!ui_focus) { if (cdr_tab_cur == CDR_TAB_NEW) ui_focus = &tf_cprompt; else return; }
    int r = tf_key(ui_focus, c, ctrl, shift);
    if (r == TF_TAB) {
        if (ui_focus == &tf_cprompt) ui_focus = &tf_cfile; else if (ui_focus == &tf_cfile) ui_focus = &tf_cprompt;
        else if (ui_focus == &tf_search) ui_focus = &tf_rename; else ui_focus = &tf_search;
    } else if (r == TF_ENTER) {
        if (ui_focus == &tf_cprompt || ui_focus == &tf_cfile) { if (!coder_job_busy()) ui_gen(); }
        else if (ui_focus == &tf_rename) ui_do_rename();
        else if (ui_focus == &tf_search) { for (int i = 0; i < project_count; i++) if (ui_proj_visible(i)) { ui_select_project(i); break; } }
    } else if (r == TF_ESC) ui_focus = 0;
    if (ui_focus == &tf_search) ui_proj_scroll = 0;
    cdr_sync_inputs();
}
static int coder_job_busy(void) { struct cdr_snap s; cdr_snapshot(&s); return s.state == CODER_JOB_WORKING; }

void coder_app_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int is_focused)
{
    struct cdr_snap snap;
    uint32_t tab_bar_y, content_y, content_h, tx;
    (void)is_focused;

    cdr_ensure_init();
    cdr_poll_pending();
    cdr_snapshot(&snap);

    fb_fill_rect(win_x, win_y, win_w, win_h, CDR_C_BG);
    fb_draw_rect(win_x, win_y, win_w, win_h, CDR_C_BORDER);

    tab_bar_y = win_y + 6;   /* PARCHE 094 */
    fb_fill_rect(win_x + 10, tab_bar_y + 37, win_w - 20, 1, CDR_C_BORDER);   /* PARCHE 094: sin caja, linea base */

    tx = win_x + 14;
    for (int ti = 0; ti < CDR_NTABS; ti++) {             /* PARCHE 093 */
        cdr_draw_tab_btn(tx, tab_bar_y + 4, cdr_tabs[ti].w, 32, cdr_tabs[ti].t,
                         cdr_tab_cur == cdr_tabs[ti].tab || (cdr_tabs[ti].tab == CDR_TAB_EDITOR && cdr_tab_cur == CDR_TAB_MODIFY), cdr_tabs[ti].icon);
        tx += cdr_tabs[ti].w + 6;
    }

    if (win_w > 720) {
        {   const char *cl = "SOMA-LANG + Python + C"; int cw = cdr_tw(cl) + 44, cx = (int)win_x + (int)win_w - 16 - cw;
            fb_fill_rrect_aa(cx, (int32_t)(tab_bar_y + 9), cw, 22, 11, 0x00102A22u); fb_stroke_rrect_aa(cx, (int32_t)(tab_bar_y + 9), cw, 22, 11, 0x60000000u | CDR_C_GREEN);
            si_icon(SI_CODE, cx + 17, (int)tab_bar_y + 20, 13, CDR_C_GREEN); font_draw_string((uint32_t)(cx + 30), tab_bar_y + 15, cl, 0x00A7F3D0u, 1); }
    }

    content_y = tab_bar_y + 42;
    content_h = (win_h > 140) ? (win_h - 140) : 100;

    if (cdr_tab_cur == CDR_TAB_MODIFY) cdr_tab_cur = CDR_TAB_EDITOR;   /* PARCHE 093: el editor sustituye al visor */
    if (cdr_tab_cur != CDR_TAB_EDITOR) ide_track_job(&snap);
    if (cdr_tab_cur == CDR_TAB_EDITOR) {
        ide_render(win_x, win_y, win_w, win_h, &snap);
    /* PESTANA 1: CREAR */
    } else if (cdr_tab_cur == CDR_TAB_NEW) {
        ui_render_new(win_x, win_y, win_w, win_h, &snap);
    } else if (cdr_tab_cur == CDR_TAB_PROJECTS) {
        ui_render_projects(win_x, win_y, win_w, win_h);
    } else if (cdr_tab_cur == CDR_TAB_LOG) {
        ui_render_log(win_x, win_y, win_w, win_h, &snap);
    }
    (void)content_y; (void)content_h;
}

/* ------------------------------------------------------------------------
 * Manejo de eventos de ratón y teclado
 * ------------------------------------------------------------------------ */
int coder_app_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    uint32_t tab_bar_y = win_y + 6;   /* PARCHE 094: sin hueco bajo el titulo */
    struct cdr_snap snap;
    (void)win_w; (void)win_h;
    cdr_snapshot(&snap);

    /* Clics en la barra de pestañas */
    {   uint32_t tx = win_x + 14;                           /* PARCHE 093 */
        for (int ti = 0; ti < CDR_NTABS; ti++) {
            if (point_in_rect(mx, my, tx, tab_bar_y + 4, cdr_tabs[ti].w, 32)) {
                cdr_tab_cur = cdr_tabs[ti].tab;
                if (cdr_tab_cur == CDR_TAB_PROJECTS || cdr_tab_cur == CDR_TAB_EDITOR) cdr_scan_projects();
                return 0;
            }
            tx += cdr_tabs[ti].w + 6;
        }
    }
    if (cdr_tab_cur == CDR_TAB_EDITOR || cdr_tab_cur == CDR_TAB_MODIFY) { ide_click(mx, my); return 0; }

    /* PESTANA 1: CREAR */
    if (ui_click(mx, my, &snap)) return 0;
    return 0;
}

void coder_app_handle_key(char c)
{
    cdr_ensure_init();
    if (cdr_tab_cur == CDR_TAB_EDITOR || cdr_tab_cur == CDR_TAB_MODIFY) { ide_key(c); return; }   /* PARCHE 093 */

    ui_key(c);
}

int coder_app_run_path(const char *path)
{
    if (crun_estado() == CRUN_MARCHA) return -1;
    cdr_tab_cur = CDR_TAB_EDITOR;
    cdr_run_app(path);
    return crun_estado() == CRUN_MARCHA || !cdr_is_ext(path) || cdr_is_html(path) ? 0 : -2;
}
/* SOMA_333_SALIDA: la GUI lo llama a menudo; 1 si la ventana tiene que redibujarse (salida nueva, el contador de tiempo) */
int coder_app_tick(void)
{
    int r = crun_poll();                                              /* 1: hay salida nueva */
    if (!r && crun_estado() == CRUN_MARCHA) {
        static uint64_t ult; uint64_t t = timer_get_uptime_ms();
        if (t - ult >= 500) { ult = t; r = 2; }                       /* 2: solo el reloj y el cursor */
    }
    return r;
}

/* PARCHE 093: Esc, portapapeles y raton para el editor */
int coder_app_escape(void)
{
    if (cdr_tab_cur == CDR_TAB_EDITOR && ide_focus == 3) {             /* SOMA_333_SALIDA: Esc para el programa; si ya termino, cierra el panel */
        if (crun_estado() == CRUN_MARCHA) crun_parar(); else { crun_cerrar(); ide_focus = 0; }
        return 1;
    }
    if (cdr_tab_cur != CDR_TAB_EDITOR) { if (ui_focus) { ui_focus = 0; return 1; } return 0; }
    if (ide_focus == 1) { ide_focus = 0; return 1; }
    return ce_escape();
}
int coder_app_clip_op(int op)
{
    if (cdr_tab_cur == CDR_TAB_EDITOR && ide_focus == 3) return 0;      /* SOMA_333_SALIDA: escribiendo para el programa: no va al editor */
    ui_tf_init();
    if (cdr_tab_cur != CDR_TAB_EDITOR) { if (ui_focus) { tf_clip(ui_focus, op); cdr_sync_inputs(); return 1; } return 0; }
    if (ide_focus == 1) { tf_clip(&tf_ia, op); ide_ia_len = (int)cdr_len(ide_ia); return 1; }
    return ce_clip_op(op);
}
int coder_app_mouse(int32_t mx, int32_t my, int wheel, int held)
{
    ui_tf_init();
    if (cdr_tab_cur == CDR_TAB_EDITOR) {
        if (wheel && ide_out_h && mx >= ide_out_x && mx < ide_out_x + ide_out_w && my >= ide_out_y && my < ide_out_y + ide_out_h) {
            ide_out_scroll -= wheel * 3; if (ide_out_scroll < 0) ide_out_scroll = 0;            /* SOMA_333_SALIDA: rueda en la salida */
            return 1;
        }
        if (wheel && ide_ex_w && mx >= ide_ex_x && mx < ide_ex_x + ide_ex_w) {     /* PARCHE 101: rueda en el explorador */
            ide_ex_scroll += wheel * 2;
            int mxs = project_count - ide_ex_rows; if (mxs < 0) mxs = 0;
            if (ide_ex_scroll > mxs) ide_ex_scroll = mxs;
            if (ide_ex_scroll < 0) ide_ex_scroll = 0;
            return 1;
        }
        int r = ce_mouse(mx, my, held, wheel); if (tf_drag(&tf_ia, mx, my, held)) r = 1;
        {   int hv = -1;
            if (ide_ex_w && mx >= ide_ex_x && mx < ide_ex_x + ide_ex_w && my >= ide_ex_y + 34 && my < ide_ex_y + 34 + ide_ex_rows * 26) {
                hv = ide_ex_scroll + (my - ide_ex_y - 34) / 26; if (hv >= project_count) hv = -1; }
            if (hv != ide_ex_hover) { ide_ex_hover = hv; r = 1; }
        }
        {   int hb = 0;                                                  /* SOMA_240_ICONOS: botones de la barra bajo el raton */
            for (int i = 0; i < ide_nb; i++) { struct ide_btn *b = &ide_btns[i];
                if (b->id != IB_IA_FIELD && mx >= b->x && mx < b->x + b->w && my >= b->y && my < b->y + b->h) { hb = b->id; break; } }
            if (hb != ide_hover_id) { ide_hover_id = hb; r = 1; }
        }
        return r;
    }
    int r = 0;
    if (ui_focus && tf_drag(ui_focus, mx, my, held)) r = 1;
    if (wheel) {
        if (cdr_tab_cur == CDR_TAB_PROJECTS && mx >= ui_list_x && mx < ui_list_x + ui_list_w && my >= ui_list_y && my < ui_list_y + ui_list_h) { ui_proj_scroll += wheel * 2; if (ui_proj_scroll < 0) ui_proj_scroll = 0; r = 1; }
        if (cdr_tab_cur == CDR_TAB_LOG) { ui_log_scroll += wheel * 3; if (ui_log_scroll < 0) ui_log_scroll = 0; r = 1; }
        if (cdr_tab_cur == CDR_TAB_NEW && tf_hit(&tf_cprompt, mx, my)) { tf_cprompt.scroll += wheel; if (tf_cprompt.scroll < 0) tf_cprompt.scroll = 0; r = 1; }
    }
    return r;
}

/* ========================================================================
 * PARCHE 097: el programador para el agente SOMA, la terminal y los subagentes
 * Todas las misiones de programacion pasan por coder_core (antes el agente generico escribia el archivo con su bucle JSON y
 * llegaba a compilar su propio razonamiento). Todo se ve en la terminal con el prefijo [PROGRAMADOR] y cada llamada al modelo
 * (pregunta y respuesta) queda en /agent/coder_ultimo.txt para revisarla.
 * ======================================================================== */
static char cm_trace[49152]; static uint32_t cm_trace_n;
static void cm_tr_add(const char *s, uint32_t max)
{
    uint32_t n = 0; while (s[n] && n < max) n++;
    if (cm_trace_n + n + 1 >= sizeof cm_trace) { if (cm_trace_n + 32 < sizeof cm_trace) { const char *c = "\n[...traza recortada...]\n"; while (*c && cm_trace_n + 1 < sizeof cm_trace) cm_trace[cm_trace_n++] = *c++; } return; }
    for (uint32_t i = 0; i < n; i++) cm_trace[cm_trace_n++] = s[i];
    cm_trace[cm_trace_n] = 0;
}
static void cm_trace_cb(const char *kind, const char *prompt, const char *reply)
{
    cm_tr_add("\n================ ", 32); cm_tr_add(kind, 32); cm_tr_add(" ================\n--- PREGUNTA (inicio) ---\n", 64);
    cm_tr_add(prompt, 1500);
    cm_tr_add("\n--- RESPUESTA DEL MODELO ---\n", 64);
    cm_tr_add(reply, 8000);
    cm_tr_add("\n", 2);
    vfs_write("/agent/coder_ultimo.txt", cm_trace, cm_trace_n);
}
static void cm_log(const char *m) { kprint("[PROGRAMADOR] "); kprint(m); kprint("\n"); cdr_log(m); cm_tr_add("# ", 2); cm_tr_add(m, 300); cm_tr_add("\n", 2); }
void (*coder_narrador)(int fase, const char *desc, const char *detalle);   /* SOMA_290_VOZ_CODIGO: el agente cuenta en voz alta las fases */
static void cm_status(int phase, const char *desc, const char *thought)
{
    kprint("[PROGRAMADOR] "); kprint(desc); if (thought && thought[0]) { kprint("  ("); kprint(thought); kprint(")"); } kprint("\n");
    gui_thought_update(desc);
    if (coder_narrador) coder_narrador(phase, desc, thought);
}
static int cm_never(void) { return 0; }

static int cdr_has_ci(const char *h, const char *n)
{
    for (; *h; h++) { const char *a = h, *b = n; while (*a && *b) { char x = *a; if (x >= 'A' && x <= 'Z') x += 32; if (x != *b) break; a++; b++; } if (!*b) return 1; }
    return 0;
}
static int cm_word_end(char c) { return c == 0 || c == ' ' || c == '"' || c == '\'' || c == ',' || c == ')' || c == '\n' || c == ':' || c == ';'; }
void coder_pick_target(const char *mission, char *out, uint32_t cap)
{
    /* 1) una ruta .soma en la mision */
    for (const char *p = mission; *p; p++) {
        if (p[0] == '.' && p[1] == 's' && p[2] == 'o' && p[3] == 'm' && p[4] == 'a' && cm_word_end(p[5])) {
            const char *b = p; while (b > mission && !cm_word_end(b[-1]) && b[-1] != '(') b--;
            char t[96]; uint32_t n = (uint32_t)(p + 5 - b); if (n > sizeof t - 1) n = sizeof t - 1;
            for (uint32_t i = 0; i < n; i++) t[i] = b[i];
            t[n] = 0;
            if (t[0] == '/') cdr_copy(out, cap, t); else { cdr_copy(out, cap, cdr_base()); cdr_cat(out, cap, t); }
            return;
        }
    }
    /* 2) modificar "el programa" sin nombre: el ultimo con el que se trabajo */
    static const char *const mod[] = { "modifica", "mejora", "corrige", "arregla", "cambia", "anade", "añade", 0 };
    for (int i = 0; mod[i]; i++) if (cdr_has_ci(mission, mod[i]) && mod_current_file[0]) { cdr_copy(out, cap, mod_current_file); return; }
    /* 3) nombre a partir de la mision: primera palabra significativa tras el verbo */
    static const char *const skip[] = { "crea", "creame", "haz", "hazme", "programa", "programame", "escribe", "genera", "desarrolla", "construye",
        "un", "una", "el", "la", "los", "las", "de", "del", "con", "que", "app", "aplicacion", "aplicación", "programa", "juego", "sencillo", "sencilla",
        "simple", "pequeno", "pequeño", "nuevo", "nueva", "en", "soma", "soma-lang", "para", "me", "por", "favor", "python", "usando", "lenguaje", "c",
        /* SOMA_235_PROGRAMADOR: verbos con los que se suele empezar ("muestra los primos" -> primos.soma, no muestra.soma) */
        "muestra", "muestre", "mostrar", "lee", "leer", "lea", "dibuja", "calcula", "calcule", "cuenta", "di", "dime", "imprime",
        "pinta", "pide", "pida", "quiero", "necesito", "dame", "hace", "hacer", "convierte", "ordena", "busca", "juega", "numeros", "números", 0 };
    char name[40]; name[0] = 0;
    const char *p = mission;
    while (*p && !name[0]) {
        while (*p == ' ') p++;
        const char *b = p; while (*p && *p != ' ') p++;
        uint32_t n = (uint32_t)(p - b); if (!n || n > 30) continue;
        char w[32]; uint32_t k = 0;
        for (uint32_t i = 0; i < n; i++) { char c = b[i]; if (c >= 'A' && c <= 'Z') c += 32; if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') w[k++] = c; }
        w[k] = 0;
        int sk = k < 3 || b[0] == '/'; for (int i = 0; skip[i] && !sk; i++) if (cdr_eq(w, skip[i])) sk = 1;   /* SOMA_235: rutas fuera */
        if (!sk) cdr_copy(name, sizeof name, w);
    }
    if (!name[0]) cdr_copy(name, sizeof name, "app");
    const char *x = cdr_ext_for(mission);                            /* SOMA_239: .py si se programa en Python; SOMA_265_CC: .c en C */
    cdr_copy(out, cap, cdr_base()); cdr_cat(out, cap, name); cdr_cat(out, cap, x);
    for (uint32_t k = 2; vfs_size(out) > 0 && k < 100; k++) {          /* PARCHE 098: crear nunca pisa otro programa */
        cdr_copy(out, cap, cdr_base()); cdr_cat(out, cap, name); cdr_cat(out, cap, "_"); cdr_cat_u32(out, cap, k); cdr_cat(out, cap, x);
    }
}

static void cm_puts(char *o, uint32_t max, uint32_t *p, const char *s) { while (s && *s && *p + 1 < max) o[(*p)++] = *s++; o[*p] = 0; }
/* SOMA_248_DOC: revision visual del programa. Si el modelo ve imagenes, se lanza la app recien escrita, se captura su ventana, un
 * "disenador de interfaces" la critica y, si propone cambios, el programador hace UNA ronda de mejora (solo se queda si compila). */
static volatile int uiv_done; static char uiv_target[128];
static void uiv_main(void *a) { (void)a; static char o[256]; (void)cmd_lang_run(uiv_target, o, sizeof o); uiv_done = 1; }
static int coder_ui_snapshot(const char *target, uint8_t **png, uint32_t *pn)
{
    *png = 0; *pn = 0;
    if (gui_soma_win_is_active()) return -1;                        /* hay otro programa abierto: no se toca */
    cdr_copy(uiv_target, sizeof uiv_target, target); uiv_done = 0;
    uint32_t p0 = gui_soma_win_presents();
    if (!thread_create("ui_captura", uiv_main, 0)) return -1;
    for (int i = 0; i < 4000 && !uiv_done; i++) { if (gui_soma_win_is_active() && gui_soma_win_presents() - p0 >= 3) break; thread_sleep(1); }
    thread_sleep(250);
    uint32_t *px = 0; int w = 0, h = 0;
    (void)gui_soma_win_snapshot(&px, &w, &h);
    if (gui_soma_win_is_active()) gui_soma_win_close();
    for (int i = 0; i < 3000 && !uiv_done; i++) thread_sleep(1);
    if (!px) return -1;
    size_t cap = (size_t)w * (size_t)h * 3u + 65536u, wk = png_work_size(w, h);
    uint8_t *b = (uint8_t *)kmalloc(cap); void *work = kmalloc(wk);
    size_t n = (b && work) ? png_encode_rgb(px, w, h, b, cap, work) : 0;
    kfree(px); if (work) kfree(work);
    if (!n) { if (b) kfree(b); return -1; }
    (void)vfs_write("/agent/coder_captura.png", (const char *)b, (uint32_t)n);
    *png = b; *pn = (uint32_t)n;
    return 0;
}
static void coder_ui_review(struct cc_env *env, const char *target, const char *mission, struct cc_result *cr)
{
    extern int gui_is_running(void);
    if (!cr->ok || cdr_is_ext(target) || !cc_mission_is_gui(mission) || !gui_is_running() || llm_vision_unsupported()) return;
    cm_log("Revision visual: abro el programa y capturo su ventana");
    uint8_t *png; uint32_t pn;
    if (coder_ui_snapshot(target, &png, &pn) != 0) { cm_log("Revision visual: no se pudo capturar la ventana"); return; }
    static char crit[2048];
    int r = docagent_ui_review(mission, png, pn, crit, sizeof crit);
    kfree(png);
    if (r == -2) { cm_log("Revision visual: el modelo no admite imagenes (se omite)"); return; }
    if (r <= 0) { cm_log(r == 0 ? "Revision visual: la interfaz esta bien" : "Revision visual: sin respuesta del modelo"); return; }
    cm_tr_add("\nRevision visual de la captura:\n", 40); cm_tr_add(crit, 2048);
    static char m2[3072]; m2[0] = 0;
    cdr_cat(m2, sizeof m2, "modifica el programa para mejorar su diseno visual segun esta revision de una captura de su ventana (no cambies lo que funciona):\n");
    cdr_cat(m2, sizeof m2, crit);
    cdr_cat(m2, sizeof m2, "\nEncargo original: "); cdr_cat(m2, sizeof m2, mission);
    static struct cc_result cr2;
    int fn = env->force_new; env->force_new = 0;
    cc_run(env, target, m2, &cr2);
    env->force_new = fn;
    if (cr2.ok) { cr2.llm_calls += cr->llm_calls; cr2.repairs += cr->repairs; cr2.auto_fixes += cr->auto_fixes; *cr = cr2; cm_log("Revision visual: diseno mejorado"); }
    else cm_log("Revision visual: la mejora no compilo; se queda la version anterior");
}

int coder_run_mission(const char *target_in, const char *mission, char *out, uint32_t max)
{
    static struct cc_result cr;
    char target[128];
    uint32_t p = 0;
    if (target_in && target_in[0]) { if (target_in[0] == '/') cdr_copy(target, sizeof target, target_in); else { cdr_copy(target, sizeof target, cdr_base()); cdr_cat(target, sizeof target, target_in); } }
    else coder_pick_target(mission, target, sizeof target);
    cm_trace_n = 0; cm_trace[0] = 0;
    cm_tr_add("Mision: ", 16); cm_tr_add(mission, 600); cm_tr_add("\nArchivo: ", 16); cm_tr_add(target, 128); cm_tr_add("\n", 2);
    kprint("\n[PROGRAMADOR] Mision: "); kprint(mission); kprint("\n[PROGRAMADOR] Archivo: "); kprint(target); kprint("\n");
    struct cc_env env;
    for (uint32_t zi = 0; zi < sizeof env; zi++) ((uint8_t *)&env)[zi] = 0;
    env.llm = cdr_env_llm; env.read = cdr_env_read; env.write = cdr_env_write; env.log = cm_log; env.status = cm_status;
    env.cancelled = cm_never; env.max_llm_calls = 7; env.trace = cm_trace_cb;
    cdr_env_python(&env, target);                                            /* SOMA_239 */
    env.force_new = (vfs_size(target) > 0 && !cdr_has_ci(mission, "modific") && !cdr_has_ci(mission, "mejora") && !cdr_has_ci(mission, "corrige")
                     && !cdr_has_ci(mission, "arregla") && !cdr_has_ci(mission, "cambia") && !cdr_has_ci(mission, "anade") && !cdr_has_ci(mission, "añade")) ? 1 : 0;
    cc_run(&env, target, mission, &cr);
    coder_ui_review(&env, target, mission, &cr);                              /* SOMA_248_DOC */
    vfs_write("/agent/coder_ultimo.txt", cm_trace, cm_trace_n);
    cdr_copy(mod_current_file, sizeof mod_current_file, target);
    g_need_rescan = 1; g_need_reload = 1;
    if (out && max) {
        out[0] = 0;
        if (cr.ok) {
            cm_puts(out, max, &p, "Programa listo: "); cm_puts(out, max, &p, target);
            if (cdr_is_py(target)) {
                cm_puts(out, max, &p, "\nPython: compila y pasa las comprobaciones.");
                cm_puts(out, max, &p, "\nEjecutalo con:  python "); cm_puts(out, max, &p, target);
            } else if (cdr_is_html(target)) {                                  /* SOMA_275_HTML */
                cm_puts(out, max, &p, "\nPagina web: etiquetas, enlaces y scripts correctos.");
                cm_puts(out, max, &p, "\nAbrela con:  html abrir "); cm_puts(out, max, &p, target);
            } else if (cdr_is_js(target)) {
                cm_puts(out, max, &p, "\nJavaScript: sin errores de sintaxis y pasa las comprobaciones.");
                cm_puts(out, max, &p, "\nEjecutalo con:  js "); cm_puts(out, max, &p, target);
            } else if (cdr_is_c(target)) {                                     /* SOMA_265_CC */
                cm_puts(out, max, &p, "\nC: compila con TinyCC y pasa las comprobaciones.");
                cm_puts(out, max, &p, "\nEjecutalo con:  cc -run "); cm_puts(out, max, &p, target);
            } else {
            cm_puts(out, max, &p, "\nCompila y pasa las comprobaciones (");
            char n[12]; n[0] = 0; cdr_cat_u32(n, sizeof n, cr.bc_len); cm_puts(out, max, &p, n); cm_puts(out, max, &p, " bytes de bytecode).");
            cm_puts(out, max, &p, "\nEjecutalo con:  app "); cm_puts(out, max, &p, target);
            }
            cm_puts(out, max, &p, "\nO abrelo en SOMA Studio con:  studio "); cm_puts(out, max, &p, target);
        } else {
            cm_puts(out, max, &p, "No he conseguido un programa correcto. "); cm_puts(out, max, &p, cr.summary);
            if (cr.last_error[0]) { cm_puts(out, max, &p, "\nUltimo error: "); cm_puts(out, max, &p, cr.last_error); }
        }
        char m[160]; m[0] = 0;
        cdr_cat(m, sizeof m, "\n("); cdr_cat_u32(m, sizeof m, (uint32_t)cr.llm_calls); cdr_cat(m, sizeof m, " llamadas al modelo, ");
        cdr_cat_u32(m, sizeof m, (uint32_t)cr.repairs); cdr_cat(m, sizeof m, " reparaciones, ");
        cdr_cat_u32(m, sizeof m, (uint32_t)cr.auto_fixes); cdr_cat(m, sizeof m, " correcciones automaticas. Detalle: orden 'coderlog'.)");
        cm_puts(out, max, &p, m);
    }
    return cr.ok;
}



/* SOMA_217_STUDIO: varias ventanas de SOMA Studio. Cada ventana tiene su editor (codeedit, por puntero) y su estado de interfaz:
 * pestana, campo de peticion a SOMA, mensajes, problemas, explorador y seguimiento del trabajo del agente. Ese estado (unos
 * pocos KiB) se guarda y se carga al cambiar de ventana. Los proyectos, la pestana Crear y el trabajo del agente son comunes:
 * cuando el agente termina, recarga el archivo la ventana que lo tiene abierto. */
#define CS_FIELDS X(ide_inited) X(cdr_tab_cur) X(ide_focus) X(ide_ia) X(ide_ia_len) X(ide_msg) X(ide_msg_col) X(ide_prob) \
    X(ide_prob_line) X(ide_prob_n) X(ide_prob_ok) X(ide_was_working) X(ide_job_mine) X(ide_job_target) X(ide_ex_scroll) \
    X(ide_ex_armed) X(ide_ex_hover) X(tf_ia)
struct cs_inst {
#define X(f) __typeof__(f) f;
    CS_FIELDS
#undef X
};
#define CS_INST_MAX 6
static struct cs_inst cs_insts[CS_INST_MAX + 1];
static int cs_used[CS_INST_MAX + 1], cs_live;
static void cs_save(struct cs_inst *q)
{
#define X(f) memcpy(&q->f, &f, sizeof f);
    CS_FIELDS
#undef X
}
static void cs_load(const struct cs_inst *q)
{
#define X(f) memcpy(&f, &q->f, sizeof f);
    CS_FIELDS
#undef X
}
static void cs_blank(void)
{
    ui_tf_init();
    ide_inited = 0; cdr_tab_cur = CDR_TAB_EDITOR; ide_focus = 0;
    ide_ia[0] = 0; ide_ia_len = 0; tf_set(&tf_ia, "");
    ide_msg[0] = 0; ide_msg_col = CDR_C_GRAY; ide_prob_n = 0; ide_prob_ok = -1;
    ide_was_working = 0; ide_job_mine = 0; ide_job_target[0] = 0;
    ide_ex_scroll = 0; ide_ex_armed = -1; ide_ex_hover = -1;
}
void coder_app_select(int slot)
{
    if (slot < 1 || slot > CS_INST_MAX || !cs_used[slot]) return;
    ce_select(slot);
    if (slot == cs_live) return;
    if (cs_live) cs_save(&cs_insts[cs_live]);
    cs_load(&cs_insts[slot]);
    cs_live = slot;
}
int coder_app_inst_new(void)
{
    int k = ce_inst_new();                       /* deja elegido el editor nuevo */
    if (!k) return 0;
    if (k > CS_INST_MAX) { ce_inst_drop(k); return 0; }
    if (cs_live) cs_save(&cs_insts[cs_live]);
    cs_blank();
    cs_used[k] = 1; cs_live = k;
    cs_save(&cs_insts[k]);
    return k;
}
void coder_app_inst_drop(int slot)
{
    if (slot < 1 || slot > CS_INST_MAX || !cs_used[slot]) return;
    ce_inst_drop(slot);
    cs_used[slot] = 0;
    if (cs_live == slot) cs_live = 0;
}
int coder_app_inst_find(const char *path)
{
    for (int k = 1; k <= CS_INST_MAX; ++k) {
        if (!cs_used[k] || !path) continue;
        const char *a = ce_inst_path(k), *b = path;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return k;
    }
    return 0;
}
const char *coder_app_inst_name(int slot)
{
    if (slot < 1 || slot > CS_INST_MAX || !cs_used[slot]) return "";
    const char *p = ce_inst_path(slot), *n = p;
    for (; *p; ++p) if (*p == '/') n = p + 1;
    return n;
}
void coder_app_open_path(const char *path)
{
    if (!path || !*path) return;
    cdr_ensure_init(); ui_tf_init();
    cdr_load_mod_file(path);
    ide_open(path); ide_inited = 1; cdr_tab_cur = CDR_TAB_EDITOR;
}
