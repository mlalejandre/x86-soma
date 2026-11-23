#ifndef SOMA_CODER_CORE_H
#define SOMA_CODER_CORE_H
/* PARCHE 096: nucleo agentico del programador de SOMA (independiente de la interfaz; se prueba en el host con un LLM simulado).
 *
 * Bucle generar -> normalizar -> verificar -> reparar, guiado por el compilador:
 *  - Contexto anclado: ficha de referencia de SOMA-LANG sacada del compilador real + ejemplo verificado que se elige segun la tarea
 *    (ventana o consola) + lecciones aprendidas de trabajos anteriores + codigo actual completo si es una modificacion.
 *  - Salida en bloque ```soma``` (sin JSON ni comillas escapadas) y extraccion robusta (<think>, prosa, bloques sin cerrar).
 *  - Normalizador determinista: corrige sin gastar llamadas los fallos tipicos de un LLM (Python/JavaScript: elif, True, x++, var,
 *    function, console.log, .length, #, comillas tipograficas, el "manejador" de ventana que SOMA-LANG no usa...).
 *  - Verificacion: compilador + lint + comprobaciones semanticas de la tarea (pide ventana y no la crea, falta win.present,
 *    modificacion que no cambia nada, programa cortado).
 *  - Reparacion con el error exacto y las lineas de alrededor numeradas, pista especifica por tipo de error y el archivo completo.
 *  - Mejor candidato conservado; si dos rondas no mejoran se regenera desde cero con un enfoque mas simple; presupuesto de llamadas.
 *  - Revision critica final (una ronda) solo si queda presupuesto; el resultado revisado se acepta solo si no empeora.
 *  - El archivo se escribe UNA vez y solo si el resultado compila: un fallo nunca deja el disco a medias.
 *  - Lecciones: cada error reparado se guarda en /agent/coder_lessons.txt y se ofrece en los siguientes trabajos. */
#include <stdint.h>

#define CC_SRC_MAX    65536   /* SOMA_264_LANG: programas de hasta 64 KB (antes 32 KB) */
#define CC_LESSONS    "/agent/coder_lessons.txt"

struct cc_test {                      /* PARCHE 116 */
    const char *name;
    const char *input;                /* lineas para input(), separadas por \n (puede ser "") */
    const char *expect;               /* salida exacta esperada (se ignoran espacios y saltos finales) */
};

struct cc_env {
    int      (*llm)(const char *prompt, char *out, uint32_t max, uint32_t timeout_ms);   /* 1 = respuesta */
    int      (*read)(const char *path, char *buf, uint32_t max);                         /* bytes o <0 */
    int      (*write)(const char *path, const char *buf, uint32_t len);                  /* >=0 ok */
    void     (*log)(const char *msg);
    void     (*status)(int phase, const char *desc, const char *thought);
    int      (*cancelled)(void);
    int      max_llm_calls;                                                              /* 0 = 7 */
    void     (*trace)(const char *kind, const char *prompt, const char *reply);              /* PARCHE 097: opcional */
    int      force_new;      /* PARCHE 098: crear desde cero aunque el archivo exista (no es una modificacion) */
    int      (*run)(const uint8_t *bc, uint32_t len, const char *input, char *out, uint32_t max);   /* PARCHE 116: ejecuta bytecode capturando su salida; 0 = termino bien, <0 = error o no termina */
    const struct cc_test *tests; /* PARCHE 116: casos de prueba del encargo (con `run`) */
    int      ntests;
    /* SOMA_239_PYTHON_GRAFICO: lenguaje del programa. CC_LANG_PYTHON escribe Python (MicroPython de SOMA, modulo soma.win) y
     * necesita check_src (compilar sin ejecutar) y, para las pruebas, run_src. Ambos devuelven 0 = bien; en out, la salida o el error.
     * SOMA_265_CC: CC_LANG_C igual, con el compilador de C (cc -s / cc -x). */
    int      lang;
    int      (*check_src)(const char *src, uint32_t n, char *out, uint32_t max);
    int      (*run_src)(const char *src, uint32_t n, const char *input, char *out, uint32_t max);
};
#define CC_LANG_SOMA   0
#define CC_LANG_PYTHON 1
#define CC_LESSONS_PY  "/agent/coder_lessons_py.txt"
#define CC_LANG_C      2      /* SOMA_265_CC: C (TinyCC de SOMA; ventana con soma.h). Usa check_src y run_src como Python */
#define CC_LESSONS_C   "/agent/coder_lessons_c.txt"
#define CC_LANG_JS     3      /* SOMA_275_HTML: JavaScript (orden js; ventana con win.*). Usa check_src y run_src como Python */
#define CC_LESSONS_JS  "/agent/coder_lessons_js.txt"
#define CC_LANG_HTML   4      /* SOMA_275_HTML: pagina web de un solo archivo (orden html). check_src = comprobar; run_src = comprobar tambien */
#define CC_LESSONS_HTML "/agent/coder_lessons_html.txt"

struct cc_result {
    int      ok;               /* el archivo se escribio y compila */
    int      wrote;
    int      llm_calls;
    int      repairs;
    int      regenerations;
    int      auto_fixes;       /* correcciones deterministas aplicadas */
    int      reviewed;         /* hubo revision critica */
    int      cancelled;
    int      stuck;            /* el modelo repetia la misma respuesta */
    int      score;            /* 0 = perfecto */
    int      tests_total;      /* PARCHE 116: casos de prueba del encargo */
    int      tests_pass;       /* ... que pasa el programa entregado (o el mejor candidato si no se entrego) */
    int      tests_first_fail; /* ... que fallaban en la PRIMERA version generada: bugs detectados por las pruebas */
    uint32_t bc_len;
    char     summary[192];
    char     last_error[192];
    char     plan[480];
};

int      cc_run(const struct cc_env *env, const char *target, const char *mission, struct cc_result *res);
/* piezas sueltas (pruebas) */
uint32_t cc_extract_code(const char *reply, char *out, uint32_t cap);
int      cc_normalize(char *src, uint32_t *len, uint32_t cap);   /* devuelve el numero de correcciones */
int      cc_mission_is_gui(const char *mission);
int      cc_mission_wants_python(const char *mission);   /* SOMA_239: "en python", "con Python"... */
int      cc_py_error(const char *out, uint32_t *line, char *msg, uint32_t cap);   /* SOMA_239: linea y mensaje de un error de Python */
int      cc_py_normalize(char *src, uint32_t *len, uint32_t cap);
int      cc_mission_wants_c(const char *mission);        /* SOMA_265_CC: "en C", "lenguaje C", "programa.c"... */
int      cc_c_error(const char *out, uint32_t *line, char *msg, uint32_t cap);    /* SOMA_265_CC: "programa.c:7: error: ..." */
int      cc_c_normalize(char *src, uint32_t *len, uint32_t cap);                  /* SOMA_265_CC: + los #include que falten */
int      cc_mission_wants_js(const char *mission);       /* SOMA_275_HTML: "en javascript", "con js", "juego.js"... */
int      cc_mission_wants_html(const char *mission);     /* SOMA_275_HTML: "pagina web", "en html", "index.html"... */
int      cc_js_error(const char *out, uint32_t *line, char *msg, uint32_t cap);   /* SOMA_275_HTML: "[error] Uncaught X: ... (programa.js, linea 7)" */
int      cc_html_normalize(char *src, uint32_t *len, uint32_t cap);                /* SOMA_275_HTML: solo \r */
int      cc_busy(void);
#endif
