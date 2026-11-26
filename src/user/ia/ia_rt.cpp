// SOMA_329_IA_LOCAL: lo que el motor de IA (llama.cpp) necesita de C++ sin la biblioteca completa del sistema:
//   - new/delete sobre el malloc de voz_libc.c (la misma libc minima que la voz);
//   - EXCEPCIONES de verdad (llama.cpp las usa para los errores al cargar un modelo): libsupc++ y el desenrollador de libgcc
//     enlazados estaticamente; aqui se registra la tabla .eh_frame al arrancar y se dan los "globales" de las excepciones sin
//     almacenamiento local de hilo (SOMA no tiene TLS en ring 3);
//   - std::thread sin hilos (solo lo usa la cuantizacion, que SOMA no hace);
//   - el registro de backends de ggml con solo la CPU;
//   - el arranque del programa (pila propia, constructores globales, monton).
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <new>
#include <thread>
#include <memory>
#include <exception>
#include "../../core/ia_abi.h"

extern "C" void voz_flush(void);
extern "C" void voz_exit(int code) __attribute__((noreturn));
extern "C" int voz_map_heap(void * va, unsigned long size);
extern "C" void voz_heap_init(void * base, size_t size);
extern "C" int voz_pretouch;
extern "C" int voz_quiet_stderr;
extern "C" long ia_set_fs(void *);

[[noreturn]] static void die(const char * what, const char * detail)
{
    voz_quiet_stderr = 0;
    std::fprintf(stdout, "ia: error interno: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    voz_flush();
    voz_exit(70);
}

void * operator new(std::size_t n) { void * p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void * operator new[](std::size_t n) { return operator new(n); }
void * operator new(std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void * operator new[](std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void * operator new(std::size_t n, std::align_val_t a) { void * p = nullptr; if (posix_memalign(&p, (std::size_t)a, n ? n : 1)) throw std::bad_alloc(); return p; }
void * operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void * operator new(std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { void * p = nullptr; if (posix_memalign(&p, (std::size_t)a, n ? n : 1)) return nullptr; return p; }
void * operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { void * p = nullptr; if (posix_memalign(&p, (std::size_t)a, n ? n : 1)) return nullptr; return p; }
void operator delete(void * p) noexcept { std::free(p); }
void operator delete[](void * p) noexcept { std::free(p); }
void operator delete(void * p, std::size_t) noexcept { std::free(p); }
void operator delete[](void * p, std::size_t) noexcept { std::free(p); }
void operator delete(void * p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void * p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void * p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void * p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void * p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void * p, const std::nothrow_t &) noexcept { std::free(p); }

extern "C" void __cxa_pure_virtual() { die("llamada a funcion virtual pura", nullptr); }

// ---- excepciones sin TLS: un "globales" por hilo de calculo (los hilos de ggml no lanzan; el principal si)
struct cxa_eh_globals { void * caught; unsigned int uncaught; };
static cxa_eh_globals g_eh[1];
extern "C" cxa_eh_globals * __cxa_get_globals_fast() noexcept { return &g_eh[0]; }   // solo el hilo principal lanza
extern "C" cxa_eh_globals * __cxa_get_globals() noexcept { return __cxa_get_globals_fast(); }

// la tabla de desenrollado (.eh_frame, terminada en 0 por ia.ld) se registra al arrancar: no hay crtbegin ni dl_iterate_phdr
extern "C" void __register_frame_info(const void *, void *);
extern "C" char __eh_frame_start[];
extern "C" int dl_iterate_phdr(int (*)(void *, size_t, void *), void *) { return 0; }

namespace std {
// std::thread sin hilos: el cuerpo se ejecuta al crearlo (solo lo usa la cuantizacion de llama.cpp, que aqui no se hace)
thread::_State::~_State() = default;
void thread::_M_start_thread(unique_ptr<_State> st, void (*)()) { st->_M_run(); _M_id = id(); }
void thread::join() { _M_id = id(); }
void thread::detach() { _M_id = id(); }
unsigned int thread::hardware_concurrency() noexcept { return 1; }
}

// ggml: piezas opcionales que SOMA no compila
#include "ggml-backend-impl.h"
#include "ggml-cpu/tiled/tiled.h"
extern "C" {
bool ggml_backend_buffer_is_meta(ggml_backend_buffer_t) { return false; }
bool ggml_backend_is_meta(ggml_backend_t) { return false; }
void ggml_backend_meta_buffer_set_usage(ggml_backend_buffer_t, enum ggml_backend_buffer_usage) {}
ggml_backend_dev_t ggml_backend_meta_device(ggml_backend_dev_t *, size_t, ggml_backend_meta_get_split_state_t, void *) { return nullptr; }
size_t ggml_tiled_wdata_size(int, struct ggml_tensor *) { return 0; }
bool ggml_compute_forward_mul_mat_tiled(const struct ggml_compute_params *, struct ggml_tensor *) { return false; }
bool ggml_compute_forward_mul_mat_id_tiled(const struct ggml_compute_params *, struct ggml_tensor *, int64_t, int64_t, const int32_t *, char *) { return false; }
}

// ---- arranque
extern "C" { uint8_t ia_stack[4u << 20] __attribute__((aligned(64), used)); }
extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);
extern "C" unsigned long ia_heap_va(void);
int ia_main(int argc, char ** argv);
extern "C" long voz_getargs(char * buf, long n);
extern "C" [[noreturn]] void ia_cstart(void)
{
    static char * argv[32];
    int argc = 0; argv[argc++] = (char *)"ia";
    size_t heap = 0;
#ifdef IA_HOST
    static char args[512];
    long n = voz_getargs(args, (long)sizeof args - 1);
    if (n > 0) {
        char * s = args;
        while (*s && argc < 31) {
            while (*s == ' ') *s++ = 0;
            if (!*s) break;
            argv[argc++] = s;
            while (*s && *s != ' ') s++;
        }
    }
    heap = (size_t)2048 << 20;
    for (int i = 1; i + 1 < argc; i++) if (!std::strcmp(argv[i], "-H")) heap = (size_t)std::atoi(argv[i + 1]) << 20;
#else
    const volatile ia_ctl * c = (const volatile ia_ctl *)IA_SH_VA;
    heap = (size_t)c->monton;
    if (heap < (64u << 20)) heap = 64u << 20;
#endif
    {   // bloque de hilo para %fs (canario del protector de pila de libstdc++.a): [0] = el propio bloque, [5] = canario
        static uint64_t tcb[64] __attribute__((aligned(64)));
        uint64_t t; __asm__ volatile ("rdtsc; shlq $32, %%rdx; orq %%rdx, %%rax" : "=a"(t) : : "rdx");
        tcb[0] = (uint64_t)(uintptr_t)tcb; tcb[5] = (t * 0x9E3779B97F4A7C15ULL) & ~(uint64_t)0xFF;
        if (ia_set_fs(tcb) != 0) { std::printf("[ia] el nucleo no permite la base de FS (hace falta SOMA 329)\n"); voz_flush(); voz_exit(5); }
    }
    if (voz_map_heap((void *)ia_heap_va(), heap) != 0) { std::printf("[ia] sin memoria para el monton\n"); voz_flush(); voz_exit(4); }
    voz_pretouch = 1;                                  // los hilos de calculo no pueden provocar fallos de pagina
    voz_heap_init((void *)ia_heap_va(), heap);
    static char eh_obj[64] __attribute__((aligned(16)));
    __register_frame_info(__eh_frame_start, eh_obj);
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
    int r = ia_main(argc, argv);
    voz_flush();
    voz_exit(r);
}
extern "C" __attribute__((naked, section(".text._start"))) void _start(void)
{
    __asm__ volatile ("lea ia_stack+%c0(%%rip), %%rsp\n\tand $-64, %%rsp\n\tcall ia_cstart\n\tud2" : : "i"(sizeof ia_stack));
}
