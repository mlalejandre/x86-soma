/* PARCHE 142: la API de red del kernel (dns_resolve, tcp_*, timer_get_uptime_ms) para procesos de ring 3 con la capacidad de red,
 * encima de las syscalls SYS_NET_DNS / SYS_TCP_* (usermode.h). La usan el servicio netsvc y el navegador aislado.
 * Las esperas pasan por unet_idle(): por defecto duerme 1 ms; el navegador la sustituye para ceder a su otra corrutina.
 * Los datos van por un bufer de rebote propio (las syscalls solo aceptan memoria de la ventana de usuario, no el monton). */
#include <stdint.h>
#include <stddef.h>
#include "../arch/x86_64/usermode.h"

#define TCP_EOF (-1)
#define TCP_ERR (-2)

static inline long us3(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r), "+D"(a), "+S"(b), "+d"(c) : "0"(nr) : "rcx", "r11", "r8", "r9", "r10", "memory");
    return r;
}

uint64_t timer_get_uptime_ms(void) { return (uint64_t)us3(SYS_UPTIME_MS, 0, 0, 0); }
__attribute__((weak)) void unet_idle(void) { us3(SYS_SLEEP, 1, 0, 0); }
void net_client_lock(void) { }
void net_client_unlock(void) { }

static uint8_t bounce[16384];

int dns_resolve(const char *name, uint8_t *ip_out, uint32_t timeout_ms)
{
    (void)timeout_ms;
    static char nm[256]; static uint8_t ip[4];
    uint32_t n = 0; while (name[n] && n < 253) { nm[n] = name[n]; n++; }
    nm[n] = 0;
    if (n == 0) return 0;
    if (us3(SYS_NET_DNS, (long)nm, (long)n, (long)ip) != 0) return 0;
    for (int i = 0; i < 4; ++i) ip_out[i] = ip[i];
    return 1;
}

int tcp_connect(const uint8_t *ip, uint16_t port, uint32_t timeout_ms)
{
    long be = ((long)ip[0] << 24) | ((long)ip[1] << 16) | ((long)ip[2] << 8) | ip[3];
    long h = us3(SYS_TCP_OPEN, be, port, 0);
    if (h < 0) return -1;
    uint64_t t0 = timer_get_uptime_ms();
    for (;;) {
        long r = us3(SYS_TCP_POLL, h, 0, 0);
        if (r == 1) return (int)h;
        if (r < 0) return -1;
        if (timer_get_uptime_ms() - t0 >= timeout_ms) { us3(SYS_TCP_CLOSE, h, 0, 0); return -1; }
        unet_idle();
    }
}

int tcp_send(int h, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data; uint32_t done = 0;
    while (done < len) {
        uint32_t n = len - done < sizeof bounce ? len - done : (uint32_t)sizeof bounce;
        for (uint32_t i = 0; i < n; ++i) bounce[i] = p[done + i];
        long r = us3(SYS_TCP_SEND, h, (long)bounce, n);
        if (r < 0) return -1;
        done += (uint32_t)r;
    }
    return (int)len;
}

int tcp_recv(int h, void *buf, uint16_t max, uint32_t timeout_ms)
{
    if (max == 0) return 0;
    uint64_t t0 = timer_get_uptime_ms();
    for (;;) {
        uint32_t m = max < sizeof bounce ? max : (uint32_t)sizeof bounce;
        long r = us3(SYS_TCP_RECV, h, (long)bounce, m);
        if (r > 0) { uint8_t *o = (uint8_t *)buf; for (long i = 0; i < r; ++i) o[i] = bounce[i]; return (int)r; }
        if (r == -1) return TCP_EOF;
        if (r < 0) return TCP_ERR;
        if (timer_get_uptime_ms() - t0 >= timeout_ms) return 0;
        unet_idle();
    }
}

int tcp_close(int h) { us3(SYS_TCP_CLOSE, h, 0, 0); return 0; }
