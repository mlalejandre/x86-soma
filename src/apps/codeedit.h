#ifndef SOMA_CODEEDIT_H
#define SOMA_CODEEDIT_H
/* PARCHE 093: editor de codigo profesional (lo usa SOMA Studio / Coder).
 * Cursor y seleccion (raton, arrastre, doble clic = palabra, triple = linea, Mayus+flechas), deshacer/rehacer ilimitado
 * por sesion, buscar / reemplazar, ir a linea, autoindentado, Tab / Mayus+Tab sobre bloques, comentar lineas,
 * duplicar linea, resaltado de sintaxis SOMA-LANG, parejas de llaves, linea de error marcada, desplazamiento con rueda,
 * barra de desplazamiento arrastrable y portapapeles del sistema. */
#include <stdint.h>

/* acciones que el editor devuelve a la aplicacion que lo aloja */
#define CE_ACT_NONE    0
#define CE_ACT_REDRAW  1
#define CE_ACT_SAVE    2      /* Ctrl+S */
#define CE_ACT_RUN     3      /* Ctrl+E */
#define CE_ACT_VERIFY  4      /* Ctrl+B */
#define CE_ACT_OPEN    5      /* barra Abrir confirmada: ruta en ce_prompt_value() */
#define CE_ACT_SAVEAS  6      /* barra Guardar como confirmada */
#define CE_ACT_NEW     7      /* Ctrl+N */

#define CE_PR_NONE     0
#define CE_PR_FIND     1
#define CE_PR_REPLACE  2
#define CE_PR_GOTO     3
#define CE_PR_OPEN     4
#define CE_PR_SAVEAS   5

int  ce_load(const char *path);                 /* 1 ok, 0 no existe (queda vacio con esa ruta), -1 demasiado grande */
void ce_new(const char *path);
int  ce_save(void);                             /* 1 ok */
int  ce_save_as(const char *path);
int  ce_reload_keep_undo(void);                 /* recarga del disco como una edicion deshacible (cambios del agente) */
const char *ce_path(void);
int  ce_dirty(void);
const char *ce_text(uint32_t *len);
void ce_set_readonly(int ro, const char *banner);
void ce_set_error(int line, const char *msg);   /* 0 = sin error */
void ce_goto_line(int line);
void ce_status(int *line, int *col, int *lines, int *sel_chars);

void ce_render(int x, int y, int w, int h, int focused);
int  ce_press(int mx, int my);                  /* clic izquierdo dentro del editor; 1 = consumido */
int  ce_mouse(int mx, int my, int held, int wheel);   /* cada evento de raton: arrastre, rueda. 1 = redibujar */
int  ce_key(char c, int ctrl, int shift);       /* CE_ACT_* */
int  ce_escape(void);                           /* 1 si Esc cerro algo del editor */
int  ce_clip_op(int op);
int  ce_undo(void);
int  ce_redo(void);
void ce_prompt(int kind);                       /* abre barra: buscar, reemplazar, ir a, abrir, guardar como */
int  ce_prompt_kind(void);
const char *ce_prompt_value(void);
void ce_find_next(int dir);
void ce_toggle_comment(void);
/* SOMA_216_STUDIO: un editor por ventana */
int  ce_inst_new(void);
void ce_select(int slot);
void ce_inst_drop(int slot);
const char *ce_inst_path(int slot);
int  ce_inst_dirty(int slot);                    /* SOMA_219_AVISO: 1 = cambios sin guardar */
#endif
