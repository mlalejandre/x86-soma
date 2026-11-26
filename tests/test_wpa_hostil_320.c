/* SOMA_320_WIFI_HOSTIL: lo que llega POR EL AIRE (src/core/wpa.c) con datos hostiles. Lo puede mandar cualquiera que este cerca,
 * sin estar en la red:
 *   1. los IE de las balizas y respuestas de sondeo (wpa_classify, wpa_parse_rsn; los usa iwl.c en x86 y wifi.c en la Pi);
 *   2. los resultados de escaneo del firmware de la Pi (wifi_escan_parse);
 *   3. las tramas EAPOL del saludo de 4 pasos y de la renovacion de grupo (wpa_sm_rx), mutadas tal cual (cabeceras y longitudes,
 *      antes del MIC) y mutadas con el MIC y el cifrado BIEN HECHOS: lo que haria un punto de acceso falso que conoce la
 *      contrasena (una wifi publica). Asi se llega a la lectura de las KDE (la clave de grupo).
 * Cada entrada en un bufer de su tamano exacto: ASan ve cualquier lectura de mas. Vectores: tests/wpa_vectors.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "../src/core/crypto.h"
#include "../src/core/wpa.h"
#include "wpa_vectors.h"

static uint64_t semilla = 320;
static uint32_t rnd(void) { semilla = semilla * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(semilla >> 33); }
static void hex(const char *h, uint8_t *o) { for (size_t i = 0; h[2 * i]; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); o[i] = (uint8_t)v; } }

/* copia de s (n bytes) mutada, en un bufer de su tamano exacto */
static uint8_t *mutada(const uint8_t *s, size_t n, size_t *on, int fuerte)
{
    size_t cap = n + 32, l = n;
    uint8_t *t = (uint8_t *)malloc(cap);
    memcpy(t, s, n);
    int cambios = 1 + (int)(rnd() % (fuerte ? 12u : 4u));
    for (int c = 0; c < cambios && l; ++c) {
        size_t i = rnd() % l; uint32_t k = rnd() % 5;
        if (k == 0) t[i] = (uint8_t)rnd();
        else if (k == 1) t[i] = (uint8_t)(rnd() & 1 ? 0xFF : 0x00);
        else if (k == 2) t[i] = (uint8_t)(t[i] + (rnd() & 1 ? 1 : -1));
        else if (k == 3 && l + 4 < cap) { memmove(t + i + 2, t + i, l - i); t[i] = (uint8_t)rnd(); t[i + 1] = (uint8_t)rnd(); l += 2; }
        else if (l > 1) { memmove(t + i, t + i + 1, l - i - 1); l--; }
    }
    if (rnd() % 5 == 0) l = rnd() % (l + 1);
    uint8_t *e = (uint8_t *)malloc(l ? l : 1);
    memcpy(e, t, l);
    free(t);
    *on = l;
    return e;
}

static int malos;

static void ies(void)
{
    uint8_t rsn[32]; size_t rl = wpa_build_rsn_ie(SUITE_CCMP, rsn);
    static const uint8_t wpa1[] = { 221, 22, 0x00, 0x50, 0xF2, 1, 1, 0, 0x00, 0x50, 0xF2, 2, 1, 0, 0x00, 0x50, 0xF2, 2, 1, 0, 0x00, 0x50, 0xF2, 2 };
    static const uint8_t ent[] = { 48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 1, 0, 0 };
    static const uint8_t mix[] = { 0, 9, 'S', 'O', 'M', 'A', '-', 'c', 'a', 's', 'a', 1, 4, 0x82, 0x84, 0x8b, 0x96, 3, 1, 6,
                                   48, 24, 1, 0, 0, 0x0f, 0xac, 4, 2, 0, 0, 0x0f, 0xac, 4, 0, 0x0f, 0xac, 2, 2, 0, 0, 0x0f, 0xac, 2, 0, 0x0f, 0xac, 8, 0x0c, 0,
                                   221, 7, 0x00, 0x50, 0xF2, 4, 0x10, 0x4a, 0x00 };
    const uint8_t *sem[4] = { rsn, wpa1, ent, mix }; size_t sl[4] = { rl, sizeof wpa1, sizeof ent, sizeof mix };
    for (int it = 0; it < 300000; ++it) {
        int k = (int)(rnd() % 4); size_t n; uint8_t *b = mutada(sem[k], sl[k], &n, 1);
        struct rsn_info o; memset(&o, 0, sizeof o);
        (void)wpa_parse_rsn(n >= 2 ? b + 2 : b, n >= 2 ? n - 2 : 0, &o);
        struct rsn_info r2; int sec = wpa_classify(b, n, (int)(rnd() & 1), &r2);
        if (sec < 0 || sec > 64) malos++;
        free(b);
    }
}

static void escaneo(void)
{
    for (int it = 0; it < 100000; ++it) {
        size_t n; uint8_t *b = mutada(ESC, sizeof ESC, &n, 1);
        struct wifi_bss l[8]; int c = 0;
        memset(l, 'Z', sizeof l);
        int r = wifi_escan_parse(b, n, l, &c, 8);
        if (r > 8 || c < 0 || c > 8) malos++;
        for (int i = 0; i < c && i < 8; ++i) if (l[i].ssid_len > 32 || !memchr(l[i].ssid, 0, sizeof l[i].ssid)) malos++;
        if (c > 0 && c <= 8) wifi_bss_sort(l, c);
        free(b);
    }
}

/* estado del saludo tras el mensaje 1 (PTK del vector) */
static void prepara(struct wpa_sm *s)
{
    uint8_t ie[32], out[512], aa[6], spa[6], sn[32]; size_t ol = 0;
    size_t il = wpa_build_rsn_ie(SUITE_CCMP, ie);
    hex("020000000001", aa); hex("dca632aabbcc", spa); for (int i = 0; i < 32; i++) sn[i] = (uint8_t)(100 + i);
    wpa_sm_init(s, PMK, aa, spa, sn, ie, il);
    if (wpa_sm_rx(s, M1, sizeof M1, out, &ol) != WPA_R_SEND || memcmp(s->ptk, PTK, 48)) { printf("el vector no prepara el estado\n"); exit(2); }
}

/* trama como 'base' (M3 o G1) con los datos de clave en claro 'kd' (n bytes): cifrados con la KEK y con el MIC bien calculado */
static size_t rehace(const uint8_t *base, const uint8_t *ptk, const uint8_t *kd, size_t n, uint8_t *f)
{
    memcpy(f, base, 99);
    size_t dl = n + 8;
    if (aes_key_wrap(ptk + 16, kd, n, f + 99)) return 0;
    f[97] = (uint8_t)(dl >> 8); f[98] = (uint8_t)dl;
    size_t flen = 99 + dl, body = flen - 4;
    f[2] = (uint8_t)(body >> 8); f[3] = (uint8_t)body;
    memset(f + 81, 0, 16);
    uint8_t mic[16]; wpa_mic(ptk, f, flen, mic);
    memcpy(f + 81, mic, 16);
    return flen;
}

static void eapol(void)
{
    struct wpa_sm s0; prepara(&s0);
    uint8_t out[512]; size_t ol;
    /* las KDE en claro de M3 y G1 (para mutarlas) */
    uint8_t kd3[512], kdg[512];
    size_t n3 = (size_t)(M3[97] << 8 | M3[98]) - 8, ng = (size_t)(G1[97] << 8 | G1[98]) - 8;
    if (aes_key_unwrap(PTK + 16, M3 + 99, n3, kd3) || aes_key_unwrap(PTK + 16, G1 + 99, ng, kdg)) { printf("no se descifran los vectores\n"); exit(2); }
    struct wpa_sm hecho = s0;
    if (wpa_sm_rx(&hecho, M3, sizeof M3, out, &ol) != WPA_R_DONE) { printf("el vector M3 no vale\n"); exit(2); }
    int listos = 0, rechazos = 0;
    for (int it = 0; it < 60000; ++it) {
        int tipo = (int)(rnd() % 4);
        if (tipo < 2) {                                                  /* mutada tal cual: cabeceras y longitudes */
            const uint8_t *b = tipo ? G1 : M3; size_t bn = tipo ? sizeof G1 : sizeof M3, n;
            uint8_t *m = mutada(b, bn, &n, 0);
            struct wpa_sm z = tipo ? hecho : s0;
            (void)wpa_sm_rx(&z, m, n, out, &ol);
            if (ol > sizeof out) malos++;
            free(m);
        } else {                                                         /* KDE mutadas, con cifrado y MIC correctos */
            const uint8_t *kb = tipo == 3 ? kdg : kd3; size_t kn = tipo == 3 ? ng : n3, n;
            uint8_t *m = mutada(kb, kn, &n, 1);
            size_t pn = n < 16 ? 16 : (n + 7) & ~(size_t)7;
            if (pn > 496) { free(m); continue; }
            uint8_t kd[512]; memset(kd, 0xDD, pn); memcpy(kd, m, n);
            if (pn > n) { kd[n] = 0xDD; for (size_t i = n + 1; i < pn; ++i) kd[i] = 0; }
            free(m);
            uint8_t f[1024]; size_t fl = rehace(tipo == 3 ? G1 : M3, PTK, kd, pn, f);
            if (!fl) continue;
            uint8_t *e = (uint8_t *)malloc(fl); memcpy(e, f, fl);
            struct wpa_sm z = tipo == 3 ? hecho : s0;
            int r = wpa_sm_rx(&z, e, fl, out, &ol);
            if (r == WPA_R_DONE) { listos++; if (z.gtk_len < 5 || z.gtk_len > 32 || z.gtk_idx > 3) malos++; }
            else if (r == WPA_E_KEYDATA) rechazos++;
            else if (r == WPA_E_MIC) malos++;                             /* el MIC lo hemos calculado bien: no puede fallar */
            free(e);
        }
    }
    printf("[wpa_hostil_320] tramas con MIC valido y KDE mutadas: %d aceptadas, %d rechazadas\n", listos, rechazos);
    CHECK(listos > 0 && rechazos > 0);                                   /* la prueba llega de verdad a leer las KDE */
}

int main(void)
{
    ies();
    escaneo();
    eapol();
    CHECK_EQ_INT(malos, 0);
    TEST_MAIN_END("wpa_hostil_320");
}
