#ifndef MYOS_USERMODE_H
#define MYOS_USERMODE_H

#include <stdint.h>
#include <stddef.h>

/* SOMA_PARCHE_014_RING3: ejecucion de codigo en anillo 3 (CPL=3).
 *
 * Mapa de usuario (PML4[1] = 512 GiB: cada proceso tiene su PROPIA tabla de paginas y la del kernel no tiene nada ahi):
 *   USER_BASE .. USER_BASE+USER_WINDOW
 * Llamadas al sistema (instruccion SYSCALL): rax = numero, rdi/rsi/rdx = argumentos, rax = resultado.
 * Se conservan rbx, rbp, r12-r15 y rsp; el resto (rcx, r11, rdi, rsi, rdx, r8-r10) se destruye.
 */
#define USER_BASE    0x8000000000ULL      /* PML4[1]: region de usuario, vacia en la PML4 del kernel (parche 017) */
/* SOMA_PARCHE_019_FILES: region de usuario ampliada para programas grandes.
 *   codigo + datos + bss : USER_BASE .. +12 MiB (USER_CODE_AREA, hasta USER_CODE_MAX_PAGES paginas)
 *   pila                 : USER_STACK_VA, USER_STACK_PAGES paginas (la pila crece hacia abajo desde su cima) */
#define USER_CODE_AREA      0xC00000ULL           /* 12 MiB (SOMA_202: eran 4; el motor del navegador con sus fuentes ocupa mas) */
#define USER_CODE_MAX_PAGES 3072
#define USER_WINDOW  0x2000000ULL                 /* 32 MiB: todo lo que un proceso puede tocar y pasar al kernel */
#define USER_CODE_VA USER_BASE
#define USER_STACK_VA (USER_BASE + 0x1000000ULL)

#define SYS_EXIT   0
#define SYS_WRITE  1   /* write(ptr, len) -> bytes escritos o -14 (EFAULT) */
#define SYS_YIELD  2
#define SYS_SLEEP  3   /* sleep(ms) */
#define SYS_GETPID 4   /* -> tid del hilo */
/* SOMA_PARCHE_019_FILES: ficheros del almacen BIGFS (solo lectura) y argumentos. Los errores son codigos negativos:
 * -14 puntero invalido, -9 descriptor invalido, -22 argumento invalido, -24 demasiados ficheros abiertos,
 * -13 ruta privada y los de BIGFS ya traducidos a la tabla unica de src/core/errno.h (-2 no existe, -21 es un directorio, -5 error de E/S...). */
#define SYS_OPEN    5   /* open(ruta_ptr, ruta_len, flags=0) -> fd >= 0 */
#define SYS_READ    6   /* read(fd, buf, len) -> bytes leidos (0 = fin); como mucho 64 KiB por llamada */
#define SYS_SEEK    7   /* seek(fd, desplazamiento, 0=desde el inicio | 1=desde la posicion | 2=desde el final) -> nueva posicion */
#define SYS_CLOSE   8   /* close(fd) */
#define SYS_FSIZE   9   /* fsize(fd) -> tamano en bytes */
#define SYS_GETARGS 10  /* getargs(buf, len) -> longitud de la cadena de argumentos (se copia con NUL) */
/* SOMA_PARCHE_020_AUDIO: salida de audio PCM 16 bits estereo (un solo flujo en todo el sistema).
 *   audio_open(frecuencia) -> 0 | -16 ocupado | -19 sin dispositivo | -22 frecuencia no soportada
 *   audio_write(buf, len)  -> bytes aceptados (bloquea hasta aceptarlos todos o 3 s sin avance); len multiplo de 4, <= 64 KiB
 *   audio_close(esperar)   -> 0 (esperar=1: deja que termine de sonar lo escrito) */
#define SYS_AUDIO_OPEN  11
#define SYS_AUDIO_WRITE 12
#define SYS_AUDIO_CLOSE 13
/* SOMA_PARCHE_021_FPU: pantalla en exclusiva (la GUI no dibuja mientras dura). Pixeles XRGB de 32 bits (0x00RRGGBB).
 *   fb_open(ancho, alto)           -> 0 | -16 ocupada | -19 sin pantalla grafica | -22 tamano no valido (ancho multiplo de 8, <= 1920x1080)
 *   fb_info(buf)                   -> 0; escribe 3 uint32: ancho, alto, bytes por fila
 *   fb_blit(buf, w<<16|h, x<<16|y) -> 0; copia un rectangulo w x h (fila a fila, w*4 bytes por fila) y lo presenta
 *   fb_close()                     -> 0; la GUI (o la consola de texto) recupera la pantalla
 * Se libera sola si el proceso termina. */
#define SYS_FB_OPEN  14
#define SYS_FB_INFO  15
#define SYS_FB_BLIT  16
#define SYS_FB_CLOSE 17
/* SOMA_PARCHE_022_VIDEO */
#define SYS_AUDIO_STATUS 18   /* audio_status(u64 out[3]) -> 0; out = {espacio libre FIFO en bytes, reloj de audio en us, huecos} */
#define SYS_UPTIME_MS    19   /* -> milisegundos desde el arranque */
/* PARCHE 139: servicios persistentes (solo los procesos lanzados por sandbox.c tienen este bit en su filtro).
 *   svc_next(resultado) -> 0 hay una peticion nueva en SBX_IN_VA | 1 terminar. Entrega la respuesta anterior (si la habia) y bloquea. */
#define SYS_SVC_NEXT     20
/* PARCHE 142: RED para procesos de ring 3, como CAPACIDAD: solo los procesos lanzados con estos bits en su filtro (servicio netsvc,
 * navegador aislado) pueden usarlas; los programas normales no (USER_SYS_DEFAULT). Cada proceso solo toca SUS conexiones (tcp_owner) y
 * como mucho USER_MAX_SOCKETS a la vez; al morir se cierran todas. Ninguna bloquea mas de unos ms salvo el DNS (hasta 4 s).
 *   net_dns(nombre, len, ip_out[4])  -> 0 | -2 no resuelve | -22 nombre no valido
 *   tcp_open(ip_be32, puerto)        -> descriptor | -16 demasiadas | -5 sin conexiones libres  (envia el SYN y vuelve)
 *   tcp_poll(d)                      -> 1 establecida | 0 pendiente | -5 fallida
 *   tcp_send(d, buf, len)            -> bytes enviados (len <= 64 KiB) | <0
 *   tcp_recv(d, buf, max)            -> bytes | 0 nada aun | -1 el otro extremo cerro | -5 error   (max <= 16 KiB)
 *   tcp_close(d)                     -> 0 */
#define SYS_NET_DNS      21
#define SYS_TCP_OPEN     22
#define SYS_TCP_POLL     23
#define SYS_TCP_SEND     24
#define SYS_TCP_RECV     25
#define SYS_TCP_CLOSE    26
#define USER_SYS_NET_MASK ((1u << SYS_NET_DNS) | (1u << SYS_TCP_OPEN) | (1u << SYS_TCP_POLL) | (1u << SYS_TCP_SEND) | (1u << SYS_TCP_RECV) | (1u << SYS_TCP_CLOSE))
#define USER_SYS_DEFAULT  0x000FFFFFu      /* syscalls 0..19: lo de siempre; red y canal del navegador, solo por capacidad. SOMA_192: sin la 20
                                            * (SYS_SVC_NEXT es solo de los servicios aislados, que la llevan en su propio filtro; a un programa normal le devolvia 1) */
/* SOMA_237_PYTHON: solo para el interprete de Python (orden "python"); ver sys_soma_con / sys_soma_vfs en usermode.c */
#define SYS_SOMA_CON     30
#define SYS_SOMA_VFS     31
#define USER_SYS_PYTHON  (USER_SYS_DEFAULT | (1u << SYS_SOMA_CON) | (1u << SYS_SOMA_VFS))
/* SOMA_244_HILOS: hilos de calculo en los nucleos secundarios (solo el proceso de voz; fuera del filtro de 32 bits) */
#define SYS_SOMA_THR     32
/* SOMA_329_IA_LOCAL: base de FS del proceso (como arch_prctl(ARCH_SET_FS) de Linux): fs(direccion) -> 0. Solo para los procesos con
 * hilos de calculo (voz, motor de IA): libstdc++ compilada con protector de pila lee su canario en %fs:0x28. El nucleo no usa FS. */
#define SYS_SOMA_FS      33
#define USER_MAX_SOCKETS  4
#define USER_MAX_FDS 8
#define USER_ARGS_MAX 128

/* Codigos de salida negativos de usermode_run_code() */
#define USER_EXIT_FAULT_BASE (-1000)   /* -1000 - numero de vector de la excepcion de CPU */
#define USER_EXIT_TIMEOUT    (-2000)   /* superó el limite de tiempo */
#define USER_EXIT_QUOTA      (-5000)   /* PARCHE 126: superó la cuota de llamadas al sistema */
#define USER_EXIT_DENIED     (-6000)   /* PARCHE 138: llamada al sistema no permitida por su filtro (procesos aislados) */
#define USER_EXIT_NOMEM      (-7000)   /* PARCHE 138: la region bajo demanda no pudo crecer (limite propio o reserva del sistema) */

/* PARCHE 138: regiones de memoria PRESTADAS a un proceso (servicios aislados, ver sandbox.h).
 * Los marcos los posee quien crea la region: el proceso solo los ve mapeados; al morir el proceso NO se liberan, para que el
 * kernel pueda leer el resultado. Con demand=1 las paginas se reservan al primer acceso (fallo de pagina) hasta npages. */
struct uregion {
    uint64_t  va;            /* alineada a 4 KiB */
    uint32_t  npages;
    uint32_t  used;          /* paginas ya reservadas (demand) */
    uint64_t  flags;         /* VMM_FLAG_* (siempre con USER; nunca W+X) */
    uint64_t *frames;        /* npages entradas; 0 = sin reservar todavia */
    int       demand;
};
#define USER_MAX_REGIONS 4
struct uexec_spec {          /* PARCHE 138: opciones de usermode_exec_ex */
    uint32_t        sysmask;           /* bit n = syscall n permitida (0 = las de siempre: todas) */
    struct uregion *regions[USER_MAX_REGIONS];
    int             nregions;
    uint32_t        reserve_frames;    /* las regiones bajo demanda no crecen si el PMM bajaria de esto */
    int             may_thr;           /* SOMA_329_IA_LOCAL: puede lanzar hilos de calculo y fijar su base de FS (como la voz) */
};
int usermode_exec_ex(const char *name, const void *img, size_t len, uint32_t max_ms, const struct uexec_spec *sp, int *err);
int usermode_demand_fault(uint64_t cr2, uint64_t error_code);   /* 1 = pagina servida (reintentar la instruccion) */
/* PARCHE 139: para la capa de servicios (sandbox.c) */
int      usermode_cur_index(void);                    /* indice del proceso actual o -1 */
void     usermode_set_deadline_cur(uint64_t abs_ms);  /* plazo absoluto del proceso actual */
void     usermode_stop(int idx);                      /* SOMA_237_PYTHON: lo para (como si se le acabara el tiempo) */
int      usermode_proc_alive(int idx);                /* 1 si sigue vivo (no terminado) */
uint64_t usermode_proc_as(int idx);                   /* su espacio de direcciones (0 si no hay) */
uint32_t usermode_proc_tid(int idx);                  /* tid de su hilo (0 si no hay) */

#define USER_MAX_PROCS 16   /* SOMA_326_TUNEL_TLS: antes 8; cada conexion TLS del correo y de YouTube es un proceso */
/* SOMA_241_VOZ: AVX en ring 3 y region grande para el reconocimiento de voz (ver src/arch/x86_64/voz.c) */
#define USER_FPU_BYTES 1024                       /* area FXSAVE (512) o XSAVE con AVX (832) */
#define USER_HEAP_VA   (USER_BASE + 0x40000000ULL)  /* 1 GiB por encima de la base: fuera de la ventana de codigo y pila */
void usermode_fpu_init(void);                     /* en el procesador de arranque, antes de smp_init */
int  usermode_avx2_ok(void);                      /* 1 si ring 3 puede usar AVX2/FMA/F16C */
int  usermode_exec_region(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, uint32_t sysmask,
                          struct uregion *r, int *err);
#define USER_STACK_PAGES 16     /* 64 KiB de pila de usuario */

/* SOMA_PARCHE_015_PROC / 017_AS: cada proceso es un hilo del planificador con su propia pila de kernel y su propio
 * espacio de direcciones (CR3 propio). Todos ven las MISMAS direcciones virtuales (USER_CODE_VA / USER_STACK_VA)
 * y un proceso nunca ve la memoria de otro. Con SMEP/SMAP (si la CPU los tiene) el kernel tampoco puede
 * ejecutar ni leer por error memoria de usuario.
 * El reloj puede quitar la CPU a un proceso SOLO mientras esta en ring 3 (el kernel sigue siendo cooperativo). */
int     usermode_spawn(const char *name, const void *code, size_t len, uint32_t max_ms);   /* >=0: indice; <0 error */
int64_t usermode_wait(int idx);
int64_t usermode_wait_cancel(int idx, const volatile int *cancel);   /* SOMA_317_DESCARGAS_AISLADAS: *cancel = 1 lo para */

/* SOMA_PARCHE_016_EXEC: carga un ejecutable ELF64 estatico (validado por elf_parse) como proceso nuevo.
 * Devuelve el indice o <0; *err recibe el codigo ELF_E_* si el archivo no es valido. */
int usermode_exec(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, int *err);
void usermode_redirect(int idx, char *out, uint32_t cap, const char *input);   /* SOMA_239: salida a un bufer y entrada fija */
int  usermode_python_window_take(void);                            /* SOMA_239: 1 si Python abrio la ventana (y lo olvida) */
void usermode_python_set_source(const char *src, uint32_t len);   /* SOMA_239: codigo para python -s / -x (0 = ninguno) */
int usermode_exec_caps(const char *name, const void *img, size_t len, uint32_t max_ms, const char *args, uint32_t sysmask, int *err);   /* SOMA_237 */
int usermode_format_table(char *out, uint32_t max, int reap);   /* lista de procesos; reap=1 libera los terminados */
void usermode_install_programs(void);                            /* copia los ELF del kernel a /bin/ */                                                             /* espera y libera */

/* Copia `code` en una pagina de usuario, lo ejecuta en ring 3 y devuelve el codigo de salida (spawn+wait). */
int64_t usermode_run_code(const void *code, size_t len, uint32_t max_ms);

/* Prueba integral (syscalls+CPL, #PF, #GP, plazo; despues concurrencia: preemption, aislamiento entre procesos, fallo de uno sin afectar a otro).
 * Devuelve 0 si todo se comporta como se espera. */
int usermode_selftest(void);
int usermode_oomtest_mem(void);   /* PARCHE 119: OOM en el monton y el sistema de archivos; 1 = todo OK */
int usermode_oomtest(void);   /* PARCHE 118: carga de procesos con memoria agotada en cada punto; 1 = todo OK */
int usermode_fuzz(int nproc, uint32_t iters, uint32_t seed);   /* PARCHE 117: fuzzing de syscalls desde ring 3; 1 = todo OK */

/* Ganchos que llama el manejador de interrupciones (idt.c) */
int  usermode_active(void);
void usermode_on_fault(uint64_t vector, uint64_t rip, uint64_t cr2);   /* no vuelve si estaba en ring 3 */
void usermode_on_tick(uint64_t now_ms, uint64_t cs);                   /* no vuelve si vencio el plazo */

#endif
