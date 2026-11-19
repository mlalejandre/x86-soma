/* PARCHE 138: pruebas de los servicios aislados (sbx_selftest). Se lanzan con el comando 'sbx test' o en QEMU con
 *   -fw_cfg name=opt/soma/selftest,string=sbx   (tools/sbx_check.py), que ademas sale de QEMU con el resultado. */
#include "sandbox.h"
#include "usermode.h"
#include "pmm.h"
#include "mem.h"
#include "thread.h"
#include "console.h"
#include "web_img.h"
#include "image.h"
#include "sbx_testimg.h"
#include "jpeg.h"
#include "web_inflate.h"
#include "pdf.h"
#include "pdf_hash.h"
int pdf_sbx_unpack_test(const uint8_t *src, uint32_t n);

uint64_t timer_get_uptime_ms(void);
extern uint32_t sbx_reserve_override;

#define EVIL _binary_sbxevil_elf_start, (size_t)(_binary_sbxevil_elf_end - _binary_sbxevil_elf_start)

static int bad;
static void ck(int ok, const char *what)
{
    kprint(ok ? "  OK    " : "  FALLO "); kprint(what); kprint("\n");
    if (!ok) bad++;
}
static void kdec64(int64_t v) { if (v < 0) { kprint("-"); v = -v; } kprint_dec((uint32_t)v); }

static int evil(uint32_t op, uint32_t heap, uint32_t ms, int64_t *code)
{
    static struct sbx_call c;                       /* 4 KiB+: fuera de la pila del hilo */
    sbx_prepare(&c, op, "x", 1, heap);
    int r = sbx_run(&c, EVIL, ms);
    *code = c.exit_code;
    sbx_release(&c);
    return r;
}

static size_t free_frames(void) { size_t f, u, t; pmm_get_stats(&f, &u, &t); return f; }

/* Marcos libres "netos": se descuenta lo que el monton del kernel haya mapeado (crece al alojar pixeles grandes y no devuelve
 * paginas: no es una fuga del servicio). */
static uint64_t heap_mapped(void) { struct kheap_counters k; kheap_get_counters(&k); return k.mapped_bytes / 4096u; }
static uint64_t g_mapped0;
static void leak_check(size_t f0, const char *what)
{
    thread_sleep(20);
    size_t f1 = free_frames();
    uint64_t grow = heap_mapped() - g_mapped0;
    kprint("  marcos libres antes/despues ("); kprint(what); kprint("): "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1);
    kprint(", crecimiento del monton del kernel "); kprint_dec((uint32_t)grow); kprint("\n");
    ck(f1 + grow + 8 >= f0, "sin fuga de memoria fisica");
    ck(kheap_check() == 0, "monton del kernel integro");
}

static uint32_t fnv(const uint32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) for (int b = 0; b < 4; ++b) { h ^= (uint8_t)(p[i] >> (8 * b)); h *= 16777619u; }
    return h;
}

/* ---- concurrencia: mas hilos que ranuras de proceso (USER_MAX_PROCS) ---- */
static volatile int conc_ok, conc_bad, conc_done;
static void conc_thread(void *arg)
{
    uintptr_t id = (uintptr_t)arg;
    struct sbx_call *c = (struct sbx_call *)kmalloc(sizeof *c);
    uint8_t *buf = (uint8_t *)kmalloc(3000), *got = (uint8_t *)kmalloc(3000);
    for (int it = 0; it < 4; ++it) {
        if (!c || !buf || !got) { conc_bad++; break; }
        uint32_t n = 1000 + (uint32_t)(id * 97 + (uintptr_t)it * 31) % 2000;
        for (uint32_t i = 0; i < n; ++i) buf[i] = (uint8_t)(i * 7 + id + (uintptr_t)it);
        sbx_prepare(c, SBX_EVIL_ECHO, buf, n, 1u << 20);
        int r = sbx_run(c, EVIL, 5000), good = 0;
        if (r == 0 && c->out.data_len == n && sbx_copy_data(c, got, n) == 0) {
            good = 1; for (uint32_t i = 0; i < n; ++i) if (got[i] != buf[n - 1 - i]) { good = 0; break; }
        }
        sbx_release(c);
        if (good) conc_ok++; else conc_bad++;
    }
    if (c) kfree(c);
    if (buf) kfree(buf);
    if (got) kfree(got);
    conc_done++;
}

int sbx_selftest(void)
{
    bad = 0;
    int64_t code;
    kprint("SBX: servicios aislados en ring 3 (parche 138)\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();

    kprint(" [ataques al mecanismo]\n");
    ck(evil(SBX_EVIL_SYSCALL, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_DENIED, "syscall prohibida (write/open) -> muere DENIED");
    ck(evil(SBX_EVIL_LOOP, 65536, 300, &code) == SBX_E_DIED && code == USER_EXIT_TIMEOUT, "bucle infinito -> plazo");
    ck(evil(SBX_EVIL_KREAD, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "lee memoria del kernel -> #PF");
    ck(evil(SBX_EVIL_WRITE_IN, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "escribe su entrada (solo lectura) -> #PF");
    ck(evil(SBX_EVIL_HEAP_PAST, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "escribe tras el final de su monton -> #PF");
    ck(evil(SBX_EVIL_EXEC_HEAP, 65536, 2000, &code) == SBX_E_DIED && code == USER_EXIT_FAULT_BASE - 14, "ejecuta codigo en su monton (NX) -> #PF");
    ck(evil(SBX_EVIL_BAD_PTR, 65536, 2000, &code) == SBX_E_REPLY, "respuesta con puntero fuera del monton -> rechazada");
    ck(evil(SBX_EVIL_BAD_LEN, 65536, 2000, &code) == SBX_E_REPLY, "respuesta con longitud enorme -> rechazada");
    ck(evil(SBX_EVIL_UNTOUCHED, 4u << 20, 2000, &code) == SBX_E_REPLY, "respuesta en paginas nunca escritas -> rechazada");
    ck(evil(SBX_EVIL_BAD_MAGIC, 65536, 2000, &code) == SBX_E_REPLY, "respuesta sin magic -> rechazada");
    {
        size_t a = free_frames();
        ck(evil(SBX_EVIL_HEAP_ALL, 8u << 20, 3000, &code) == 0, "toca todo su monton (8 MiB) -> OK, bajo demanda");
        size_t b = free_frames();
        ck(b + 8 >= a, "y la memoria vuelve al terminar");
        sbx_reserve_override = (uint32_t)(a > 600 ? a - 600 : 1);    /* solo deja crecer ~600 paginas */
        ck(evil(SBX_EVIL_HEAP_ALL, 8u << 20, 3000, &code) == SBX_E_HEAPOOM && code == USER_EXIT_NOMEM, "reserva del sistema -> muere NOMEM sin agotar la memoria");
        sbx_reserve_override = 0;
    }
    {
        static uint8_t buf[10000]; static uint8_t got[10000];
        static struct sbx_call c;
        for (int i = 0; i < 10000; ++i) buf[i] = (uint8_t)(i * 13 + 5);
        sbx_prepare(&c, SBX_EVIL_ECHO, buf, sizeof buf, 1u << 20);
        int r = sbx_run(&c, EVIL, 2000), good = r == 0 && c.out.data_len == sizeof buf && sbx_copy_data(&c, got, sizeof got) == 0;
        for (int i = 0; good && i < 10000; ++i) if (got[i] != buf[9999 - i]) good = 0;
        sbx_release(&c);
        ck(good, "ida y vuelta de 10000 bytes (entrada -> monton -> kernel) exacta");
    }
    leak_check(f0, "ataques");

    kprint(" [decodificadores de imagen aislados vs referencia de host]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    for (int i = 0; i < SBX_TESTIMG_N; ++i) {
        const struct sbx_testimg *t = &sbx_testimg[i];
        uint64_t t0 = timer_get_uptime_ms();
        int r, w = 0, h = 0; uint32_t hs = 0;
        if (t->op == 4) {                                   /* PARCHE 139: MJPEG del reproductor */
            struct jpeg_info ji; uint32_t *b = (uint32_t *)kmalloc((size_t)t->a0 * (size_t)t->a1 * 4u);
            r = b ? jpeg_decode_isolated(t->data, t->len, b, (uint32_t)t->a0, (uint32_t)t->a1, &ji) : -99;
            if (r == 0) { w = (int)ji.width; h = (int)ji.height; hs = fnv(b, (size_t)t->a0 * h); }
            if (b) kfree(b);
            if (r != 0 && r == t->exp_r) r = -1;            /* mismo codigo de error que la referencia */
        } else if (t->op == 3) {
            struct image im; r = wi_decode_bmp_raw(t->data, t->len, &im);
            if (r == 0) { w = (int)im.width; h = (int)im.height; hs = fnv(im.pixels, (size_t)w * h); kfree(im.pixels); }
        } else {
            struct wimg im; r = t->op == 2 ? wi_decode_fit(t->data, t->len, &im, t->a0, t->a1) : wi_decode(t->data, t->len, &im);
            if (r == 0) { w = im.w; h = im.h; hs = fnv(im.px, (size_t)w * h); wi_free(&im); }
        }
        uint64_t dt = timer_get_uptime_ms() - t0;
        int ok = (r == 0) == (t->exp_r == 0) && w == t->exp_w && h == t->exp_h && hs == t->exp_hash;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(t->name); kprint(": "); kprint_dec((uint32_t)w); kprint("x"); kprint_dec((uint32_t)h);
        kprint(" hash=0x"); kprint_hex32(hs); kprint(" en "); kprint_dec((uint32_t)dt); kprint(" ms\n");
        if (!ok) bad++;
    }
    sbx_svc_stop_all();                                     /* PARCHE 139: los servicios guardan monton entre peticiones */
    leak_check(f0, "imagenes");

    kprint(" [servicios persistentes (parche 139)]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    {
        static struct sbx_call c; static uint8_t buf[4000], got[4000];
        struct sbx_stats a0; sbx_get_stats(&a0);
        int good = 0; uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 200; ++it) {
            uint32_t n = 100 + (uint32_t)it * 17;
            for (uint32_t i = 0; i < n; ++i) buf[i] = (uint8_t)(i ^ (uint32_t)it);
            sbx_prepare(&c, SBX_EVIL_ECHO, buf, n, 0);
            int r = sbx_svc_call(SBX_SVC_TEST, &c, 2000), ok = r == 0 && c.out.data_len == n && sbx_copy_data(&c, got, n) == 0;
            for (uint32_t i = 0; ok && i < n; ++i) if (got[i] != buf[n - 1 - i]) ok = 0;
            sbx_release(&c);
            good += ok;
        }
        uint64_t tsvc = timer_get_uptime_ms() - t0;
        t0 = timer_get_uptime_ms();
        for (int it = 0; it < 50; ++it) { sbx_prepare(&c, SBX_EVIL_ECHO, buf, 100, 1u << 20); (void)sbx_run(&c, EVIL, 2000); sbx_release(&c); }
        uint64_t tone = timer_get_uptime_ms() - t0;
        struct sbx_stats a1; sbx_get_stats(&a1);
        kprint("  200 llamadas al servicio: "); kprint_dec((uint32_t)tsvc); kprint(" ms ("); kprint_dec((uint32_t)(tsvc * 1000 / 200)); kprint(" us/llamada); un proceso por llamada: ");
        kprint_dec((uint32_t)(tone * 1000 / 50)); kprint(" us/llamada; procesos de servicio creados: "); kprint_dec((uint32_t)(a1.svc_starts - a0.svc_starts)); kprint("\n");
        ck(good == 200, "200 peticiones correctas al mismo servicio");
        ck(a1.svc_starts - a0.svc_starts <= 2, "sin crear un proceso por peticion");

        sbx_prepare(&c, SBX_EVIL_PLANT, "x", 1, 0); int r1 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); sbx_release(&c);
        sbx_prepare(&c, SBX_EVIL_SNOOP, "x", 1, 0); int r2 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int32_t nz = c.out.val[0]; sbx_release(&c);
        kprint("  restos de la peticion anterior visibles en el monton: "); kprint_dec((uint32_t)nz); kprint(" bytes\n");
        ck(r1 == 0 && r2 == 0 && nz == 0, "una peticion no ve datos de la anterior (monton a cero entre peticiones)");

        sbx_prepare(&c, SBX_EVIL_KREAD, "x", 1, 0); int r3 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int64_t e3 = c.exit_code; sbx_release(&c);
        ck(r3 == SBX_E_DIED && e3 == USER_EXIT_FAULT_BASE - 14, "el servicio muere por #PF a mitad de peticion -> error");
        sbx_prepare(&c, SBX_EVIL_STATIC, "x", 1, 0); int r4 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int32_t v4 = c.out.val[0]; sbx_release(&c);
        ck(r4 == 0 && v4 == 1, "la siguiente peticion la atiende un proceso NUEVO (estado global limpio)");
        sbx_prepare(&c, SBX_EVIL_LOOP, "x", 1, 0); int r5 = sbx_svc_call(SBX_SVC_TEST, &c, 300); int64_t e5 = c.exit_code; sbx_release(&c);
        ck(r5 == SBX_E_DIED && e5 == USER_EXIT_TIMEOUT, "bucle infinito en el servicio -> plazo de la peticion");
        sbx_prepare(&c, SBX_EVIL_SYSCALL, "x", 1, 0); int r6 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); int64_t e6 = c.exit_code; sbx_release(&c);
        ck(r6 == SBX_E_DIED && e6 == USER_EXIT_DENIED, "syscall prohibida en el servicio -> DENIED");
        sbx_prepare(&c, SBX_EVIL_ECHO, buf, 10, 0); int r7 = sbx_svc_call(SBX_SVC_TEST, &c, 2000); sbx_release(&c);
        ck(r7 == 0, "y despues sigue atendiendo");
        int same = 1;
        for (int k = 1; k <= 5 && same; ++k) { sbx_prepare(&c, SBX_EVIL_STATIC, "x", 1, 0); (void)sbx_svc_call(SBX_SVC_TEST, &c, 2000); if (c.out.val[0] < k) same = 0; sbx_release(&c); }
        ck(same, "entre peticiones correctas el proceso se reutiliza");
    }
    kprint(" [inflate aislado (zdec) vs referencia de host]\n");
    for (int i = 0; i < SBX_TESTZ_N; ++i) {
        const struct sbx_testz *z = &sbx_testz[i];
        struct wb o; o.p = 0; o.len = o.cap = 0; o.err = 0;
        uint64_t t0 = timer_get_uptime_ms();
        int r = wz_inflate(z->data, z->len, z->fmt, &o, z->max_out);
        uint32_t hs = 2166136261u; for (uint32_t k = 0; k < o.len; ++k) { hs ^= (uint8_t)o.p[k]; hs *= 16777619u; }
        int ok = (r < 0) == (z->exp_r < 0) && o.len == z->exp_len && hs == z->exp_hash;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(z->name); kprint(": "); kprint_dec(o.len); kprint(" bytes hash=0x"); kprint_hex32(hs);
        kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        if (!ok) bad++;
        if (o.p) kfree(o.p);
    }
    kprint(" [analizador PDF aislado (pdfsvc) vs referencia de host]\n");
    for (int i = 0; i < SBX_TESTPDF_N; ++i) {
        const struct sbx_testpdf *t = &sbx_testpdf[i];
        char err[128]; uint32_t h = 2166136261u; int np = 0, ok = 0;
        uint64_t t0 = timer_get_uptime_ms();
        struct pdf *p = pdf_open(t->data, t->len, err, sizeof err);
        if (p) {
            ok = 1; np = pdf_page_count(p);
            char title[256]; int tl = pdf_title(p, title, sizeof title); h = pdfh_i(h, np); h = pdfh_mix(h, title, tl > 0 ? (uint32_t)tl : 0);
            for (int k = 0; k < np; ++k) {
                int w = 0, hh = 0; pdf_page_size(p, k, &w, &hh); h = pdfh_i(h, w); h = pdfh_i(h, hh);
                struct pdf_page pg; int r = pdf_render_page(p, k, 256, &pg); h = pdfh_i(h, r);
                if (r == 0) { h = pdfh_i(h, pdf_dl_hash(&pg)); pdf_page_free(&pg); }
            }
            pdf_close(p);
        }
        int good = ok == t->exp_open && np == t->exp_pages && h == t->exp_hash;
        kprint(good ? "  OK    " : "  FALLO "); kprint(t->name); kprint(ok ? ": " : ": rechazado ("); if (!ok) { kprint(err); kprint(") "); }
        kprint_dec((uint32_t)np); kprint(" paginas, huella 0x"); kprint_hex32(h); kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        if (!good) bad++;
    }
    {   /* listas de dibujo manipuladas: lo que devolveria un analizador comprometido */
        static uint8_t b[4096];
        struct sbx_pdf_hdr *hd = (struct sbx_pdf_hdr *)b; struct sbx_pdf_op *o = (struct sbx_pdf_op *)(b + sizeof *hd);
        #define RESET() do { for (int _i = 0; _i < 4096; ++_i) b[_i] = 0; hd->magic = SBX_PDFL_MAGIC; hd->w = 100; hd->h = 100; hd->nops = 1; hd->blob_len = 64; \
                             o->kind = PDF_OP_TEXT; o->off = 0; o->n = 8; o->font = 1; } while (0)
        uint32_t N = (uint32_t)(sizeof *hd + sizeof *o + 64);
        int rej = 0, tot = 0;
        #define MUST_REJECT(stmt) do { RESET(); stmt; tot++; if (pdf_sbx_unpack_test(b, N) != 0) rej++; else { kprint("  aceptada: " #stmt "\n"); } } while (0)
        RESET(); ck(pdf_sbx_unpack_test(b, N) == 0, "lista de dibujo valida -> aceptada");
        MUST_REJECT(hd->magic = 0);
        MUST_REJECT(hd->nops = 400001);
        MUST_REJECT(hd->nops = -1);
        MUST_REJECT(hd->blob_len = 65);
        MUST_REJECT(hd->w = 16385);
        MUST_REJECT(hd->h = -5);
        MUST_REJECT(o->kind = 9);
        MUST_REJECT(o->font = 65);
        MUST_REJECT(o->off = 60);
        MUST_REJECT(o->n = 0xFFFFFFF0u);
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 2, o->n = 4));
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 0, o->n = 9));
        MUST_REJECT((o->kind = PDF_OP_PATH, o->off = 0, o->n = 0x20000000u));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 4, o->ih = 4, o->w = 10, o->h = 0));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 65536, o->ih = 65536, o->w = 10, o->h = 10));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 4, o->ih = 5, o->w = 10, o->h = 10));
        MUST_REJECT((o->kind = PDF_OP_IMAGE, o->iw = 0, o->ih = 4, o->w = 10, o->h = 10));
        { RESET(); o->kind = PDF_OP_IMAGE; o->iw = 4; o->ih = 4; o->w = 10; o->h = 10; ck(pdf_sbx_unpack_test(b, N) == 0, "imagen 4x4 dentro del blob -> aceptada"); }
        kprint("  listas manipuladas rechazadas: "); kprint_dec((uint32_t)rej); kprint(" de "); kprint_dec((uint32_t)tot); kprint("\n");
        ck(rej == tot, "el kernel rechaza toda lista de dibujo incoherente");
        #undef RESET
        #undef MUST_REJECT
    }
    sbx_svc_stop_all();
    ck(sbx_svc_running(SBX_SVC_IMG) + sbx_svc_running(SBX_SVC_ZLIB) + sbx_svc_running(SBX_SVC_TEST) + sbx_svc_running(SBX_SVC_PDF) == 0, "todos los servicios detenidos");
    leak_check(f0, "servicios");

    kprint(" [fuzzing: imagenes mutadas a traves del servicio]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    {
        uint32_t seed = 0x1234567u, nok = 0, nfail = 0;
        uint64_t t0 = timer_get_uptime_ms();
        for (int it = 0; it < 240; ++it) {
            const struct sbx_testimg *t = &sbx_testimg[it % SBX_TESTIMG_N];
            if (t->len > 20000) continue;
            uint8_t *d = (uint8_t *)kmalloc(t->len);
            if (!d) continue;
            for (uint32_t k = 0; k < t->len; ++k) d[k] = t->data[k];
            int nm = 1 + (int)(seed % 8);
            for (int m = 0; m < nm; ++m) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; d[seed % t->len] ^= (uint8_t)(seed >> 8); }
            struct wimg im;
            if (wi_decode(d, t->len, &im) == 0) { nok++; wi_free(&im); } else nfail++;
            kfree(d);
        }
        kprint("  240 entradas mutadas: "); kprint_dec(nok); kprint(" decodificadas, "); kprint_dec(nfail); kprint(" rechazadas, en ");
        kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(nok + nfail > 200, "el kernel sobrevive a todas");
    }
    {   /* PARCHE 141: PDF mutados: abrir y dibujar todas sus paginas a traves de pdfsvc */
        const struct sbx_testpdf *t = &sbx_testpdf[2];
        uint32_t seed = 0xBADC0DEu, opened = 0, pages = 0, rejected = 0;
        uint64_t t0 = timer_get_uptime_ms();
        uint8_t *d = (uint8_t *)kmalloc(t->len);
        for (int it = 0; d && it < 120; ++it) {
            for (uint32_t k = 0; k < t->len; ++k) d[k] = t->data[k];
            int nm = 1 + (int)(seed % 6);
            for (int m = 0; m < nm; ++m) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; d[seed % t->len] ^= (uint8_t)(seed >> 8); }
            char err[64]; struct pdf *p = pdf_open(d, t->len, err, sizeof err);
            if (!p) { rejected++; continue; }
            opened++;
            for (int k = 0; k < pdf_page_count(p) && k < 8; ++k) { struct pdf_page pg; if (pdf_render_page(p, k, 256, &pg) == 0) { pages++; pdf_page_free(&pg); } }
            pdf_close(p);
        }
        if (d) kfree(d);
        kprint("  120 PDF mutados: "); kprint_dec(opened); kprint(" abiertos ("); kprint_dec(pages); kprint(" paginas dibujadas), "); kprint_dec(rejected);
        kprint(" rechazados, en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
        ck(opened + rejected == 120, "el kernel sobrevive a todos los PDF mutados");
    }
    sbx_svc_stop_all();
    leak_check(f0, "fuzzing");

    kprint(" [concurrencia: 12 hilos x 4 llamadas, 8 ranuras de proceso]\n");
    f0 = free_frames(); g_mapped0 = heap_mapped();
    conc_ok = conc_bad = conc_done = 0;
    for (uintptr_t i = 0; i < 12; ++i) thread_create("sbxc", conc_thread, (void *)i);
    for (int w = 0; w < 3000 && conc_done < 12; ++w) thread_sleep(10);
    kprint("  correctas "); kprint_dec((uint32_t)conc_ok); kprint(", erroneas "); kprint_dec((uint32_t)conc_bad); kprint("\n");
    ck(conc_done == 12 && conc_ok == 48 && conc_bad == 0, "48 llamadas concurrentes correctas");
    leak_check(f0, "concurrencia");

    struct sbx_stats s; sbx_get_stats(&s);
    kprint("  estadisticas: "); kprint_dec((uint32_t)s.runs); kprint(" llamadas, "); kprint_dec((uint32_t)s.ok); kprint(" bien, ");
    kprint_dec((uint32_t)s.died); kprint(" murieron, "); kprint_dec((uint32_t)s.bad_reply); kprint(" respuestas rechazadas, ");
    kprint_dec((uint32_t)s.oom); kprint(" sin memoria; maximo "); kprint_dec((uint32_t)s.max_ms); kprint(" ms\n");
    kprint(bad ? "SBX: FALLO (" : "SBX: OK\n"); if (bad) { kdec64(bad); kprint(" comprobaciones)\n"); }
    return bad;
}

/* ---- prueba de extremo a extremo: el NAVEGADOR real carga por red una pagina con las imagenes de prueba (tools/sbx_check.py --web
 * la sirve desde el host); cada imagen debe llegar decodificada por el servicio aislado e identica a la referencia de host. ---- */
#include "web_page.h"
static const char *ext_of(const char *name)
{
    if (name[0] == 'p') return ".png";
    if (name[0] == 'g') return ".gif";
    if (name[0] == 'j') return ".jpg";
    if (name[0] == 'b' && name[1] == 'm') return ".bmp";
    return ".bin";
}
int sbx_webtest(const char *url)
{
    bad = 0;
    kprint("SBXWEB: navegador -> red -> servicio aislado imgdec ("); kprint(url); kprint(")\n");
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    struct wpage *p = wp_new();
    if (!p) { ck(0, "wp_new"); return bad; }
    wp_set_viewport(p, 1024, 768);
    wp_navigate(p, url, 0);
    uint64_t t0 = timer_get_uptime_ms(), idle_since = 0;
    while (timer_get_uptime_ms() - t0 < 60000) {
        wp_tick(p, timer_get_uptime_ms());
        if (p->doc && p->img_pending == 0 && p->img_ok + p->img_fail > 0) { if (!idle_since) idle_since = timer_get_uptime_ms(); else if (timer_get_uptime_ms() - idle_since > 300) break; }
        thread_sleep(5);
    }
    sbx_get_stats(&s1);
    kprint("  imagenes: "); kprint_dec((uint32_t)p->img_ok); kprint(" bien, "); kprint_dec((uint32_t)p->img_fail); kprint(" fallan; llamadas al servicio aislado: ");
    kprint_dec((uint32_t)(s1.runs - s0.runs)); kprint(" en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms\n");
    int expected_ok = 0;
    for (int i = 0; i < SBX_TESTIMG_N; ++i) {
        const struct sbx_testimg *t = &sbx_testimg[i];
        if (t->op != 1) continue;
        char src[48]; int k = 0;
        for (const char *q = t->name; *q && k < 40; ++q) src[k++] = *q;
        for (const char *q = ext_of(t->name); *q; ++q) src[k++] = *q;
        src[k] = 0;
        const uint32_t *px; int w = 0, h = 0;
        int got = wp_img_get(p, src, &px, &w, &h, 0, 0);
        uint32_t hs = got ? fnv(px, (size_t)w * h) : 0;
        int ok = t->exp_r == 0 ? (got && w == t->exp_w && h == t->exp_h && hs == t->exp_hash) : !got;
        if (t->exp_r == 0) expected_ok++;
        kprint(ok ? "  OK    " : "  FALLO "); kprint(src); kprint(got ? ": " : ": no decodificada (esperado: "); 
        if (got) { kprint_dec((uint32_t)w); kprint("x"); kprint_dec((uint32_t)h); kprint(" hash=0x"); kprint_hex32(hs); kprint("\n"); }
        else kprint(t->exp_r == 0 ? "decodificada)\n" : "rechazada)\n");
        if (!ok) bad++;
    }
    ck(p->img_ok == expected_ok, "todas las imagenes validas llegaron a la pagina");
    ck(s1.runs - s0.runs >= (uint64_t)expected_ok, "y pasaron por el servicio aislado");
    ck(s1.died == s0.died, "ningun proceso de decodificacion murio");
    ck(s1.svc_calls - s0.svc_calls >= (uint64_t)expected_ok + 1, "la pagina (gzip) se inflo en el servicio zdec y las imagenes en imgdec");
    wp_free(p);
    kprint(bad ? "SBXWEB: FALLO\n" : "SBXWEB: OK\n");
    return bad;
}

/* ---- PARCHE 140: el kernel lee las fuentes COMPRIMIDAS del disco (SRCFS v02); la descompresion ocurre en el servicio zdec ---- */
#include "srcfs.h"
static int has(const char *h, const char *n) { for (; *h; ++h) { const char *a = h, *b = n; while (*a && *b && *a == *b) { ++a; ++b; } if (!*b) return 1; } return 0; }
int sbx_srcfstest(void)
{
    bad = 0;
    kprint("SBXSRC: fuentes comprimidas en disco -> servicio aislado zdec\n");
    static char out[4096];
    struct sbx_stats s0, s1; sbx_get_stats(&s0);
    int n = srcfs_ls_filter(0, out, sizeof out);
    kprint("  fuentes en el disco: "); kprint_dec((uint32_t)n); kprint("\n");
    ck(n > 300, "indice SRCFS v02 leido");
    int r = srcfs_cat("core/mem.c", 0, out, sizeof out);
    ck(r > 0 && has(out, "#include") && has(out, "siguiente OFFSET"), "src_cat core/mem.c (primera pagina)");
    int r2 = srcfs_cat("core/mem.c", (uint32_t)r, out, sizeof out);
    ck(r2 > 0, "src_cat segunda pagina (de la cache)");
    r = srcfs_grep("fs/srcfs.c", "SRCFSv02", out, sizeof out);
    ck(r > 0 && has(out, "SRCFSv02"), "src_grep encuentra texto en un archivo comprimido");
    r = srcfs_cat("fonts/vfont_data.h", 1000000, out, sizeof out);
    ck(r > 0, "tabla de datos grande (1,7 MB) tambien cabe y se lee por el final");
    sbx_get_stats(&s1);
    ck(s1.svc_calls - s0.svc_calls >= 3, "y se descomprimio en el servicio aislado");
    kprint(bad ? "SBXSRC: FALLO\n" : "SBXSRC: OK\n");
    return bad;
}

/* ---- PARCHE 142: cliente HTTP aislado (netsvc) y capacidad de red ---- */
#include "web_net.h"
#include "tcp.h"
#include "browser.h"
#include "fb.h"
static void netpat_text(char *o, uint32_t *n)       /* lo mismo que NET_TEXT de tools/sbx_check.py */
{
    uint32_t k = 0;
    for (int i = 0; i < 500; ++i) {
        const char *pre = "SOMA netsvc linea "; while (*pre) o[k++] = *pre++;
        o[k++] = (char)('0' + i / 1000); o[k++] = (char)('0' + i / 100 % 10); o[k++] = (char)('0' + i / 10 % 10); o[k++] = (char)('0' + i % 10); o[k++] = '\n';
    }
    *n = k;
}
static int fetch_eq(const char *base, const char *path, const char *want, uint32_t wn, int want_status, const char *what)
{
    char url[256]; uint32_t k = 0; for (const char *q = base; *q && k < 200; ++q) url[k++] = *q; for (const char *q = path; *q && k < 250; ++q) url[k++] = *q; url[k] = 0;
    struct wn_req rq; uint8_t *z = (uint8_t *)&rq; for (uint32_t i = 0; i < sizeof rq; ++i) z[i] = 0;
    rq.url = url; rq.timeout_ms = 8000; rq.max_body = 1u << 20; rq.want_binary = 1;
    struct wn_resp rs; uint64_t t0 = timer_get_uptime_ms();
    int r = wn_fetch_isolated(&rq, &rs);
    int ok = r == 0 && rs.status == want_status;
    if (ok && want) { ok = rs.len == wn; for (uint32_t i = 0; ok && i < wn; ++i) if (rs.body[i] != want[i]) ok = 0; }
    kprint(ok ? "  OK    " : "  FALLO "); kprint(what); kprint(": HTTP "); kprint_dec((uint32_t)rs.status); kprint(", "); kprint_dec(rs.len);
    kprint(" bytes en "); kprint_dec((uint32_t)(timer_get_uptime_ms() - t0)); kprint(" ms"); if (rs.err[0]) { kprint(" ("); kprint(rs.err); kprint(")"); } kprint("\n");
    if (!ok) bad++;
    wn_free_isolated(&rs);
    return ok;
}
int sbx_nettest(const char *base)
{
    bad = 0;
    kprint("SBXNET: cliente HTTP aislado (netsvc) y capacidad de red\n");
    size_t f0 = free_frames(); g_mapped0 = heap_mapped();
    static char text[16000]; uint32_t tn; netpat_text(text, &tn);
    fetch_eq(base, "/n/text", text, tn, 200, "texto plano");
    fetch_eq(base, "/n/gz", text, tn, 200, "gzip (inflado dentro de netsvc)");
    fetch_eq(base, "/n/chunk", text, tn, 200, "chunked");
    fetch_eq(base, "/n/redir", text, tn, 200, "redireccion 302");
    fetch_eq(base, "/n/nada", 0, 0, 404, "404");
    {
        static char bin[300000]; for (uint32_t i = 0; i < sizeof bin; ++i) bin[i] = (char)((i * 7 + 3) % 251);
        fetch_eq(base, "/n/bin", bin, sizeof bin, 200, "binario de 300 KB");
    }
    /* capacidad */
    uint32_t ip_be = (10u << 24) | (0u << 16) | (2u << 8) | 2u; int port = 0;
    { const char *q = base; while (*q && !(q[0] == ':' && q[1] >= '0' && q[1] <= '9')) q++; if (*q) { q++; while (*q >= '0' && *q <= '9') port = port * 10 + (*q++ - '0'); } }
    static struct sbx_call c; int64_t code;
    sbx_prepare(&c, SBX_EVIL_NET_NOCAP, "x", 1, 65536); int r = sbx_run(&c, EVIL, 2000); code = c.exit_code; sbx_release(&c);
    ck(r == SBX_E_DIED && code == USER_EXIT_DENIED, "sin la capacidad de red, abrir un socket mata el proceso (DENIED)");
    const uint32_t caps = USER_SYS_NET_MASK | (1u << SYS_SLEEP) | (1u << SYS_UPTIME_MS);
    uint8_t ip[4] = { 10, 0, 2, 2 };
    int kh = tcp_connect(ip, (uint16_t)port, 3000);                    /* conexion del KERNEL */
    ck(kh >= 0, "el kernel abre su propia conexion");
    sbx_prepare(&c, SBX_EVIL_NET_STEAL, "x", 1, 65536); c.in.arg[0] = kh; r = sbx_run_caps(&c, EVIL, 4000, caps); int32_t stolen = c.out.val[0]; sbx_release(&c);
    kprint("  operaciones sobre sockets ajenos no rechazadas: "); kprint_dec((uint32_t)stolen); kprint(" de 37\n");
    ck(r == 0 && stolen == 0, "un proceso con red no puede usar ni cerrar sockets ajenos (EBADF)");
    ck(kh >= 0 && tcp_owner(kh) != 0, "y la conexion del kernel sigue viva");
    if (kh >= 0) tcp_close(kh);
    int owned0 = 0; for (int h = 0; h < 8; ++h) if (tcp_owner(h)) owned0++;     /* (la del kernel puede seguir cerrandose: FIN_WAIT) */
    sbx_prepare(&c, SBX_EVIL_NET_FLOOD, "x", 1, 65536); c.in.arg[0] = (int32_t)ip_be; c.in.arg[1] = port; r = sbx_run_caps(&c, EVIL, 6000, caps);
    int32_t opened = c.out.val[0], fifth = c.out.val[1]; sbx_release(&c);
    kprint("  sockets abiertos de 6 pedidos: "); kprint_dec((uint32_t)opened); kprint("\n");
    ck(r == 0 && opened == USER_MAX_SOCKETS && fifth == -16, "como mucho 4 sockets por proceso (el 5o: EBUSY)");
    thread_sleep(20);
    { int left = 0; for (int h = 0; h < 8; ++h) if (tcp_owner(h)) left++;
      kprint("  sockets con dueno antes/despues del proceso: "); kprint_dec((uint32_t)owned0); kprint("/"); kprint_dec((uint32_t)left); kprint("\n");
      ck(left <= owned0, "al morir el proceso sus 4 sockets se cierran"); }
    sbx_prepare(&c, SBX_EVIL_NET_DNS, "x", 1, 65536); r = sbx_run_caps(&c, EVIL, 4000, caps); int32_t nr = c.out.val[0]; sbx_release(&c);
    ck(r == 0 && nr == 0, "nombres DNS maliciosos rechazados (EINVAL)");
    sbx_svc_stop_all();
    leak_check(f0, "red");
    kprint(bad ? "SBXNET: FALLO\n" : "SBXNET: OK\n");
    return bad;
}

/* ---- PARCHE 142: HUELLA del navegador: se dibuja la ventana entera (1024x700) en un bufer con una pagina fija (ref.html) ya cargada.
 * El parche 143 lleva el navegador a ring 3 y exige la MISMA huella: mismo resultado, otro anillo. ---- */
#define WREF_W 1024
#define WREF_H 700
int sbx_webref(const char *url)
{
    bad = 0;
    kprint("SBXWREF: huella del navegador ("); kprint(url); kprint(")\n");
    uint32_t *buf = (uint32_t *)kmalloc((size_t)WREF_W * WREF_H * 4u);
    if (!buf) { ck(0, "memoria"); return bad; }
    browser_init();
    browser_load_url(url);
    uint64_t t0 = timer_get_uptime_ms(), idle = 0;
    uint32_t h1 = 0, h2 = 0;
    while (timer_get_uptime_ms() - t0 < 30000) {
        fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end();
        if (!browser_is_loading()) { if (!idle) idle = timer_get_uptime_ms(); else if (timer_get_uptime_ms() - idle > 1500) break; } else idle = 0;
        thread_sleep(40);
    }
    fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end(); h1 = fnv(buf, (size_t)WREF_W * WREF_H);
    thread_sleep(700);
    fb_target_begin(buf, WREF_W, WREF_H); browser_render_window(0, 0, WREF_W, WREF_H, 1); fb_target_end(); h2 = fnv(buf, (size_t)WREF_W * WREF_H);
    kprint("  huella 0x"); kprint_hex32(h1); kprint(" / repetida 0x"); kprint_hex32(h2); kprint(" (cargada en "); kprint_dec((uint32_t)(idle - t0)); kprint(" ms)\n");
    ck(h1 == h2, "la huella es estable (pagina quieta)");
    { uint32_t c = buf[(size_t)200 * WREF_W + 30]; kprint("  pixel (30,200) = 0x"); kprint_hex32(c); kprint("\n"); }
    kfree(buf);
    kprint(bad ? "SBXWREF: FALLO\n" : "SBXWREF: OK\n");
    return bad;
}
