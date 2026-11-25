/* SOMA_307_SEGURIDAD: archivos de src/ que la IA no puede cambiar con un parche (action "patch" del agente).
 * Es la MISMA lista que aplica el anfitrion (PROTECTED_FILES de ejecutar.py + protected.txt). Antes el SO tenia su propia
 * lista de 23 nombres (el anfitrion, 76) y ademas comparaba la ruta entera: "core/thread.c" no casaba con "thread.c", asi que
 * dentro del SO no se rechazaba nada y todo dependia del anfitrion. Ahora se compara el NOMBRE de archivo, como ejecutar.py.
 * tests/test_protegidos_307.py comprueba en cada `make test` que esta lista y la del anfitrion son iguales.
 * Funciones puras (sin nucleo): se prueban en el anfitrion (tests/test_agent_protegidos_307.c). */
#ifndef SOMA_AGENT_PROTEGIDOS_H
#define SOMA_AGENT_PROTEGIDOS_H

#include <stdint.h>

static const char *const agent_protegidos[] = {
    "agent.c", "agent_net.c", "agent_net.h", "agent_policy.h", "agent_protegidos.h", "arch.h", "boot.s",
    "boot_gate.c", "boot_gate.h", "ccmp.c", "console.c", "crypto.c", "disk_layout.h", "elf.c", "elf.h", "fs.c",
    "httpd.c", "httpd_policy.h", "idt.c", "idt.h", "io.h", "lcm_ordenes.inc", "lcm_ordenes_pesos.inc",
    "lcm_ordenes_shell.inc", "lcm_soma.inc", "llm.c", "llm_proto.c", "llm_proto.h", "mem.c", "mem.h", "memfn.h", "memfn_gen.h", "memprot.c",
    "memprot.h", "mmu.c", "mutex.c", "mutex.h", "net.c", "panic.c", "pila_canario.h", "pmm.c", "pmm.h", "rng.c",
    "rng.h", "sandbox.c", "sandbox.h", "sbx_abi.h", "shell.c", "smp.c", "srcfs.c", "srcfs.h", "ssp.c", "ssp_init.c",
    "switch.s", "sysdisk.h", "tcp_policy.h", "thread.c", "thread.h", "tls13.c", "tls13.h", "tls_roots.c",
    "tls_trust.c", "tlscrypto.c", "uaccess.h", "user_path.h", "usermode.c", "usermode.h", "virtio_blk.c",
    "virtio_blk.h", "vmm.c", "vmm.h", "web_net.c", "web_page.c", "web_r3.c", "web_r3.h", "web_r3_abi.h", "websvc.c",
    "wpa.c", "x509.c", "x509.h", 0
};

/* Copia en out (max bytes, con terminador) la ruta del principio de s: salta espacios, quita "/" y "src/" del principio y
 * corta en el primer espacio, comilla, salto de linea o fin. Devuelve su longitud. */
static inline uint32_t agent_ruta_parche(const char *s, char *out, uint32_t max)
{
    uint32_t n = 0;
    if (!out || !max) return 0;
    out[0] = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s == '/') s++;
    if (s[0] == 's' && s[1] == 'r' && s[2] == 'c' && s[3] == '/') s += 4;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != '"' && *s != '\'' && n + 1 < max) out[n++] = *s++;
    out[n] = 0;
    return n;
}

/* Nombre de archivo de una ruta (lo que va tras la ultima barra). */
static inline const char *agent_nombre_de(const char *ruta)
{
    const char *b = ruta;
    for (const char *p = ruta; p && *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

/* 1 si el archivo de la ruta (o del principio de un texto "FILE: src/<ruta> ...") esta protegido. */
static inline int agent_ruta_protegida(const char *s)
{
    char ruta[160];
    agent_ruta_parche(s, ruta, sizeof ruta);
    const char *b = agent_nombre_de(ruta);
    for (uint32_t i = 0; agent_protegidos[i]; ++i) {
        const char *x = agent_protegidos[i], *y = b;
        while (*x && *x == *y) { x++; y++; }
        if (!*x && !*y) return 1;
    }
    return 0;
}

/* 1 si la ruta termina en ext (".c", ".h"...). */
static inline int agent_ruta_termina(const char *ruta, const char *ext)
{
    uint32_t a = 0, e = 0;
    while (ruta[a]) a++;
    while (ext[e]) e++;
    if (e > a) return 0;
    for (uint32_t i = 0; i < e; ++i) if (ruta[a - e + i] != ext[i]) return 0;
    return 1;
}

#endif
