/* SOMA_294_AYUDA: pruebas en el anfitrion de la bienvenida y la ayuda rapida (src/ui/ayuda.h): que consejo toca y cuando,
 * que hace cada tecla o clic (sin quitarle nada al usuario), donde van las tarjetas, el ajuste guardado y el texto sin acentos. */
#include <stdio.h>
#include <string.h>
#include "ayuda.h"

static int ok_n, total;
static void chk(int c, const char *que) { total++; if (c) ok_n++; else printf("FALLO: %s\n", que); }

/* un encendido completo sin tocar nada: cuantos consejos salen y cuanto dura */
static int recorrido_entero(int voz_ok, uint64_t *fin, int *repintes)
{
    struct ayuda a; ay_arranque(&a, 1000, 1, voz_ok);
    int vistos = 0, ultimo = -1; *repintes = 0;
    for (uint64_t t = 1000; t < 1000 + 120000; t += 10) {
        int r = ay_tick(&a, t);
        if (r == 2) (*repintes)++;
        if (a.modo == AY_RECORRIDO && a.i != ultimo) { ultimo = a.i; vistos++; }
        if (a.modo == AY_NADA && ultimo >= 0) { *fin = t - 1000; return vistos; }
    }
    *fin = 0; return vistos;
}

int main(void)
{
    struct ayuda a;
    /* desactivada: nunca sale nada, ni siquiera la bienvenida */
    ay_arranque(&a, 0, 0, 1);
    { int algo = 0; for (uint64_t t = 0; t < 60000; t += 50) algo |= ay_tick(&a, t); chk(!algo && a.modo == AY_NADA && !a.saludo_pend, "con el ajuste apagado no sale nada"); }
    chk(ay_evento(&a, AYEV_ESC, 10) == 0, "Esc sin ayuda a la vista: no es para la ayuda");
    chk(ay_evento(&a, AYEV_CLIC_CUERPO, 10) == 0 && ay_evento(&a, AYEV_CLIC_X, 10) == 0, "clic sin ayuda a la vista: sigue su camino");
    chk((ay_evento(&a, AYEV_F1, 10) & (AY_CONSUME | AY_REPINTA)) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_TARJETA, "F1 abre la ayuda aunque este apagada al iniciar");

    /* encendido normal */
    ay_arranque(&a, 1000, 1, 1);
    chk(a.modo == AY_ESPERA && ay_tick(&a, 1000 + AY_ESPERA_MS - 1) == 0, "primero se ve el escritorio (nada durante la espera)");
    chk(ay_tick(&a, 1000 + AY_ESPERA_MS) == 2 && a.modo == AY_RECORRIDO && a.i == 0 && a.saludo_pend, "luego bienvenida y primer consejo");
    chk(!strcmp(ay_consejo(&a, 0)->tecla, "Tecla Windows") && strstr(ay_consejo(&a, 0)->texto, "reloj"), "el primero: tecla Windows, aplicaciones y reloj");
    chk(strstr(ay_consejo(&a, 1)->texto, "Alt") && ay_consejo(&a, 1)->accion == AYEV_VOZ, "el segundo: mantener Alt para hablar");
    chk(ay_tick(&a, 1000 + AY_ESPERA_MS + 50) == 0 && ay_tick(&a, 1000 + AY_ESPERA_MS + 100) == 1, "la barra de tiempo se mueve a 10 imagenes por segundo");
    chk(ay_progreso(&a, 1000 + AY_ESPERA_MS) == 0 && ay_progreso(&a, 1000 + AY_ESPERA_MS + AY_CONSEJO_MS / 2) == 128, "barra de tiempo a la mitad");
    { uint32_t p0 = 0; int mono = 1; for (uint64_t t = 2500; t < 2500 + AY_CONSEJO_MS; t += 37) { uint32_t p = ay_progreso(&a, t); if (p < p0 || p > 256) mono = 0; p0 = p; } chk(mono, "la barra solo crece y no pasa de 256"); }
    chk(ay_tick(&a, 2500 + AY_CONSEJO_MS) == 2 && a.i == 1, "a los 6 s, el siguiente consejo");

    /* hacer lo que dice el consejo */
    chk(ay_evento(&a, AYEV_VOZ, 9000) == AY_REPINTA && a.t_bien == 9000 + AY_BIEN_MS, "hablar con Alt mientras lo dice: 'Eso es' (sin quitarle la tecla a nadie)");
    chk(ay_progreso(&a, 9001) == 256, "con 'Eso es' la barra se llena");
    chk(ay_evento(&a, AYEV_VOZ, 9100) == 0, "repetirlo no cambia nada");
    chk(ay_tick(&a, 9000 + AY_BIEN_MS - 1) == 0 && ay_tick(&a, 9000 + AY_BIEN_MS) == 2 && a.i == 2, "y pasa al siguiente");
    chk(ay_evento(&a, AYEV_SUPER, 10400) == 0 && (a.hechos & 1u), "la tecla Windows con otro consejo a la vista: cuenta como aprendida");

    /* lo ya hecho no se repite: Windows antes de que salgan los consejos */
    ay_arranque(&a, 0, 1, 1);
    chk(ay_evento(&a, AYEV_SUPER, 300) == 0 && a.actuo, "Windows en el primer segundo: funciona normal (no es para la ayuda)");
    ay_tick(&a, AY_ESPERA_MS);
    chk(a.modo == AY_RECORRIDO && a.i == 1 && !a.saludo_pend, "si ya actuo: no se le habla encima y se salta lo que ya sabe");
    ay_arranque(&a, 0, 1, 1);
    chk(ay_evento(&a, AYEV_TECLA, 200) == 0 && ay_evento(&a, AYEV_ESC, 300) == 0, "teclas y Esc durante la espera siguen su camino");
    ay_tick(&a, AY_ESPERA_MS); chk(!a.saludo_pend && a.modo == AY_RECORRIDO, "tras escribir no hay bienvenida hablada, pero si consejos");
    ay_arranque(&a, 0, 1, 1);
    ay_evento(&a, AYEV_SUPER, 1); ay_evento(&a, AYEV_VOZ, 2); ay_evento(&a, AYEV_ENTER, 3);
    ay_tick(&a, AY_ESPERA_MS); chk(a.i == 3, "las tres cosas hechas: empieza por el cuarto consejo");

    /* recorrido entero sin tocar nada */
    { uint64_t fin; int rep; int v = recorrido_entero(1, &fin, &rep);
      chk(v == AY_NCONSEJOS && fin == AY_ESPERA_MS + (uint64_t)AY_NCONSEJOS * AY_CONSEJO_MS, "sin tocar nada: todos los consejos, uno tras otro, y se van solos");
      chk(fin <= 40000, "la ayuda de inicio dura 40 s como mucho");
      chk(rep == AY_NCONSEJOS + 1, "un repintado completo por cambio (no en cada imagen)"); }
    { uint64_t fin; int rep; chk(recorrido_entero(0, &fin, &rep) == AY_NCONSEJOS, "sin microfono tambien"); }

    /* el usuario con experiencia: se aparta */
    ay_arranque(&a, 0, 1, 1); ay_tick(&a, AY_ESPERA_MS);
    { int r = 0; for (uint32_t k = 0; k + 1 < AY_TECLEO_MAX; k++) r |= ay_evento(&a, AYEV_TECLA, 2000 + k);
      chk(r == 0 && a.modo == AY_RECORRIDO, "las teclas nunca se las queda la ayuda"); }
    chk(ay_evento(&a, AYEV_TECLA, 3000) == AY_REPINTA && a.modo == AY_NADA, "quien escribe 40 teclas ya sabe: los consejos se apartan");
    ay_arranque(&a, 0, 1, 1); ay_tick(&a, AY_ESPERA_MS);
    chk(ay_evento(&a, AYEV_ESC, 2000) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_NADA && !a.saludo_pend, "Esc cierra los consejos");
    ay_arranque(&a, 0, 1, 1); ay_tick(&a, AY_ESPERA_MS);
    chk(ay_evento(&a, AYEV_CLIC_X, 2000) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_NADA, "la X cierra los consejos");
    ay_arranque(&a, 0, 1, 1); ay_tick(&a, AY_ESPERA_MS);
    chk(ay_evento(&a, AYEV_CLIC_CUERPO, 2000) == (AY_CONSUME | AY_REPINTA) && a.i == 1 && a.t_i == 2000, "clic en el consejo: el siguiente");
    chk(ay_evento(&a, AYEV_CLIC_INICIO, 2000) == 0 && !a.guardar, "el ajuste solo se cambia en la ayuda completa");

    /* F1 en cualquier momento */
    chk(ay_evento(&a, AYEV_F1, 2100) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_TARJETA, "F1 con consejos: la ayuda completa");
    chk(ay_tick(&a, 900000) == 0 && a.modo == AY_TARJETA, "la ayuda completa no se cierra sola ni repinta en cada imagen");
    chk(ay_evento(&a, AYEV_TECLA, 2200) == 0 && ay_evento(&a, AYEV_SUPER, 2200) == 0 && ay_evento(&a, AYEV_ENTER, 2200) == 0 && a.modo == AY_TARJETA, "con la ayuda abierta todo funciona igual");
    chk(ay_evento(&a, AYEV_CLIC_CUERPO, 2300) == AY_CONSUME, "clic dentro de la ayuda: no atraviesa");
    chk(ay_evento(&a, AYEV_CLIC_INICIO, 2300) == (AY_CONSUME | AY_REPINTA) && a.inicio == 0 && a.guardar, "no mostrar al iniciar (se guarda)");
    a.guardar = 0; ay_evento(&a, AYEV_CLIC_INICIO, 2400); chk(a.inicio == 1 && a.guardar, "y volver a mostrar");
    chk(ay_evento(&a, AYEV_F1, 2500) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_NADA, "F1 otra vez la cierra");
    ay_evento(&a, AYEV_F1, 2600); chk(ay_evento(&a, AYEV_ESC, 2700) == (AY_CONSUME | AY_REPINTA) && a.modo == AY_NADA, "Esc la cierra");
    ay_arranque(&a, 0, 1, 1); ay_evento(&a, AYEV_F1, 100);
    chk(a.modo == AY_TARJETA && ay_tick(&a, AY_ESPERA_MS) == 0 && !a.saludo_pend, "F1 antes de la bienvenida: la ayuda completa, sin repetirla luego");

    /* donde van las tarjetas */
    static const uint32_t pant[][2] = { {640,400}, {800,600}, {1024,768}, {1280,720}, {1280,800}, {1366,768}, {1440,900}, {1920,1080}, {2560,1440}, {3840,2160} };
    int dentro = 1, sin_orbita = 1, zonas = 1;
    for (unsigned k = 0; k < sizeof pant / sizeof pant[0]; k++) {
        uint32_t w = pant[k][0], h = pant[k][1], bar = 44;
        struct ay_rect r = ay_rect_recorrido(w, h, bar), t = ay_rect_tarjeta(w, h, bar);
        if (r.x < 0 || r.y < 0 || r.x + r.w > (int32_t)w || r.y + r.h > (int32_t)(h - bar)) dentro = 0;
        if (t.x < 0 || t.y < 0 || t.x + t.w > (int32_t)w || t.y + t.h > (int32_t)(h - bar)) dentro = 0;
        if (h >= 720) {                    /* la orbita de las aplicaciones (gui_iconos.c, her_orbit_radius_y) mas medio icono */
            int32_t ry = (int32_t)(h * (h < 650 ? 13u : 21u) / 100u); if (ry > 175) ry = 175;
            if (r.y < (int32_t)h / 2 + ry + 40) sin_orbita = 0;
        }
        struct ayuda z; ay_arranque(&z, 0, 1, 1); ay_tick(&z, AY_ESPERA_MS);
        struct ay_rect x = ay_rect_x(r);
        if (ay_zona(&z, w, h, bar, x.x + 5, x.y + 5) != AY_ZONA_X || ay_zona(&z, w, h, bar, r.x + 20, r.y + 50) != AY_ZONA_CUERPO
            || ay_zona(&z, w, h, bar, r.x - 1, r.y) != AY_ZONA_NADA || ay_zona(&z, w, h, bar, r.x, r.y + r.h) != AY_ZONA_NADA) zonas = 0;
        ay_evento(&z, AYEV_F1, 1);
        struct ay_rect in = ay_rect_inicio(t);
        if (ay_zona(&z, w, h, bar, in.x + 4, in.y + 4) != AY_ZONA_INICIO || ay_zona(&z, w, h, bar, t.x + t.w / 2, t.y + AY_TAR_CAB + 5) != AY_ZONA_CUERPO
            || ay_zona(&z, w, h, bar, t.x + t.w, t.y) != AY_ZONA_NADA) zonas = 0;
        if (in.y + in.h > t.y + t.h || ay_rect_x(t).x + 28 > t.x + t.w) zonas = 0;
        z.modo = AY_NADA; if (ay_zona(&z, w, h, bar, t.x + 5, t.y + 5) != AY_ZONA_NADA) zonas = 0;
    }
    chk(dentro, "las dos tarjetas caben en la pantalla, encima de la barra (de 640x400 a 3840x2160)");
    chk(sin_orbita, "los consejos no tapan la orbita de las aplicaciones (720 lineas o mas)");
    chk(zonas, "la X, el ajuste y el cuerpo se reconocen; fuera de la tarjeta no es de la ayuda");
    chk(ay_zona_evento(AY_ZONA_X) == AYEV_CLIC_X && ay_zona_evento(AY_ZONA_INICIO) == AYEV_CLIC_INICIO && ay_zona_evento(AY_ZONA_NADA) == 0, "zona -> evento");

    /* el ajuste guardado */
    chk(ay_conf_inicio("inicio=0\n", 1) == 0 && ay_conf_inicio("x=3\ninicio=1\n", 0) == 1, "lee inicio=0/1");
    chk(ay_conf_inicio("", 1) == 1 && ay_conf_inicio("noinicio=0\n", 1) == 1 && ay_conf_inicio("inicio=x", 1) == 1 && ay_conf_inicio(0, 0) == 0, "sin la clave (o rara): lo de siempre");
    { char b[16]; int n = ay_conf_texto(0, b, sizeof b); chk(n == 9 && !strcmp(b, "inicio=0\n") && ay_conf_inicio(b, 1) == 0, "escribe y relee 0");
      n = ay_conf_texto(1, b, sizeof b); chk(ay_conf_inicio(b, 0) == 1, "escribe y relee 1");
      chk(ay_conf_texto(1, b, 5) == 0 && b[0] == 0, "respeta el tope"); }

    /* sin la fuente con acentos */
    { char b[200];
      ay_ascii("Mant\303\251n Alt", b, sizeof b); chk(!strcmp(b, "Manten Alt"), "quita la tilde");
      ay_ascii("\302\241Hola! \302\277Qu\303\251? \303\221and\303\272 \303\201", b, sizeof b); chk(!strcmp(b, "Hola! Que? Nandu A"), "signos de apertura, enes y mayusculas");
      ay_ascii("a\342\200\246b\360\237\230\200c", b, sizeof b); chk(!strcmp(b, "a-b-c"), "otros caracteres: un guion");
      ay_ascii("abcdef", b, 4); chk(!strcmp(b, "abc"), "respeta el tope");
      int limpio = 1;
      for (int v = 0; v < 2; v++) {
          struct ayuda z; ay_arranque(&z, 0, 1, v);
          for (int i = 0; i < AY_NCONSEJOS; i++) { ay_ascii(ay_consejo(&z, i)->texto, b, sizeof b); for (int k = 0; b[k]; k++) if ((unsigned char)b[k] >= 0x80) limpio = 0; if (strlen(ay_consejo(&z, i)->texto) > 80) limpio = 0; }
          for (int i = 0; i < AY_NFILAS; i++) { ay_ascii(ay_fila_texto(&z, i), b, sizeof b); for (int k = 0; b[k]; k++) if ((unsigned char)b[k] >= 0x80) limpio = 0; if (strlen(b) > 64 || strlen(ay_filas[i].tecla) > 24) limpio = 0; }
      }
      chk(limpio, "todos los textos se pueden escribir sin acentos y caben (consejos 80, filas 64)"); }

    /* los textos dicen lo que hay */
    { struct ayuda z; ay_arranque(&z, 0, 1, 1);
      chk(strstr(ay_saludo(&z), "Windows") && strstr(ay_saludo(&z), "Alt") && strstr(ay_saludo(&z), "F1"), "la bienvenida: Windows, Alt y F1");
      ay_arranque(&z, 0, 1, 0);
      chk(!strstr(ay_saludo(&z), "Alt") && strstr(ay_saludo(&z), "F1") && strstr(ay_fila_texto(&z, 1), "micr\303\263fono") && ay_consejo(&z, 1)->accion == 0, "sin microfono no promete la voz");
      chk(!strcmp(ay_fila_texto(&z, 99), "") && !ay_consejo(&z, -1) && !ay_consejo(&z, AY_NCONSEJOS), "fuera de rango: nada");
      int f1 = 0; for (int i = 0; i < AY_NFILAS; i++) if (!strcmp(ay_filas[i].tecla, "F1")) f1 = 1;
      chk(f1 && AY_NCONSEJOS <= 32, "la ayuda completa dice como cerrarla"); }

    printf("== test_ayuda_294: %d/%d\n", ok_n, total);
    return ok_n == total ? 0 : 1;
}
