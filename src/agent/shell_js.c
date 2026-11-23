/* SOMA_274_JS: la orden "js": JavaScript para aplicaciones y utilidades (el motor del navegador sin pagina, como proceso de ring 3).
 *   js archivo.js [args]   ejecuta /workspace/archivo.js (consola: console.log, print, input(); archivos: fs.read/fs.write en /workspace;
 *                          ventana: win.create/win.rect/win.text...; temporizadores y promesas: setTimeout, async/await)
 *   js -c "codigo"         una linea
 * Se incluye en shell.c (una sola unidad de traduccion), despues de shell_procesos.c (que aporta g_python_idx y el cerrojo del codigo inyectado). */
#if defined(__x86_64__)
#include "vmm.h"
#include "pmm.h"
extern const uint8_t _binary_jsrun_elf_start[], _binary_jsrun_elf_end[];
#define JS_HEAP_MIB 160u           /* debe coincidir con JS_HEAP_MIB de src/user/jsrun.c */

/* lanza jsrun con su region de memoria bajo demanda y espera. out != 0: salida y entrada propias (programador). < -1000000: no se pudo lanzar */
static int64_t js_proc(uint32_t ms, const char *args, char *out, uint32_t cap, const char *input)
{
    struct uregion r;
    r.va = USER_HEAP_VA; r.npages = JS_HEAP_MIB << 8; r.used = 0;
    r.flags = VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX;
    r.demand = 1;
    r.frames = (uint64_t *)kmalloc((size_t)r.npages * sizeof(uint64_t));
    if (!r.frames) return -1000001;
    for (uint32_t k = 0; k < r.npages; ++k) r.frames[k] = 0;
    int err = 0;
    int idx = usermode_exec_region("js", _binary_jsrun_elf_start, (size_t)(_binary_jsrun_elf_end - _binary_jsrun_elf_start), ms, args,
                                   USER_SYS_PYTHON, &r, &err);
    int64_t code = -1000002;
    if (idx >= 0) {
        int guest = 0;
        if (out) usermode_redirect(idx, out, cap, input ? input : "");
        else guest = console_capture_active() && console_capture_guest(usermode_proc_tid(idx));
        g_python_stopped = 0;
        g_python_idx = idx;                                    /* Esc en el terminal lo para, como a python */
        code = usermode_wait(idx);
        g_python_idx = -1;
        if (guest) (void)console_capture_guest(0);
        if (usermode_python_window_take()) gui_soma_win_close();
    }
    for (uint32_t k = 0; k < r.npages; ++k) if (r.frames[k]) pmm_free_frame((uintptr_t)r.frames[k]);
    kfree(r.frames);
    return code;
}
#endif

static int cmd_js(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
#if defined(__x86_64__)
    while (args && *args == ' ') args++;
    if (!args || !*args) {
        soma_str_put(out_buf, max_out, &pos, "JavaScript de SOMA (el motor del navegador como lenguaje de aplicaciones). Uso: js archivo.js [args] | js -c \"codigo\". "
                                              "Funciones: console.log/print, input(\"?\"), fs.read/fs.write/fs.exists (/workspace), sleep(ms), exit(n), "
                                              "ventana: win.create(\"t\",ancho,alto), win.rect/text/line/circle/pixel/clear/present/mouse/key, setTimeout/setInterval, "
                                              "async/await, clases, modulos. Rutas relativas: /workspace. Web: html ayuda");
        return 1;
    }
    char a[USER_ARGS_MAX]; uint32_t n = 0;
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || !args[2])) {
        const char *c = args + 2; while (*c == ' ') c++;
        uint32_t cl = 0; while (c[cl]) cl++;
        while (cl && c[cl - 1] == ' ') cl--;
        if (cl >= 1 && (c[0] == '"' || c[0] == '\'')) {          /* quita las comillas de fuera; el terminal a veces ya se comio la de cierre */
            if (cl >= 2 && c[cl - 1] == c[0]) { c++; cl -= 2; } else { c++; cl--; }
        }
        if (!cl) { soma_str_put(out_buf, max_out, &pos, "uso: js -c \"codigo\""); return 1; }
        if (cl + 4 > sizeof a) { soma_str_put(out_buf, max_out, &pos, "js -c: el codigo no cabe; guardalo en /workspace/algo.js y usa: js algo.js"); return 1; }
        a[n++] = '-'; a[n++] = 'c'; a[n++] = ' ';
        for (uint32_t i = 0; i < cl; ++i) a[n++] = c[i];
    } else {
        while (args[n]) { if (n + 1 >= sizeof a) { soma_str_put(out_buf, max_out, &pos, "js: demasiados argumentos"); return 1; } a[n] = args[n]; n++; }
        while (n && a[n - 1] == ' ') n--;
    }
    a[n] = 0;
    int64_t code = js_proc(10u * 60u * 1000u, a, 0, 0, 0);
    if (code < -1000000) { soma_str_put(out_buf, max_out, &pos, "js: no se pudo lanzar el interprete (procesos o memoria)"); return 1; }
    if (code < 0 && g_python_stopped) kprint("js: detenido\n");
    else if (code < 0) { kprint("js: el proceso termino a la fuerza ("); kprint(cc_motivo(code)); kprint(")\n"); }
    else if (code != 0) { kprint("js: codigo de salida "); kprint_dec((uint32_t)code); kprint("\n"); }
#else
    (void)args;
    soma_str_put(out_buf, max_out, &pos, "js: de momento solo en x86_64 (en aarch64 aun no hay procesos de usuario)");
#endif
    return 1;
}

/* Para el programador: como shell_python_source, con JavaScript. check=1 solo analiza la sintaxis; check=0 ejecuta con `input` como entrada.
 * 0 bien, 1 error (el texto esta en out), 2 se agoto el tiempo, <0 no se pudo ejecutar. */
int shell_js_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms)
{
    if (out && cap) out[0] = 0;
#if defined(__x86_64__)
    if (!out || cap < 2) return -1;
    src_lock();
    usermode_python_set_source(src, len);
    int64_t code = js_proc(max_ms ? max_ms : 5000, check ? "-s" : "-x", out, cap, input);
    usermode_python_set_source(0, 0);
    kmutex_unlock(&g_src_lk);
    if (code < -1000000) return -1;
    if (code == USER_EXIT_TIMEOUT) return 2;
    if (code == 0) return 0;
    return code > 0 ? 1 : -1;
#else
    (void)check; (void)src; (void)len; (void)input; (void)max_ms;
    return -1;
#endif
}
