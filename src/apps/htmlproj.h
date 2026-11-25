/* SOMA_275_HTML: proyectos web de SOMA: comprobar (el "compilador" de HTML), empaquetar en un solo archivo y plantillas.
 * Codigo puro (sin nucleo ni libc): lo usan la orden `html` y el Programador, y se prueba en el anfitrion. */
#ifndef SOMA_HTMLPROJ_H
#define SOMA_HTMLPROJ_H
#include <stdint.h>

struct html_env {
    void *ud;
    /* 1 si el archivo local existe (ruta ya resuelta respecto al documento); NULL = no se comprueba */
    int (*exists)(void *ud, const char *path);
    /* lee un archivo local (lo libera quien llama con html_env.release); 0 = bien */
    int (*read)(void *ud, const char *path, const char **data, uint32_t *len);
    void (*release)(void *ud, const char *data);
    /* analiza un <script> en linea: 0 = bien; si no, msg = "texto del error" y *line = linea DENTRO del script (1 = la primera); NULL = no se comprueba */
    int (*js_check)(void *ud, const char *code, uint32_t len, int *line, char *msg, uint32_t cap);
};
struct html_report { int errores, avisos, scripts, enlaces; };

/* Comprueba src (nombre = como se muestra en los mensajes, dir = carpeta del documento para resolver rutas relativas).
 * Escribe en out lineas "nombre:LINEA: error: texto" / "nombre:LINEA: aviso: texto". Devuelve los errores. */
int html_check(const char *src, uint32_t len, const char *name, const char *dir, const struct html_env *env, char *out, uint32_t cap,
               struct html_report *rep);

/* Empaqueta: CSS (<link rel=stylesheet>), JS (<script src>) e imagenes locales (<img src>, hasta 300 KB) dentro del propio HTML.
 * Devuelve la longitud escrita en out (NUL al final) o -1 si no cabe. En *missing, cuantos recursos locales no se encontraron
 * (se dejan tal cual). */
int html_bundle(const char *src, uint32_t len, const char *dir, const struct html_env *env, char *out, uint32_t cap, int *missing);

/* Plantillas de proyecto: kind "basica" | "app" | "juego" (0 = la lista por defecto). file 0 = index.html, 1 = estilo.css, 2 = app.js.
 * Devuelve el texto (con %TITULO% ya sustituido en out) o -1 si no cabe / no existe. */
int html_template(const char *kind, int file, const char *titulo, char *out, uint32_t cap);
const char *html_template_name(int file);     /* "index.html" ... */

int html_path_join(const char *dir, const char *rel, char *out, uint32_t cap);   /* resuelve ./ y ../; 0 = bien */
#endif
