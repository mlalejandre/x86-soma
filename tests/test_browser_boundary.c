/* SOMA 143A: comprueba las rutas compiladas y la politica fail-closed.
 * En host no arranca el kernel ni crea sockets; son funciones simuladas. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "web_browser_transport.h"
static int kernel_calls, isolated_calls, fail;
static const struct wn_req *last_req;
int wn_fetch(const struct wn_req *rq, struct wn_resp *rs)
{
    kernel_calls++; last_req = rq; rs->status = 210;
    return fail ? -71 : 0;
}
int wn_fetch_isolated(const struct wn_req *rq, struct wn_resp *rs)
{
    isolated_calls++; last_req = rq; rs->status = 220;
    return fail ? -73 : 0;
}
int main(void)
{
    struct wn_req rq = {0};
    struct wn_resp rs = {0};
    rq.url = "http://127.0.0.1/prueba";
    rq.method = "POST";
    rq.body = "abc";
    rq.body_len = 3;
    rq.max_body = 12345;
    for (int i = 0; i < 2; i++) {
        fail = i;
        int rc = wb_network_fetch(&rq, &rs);
        assert(last_req == &rq);
#if SOMA_BROWSER_NET_RING3
        assert(isolated_calls == i + 1 && kernel_calls == 0);
        assert(rs.status == 220);
        assert(rc == (fail ? -73 : 0));
#else
        assert(kernel_calls == i + 1 && isolated_calls == 0);
        assert(rs.status == 210);
        assert(rc == (fail ? -71 : 0));
#endif
    }
    assert(strcmp(rq.url, "http://127.0.0.1/prueba") == 0);
    puts(SOMA_BROWSER_NET_RING3 ? "143A: servicio aislado y fail-closed OK" : "143A: compatibilidad de la ruta original OK");
    return 0;
}
