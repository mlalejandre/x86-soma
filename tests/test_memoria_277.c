/* SOMA_277_MEMORIA: pruebas del nucleo de la memoria (src/agent/memoria_core.c) en el anfitrion, con ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "memoria_core.h"

static int checks, fails;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CKE(a, b) do { long long _a = (long long)(a), _b = (long long)(b); checks++; if (_a != _b) { fails++; printf("  FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CKS(a, b) do { const char *_a = (a), *_b = (b); checks++; if (!_a || strcmp(_a, _b)) { fails++; printf("  FALLO %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b); } } while (0)

void *mc_alloc(uint32_t n) { return calloc(1, n); }
void mc_free(void *p) { free(p); }

/* vector de prueba: un "tema" (0..7) mas un poco de ruido determinista -> normalizado a int8 con la misma funcion que usa el nucleo */
static void vec_tema(int tema, int ruido, int8_t *v, int dim)
{
    char js[16384]; int p = 0;
    p += sprintf(js, "{\"data\":[{\"embedding\":[");
    for (int i = 0; i < dim; i++) {
        double x = (i % 8 == tema) ? 1.0 : 0.0;
        x += ((i * 7 + ruido * 13) % 11) * 0.004;
        p += sprintf(js + p, "%s%.6f", i ? "," : "", x);
    }
    sprintf(js + p, "]}]}");
    int d = mc_parse_embedding(js, v, MC_DIM_MAX);
    if (d != dim) printf("  (vec_tema dim %d != %d)\n", d, dim);
}

int main(void)
{
    /* 1. fechas */
    CKE(mc_epoch(1970, 1, 1, 0, 0, 0), 0);
    CKE(mc_epoch(2026, 11, 23, 14, 13, 0), 1795443180u);
    char f[32]; mc_fecha(1795443180u, f, sizeof f); CKS(f, "23/11/2026 14:13");
    mc_fecha(mc_epoch(2024, 2, 29, 9, 5, 0), f, sizeof f); CKS(f, "29/02/2024 09:05");
    mc_fecha(0, f, sizeof f); CKS(f, "01/01/1970 00:00");

    /* 2. plegado de texto */
    char o[128];
    mc_fold("¡Vivo en Bilbao, desde 2019!", o, sizeof o); CKS(o, "vivo en bilbao desde 2019");
    mc_fold("  Camión  AÑO nuevo ", o, sizeof o); CKS(o, "camion ano nuevo");
    mc_fold("Ünïcödé Ñandú", o, sizeof o); CKS(o, "unicode nandu");
    mc_fold("日本語 ok", o, sizeof o); CKS(o, "ok");
    CKE(mc_fold("abcdefghij", o, 5), 4); CKS(o, "abcd");

    /* 3. no factual / actualizacion temporal */
    CK(mc_noFactual("hola")); CK(mc_noFactual("¿Dónde vivo?")); CK(mc_noFactual("Qué hora es?")); CK(mc_noFactual("gracias")); CK(mc_noFactual("Buenos días!"));
    CK(!mc_noFactual("Mi perro se llama Thor y tiene cuatro años")); CK(!mc_noFactual("Vivo en Bilbao con mi hermana"));
    CK(mc_parece_actualizacion("El usuario vive en Madrid", "El usuario vive en Bilbao"));
    CK(mc_parece_actualizacion("El usuario trabaja en Telefónica", "El usuario trabaja en Iberdrola"));
    CK(mc_parece_actualizacion("El usuario vive en Madrid", "El usuario se ha mudado a Sevilla"));
    CK(mc_parece_actualizacion("x", "Ahora trabajo en una empresa nueva"));
    CK(!mc_parece_actualizacion("El usuario vive en Bilbao", "El usuario vive en Bilbao capital"));       /* uno empieza por el otro: no cambia */
    CK(!mc_parece_actualizacion("El usuario tiene un perro", "El usuario tiene una hermana"));
    CK(!mc_parece_actualizacion("A", "La capital de Francia es Paris"));

    /* 4. embeddings: parseo, normalizacion, notacion cientifica */
    int8_t v[MC_DIM_MAX];
    CKE(mc_parse_embedding("{\"data\":[{\"embedding\":[1,0,0,0,0,0,0,0],\"index\":0}]}", v, MC_DIM_MAX), 8);
    CKE(v[0], 127); CKE(v[1], 0);
    CKE(mc_parse_embedding("{\"embedding\": [ -0.5, 0.5, 1e-3, -2.5E-1, 0.0, 0, 3e-8, 0.25 ] }", v, MC_DIM_MAX), 8);
    CK(v[0] < 0 && v[1] > 0 && v[0] == -v[1] && v[3] < 0 && v[2] >= 0 && v[2] < 3);
    CKE(mc_parse_embedding("{\"embedding\":[[0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8]]}", v, MC_DIM_MAX), 8);    /* lista anidada */
    CK(mc_parse_embedding("{\"embedding\":[0,0,0,0,0,0,0,0]}", v, MC_DIM_MAX) <= 0);                   /* norma cero */
    CK(mc_parse_embedding("{\"error\":\"x\"}", v, MC_DIM_MAX) <= 0);
    CK(mc_parse_embedding("{\"embedding\":[1,2,3]}", v, MC_DIM_MAX) <= 0);                              /* muy corto */
    CK(mc_parse_embedding("{\"embedding\":[1,2,3,4,5,6,7,x]}", v, MC_DIM_MAX) <= 0);
    int8_t a[64], b[64], c[64];
    vec_tema(1, 0, a, 64); vec_tema(1, 3, b, 64); vec_tema(2, 0, c, 64);
    CK(mc_dot(a, a, 64) > MC_COS_UNO * 97 / 100);                      /* un vector consigo mismo ~ 1 */
    CK(mc_dot(a, b, 64) > MC_COS_UNO * 9 / 10);                        /* mismo tema: muy cercano */
    CK(mc_dot(a, c, 64) < MC_COS_UNO / 4);                             /* otro tema: lejano */

    /* 5. celdas y episodios, busqueda vectorial */
    struct mc m; CKE(mc_init(&m, 4096), 0);
    int8_t vt[8][64];
    for (int t = 0; t < 8; t++) vec_tema(t, 1, vt[t], 64);
    uint32_t t0 = mc_epoch(2026, 11, 1, 10, 0, 0);
    uint32_t c1 = mc_add(&m, MC_CELDA, 0, "El usuario vive en Bilbao", "Conversacion del 01/11/2026", 8, t0, 1, vt[0], 64);
    uint32_t c2 = mc_add(&m, MC_CELDA, 0, "El perro del usuario se llama Thor", "Conversacion", 7, t0, 1, vt[1], 64);
    uint32_t c3 = mc_add(&m, MC_CELDA, 0, "La capital de Francia es Paris", "", 5, t0, 1, vt[2], 64);
    uint32_t e1 = mc_add(&m, MC_EPISODIO, MC_ROL_USUARIO, "Hoy he sacado a pasear a Thor por el parque", "", 5, t0 + 60, 1, vt[1], 64);
    uint32_t e2 = mc_add(&m, MC_EPISODIO, MC_ROL_ASISTENTE, "Que bien, a Thor le gustara", "", 5, t0 + 90, 1, vt[1], 64);
    CKE(c1, 1); CKE(c2, 2); CKE(c3, 3); CKE(e1, 4); CKE(e2, 5);
    CK(mc_get(&m, 0) == 0 && mc_get(&m, 6) == 0 && mc_get(&m, 3) != 0);
    uint32_t ids[16]; int32_t sc[16];
    int n = mc_busca(&m, vt[1], 64, "perro", 1u << MC_CELDA, 0, 6129, 5, ids, sc);
    CKE(n, 1); CKE(ids[0], c2); CK(sc[0] > 0);
    n = mc_busca(&m, vt[1], 64, "perro", (1u << MC_CELDA) | (1u << MC_EPISODIO), 0, 6129, 5, ids, sc);
    CKE(n, 3);
    CK(ids[0] == c2 || ids[0] == e1 || ids[0] == e2);
    n = mc_busca(&m, vt[1], 64, "", 1u << MC_EPISODIO, 0, 6129, 5, ids, sc); CKE(n, 2);
    n = mc_busca(&m, vt[5], 64, "", 1u << MC_CELDA, 0, 6129, 5, ids, sc); CKE(n, 0);              /* nada parecido */
    CKE(mc_find_exacto(&m, MC_CELDA, "el usuario VIVE en  Bilbao."), c1);
    CKE(mc_find_exacto(&m, MC_CELDA, "El usuario vive en Madrid"), 0);
    CKE(mc_find_exacto(&m, MC_EPISODIO, "El usuario vive en Bilbao"), 0);

    /* 6. vecinos y sustitucion */
    uint32_t nid[4]; int32_t nd[4];
    CKE(mc_vecinos(&m, vt[0], 64, 2, nid, nd), 2); CKE(nid[0], c1); CK(nd[0] > nd[1]);
    uint32_t c4 = mc_add(&m, MC_CELDA, 0, "El usuario vive en Madrid", "", 8, t0 + 1000, 2, vt[0], 64);
    CKE(mc_set_estado(&m, c1, MC_SUSTITUIDO, t0 + 1000, c4), 0);
    n = mc_busca(&m, vt[0], 64, "", 1u << MC_CELDA, 0, 6129, 5, ids, sc); CKE(n, 1); CKE(ids[0], c4);       /* lo sustituido no sale */
    n = mc_busca(&m, vt[0], 64, "", 1u << MC_CELDA, 1, 6129, 5, ids, sc); CKE(n, 2);                         /* ...salvo si se pide el historial */
    CKE(mc_find_exacto(&m, MC_CELDA, "El usuario vive en Bilbao"), 0);
    CKE(mc_refuerza(&m, c4, 7), 0); CKE(mc_get(&m, c4)->imp, 9); CKE(mc_get(&m, c4)->refuerzos, 1);
    for (int i = 0; i < 5; i++) mc_refuerza(&m, c4, 8);
    CKE(mc_get(&m, c4)->imp, 10);
    uint32_t ce, ep, ar, su, sv; mc_stats(&m, &ce, &ep, &ar, &su, &sv);
    CKE(ce, 3); CKE(ep, 2); CKE(ar, 0); CKE(su, 1); CKE(sv, 0);

    /* 7. canal lexical: sin vector de consulta, y para los elementos sin embedding */
    uint32_t c5 = mc_add(&m, MC_CELDA, 0, "La hermana del usuario se llama Marta y es medica", "Conversacion", 7, t0, 3, 0, 0);
    uint32_t c6 = mc_add(&m, MC_CELDA, 0, "El usuario es alergico a la penicilina", "Conversacion", 9, t0, 3, 0, 0);
    uint32_t c7 = mc_add(&m, MC_CELDA, 0, "La reunion del equipo es los lunes a las 11", "Agenda", 6, t0, 3, 0, 0);
    n = mc_busca(&m, 0, 0, "¿Cómo se llama la hermana del usuario?", 1u << MC_CELDA, 0, 0, 3, ids, sc);
    CK(n >= 1 && ids[0] == c5 && sc[0] < 0);
    n = mc_busca(&m, 0, 0, "alergias penicilina", 1u << MC_CELDA, 0, 0, 3, ids, sc); CK(n >= 1 && ids[0] == c6);   /* "alergias" no coincide, "penicilina" si */
    n = mc_busca(&m, 0, 0, "a que hora es la reunion de los lunes", 1u << MC_CELDA, 0, 0, 3, ids, sc); CK(n >= 1 && ids[0] == c7);
    n = mc_busca(&m, 0, 0, "capital de Francia", 1u << MC_CELDA, 0, 0, 3, ids, sc); CK(n >= 1 && ids[0] == c3);   /* tambien entre los que tienen vector */
    n = mc_busca(&m, 0, 0, "xyzzy plugh", 1u << MC_CELDA, 0, 0, 3, ids, sc); CKE(n, 0);
    /* con vector de consulta: primero los vectoriales, luego los lexicales pendientes */
    n = mc_busca(&m, vt[1], 64, "hermana Marta", (1u << MC_CELDA), 0, 6129, 5, ids, sc);
    CK(n == 2 && ids[0] == c2 && sc[0] > 0 && ids[1] == c5 && sc[1] < 0);
    CKE(mc_set_vec(&m, c5, vt[3], 64), 0);
    n = mc_busca(&m, vt[1], 64, "hermana Marta", (1u << MC_CELDA), 0, 6129, 5, ids, sc); CKE(n, 1);               /* ya tiene vector: deja de ser lexical */

    /* 8. respuestas del LLM */
    struct mc_hecho h[8];
    n = mc_parse_hechos("```json\n{\"hechos\":[{\"texto\":\"El usuario vive en Bilbao\",\"importancia\":8,\"tipo\":\"dato\"},"
                        "{\"texto\":\"Siempre  responde\\n en espa\\u00f1ol\",\"importancia\":\"10\",\"tipo\":\"instruccion\"},\"Un hecho suelto\",{\"texto\":\"a\"}]}\n```", h, 8);
    CKE(n, 3);
    CKS(h[0].texto, "El usuario vive en Bilbao"); CKE(h[0].imp, 8); CKE(h[0].instruccion, 0);
    CKS(h[1].texto, "Siempre responde en espa\xC3\xB1ol"); CKE(h[1].imp, 10); CKE(h[1].instruccion, 1);
    CKS(h[2].texto, "Un hecho suelto"); CKE(h[2].imp, 6);
    CKE(mc_parse_hechos("{\"hechos\": []}", h, 8), 0);
    CKE(mc_parse_hechos("Lo siento, no puedo", h, 8), -1);
    CKE(mc_parse_hechos("{\"hechos\":[{\"texto\":\"Uno\",\"importancia\":3.7,\"extra\":{\"a\":[1,2,{\"b\":\"}\"}]},\"tipo\":\"dato\"},{\"texto\":\"Dos hechos\",\"importancia\":99}]}", h, 8), 2);
    CKE(h[0].imp, 3); CKE(h[1].imp, 10);
    CKE(mc_parse_hechos("{\"hechos\":[{\"texto\":\"a\"},{\"texto\":\"b b b\"},{\"texto\":\"c c c\"}]}", h, 1), 1);        /* tope */
    int cf;
    CKE(mc_parse_relacion("{\"relation\":\"supersedes\",\"confidence\":0.92,\"reason\":\"mudanza\"}", &cf), MC_REL_SUSTITUYE); CKE(cf, 92);
    CKE(mc_parse_relacion("Aqui: {\"relation\": \"same\", \"confidence\": 1}", &cf), MC_REL_IGUAL); CKE(cf, 100);
    CKE(mc_parse_relacion("{\"relation\":\"unrelated\",\"confidence\":0.5}", &cf), MC_REL_NO_RELACIONADO); CKE(cf, 50);
    CKE(mc_parse_relacion("{\"relation\":\"complementary\"}", &cf), MC_REL_COMPLEMENTA); CKE(cf, 0);
    CKE(mc_parse_relacion("nada", &cf), MC_REL_NINGUNA);
    char pr[4096];
    CK(mc_prompt_hechos("Me llamo Marcos", pr, sizeof pr) > 400 && strstr(pr, "<<<Me llamo Marcos>>>") && strstr(pr, "\"hechos\""));
    CK(strstr(pr, "\xC3\xA1rbol de conocimiento") && strstr(pr, "tercera persona"));
    CK(mc_prompt_relacion("Vive en Madrid", 7, "Vive en Bilbao", "2026-11-23", pr, sizeof pr) > 300 && strstr(pr, "id=7") && strstr(pr, "supersedes"));
    char peq[40]; mc_prompt_hechos("x", peq, sizeof peq); CK(strlen(peq) < 40);                                    /* nunca se pasa del buffer */

    /* 9. contexto para el prompt */
    uint32_t cs[2] = { c4, c2 }, es[2] = { e1, e2 };
    char cx[2048]; uint32_t cl = mc_contexto(&m, cs, 2, es, 2, cx, sizeof cx);
    CK(cl == strlen(cx) && strstr(cx, "[MEMORIA A LARGO PLAZO") && strstr(cx, "- El usuario vive en Madrid (desde 01/11/2026"));
    CK(strstr(cx, "fuente: Conversacion)") && strstr(cx, "] Usuario: Hoy he sacado a pasear a Thor") && strstr(cx, "] Asistente: Que bien"));
    CK(strstr(cx, "[01/11/2026 10:01]") != 0);
    {   /* un mensaje largo se recorta sin partir un caracter */
        char gr[1400]; int q = 0; for (int i = 0; i < 600; i++) { gr[q++] = (char)0xC3; gr[q++] = (char)0xB1; } gr[q] = 0;     /* 1200 bytes de "ñ" */
        uint32_t eg = mc_add(&m, MC_EPISODIO, MC_ROL_USUARIO, gr, "", 5, t0, 1, 0, 0);
        char cg[2048]; mc_contexto(&m, 0, 0, &eg, 1, cg, sizeof cg);
        const char *dd = strstr(cg, "...");
        CK(dd && strlen(cg) < 700 && ((uint8_t)dd[-1] == 0xB1) && ((uint8_t)dd[-2] == 0xC3));
    }
    CKE(mc_contexto(&m, 0, 0, 0, 0, cx, sizeof cx), 0);
    char pc[64]; mc_contexto(&m, cs, 2, es, 2, pc, sizeof pc); CK(strlen(pc) < 64);
    char ch[1024]; uint32_t cd = mc_contexto_charla(&m, es, 2, ch, sizeof ch);
    CK(cd == strlen(ch) && strstr(ch, "[MENSAJES RECIENTES DE ESTA CHARLA]") && strstr(ch, "] Usuario: Hoy he sacado") && strstr(ch, "] Asistente: Que bien"));
    CKE(mc_contexto_charla(&m, es, 0, ch, sizeof ch), 0);

    /* 10. guardado por fragmentos: ida y vuelta, corrupcion */
    uint32_t cap = mc_frag_max_bytes();
    uint8_t *buf = malloc(cap);
    for (int i = 0; i < 2500; i++) {                 /* mas de 2 fragmentos */
        char tx[64]; sprintf(tx, "Hecho numero %d del usuario", i);
        mc_add(&m, MC_CELDA, 0, tx, "carga", 5, t0 + (uint32_t)i, 5, i % 3 ? vt[i % 8] : 0, i % 3 ? 64 : 0);
    }
    CKE(mc_nfrag(&m), 3);
    struct mc r; CKE(mc_init(&r, 4096), 0);
    for (uint32_t i = 0; i < mc_nfrag(&m); i++) {
        CK(mc_frag_sucio(&m, i));
        int len = mc_frag_guarda(&m, i, buf, cap); CK(len > 0);
        CKE(mc_frag_carga(&r, buf, (uint32_t)len), 0);
        if (i == 0) { buf[len / 2] ^= 0x40; CK(mc_frag_carga(&r, buf, (uint32_t)len) != 0); buf[len / 2] ^= 0x40; }       /* corrupto: se rechaza */
        mc_frag_limpio(&m, i); CK(!mc_frag_sucio(&m, i));
    }
    CKE(r.n, m.n);
    int same = 1;
    for (uint32_t i = 1; i <= m.n; i++) {
        const struct mc_item *x = mc_get(&m, i), *y = mc_get(&r, i);
        if (!(x->id == y->id && x->t == y->t && x->t_hasta == y->t_hasta && x->sust_por == y->sust_por && x->kind == y->kind && x->estado == y->estado && x->imp == y->imp &&
              x->rol == y->rol && x->refuerzos == y->refuerzos && x->dim == y->dim && !strcmp(x->texto, y->texto) && !strcmp(x->fuente, y->fuente) &&
              ((!x->vec && !y->vec) || (x->vec && y->vec && !memcmp(x->vec, y->vec, x->dim))))) { same = 0; break; }
    }
    CK(same);
    CKE(mc_frag_carga(&r, buf, 10), -1);                                      /* fragmento truncado */
    int len0 = mc_frag_guarda(&m, 0, buf, cap);
    CK(mc_frag_carga(&r, buf, (uint32_t)len0) != 0);                          /* ya cargado: no encaja con lo que hay */
    CK(mc_frag_guarda(&m, 0, buf, 100) < 0);                                  /* buffer pequeno */
    mc_set_estado(&m, 5, MC_ARCHIVADO, 0, 0); CK(mc_frag_sucio(&m, 0) && !mc_frag_sucio(&m, 1));        /* solo se ensucia su fragmento */
    mc_add(&m, MC_CELDA, 0, "otro", "", 5, 0, 0, 0, 0); CK(mc_frag_sucio(&m, 2));
    free(buf);

    /* 11. limites */
    struct mc s; CKE(mc_init(&s, 3), 0);
    CK(mc_add(&s, MC_CELDA, 0, "uno", "", 5, 0, 0, 0, 0) && mc_add(&s, MC_CELDA, 0, "dos", "", 5, 0, 0, 0, 0) && mc_add(&s, MC_CELDA, 0, "tres", "", 5, 0, 0, 0, 0));
    CKE(mc_add(&s, MC_CELDA, 0, "cuatro", "", 5, 0, 0, 0, 0), 0);             /* lleno */
    CKE(mc_add(&s, 9, 0, "x", "", 5, 0, 0, 0, 0), 0);                         /* tipo invalido */
    mc_destroy(&s);
    CK(mc_init(&s, 0) != 0 && mc_init(&s, MC_MAX_ITEMS + 1) != 0);
    char largo[3000]; memset(largo, 'a', sizeof largo); largo[2999] = 0;
    CKE(mc_init(&s, 8), 0);
    uint32_t cl2 = mc_add(&s, MC_CELDA, 0, largo, "", 5, 0, 0, 0, 0);
    CKE(mc_get(&s, cl2)->tlen, MC_TEXTO_MAX);                                 /* se recorta */
    char u8[2000]; for (int i = 0; i < 1999; i += 2) { u8[i] = (char)0xC3; u8[i + 1] = (char)0xB1; } u8[1998] = 0;       /* "ñ" repetida: 1998 bytes */
    cl2 = mc_add(&s, MC_CELDA, 0, u8, "", 5, 0, 0, 0, 0);
    CK(mc_get(&s, cl2)->tlen % 2 == 0 && mc_get(&s, cl2)->tlen <= MC_TEXTO_MAX);                                     /* no parte un caracter */
    CK(mc_add(&s, MC_CELDA, 0, "x", "", 0, 0, 0, 0, 0) && mc_get(&s, 3)->imp == 1);                                    /* importancia acotada */
    mc_destroy(&s);

    mc_destroy(&m); mc_destroy(&r);
    printf("test_memoria_277: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
