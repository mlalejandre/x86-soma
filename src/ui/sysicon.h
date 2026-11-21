#ifndef SOMA_SYSICON_H
#define SOMA_SYSICON_H
/* SOMA_210_SYSICON: iconos del sistema en graficos vectoriales y controles comunes (botones, tarjetas, etiquetas, medidor).
 * Los iconos se definen sobre una rejilla de 48x48 y se rasterizan con suavizado a cualquier tamano: no hay mapas de bits.
 * Los usan la Grabadora, la Camara y la Captura; cualquier otra aplicacion puede usarlos. */
#include <stdint.h>

enum si_icon {
    SI_NONE = 0, SI_REC, SI_STOP, SI_PLAY, SI_PAUSE, SI_MIC, SI_MIC_OFF, SI_CAMERA, SI_CAMERA_OFF, SI_VIDEO, SI_SAVE, SI_NEW,
    SI_TRASH, SI_LEVEL, SI_SCREEN, SI_SPEAKER, SI_SCREEN_AUDIO, SI_TIMER, SI_SCALE, SI_GAUGE, SI_FOLDER, SI_PREV, SI_NEXT,
    SI_AUTO, SI_POWER, SI_MINUS, SI_PLUS, SI_IMAGE, SI_WAVE, SI_CHECK, SI_SHUTTER, SI_RESTART,
    /* SOMA_240_ICONOS: reproductor (SOMA Media) y editor (SOMA Studio) */
    SI_MUSIC, SI_FILM, SI_REWIND, SI_FFWD, SI_SKIP_PREV, SI_SKIP_NEXT, SI_LIST, SI_CODE, SI_UNDO, SI_REDO, SI_SEARCH, SI_REPLACE,
    SI_GOTO, SI_COMMENT, SI_FILE, SI_FILE_CODE, SI_DISK, SI_EDIT, SI_GRID, SI_COPY, SI_CLOSE, SI_SPEAKER_OFF, SI_LOOP, SI_TERMINAL,
    SI_COUNT
};

/* Paleta comun (la del Monitor del sistema y el explorador) */
#define SI_WIN     0x000C111Bu
#define SI_SIDE    0x000F1520u
#define SI_CARD    0x00111827u
#define SI_CARD2   0x00151E2Eu
#define SI_LINE    0x00212B3Bu
#define SI_TXT     0x00E5EAF2u
#define SI_DIM     0x008592A8u
#define SI_FAINT   0x00566379u
#define SI_RED     0x00F43F5Eu
#define SI_GREEN   0x0034D399u
#define SI_AMBER   0x00FBBF24u

enum { SIB_NEUTRAL = 0, SIB_ACCENT, SIB_DANGER, SIB_SUCCESS, SIB_GHOST, SIB_ON };

uint32_t si_mix(uint32_t a, uint32_t b, uint32_t k256);          /* k = 0 -> a, 256 -> b */
void si_icon(int icon, int32_t cx, int32_t cy, int32_t size, uint32_t rgb);

int  si_text_w(const char *s, int px, int bold);
void si_text(int32_t x, int32_t top, int32_t h, const char *s, int px, uint32_t rgb, int bold);   /* centrado vertical en [top, top+h) */
void si_text_r(int32_t right, int32_t top, int32_t h, const char *s, int px, uint32_t rgb, int bold);
void si_text_c(int32_t x, int32_t top, int32_t w, int32_t h, const char *s, int px, uint32_t rgb, int bold);
void si_text_fit(int32_t x, int32_t top, int32_t h, const char *s, int maxw, int px, uint32_t rgb, int bold);   /* recorta con "..." */

void si_card(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t fill);
void si_button(int32_t x, int32_t y, int32_t w, int32_t h, int icon, const char *label, int kind, uint32_t accent, int disabled);
void si_round(int32_t cx, int32_t cy, int32_t r, int icon, int kind, uint32_t accent, int disabled);
void si_round_label(int32_t cx, int32_t cy, int32_t r, const char *label, uint32_t rgb);
int  si_chip(int32_t x, int32_t y, int32_t h, const char *text, uint32_t rgb, int dot);            /* devuelve el ancho */
int  si_chip_w(int32_t h, const char *text, int dot);
void si_badge(int32_t x, int32_t y, int32_t size, int icon, uint32_t accent);                      /* icono de la aplicacion */
void si_meter(int32_t x, int32_t y, int32_t w, int32_t h, int pct, int hold, int active);
void si_header(int32_t x, int32_t y, int32_t w, int icon, uint32_t accent, const char *title, const char *subtitle);

#endif
