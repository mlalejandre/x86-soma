/* SOMA_272_AGENDA: banco del calendario (cc -std=gnu11 -g -fsanitize=address,undefined -Isrc/apps tests/test_agenda_272.c src/apps/agenda_core.c -o /tmp/ta) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agenda_core.h"

static int fallos = 0, n = 0;
#define CK(c, ...) do { ++n; if (!(c)) { ++fallos; printf("FALLO (l.%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int64_t M(int y, int mo, int d, int h, int mi) { return ag_min(y, mo, d, h, mi); }
static const int64_t AHORA = 0;                       /* se fija en main: lunes 2026-11-23 10:30 */
static int64_t ahora(void) { return M(2026, 11, 23, 10, 30); }
static char buf[600];
static struct ag_ev ev;
#define PARSEA(txt) (memset(&ev, 0, sizeof ev), ag_parsea(txt, ahora(), &ev))
static const char *quando(const struct ag_ev *e) { int y, m, d, h, mi; ag_desde_min(e->ini, &y, &m, &d, &h, &mi); snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d%s", y, m, d, h, mi, e->todo_dia ? " (todo el dia)" : ""); return buf; }
#define CKP(txt, esperado, tit) do { int r_ = PARSEA(txt); const char *t_ = tit; CK(r_ == 0 && !strcmp(quando(&ev), esperado), "«%s» -> %d %s (esperado %s)", txt, r_, r_ ? "" : quando(&ev), esperado); if (t_) CK(!strcmp(ev.titulo, t_), "«%s» titulo «%s» (esperado «%s»)", txt, ev.titulo, t_); } while (0)

int main(void)
{
    (void)AHORA;
    /* ---- fechas */
    CK(ag_dias(1970, 1, 1) == 0 && ag_dias(2000, 3, 1) == 11017 && ag_dias(1969, 12, 31) == -1, "dias");
    for (int64_t d = -800000; d < 900000; d += 37) { int y, m, dd; ag_civil(d, &y, &m, &dd); if (ag_dias(y, m, dd) != d) { CK(0, "ida y vuelta %lld", (long long)d); break; } }
    CK(ag_dia_semana(ag_dias(2026, 11, 23)) == 0 && ag_dia_semana(ag_dias(2027, 1, 6)) == 2 && ag_dia_semana(0) == 3, "dia de la semana");
    CK(ag_dias_mes(2024, 2) == 29 && ag_dias_mes(1900, 2) == 28 && ag_dias_mes(2000, 2) == 29 && ag_dias_mes(2026, 4) == 30, "dias del mes");
    { int y, mo, d, h, mi; ag_desde_min(M(2026, 11, 23, 10, 30), &y, &mo, &d, &h, &mi); CK(y == 2026 && mo == 11 && d == 23 && h == 10 && mi == 30, "desde_min");
      ag_desde_min(-1, &y, &mo, &d, &h, &mi); CK(y == 1969 && mo == 12 && d == 31 && h == 23 && mi == 59, "desde_min negativo"); }
    /* horario de verano de Europa/Madrid */
    CK(ag_utc_a_local(M(2026, 3, 29, 0, 59)) == M(2026, 3, 29, 1, 59), "antes del cambio de marzo");
    CK(ag_utc_a_local(M(2026, 3, 29, 1, 0)) == M(2026, 3, 29, 3, 0), "tras el cambio de marzo: %lld", (long long)(ag_utc_a_local(M(2026, 3, 29, 1, 0)) - M(2026, 3, 29, 0, 0)));
    CK(ag_utc_a_local(M(2026, 10, 25, 0, 59)) == M(2026, 10, 25, 2, 59), "antes del cambio de octubre");
    CK(ag_utc_a_local(M(2026, 10, 25, 1, 0)) == M(2026, 10, 25, 2, 0), "tras el cambio de octubre");
    CK(ag_utc_a_local(M(2026, 7, 1, 12, 0)) == M(2026, 7, 1, 14, 0) && ag_utc_a_local(M(2026, 1, 1, 12, 0)) == M(2026, 1, 1, 13, 0), "verano e invierno");

    /* ---- lenguaje natural: los ejemplos del usuario */
    CKP("el miércoles día 6 a las 13 horas tengo un partido de fútbol con los amigos", "2027-01-06 13:00", "Partido de fútbol con los amigos");
    CK(ev.cat == AG_CAT_DEPORTE && !ev.todo_dia && ev.rep == 0, "partido: categoria deporte (%d)", ev.cat);
    CKP("mañana tengo alguna reunión", "2026-11-24 00:00 (todo el dia)", "Reunión");
    CK(ev.cat == AG_CAT_TRABAJO, "reunion: trabajo");
    CKP("mañana a las 9:30 reunión con Marta", "2026-11-24 09:30", "Reunión con Marta");
    CKP("el viernes a las 5 y media de la tarde dentista", "2026-11-27 17:30", "Dentista");
    CK(ev.cat == AG_CAT_SALUD, "dentista: salud");
    CKP("dia 15 a las 5 y media de la tarde dentista avisame 1 hora antes", "2026-12-15 17:30", "Dentista");
    CK(ev.antes == 60, "avisame 1 hora antes: %d", ev.antes);
    CKP("dentro de 2 horas llamar a Ana", "2026-11-23 12:30", "Llamar a Ana");
    CKP("dentro de 3 días revisar el coche", "2026-11-26 00:00 (todo el dia)", "Revisar el coche");
    CKP("dentro de una semana cita en el banco a las 10", "2026-11-30 10:00", "Cita en el banco");
    CKP("el 25 de diciembre cena de Navidad", "2026-12-25 00:00 (todo el dia)", "Cena de Navidad");
    CK(ev.cat == AG_CAT_SOCIAL, "cena: social");
    CKP("pasado mañana a las 20 cumpleaños de Lucía", "2026-11-25 20:00", "Cumpleaños de Lucía");
    CKP("6/12 comida familiar a las 14:30", "2026-12-06 14:30", "Comida familiar");
    CKP("hoy a las 18 llamar al médico", "2026-11-23 18:00", "Llamar al médico");
    CKP("a las 9 desayuno con Pedro", "2026-11-24 09:00", "Desayuno con Pedro");
    CKP("a las 7 de la mañana despertador", "2026-11-24 07:00", "Despertador");
    CKP("esta tarde partido de pádel", "2026-11-23 17:00", "Partido de pádel");
    CKP("el jueves de 10 a 12 reunión", "2026-11-26 10:00", "Reunión");
    CK(ev.dur == 120, "de 10 a 12: duracion %u", ev.dur);
    CKP("el 3 de marzo de 2028 boda de Carlos", "2028-03-03 00:00 (todo el dia)", "Boda de Carlos");
    CKP("el 3 de marzo boda", "2027-03-03 00:00 (todo el dia)", "Boda");
    CKP("el lunes a las 8 gimnasio", "2026-11-30 08:00", "Gimnasio");
    CKP("el lunes que viene a las 11 revisión", "2026-11-30 11:00", 0);
    CKP("el 1 de enero de 2027 a las 0:00 fin de año", "2027-01-01 00:00", "Fin de año");
    CKP("reunión 2027-02-14 a las 16h30 durante 90 minutos", "2027-02-14 16:30", "Reunión");
    CK(ev.dur == 90, "durante 90 minutos: %u", ev.dur);
    CKP("mañana a las 8 menos cuarto autobús", "2026-11-24 07:45", "Autobús");
    CKP("mañana a las 8 y cuarto autobús", "2026-11-24 08:15", "Autobús");
    CKP("mañana al mediodía comida", "2026-11-24 12:00", "Comida");
    CKP("mañana por la noche cine", "2026-11-24 21:00", "Cine");
    CKP("A LAS 22:15 PELÍCULA", "2026-11-23 22:15", "PELÍCULA");
    CKP("el sábado 12 a las 21:00 cena en casa de Juan", "2026-12-12 21:00", "Cena en casa de Juan");
    CKP("el sábado 12 de diciembre boda", "2026-12-12 00:00 (todo el dia)", "Boda");
    CKP("viaje a Roma del 3 al 7 de marzo", "2027-03-03 00:00 (todo el dia)", "Viaje a Roma");
    CK(ev.dur == 5 * 1440 && ev.cat == AG_CAT_VIAJE, "del 3 al 7: 5 dias (%u)", ev.dur);
    CKP("recuérdame el jueves a las 10 llamar al banco", "2026-11-26 10:00", "Llamar al banco");
    CKP("añade cita con el dentista el 14 de enero a las 16:30", "2027-01-14 16:30", "Cita con el dentista");
    CKP("dentro de 30 minutos sacar el pan", "2026-11-23 11:00", "Sacar el pan");
    CK(PARSEA("comprar pan") == -1, "sin fecha ni hora debe fallar");
    CK(PARSEA("") == -1 && PARSEA("   ") == -1 && PARSEA("el 31 de febrero cita") == -1, "vacio / fecha imposible");
    /* repeticiones */
    CK(PARSEA("cada lunes a las 8 gimnasio") == 0 && ev.rep == AG_REP_SEM && ev.cada == 1 && !strcmp(quando(&ev), "2026-11-30 08:00"), "cada lunes: %s rep %d", quando(&ev), ev.rep);
    CK(PARSEA("todos los días a las 7 de la mañana correr") == 0 && ev.rep == AG_REP_DIA && !strcmp(quando(&ev), "2026-11-24 07:00") && !strcmp(ev.titulo, "Correr"), "todos los dias: %s «%s»", quando(&ev), ev.titulo);
    CK(PARSEA("cada 2 semanas el martes a las 10 revisión") == 0 && ev.rep == AG_REP_SEM && ev.cada == 2 && !strcmp(quando(&ev), "2026-11-24 10:00"), "cada 2 semanas: %s %d/%d", quando(&ev), ev.rep, ev.cada);
    CK(PARSEA("todos los meses el día 5 pagar el alquiler") == 0 && ev.rep == AG_REP_MES && !strcmp(quando(&ev), "2026-12-05 00:00 (todo el dia)"), "cada mes: %s %d", quando(&ev), ev.rep);
    CK(PARSEA("cada año el 14 de febrero aniversario") == 0 && ev.rep == AG_REP_ANO && !strcmp(quando(&ev), "2027-02-14 00:00 (todo el dia)"), "cada año: %s %d", quando(&ev), ev.rep);
    CK(PARSEA("de lunes a viernes a las 9 reunión diaria") == 0 && ev.rep == AG_REP_LAB && !strcmp(quando(&ev), "2026-11-24 09:00"), "laborables: %s %d", quando(&ev), ev.rep);
    CK(PARSEA("mañana a las 10 reunión sin aviso") == 0 && ev.antes == 0, "sin aviso");

    /* ---- ocurrencias */
    static struct ag_cal cal; memset(&cal, 0, sizeof cal);
    struct ag_ev *e = ag_nuevo(&cal); strcpy(e->titulo, "Gimnasio"); e->ini = M(2026, 11, 30, 8, 0); e->dur = 60; e->rep = AG_REP_SEM; e->cada = 1;
    uint32_t id_gym = e->id; int64_t t[100];
    CK(ag_en_rango(e, M(2026, 11, 30, 0, 0), M(2027, 1, 1, 0, 0), t, 100) == 5 && t[4] == M(2026, 12, 28, 8, 0), "semanal: 5 lunes");
    CK(ag_en_rango(e, M(2026, 11, 23, 0, 0), M(2026, 11, 30, 0, 0), t, 100) == 0, "no antes de empezar");
    CK(ag_en_rango(e, M(2030, 6, 1, 0, 0), M(2030, 6, 30, 0, 0), t, 100) == 4, "semanal dentro de anos (salto directo)");
    CK(ag_en_rango(e, M(2026, 12, 7, 8, 30), M(2026, 12, 7, 8, 31), t, 100) == 1, "una celebracion en curso cuenta (dura 60)");
    CK(ag_borra_ocurrencia(&cal, id_gym, ag_dias(2026, 12, 14)) == 1 && ag_en_rango(e, M(2026, 11, 30, 0, 0), M(2027, 1, 1, 0, 0), t, 100) == 4, "borrar solo un lunes");
    e->hasta = ag_dias(2026, 12, 21); CK(ag_en_rango(e, M(2026, 11, 30, 0, 0), M(2027, 6, 1, 0, 0), t, 100) == 3, "hasta una fecha: %d", ag_en_rango(e, M(2026, 11, 30, 0, 0), M(2027, 6, 1, 0, 0), t, 100));
    e = ag_nuevo(&cal); e->ini = M(2026, 1, 31, 10, 0); e->rep = AG_REP_MES; e->cada = 1; e->titulo[0] = 'M';
    CK(ag_en_rango(e, M(2026, 1, 1, 0, 0), M(2027, 1, 1, 0, 0), t, 100) == 7, "el 31 solo en los meses con 31: %d", ag_en_rango(e, M(2026, 1, 1, 0, 0), M(2027, 1, 1, 0, 0), t, 100));
    e = ag_nuevo(&cal); e->ini = M(2024, 2, 29, 0, 0); e->todo_dia = 1; e->rep = AG_REP_ANO; e->cada = 1; e->titulo[0] = 'B';
    CK(ag_en_rango(e, M(2024, 1, 1, 0, 0), M(2033, 1, 1, 0, 0), t, 100) == 3 && t[1] == M(2028, 2, 29, 0, 0), "29 de febrero solo en bisiestos");
    e = ag_nuevo(&cal); e->ini = M(2026, 11, 23, 9, 0); e->rep = AG_REP_LAB; e->cada = 1; e->titulo[0] = 'L';
    CK(ag_en_rango(e, M(2026, 11, 23, 0, 0), M(2026, 11, 30, 0, 0), t, 100) == 5 && t[4] == M(2026, 11, 27, 9, 0), "laborables: lunes a viernes");
    CK(ag_en_rango(e, M(2026, 11, 28, 0, 0), M(2026, 11, 30, 0, 0), t, 100) == 0, "sin fin de semana");
    CK(ag_en_rango(e, M(2027, 5, 1, 0, 0), M(2027, 5, 8, 0, 0), t, 100) == 5, "laborables meses despues");
    e = ag_nuevo(&cal); e->ini = M(2026, 11, 24, 13, 0); e->rep = AG_REP_DIA; e->cada = 3; e->titulo[0] = 'D';
    CK(ag_en_rango(e, M(2026, 11, 24, 0, 0), M(2026, 12, 8, 0, 0), t, 100) == 5, "cada 3 dias");
    { struct ag_oc oc[50]; int k = ag_agenda_rango(&cal, M(2026, 11, 23, 0, 0), M(2026, 11, 28, 0, 0), oc, 50);
      int orden = 1; for (int i = 1; i < k; ++i) if (oc[i].ini < oc[i - 1].ini) orden = 0; CK(k > 5 && orden, "agenda ordenada (%d)", k);
      struct ag_oc p; CK(ag_proxima(&cal, M(2026, 11, 23, 10, 0), &p) && p.ini == M(2026, 11, 24, 9, 0), "proxima: %lld", (long long)p.ini); }
    CK(ag_numero(&cal) == 5, "numero de eventos %u", ag_numero(&cal));
    CK(ag_borra(&cal, id_gym) == 1 && ag_borra(&cal, id_gym) == 0 && ag_numero(&cal) == 4, "borrar");

    /* ---- guardado propio */
    { static struct ag_cal a, b; memset(&a, 0, sizeof a); static char tx[40000];
      struct ag_ev *x = ag_nuevo(&a); PARSEA("el miércoles día 6 a las 13 horas tengo un partido de fútbol con los amigos avisame 20 minutos antes"); uint32_t id = x->id; *x = ev; x->id = id; x->activo = 1;
      strcpy(x->lugar, "Campo|municipal"); strcpy(x->nota, "Llevar\nbotas");
      x = ag_nuevo(&a); PARSEA("cada 2 semanas el martes a las 10 revisión"); id = x->id; *x = ev; x->id = id; x->activo = 1; x->exc[0] = (int32_t)ag_dias(2026, 12, 8);
      uint32_t L = ag_serializa(&a, tx, sizeof tx); CK(L > 50 && !strncmp(tx, "SOMA-AGENDA 1\n", 14), "serializa");
      CK(ag_carga(&b, tx, L) == 2, "carga 2");
      int ok = b.sig_id == a.sig_id; for (int i = 0; i < 2; ++i) { struct ag_ev *p = &a.ev[i], *q = ag_busca_id(&b, p->id); if (!q || q->ini != p->ini || q->dur != p->dur || q->rep != p->rep || q->cada != p->cada || q->antes != p->antes || q->cat != p->cat || q->exc[0] != p->exc[0] || q->todo_dia != p->todo_dia || strcmp(q->titulo, p->titulo)) ok = 0; }
      CK(ok, "ida y vuelta del formato propio");
      CK(!strcmp(ag_busca_id(&b, 1)->lugar, "Campo municipal") && !strcmp(ag_busca_id(&b, 1)->nota, "Llevar botas"), "los separadores se limpian");
      CK(ag_carga(&b, "hola", 4) == -1 && ag_carga(&b, "SOMA-AGENDA 1\nE|1|x\nE|basura\n", 28) == 0, "basura rechazada");
      char tx2[100]; CK(ag_serializa(&a, tx2, sizeof tx2) == 0, "no cabe: devuelve 0"); }

    /* ---- texto */
    { static char o[3000]; static struct ag_cal a; memset(&a, 0, sizeof a);
      struct ag_ev *x = ag_nuevo(&a); PARSEA("hoy a las 18 llamar al médico durante 1 hora"); uint32_t id = x->id; *x = ev; x->id = id; x->activo = 1;
      ag_describe(x, x->ini, ahora(), o, sizeof o); CK(!strcmp(o, "Llamar al médico \xE2\x80\x94 Hoy a las 18:00-19:00"), "describe: %s", o);
      x = ag_nuevo(&a); PARSEA("el miércoles día 6 a las 13 horas partido de fútbol"); id = x->id; *x = ev; x->id = id; x->activo = 1; strcpy(x->lugar, "Retiro");
      ag_describe(x, x->ini, ahora(), o, sizeof o); CK(strstr(o, "Mi\xC3\xA9rcoles 6 de enero de 2027 a las 13:00 en Retiro") != 0, "describe lejano: %s", o);
      ag_dicho(x, x->ini, ahora(), 10, o, sizeof o); CK(!strcmp(o, "En 10 minutos: Partido de fútbol, a las 13:00, en Retiro."), "dicho: %s", o);
      ag_lista_txt(&a, ag_dia_de(ahora()), ag_dia_de(ahora()) + 60, ahora(), o, sizeof o); CK(strstr(o, "Hoy, lunes 23 de noviembre:\n  [1] 18:00-19:00 Llamar al médico") != 0, "lista: %s", o);
      ag_lista_txt(&a, ag_dia_de(ahora()), ag_dia_de(ahora()) + 1, ahora(), o, sizeof o); CK(!strstr(o, "Partido"), "la lista respeta el tramo");
      ag_lista_txt(&a, 1, 2, ahora(), o, sizeof o); CK(strstr(o, "No hay nada") != 0, "lista vacia"); }
    { int64_t d0, d1; int64_t hoy = ag_dia_de(ahora());
      CK(ag_parsea_tramo("hoy", ahora(), &d0, &d1) == 0 && d0 == hoy && d1 == hoy + 1, "tramo hoy");
      CK(ag_parsea_tramo("mañana", ahora(), &d0, &d1) == 0 && d0 == hoy + 1, "tramo manana");
      CK(ag_parsea_tramo("la semana", ahora(), &d0, &d1) == 0 && d0 == hoy && d1 == hoy + 7, "tramo semana (hoy es lunes)");
      CK(ag_parsea_tramo("la semana que viene", ahora(), &d0, &d1) == 0 && d0 == hoy + 7, "tramo semana que viene");
      CK(ag_parsea_tramo("este mes", ahora(), &d0, &d1) == 0 && d0 == ag_dias(2026, 11, 1) && d1 == ag_dias(2026, 12, 1), "tramo mes");
      CK(ag_parsea_tramo("próximos 3 días", ahora(), &d0, &d1) == 0 && d1 - d0 == 3, "tramo proximos 3 dias");
      CK(ag_parsea_tramo("el miércoles día 6", ahora(), &d0, &d1) == 0 && d0 == ag_dias(2027, 1, 6), "tramo miercoles 6");
      CK(ag_parsea_tramo("patatas", ahora(), &d0, &d1) == -1, "tramo sin sentido"); }

    /* ---- .ics */
    { static struct ag_cal a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); static char ics[40000];
      const char *frases[] = { "el miércoles día 6 a las 13 horas tengo un partido de fútbol con los amigos durante 2 horas avisame 20 minutos antes", "mañana tengo reunión",
                               "cada lunes a las 8 gimnasio", "de lunes a viernes a las 9 reunión diaria", "cada 2 semanas el martes a las 10 revisión, con coma; y punto y coma", 0 };
      for (int i = 0; frases[i]; ++i) { struct ag_ev *x = ag_nuevo(&a); PARSEA(frases[i]); uint32_t id = x->id; *x = ev; x->id = id; x->activo = 1; if (i == 0) strcpy(x->lugar, "Retiro, Madrid"); if (i == 2) x->hasta = ag_dias(2027, 6, 30); if (i == 2) x->exc[0] = (int32_t)ag_dias(2026, 12, 14); }
      uint32_t L = ag_ics_exporta(&a, ics, sizeof ics);
      CK(L > 300 && strstr(ics, "BEGIN:VCALENDAR\r\n") && strstr(ics, "DTSTART:20270106T130000\r\n") && strstr(ics, "DTEND:20270106T150000\r\n") && strstr(ics, "SUMMARY:Partido de f\xC3\xBAtbol con los amigos\r\n")
         && strstr(ics, "LOCATION:Retiro\\, Madrid\r\n") && strstr(ics, "TRIGGER:-PT20M") && strstr(ics, "DTSTART;VALUE=DATE:20261124") && strstr(ics, "RRULE:FREQ=WEEKLY;UNTIL=20270630")
         && strstr(ics, "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR") && strstr(ics, "RRULE:FREQ=WEEKLY;INTERVAL=2") && strstr(ics, "EXDATE:20261214T080000") && strstr(ics, "END:VCALENDAR"), "exporta: %s", ics);
      int k = ag_ics_importa(&b, ics, L); CK(k == 5, "importa 5 (%d)", k);
      int ok = 1; for (int i = 0; i < 5; ++i) { struct ag_ev *p = &a.ev[i]; struct ag_ev *q = &b.ev[i];
        if (q->ini != p->ini || q->dur != p->dur || q->rep != p->rep || q->cada != p->cada || q->antes != p->antes || q->todo_dia != p->todo_dia || q->hasta != p->hasta || q->exc[0] != p->exc[0] || strcmp(q->titulo, p->titulo) || strcmp(q->lugar, p->lugar)) { ok = 0; printf("   dif evento %d: «%s»/«%s» ini %lld/%lld rep %d/%d\n", i, p->titulo, q->titulo, (long long)p->ini, (long long)q->ini, p->rep, q->rep); } }
      CK(ok, "exportar e importar es identico");
      /* un .ics de Google (con Z, TZID, lineas plegadas, COUNT, alarma) */
      static struct ag_cal g; memset(&g, 0, sizeof g);
      const char *gg = "BEGIN:VCALENDAR\r\nPRODID:-//Google Inc//Google Calendar 70.9054//EN\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nDTSTART:20270115T120000Z\r\nDTEND:20270115T130000Z\r\nUID:abc@google.com\r\n"
        "SUMMARY:Reuni\xC3\xB3n con el equipo de proyecto con un titulo muy largo que\r\n  se pliega en varias lineas\r\nLOCATION:Sala 3\\nPlanta 2\r\nRRULE:FREQ=WEEKLY;COUNT=4;BYDAY=FR\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT1H\r\nEND:VALARM\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nDTSTART;TZID=Europe/Madrid:20270301T093000\r\nSUMMARY:Dentista\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART;VALUE=DATE:20270320\r\nDTEND;VALUE=DATE:20270322\r\nSUMMARY:Vacaciones\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nSUMMARY:sin fecha\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
      CK(ag_ics_importa(&g, gg, (uint32_t)strlen(gg)) == 3, "google: 3 eventos (el sin fecha se ignora)");
      struct ag_ev *q = &g.ev[0];
      CK(q->ini == M(2027, 1, 15, 13, 0) && q->dur == 60 && q->rep == AG_REP_SEM && q->antes == 60 && !strncmp(q->titulo, "Reuni\xC3\xB3n con el equipo de proyecto con un titulo muy largo que  se pliega", 60) && !strcmp(q->lugar, "Sala 3 Planta 2"), "google: Z a hora local (invierno +1), plegado, alarma: ini %lld «%s» «%s»", (long long)q->ini, q->titulo, q->lugar);
      CK(ag_en_rango(q, M(2027, 1, 1, 0, 0), M(2027, 12, 31, 0, 0), t, 100) == 4, "google: COUNT=4 -> 4 celebraciones (%d)", ag_en_rango(q, M(2027, 1, 1, 0, 0), M(2027, 12, 31, 0, 0), t, 100));
      CK(g.ev[1].ini == M(2027, 3, 1, 9, 30) && !g.ev[1].todo_dia, "google: TZID");
      CK(g.ev[2].todo_dia && g.ev[2].ini == M(2027, 3, 20, 0, 0), "google: todo el dia");
      CK(g.ev[2].dur == 2880, "google: vacaciones de 2 dias: %u", g.ev[2].dur);
      CK(ag_ics_importa(&g, "hola", 4) == -1, "no es un .ics");
      { static struct ag_cal v, w; memset(&v, 0, sizeof v); memset(&w, 0, sizeof w); struct ag_ev *x = ag_nuevo(&v); PARSEA("viaje a Roma del 3 al 7 de marzo"); uint32_t id = x->id; *x = ev; x->id = id; x->activo = 1;
        char ic[3000]; uint32_t L2 = ag_ics_exporta(&v, ic, sizeof ic); CK(strstr(ic, "DTEND;VALUE=DATE:20270308") != 0, "DTEND exclusivo de 5 dias");
        CK(ag_ics_importa(&w, ic, L2) == 1 && w.ev[0].dur == 5 * 1440 && w.ev[0].todo_dia, "viaje de varios dias: ida y vuelta"); } }

    /* ---- avisos */
    { static struct ag_cal a; memset(&a, 0, sizeof a); struct ag_avisos st; memset(&st, 0, sizeof st); uint32_t id; int64_t ini;
      struct ag_ev *x = ag_nuevo(&a); x->ini = M(2026, 11, 23, 11, 0); strcpy(x->titulo, "A"); uint32_t ida = x->id;
      x = ag_nuevo(&a); x->ini = M(2026, 11, 23, 11, 30); x->antes = 0; strcpy(x->titulo, "B");
      x = ag_nuevo(&a); x->ini = M(2026, 11, 23, 12, 0); x->antes = 45; strcpy(x->titulo, "C"); uint32_t idc = x->id;
      CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 10, 30), 10, &id, &ini), "aun no");
      CK(ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 10, 50), 10, &id, &ini) && id == ida && ini == M(2026, 11, 23, 11, 0), "A a las 10:50 (10 min antes)");
      CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 10, 51), 10, &id, &ini), "no se repite");
      CK(ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 11, 15), 10, &id, &ini) && id == idc, "C con su propio aviso de 45 min");
      CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 11, 29), 10, &id, &ini), "B no avisa nunca (antes=0)");
      CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 11, 15), 10, &id, &ini) && !ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 12, 1), 10, &id, &ini), "ni despues de empezar");
      memset(&st, 0, sizeof st); CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 23, 10, 55), 0, &id, &ini), "sin aviso por defecto: A no avisa");
      /* repetidos: cada lunes avisa cada lunes */
      memset(&a, 0, sizeof a); memset(&st, 0, sizeof st); x = ag_nuevo(&a); x->ini = M(2026, 11, 23, 8, 0); x->rep = AG_REP_SEM; x->cada = 1; strcpy(x->titulo, "G");
      CK(ag_aviso_siguiente(&a, &st, M(2026, 11, 30, 7, 55), 10, &id, &ini) && ini == M(2026, 11, 30, 8, 0), "avisa el lunes siguiente");
      CK(ag_aviso_siguiente(&a, &st, M(2026, 12, 7, 7, 55), 10, &id, &ini) && ini == M(2026, 12, 7, 8, 0), "y el otro");
      /* de todo el dia: a las 09:00 */
      memset(&a, 0, sizeof a); memset(&st, 0, sizeof st); x = ag_nuevo(&a); x->ini = M(2026, 11, 24, 0, 0); x->todo_dia = 1; strcpy(x->titulo, "T");
      CK(!ag_aviso_siguiente(&a, &st, M(2026, 11, 24, 8, 0), 10, &id, &ini) && ag_aviso_siguiente(&a, &st, M(2026, 11, 24, 8, 55), 10, &id, &ini), "todo el dia avisa a las 9"); }

    /* ---- aguante: basura de todo tipo no rompe nada (ASAN/UBSAN) */
    { srand(7); static const char *const P[] = { "el", "miércoles", "dia", "6", "a", "las", "13", "horas", "mañana", "cada", "lunes", "de", "viernes", "y", "media", "cuarto", "menos", "dentro", "2", "semanas", "durante", "hora", "avisame", "antes", "99999999999", "31", "2/30", "0:99", "25h", "hoy", "pasado", "enero", "2027", "-", "\xC3", "\xC2", "reunión", "tarde", "noche", "todos", "los", "meses", "año", "5/", "/", "::", "..", "(", ")" };
      char q[300]; struct ag_ev e2; int64_t a2, b2; static struct ag_cal cc;
      for (int it = 0; it < 60000; ++it) { q[0] = 0; int w = rand() % 14; for (int i = 0; i < w; ++i) { strcat(q, P[rand() % (int)(sizeof P / sizeof *P)]); strcat(q, rand() % 9 ? " " : ""); }
        (void)ag_parsea(q, ahora() + (rand() % 1000000), &e2); (void)ag_parsea_tramo(q, ahora(), &a2, &b2); }
      for (int it = 0; it < 4000; ++it) { for (int i = 0; i < 200; ++i) q[i] = (char)(rand() % 256); q[199] = 0; (void)ag_ics_importa(&cc, q, 199); (void)ag_carga(&cc, q, 199); }
      CK(1, "aguante"); }
    printf("%s banco del calendario (272): %d comprobaciones, %d fallos\n", fallos ? "FALLO" : "ok ", n, fallos);
    return fallos != 0;
}
