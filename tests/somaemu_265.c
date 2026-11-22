/* SOMA_265_CC: ejecuta en Linux x86_64 un programa de ring 3 de SOMA (el MISMO .elf que carga el nucleo) atendiendo sus llamadas
 * al sistema como lo haria SOMA (src/arch/x86_64/usermode.c), con ptrace(PTRACE_SYSEMU): ninguna llamada llega a Linux.
 * Solo sirve para las pruebas del compilador de C en el ordenador de desarrollo (tests/test_cc_265.py); en macOS no existe.
 *
 *   somaemu [-r RAIZ] [-a "argumentos"] [-s codigo.c] [-i entrada.txt] [-t ms] programa.elf
 *
 * RAIZ/workspace/... es /workspace/... ; el resto de rutas, RAIZ/... (solo lectura). Rutas privadas (/etc, /agent, /mem, /.ramfs):
 * EACCES. Escritura: solo /workspace/, hasta 4 MiB. Sale con el codigo del programa; 124 si se pasa de tiempo; 125 si muere por
 * una excepcion (fallo de pagina, instruccion ilegal...): lo que en SOMA seria "[USER] proceso terminado por excepcion". */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t g_pid;
static const char *g_raiz = ".", *g_args = "", *g_src, *g_in;
static char *g_input; static size_t g_inpos;
static long t0_ms;
static long ahora_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static int lee(uint64_t a, void *b, size_t n) { struct iovec l = { b, n }, r = { (void *)a, n }; return process_vm_readv(g_pid, &l, 1, &r, 1, 0) == (ssize_t)n ? 0 : -1; }
static int escribe(uint64_t a, const void *b, size_t n) { struct iovec l = { (void *)b, n }, r = { (void *)a, n }; return process_vm_writev(g_pid, &l, 1, &r, 1, 0) == (ssize_t)n ? 0 : -1; }

static char *lee_archivo(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    struct stat st; if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode)) { fclose(f); return 0; }
    char *b = malloc((size_t)st.st_size + 1); size_t k = fread(b, 1, (size_t)st.st_size, f); fclose(f);
    b[k] = 0; if (n) *n = k; return b;
}
/* ruta de SOMA -> ruta real; 1 = bajo /workspace/ ; -1 = no permitida. Se normaliza como agent_path_canon: // . y .. */
static int ruta(const char *p, char *o, size_t cap)
{
    if (p[0] != '/') return -1;
    char c[256]; size_t n = 0;
    for (const char *q = p; *q;) {
        while (*q == '/') q++;
        if (!*q) break;
        const char *e = q; while (*e && *e != '/') e++;
        size_t l = (size_t)(e - q);
        if (l == 1 && q[0] == '.') { q = e; continue; }
        if (l == 2 && q[0] == '.' && q[1] == '.') { while (n && c[n - 1] != '/') n--; if (n) n--; q = e; continue; }
        if (n + l + 2 >= sizeof c) return -1;
        c[n++] = '/'; memcpy(c + n, q, l); n += l; q = e;
    }
    if (!n) c[n++] = '/';
    c[n] = 0;
    static const char *priv[] = { "/etc", "/agent", "/mem", "/.ramfs", 0 };
    for (int i = 0; priv[i]; ++i) { size_t l = strlen(priv[i]); if (!strncmp(c, priv[i], l) && (c[l] == '/' || !c[l])) return -1; }
    snprintf(o, cap, "%s%s", g_raiz, c);
    return !strncmp(c, "/workspace/", 11) && c[11];
}

static long sys_con(uint64_t op, uint64_t ub, uint64_t len)
{
    if (op == 0) {                                                   /* una linea */
        if (!g_input) return -1;
        if (!g_input[g_inpos]) return -1;
        size_t s = g_inpos, n = 0;
        while (g_input[g_inpos] && g_input[g_inpos] != '\n') { g_inpos++; n++; }
        if (g_input[g_inpos] == '\n') g_inpos++;
        if (len > 1024) len = 1024;
        if (n > len) n = len;
        if (n && escribe(ub, g_input + s, n)) return -14;
        return (long)n;
    }
    if (op == 1) { int64_t v[6] = { 2026, 11, 22, 12, 0, 0 }; return escribe(ub, v, sizeof v) ? -14 : 0; }
    if (op == 2) return 0;                                           /* sin escritorio: la ventana no se abre */
    if (op == 3 || op == 4 || op == 5) return 0;
    if (op == 6) { int64_t v[8] = { 0 }; return escribe(ub, v, sizeof v) ? -14 : 0; }
    if (op == 7) return 0;
    if (op == 8) {
        size_t n; char *s = g_src ? lee_archivo(g_src, &n) : 0;
        if (!s) return -2;
        if (!ub) { free(s); return (long)n; }
        size_t m = len < n ? len : n;
        long r = (m && escribe(ub, s, m)) ? -14 : (long)m;
        free(s); return r;
    }
    return -22;
}
static long sys_vfs(uint64_t op, uint64_t ureq)
{
    uint64_t q[4];
    if (lee(ureq, q, sizeof q)) return -14;
    if (q[1] == 0 || q[1] >= 200) return -36;
    char p[256], real[512];
    if (lee(q[0], p, q[1])) return -14;
    p[q[1]] = 0;
    int w = ruta(p, real, sizeof real);
    if (w < 0) return -13;
    if (op == 2) { struct stat st; return stat(real, &st) || !S_ISREG(st.st_mode) ? -2 : (long)st.st_size; }
    if (op == 0) {
        size_t n; char *d = lee_archivo(real, &n);
        if (!d) return -2;
        size_t m = n < q[3] ? n : q[3];
        long r = (m && escribe(q[2], d, m)) ? -14 : (long)m;
        free(d); return r;
    }
    if (op != 1) return -22;
    if (w != 1) return -13;
    if (q[3] > (4u << 20)) return -27;
    char *b = malloc(q[3] + 1);
    if (q[3] && lee(q[2], b, q[3])) { free(b); return -14; }
    FILE *f = fopen(real, "wb");
    if (!f) { free(b); return -5; }
    fwrite(b, 1, q[3], f); fclose(f); free(b);
    return (long)q[3];
}

int main(int argc, char **argv)
{
    int o; long plazo = 20000;
    while ((o = getopt(argc, argv, "r:a:s:i:t:")) != -1) {
        if (o == 'r') g_raiz = optarg; else if (o == 'a') g_args = optarg; else if (o == 's') g_src = optarg;
        else if (o == 'i') g_in = optarg; else if (o == 't') plazo = atol(optarg);
        else return 2;
    }
    if (optind >= argc) { fprintf(stderr, "uso: somaemu [-r raiz] [-a args] [-s fuente] [-i entrada] [-t ms] prog.elf\n"); return 2; }
    if (g_in) g_input = lee_archivo(g_in, 0);
    fflush(stdout);
    g_pid = fork();
    if (g_pid == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        char *av[] = { argv[optind], 0 }, *ev[] = { 0 };
        execve(argv[optind], av, ev);
        _exit(127);
    }
    int st;
    waitpid(g_pid, &st, 0);                                          /* parada del execve */
    if (!WIFSTOPPED(st)) { fprintf(stderr, "somaemu: no arranca %s\n", argv[optind]); return 2; }
    ptrace(PTRACE_SETOPTIONS, g_pid, 0, PTRACE_O_EXITKILL);
    t0_ms = ahora_ms();
    for (;;) {
        if (ptrace(PTRACE_SYSEMU, g_pid, 0, 0) < 0) { perror("ptrace"); return 2; }
        if (waitpid(g_pid, &st, 0) < 0) return 2;
        if (WIFEXITED(st)) return WEXITSTATUS(st);
        if (WIFSIGNALED(st)) return 125;
        if (!WIFSTOPPED(st)) continue;
        if (WSTOPSIG(st) != SIGTRAP) {                               /* excepcion de CPU del programa */
            struct user_regs_struct r; ptrace(PTRACE_GETREGS, g_pid, 0, &r);
            fprintf(stderr, "[USER] proceso terminado por excepcion de CPU: senal %d rip=0x%llx\n", WSTOPSIG(st), (unsigned long long)r.rip);
            kill(g_pid, SIGKILL); waitpid(g_pid, &st, 0);
            return 125;
        }
        if (ahora_ms() - t0_ms > plazo) { fprintf(stderr, "[USER] proceso terminado: tiempo agotado\n"); kill(g_pid, SIGKILL); waitpid(g_pid, &st, 0); return 124; }
        struct user_regs_struct r;
        ptrace(PTRACE_GETREGS, g_pid, 0, &r);
        uint64_t nr = r.orig_rax, a0 = r.rdi, a1 = r.rsi, a2 = r.rdx;
        long ret;
        switch (nr) {
        case 0: fflush(stdout); kill(g_pid, SIGKILL); waitpid(g_pid, &st, 0); return (int)(a0 & 0xff);
        case 1: {
            if (a1 > 65536) a1 = 65536;
            char *b = malloc(a1 + 1);
            if (a1 && lee(a0, b, a1)) ret = -14; else { fwrite(b, 1, a1, stdout); ret = (long)a1; }
            free(b); break;
        }
        case 2: ret = 0; break;
        case 3: { struct timespec t = { (time_t)(a0 > 10000 ? 10 : a0 / 1000), (long)((a0 > 10000 ? 0 : a0 % 1000) * 1000000) }; nanosleep(&t, 0); ret = 0; break; }
        case 10: { size_t n = strlen(g_args); if (n > 128) n = 128; if (n > a1) n = a1; ret = (n && escribe(a0, g_args, n)) ? -14 : (long)n; break; }
        case 19: ret = ahora_ms() - t0_ms; break;
        case 30: ret = sys_con(a0, a1, a2); break;
        case 31: ret = sys_vfs(a0, a1); break;
        default: ret = -38;
        }
        r.rax = (uint64_t)ret;
        ptrace(PTRACE_SETREGS, g_pid, 0, &r);
    }
}
