// SOMA_241_VOZ: lo minimo de C++ que necesitan whisper.cpp y ggml sin la biblioteca de C++ completa:
// new/delete sobre el malloc de voz_libc.c, las funciones __throw_* de libstdc++ (sin excepciones: avisan y terminan),
// y std::thread SIN hilos: el trabajo de cada "hilo" se hace en el momento en el hilo que lo crea. whisper.cpp solo usa
// std::thread para repartir trabajos independientes (espectrograma, cabeceras de atencion), asi que el resultado es el mismo.
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <thread>
#include <memory>
#include <exception>

extern "C" void voz_flush(void);
extern "C" void voz_exit(int code) __attribute__((noreturn));

[[noreturn]] static void die(const char * what, const char * detail)
{
    extern int voz_quiet_stderr;
    voz_quiet_stderr = 0;
    std::fprintf(stdout, "voz: error interno: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    voz_flush();
    voz_exit(70);
}

void * operator new(std::size_t n) { void * p = std::malloc(n ? n : 1); if (!p) die("sin memoria", nullptr); return p; }
void * operator new[](std::size_t n) { return operator new(n); }
void * operator new(std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void * operator new[](std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void * operator new(std::size_t n, std::align_val_t a) { void * p = nullptr; if (posix_memalign(&p, (std::size_t)a, n ? n : 1)) die("sin memoria", nullptr); return p; }
void * operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void operator delete(void * p) noexcept { std::free(p); }
void operator delete[](void * p) noexcept { std::free(p); }
void operator delete(void * p, std::size_t) noexcept { std::free(p); }
void operator delete[](void * p, std::size_t) noexcept { std::free(p); }
void operator delete(void * p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void * p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void * p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void * p, std::size_t, std::align_val_t) noexcept { std::free(p); }

extern "C" void __cxa_pure_virtual() { die("llamada a funcion virtual pura", nullptr); }

namespace std {
void __throw_bad_alloc() { die("sin memoria", nullptr); }
void __throw_bad_array_new_length() { die("tamano de array no valido", nullptr); }
void __throw_bad_function_call() { die("std::function vacia", nullptr); }
void __throw_length_error(const char * s) { die("longitud", s); }
void __throw_logic_error(const char * s) { die("logica", s); }
void __throw_out_of_range(const char * s) { die("fuera de rango", s); }
void __throw_out_of_range_fmt(const char * s, ...) { die("fuera de rango", s); }
void __throw_invalid_argument(const char * s) { die("argumento no valido", s); }
void __throw_runtime_error(const char * s) { die("error", s); }
void __throw_system_error(int) { die("error del sistema", nullptr); }
void terminate() noexcept { die("terminate", nullptr); }
void __glibcxx_assert_fail(const char * f, int, const char * fn, const char * c) noexcept { (void)f; (void)fn; die("comprobacion de libstdc++", c); }

// std::thread sin hilos: el cuerpo se ejecuta al crearlo; join() no tiene nada que esperar
thread::_State::~_State() = default;
void thread::_M_start_thread(unique_ptr<_State> st, void (*)()) { st->_M_run(); _M_id = id(); }
void thread::join() { _M_id = id(); }
void thread::detach() { _M_id = id(); }
unsigned int thread::hardware_concurrency() noexcept { return 1; }
} // namespace std

void whisper_soma_fail(const char * msg) { die("whisper", msg); }

// ggml: piezas opcionales que SOMA no compila (backend "meta", multiplicacion por bloques "tiled", ANEForge de Apple)
#include "ggml-backend-impl.h"
#include "ggml-cpu/tiled/tiled.h"
#include "aneforge/whisper-aneforge.h"
extern "C" {
bool ggml_backend_buffer_is_meta(ggml_backend_buffer_t) { return false; }
void ggml_backend_meta_buffer_set_usage(ggml_backend_buffer_t, enum ggml_backend_buffer_usage) {}
size_t ggml_tiled_wdata_size(int, struct ggml_tensor *) { return 0; }
bool ggml_compute_forward_mul_mat_tiled(const struct ggml_compute_params *, struct ggml_tensor *) { return false; }
bool ggml_compute_forward_mul_mat_id_tiled(const struct ggml_compute_params *, struct ggml_tensor *, int64_t, int64_t, const int32_t *, char *) { return false; }
struct whisper_aneforge_context * whisper_aneforge_init(const char *) { return nullptr; }   // nunca: SOMA no tiene ANEFORGE_ENCODER
void whisper_aneforge_encode(struct whisper_aneforge_context *, int64_t, int64_t, const float *, float *) {}
void whisper_aneforge_free(struct whisper_aneforge_context *) {}
}
