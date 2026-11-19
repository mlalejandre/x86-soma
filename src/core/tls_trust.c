/* SOMA_194: confianza del TLS del nucleo. Conecta x509.c (puro) con el sistema:
 *   - autoridades de fabrica: las raices de Mozilla que van en el nucleo (tls_roots.c);
 *   - autoridades del usuario: /etc/tls_confianza.pem (uno o varios certificados PEM; p. ej. el autofirmado de un servidor propio).
 *     Un agente no puede escribir en /etc, asi que no puede anadirse autoridades;
 *   - la fecha: el reloj de tiempo real. Si el reloj no esta puesto (ano < 2025) no se comprueban las fechas de los certificados;
 *   - soma.tlsverificar=off (o el modo seguro): cifrado sin verificar, como antes del parche 194. Solo para diagnostico. */
#include "tls_trust.h"
#include "tls13.h"
#include "x509.h"
#include "tls_roots.h"
#include "rtc.h"
#include "fs.h"
#ifdef __x86_64__
#include "bootfb.h"
#endif

static int g_user;

static uint64_t unix_now(void)
{
    struct rtc_time t; rtc_get_datetime(&t);
    if (t.year < 2025 || t.year > 2200 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.min > 59 || t.sec > 60) return 0;
    int64_t y = (int64_t)t.year - (t.month <= 2), era = y / 400, yoe = y - era * 400;
    int64_t doy = (153 * (t.month + (t.month > 2 ? -3 : 9)) + 2) / 5 + t.day - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint64_t)(era * 146097 + doe - 719468) * 86400u + (uint64_t)t.hour * 3600u + (uint64_t)t.min * 60u + t.sec;
}

void tls_trust_setup(void)
{
    static int inited; static uint32_t last_sum, last_len;
    static char pem[FS_DATA_MAX + 1];
    if (!inited) {
        inited = 1;
        x509_set_roots(tls_roots_blob, tls_roots_len);
        tls13_unix_time = unix_now;
#ifdef __x86_64__
        if (bootinfo_disabled("tlsverificar")) tls13_verify = 0;
#endif
    }
    int n = vfs_read("/etc/tls_confianza.pem", pem, sizeof pem - 1);
    uint32_t len = n > 0 ? (uint32_t)n : 0, sum = 2166136261u;
    for (uint32_t i = 0; i < len; i++) { sum ^= (uint8_t)pem[i]; sum *= 16777619u; }
    if (len == last_len && sum == last_sum) return;                    /* sin cambios */
    last_len = len; last_sum = sum;
    x509_clear_anchors();
    g_user = len ? x509_add_anchors_pem(pem, len) : 0;
}

void tls_trust_get_info(struct tls_trust_info *i)
{
    tls_trust_setup();
    i->verify = tls13_verify; i->roots = x509_root_count() - (uint32_t)g_user; i->user = g_user; i->now = unix_now();
}
