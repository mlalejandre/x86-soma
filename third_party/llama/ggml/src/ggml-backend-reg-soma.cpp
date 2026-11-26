// SOMA_241_VOZ (y SOMA_329_IA_LOCAL, copia para el motor de IA): registro de backends reducido para SOMA. Sustituye a ggml-backend-reg.cpp (que carga backends dinamicos
// con dlopen y std::filesystem): en SOMA solo existe el backend de CPU, enlazado estaticamente.
#include "ggml-backend-impl.h"
#include "ggml-backend.h"
#include "ggml-impl.h"
#include "ggml-cpu.h"
#include <cstring>

#define SOMA_MAX_REGS 4
#define SOMA_MAX_DEVS 8
static ggml_backend_reg_t s_regs[SOMA_MAX_REGS]; static size_t s_nregs;
static ggml_backend_dev_t s_devs[SOMA_MAX_DEVS]; static size_t s_ndevs;
static bool s_init;

static void soma_reg_init() {
    if (s_init) return;
    s_init = true;
    ggml_backend_register(ggml_backend_cpu_reg());
}

void ggml_backend_register(ggml_backend_reg_t reg) {
    if (!reg) return;
    for (size_t i = 0; i < s_nregs; i++) if (s_regs[i] == reg) return;
    if (s_nregs < SOMA_MAX_REGS) s_regs[s_nregs++] = reg;
    for (size_t i = 0; i < ggml_backend_reg_dev_count(reg); i++) ggml_backend_device_register(ggml_backend_reg_dev_get(reg, i));
}

void ggml_backend_device_register(ggml_backend_dev_t device) {
    for (size_t i = 0; i < s_ndevs; i++) if (s_devs[i] == device) return;
    if (s_ndevs < SOMA_MAX_DEVS) s_devs[s_ndevs++] = device;
}

static bool striequals(const char * a, const char * b) {
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

size_t ggml_backend_reg_count() { soma_reg_init(); return s_nregs; }
ggml_backend_reg_t ggml_backend_reg_get(size_t index) { GGML_ASSERT(index < ggml_backend_reg_count()); return s_regs[index]; }
ggml_backend_reg_t ggml_backend_reg_by_name(const char * name) {
    for (size_t i = 0; i < ggml_backend_reg_count(); i++) if (striequals(ggml_backend_reg_name(s_regs[i]), name)) return s_regs[i];
    return nullptr;
}
size_t ggml_backend_dev_count() { soma_reg_init(); return s_ndevs; }
ggml_backend_dev_t ggml_backend_dev_get(size_t index) { GGML_ASSERT(index < ggml_backend_dev_count()); return s_devs[index]; }
ggml_backend_dev_t ggml_backend_dev_by_name(const char * name) {
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) if (striequals(ggml_backend_dev_name(s_devs[i]), name)) return s_devs[i];
    return nullptr;
}
ggml_backend_dev_t ggml_backend_dev_by_type(enum ggml_backend_dev_type type) {
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) if (ggml_backend_dev_type(s_devs[i]) == type) return s_devs[i];
    return nullptr;
}
ggml_backend_t ggml_backend_init_by_name(const char * name, const char * params) {
    ggml_backend_dev_t dev = ggml_backend_dev_by_name(name);
    return dev ? ggml_backend_dev_init(dev, params) : nullptr;
}
ggml_backend_t ggml_backend_init_by_type(enum ggml_backend_dev_type type, const char * params) {
    ggml_backend_dev_t dev = ggml_backend_dev_by_type(type);
    return dev ? ggml_backend_dev_init(dev, params) : nullptr;
}
ggml_backend_t ggml_backend_init_best(void) {
    ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    return dev ? ggml_backend_dev_init(dev, nullptr) : nullptr;
}
ggml_backend_reg_t ggml_backend_load(const char * path) { (void)path; return nullptr; }
void ggml_backend_unload(ggml_backend_reg_t reg) { (void)reg; }
void ggml_backend_load_all() {}
void ggml_backend_load_all_from_path(const char * dir_path) { (void)dir_path; }
