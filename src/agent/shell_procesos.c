/* shell_procesos.c: programas y procesos: exec, procs, pci, ps, hilos, spawn, kill, reinicio y apagado.
 * SOMA_233_SHELL_PARTIDO: NO se compila por separado. Lo incluye src/agent/shell.c en su sitio (una sola unidad de
 * compilacion: usa las funciones auxiliares y la tabla de ordenes de shell.c, y el nucleo compilado sale identico). */
/* SOMA_PARCHE_016_EXEC / 019_FILES: exec [-b] [-t ms] <ruta.elf> [argumentos...]  |  procs
 * La ruta puede ser del VFS pequeno (/bin/hello.elf, max 16 KiB) o del almacen grande (/big/ruta, hasta 8 MiB). */
static int cmd_exec_impl(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    int bg = 0; uint32_t ms = 0;
    for (;;) {
        while (*args == ' ') args++;
        if (args[0] == '-' && args[1] == 'b' && (args[2] == ' ' || args[2] == 0)) { bg = 1; args += 2; continue; }
        if (args[0] == '-' && args[1] == 't' && args[2] == ' ') {
            args += 2; while (*args == ' ') args++;
            while (*args >= '0' && *args <= '9') ms = ms * 10 + (uint32_t)(*args++ - '0');
            continue;
        }
        break;
    }
    if (ms == 0) ms = bg ? 60000u : 30000u;
    char path[96]; uint32_t n = 0;
    while (*args && *args != ' ' && n < sizeof(path) - 1) path[n++] = *args++;
    path[n] = 0;
    while (*args == ' ') args++;                      /* lo que queda son los argumentos del programa */
    if (n == 0) { soma_str_put(out_buf, max_out, &pos, "uso: exec [-b] [-t plazo_ms] <ruta.elf> [argumentos]   (ej: exec /bin/hello.elf | exec /big/bin/prog.elf datos.bin)"); return 1; }

    char *img = 0; size_t len = 0;
    if (path[0] == '/' && path[1] == 'b' && path[2] == 'i' && path[3] == 'g' && (path[4] == '/' || path[4] == 0)) {
        int e; struct bigfs *fs = bigfs_sys(&e);
        uint32_t ino; struct bigfs_stat st;
        if (!fs) { soma_str_put(out_buf, max_out, &pos, "exec: BIGFS no disponible: "); soma_str_put(out_buf, max_out, &pos, bigfs_strerror(e)); return 1; }
        int t = bigfs_lookup(fs, path[4] ? path + 4 : "/", &ino);
        if (t < 0 || t != (int)BIGFS_T_FILE || bigfs_stat(fs, ino, &st) < 0) { soma_str_put(out_buf, max_out, &pos, "exec: no se puede leer "); soma_str_put(out_buf, max_out, &pos, path); return 1; }
        if (st.size > 8u * 1024 * 1024) { soma_str_put(out_buf, max_out, &pos, "exec: el ejecutable supera 8 MiB"); return 1; }
        img = (char *)kmalloc(st.size ? (size_t)st.size : 1);
        if (!img) { soma_str_put(out_buf, max_out, &pos, "exec: sin memoria"); return 1; }
        int64_t g = bigfs_read(fs, ino, 0, img, st.size);
        if (g != (int64_t)st.size) { kfree(img); soma_str_put(out_buf, max_out, &pos, "exec: error de lectura de "); soma_str_put(out_buf, max_out, &pos, path); return 1; }
        len = (size_t)st.size;
    } else {
        uint32_t ul = 0; img = vfs_read_alloc(path, &ul);   /* PARCHE 087 */
        int l = img ? (int)ul : -1;
        if (l <= 0) { if (img) kfree(img); soma_str_put(out_buf, max_out, &pos, "exec: no se puede leer "); soma_str_put(out_buf, max_out, &pos, path); return 1; }
        len = (size_t)l;
    }

    const char *base = path; for (const char *q = path; *q; ++q) if (*q == '/') base = q + 1;
    int err = 0;
    int idx = usermode_exec(base, img, len, ms, args, &err);
    kfree(img);
    if (idx < 0) {
        if (err != 0) { soma_str_put(out_buf, max_out, &pos, "exec: ELF invalido: "); soma_str_put(out_buf, max_out, &pos, elf_strerror(err)); }
        else { soma_str_put(out_buf, max_out, &pos, "exec: sin recursos (procesos/memoria), codigo "); soma_str_put_dec(out_buf, max_out, &pos, (uint64_t)(-idx)); }
        kprint("exec: rechazado: "); kprint(err != 0 ? elf_strerror(err) : "sin recursos"); kprint("\n");
        return 1;
    }
    if (bg) {
        soma_str_put(out_buf, max_out, &pos, "exec: "); soma_str_put(out_buf, max_out, &pos, base);
        soma_str_put(out_buf, max_out, &pos, " en segundo plano (idx "); soma_str_put_dec(out_buf, max_out, &pos, (uint64_t)idx);
        soma_str_put(out_buf, max_out, &pos, "); mira 'procs'");
        return 1;
    }
    int64_t code = usermode_wait(idx);
    soma_str_put(out_buf, max_out, &pos, "exec: "); soma_str_put(out_buf, max_out, &pos, base); soma_str_put(out_buf, max_out, &pos, " termino con codigo ");
    if (code < 0) { soma_str_put(out_buf, max_out, &pos, "-"); soma_str_put_dec(out_buf, max_out, &pos, (uint64_t)(-code)); }
    else soma_str_put_dec(out_buf, max_out, &pos, (uint64_t)code);
    kprint("exec: terminado\n");
    return 1;
}

static int cmd_exec(const char *args, char *out_buf, uint32_t max_out)
{
    if (max_out) out_buf[0] = 0;
    int r = cmd_exec_impl(args, out_buf, max_out);
    if (max_out && out_buf[0]) { kprint(out_buf); kprint("\n"); }      /* resultado y errores tambien por la consola serie */
    return r;
}

/* SOMA_237_PYTHON: python [script.py [argumentos] | -c "codigo"]. Sin nada: modo interactivo (>>>).
 * El interprete (MicroPython, src/user/python.c) va dentro del nucleo y corre como proceso de ring 3 con un filtro de llamadas
 * propio: lo de siempre + leer la consola y los archivos del VFS (escribir solo en /workspace). Si la orden se ejecuta capturada
 * (terminal de la GUI), el proceso comparte la captura: su salida aparece en el terminal y su entrada son lineas vacias. */
#if defined(__x86_64__)
#include "progz.h"
PROGZ_DECL(python);                                         /* SOMA_314_PROGZ: python.elf va comprimido en la imagen */
static volatile int g_python_idx = -1;                       /* proceso de la orden python en marcha (para pararlo con Esc) */
static volatile int g_python_stopped;
#endif
/* Para la orden python en marcha (Esc en el terminal de la GUI). 1 si habia una. */
int shell_python_stop(void)
{
#if defined(__x86_64__)
    int i = g_python_idx;
    if (i < 0) return 0;
    g_python_stopped = 1;
    usermode_stop(i);
    return 1;
#else
    return 0;
#endif
}
static int cmd_python(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
#if defined(__x86_64__)
    while (args && *args == ' ') args++;
    if (!args) args = "";
    char a[USER_ARGS_MAX]; uint32_t n = 0;
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == 0)) {
        const char *c = args + 2; while (*c == ' ') c++;
        uint32_t cl = 0; while (c[cl]) cl++;
        while (cl && c[cl - 1] == ' ') cl--;
        if (cl >= 2 && (c[0] == '"' || c[0] == '\'') && c[cl - 1] == c[0]) { c++; cl -= 2; }   /* quita las comillas de fuera */
        if (cl == 0) { soma_str_put(out_buf, max_out, &pos, "uso: python -c \"codigo\""); return 1; }
        if (cl + 4 > sizeof a) {
            soma_str_put(out_buf, max_out, &pos, "python -c: el codigo no cabe (maximo ");
            soma_str_put_dec(out_buf, max_out, &pos, sizeof a - 4);
            soma_str_put(out_buf, max_out, &pos, " caracteres); guardalo en /workspace/algo.py y usa: python algo.py");
            return 1;
        }
        a[n++] = '-'; a[n++] = 'c'; a[n++] = ' ';
        for (uint32_t i = 0; i < cl; ++i) a[n++] = c[i];
    } else {
        while (args[n]) {
            if (n + 1 >= sizeof a) { soma_str_put(out_buf, max_out, &pos, "python: demasiados argumentos"); return 1; }
            a[n] = args[n]; n++;
        }
        while (n && a[n - 1] == ' ') n--;
    }
    a[n] = 0;
    int capturing = console_capture_active();
    uint32_t ms = n == 0 ? 8u * 3600u * 1000u : 10u * 60u * 1000u;   /* interactivo: 8 h; script: 10 min */
    int err = 0;
    int idx = -2;                                                    /* SOMA_314_PROGZ: se descomprime, se lanza y se libera */
    { struct progz pz; if (progz_abre(PROGZ(python), "python", &pz) == 0) { idx = usermode_exec_caps("python", pz.elf, pz.len, ms, a, USER_SYS_PYTHON, &err); progz_cierra(&pz); } }
    if (idx < 0) {
        soma_str_put(out_buf, max_out, &pos, err ? "python: interprete no valido: " : "python: sin recursos (procesos o memoria)");
        if (err) soma_str_put(out_buf, max_out, &pos, elf_strerror(err));
        return 1;
    }
    int guest = capturing && console_capture_guest(usermode_proc_tid(idx));
    g_python_stopped = 0;
    g_python_idx = idx;
    int64_t code = guest ? shell_espera_proc(idx) : usermode_wait(idx);   /* SOMA_333_SALIDA */
    g_python_idx = -1;
    if (guest) (void)console_capture_guest(0);
    if (usermode_python_window_take()) gui_soma_win_close();       /* SOMA_239: la ventana que abrio el script no se queda huerfana */
    if (code < 0 && g_python_stopped) kprint("python: detenido\n");
    else if (code < 0) {
        kprint("python: el proceso termino a la fuerza (");
        kprint(code == USER_EXIT_TIMEOUT ? "se agoto el tiempo" : code == USER_EXIT_NOMEM ? "sin memoria" :
               code <= USER_EXIT_FAULT_BASE && code > USER_EXIT_FAULT_BASE - 256 ? "fallo de la CPU" : "codigo negativo");
        kprint(")\n");
    } else if (code != 0 && n != 0) {
        kprint("python: codigo de salida "); kprint_dec((uint32_t)code); kprint("\n");
    }
    (void)pos;
#else
    (void)args;
    soma_str_put(out_buf, max_out, &pos, "python: de momento solo en x86_64 (en aarch64 aun no hay procesos de usuario)");
#endif
    return 1;
}

/* SOMA_247_HABLA: texto a voz con una voz de Piper (ver src/arch/x86_64/voz_sys.c y src/user/voz/habla.cpp) */
static int cmd_habla(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
#if defined(__x86_64__)
    while (args && *args == ' ') args++;
    if (!args) args = "";
    if (!args[0]) {
        char v[96]; uint64_t sz = 0;
        soma_str_put(out_buf, max_out, &pos, "Texto a voz (voces de Piper, sin conexion)\n");
        if (voz_voice(v, sizeof v, &sz) == 0) {
            soma_str_put(out_buf, max_out, &pos, "  voz: "); soma_str_put(out_buf, max_out, &pos, v);
            soma_str_put(out_buf, max_out, &pos, " ("); soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)(sz >> 20)); soma_str_put(out_buf, max_out, &pos, " MiB)\n");
        } else soma_str_put(out_buf, max_out, &pos, "  voz: ninguna. Copia una voz de Piper (el .onnx y su .onnx.json) en la carpeta MODELOS del pendrive\n");
        soma_str_put(out_buf, max_out, &pos, voz_speak_state() ? "  leyendo ahora (habla parar)\n" : "");
        soma_str_put(out_buf, max_out, &pos, "Uso: habla texto  |  habla instalar  |  habla parar. En Notas: boton Leer en voz alta");
        return 1;
    }
    if (str_eq(args, "instalar")) { (void)voz_voice_install(out_buf, max_out); return 1; }
    if (str_eq(args, "parar")) { voz_speak_stop(); voz_stop(); soma_str_put(out_buf, max_out, &pos, "Lectura detenida"); return 1; }
    uint32_t n = 0; while (args[n]) n++;
    int r = voz_speak_run(args, n, 0, 0);
    soma_str_put(out_buf, max_out, &pos, r == 0 ? "" : "habla: no se pudo leer el texto");
#else
    (void)args;
    soma_str_put(out_buf, max_out, &pos, "habla: de momento solo en x86_64");
#endif
    return 1;
}

/* SOMA_329_IA_LOCAL: el motor de IA local (ver src/arch/x86_64/ia_sys.c) */
static int cmd_ia(const char *args, char *out_buf, uint32_t max_out)
{
#if defined(__x86_64__)
    return ia_cmd(args, out_buf, max_out);
#else
    (void)args; uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "ia: el motor de IA local solo esta en x86_64 (necesita AVX2 en ring 3)\n"); return 1;
#endif
}

/* SOMA_241_VOZ: voz a texto con whisper.cpp (ver src/arch/x86_64/voz_sys.c). Los audios y el modelo estan en el almacen BIGFS. */
static int cmd_voz(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
#if defined(__x86_64__)
    while (args && *args == ' ') args++;
    if (!args) args = "";
    if (shell_starts_with(args, "modelo ")) {                 /* SOMA_246_VOZ_MODELOS: voz modelo rapido|preciso|maximo */
        const char *k = args + 7; while (*k == ' ') k++;
        int t = voz_tier_from_key(k);
        if (t < 0) { soma_str_put(out_buf, max_out, &pos, "voz modelo: elige rapido, preciso o maximo"); return 1; }
        voz_set_pref(t);
        char m[96];
        soma_str_put(out_buf, max_out, &pos, "Modelo de voz: "); soma_str_put(out_buf, max_out, &pos, voz_tier_name(t));
        if (voz_installed(t, m, sizeof m, 0) == 0) { soma_str_put(out_buf, max_out, &pos, " ("); soma_str_put(out_buf, max_out, &pos, m); soma_str_put(out_buf, max_out, &pos, ")"); }
        else if (voz_on_stick(t, 0, 0, 0) == 0) soma_str_put(out_buf, max_out, &pos, ": esta en el pendrive y se instalara al usarlo (o ahora con: voz instalar)");
        else { soma_str_put(out_buf, max_out, &pos, ": no instalado. Copia "); soma_str_put(out_buf, max_out, &pos, voz_tier_file(t)); soma_str_put(out_buf, max_out, &pos, " en la carpeta MODELOS del pendrive; mientras tanto se usa otro"); }
        return 1;
    }
    if (!args[0] || str_eq(args, "modelo") || str_eq(args, "estado")) {
        const char *why; char m[96]; uint64_t sz = 0; int tier = 0;
        soma_str_put(out_buf, max_out, &pos, "Voz a texto (whisper.cpp, sin conexion; espanol por defecto)\n");
        soma_str_put(out_buf, max_out, &pos, usermode_avx2_ok() ? "  CPU: AVX2 activo\n" : "  CPU: sin AVX2 (no se puede usar)\n");
        soma_str_put(out_buf, max_out, &pos, "  elegido: "); soma_str_put(out_buf, max_out, &pos, voz_tier_name(voz_pref())); soma_str_put(out_buf, max_out, &pos, " (Ajustes > Voz, o: voz modelo rapido|preciso|maximo)\n");
        if (voz_model_tier(m, sizeof m, &sz, &tier) == 0) {
            soma_str_put(out_buf, max_out, &pos, "  en uso: "); soma_str_put(out_buf, max_out, &pos, m);
            soma_str_put(out_buf, max_out, &pos, " ("); soma_str_put_dec(out_buf, max_out, &pos, (uint32_t)(sz >> 20)); soma_str_put(out_buf, max_out, &pos, " MiB)");
            if (tier != voz_pref()) soma_str_put(out_buf, max_out, &pos, "  <- el elegido no esta instalado");
            soma_str_put(out_buf, max_out, &pos, "\n");
        } else soma_str_put(out_buf, max_out, &pos, "  modelo: ninguno. Copia ggml-base-q5_1.bin en la carpeta MODELOS del pendrive y ejecuta: voz instalar\n");
        if (!voz_ready(&why) && usermode_avx2_ok() && voz_model(m, sizeof m, 0) == 0) { soma_str_put(out_buf, max_out, &pos, "  "); soma_str_put(out_buf, max_out, &pos, why); soma_str_put(out_buf, max_out, &pos, "\n"); }
        soma_str_put(out_buf, max_out, &pos, "Uso: voz archivo.wav  |  voz ultima (la ultima grabacion)  |  voz instalar  |  voz modelo rapido|preciso|maximo  |  opciones -T -l idioma -v -b haz");
        return 1;
    }
    if (str_eq(args, "instalar")) {
        int r = voz_install(out_buf, max_out);
        pos = 0; while (pos < max_out && out_buf[pos]) pos++;
        soma_str_put(out_buf, max_out, &pos, r == 0 ? "\nPrueba: voz ultima" : "");
        return 1;
    }
    char a[USER_ARGS_MAX]; uint32_t n = 0;
    const char *rest = args;
    if (shell_starts_with(args, "ultima") && (args[6] == 0 || args[6] == ' ')) {   /* la ultima grabacion de la Grabadora */
        char w[80];
        if (voz_last_recording(w, sizeof w) != 0) { soma_str_put(out_buf, max_out, &pos, "voz: no hay grabaciones (usa la Grabadora o la orden grabar)"); return 1; }
        rest = args + 6; while (*rest == ' ') rest++;
        for (const char *c = rest; *c && n + 2 < sizeof a; ++c) a[n++] = *c;
        if (n) a[n++] = ' ';
        for (const char *c = w; *c && n + 1 < sizeof a; ++c) a[n++] = *c;
        kprint("voz: "); kprint(w); kprint("\n");
    } else for (const char *c = args; *c && n + 1 < sizeof a; ++c) a[n++] = *c;
    while (n && a[n - 1] == ' ') n--;
    a[n] = 0;
    (void)voz_run(a, 0, 0, 0);
#else
    (void)args;
    soma_str_put(out_buf, max_out, &pos, "voz: de momento solo en x86_64 (necesita procesos de usuario con AVX2)");
#endif
    return 1;
}

/* SOMA_239_PYTHON_GRAFICO: para el programador de SOMA. Comprueba (check=1: solo compila) o ejecuta un programa Python que todavia
 * no esta en disco, con `input` como entrada (lineas separadas por \n; agotada = EOFError) y su salida en out.
 * Devuelve 0 si termino bien, 1 si Python dio un error (el texto esta en out), 2 si se agoto el tiempo, <0 si no se pudo ejecutar. */
#if defined(__x86_64__)
static struct kmutex g_src_lk; static int g_src_lk_init;     /* SOMA_265_CC: Python y C comparten el codigo inyectado (op 8) */
static void src_lock(void) { if (!g_src_lk_init) { kmutex_init(&g_src_lk, "pysrc"); g_src_lk_init = 1; } kmutex_lock(&g_src_lk); }
#endif
int shell_python_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms)
{
    if (out && cap) out[0] = 0;
#if defined(__x86_64__)
    if (!out || cap < 2) return -1;
    src_lock();
    usermode_python_set_source(src, len);
    int err = 0;
    int idx = -2;                                                    /* SOMA_314_PROGZ */
    { struct progz pz; if (progz_abre(PROGZ(python), "python", &pz) == 0) { idx = usermode_exec_caps("python", pz.elf, pz.len, max_ms ? max_ms : 5000, check ? "-s" : "-x", USER_SYS_PYTHON, &err); progz_cierra(&pz); } }
    int64_t code = -1;
    if (idx >= 0) {                                   /* salida y entrada propias: no usa la captura de la consola (que no se anida) */
        usermode_redirect(idx, out, cap, input ? input : "");
        code = usermode_wait(idx);
        if (usermode_python_window_take()) gui_soma_win_close();
    }
    usermode_python_set_source(0, 0);
    kmutex_unlock(&g_src_lk);
    if (idx < 0) return -1;
    if (code == USER_EXIT_TIMEOUT) return 2;
    if (code == 0) return 0;
    return code > 0 ? 1 : -1;
#else
    (void)check; (void)src; (void)len; (void)input; (void)max_ms;
    return -1;
#endif
}

/* SOMA_265_CC: compilador de C. TinyCC (third_party/tinycc) con la biblioteca de C de SOMA (src/user/cc), dentro del nucleo como
 * Python y ejecutado como proceso de ring 3 con las mismas capacidades: consola, reloj, ventana y archivos del VFS (escribir solo en
 * /workspace). Los programas que compila son ELF normales de SOMA y corren igual, con esas capacidades.
 *   cc hola.c [mas.c] [-o x.elf] [-c] [-w] [-Dn=v] [-heap MiB]   compila (rutas relativas: /workspace)
 *   cc -run hola.c [argumentos]                                    compila y ejecuta
 *   cc -run hola.elf [argumentos]                                  ejecuta uno ya compilado */
#if defined(__x86_64__)
PROGZ_DECL(cc);                                             /* SOMA_314_PROGZ: cc.elf va comprimido en la imagen */
#define CC_RUN_ELF "/workspace/.cc_run.elf"
#define CC_PROG_ELF "/workspace/.programa.elf"
/* lanza un proceso con las capacidades de Python/C y espera; con out != 0, su salida va a out y su entrada es input */
static int64_t cc_proc(const char *name, const void *img, size_t len, uint32_t ms, const char *args, char *out, uint32_t cap,
                       const char *input, int *err)
{
    *err = 0;
    int idx = usermode_exec_caps(name, img, len, ms, args, USER_SYS_PYTHON, err);
    if (idx < 0) return (int64_t)idx - 1000000;                           /* < -1000000: no se pudo lanzar */
    int guest = 0;
    if (out) usermode_redirect(idx, out, cap, input ? input : "");
    else guest = console_capture_active() && console_capture_guest(usermode_proc_tid(idx));
    g_python_stopped = 0;
    g_python_idx = idx;                                                   /* Esc en el terminal lo para, como a python */
    int64_t code = guest ? shell_espera_proc(idx) : usermode_wait(idx);   /* SOMA_333_SALIDA */
    g_python_idx = -1;
    if (guest) (void)console_capture_guest(0);
    if (usermode_python_window_take()) gui_soma_win_close();
    return code;
}
/* SOMA_314_PROGZ: cc_proc con el compilador incrustado (se descomprime para lanzarlo y se libera al terminar) */
static int64_t cc_proc_cc(uint32_t ms, const char *args, char *out, uint32_t cap, const char *input, int *err)
{
    struct progz pz;
    *err = 0;
    if (progz_abre(PROGZ(cc), "cc", &pz) != 0) return -1000001;
    int64_t code = cc_proc("cc", pz.elf, pz.len, ms, args, out, cap, input, err);
    progz_cierra(&pz);
    return code;
}
static const char *cc_motivo(int64_t code)
{
    return code == USER_EXIT_TIMEOUT ? "se agoto el tiempo" : code == USER_EXIT_NOMEM ? "sin memoria" :
           code == USER_EXIT_QUOTA ? "demasiadas llamadas al sistema" :
           code <= USER_EXIT_FAULT_BASE && code > USER_EXIT_FAULT_BASE - 256 ? "fallo de la CPU: puntero no valido, division por cero o pila agotada" : "a la fuerza";
}
/* carga un .elf del VFS y lo ejecuta; 0 bien, 1 no se pudo */
static int cc_run_elf(const char *path, const char *args, char *out_buf, uint32_t max_out, uint32_t *pos)
{
    uint32_t ul = 0; char *img = vfs_read_alloc(path, &ul);
    if (!img || ul == 0) {
        if (img) kfree(img);
        soma_str_put(out_buf, max_out, pos, "cc: no se puede leer "); soma_str_put(out_buf, max_out, pos, path);
        return 1;
    }
    const char *base = path; for (const char *q = path; *q; ++q) if (*q == '/') base = q + 1;
    int err;
    int64_t code = cc_proc(base, img, ul, 10u * 60u * 1000u, args, 0, 0, 0, &err);
    kfree(img);
    if (code < -1000000) {
        soma_str_put(out_buf, max_out, pos, err ? "cc: el ejecutable no es valido: " : "cc: sin recursos (procesos o memoria)");
        if (err) soma_str_put(out_buf, max_out, pos, elf_strerror(err));
        return 1;
    }
    if (code < 0 && g_python_stopped) kprint("cc: detenido\n");
    else if (code < 0) { kprint("cc: el programa termino ("); kprint(cc_motivo(code)); kprint(")\n"); }
    else if (code != 0) { kprint("cc: el programa termino con codigo "); kprint_dec((uint32_t)code); kprint("\n"); }
    return 0;
}
#endif
static int cmd_cc(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (max_out) out_buf[0] = 0;
#if defined(__x86_64__)
    while (args && *args == ' ') args++;
    if (!args || !*args) {
        soma_str_put(out_buf, max_out, &pos, "Compilador de C (TinyCC). Uso: cc hola.c  (crea /workspace/hola.elf) | cc -run hola.c [args] | "
                                              "cc -run hola.elf [args] | cc a.c b.c -o juego.elf | opciones -c -w -Dnombre=valor -heap MiB. "
                                              "Cabeceras: stdio.h, stdlib.h, string.h, math.h, time.h... y soma.h (ventanas)");
        return 1;
    }
    char a[USER_ARGS_MAX]; uint32_t n = 0;
    int run = args[0] == '-' && args[1] == 'r' && args[2] == 'u' && args[3] == 'n' && (args[4] == ' ' || !args[4]);
    if (!run) {
        while (args[n]) { if (n + 1 >= sizeof a) { soma_str_put(out_buf, max_out, &pos, "cc: demasiados argumentos"); return 1; } a[n] = args[n]; n++; }
        while (n && a[n - 1] == ' ') n--;
        a[n] = 0;
        int err;
        int64_t code = cc_proc_cc(120000, a, 0, 0, 0, &err);
        if (code < -1000000) { soma_str_put(out_buf, max_out, &pos, "cc: no se pudo lanzar el compilador"); return 1; }
        if (code < 0 && !g_python_stopped) { kprint("cc: el compilador termino ("); kprint(cc_motivo(code)); kprint(")\n"); }
        return 1;
    }
    /* cc -run archivo [argumentos] */
    const char *f = args + 4; while (*f == ' ') f++;
    char file[96]; uint32_t fl = 0;
    while (f[fl] && f[fl] != ' ' && fl < sizeof file - 12) { file[fl] = f[fl]; fl++; }
    file[fl] = 0;
    const char *rest = f + fl; while (*rest == ' ') rest++;
    if (!fl) { soma_str_put(out_buf, max_out, &pos, "uso: cc -run hola.c [argumentos]"); return 1; }
    char path[112]; uint32_t pl = 0;
    if (file[0] != '/') { const char *w = "/workspace/"; while (*w) path[pl++] = *w++; }
    for (uint32_t i = 0; i < fl; ++i) path[pl++] = file[i];
    path[pl] = 0;
    int es_elf = pl > 4 && path[pl - 4] == '.' && path[pl - 3] == 'e' && path[pl - 2] == 'l' && path[pl - 1] == 'f';
    if (!es_elf) {
        const char *pre = "-run "; while (*pre) a[n++] = *pre++;
        for (uint32_t i = 0; i < pl && n + 1 < sizeof a; ++i) a[n++] = path[i];
        a[n] = 0;
        int err;
        int64_t code = cc_proc_cc(120000, a, 0, 0, 0, &err);
        if (code < -1000000) { soma_str_put(out_buf, max_out, &pos, "cc: no se pudo lanzar el compilador"); return 1; }
        if (code != 0) { if (code < 0 && !g_python_stopped) { kprint("cc: el compilador termino ("); kprint(cc_motivo(code)); kprint(")\n"); } return 1; }
        (void)cc_run_elf(CC_RUN_ELF, rest, out_buf, max_out, &pos);
        return 1;
    }
    (void)cc_run_elf(path, rest, out_buf, max_out, &pos);
#else
    (void)args;
    soma_str_put(out_buf, max_out, &pos, "cc: de momento solo en x86_64 (en aarch64 aun no hay procesos de usuario)");
#endif
    return 1;
}

/* SOMA_265_CC: para el programador. Como shell_python_source, pero con C: check=1 solo compila (los errores, "programa.c:N: error:
 * ...", quedan en out); check=0 compila y ejecuta con `input` como entrada. 0 bien, 1 error (de compilacion o el programa termino
 * con codigo distinto de 0 o por un fallo), 2 se agoto el tiempo, <0 no se pudo ejecutar. */
int shell_c_source(int check, const char *src, uint32_t len, const char *input, char *out, uint32_t cap, uint32_t max_ms)
{
    if (out && cap) out[0] = 0;
#if defined(__x86_64__)
    if (!out || cap < 2) return -1;
    src_lock();
    usermode_python_set_source(src, len);
    int err;
    int64_t code = cc_proc_cc(20000, check ? "-s" : "-x", out, cap, "", &err);
    usermode_python_set_source(0, 0);
    if (code < -1000000) { kmutex_unlock(&g_src_lk); return -1; }
    if (code == USER_EXIT_TIMEOUT) { kmutex_unlock(&g_src_lk); return 2; }
    if (code != 0 || check) { kmutex_unlock(&g_src_lk); return code == 0 ? 0 : code > 0 ? 1 : -1; }
    uint32_t ul = 0; char *img = vfs_read_alloc(CC_PROG_ELF, &ul);
    if (!img || !ul) { if (img) kfree(img); kmutex_unlock(&g_src_lk); return -1; }
    code = cc_proc("programa", img, ul, max_ms ? max_ms : 5000, "", out, cap, input, &err);
    kfree(img);
    kmutex_unlock(&g_src_lk);
    if (code < -1000000) return -1;
    if (code == USER_EXIT_TIMEOUT) return 2;
    if (code < 0) {                                                       /* el fallo se cuenta en la salida: el programador lo lee */
        uint32_t p = 0; while (p < cap && out[p]) p++;
        soma_str_put(out, cap, &p, "\n[el programa termino: "); soma_str_put(out, cap, &p, cc_motivo(code)); soma_str_put(out, cap, &p, "]");
        return 1;
    }
    return code == 0 ? 0 : 1;
#else
    (void)check; (void)src; (void)len; (void)input; (void)max_ms;
    return -1;
#endif
}

static int cmd_procs(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    usermode_format_table(out_buf, max_out, 1);
    kprint(out_buf);
    return 1;
}

/* SOMA_PARCHE_014_RING3 */
static int cmd_usertest(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    int bad = usermode_selftest();
    uint32_t pos = 0;
    soma_str_put(out_buf, max_out, &pos, bad == 0 ? "usertest: OK (ring 3, syscalls y aislamiento)" :
                                               "usertest: FALLO (ver consola serie)");
    return 1;
}

/* SOMA_PARCHE_012_RENDER */
static int cmd_fbverify(const char *args, char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    if (args && args[0] == 'o' && args[1] == 'n') fb_verify_enabled = 1;
    else if (args && args[0] == 'o' && args[1] == 'f') fb_verify_enabled = 0;
    uint32_t f, t, w; fb_debug_stats(&f, &t, &w);
    kprint("FBVERIFY: "); kprint(fb_verify_enabled ? "activo" : "inactivo");
    kprint(" | fotogramas: "); kprint_dec(f);
    kprint(" | pixeles fuera del dano (total): "); kprint_dec(t);
    kprint(" | peor fotograma: "); kprint_dec(w); kprint("\n");
    soma_str_put(out_buf, max_out, &pos, "fbverify on|off: comprueba que todo lo dibujado fue marcado sucio");
    return 1;
}

static int cmd_pmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    pmm_dump_stats();
    pmm_test_self();
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "PMM: Verificado.");
    return 1;
}

static int cmd_vmm(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    vmm_test_self();
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "VMM: Verificado.");
    return 1;
}

static int cmd_pci(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
#if defined(__aarch64__)
    if (pcie_present()) { pcie_init(); (void)rp1_init(); pcie_format(out_buf, max_out); kprint(out_buf); rp1_format(out_buf, max_out); kprint(out_buf); return 1; }     /* PARCHE 062/067: Pi 5 */
#endif
    pci_scan();
    pci_format_scan(out_buf, max_out);
    return 1;
}

static int cmd_arp(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    arp_format_cache(out_buf, max_out);
    kprint(out_buf);
    return 1;
}

static int cmd_ps(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

/* PARCHE 132 */
static int cmd_stacks(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_format_stacks(out_buf, max_out);
    return 1;
}

/* PARCHE 138: servicios aislados (decodificadores de imagen en ring 3) */
#ifdef __x86_64__
#include "sandbox.h"
static void shell_copy(char *dst, const char *src, uint32_t cap);
static int cmd_sbx(const char *args, char *out_buf, uint32_t max_out)
{
    if (args && args[0] == 't') { int b = sbx_selftest(); shell_copy(out_buf, b ? "sbx: FALLO (ver consola)\n" : "sbx: OK\n", max_out); return 1; }
    struct sbx_stats s; sbx_get_stats(&s);
    char *o = out_buf; uint32_t n = 0;
    #define SP(t) do { const char *_t = (t); while (*_t && n + 1 < max_out) o[n++] = *_t++; } while (0)
    #define SN(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } SP(_b + _i); } while (0)
    SP("Servicios aislados (ring 3, solo exit): decodificadores de imagen\n  llamadas "); SN(s.runs); SP(", correctas "); SN(s.ok);
    SP(", procesos muertos "); SN(s.died); SP(", respuestas rechazadas "); SN(s.bad_reply); SP(", sin memoria "); SN(s.oom);
    SP(", sin ranura "); SN(s.spawn_fail); SP("\n  ultima "); SN(s.last_ms); SP(" ms, maxima "); SN(s.max_ms); SP(" ms, total "); SN(s.total_ms);
    SP(" ms\n  'sbx test' ejecuta las pruebas de aislamiento\n");
    o[n] = 0;
    #undef SP
    #undef SN
    return 1;
}
#endif

static int cmd_threads(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    thread_dump();
    thread_format_table(out_buf, max_out);
    return 1;
}

static int cmd_spawn(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: spawn <comando>  (alias: bg <comando>)\n");
        uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "Uso: spawn <comando>");
        return 1;
    }

    struct bg_job *job = (struct bg_job *)kmalloc(sizeof(struct bg_job));
    if (!job) {
        kprint("Error: Sin memoria para spawn.\n");
        return 1;
    }
    memset(job, 0, sizeof(*job));

    /* AGENT_POLICY_V1: capturar el origen antes de lanzar el hilo. */
    job->agent_origin = (uint8_t)(agent_dispatch_mode != 0);

    uint32_t ci = 0;
    while (args[ci] && ci < sizeof(job->cmd) - 1) {
        job->cmd[ci] = args[ci];
        ci++;
    }
    job->cmd[ci] = '\0';

    char th_name[32];
    th_name[0] = 'b'; th_name[1] = 'g'; th_name[2] = '_';
    uint32_t ni = 3;
    for (uint32_t i = 0; args[i] && args[i] != ' ' && ni < sizeof(th_name) - 1; ++i) {
        th_name[ni++] = args[i];
    }
    th_name[ni] = '\0';

    struct tcb *nt = thread_create(th_name, spawn_job_worker, job);
    if (!nt) {
        kprint("Error: Fallo al crear hilo para spawn.\n");
        kfree(job);
        return 1;
    }

    job->tid = nt->tid;

    /* Formatear /tmp/job_<TID>.log limpiamente */
    uint32_t li = 0;
    const char *pfx = "/tmp/job_";
    while (*pfx) job->log_file[li++] = *pfx++;
    char tbuf[10]; int tn = 0; uint32_t tid_tmp = job->tid;
    if (tid_tmp == 0) tbuf[tn++] = '0';
    else { while (tid_tmp) { tbuf[tn++] = (char)('0' + (tid_tmp % 10)); tid_tmp /= 10; } }
    while (tn > 0) job->log_file[li++] = tbuf[--tn];
    const char *sfx = ".log";
    while (*sfx) job->log_file[li++] = *sfx++;
    job->log_file[li] = '\0';

    kprint("[SPAWN] Tarea '"); kprint(job->cmd);
    kprint("' lanzada en segundo plano [TID "); kprint_dec(job->tid);
    kprint(", Hilo '"); kprint(th_name);
    kprint("'] -> Log: "); kprint(job->log_file); kprint("\n");

    uint32_t pos = 0;
    soma_str_put(out_buf, max_out, &pos, "[SPAWN OK] TID=");
    soma_str_put_dec(out_buf, max_out, &pos, job->tid);
    soma_str_put(out_buf, max_out, &pos, " Log=");
    soma_str_put(out_buf, max_out, &pos, job->log_file);
    return 1;
}

static int cmd_kill(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t tid = 0; while (*args >= '0' && *args <= '9') tid = tid * 10 + (*args++ - '0');
    thread_kill(tid);
    uint32_t pos = 0; soma_str_put(out_buf, max_out, &pos, "Kill ejecutado");
    return 1;
}

static int cmd_sleep(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    uint32_t ms = 0; while (*args >= '0' && *args <= '9') ms = ms * 10 + (*args++ - '0');
    if (ms == 0) ms = 1000;
    thread_sleep(ms);
    uint32_t p = 0; soma_str_put(out_buf, max_out, &p, "sleep completado");
    return 1;
}

static int cmd_time(const char *args, char *out_buf, uint32_t max_out)
{
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprint("Uso: time <comando>\n");
        return 1;
    }
    uint64_t t0 = timer_get_uptime_ms(); uint64_t c0 = rdtsc();
    dispatch_command(args, out_buf, max_out);
    uint64_t c1 = rdtsc(); uint64_t t1 = timer_get_uptime_ms();
    kprint("\n[TIME]: "); kprint_dec((uint32_t)(t1 - t0)); kprint(" ms | "); kprint_dec((uint32_t)(c1 - c0)); kprint(" ciclos TSC\n");
    return 1;
}

static int cmd_reboot(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_reboot();
    return 1;
}

static int cmd_poweroff(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    system_poweroff();
    return 1;
}

static int cmd_halt(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args; (void)out_buf; (void)max_out;
    vfs_sync();
    kprint("Sistema detenido (HALT).\n");
    for (;;) { arch_interrupts_disable(); arch_halt(); }
    return 1;
}
