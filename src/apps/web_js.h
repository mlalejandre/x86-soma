#ifndef SOMA_WEB_JS_H
#define SOMA_WEB_JS_H
/* Motor JavaScript de SOMA (subconjunto ES2015+ sin FPU): interprete de arbol, DOM, temporizadores, promesas. */
#include <stdint.h>
#include "web_dom.h"

struct wjs;

/* Servicios que aporta el navegador al motor */
struct wjs_host {
    void *ud;
    void (*log)(void *ud, int level, const char *msg);                       /* console.* / errores (0 info,1 aviso,2 error) */
    void (*dirty)(void *ud, int what);                                         /* 1 = DOM/estilo cambiado, 2 = repintar */
    void (*navigate)(void *ud, const char *url, int replace);                  /* mode: 0 navegar, 1 reemplazar, 2 pushState (solo actualizar URL), 3 replaceState, 4 recargar */
    /* sincrono; 0 = OK. *out se reserva con kmalloc (lo libera el motor con kfree) */
    int  (*fetch)(void *ud, const char *method, const char *url, const char *body, uint32_t blen, const char *req_ctype,
                  int *status, char **out, uint32_t *outlen, char *ctype, uint32_t ctmax);
    void (*submit)(void *ud, struct wnode *form);                              /* form.submit() */
    void (*activate)(void *ud, struct wnode *n);                               /* el.click(): accion por defecto (enlaces, casillas, envio) */
    void (*focus)(void *ud, struct wnode *n);                                  /* el.focus()/blur() (NULL = quitar foco) */
    void (*load_script)(void *ud, struct wnode *script);                       /* <script src> insertado dinamicamente */
    void (*history)(void *ud, int delta);
    void (*alert)(void *ud, const char *msg);
    const char *(*get_cookie)(void *ud, char *buf, uint32_t max);
    void (*set_cookie)(void *ud, const char *c);
    const char *(*storage)(void *ud, int local, const char *key, const char *val, int op); /* 0 get,1 set,2 remove,3 clear,4 key(i) */
    void (*layout_rect)(void *ud, struct wnode *n, int *x, int *y, int *w, int *h);          /* pagina, px */
    void (*viewport)(void *ud, int *w, int *h, int *sx, int *sy);
    void (*scroll_to)(void *ud, int x, int y);
    int  (*computed)(void *ud, struct wnode *n, const char *prop, char *out, uint32_t max); /* estilo calculado */
    const char *url;      /* URL del documento */
    const char *ua;
};

struct wjs *wjs_new(struct wdoc *doc, const struct wjs_host *host);
void wjs_free(struct wjs *js);
/* Ejecuta codigo de nivel superior. 0 = OK, -1 = excepcion/error (mensaje enviado a host->log). */
int  wjs_eval(struct wjs *js, const char *src, uint32_t len, const char *name);
/* Evalua una expresion y devuelve su texto (para la consola). */
int  wjs_eval_str(struct wjs *js, const char *src, char *out, uint32_t max);
/* Temporizadores / promesas: ejecuta lo vencido; devuelve ms hasta el siguiente (-1 = ninguno). */
int  wjs_pump(struct wjs *js, uint64_t now_ms, int *ran);
int  wjs_has_timers(struct wjs *js);
/* Eventos del DOM disparados desde el navegador. Devuelve 1 si preventDefault. */
int  wjs_dispatch(struct wjs *js, struct wnode *target, const char *type, int x, int y, int key, const char *keystr, int mods);
void wjs_fire_load(struct wjs *js);                    /* DOMContentLoaded + load */
int  wjs_heap_used(struct wjs *js);
int  wjs_halted(struct wjs *js);
void wjs_gc_stats(struct wjs *js, uint32_t *collections, uint32_t *live_bytes);   /* SOMA 189: recolecciones hechas y bytes vivos */
void wjs_set_url(struct wjs *js, const char *url);                 /* location tras un cambio de URL sin recarga (hash, pushState) */
void wjs_set_script(struct wjs *js, struct wnode *n);   /* document.currentScript durante la ejecucion de un <script src> */
#endif
