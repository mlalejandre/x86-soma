/* SOMA_340_RED_HOSTIL: la camara USB con datos hostiles (src/core/uvc.c, en el nucleo). Una camara falsa (o un pendrive que se hace
 * pasar por camara) controla el descriptor de configuracion y cada paquete del flujo de video. test_uvc_178 prueba casos a mano;
 * aqui se mutan al azar:
 *   - el descriptor (longitudes, tipos, recuentos, cortes) y lo que se hace despues con lo leido: elegir formato y ajuste alterno;
 *   - los paquetes del flujo (cabeceras, banderas FID/EOF/ERR, tamanos) contra bufferes pequenos;
 *   - los JPEG a los que se les insertan las tablas Huffman (uvc_mjpeg_add_dht), con salidas de todos los tamanos.
 * Reutiliza el constructor de descriptores de test_uvc_178.c. Con ASan/UBSan. */
#define main test_uvc_178_main
#include "test_uvc_178.c"
#undef main

static uint64_t sem = 0xD1B54A32D192ED03ull;
static uint32_t az(void) { sem ^= sem << 13; sem ^= sem >> 7; sem ^= sem << 17; return (uint32_t)(sem >> 11); }
static long comprobaciones;
#define CK(c, m) do { comprobaciones++; if (!(c)) { printf("FALLO: %s (linea %d)\n", m, __LINE__); fails++; } } while (0)

static void descriptores(void)
{
    build();
    uint8_t base[2048]; uint32_t bl = cl; memcpy(base, cfg, cl);
    long leidos = 0, elegidos = 0;
    for (int it = 0; it < 200000; it++) {
        uint32_t len = bl;
        if (az() % 3 == 0) len = az() % (bl + 1);
        uint8_t *d = malloc(len ? len : 1);
        memcpy(d, base, len);
        int k = 1 + (int)(az() % 8);
        for (int i = 0; i < k && len; i++) {
            uint32_t o = az() % len;
            switch (az() % 4) {
            case 0: d[o] = (uint8_t)az(); break;
            case 1: d[o] = (uint8_t)(az() % 4); break;                 /* bLength 0..3: bucles que no avanzan */
            case 2: d[o] = 0xFF; break;                                /* recuentos y longitudes maximos */
            default: d[o] ^= (uint8_t)(1u << (az() % 8)); break;
            }
        }
        struct uvc_cfg c; memset(&c, 0xA5, sizeof c);
        if (uvc_parse(d, len, &c) == 0) {
            leidos++;
            CK(c.n_vs <= UVC_MAX_VS, "interfaces de streaming dentro del limite");
            for (int v = 0; v < c.n_vs && v < UVC_MAX_VS; v++) {
                CK(c.vs[v].n_fmt <= UVC_MAX_FMT && c.vs[v].n_alt <= UVC_MAX_ALT, "formatos y alternos dentro del limite");
                for (int f = 0; f < c.vs[v].n_fmt && f < UVC_MAX_FMT; f++) {
                    CK(c.vs[v].fmt[f].n_frames <= UVC_MAX_FRM, "tamanos dentro del limite");
                    for (int r = 0; r < c.vs[v].fmt[f].n_frames && r < UVC_MAX_FRM; r++)
                        CK(c.vs[v].fmt[f].frame[r].n_int <= UVC_MAX_INT, "intervalos dentro del limite");
                }
            }
            struct uvc_sel s;
            for (int raw = 0; raw < 2; raw++) if (uvc_choose(&c, 640, 480, 30, raw, &s) == 0) {
                elegidos++;
                CK(s.vs >= 0 && s.vs < c.n_vs && s.fmt >= 0 && s.fmt < c.vs[s.vs].n_fmt && s.frm >= 0 && s.frm < c.vs[s.vs].fmt[s.fmt].n_frames,
                   "la eleccion apunta dentro de lo leido");
                int alt = uvc_pick_alt(&c.vs[s.vs], az() % 4000);
                CK(alt < c.vs[s.vs].n_alt, "ajuste alterno dentro de lo leido");
            }
        }
        free(d);
    }
    printf("  descriptores: 200000 mutados (%ld leidos, %ld con formato elegido), ninguno lee fuera\n", leidos, elegidos);
}

static void flujo(void)
{
    static uint8_t b0[4096], b1[4096], pk[3200];
    long tramas = 0;
    for (int ronda = 0; ronda < 400; ronda++) {
        uint32_t cap = 16 + az() % 4000, expect = az() % 2 ? 0 : az() % 5000;
        struct uvc_asm a; uvc_asm_init(&a, b0, b1, cap, expect);
        for (int i = 0; i < 300; i++) {
            uint32_t n = az() % 6 == 0 ? az() % 4 : az() % sizeof pk;
            for (uint32_t j = 0; j < n; j++) pk[j] = (uint8_t)az();
            if (n >= 2 && az() % 2) {                                  /* cabecera plausible: 2..12 bytes, FID que alterna, EOF a veces */
                pk[0] = (uint8_t)(2 + az() % 11); pk[1] = (uint8_t)(0x80 | (i / 7 % 2) | (az() % 5 == 0 ? 2 : 0) | (az() % 40 == 0 ? 0x40 : 0));
                if (az() % 4 == 0 && n > pk[0] + 2u) { pk[pk[0]] = 0xFF; pk[pk[0] + 1] = 0xD8; }
                if (az() % 4 == 0 && n >= 2) { pk[n - 2] = 0xFF; pk[n - 1] = 0xD9; }
            }
            int r = uvc_asm_feed(&a, pk, n);
            CK(r == UVC_FRAME_NONE || r == UVC_FRAME_OK || r == UVC_FRAME_BAD, "resultado conocido");
            CK(a.len <= cap, "la trama en curso cabe en el bufer");
            if (r == UVC_FRAME_OK) {
                tramas++;
                CK(a.done && a.done_len <= cap && (a.done == b0 || a.done == b1), "la trama completa esta en uno de los dos bufferes");
                volatile uint8_t x = 0; for (uint32_t j = 0; j < a.done_len; j++) x ^= a.done[j]; (void)x;
            }
        }
    }
    printf("  flujo: 120000 paquetes al azar (%ld tramas completas), siempre dentro de los bufferes\n", tramas);
}

static void tablas_huffman(void)
{
    static uint8_t in[2048], out[4096];
    long reparados = 0;
    for (int it = 0; it < 100000; it++) {
        uint32_t n = az() % sizeof in;
        for (uint32_t j = 0; j < n; j++) in[j] = (uint8_t)az();
        if (n >= 2) { in[0] = 0xFF; in[1] = 0xD8; }
        for (uint32_t j = 2; j + 3 < n; j += 1 + az() % 60) {            /* marcadores con longitudes que pueden salirse */
            static const uint8_t mk[] = { 0xDB, 0xC0, 0xC4, 0xDA, 0xDD, 0xE0, 0xFE, 0xD9, 0x00, 0xFF };
            in[j] = 0xFF; in[j + 1] = mk[az() % sizeof mk];
            if (az() % 3 == 0) { in[j + 2] = (uint8_t)(az() % 3); in[j + 3] = (uint8_t)az(); }
        }
        uint32_t cap = az() % sizeof out;
        uint8_t *o = malloc(cap ? cap : 1);
        int32_t r = uvc_mjpeg_add_dht(in, n, o, cap);
        CK(r >= -1 && r <= (int32_t)cap, "longitud nueva dentro de la salida");
        reparados += r > 0;
        (void)uvc_mjpeg_valid(in, n);
        free(o);
    }
    printf("  tablas Huffman: 100000 JPEG hostiles (%ld reparados), la salida nunca se desborda\n", reparados);
}

int main(void)
{
    descriptores();
    flujo();
    tablas_huffman();
    printf("test_uvc_hostil_340: %ld comprobaciones, %d fallos\n", comprobaciones, fails);
    return fails ? 1 : 0;
}
