/* ayuda.h: SOMA_294_AYUDA: la bienvenida del orbe y la ayuda rapida (consejos al iniciar y tarjeta de F1).
 * Solo la logica pura (que consejo toca, cuando se pasa al siguiente, que hace cada tecla o clic, donde van las tarjetas y
 * el ajuste guardado), sin dibujar nada: la usan src/ui/gui_ayuda.c y la prueba del anfitrion tests/test_ayuda_294.c.
 *
 * Nada de esto bloquea: SOMA se usa desde el segundo cero. La ayuda nunca se queda con el teclado ni con el raton; solo
 * toma Esc (y el clic) cuando hay una tarjeta a la vista, y se aparta sola si el usuario ya sabe lo que hace. */
#ifndef SOMA_AYUDA_H
#define SOMA_AYUDA_H
#include <stdint.h>

#define AY_ESPERA_MS     1500u   /* la primera imagen del escritorio, y despues la bienvenida */
#define AY_CONSEJO_MS    6000u   /* cada consejo */
#define AY_BIEN_MS       1300u   /* "Eso es" tras hacer lo que decia el consejo */
#define AY_TECLEO_MAX    40u     /* quien escribe tanto mientras salen los consejos ya sabe usar SOMA: se apartan */
#define AY_ANIM_MS       100u    /* la barra de tiempo del consejo se mueve a 10 imagenes por segundo */

enum { AY_NADA = 0, AY_ESPERA, AY_RECORRIDO, AY_TARJETA };
enum { AYEV_SUPER = 1, AYEV_VOZ, AYEV_ENTER, AYEV_F1, AYEV_TECLA, AYEV_ESC, AYEV_CLIC_X, AYEV_CLIC_CUERPO, AYEV_CLIC_INICIO };
#define AY_CONSUME 1             /* el evento era para la ayuda: nadie mas lo recibe */
#define AY_REPINTA 2             /* cambia lo que se ve: hay que repintar lo que hay debajo */
enum { AY_ZONA_NADA = 0, AY_ZONA_CUERPO, AY_ZONA_X, AY_ZONA_INICIO };

struct ay_consejo { const char *tecla, *texto; int accion; };   /* accion: el AYEV_ que lo cumple (0 = solo se lee) */
struct ay_fila { const char *tecla, *texto; };

struct ayuda {
    int modo, inicio, voz_ok;
    int i;                       /* consejo a la vista */
    uint32_t hechos;             /* consejos ya cumplidos (no se repiten) */
    uint64_t t_i, t_bien, t_anim, t0;
    uint32_t tecleo;
    int saludo_pend;             /* el escritorio debe dar la bienvenida (una vez) */
    int actuo;                   /* el usuario hizo algo antes de la bienvenida: no se le habla encima */
    int guardar;                 /* cambio el ajuste "mostrar al iniciar" */
};

static const struct ay_consejo ay_tabla_voz[] = {
    { "Tecla Windows", "Pulsa la tecla Windows (o toca Alt) y ver\303\241s tus aplicaciones y el reloj.", AYEV_SUPER },
    { "Mant\303\251n Alt", "Mant\303\251n Alt pulsada y habla; al soltarla, SOMA escribe lo que dijiste.", AYEV_VOZ },
    { "Escribe", "Escribe lo que necesites y pulsa Intro: abrir apps, buscar, programar...", AYEV_ENTER },
    { "Bot\303\263n X", "La X de una ventana la minimiza: queda orbitando el agujero negro.", 0 },
    { "Clic derecho", "Clic derecho en una app en \303\263rbita la cierra (o arr\303\241strala al agujero negro).", 0 },
    { "F1", "Pulsa F1 cuando quieras para ver la ayuda completa.", AYEV_F1 },
};
static const struct ay_consejo ay_tabla_sin_voz[] = {
    { "Tecla Windows", "Pulsa la tecla Windows (o toca Alt) y ver\303\241s tus aplicaciones y el reloj.", AYEV_SUPER },
    { "Mant\303\251n Alt", "Con micr\303\263fono, mant\303\251n Alt pulsada para hablar con SOMA.", 0 },
    { "Escribe", "Escribe lo que necesites y pulsa Intro: abrir apps, buscar, programar...", AYEV_ENTER },
    { "Bot\303\263n X", "La X de una ventana la minimiza: queda orbitando el agujero negro.", 0 },
    { "Clic derecho", "Clic derecho en una app en \303\263rbita la cierra (o arr\303\241strala al agujero negro).", 0 },
    { "F1", "Pulsa F1 cuando quieras para ver la ayuda completa.", AYEV_F1 },
};
#define AY_NCONSEJOS ((int)(sizeof ay_tabla_voz / sizeof ay_tabla_voz[0]))

static const struct ay_fila ay_filas[] = {
    { "Windows / toque de Alt", "Aplicaciones, reloj y estado; con ventanas, el orbe al frente." },
    { "Mant\303\251n Alt", "Habla con SOMA; al soltar, escribe lo que dijiste." },
    { "Escribe + Intro", "Pide lo que quieras: abrir apps, buscar, programar, dibujar..." },
    { "Esc", "Detiene lo que SOMA est\303\251 leyendo o haciendo." },
    { "Bot\303\263n X", "Minimiza la ventana; un clic en su icono en \303\263rbita la recupera." },
    { "Clic derecho", "Cierra una app en \303\263rbita (o arr\303\241strala al agujero negro)." },
    { "ImprPant", "Captura la pantalla; con Ctrl, graba un v\303\255deo." },
    { "F1", "Muestra u oculta esta ayuda." },
};
#define AY_NFILAS ((int)(sizeof ay_filas / sizeof ay_filas[0]))
static const char ay_fila_voz_no[] = "Hablar con SOMA (necesita micr\303\263fono).";

static const char ay_saludo_voz[] = "\302\241Hola! Soy SOMA. Ya puedes usarme: pulsa la tecla Windows para ver tus aplicaciones y el reloj, "
                                   "mant\303\251n Alt pulsada para hablarme, o escr\303\255beme y pulsa Intro. Si te pierdes, pulsa F1.";
static const char ay_saludo_sin_voz[] = "\302\241Hola! Soy SOMA. Ya puedes usarme: pulsa la tecla Windows para ver tus aplicaciones y el reloj, "
                                        "o escr\303\255beme lo que necesites y pulsa Intro. Si te pierdes, pulsa F1.";

static inline const struct ay_consejo *ay_consejo(const struct ayuda *a, int i)
{
    if (i < 0 || i >= AY_NCONSEJOS) return 0;
    return a->voz_ok ? &ay_tabla_voz[i] : &ay_tabla_sin_voz[i];
}
static inline const char *ay_fila_texto(const struct ayuda *a, int i)
{
    if (i < 0 || i >= AY_NFILAS) return "";
    return (i == 1 && !a->voz_ok) ? ay_fila_voz_no : ay_filas[i].texto;
}
static inline const char *ay_saludo(const struct ayuda *a) { return a->voz_ok ? ay_saludo_voz : ay_saludo_sin_voz; }

/* el siguiente consejo sin cumplir a partir de i (incluido); AY_NCONSEJOS si no queda ninguno */
static inline int ay_siguiente(const struct ayuda *a, int i)
{
    while (i < AY_NCONSEJOS && (a->hechos & (1u << i))) i++;
    return i;
}
static inline void ay_avanza(struct ayuda *a, uint64_t now)
{
    int n = ay_siguiente(a, a->i + 1);
    a->t_bien = 0;
    if (n >= AY_NCONSEJOS) { a->modo = AY_NADA; return; }
    a->i = n; a->t_i = now; a->t_anim = now;
}

/* al arrancar el escritorio (una vez por encendido) */
static inline void ay_arranque(struct ayuda *a, uint64_t now, int inicio, int voz_ok)
{
    struct ayuda z = { 0 };
    *a = z;
    a->inicio = inicio ? 1 : 0; a->voz_ok = voz_ok ? 1 : 0;
    a->t0 = now;
    a->modo = a->inicio ? AY_ESPERA : AY_NADA;
}

/* cada vuelta del bucle del escritorio: 0 nada, 1 basta volcar (la barra de tiempo), 2 repintar todo */
static inline int ay_tick(struct ayuda *a, uint64_t now)
{
    if (a->modo == AY_ESPERA) {
        if (now - a->t0 < AY_ESPERA_MS) return 0;
        a->i = ay_siguiente(a, 0);
        if (a->i >= AY_NCONSEJOS) { a->modo = AY_NADA; return 0; }
        a->modo = AY_RECORRIDO; a->t_i = now; a->t_anim = now; a->t_bien = 0;
        a->saludo_pend = !a->actuo;
        return 2;
    }
    if (a->modo != AY_RECORRIDO) return 0;
    if (a->t_bien) {                                     /* "Eso es": la barra ya esta llena, no hay nada que mover */
        if (now >= a->t_bien) { ay_avanza(a, now); return 2; }
        return 0;
    }
    if (now - a->t_i >= AY_CONSEJO_MS) { ay_avanza(a, now); return 2; }
    if (now - a->t_anim >= AY_ANIM_MS) { a->t_anim = now; return 1; }
    return 0;
}

/* 0..256: cuanto lleva el consejo a la vista */
static inline uint32_t ay_progreso(const struct ayuda *a, uint64_t now)
{
    if (a->modo != AY_RECORRIDO) return 0;
    if (a->t_bien) return 256u;
    uint64_t d = now - a->t_i;
    return d >= AY_CONSEJO_MS ? 256u : (uint32_t)(d * 256u / AY_CONSEJO_MS);
}

/* lo que hace el usuario. Devuelve AY_CONSUME | AY_REPINTA. */
static inline int ay_evento(struct ayuda *a, int ev, uint64_t now)
{
    switch (ev) {
    case AYEV_F1:
        if (a->modo == AY_TARJETA) { a->modo = AY_NADA; return AY_CONSUME | AY_REPINTA; }
        a->saludo_pend = 0;
        a->modo = AY_TARJETA;
        return AY_CONSUME | AY_REPINTA;
    case AYEV_ESC:
        if (a->modo == AY_ESPERA) { a->actuo = 1; return 0; }
        if (a->modo == AY_RECORRIDO || a->modo == AY_TARJETA) { a->modo = AY_NADA; a->saludo_pend = 0; return AY_CONSUME | AY_REPINTA; }
        return 0;
    case AYEV_CLIC_X:
        if (a->modo == AY_RECORRIDO || a->modo == AY_TARJETA) { a->modo = AY_NADA; a->saludo_pend = 0; return AY_CONSUME | AY_REPINTA; }
        return 0;
    case AYEV_CLIC_INICIO:
        if (a->modo != AY_TARJETA) return 0;
        a->inicio = !a->inicio; a->guardar = 1;
        return AY_CONSUME | AY_REPINTA;
    case AYEV_CLIC_CUERPO:
        if (a->modo == AY_RECORRIDO) { ay_avanza(a, now); return AY_CONSUME | AY_REPINTA; }
        if (a->modo == AY_TARJETA) return AY_CONSUME;
        return 0;
    case AYEV_TECLA:
        if (a->modo == AY_ESPERA) { a->actuo = 1; return 0; }
        if (a->modo != AY_RECORRIDO) return 0;
        if (++a->tecleo >= AY_TECLEO_MAX) { a->modo = AY_NADA; a->saludo_pend = 0; return AY_REPINTA; }
        return 0;
    case AYEV_SUPER: case AYEV_VOZ: case AYEV_ENTER: {
        int r = 0;
        for (int k = 0; k < AY_NCONSEJOS; k++) {
            const struct ay_consejo *c = ay_consejo(a, k);
            if (c && c->accion == ev) a->hechos |= 1u << k;
        }
        if (a->modo == AY_ESPERA) { a->actuo = 1; return 0; }
        if (a->modo == AY_RECORRIDO && !a->t_bien) {
            const struct ay_consejo *c = ay_consejo(a, a->i);
            if (c && c->accion == ev) { a->t_bien = now + AY_BIEN_MS; r = AY_REPINTA; }
        }
        return r;
    }
    default: return 0;
    }
}

/* ---- donde van las tarjetas (en pixeles de la pantalla logica) ---- */
struct ay_rect { int32_t x, y, w, h; };
static inline int ay_dentro(struct ay_rect r, int32_t px, int32_t py) { return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h; }
#define AY_REC_H      100
#define AY_FILA_H     30
#define AY_TAR_CAB    58
#define AY_TAR_PIE    50
/* consejos: abajo en el centro, encima de la barra del sistema (lejos del orbe y de la orbita de las aplicaciones) */
static inline struct ay_rect ay_rect_recorrido(uint32_t sw, uint32_t sh, uint32_t barra)
{
    struct ay_rect r;
    r.w = (int32_t)sw - 32; if (r.w > 640) r.w = 640; if (r.w < 0) r.w = 0;
    r.h = AY_REC_H;
    r.x = ((int32_t)sw - r.w) / 2;
    r.y = (int32_t)sh - (int32_t)barra - 18 - r.h; if (r.y < 8) r.y = 8;
    return r;
}
/* la ayuda completa (F1): en el centro */
static inline struct ay_rect ay_rect_tarjeta(uint32_t sw, uint32_t sh, uint32_t barra)
{
    struct ay_rect r;
    r.w = (int32_t)sw - 32; if (r.w > 780) r.w = 780; if (r.w < 0) r.w = 0;
    r.h = AY_TAR_CAB + AY_NFILAS * AY_FILA_H + AY_TAR_PIE;
    r.x = ((int32_t)sw - r.w) / 2;
    int32_t alto = (int32_t)sh - (int32_t)barra;
    r.y = (alto - r.h) / 2; if (r.y < 8) r.y = 8;
    return r;
}
static inline struct ay_rect ay_rect_x(struct ay_rect c) { struct ay_rect r = { c.x + c.w - 38, c.y + 10, 28, 28 }; return r; }
static inline struct ay_rect ay_rect_inicio(struct ay_rect c) { struct ay_rect r = { c.x + 18, c.y + c.h - AY_TAR_PIE + 10, 320, 30 }; return r; }
/* que hay bajo el puntero */
static inline int ay_zona(const struct ayuda *a, uint32_t sw, uint32_t sh, uint32_t barra, int32_t mx, int32_t my)
{
    struct ay_rect c;
    if (a->modo == AY_RECORRIDO) c = ay_rect_recorrido(sw, sh, barra);
    else if (a->modo == AY_TARJETA) c = ay_rect_tarjeta(sw, sh, barra);
    else return AY_ZONA_NADA;
    if (!ay_dentro(c, mx, my)) return AY_ZONA_NADA;
    if (ay_dentro(ay_rect_x(c), mx, my)) return AY_ZONA_X;
    if (a->modo == AY_TARJETA && ay_dentro(ay_rect_inicio(c), mx, my)) return AY_ZONA_INICIO;
    return AY_ZONA_CUERPO;
}
static inline int ay_zona_evento(int zona)
{
    return zona == AY_ZONA_X ? AYEV_CLIC_X : zona == AY_ZONA_INICIO ? AYEV_CLIC_INICIO : zona == AY_ZONA_CUERPO ? AYEV_CLIC_CUERPO : 0;
}

/* ---- el ajuste guardado (/etc/ayuda.conf: "inicio=1") ---- */
static inline int ay_conf_inicio(const char *t, int def)
{
    static const char k[] = "inicio=";
    for (int i = 0; t && t[i]; i++) {
        if (i && t[i - 1] != '\n') continue;
        int j = 0; while (k[j] && t[i + j] == k[j]) j++;
        if (!k[j]) return t[i + j] == '0' ? 0 : t[i + j] == '1' ? 1 : def;
    }
    return def;
}
static inline int ay_conf_texto(int inicio, char *o, int max)
{
    static const char k[] = "inicio=";
    int n = 0;
    if (max < 10) { if (max > 0) o[0] = 0; return 0; }
    for (int j = 0; k[j]; j++) o[n++] = k[j];
    o[n++] = inicio ? '1' : '0'; o[n++] = '\n'; o[n] = 0;
    return n;
}

/* ---- sin la fuente con acentos (la de mapa de bits es ASCII): se escribe sin ellos ---- */
static inline int ay_ascii(const char *s, char *o, int max)
{
    int n = 0;
    if (max <= 0) return 0;
    for (int i = 0; s && s[i] && n + 1 < max;) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { o[n++] = (char)c; i++; continue; }
        unsigned char d = (unsigned char)s[i + 1];
        char r = 0; int len = 1;
        if (c == 0xC3 && d) {
            len = 2;
            switch (d) {
            case 0xA1: case 0xA0: r = 'a'; break; case 0xA9: case 0xA8: r = 'e'; break; case 0xAD: r = 'i'; break;
            case 0xB3: r = 'o'; break; case 0xBA: case 0xBC: r = 'u'; break; case 0xB1: r = 'n'; break;
            case 0x81: r = 'A'; break; case 0x89: r = 'E'; break; case 0x8D: r = 'I'; break; case 0x93: r = 'O'; break;
            case 0x9A: case 0x9C: r = 'U'; break; case 0x91: r = 'N'; break;
            default: r = '?'; break;
            }
        } else if (c == 0xC2 && d) {
            len = 2;
            r = (d == 0xB7) ? '-' : 0;            /* el punto medio; los signos de apertura (de exclamacion e interrogacion) se quitan */
        } else {
            len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            r = '-';
        }
        for (int k = 0; k < len && s[i]; k++) i++;
        if (r) o[n++] = r;
    }
    o[n] = 0;
    return n;
}
#endif
