/* SOMA_PARCHE_014_RING3: modo usuario (ring 3) en x86_64. Ver usermode.h. */
#include <stdint.h>
#include <stddef.h>
#include "usermode.h"
#include "web_r3.h"            /* SOMA_202 */
#include "sandbox.h"   /* PARCHE 139: sbx_svc_next */
#include "uaccess.h"
#include "user_path.h"
#include "bigfs_errno.h"
#include "vmm.h"
#include "pmm.h"
#include "smp.h"
#include "idt.h"
#include "arch.h"
#include "console.h"
#include "thread.h"
#include "elf.h"
#include "fs.h"
#include "bigfs_glue.h"
#include "audio_sys.h"
#include "fb.h"
#include "mem.h"
#include "tcp.h"        /* PARCHE 142 */
#include "dns.h"
#include "net.h"
#include "rtc.h"        /* SOMA_237_PYTHON */
int gui_is_running(void);
#include "gui.h"                /* SOMA_239: gui_soma_win_* */

#define MSR_EFER   0xC0000080u
#define MSR_STAR   0xC0000081u
#define MSR_LSTAR  0xC0000082u
#define MSR_SFMASK 0xC0000084u

#define SEL_KCODE 0x08
#define SEL_UDATA 0x1B   /* 0x18 | RPL 3 */
#define SEL_UCODE 0x23   /* 0x20 | RPL 3 */

extern void tss_set_rsp0(uint64_t rsp0);
static int inited;

#define USER_MAX_UPAGES (USER_CODE_MAX_PAGES + USER_STACK_PAGES + 16)
struct upage { uint64_t va, frame, flags; };
struct ufd { uint32_t ino; uint64_t pos; int used; };

/* Un proceso de usuario. Los tres primeros campos los usa el ensamblador (desplazamientos 0, 8, 16). */
struct uproc {
    uint64_t user_rsp;       /*  0: rsp de usuario durante una llamada al sistema */
    uint64_t kstack;         /*  8: pila de kernel para SYSCALL/IRQ (bajo los marcos de entrada del hilo) */
    uint64_t saved_rsp;      /* 16: pila del hilo al entrar en ring 3 (para volver con usermode_leave) */
    struct upage pages[USER_MAX_UPAGES];   /* paginas de usuario del proceso (codigo, datos, pila) */
    int      npages;
    uint64_t entry, stack_top;
    uint64_t as;             /* PML4 propia (direccion fisica) */
    uint64_t deadline_ms;
    uint64_t start_ms, end_ms;
    int64_t  exit_code;
    uint32_t tid;
    int      state;          /* 0 libre, 1 vivo, 2 terminado (hilo ya recogido; falta `usermode_wait`) */
    int      finished;       /* 1 = salio por exit/fallo/plazo (distingue de 'matado desde fuera') */
    char     name[16];
    struct ufd fds[USER_MAX_FDS];           /* SOMA_PARCHE_019_FILES */
    char     args[USER_ARGS_MAX];
    uint64_t nsys;           /* PARCHE 126: syscalls hechas (cuota) */
    uint64_t wbytes;         /* PARCHE 126: bytes escritos con SYS_WRITE */
    uint32_t sysmask;        /* PARCHE 138: syscalls permitidas (bit n) */
    uint32_t reserve_frames; /* PARCHE 138: reserva minima del PMM para crecer regiones bajo demanda */
    struct uregion *regions[USER_MAX_REGIONS];   /* PARCHE 138: memoria prestada (no se libera al morir) */
    int      nregions;
    int      audio;                         /* SOMA_PARCHE_020_AUDIO: este proceso tiene el flujo de audio abierto */
    int      fb;                            /* SOMA_PARCHE_021_FPU: este proceso tiene la pantalla */
    uint8_t  fpu[USER_FPU_BYTES] __attribute__((aligned(64)));   /* SOMA_PARCHE_021_FPU: estado x87/MMX/SSE (FXSAVE); SOMA_241_VOZ: y AVX (XSAVE) */
    uint8_t  io[16384];                     /* bufer de rebote por proceso para SYS_READ (una lectura puede ceder la CPU) */
    char    *rd_out;                        /* SOMA_239: salida redirigida a un bufer del nucleo (0 = a la consola) */
    uint32_t rd_cap, rd_len;
    const char *rd_in;                      /* SOMA_239: entrada fija (lineas con \n); agotada = fin de entrada */
    int      may_thr;                       /* SOMA_244_HILOS: puede lanzar hilos en los nucleos secundarios */
};

static struct uproc procs[USER_MAX_PROCS];
struct uproc *usermode_cur;           /* proceso en ejecucion (0 = hilo de kernel); lo lee el ensamblador */
static uint64_t slice_start_ms;
#define USER_SLICE_MS 10
#define USER_KSTACK_RESERVE 1024      /* se reserva arriba de la pila del hilo para sus marcos de entrada */
#define USER_EXIT_KILLED (-4000)
/* PARCHE 126: politica de interrupciones y cuotas. SFMASK entra en cada syscall con IF=0; las syscalls cortas siguen
 * asi, pero las que esperan disco/audio o imprimen mucho abren una ventana con IF=1 (el kernel ya es cooperativo y el
 * reloj solo actua si interrumpe ring 3, asi que es seguro) y la cierran antes de tocar memoria de usuario. */
#define USER_MAX_SYSCALLS  (1ULL << 28)    /* mas llamadas que esto = bucle desbocado: se mata */
#define USER_WRITE_MAX     65536           /* bytes por SYS_WRITE: escritura corta (como POSIX), nunca un bloqueo largo */
#define USER_BLIT_ROWS_BYTES (256 * 1024)  /* el blit abre ventana de IRQ cada tanto */
static inline void sys_irq_window(void) { arch_interrupts_enable(); __asm__ volatile ("nop; nop" ::: "memory"); arch_interrupts_disable(); }

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi; __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

extern void usermode_syscall_entry(void);
extern int64_t usermode_enter(struct uproc *up, uint64_t rip, uint64_t rsp);   /* vuelve con el codigo de salida */
extern void usermode_leave(int64_t code) __attribute__((noreturn));

__asm__(
".text\n"
".global usermode_enter\n"
"usermode_enter:\n"
"    pushfq\n"
"    pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
"    movq %rsp, 16(%rdi)\n"
"    cli\n"
"    movw $0x1B, %ax\n movw %ax, %ds\n movw %ax, %es\n"
"    pushq $0x1B\n"            /* SS */
"    pushq %rdx\n"             /* RSP de usuario */
"    pushq $0x202\n"           /* RFLAGS: IF=1 */
"    pushq $0x23\n"            /* CS */
"    pushq %rsi\n"             /* RIP */
"    iretq\n"
".global usermode_leave\n"
"usermode_leave:\n"
"    movq usermode_cur(%rip), %r10\n"
"    movq %rdi, %rax\n"
"    movq 16(%r10), %rsp\n"
"    movw $0x10, %cx\n movw %cx, %ds\n movw %cx, %es\n"
"    popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rbp\n popq %rbx\n"
"    popfq\n"
"    ret\n"
".global usermode_syscall_entry\n"
"usermode_syscall_entry:\n"
"    movq usermode_cur(%rip), %r10\n"
"    movq %rsp, 0(%r10)\n"
"    movq 8(%r10), %rsp\n"
"    pushq 0(%r10)\n"
"    pushq %rcx\n"             /* RIP de retorno */
"    pushq %r11\n"             /* RFLAGS */
"    subq $8, %rsp\n"          /* alineacion a 16 */
"    movq %rdx, %rcx\n"
"    movq %rsi, %rdx\n"
"    movq %rdi, %rsi\n"
"    movq %rax, %rdi\n"
"    cld\n"
"    call usermode_syscall\n"
"    addq $8, %rsp\n"
"    popq %r11\n"
"    popq %rcx\n"
"    popq %rsp\n"
"    sysretq\n"
);

static int smap_on, smep_on;

static inline void cpuid_leaf(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c2, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c2), "=d"(*d) : "0"(leaf), "2"(sub));
}

int usermode_cpu_features(void) { return (smep_on ? 1 : 0) | (smap_on ? 2 : 0); }

/* SOMA_241_VOZ: AVX para los procesos de usuario (whisper.cpp/ggml calculan con AVX2+FMA). Sin CR4.OSXSAVE y XCR0 la CPU da #UD con
 * cualquier instruccion AVX. Con XSAVE, el estado de cada proceso (x87, SSE y la mitad alta de los YMM) se guarda con xsave64/xrstor64
 * al cambiar de hilo en lugar de fxsave64. El nucleo no usa SIMD (-mgeneral-regs-only), asi que solo los procesos tienen ese estado.
 * Se llama en el procesador de arranque antes de despertar a los demas (smp.c copia CR4 y XCR0 a cada nucleo). */
static int xsave_on;                  /* 1 = se guarda con XSAVE (x87|SSE|AVX) */
static int avx2_ok;                   /* AVX2 + FMA + F16C + BMI2 disponibles para ring 3 */
void usermode_fpu_init(void)
{
    uint32_t a, b, c2, d;
    cpuid_leaf(0, 0, &a, &b, &c2, &d);
    uint32_t maxleaf = a;
    cpuid_leaf(1, 0, &a, &b, &c2, &d);
    int has_xsave = (c2 >> 26) & 1, has_avx = (c2 >> 28) & 1, has_fma = (c2 >> 12) & 1, has_f16c = (c2 >> 29) & 1;
    if (!has_xsave || !has_avx || maxleaf < 0xD) { kprint("USERMODE: sin AVX (XSAVE/AVX no disponibles): la orden voz no funcionara\n"); return; }
    uint64_t cr4; __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4 | (1ULL << 18)) : "memory");              /* OSXSAVE */
    __asm__ volatile ("xsetbv" : : "c"(0), "a"(7u), "d"(0u));                              /* XCR0 = x87 | SSE | AVX */
    cpuid_leaf(0xD, 0, &a, &b, &c2, &d);                                                   /* b = tamano del area para XCR0 actual */
    if (b > USER_FPU_BYTES) {                                                              /* no deberia pasar: 832 bytes con AVX */
        __asm__ volatile ("xsetbv" : : "c"(0), "a"(3u), "d"(0u));
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
        kprint("USERMODE: area XSAVE demasiado grande; sin AVX\n");
        return;
    }
    xsave_on = 1;
    if (maxleaf >= 7) { cpuid_leaf(7, 0, &a, &b, &c2, &d); avx2_ok = ((b >> 5) & 1) && ((b >> 8) & 1) && has_fma && has_f16c; }
    kprint(avx2_ok ? "USERMODE: AVX2 activo para los procesos (XSAVE)\n" : "USERMODE: AVX activo (sin AVX2: la orden voz no funcionara)\n");
}
int usermode_avx2_ok(void) { return xsave_on && avx2_ok; }

/* SOMA_PARCHE_017_AS: SMEP (el kernel no ejecuta paginas de usuario) y SMAP (no las lee/escribe salvo
 * con stac/clac). Solo si la CPU los tiene (QEMU: -cpu qemu64,+smep,+smap). */
static void enable_smep_smap(void)
{
    uint32_t a, b, c2, d;
    cpuid_leaf(0, 0, &a, &b, &c2, &d);
    if (a < 7) return;
    cpuid_leaf(7, 0, &a, &b, &c2, &d);
    uint64_t cr4; __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (b & (1u << 7))  { cr4 |= (1ULL << 20); smep_on = 1; }
    if (b & (1u << 20)) { cr4 |= (1ULL << 21); smap_on = 1; }
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

static void usermode_init(void)
{
    if (inited) return;
    enable_smep_smap();
    kprint("USERMODE: SMEP "); kprint(smep_on ? "activo" : "no disponible");
    kprint(", SMAP "); kprint(smap_on ? "activo" : "no disponible"); kprint(" (CPU)\n");
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1ULL);                       /* SCE */
    wrmsr(MSR_STAR, ((uint64_t)0x10 << 48) | ((uint64_t)SEL_KCODE << 32));
    wrmsr(MSR_LSTAR, (uint64_t)&usermode_syscall_entry);
    /* SOMA_191: ademas AC (bit 18) y NT (bit 14). Ring 3 puede poner AC con popf: sin limpiarlo, la syscall entera corria con SMAP
     * anulado. Con NT puesto, un iretq del kernel da #GP. (Linux limpia los mismos bits.) */
    wrmsr(MSR_SFMASK, 0x47700);                                    /* IF, TF, DF, NT y AC a 0 al entrar */
    inited = 1;
}

/* El rango [p, p+len) debe estar en la ventana de usuario y mapeado con el bit U (y, si need_write, escribible)
 * en cada pagina. len hasta 1 MiB. */
_Static_assert(UACC_PRESENT == VMM_FLAG_PRESENT && UACC_WRITABLE == VMM_FLAG_WRITABLE && UACC_USER == VMM_FLAG_USER, "uaccess.h desincronizado de vmm.h");
static uint64_t user_pte_of(void *ctx, uint64_t va) { return vmm_as_get_pte(((struct uproc *)ctx)->as, va); }
static int user_range(uint64_t p, uint64_t len, int need_write)
{
    if (!usermode_cur) return 0;
    if (p >= USER_BASE && p - USER_BASE < USER_WINDOW)
        return uaccess_range_ok(p, len, need_write, USER_BASE, USER_WINDOW, user_pte_of, usermode_cur);   /* PARCHE 102 */
    /* SOMA_241_VOZ: tambien dentro de una region del proceso (el monton grande de voz); las paginas deben estar ya presentes */
    for (int i = 0; i < usermode_cur->nregions; ++i) {
        const struct uregion *r = usermode_cur->regions[i];
        uint64_t sz = (uint64_t)r->npages * 4096;
        if (r && p >= r->va && p - r->va < sz && len <= 1u << 20)
            return uaccess_range_ok(p, len, need_write, r->va, sz, user_pte_of, usermode_cur);
    }
    return 0;
}
static int user_range_ok(uint64_t p, uint64_t len) { return user_range(p, len, 0); }

/* SOMA_PARCHE_019_FILES: copias entre kernel y usuario con SMAP abierto solo durante la copia */
static void copy_from_user(void *dst, uint64_t usrc, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)usrc;
    if (smap_on) __asm__ volatile ("stac" ::: "memory");
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    if (smap_on) __asm__ volatile ("clac" ::: "memory");
}
static void copy_to_user(uint64_t udst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)udst; const uint8_t *s = (const uint8_t *)src;
    if (smap_on) __asm__ volatile ("stac" ::: "memory");
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    if (smap_on) __asm__ volatile ("clac" ::: "memory");
}

#define E_FAULT  SOMA_EFAULT                 /* PARCHE 124: tabla unica (src/core/errno.h); los errores de BIGFS se traducen con bigfs_errno() */
#define E_BADF   SOMA_EBADF
#define E_INVAL  SOMA_EINVAL
#define E_MFILE  SOMA_EMFILE
#define E_ACCES  SOMA_EACCES

static struct ufd *get_fd(int64_t fd)
{
    if (fd < 0 || fd >= USER_MAX_FDS || !usermode_cur || !usermode_cur->fds[fd].used) return 0;
    return &usermode_cur->fds[fd];
}

static int64_t sys_open(uint64_t pptr, uint64_t plen)
{
    if (plen == 0 || plen > BIGFS_PATH_MAX || !user_range(pptr, plen, 0)) return E_FAULT;
    char path[BIGFS_PATH_MAX + 1];
    copy_from_user(path, pptr, plen); path[plen] = 0;
    if (!user_path_allowed(path, (unsigned)plen)) return E_ACCES;     /* PARCHE 121: nada privado ni interno de BIGFS desde ring 3 */
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return bigfs_errno(e);
    uint32_t ino; int t = bigfs_lookup(fs, path, &ino);
    if (t < 0) return bigfs_errno(t);
    if (t != (int)BIGFS_T_FILE) return SOMA_EISDIR;
    for (int i = 0; i < USER_MAX_FDS; ++i)
        if (!usermode_cur->fds[i].used) { usermode_cur->fds[i].used = 1; usermode_cur->fds[i].ino = ino; usermode_cur->fds[i].pos = 0; return i; }
    return E_MFILE;
}

/* ---- PARCHE 142: red por capacidad ---- */
static int own_sock(uint64_t h) { return h < 64 && usermode_cur && tcp_owner((int)h) == usermode_cur->tid && usermode_cur->tid != 0; }
static int64_t sys_dns(uint64_t uname, uint64_t len, uint64_t uip)
{
    if (len == 0 || len > 253) return E_INVAL;
    if (!user_range(uname, len, 0) || !user_range(uip, 4, 1)) return E_FAULT;
    char name[256]; copy_from_user(name, uname, len); name[len] = 0;
    for (uint64_t i = 0; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return E_INVAL;
    }
    uint8_t ip[4];
    arch_interrupts_enable();
    net_client_lock();
    int ok = dns_resolve(name, ip, 4000);
    net_client_unlock();
    arch_interrupts_disable();
    if (!ok) return SOMA_ENOENT;
    copy_to_user(uip, ip, 4);
    return 0;
}
static int64_t sys_tcp_open(uint64_t ip_be, uint64_t port)
{
    if (port == 0 || port > 65535) return E_INVAL;
    if (tcp_count_owned(usermode_cur->tid) >= USER_MAX_SOCKETS) return SOMA_EBUSY;
    uint8_t ip[4] = { (uint8_t)(ip_be >> 24), (uint8_t)(ip_be >> 16), (uint8_t)(ip_be >> 8), (uint8_t)ip_be };
    arch_interrupts_enable();
    net_client_lock();
    int h = tcp_connect_start(ip, (uint16_t)port);
    net_client_unlock();
    arch_interrupts_disable();
    return h < 0 ? SOMA_EIO : h;
}
static int64_t sys_tcp_poll(uint64_t h)
{
    if (!own_sock(h)) return E_BADF;
    arch_interrupts_enable();
    int r = tcp_connect_poll((int)h);
    arch_interrupts_disable();
    return r < 0 ? SOMA_EIO : r;
}
static int64_t sys_tcp_send(uint64_t h, uint64_t ubuf, uint64_t len)
{
    if (!own_sock(h)) return E_BADF;
    if (len > 65536) return E_INVAL;
    if (!user_range(ubuf, len, 0)) return E_FAULT;
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done < sizeof(usermode_cur->io) ? len - done : sizeof(usermode_cur->io);
        copy_from_user(usermode_cur->io, ubuf + done, n);
        arch_interrupts_enable();
        int r = tcp_send((int)h, usermode_cur->io, (uint32_t)n);
        arch_interrupts_disable();
        if (r < 0) return done ? (int64_t)done : SOMA_EIO;
        done += n;
    }
    return (int64_t)done;
}
static int64_t sys_tcp_recv(uint64_t h, uint64_t ubuf, uint64_t max)
{
    if (!own_sock(h)) return E_BADF;
    if (max == 0) return 0;
    if (max > sizeof(usermode_cur->io)) max = sizeof(usermode_cur->io);
    if (!user_range(ubuf, max, 1)) return E_FAULT;
    arch_interrupts_enable();
    int r = tcp_recv((int)h, usermode_cur->io, (uint16_t)max, 2);
    arch_interrupts_disable();
    if (r > 0) { copy_to_user(ubuf, usermode_cur->io, (uint64_t)r); return r; }
    if (r == 0) return 0;
    return r == TCP_EOF ? -1 : SOMA_EIO;
}
static int64_t sys_tcp_close(uint64_t h)
{
    if (!own_sock(h)) return E_BADF;
    arch_interrupts_enable();
    tcp_close((int)h);
    arch_interrupts_disable();
    return 0;
}

static int64_t sys_read(uint64_t fdn, uint64_t ubuf, uint64_t len)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;                               /* con IF=0: acota lo que se retrasa el reloj */
    if (!user_range(ubuf, len, 1)) return E_FAULT;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return bigfs_errno(e);
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done < sizeof(usermode_cur->io) ? len - done : sizeof(usermode_cur->io);
        arch_interrupts_enable();
        int64_t g = bigfs_read(fs, f->ino, f->pos, usermode_cur->io, n);
        arch_interrupts_disable();
        if (g < 0) return done ? (int64_t)done : bigfs_errno((int)g);
        if (g == 0) break;
        copy_to_user(ubuf + done, usermode_cur->io, (uint64_t)g);
        f->pos += (uint64_t)g; done += (uint64_t)g;
        if ((uint64_t)g < n) break;
    }
    return (int64_t)done;
}

static int64_t sys_seek(uint64_t fdn, int64_t off, uint64_t whence)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return bigfs_errno(e);
    struct bigfs_stat st;
    if (bigfs_stat(fs, f->ino, &st) < 0) return SOMA_ENOENT;
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)f->pos : whence == 2 ? (int64_t)st.size : -1;
    if (base < 0) return E_INVAL;
    int64_t np;                                                  /* PARCHE 030: el desbordamiento con signo era indefinido */
    if (__builtin_add_overflow(base, off, &np) || np < 0) return E_INVAL;
    f->pos = (uint64_t)np;
    return np;
}

static int64_t sys_fsize(uint64_t fdn)
{
    struct ufd *f = get_fd((int64_t)fdn);
    if (!f) return E_BADF;
    int e; struct bigfs *fs = bigfs_sys(&e);
    if (!fs) return bigfs_errno(e);
    struct bigfs_stat st;
    if (bigfs_stat(fs, f->ino, &st) < 0) return SOMA_ENOENT;
    return (int64_t)st.size;
}

/* SOMA_PARCHE_021_FPU: pantalla */
static int64_t sys_fb_blit(uint64_t ubuf, uint64_t wh, uint64_t xy)
{
    if (!usermode_cur->fb) return E_BADF;
    uint32_t w = (uint32_t)(wh >> 16) & 0xFFFF, h = (uint32_t)wh & 0xFFFF, x = (uint32_t)(xy >> 16) & 0xFFFF, y = (uint32_t)xy & 0xFFFF;
    uint32_t sw = fb_get_width(), sh = fb_get_height();
    if (w == 0 || h == 0 || x + w > sw || y + h > sh) return E_INVAL;
    uint64_t bytes = (uint64_t)w * h * 4;
    if (!user_range(ubuf, bytes, 0)) return E_FAULT;
    volatile uint32_t *dst = fb_get_buffer();
    if (!dst) return E_FAULT;
    /* se copia fila a fila con SMAP abierto solo durante la copia (con interrupciones cerradas: nadie mas toca el
     * backbuffer a mitad); despues se marca el rectangulo y se presenta */
    const uint8_t *src = (const uint8_t *)ubuf;
    uint32_t rows_per = USER_BLIT_ROWS_BYTES / (w * 4); if (rows_per == 0) rows_per = 1;
    for (uint32_t r0 = 0; r0 < h; r0 += rows_per) {
        uint32_t r1 = r0 + rows_per < h ? r0 + rows_per : h;
        if (smap_on) __asm__ volatile ("stac" ::: "memory");
        for (uint32_t r = r0; r < r1; ++r) {
            uint32_t *d = (uint32_t *)(uintptr_t)(dst + (uint64_t)(y + r) * sw + x);
            const uint32_t *s = (const uint32_t *)(src + (uint64_t)r * w * 4);
            uint64_t n = w;
            __asm__ volatile ("rep movsl" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
        }
        if (smap_on) __asm__ volatile ("clac" ::: "memory");
        if (r1 < h) sys_irq_window();                     /* SMAP ya cerrado: el reloj puede atender */
    }
    fb_damage((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
    fb_present();
    return 0;
}

/* SOMA_PARCHE_020_AUDIO */
static int64_t sys_audio_write(uint64_t ubuf, uint64_t len)
{
    if (!usermode_cur->audio) return E_BADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;
    len &= ~3ULL;
    if (len == 0 || !user_range(ubuf, len, 0)) return E_FAULT;
    uint64_t done = 0;
    while (done < len) {
        uint64_t n = len - done < sizeof(usermode_cur->io) ? len - done : sizeof(usermode_cur->io);
        copy_from_user(usermode_cur->io, ubuf + done, n);
        arch_interrupts_enable();
        int k = audio_sys_write(usermode_cur->io, (uint32_t)n);
        arch_interrupts_disable();
        if (k <= 0) break;
        done += (uint64_t)k;
        if ((uint64_t)k < n) break;
    }
    return (int64_t)done;
}

static int64_t sys_getargs(uint64_t ubuf, uint64_t len)
{
    if (len == 0 || !user_range(ubuf, len, 1)) return E_FAULT;
    uint64_t n = 0; while (usermode_cur->args[n] && n < USER_ARGS_MAX - 1) n++;
    uint64_t c = n + 1 < len ? n + 1 : len;                    /* con NUL si cabe */
    char tmp[USER_ARGS_MAX];
    for (uint64_t i = 0; i < c; ++i) tmp[i] = i < n ? usermode_cur->args[i] : 0;
    if (c == len) tmp[c - 1] = 0;
    copy_to_user(ubuf, tmp, c);
    return (int64_t)n;
}

/* SOMA_237_PYTHON: las dos llamadas propias del interprete de Python (solo las tiene el proceso que lanza la orden "python").
 * SYS_SOMA_CON: op 0 = leer una linea de la consola (o de la entrada simulada si la orden se ejecuta capturada; -1 = no hay mas),
 *               op 1 = reloj del equipo en out[6] = anio, mes, dia, hora, minuto, segundo. */
#define SOMA_LINE_MAX 1024
static uint64_t path_len(const char *p) { uint64_t n = 0; while (p[n]) n++; return n; }
/* SOMA_239_PYTHON_GRAFICO: la ventana de las aplicaciones (la misma que usa SOMA-LANG: gui_soma_win_*) para el proceso de Python.
 *   op 2: crear(req = { titulo, largo, ancho, alto })   -> 1 abierta | 0 sin escritorio
 *   op 3: cerrar
 *   op 4: dibujar(lote, bytes): palabras de 32 bits [orden, args...] (ver PYG_* abajo); como mucho PYG_MAX bytes por llamada
 *   op 5: presentar (el escritorio compone el fotograma)
 *   op 6: estado(out[8], consumir) = { activa, ancho, alto, raton x, raton y, boton, dentro, clic }; el clic solo se borra con consumir=1
 *   op 7: tecla -> codigo (0 = ninguna; flechas 16/14/2/6 como en SOMA-LANG)
 *   op 8: codigo fuente inyectado por el nucleo (programador: comprobar o probar sin escribir el archivo): out, max -> bytes */
#define PYG_CLEAR 1
#define PYG_PIXEL 2
#define PYG_RECT  3
#define PYG_LINE  4
#define PYG_CIRC  5
#define PYG_TEXT  6
#define PYG_MAX   (64u * 1024u)
static volatile int g_py_win;                                      /* el proceso de Python abrio la ventana (se cierra al terminar) */
static int g_py_click;                                             /* clic pendiente de leer con win.mouse() */
static const char *volatile g_py_src; static volatile uint32_t g_py_src_len;
void usermode_redirect(int idx, char *out, uint32_t cap, const char *input)   /* SOMA_239: llamar justo despues de lanzarlo */
{
    if (idx < 0 || idx >= USER_MAX_PROCS || !out || cap < 2) return;
    struct uproc *u = &procs[idx];
    out[0] = 0; u->rd_len = 0; u->rd_cap = cap; u->rd_in = input ? input : ""; u->rd_out = out;
}
int  usermode_python_window_take(void) { int w = g_py_win; g_py_win = 0; return w; }
void usermode_python_set_source(const char *src, uint32_t len) { g_py_src = src; g_py_src_len = src ? len : 0; }
static int32_t *g_pyg;
static int64_t pyg_draw(uint64_t ubuf, uint64_t len)
{
    if (len == 0) return 0;
    if (len > PYG_MAX || (len & 3) || !user_range(ubuf, len, 0)) return E_FAULT;
    if (!g_pyg) { g_pyg = (int32_t *)kmalloc(PYG_MAX + 4); if (!g_pyg) return SOMA_ENOMEM; }
    copy_from_user(g_pyg, ubuf, len);
    uint32_t n = (uint32_t)(len / 4), i = 0;
    arch_interrupts_enable();
    while (i < n) {
        int32_t o = g_pyg[i]; const int32_t *a = g_pyg + i + 1; uint32_t left = n - i - 1;
        if (o == PYG_CLEAR && left >= 1) { gui_soma_win_clear((uint32_t)a[0]); i += 2; }
        else if (o == PYG_PIXEL && left >= 3) { gui_soma_win_pixel(a[0], a[1], (uint32_t)a[2]); i += 4; }
        else if (o == PYG_RECT && left >= 5) { gui_soma_win_rect(a[0], a[1], a[2], a[3], (uint32_t)a[4]); i += 6; }
        else if (o == PYG_LINE && left >= 5) {
            int32_t x0 = a[0], y0 = a[1], x1 = a[2], y1 = a[3];                     /* acotada: una linea de 2^31 px no cuelga el nucleo */
            if (x0 > -4096 && x0 < 8192 && x1 > -4096 && x1 < 8192 && y0 > -4096 && y0 < 8192 && y1 > -4096 && y1 < 8192)
                gui_soma_win_line(x0, y0, x1, y1, (uint32_t)a[4]);
            i += 6;
        }
        else if (o == PYG_CIRC && left >= 4) { if (a[2] > 0 && a[2] < 4096) gui_soma_win_circle(a[0], a[1], a[2], (uint32_t)a[3]); i += 5; }
        else if (o == PYG_TEXT && left >= 4) {
            uint32_t tl = (uint32_t)a[3], words = (tl + 3) / 4;
            if (tl > 1024 || words > left - 4) break;
            char t[1025]; const char *src = (const char *)(a + 4);
            for (uint32_t k = 0; k < tl; ++k) t[k] = src[k] ? src[k] : ' ';
            t[tl] = 0;
            gui_soma_win_text(a[0], a[1], t, (uint32_t)a[2]);
            i += 5 + words;
        }
        else break;                                                             /* lote mal formado: se ignora el resto */
    }
    arch_interrupts_disable();
    return 0;
}
static int64_t sys_soma_con(uint64_t op, uint64_t ubuf, uint64_t len)
{
    if (op == 2) {
        uint64_t q[4];
        if (!user_range(ubuf, sizeof q, 0)) return E_FAULT;
        copy_from_user(q, ubuf, sizeof q);
        char t[64]; uint64_t tl = q[1] < sizeof t - 1 ? q[1] : sizeof t - 1;
        if (tl && !user_range(q[0], tl, 0)) return E_FAULT;
        if (tl) copy_from_user(t, q[0], tl);
        t[tl] = 0;
        int w = (int)(q[2] > 2000 ? 2000 : q[2]), h = (int)(q[3] > 2000 ? 2000 : q[3]);
        arch_interrupts_enable();
        int ok = gui_is_running() && gui_soma_win_open(t, w, h);
        arch_interrupts_disable();
        if (ok) { g_py_win = 1; g_py_click = 0; }
        return ok ? 1 : 0;
    }
    if (op == 3) { arch_interrupts_enable(); gui_soma_win_close(); arch_interrupts_disable(); g_py_win = 0; return 0; }
    if (op == 4) return pyg_draw(ubuf, len);
    if (op == 5) { arch_interrupts_enable(); gui_soma_win_present(); arch_interrupts_disable(); return 0; }
    if (op == 6) {
        if (!user_range(ubuf, 8 * sizeof(int64_t), 1)) return E_FAULT;
        int st[8] = { 0 };
        arch_interrupts_enable();
        st[0] = gui_soma_win_is_active();
        gui_soma_win_get_size(&st[1], &st[2]);
        gui_soma_win_get_mouse(&st[3], &st[4], &st[5], &st[6], &st[7]);
        arch_interrupts_disable();
        g_py_click |= st[7]; st[7] = g_py_click;                   /* el clic se guarda hasta que lo lea win.mouse() (len = 1): */
        if (len) g_py_click = 0;                                   /* win.active() o win.size() no se lo comen */
        int64_t v[8]; for (int i = 0; i < 8; ++i) v[i] = st[i];
        copy_to_user(ubuf, v, sizeof v);
        usermode_cur->nsys = 0;                                    /* un juego pregunta el estado sin parar: no es un bucle desbocado */
        return 0;
    }
    if (op == 7) { arch_interrupts_enable(); int k = gui_soma_win_get_key(); arch_interrupts_disable(); return k; }
    if (op == 8) {
        if (!g_py_src) return SOMA_ENOENT;
        if (ubuf == 0) return g_py_src_len;
        uint64_t m = len < g_py_src_len ? len : g_py_src_len;
        if (m && !user_range(ubuf, m, 1)) return E_FAULT;
        if (m) copy_to_user(ubuf, g_py_src, m);
        return (int64_t)m;
    }
    if (op == 1) {
        if (!user_range(ubuf, 6 * sizeof(int64_t), 1)) return E_FAULT;
        struct rtc_time t; rtc_get_datetime(&t);
        int64_t v[6] = { t.year, t.month, t.day, t.hour, t.min, t.sec };
        copy_to_user(ubuf, v, sizeof v);
        return 0;
    }
    if (op != 0) return E_INVAL;
    if (len == 0) return 0;
    if (len > SOMA_LINE_MAX) len = SOMA_LINE_MAX;
    if (!user_range(ubuf, len, 1)) return E_FAULT;
    if (usermode_cur->rd_out) {                                     /* SOMA_239: entrada fija del que lo lanzo */
        const char *p = usermode_cur->rd_in;
        if (!p || !*p) return -1;
        char line[SOMA_LINE_MAX]; uint64_t n = 0;
        while (*p && *p != '\n') { if (n < len) line[n++] = *p; p++; }
        if (*p == '\n') p++;
        usermode_cur->rd_in = p;
        if (n) copy_to_user(ubuf, line, n);
        return (int64_t)n;
    }
    if (console_capture_input_eof()) return -1;                    /* ejecucion capturada (terminal de la GUI): entrada agotada */
    if (!console_capture_mine() && gui_is_running()) return -1;    /* sin captura y con el escritorio: el teclado es de la GUI */
    char line[SOMA_LINE_MAX + 1];
    arch_interrupts_enable();
    int n = kgetline(line, (uint32_t)len + 1);
    arch_interrupts_disable();
    if (n < 0) return -1;
    usermode_cur->nsys = 0;                                        /* esperar al teclado no cuenta como bucle desbocado */
    if (n) copy_to_user(ubuf, line, (uint64_t)n);
    return n;
}
/* SYS_SOMA_VFS(op, &req): archivos del VFS de SOMA. op 0 = leer (hasta req.len bytes), 1 = escribir (crea o sustituye; SOLO bajo
 * /workspace/), 2 = tamano. La ruta se normaliza y pasa el mismo filtro que SYS_OPEN: nada de /etc, /agent, /mem, /.ramfs... */
struct soma_vfs_req { uint64_t path, plen, buf, len; };
#define SOMA_VFS_WMAX (4u << 20)                                   /* escritura maxima: 4 MiB */
static int64_t sys_soma_vfs(uint64_t op, uint64_t ureq)
{
    struct soma_vfs_req r;
    if (!user_range(ureq, sizeof r, 0)) return E_FAULT;
    copy_from_user(&r, ureq, sizeof r);
    if (r.plen == 0 || r.plen >= FS_NAME_MAX) return SOMA_ENAMETOOLONG;
    if (!user_range(r.path, r.plen, 0)) return E_FAULT;
    char raw[FS_NAME_MAX], path[AGENT_POLICY_PATH_MAX + 1];
    copy_from_user(raw, r.path, r.plen); raw[r.plen] = 0;
    if (!user_path_allowed(raw, (unsigned)r.plen)) return E_ACCES;
    if (!agent_path_canon(raw, path, sizeof path) || path_len(path) >= FS_NAME_MAX) return E_INVAL;
    if (op == 2) {
        arch_interrupts_enable(); int sz = vfs_size(path); arch_interrupts_disable();
        return sz < 0 ? SOMA_ENOENT : sz;
    }
    if (op == 0) {
        if (r.len && !user_range(r.buf, r.len, 1)) return E_FAULT;
        uint32_t n = 0;
        arch_interrupts_enable(); char *d = vfs_read_alloc(path, &n); arch_interrupts_disable();
        if (!d) return SOMA_ENOENT;
        uint64_t m = n < r.len ? n : r.len;
        if (m) copy_to_user(r.buf, d, m);
        kfree(d);
        return (int64_t)m;
    }
    if (op != 1) return E_INVAL;
    static const char ws[] = "/workspace/";
    for (unsigned i = 0; i < sizeof ws - 1; ++i) if (path[i] != ws[i]) return E_ACCES;
    if (!path[sizeof ws - 1]) return E_INVAL;
    if (r.len > SOMA_VFS_WMAX) return SOMA_EFBIG;
    if (r.len && !user_range(r.buf, r.len, 0)) return E_FAULT;
    char *k = (char *)kmalloc(r.len + 1);
    if (!k) return SOMA_ENOMEM;
    if (r.len) copy_from_user(k, r.buf, r.len);
    k[r.len] = 0;
    arch_interrupts_enable(); int w = vfs_write(path, k, (uint32_t)r.len); arch_interrupts_disable();
    kfree(k);
    return w < 0 ? SOMA_EIO : (int64_t)r.len;
}

/* SOMA_244_HILOS: hilos de usuario en los nucleos secundarios (ver smp.c). Solo para procesos con este bit (voz).
 *   op 0: nucleos libres; op 1 (a1 = {rip, rsp, arg, exit_rip}): lanza un hilo -> nucleo (>0) o <0;
 *   op 2 (a1 = nucleo): espera a que termine -> 0 bien, <0 fallo del hilo (o el proceso se para). */
static int64_t sys_soma_thr(uint64_t op, uint64_t a1)
{
    struct uproc *up = usermode_cur;
    if (op == 0) return smp_user_free();
    if (op == 1) {
        if (!usermode_avx2_ok()) return -38;
        if (!user_range_ok(a1, 32)) return E_FAULT;
        uint64_t q[4]; copy_from_user(q, a1, 32);
        if (q[0] < USER_BASE || q[0] - USER_BASE >= USER_CODE_AREA || q[3] < USER_BASE || q[3] - USER_BASE >= USER_CODE_AREA) return -22;
        return smp_user_start(up->as, q[0], q[1], q[2], q[3], up->tid);
    }
    if (op == 2) {
        for (;;) {
            int64_t st = 0;
            int r = smp_user_poll((int)a1, up->tid, &st);
            if (r < 0) return -22;
            if (r == 1) {
                if (st != 0 && st != -4000) {
                    int vec; uint64_t rip, cr2; smp_user_info((int)a1, &vec, &rip, &cr2);
                    kprint("[USER] hilo terminado por excepcion de CPU: vector "); kprint_dec((uint32_t)vec);
                    kprint(" rip=0x"); kprint_hex32((uint32_t)(rip >> 32)); kprint_hex32((uint32_t)rip);
                    if (vec == 14) { kprint(" cr2=0x"); kprint_hex32((uint32_t)(cr2 >> 32)); kprint_hex32((uint32_t)cr2); }
                    kprint("\n");
                }
                return st;
            }
            if (timer_get_uptime_ms() >= up->deadline_ms) { smp_user_kill_owner(up->tid); return -4000; }   /* se paro el proceso */
            sys_irq_window();                                     /* el reloj y el teclado siguen mientras espera */
            thread_yield();
        }
    }
    return -22;
}

int64_t usermode_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2)
{
    (void)a2;
    if (++usermode_cur->nsys > USER_MAX_SYSCALLS) {                /* PARCHE 126: cuota de llamadas */
        kprint("[USER] proceso terminado: supero la cuota de llamadas al sistema\n");
        usermode_cur->exit_code = USER_EXIT_QUOTA;
        usermode_leave(USER_EXIT_QUOTA);
    }
    /* SOMA_192: un programa NORMAL (filtro por defecto) que pide una llamada inexistente o sin capacidad recibe ENOSYS, como antes del
     * parche 138, y sigue vivo: asi lo exige el oraculo de `usfuzz` (que fallaba desde entonces: los procesos morian con DENIED).
     * Los procesos AISLADOS (filtro restringido: decodificadores, PDF, red) siguen muriendo al primer intento. */
    if (nr == SYS_SOMA_THR && usermode_cur->may_thr) return sys_soma_thr(a0, a1);   /* SOMA_244_HILOS: solo el proceso de voz */
    if ((nr >= 32 || !(usermode_cur->sysmask & (1u << nr))) && usermode_cur->sysmask == USER_SYS_DEFAULT) return SOMA_ENOSYS;
    if (nr >= 32 || !(usermode_cur->sysmask & (1u << nr))) {        /* PARCHE 138: filtro por proceso (aislados: solo exit) */
        kprint("[USER] proceso terminado: llamada al sistema no permitida por su filtro\n");
        usermode_cur->exit_code = USER_EXIT_DENIED;
        usermode_leave(USER_EXIT_DENIED);
    }
    switch (nr) {
    case SYS_EXIT:
        usermode_cur->exit_code = (int64_t)a0;
        usermode_leave((int64_t)a0);
    case SYS_WRITE:
        if (a1 == 0) return 0;                                   /* PARCHE 102: escribir 0 bytes no es un fallo */
        if (a1 > USER_WRITE_MAX) a1 = USER_WRITE_MAX;            /* PARCHE 126: escritura corta */
        if (!user_range_ok(a0, a1)) return E_FAULT;
        usermode_cur->wbytes += a1;
        if (usermode_cur->rd_out) {                                 /* SOMA_239: salida a un bufer del nucleo (el que lo lanzo la lee) */
            struct uproc *u = usermode_cur;
            uint64_t room = u->rd_cap > u->rd_len + 1 ? u->rd_cap - u->rd_len - 1 : 0, n = a1 < room ? a1 : room;
            if (n) { copy_from_user(u->rd_out + u->rd_len, a0, n); u->rd_len += (uint32_t)n; }
            u->rd_out[u->rd_len] = 0;
            return (int64_t)a1;
        }
        {   /* se copia a un buffer del kernel con SMAP abierto el minimo tiempo, y se imprime ya cerrado */
            char buf[256];
            const char *s = (const char *)a0;
            for (uint64_t off = 0; off < a1; off += sizeof(buf)) {
                uint64_t n = a1 - off < sizeof(buf) ? a1 - off : sizeof(buf);
                if (smap_on) __asm__ volatile ("stac" ::: "memory");
                for (uint64_t i = 0; i < n; ++i) buf[i] = s[off + i];
                if (smap_on) __asm__ volatile ("clac" ::: "memory");
                arch_interrupts_enable();
                for (uint64_t i = 0; i < n; ++i) kputc(buf[i]);
                arch_interrupts_disable();
            }
        }
        return (int64_t)a1;
    case SYS_YIELD:
        thread_yield();
        return 0;
    case SYS_SLEEP:
        thread_sleep((uint32_t)(a0 > 10000 ? 10000 : a0));
        return 0;
    case SYS_GETPID:
        return (int64_t)usermode_cur->tid;
    case SYS_NET_DNS:   return sys_dns(a0, a1, a2);         /* PARCHE 142 */
    case SYS_TCP_OPEN:  return sys_tcp_open(a0, a1);
    case SYS_TCP_POLL:  return sys_tcp_poll(a0);
    case SYS_TCP_SEND:  return sys_tcp_send(a0, a1, a2);
    case SYS_TCP_RECV:  return sys_tcp_recv(a0, a1, a2);
    case SYS_TCP_CLOSE: return sys_tcp_close(a0);
    case SYS_WEB_WAIT:                                      /* SOMA_202: canal del navegador aislado (web_r3.c) */
        usermode_cur->nsys = 0;                             /* proceso de larga vida: la cuota es por espera, no de por vida */
        return web_r3_sys_wait(usermode_cur_index(), (int64_t)a0);
    case SYS_WEB_POST:  return web_r3_sys_post(usermode_cur_index(), a0, a1, a2);
    case SYS_WEB_FS:    return web_r3_sys_fs(usermode_cur_index(), a0);
    case SYS_SVC_NEXT:                                      /* PARCHE 139: servicio persistente (sandbox.c) */
        return sbx_svc_next(usermode_cur_index(), (int64_t)a0);
    case SYS_OPEN:    return sys_open(a0, a1);
    case SYS_READ:    return sys_read(a0, a1, a2);
    case SYS_SEEK:    return sys_seek(a0, (int64_t)a1, a2);
    case SYS_CLOSE:   { struct ufd *f = get_fd((int64_t)a0); if (!f) return E_BADF; f->used = 0; return 0; }
    case SYS_FSIZE:   return sys_fsize(a0);
    case SYS_GETARGS: return sys_getargs(a0, a1);
    case SYS_SOMA_CON: return sys_soma_con(a0, a1, a2);       /* SOMA_237_PYTHON */
    case SYS_SOMA_VFS: return sys_soma_vfs(a0, a1);
    case SYS_AUDIO_OPEN: {
        int r = audio_sys_open((uint32_t)a0, usermode_cur->tid);
        if (r == 0) usermode_cur->audio = 1;
        return r;
    }
    case SYS_AUDIO_WRITE: return sys_audio_write(a0, a1);
    case SYS_AUDIO_STATUS: {
        if (!usermode_cur->audio) return E_BADF;
        if (!user_range(a0, 24, 1)) return E_FAULT;
        uint64_t st[3]; int r = audio_sys_status(st);
        if (r) return r;
        copy_to_user(a0, st, 24);
        return 0;
    }
    case SYS_UPTIME_MS: return (int64_t)timer_get_uptime_ms();
    case SYS_FB_OPEN: {
        if (usermode_cur->fb) return SOMA_EBUSY;
        int r = fb_user_begin((uint32_t)a0, (uint32_t)a1);
        if (r == 0) usermode_cur->fb = 1;
        return r;
    }
    case SYS_FB_INFO: {
        if (!usermode_cur->fb) return E_BADF;
        if (!user_range(a0, 12, 1)) return E_FAULT;
        uint32_t info[3] = { fb_get_width(), fb_get_height(), fb_get_width() * 4 };
        copy_to_user(a0, info, 12);
        return 0;
    }
    case SYS_FB_BLIT: return sys_fb_blit(a0, a1, a2);
    case SYS_FB_CLOSE:
        if (!usermode_cur->fb) return E_BADF;
        fb_user_end(); usermode_cur->fb = 0;
        return 0;
    case SYS_AUDIO_CLOSE:
        if (!usermode_cur->audio) return E_BADF;
        audio_sys_close(a0 ? 1 : 0);
        usermode_cur->audio = 0;
        return 0;
    default:
        return SOMA_ENOSYS;
    }
}

int usermode_active(void) { return usermode_cur != 0; }
/* PARCHE 139 */
int usermode_cur_index(void) { return usermode_cur ? (int)(usermode_cur - procs) : -1; }
void usermode_set_deadline_cur(uint64_t abs_ms) { if (usermode_cur) usermode_cur->deadline_ms = abs_ms; }
void usermode_stop(int idx) { if (idx >= 0 && idx < USER_MAX_PROCS && procs[idx].state == 1) procs[idx].deadline_ms = 0; }   /* SOMA_237: muere en su proximo tick en ring 3 */
int usermode_proc_alive(int idx) { return idx >= 0 && idx < USER_MAX_PROCS && (procs[idx].state == 1 || procs[idx].state == 3); }
uint32_t usermode_proc_tid(int idx) { return (idx >= 0 && idx < USER_MAX_PROCS && procs[idx].state == 1) ? procs[idx].tid : 0; }
uint64_t usermode_proc_as(int idx) { return (idx >= 0 && idx < USER_MAX_PROCS && procs[idx].state == 1) ? procs[idx].as : 0; }

/* PARCHE 138: fallo de pagina en ring 3 dentro de una region bajo demanda -> se reserva un marco a cero y se reintenta.
 * Se ejecuta con IF=0 en el manejador de excepcion; el fallo vino de ring 3, asi que el kernel no tenia nada a medias
 * (igual que usermode_on_tick). Fuera de region, pagina ya presente (violacion de permisos), limite de la region o
 * reserva del sistema agotada -> 0 (el proceso muere; con la reserva o el limite, con USER_EXIT_NOMEM). */
int usermode_demand_fault(uint64_t cr2, uint64_t error_code)
{
    struct uproc *up = usermode_cur;
    if (!up || (error_code & 1)) return 0;                     /* bit 0 = pagina presente: es una violacion, no falta */
    for (int i = 0; i < up->nregions; ++i) {
        struct uregion *r = up->regions[i];
        if (!r || !r->demand || cr2 < r->va || cr2 >= r->va + (uint64_t)r->npages * 4096) continue;
        uint32_t k = (uint32_t)((cr2 - r->va) >> 12);
        if (r->frames[k]) return 0;
        size_t fr, us, tot; pmm_get_stats(&fr, &us, &tot);
        if (fr <= up->reserve_frames) goto nomem;
        uint64_t va = r->va + (uint64_t)k * 4096;
        if (r->demand == 2) {                                   /* SOMA_241_VOZ: marco de la reserva ALTA (no accesible por identidad) */
            uintptr_t f = pmm_alloc_frame_high();
            if (!f) goto nomem;
            if (vmm_as_map(up->as, va, f, r->flags) != 0) { pmm_free_frame(f); goto nomem; }
            r->frames[k] = f; r->used++;
            __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
            /* se pone a cero a traves de la propia direccion de usuario (CR3 es el del proceso: el fallo vino de ring 3) */
            if (smap_on) __asm__ volatile ("stac" ::: "memory");
            uint64_t *z = (uint64_t *)va; for (int q = 0; q < 512; ++q) z[q] = 0;
            if (smap_on) __asm__ volatile ("clac" ::: "memory");
            return 1;
        }
        uintptr_t f = pmm_alloc_frame();
        if (!f) goto nomem;
        uint8_t *p = (uint8_t *)f; for (size_t b = 0; b < 4096; ++b) p[b] = 0;
        if (vmm_as_map(up->as, va, f, r->flags) != 0) { pmm_free_frame(f); goto nomem; }
        r->frames[k] = f; r->used++;
        __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
        return 1;
    }
    return 0;
nomem:
    kprint("[USER] proceso terminado: su memoria bajo demanda no puede crecer (reserva del sistema)\n");
    up->exit_code = USER_EXIT_NOMEM;
    usermode_leave(USER_EXIT_NOMEM);
}

void usermode_on_fault(uint64_t vector, uint64_t rip, uint64_t cr2)
{
    kprint("[USER] proceso terminado por excepcion de CPU: vector ");
    kprint_dec((uint32_t)vector);
    kprint(" rip=0x"); kprint_hex32((uint32_t)(rip >> 32)); kprint_hex32((uint32_t)rip);
    if (vector == 14) { kprint(" cr2=0x"); kprint_hex32((uint32_t)(cr2 >> 32)); kprint_hex32((uint32_t)cr2); }
    kprint("\n");
    usermode_cur->exit_code = USER_EXIT_FAULT_BASE - (int64_t)vector;
    usermode_leave(USER_EXIT_FAULT_BASE - (int64_t)vector);
}

/* IRQ de reloj. Solo se actua si se interrumpio ring 3: ahi el kernel no tiene cerrojos ni estructuras a medias,
 * asi que ceder la CPU es seguro (el resto del kernel sigue siendo cooperativo). */
void usermode_on_tick(uint64_t now_ms, uint64_t cs)
{
    struct uproc *up = usermode_cur;
    if (!up || (cs & 3) != 3) return;
    if (now_ms >= up->deadline_ms) {
        kprint("[USER] proceso terminado: supero el limite de tiempo\n");
        up->exit_code = USER_EXIT_TIMEOUT;
        usermode_leave(USER_EXIT_TIMEOUT);
    }
    if (now_ms - slice_start_ms >= USER_SLICE_MS) thread_yield();
}

/* ---- SOMA_PARCHE_017_AS: cada proceso tiene su CR3; los hilos de kernel corren siempre con el del kernel ---- */
void arch_thread_switch(struct tcb *prev, struct tcb *next)
{
    /* SOMA_PARCHE_021_FPU: el kernel no usa SSE/x87 (-mgeneral-regs-only) y las interrupciones tampoco, asi que solo
     * los procesos de usuario tienen estado que proteger. Se guarda al salir y se restaura al entrar (sin
     * conmutacion perezosa: es mas simple y los procesos son pocos). */
    struct uproc *pu = prev ? (struct uproc *)prev->uproc : 0;
    struct uproc *nu = next ? (struct uproc *)next->uproc : 0;
    if (xsave_on) {                                            /* SOMA_241_VOZ: con AVX (mascara x87|SSE|AVX en EDX:EAX) */
        if (pu) __asm__ volatile ("xsave64 %0" : "=m"(*(uint8_t (*)[USER_FPU_BYTES])pu->fpu) : "a"(7u), "d"(0u) : "memory");
        if (nu) __asm__ volatile ("xrstor64 %0" : : "m"(*(uint8_t (*)[USER_FPU_BYTES])nu->fpu), "a"(7u), "d"(0u) : "memory");
    } else {
        if (pu) __asm__ volatile ("fxsave64 %0" : "=m"(*(uint8_t (*)[512])pu->fpu) : : "memory");
        if (nu) __asm__ volatile ("fxrstor64 %0" : : "m"(*(uint8_t (*)[512])nu->fpu) : "memory");
    }
    if (nu) {
        tss_set_rsp0(nu->kstack);
        vmm_load_as(nu->as);
    } else {
        vmm_load_as(vmm_kernel_as());
    }
    usermode_cur = nu;
    slice_start_ms = timer_get_uptime_ms();
}

static void free_pages(struct uproc *up)
{
    for (int i = 0; i < up->npages; ++i) if (up->pages[i].frame) { pmm_free_frame(up->pages[i].frame); up->pages[i].frame = 0; }
    up->npages = 0;
}

void arch_thread_cleanup(struct tcb *t)
{
    struct uproc *up = (struct uproc *)t->uproc;
    if (!up || up->state == 0 || up->tid != t->tid) return;    /* ranura ya reutilizada: no es de este hilo */
    if (!up->finished) up->exit_code = USER_EXIT_KILLED;       /* matado desde fuera (thread_kill) */
    if (up->audio) { audio_sys_owner_died(up->tid); up->audio = 0; }
    if (up->fb) { fb_user_end(); up->fb = 0; }
    smp_user_kill_owner(up->tid);                              /* SOMA_244_HILOS: antes de destruir sus tablas de paginas */
    free_pages(up);
    up->nregions = 0;                                          /* PARCHE 138: regiones prestadas: las libera su dueno (sandbox.c) */
    vmm_as_destroy(up->as); up->as = 0;                        /* nunca esta cargado: este hilo ya no es el actual */
    up->end_ms = timer_get_uptime_ms();
    up->state = 2;
}

static void proc_thread_main(void *arg)
{
    struct uproc *up = (struct uproc *)arg;
    up->start_ms = timer_get_uptime_ms();
    (void)usermode_enter(up, up->entry, up->stack_top);
    up->finished = 1;
    if (up->audio) { audio_sys_close(0); up->audio = 0; }       /* salio con el audio abierto (exit, fallo o plazo) */
    if (up->fb) { fb_user_end(); up->fb = 0; }                   /* y con la pantalla */
    /* vuelve aqui al terminar (exit, fallo o plazo); el codigo ya esta en up->exit_code */
}

/* ---- construccion de procesos: las paginas se reservan directamente en la ranura (un programa de hasta 4 MiB
 * son 1024 paginas: no caben listas temporales en la pila del kernel) ---- */
static struct uproc *slot_begin(void)
{
    for (int i = 0; i < USER_MAX_PROCS; ++i)
        if (procs[i].state == 0) {
            struct uproc *up = &procs[i];
            up->state = 3;                                   /* reservada, construyendose */
            up->npages = 0; up->as = 0; up->tid = 0; up->exit_code = 0; up->finished = 0;
            up->start_ms = up->end_ms = 0; up->user_rsp = 0; up->saved_rsp = 0;
            for (int k = 0; k < USER_MAX_FDS; ++k) up->fds[k].used = 0;
            up->args[0] = 0; up->name[0] = 0; up->audio = 0; up->fb = 0;
            up->sysmask = USER_SYS_DEFAULT; up->reserve_frames = 0; up->nregions = 0;   /* PARCHE 138 */
            up->rd_out = 0; up->rd_cap = up->rd_len = 0; up->rd_in = 0;                 /* SOMA_239 */
            up->may_thr = 0;                                                            /* SOMA_244 */
            for (int k = 0; k < USER_MAX_REGIONS; ++k) up->regions[k] = 0;
            /* estado inicial de la FPU: FCW=0x037F (todas las excepciones enmascaradas, doble extendida), MXCSR=0x1F80
             * (idem SSE, redondeo al mas cercano), registros vacios: igual que tras FNINIT */
            for (int k = 0; k < USER_FPU_BYTES; ++k) up->fpu[k] = 0;   /* SOMA_241: cabecera XSAVE a cero = estado inicial de AVX */
            up->fpu[0] = 0x7F; up->fpu[1] = 0x03;
            up->fpu[24] = 0x80; up->fpu[25] = 0x1F;
            return up;
        }
    return 0;
}

static void free_pages(struct uproc *up);
static void slot_abort(struct uproc *up)
{
    up->nregions = 0;                                                /* PARCHE 138: prestadas: las libera su dueno */
    free_pages(up);
    if (up->as) { vmm_as_destroy(up->as); up->as = 0; }
    up->state = 0;
}

static int add_page(struct uproc *up, uint64_t va, uint64_t flags, uint8_t **page_out)
{
    if (up->npages >= USER_MAX_UPAGES) return -1;
    uintptr_t f = pmm_alloc_frame();
    if (!f) return -1;
    uint8_t *p = (uint8_t *)f;
    for (size_t k = 0; k < 4096; ++k) p[k] = 0;
    up->pages[up->npages].va = va; up->pages[up->npages].frame = f; up->pages[up->npages].flags = flags;
    up->npages++;
    if (page_out) *page_out = p;
    return 0;
}

static int add_stack(struct uproc *up)
{
    for (int i = 0; i < USER_STACK_PAGES; ++i)
        if (add_page(up, USER_STACK_VA + (uint64_t)i * 4096, VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX, 0) != 0) return -1;
    return 0;
}

/* Crea la PML4, mapea las paginas ya reservadas y lanza el hilo. Devuelve el indice o <0 (y libera todo). */
static int slot_launch(struct uproc *up, const char *name, uint64_t entry, uint32_t max_ms, const char *args)
{
    int idx = (int)(up - procs);
    up->as = vmm_as_create();
    int ok = up->as != 0;
    for (int i = 0; i < up->npages && ok; ++i)
        if (vmm_as_map(up->as, up->pages[i].va, up->pages[i].frame, up->pages[i].flags) != 0) ok = 0;
    for (int i = 0; i < up->nregions && ok; ++i) {                 /* PARCHE 138: regiones prestadas ya reservadas */
        struct uregion *r = up->regions[i];
        for (uint32_t k = 0; k < r->npages && ok; ++k)
            if (r->frames[k] && vmm_as_map(up->as, r->va + (uint64_t)k * 4096, r->frames[k], r->flags) != 0) ok = 0;
    }
    if (!ok) { slot_abort(up); return -5; }

    up->nsys = 0; up->wbytes = 0;                             /* PARCHE 126 */
    up->entry = entry; up->stack_top = USER_STACK_VA + (uint64_t)USER_STACK_PAGES * 4096;
    up->deadline_ms = timer_get_uptime_ms() + max_ms;
    up->name[0] = 0;
    for (int i = 0; i < 15 && name && name[i]; ++i) { up->name[i] = name[i]; up->name[i + 1] = 0; }
    up->args[0] = 0;
    for (int i = 0; i < USER_ARGS_MAX - 1 && args && args[i]; ++i) { up->args[i] = args[i]; up->args[i + 1] = 0; }

    struct tcb *t = thread_create(name ? name : "uproc", proc_thread_main, up);
    if (!t) { slot_abort(up); return -4; }
    up->tid = t->tid;
    up->kstack = (t->kstack_top - USER_KSTACK_RESERVE) & ~0xFULL;
    up->state = 1;
    t->uproc = up;           /* antes de que corra por primera vez: el planificador es cooperativo */
    return idx;
}

int usermode_spawn(const char *name, const void *code, size_t len, uint32_t max_ms)
{
    if (!code || len == 0 || len > 4096) return -1;
    usermode_init();
    struct uproc *up = slot_begin();
    if (!up) return -2;
    uint8_t *pg;
    if (add_page(up, USER_CODE_VA, VMM_FLAG_PRESENT | VMM_FLAG_USER, &pg) != 0) { slot_abort(up); return -3; }   /* R+X */
    for (size_t i = 0; i < len; ++i) pg[i] = ((const uint8_t *)code)[i];
    if (add_stack(up) != 0) { slot_abort(up); return -3; }
    return slot_launch(up, name, USER_CODE_VA, max_ms, 0);
}

/* Carga un ejecutable ELF validado. *err recibe el codigo de elf_parse (o 0). */
static int exec_common(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, const struct uexec_spec *sp, int *err);
int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err)
{
    return exec_common(name, img, len, max_ms, args, 0, err);
}
/* SOMA_237_PYTHON: con argumentos Y con un filtro de llamadas propio (el interprete de Python: lo de siempre + SYS_SOMA_CON/VFS) */
int usermode_exec_caps(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, uint32_t sysmask, int *err)
{
    struct uexec_spec sp;
    uint8_t *z = (uint8_t *)&sp; for (size_t i = 0; i < sizeof sp; ++i) z[i] = 0;
    sp.sysmask = sysmask;
    return exec_common(name, img, len, max_ms, args, &sp, err);
}

/* PARCHE 138: igual que usermode_exec, con filtro de syscalls y regiones prestadas. Las regiones deben estar fuera del
 * codigo y la pila, alineadas, sin solaparse y sin W+X (si no, -11). */
int usermode_exec_ex(const char *name, const void *img, size_t len, uint32_t max_ms, const struct uexec_spec *sp, int *err)
{
    if (!sp || sp->nregions < 0 || sp->nregions > USER_MAX_REGIONS) return -11;
    for (int i = 0; i < sp->nregions; ++i) {
        const struct uregion *r = sp->regions[i];
        if (!r || !r->frames || r->npages == 0 || (r->va & 0xFFF)) return -11;
        uint64_t a = r->va, b = r->va + (uint64_t)r->npages * 4096;
        if (a < USER_STACK_VA + (uint64_t)USER_STACK_PAGES * 4096 || b > USER_BASE + 0x8000000000ULL || b < a) return -11;
        if ((r->flags & VMM_FLAG_WRITABLE) && !(r->flags & VMM_FLAG_NX)) return -11;
        for (int j = 0; j < i; ++j) {
            const struct uregion *q = sp->regions[j];
            if (a < q->va + (uint64_t)q->npages * 4096 && q->va < b) return -11;
        }
    }
    return exec_common(name, img, len, max_ms, 0, sp, err);
}

/* SOMA_241_VOZ: con argumentos, filtro de llamadas y UNA region (el monton grande de voz). La region es del llamante. */
int usermode_exec_region(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, uint32_t sysmask,
                         struct uregion *r, int *err)
{
    if (!r || !r->frames || r->npages == 0 || (r->va & 0xFFF)) return -11;
    uint64_t a = r->va, b = r->va + (uint64_t)r->npages * 4096;
    if (a < USER_BASE + USER_WINDOW || b > USER_BASE + 0x8000000000ULL || b < a) return -11;
    if ((r->flags & VMM_FLAG_WRITABLE) && !(r->flags & VMM_FLAG_NX)) return -11;
    struct uexec_spec sp;
    uint8_t *z = (uint8_t *)&sp; for (size_t i = 0; i < sizeof sp; ++i) z[i] = 0;
    sp.sysmask = sysmask;
    sp.regions[0] = r; sp.nregions = 1;
    sp.reserve_frames = 16384;                                     /* la region no crece si al sistema le quedarian menos de 64 MiB */
    int idx = exec_common(name, img, len, max_ms, args, &sp, err);
    if (idx >= 0) procs[idx].may_thr = 1;                          /* SOMA_244_HILOS: aun no ha corrido (planificador cooperativo) */
    return idx;
}

static int exec_common(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, const struct uexec_spec *sp, int *err)
{
    static const struct elf_limits lim = { USER_BASE, USER_CODE_AREA, USER_CODE_MAX_PAGES };
    struct elf_image im;
    int r = elf_parse((const uint8_t *)img, len, &lim, &im);
    if (err) *err = r;
    if (r != ELF_OK) return -10;
    usermode_init();

    struct uproc *up = slot_begin();
    if (!up) return -2;
    for (int si = 0; si < im.nsegs; ++si) {
        const struct elf_seg *s = &im.seg[si];
        uint64_t first = s->vaddr & ~0xFFFULL, last = (s->vaddr + s->memsz - 1) & ~0xFFFULL;
        uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
        if (s->flags & ELF_PF_W) flags |= VMM_FLAG_WRITABLE;
        if (!(s->flags & ELF_PF_X)) flags |= VMM_FLAG_NX;
        for (uint64_t va = first; va <= last; va += 4096) {
            uint8_t *dst;
            if (add_page(up, va, flags, &dst) != 0) { slot_abort(up); return -3; }
            /* bytes del archivo que caen en esta pagina */
            uint64_t lo = va > s->vaddr ? va : s->vaddr;                        /* primer byte de la pagina dentro del segmento */
            uint64_t hi = va + 4096 < s->vaddr + s->filesz ? va + 4096 : s->vaddr + s->filesz;   /* fin dentro del archivo */
            for (uint64_t addr = lo; addr < hi; ++addr)
                dst[addr - va] = ((const uint8_t *)img)[s->offset + (addr - s->vaddr)];
        }
    }
    if (add_stack(up) != 0) { slot_abort(up); return -3; }
    if (sp) {                                                       /* PARCHE 138 */
        up->sysmask = sp->sysmask ? sp->sysmask : USER_SYS_DEFAULT;
        up->reserve_frames = sp->reserve_frames;
        up->nregions = sp->nregions;
        for (int i = 0; i < sp->nregions; ++i) up->regions[i] = sp->regions[i];
    }
    return slot_launch(up, name, im.entry, max_ms, args);
}

int64_t usermode_wait(int idx)
{
    if (idx < 0 || idx >= USER_MAX_PROCS || procs[idx].state == 0) return -3000;
    struct uproc *up = &procs[idx];
    while (up->state != 2) thread_sleep(1);
    int64_t code = up->exit_code;
    up->state = 0;
    return code;
}

int64_t usermode_run_code(const void *code, size_t len, uint32_t max_ms)
{
    int idx = usermode_spawn("uproc", code, len, max_ms);
    if (idx < 0) return -3000 + idx;
    return usermode_wait(idx);
}

/* Lista de procesos (tambien libera los terminados en segundo plano). */
int usermode_format_table(char *out, uint32_t max, int reap)
{
    uint32_t pos = 0;
    #define PUTS(str) do { const char *_s = (str); while (*_s && pos + 1 < max) out[pos++] = *_s++; } while (0)
    #define PUTN(v) do { char _b[24]; int _i = 23; uint64_t _v = (uint64_t)(v); _b[_i] = 0; if (!_v) _b[--_i] = '0'; while (_v) { _b[--_i] = (char)('0' + _v % 10); _v /= 10; } PUTS(_b + _i); } while (0)
    int shown = 0;
    PUTS("PROCESOS DE USUARIO\n IDX  TID  Nombre           Estado      Codigo   ms\n");
    for (int i = 0; i < USER_MAX_PROCS; ++i) {
        struct uproc *up = &procs[i];
        if (up->state == 0 || up->state == 3) continue;
        shown++;
        PUTS(" "); PUTN(i); PUTS("    "); PUTN(up->tid); PUTS("    "); PUTS(up->name); PUTS("          ");
        if (up->state == 1) { PUTS("vivo       -"); PUTS("        "); PUTN(timer_get_uptime_ms() - up->start_ms); }
        else {
            PUTS("terminado  ");
            if (up->exit_code < 0) { PUTS("-"); PUTN(-up->exit_code); } else PUTN(up->exit_code);
            PUTS("      "); PUTN(up->end_ms - up->start_ms);
            if (reap) up->state = 0;
        }
        PUTS("\n");
    }
    if (!shown) PUTS(" (ninguno)\n");
    out[pos < max ? pos : max - 1] = 0;
    #undef PUTS
    #undef PUTN
    return (int)pos;
}

/* Los programas ELF que lleva el kernel (compilados desde src/user/) se copian a /bin/ en cada arranque. */
extern const uint8_t _binary_hello_elf_start[], _binary_hello_elf_end[];
extern const uint8_t _binary_fault_elf_start[], _binary_fault_elf_end[];
extern const uint8_t _binary_ticker_elf_start[], _binary_ticker_elf_end[];
extern const uint8_t _binary_sumfile_elf_start[], _binary_sumfile_elf_end[];
extern const uint8_t _binary_wavplay_elf_start[], _binary_wavplay_elf_end[];
extern const uint8_t _binary_ssetest_elf_start[], _binary_ssetest_elf_end[];
extern const uint8_t _binary_fbtest_elf_start[], _binary_fbtest_elf_end[];
extern const uint8_t _binary_sysfuzz_elf_start[], _binary_sysfuzz_elf_end[];

void usermode_install_programs(void)
{
    static const struct { const char *path; const uint8_t *a, *b; } progs[] = {
        { "/bin/hello.elf",  _binary_hello_elf_start,  _binary_hello_elf_end },
        { "/bin/fault.elf",  _binary_fault_elf_start,  _binary_fault_elf_end },
        { "/bin/ticker.elf", _binary_ticker_elf_start, _binary_ticker_elf_end },
        { "/bin/sumfile.elf", _binary_sumfile_elf_start, _binary_sumfile_elf_end },
        { "/bin/wavplay.elf", _binary_wavplay_elf_start, _binary_wavplay_elf_end },
        { "/bin/ssetest.elf", _binary_ssetest_elf_start, _binary_ssetest_elf_end },
        { "/bin/fbtest.elf", _binary_fbtest_elf_start, _binary_fbtest_elf_end },
        { "/bin/sysfuzz.elf", _binary_sysfuzz_elf_start, _binary_sysfuzz_elf_end },
    };
    for (size_t i = 0; i < sizeof(progs) / sizeof(progs[0]); ++i)
        vfs_write(progs[i].path, (const char *)progs[i].a, (uint32_t)(progs[i].b - progs[i].a));
}

/* ------------------------------------------------------------------ programas de prueba */
__asm__(
".section .rodata\n"
".global uprog_ok_start\n.global uprog_ok_end\n"
"uprog_ok_start:\n"
"    movq $1, %rax\n movq $0x100000, %rdi\n movq $4, %rsi\n syscall\n"     /* write con puntero del kernel */
"    movq %rax, %r12\n"
"    leaq 2f(%rip), %rdi\n movq $(3f-2f), %rsi\n movq $1, %rax\n syscall\n"  /* write valido */
"    movw %cs, %ax\n andq $3, %rax\n"                                         /* CPL */
"    leaq 40(%rax), %rdi\n"                                                   /* 40 + CPL */
"    cmpq $-14, %r12\n je 1f\n movq $1, %rdi\n"   /* PARCHE 138: SYS_WRITE devuelve SOMA_EFAULT (-14) desde el parche 121; la prueba seguia esperando -1 */
"1:  xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"2:  .ascii \"[USER] hola desde ring 3 (CPL=3)\\n\"\n"
"3:\n"
"uprog_ok_end:\n"
".global uprog_pf_start\n.global uprog_pf_end\n"
"uprog_pf_start:\n movq $0x100000, %rax\n movq (%rax), %rbx\n jmp .\nuprog_pf_end:\n"
".global uprog_gp_start\n.global uprog_gp_end\n"
"uprog_gp_start:\n hlt\n jmp .\nuprog_gp_end:\n"
".global uprog_loop_start\n.global uprog_loop_end\n"
"uprog_loop_start:\n jmp .\nuprog_loop_end:\n"
".global uprog_b_start\n.global uprog_b_end\n"
"uprog_b_start:\n"
"    leaq 2f(%rip), %rdi\n movq $(3f-2f), %rsi\n movq $1, %rax\n syscall\n"
"    movq $30, %rdi\n movq $3, %rax\n syscall\n"                  /* sleep(30 ms): solo despierta si el reloj quita la CPU al que gira */
"    movq $7, %rdi\n xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"2:  .ascii \"[USER] B: vivo mientras otro proceso gira\\n\"\n"
"3:\n"
"uprog_b_end:\n"
/* aislamiento: ambos usan la MISMA direccion virtual de pila; cada uno escribe su valor, duerme y lo relee */
".macro ISO name, val\n"
".global uprog_\\name\\()_start\n.global uprog_\\name\\()_end\n"
"uprog_\\name\\()_start:\n"
"    movabsq $0x8001000000, %rbx\n movq $\\val, (%rbx)\n"
"    movq $60, %rdi\n movq $3, %rax\n syscall\n"
"    movq (%rbx), %rdi\n xorq %rax, %rax\n syscall\n"
"    jmp .\n"
"uprog_\\name\\()_end:\n"
".endm\n"
"ISO isoa, 0xAA\n"
"ISO isob, 0xBB\n"
".text\n"
);
extern const uint8_t uprog_b_start[], uprog_b_end[], uprog_isoa_start[], uprog_isoa_end[],
                     uprog_isob_start[], uprog_isob_end[];
extern const uint8_t uprog_ok_start[], uprog_ok_end[], uprog_pf_start[], uprog_pf_end[],
                     uprog_gp_start[], uprog_gp_end[], uprog_loop_start[], uprog_loop_end[];

static int expect(const char *name, int64_t got, int64_t want)
{
    kprint("  "); kprint(name); kprint(": ");
    if (got == want) { kprint("OK\n"); return 0; }
    kprint("FALLO (codigo "); kprint_dec((uint32_t)(got < 0 ? -got : got));
    kprint(got < 0 ? " negativo" : ""); kprint(", esperado ");
    kprint_dec((uint32_t)(want < 0 ? -want : want)); kprint(want < 0 ? " negativo)\n" : ")\n");
    return 1;
}

int usermode_selftest(void)
{
    int bad = 0;
    kprint("USERMODE: prueba de ring 3\n");
    bad += expect("syscalls + CPL=3 + puntero de kernel rechazado",
                  usermode_run_code(uprog_ok_start, (size_t)(uprog_ok_end - uprog_ok_start), 500), 43);
    bad += expect("lectura de memoria del kernel -> #PF, proceso muerto, kernel vivo",
                  usermode_run_code(uprog_pf_start, (size_t)(uprog_pf_end - uprog_pf_start), 500),
                  USER_EXIT_FAULT_BASE - 14);
    bad += expect("instruccion privilegiada (hlt) -> #GP, proceso muerto, kernel vivo",
                  usermode_run_code(uprog_gp_start, (size_t)(uprog_gp_end - uprog_gp_start), 500),
                  USER_EXIT_FAULT_BASE - 13);
    bad += expect("bucle infinito -> cortado por el plazo",
                  usermode_run_code(uprog_loop_start, (size_t)(uprog_loop_end - uprog_loop_start), 200),
                  USER_EXIT_TIMEOUT);

    /* ---- concurrencia (parche 015) ---- */
    #define LEN(n) ((size_t)(uprog_##n##_end - uprog_##n##_start))
    {   /* A gira sin ceder; B debe ejecutarse igualmente (preemption en ring 3) y terminar antes que A */
        int a = usermode_spawn("spin", uprog_loop_start, LEN(loop), 400);
        int b = usermode_spawn("B", uprog_b_start, LEN(b), 1000);
        uint64_t t0 = timer_get_uptime_ms();
        int64_t rb = usermode_wait(b);
        uint64_t tb = timer_get_uptime_ms() - t0;
        int64_t ra = usermode_wait(a);
        bad += expect("preemption: B termina (7) mientras A gira sin ceder", rb, 7);
        bad += expect("preemption: A cortado por plazo", ra, USER_EXIT_TIMEOUT);
        kprint("  B termino a los "); kprint_dec((uint32_t)tb); kprint(" ms (A gira hasta 400 ms)\n");
        if (tb > 250) { kprint("  FALLO: B tardo demasiado\n"); bad++; }
    }
    {   /* misma direccion virtual, memoria distinta */
        int a = usermode_spawn("isoA", uprog_isoa_start, LEN(isoa), 1000);
        int b = usermode_spawn("isoB", uprog_isob_start, LEN(isob), 1000);
        int64_t ra = usermode_wait(a), rb = usermode_wait(b);
        bad += expect("aislamiento: A relee su valor 0xAA (170)", ra, 0xAA);
        bad += expect("aislamiento: B relee su valor 0xBB (187)", rb, 0xBB);
    }
    {   /* un proceso muere por #PF mientras otro sigue vivo */
        int a = usermode_spawn("spin2", uprog_loop_start, LEN(loop), 300);
        int b = usermode_spawn("pf", uprog_pf_start, LEN(pf), 1000);
        int64_t rb = usermode_wait(b), ra = usermode_wait(a);
        bad += expect("fallo aislado: el que lee memoria del kernel muere (#PF)", rb, USER_EXIT_FAULT_BASE - 14);
        bad += expect("fallo aislado: el otro proceso sigue hasta su plazo", ra, USER_EXIT_TIMEOUT);
    }
    {   /* SOMA_PARCHE_017_AS: las paginas de usuario NO existen en el espacio de direcciones del kernel */
        int a = usermode_spawn("spin3", uprog_loop_start, LEN(loop), 300);
        thread_sleep(20);                           /* deja que arranque y quede cargado su CR3 al volver */
        bad += expect("aislamiento: PML4 del kernel sin entradas de usuario con un proceso vivo",
                      vmm_kernel_user_region_empty() ? 1 : 0, 1);
        bad += expect("aislamiento: el kernel no ve la pagina de codigo del proceso",
                      vmm_get_pte(USER_CODE_VA) == 0 ? 1 : 0, 1);
        (void)usermode_wait(a);
    }
    {   /* todo se libero: se puede repetir y no se pierden marcos */
        size_t f0, u0, t0m, f1, u1, t1m;
        pmm_get_stats(&f0, &u0, &t0m);
        int64_t r = usermode_run_code(uprog_ok_start, (size_t)(uprog_ok_end - uprog_ok_start), 500);
        thread_sleep(5);
        pmm_get_stats(&f1, &u1, &t1m);
        bad += expect("repetible tras liberar procesos", r, 43);
        kprint("  marcos libres antes/despues: "); kprint_dec((uint32_t)f0); kprint("/"); kprint_dec((uint32_t)f1); kprint("\n");
        if (f1 + 2 < f0) { kprint("  FALLO: fuga de marcos\n"); bad++; }
    }
    #undef LEN
    kprint(bad ? "USERMODE: FALLO\n" : "USERMODE: OK\n");
    return bad;
}
