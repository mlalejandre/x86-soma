#ifndef SOMA_GUI_H
#define SOMA_GUI_H

#include <stdint.h>

int  cmd_vsoma(const char *args, char *out_buf, uint32_t max_out);
void gui_open_browser(const char *url);
void gui_open_file_manager(const char *folder);
void gui_open_memview(void);
void gui_open_layout(void);
void gui_open_settings(void);
void gui_open_web(void);
void gui_open_pdf(const char *path);        /* PARCHE 088 */
void gui_open_sysmon(void);                  /* PARCHE 089 */
void gui_open_recorder(void);                /* SOMA_177_T480S */
void gui_open_capture(void);                /* SOMA_179_T480S */
void gui_open_camera(void);                  /* SOMA_178_T480S */
const char *gui_app_title(int app);
int  gui_app_list(int *apps, int max);
int  gui_close_app(int app);
extern const int gui_app_sysmon;
extern const int gui_app_agenda;                /* SOMA_272_AGENDA */
void gui_open_agenda(void);                     /* SOMA_272_AGENDA */
extern const int gui_app_correo;                /* SOMA_295_CORREO */
void gui_open_correo(void);                     /* SOMA_295_CORREO */
/* PARCHE 091: el agente corre en su hilo; para tocar la pantalla pide turno (0 = no hacia falta) */
int  gui_enter(void);
void gui_leave(int tok);
int  gui_turn_begin(void);                  /* SOMA_248_DOC: turno de pantalla desde cualquier hilo (0/1/2) */
void gui_turn_end(int tok);
int  gui_agent_busy(void);                   /* PARCHE 085 */
int  gui_agent_submit(const char *mission);  /* SOMA_248_DOC: mision al agente (como escrita en el orbe); 0 enviada, -1 ocupado */
void gui_open_layout_file(const char *path);   /* PARCHE 084: abre un .tex en el maquetador */
void gui_open_agents(void);
void gui_open_notepad(const char *path);
void gui_open_paint(const char *path);
void gui_open_imgview(const char *path);   /* SOMA_PARCHE_ARCHIVOS */
void gui_open_terminal(int debug);         /* SOMA_PARCHE_024_TERM: abre la ventana del terminal (debug = traza por serie de la geometria y del portapapeles) */
void gui_open_media(const char *path);     /* SOMA_PARCHE_023_MEDIA: abre la ventana del reproductor (y reproduce path si se da) */
void gui_open_youtube(const char *query);  /* SOMA_250_YOUTUBE: abre la ventana de YouTube (y busca query si se da) */
void gui_open_coder_app(void);

/* HER-AURA v4: razonamiento en vivo y streaming de la respuesta */
void gui_thought_update(const char *txt);
void gui_stream_begin(void);
void gui_stream_push(const char *chunk);

/*PL12_SOMA_WIN_H*/
/* API de Ventana Estandar SOMA-LANG */
int         gui_soma_win_open(const char *title, int w, int h);
int         gui_soma_win_is_active(void);
void        gui_soma_win_close(void);
void        gui_soma_win_on_close(void);
void        gui_soma_win_clear(uint32_t color);
void        gui_soma_win_rect(int x, int y, int w, int h, uint32_t color);
void        gui_soma_win_pixel(int x, int y, uint32_t color);
void        gui_soma_win_line(int x0, int y0, int x1, int y1, uint32_t color);
void        gui_soma_win_circle(int cx, int cy, int r, uint32_t color);
void        gui_soma_win_text(int x, int y, const char *str, uint32_t color);
void        gui_soma_win_present(void);
void        gui_soma_win_get_mouse(int *out_rx, int *out_ry, int *out_btn, int *out_inside, int *out_clicked);
int         gui_soma_win_get_key(void);
void        gui_soma_win_push_key(int key);
void        gui_soma_win_record_click(void);
void        gui_soma_win_get_size(int *out_w, int *out_h);
uint32_t    gui_soma_win_snapshot(uint32_t **px, int *w, int *h);   /* SOMA_248_DOC: captura de la ventana del programa (kmalloc) */
uint32_t    gui_soma_win_presents(void);                             /* SOMA_248_DOC: fotogramas presentados */
const char *gui_soma_win_get_title(void);
void        gui_terminal_run(const char *cmd);   /* SOMA_239: ejecuta una orden en el Terminal del escritorio (lo abre si hace falta) */
void        gui_soma_win_render_body(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh);

#endif
