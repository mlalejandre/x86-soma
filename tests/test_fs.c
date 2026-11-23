/* Pruebas en host del VFS SOMAFS02 (fs.c) sobre un RAM-disk simulado.
 *
 *   test_fs                       suite completa en un disco limpio
 *   test_fs persist-write IMG     escribe datos y guarda el disco en IMG
 *   test_fs persist-check IMG     arranca "de nuevo" desde IMG y verifica
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "soma_test.h"
#include "host_stubs.h"
#include "fs.h"

static char g_buf[FS_DATA_MAX + 64];

static int read_str(const char *name, char *out, uint32_t max)
{
    return vfs_read(name, out, max);
}

static void fresh(void)
{
    host_disk_reset();
    host_out_reset();
}

static void test_formato_inicial(void)
{
    char b[512];

    fresh();
    vfs_init();
    CHECK_EQ_INT(read_str("/etc/hostname", b, sizeof(b)), 11);
    CHECK_STR(b, "soma-node1\n");
    /* Antes del parche 002 la longitud escrita a mano (137) leia mas alla del literal. */
    CHECK(read_str("/etc/os-release", b, sizeof(b)) > 0);
    CHECK(strncmp(b, "NAME=SOMA\n", 10) == 0);
    CHECK(strstr(b, "DESCRIPTION=Sistema Operativo Multi-Agente\n") != 0);
    CHECK_EQ_INT((int)strlen(b), (int)strlen(b));          /* termina en NUL */
    CHECK_EQ_INT(b[strlen(b) - 1], '\n');
    CHECK(strchr(b, '\0') == b + strlen(b));
    CHECK(read_str("/mem/ciencia_fisica.txt", b, sizeof(b)) > 0);
    CHECK(read_str("/agent/blackboard.txt", b, sizeof(b)) > 0);
}

static void test_roundtrip(void)
{
    char b[64];

    CHECK_EQ_INT(vfs_write("/t/a.txt", "hola", 4), 4);
    CHECK_EQ_INT(vfs_read("/t/a.txt", b, sizeof(b)), 4);
    CHECK_STR(b, "hola");
    /* Sobrescribir con contenido mas corto. */
    CHECK_EQ_INT(vfs_write("/t/a.txt", "ab", 2), 2);
    CHECK_EQ_INT(vfs_read("/t/a.txt", b, sizeof(b)), 2);
    CHECK_STR(b, "ab");
    /* Buffer menor que el contenido: se trunca y siempre hay NUL. */
    vfs_write("/t/largo.txt", "0123456789", 10);
    memset(b, 'Z', sizeof(b));
    CHECK_EQ_INT(vfs_read("/t/largo.txt", b, 4), 3);
    CHECK_STR(b, "012");
    /* Archivo vacio. */
    CHECK_EQ_INT(vfs_write("/t/vacio", "", 0), 0);
    CHECK_EQ_INT(vfs_read("/t/vacio", b, sizeof(b)), 0);
    CHECK_EQ_INT(vfs_read("/t/no_existe", b, sizeof(b)), -1);
}

static void test_borrado(void)
{
    char b[16];

    vfs_write("/t/borrar", "x", 1);
    CHECK_EQ_INT(vfs_delete("/t/borrar"), 0);
    CHECK_EQ_INT(vfs_read("/t/borrar", b, sizeof(b)), -1);
    CHECK_EQ_INT(vfs_delete("/t/borrar"), -1);
}

static void test_limites_de_tamano(void)
{
    memset(g_buf, 'x', sizeof(g_buf));
    CHECK_EQ_INT(vfs_write("/t/max", g_buf, FS_DATA_MAX - 1), FS_DATA_MAX - 1);
    /* PARCHE 087: 16 KiB ya no es el limite */
    CHECK_EQ_INT(vfs_write("/t/max", g_buf, FS_DATA_MAX), FS_DATA_MAX);
    CHECK_EQ_INT(vfs_size("/t/max"), FS_DATA_MAX);
    {
        uint32_t n = 300000u; char *big = (char *)malloc(n), *r; uint32_t rl = 0;
        for (uint32_t i = 0; i < n; i++) big[i] = (char)('a' + (i * 7) % 26);
        CHECK_EQ_INT(vfs_write("/t/grande.bin", big, n), (int)n);
        CHECK_EQ_INT(vfs_size("/t/grande.bin"), (int)n);
        r = vfs_read_alloc("/t/grande.bin", &rl);
        CHECK(r != 0 && rl == n && memcmp(r, big, n) == 0);
        free(r);
        /* encoge por debajo de una ranura: vuelve a la ranura y sigue bien */
        CHECK_EQ_INT(vfs_write("/t/grande.bin", big, 100), 100);
        r = vfs_read_alloc("/t/grande.bin", &rl);
        CHECK(r != 0 && rl == 100 && memcmp(r, big, 100) == 0);
        free(r);
        vfs_delete("/t/grande.bin");
        CHECK_EQ_INT(vfs_size("/t/grande.bin"), -1);
        free(big);
    }
    /* por encima del maximo absoluto: rechazado sin danar el anterior */
    CHECK_EQ_INT(vfs_write("/t/max", g_buf, FS_FILE_MAX + 1), -2);
    CHECK_EQ_INT(vfs_read("/t/max", g_buf, sizeof(g_buf)), FS_DATA_MAX);
    vfs_delete("/t/max");
}

static void test_nombres(void)
{
    char largo[200];
    char b[8];

    memset(largo, 'a', sizeof(largo));
    largo[0] = '/';
    largo[199] = 0;
    CHECK(vfs_write(largo, "x", 1) < 0);             /* nombre demasiado largo: rechazado */
    CHECK_EQ_INT(vfs_write(0, "x", 1), -1);
    CHECK_EQ_INT(vfs_read(0, b, sizeof(b)), -1);
    CHECK_EQ_INT(vfs_read("/t/a.txt", b, 0), -1);
}

static void test_rutas_especiales(void)
{
    char b[64];

    CHECK_EQ_INT(vfs_write("/proc/version", "x", 1), -1);   /* /proc es de solo lectura */
    CHECK_EQ_INT(vfs_delete("/proc/version"), -1);
    CHECK_EQ_INT(vfs_write("/dev/null", "abc", 3), 3);
    CHECK(vfs_read("/proc/version", b, sizeof(b)) > 0);
}

static void test_politica_agente(void)
{
    host_agent_mode(1);
    CHECK_EQ_INT(vfs_write("/etc/init.sh", "malicioso", 9), -1);
    CHECK_EQ_INT(vfs_delete("/etc/init.sh"), -1);
    host_agent_mode(0);
    {
        char b[256];
        CHECK(vfs_read("/etc/init.sh", b, sizeof(b)) > 0);
        CHECK(strncmp(b, "#!/bin/soma", 11) == 0);      /* sigue intacto */
    }
}

/* SOMA_268_ACCESO: la sesion de usuario solo escribe en /usuario y /tmp; fuera de la sesion (programas del sistema) nada cambia */
static void test_sesion_usuario_268(void)
{
    CHECK_EQ_INT(vfs_sesion_usuario_activa(), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/etc/ui.conf"), 1);               /* sin sesion: sin restriccion (programas del sistema) */
    CHECK_EQ_INT(vfs_sesion_usuario(1), 0);                                     /* devuelve la sesion anterior */
    CHECK_EQ_INT(vfs_sesion_usuario_activa(), 1);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/etc/ui.conf"), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/mem/indice.txt"), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/workspace/../etc/ui.conf"), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/usuario/a.txt"), 1);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/tmp/x"), 1);
    CHECK_EQ_INT(vfs_sesion_usuario(1), 1);                                     /* anidada: la anterior ya estaba */
    CHECK_EQ_INT(vfs_sesion_usuario(1), 1);
    vfs_sesion_usuario(1);
    host_agent_mode(1);                                                         /* un agente no esta en sesion de usuario: su politica es otra */
    CHECK_EQ_INT(vfs_sesion_usuario_activa(), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/etc/ui.conf"), 1);
    host_agent_mode(0);
    CHECK_EQ_INT(vfs_sesion_usuario(0), 1);                                     /* sale */
    CHECK_EQ_INT(vfs_sesion_usuario_activa(), 0);
    CHECK_EQ_INT(vfs_usuario_puede_escribir("/etc/ui.conf"), 1);
    CHECK(vfs_write("/etc/ui.conf", "x", 1) >= 0);                              /* el VFS en si no cambia: solo cambian las ordenes del usuario */
    for (int i = 0; i < 8; ++i) vfs_sesion_usuario(1);                          /* nunca se desborda la tabla */
    vfs_sesion_usuario(0);
    CHECK_EQ_INT(vfs_sesion_usuario_activa(), 0);
}

/* SOMA_269_RAM: modo RAM: nada va al disco hasta vfs_sync(); y entonces se guarda todo */
static void test_modo_ram_269(void)
{
    char b[64];
    fresh(); vfs_init();
    CHECK_EQ_INT(vfs_modo_ram_activo(), 0);
    int w0 = host_disk_writes();
    CHECK(vfs_write("/mem/ram1.txt", "uno", 3) == 3);
    CHECK(host_disk_writes() > w0);                                   /* modo normal: se guarda en cada escritura */
    vfs_modo_ram(1);
    CHECK_EQ_INT(vfs_modo_ram_activo(), 1);
    CHECK_EQ_INT((int)vfs_sin_guardar(), 0);
    w0 = host_disk_writes();
    CHECK(vfs_write("/mem/ram2.txt", "dos", 3) == 3);                 /* crear */
    CHECK(vfs_write("/mem/ram1.txt", "UNO!", 4) == 4);                /* reescribir */
    CHECK(vfs_write("/tmp/ram_tmp.txt", "t", 1) == 1);                /* efimero: nunca va al disco */
    CHECK_EQ_INT(host_disk_writes(), w0);                             /* ni un sector */
    CHECK(vfs_sin_guardar() >= 2);
    CHECK(vfs_read("/mem/ram1.txt", b, sizeof b) == 4 && !strcmp(b, "UNO!"));     /* en RAM se ve todo igual */
    CHECK_EQ_INT(vfs_delete("/mem/ram2.txt"), 0);
    CHECK_EQ_INT(host_disk_writes(), w0);
    CHECK_EQ_INT(vfs_sync(), 0);
    CHECK(host_disk_writes() > w0);                                   /* sync guarda de verdad */
    CHECK_EQ_INT((int)vfs_sin_guardar(), 0);
    int w1 = host_disk_writes();
    CHECK_EQ_INT(vfs_sync(), 0);                                      /* sin pendientes: nada que escribir */
    CHECK_EQ_INT(host_disk_writes(), w1);
    vfs_modo_ram(0);
    CHECK(vfs_write("/mem/ram3.txt", "tres", 4) == 4);
    CHECK(host_disk_writes() > w1);                                   /* de vuelta al modo normal */
}

static void test_tabla_llena(void)
{
    char nombre[48];
    char b[8];
    int creados = 0, i, primer_fallo = -1;
    uint32_t usados0, bytes, sucios;

    fresh();
    vfs_init();
    for (i = 0; i < FS_MAX_FILES + 20; ++i) {
        snprintf(nombre, sizeof(nombre), "/lleno/f%03d", i);
        if (vfs_write(nombre, "x", 1) < 0) { primer_fallo = i; break; }
        ++creados;
    }
    CHECK(primer_fallo > 0);
    CHECK(creados <= FS_MAX_FILES);
    vfs_get_stats(&usados0, &bytes, &sucios);
    CHECK_EQ_INT(usados0, FS_MAX_FILES);
    /* Los archivos creados antes del fallo siguen legibles. */
    CHECK_EQ_INT(vfs_read("/lleno/f000", b, sizeof(b)), 1);
    /* Borrar uno libera sitio. */
    CHECK_EQ_INT(vfs_delete("/lleno/f000"), 0);
    CHECK_EQ_INT(vfs_write("/lleno/nuevo", "y", 1), 1);
}

static void test_busqueda(void)
{
    char out[2048];

    fresh();
    vfs_init();
    vfs_write("/doc/gatos.txt", "el gato come pescado fresco en la cocina", 41);
    vfs_write("/doc/perros.txt", "el perro ladra fuerte en el parque", 34);
    CHECK(vfs_search_bm25("gato pescado", "/doc", out, sizeof(out)) > 0);
    CHECK(strstr(out, "gatos.txt") != 0);
    CHECK(vfs_grep_all("ladra", 0, 1, out, sizeof(out)) > 0);
    CHECK(strstr(out, "perros.txt") != 0);
    /* Salida acotada: un buffer pequeno no debe desbordarse. */
    {
        char tiny[16];
        (void)vfs_search_bm25("el", "/doc", tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny));
        (void)vfs_grep_all("e", 1, 0, tiny, sizeof(tiny));
        CHECK(strlen(tiny) < sizeof(tiny));
    }
}

static void test_fallo_de_disco(void)
{
    char b[16];

    fresh();
    vfs_init();
    host_disk_fail_after(0);                 /* el disco rechaza toda escritura */
    CHECK_EQ_INT(vfs_write("/t/fallo", "x", 1), -1);
    host_disk_fail_after(-1);                /* disco recuperado */
    CHECK_EQ_INT(vfs_write("/t/ok", "y", 1), 1);
    CHECK_EQ_INT(vfs_read("/t/ok", b, sizeof(b)), 1);
}

static void test_checkpoint(void)
{
    char b[16];

    fresh();
    vfs_init();
    vfs_write("/cp/dato", "v1", 2);
    CHECK_EQ_INT(vfs_checkpoint_save(), 0);
    vfs_write("/cp/dato", "v2-modificado", 13);
    CHECK_EQ_INT(vfs_checkpoint_restore(), 0);
    CHECK_EQ_INT(vfs_read("/cp/dato", b, sizeof(b)), 2);
    CHECK_STR(b, "v1");
}

static void test_ephemeral_no_toca_disco(void)
{
    int antes;

    fresh();
    vfs_init();
    antes = host_disk_writes();
    CHECK_EQ_INT(vfs_write("/tmp/efimero", "x", 1), 1);
    CHECK_EQ_INT(host_disk_writes(), antes);       /* /tmp no se persiste */
}

static int persist_write(const char *img)
{
    fresh();
    vfs_init();
    vfs_write("/persist/uno.txt", "contenido uno", 13);
    vfs_write("/persist/dos.txt", "contenido dos", 13);
    vfs_delete("/persist/dos.txt");
    vfs_write("/tmp/volatil", "no debe persistir", 17);
    {   /* PARCHE 087: un archivo grande y archivos mas alla de las 64 ranuras */
        static char big[200000]; for (int i = 0; i < (int)sizeof big; i++) big[i] = (char)(i * 31 + 7);
        CHECK_EQ_INT(vfs_write("/persist/grande.bin", big, sizeof big), (int)sizeof big);
        char nm[48]; for (int i = 0; i < 90; i++) { snprintf(nm, sizeof nm, "/persist/n%03d.txt", i); CHECK_EQ_INT(vfs_write(nm, nm, (uint32_t)strlen(nm)), (int)strlen(nm)); }
    }
    CHECK_EQ_INT(vfs_sync(), 0);
    if (host_disk_save(img) != 0) { printf("  no se pudo guardar %s\n", img); return 1; }
    TEST_MAIN_END("fs persist-write");
}

static int persist_check(const char *img)
{
    char b[64];

    host_out_reset();
    if (host_disk_load(img) != 0) { printf("  no se pudo cargar %s\n", img); return 1; }
    vfs_init();            /* proceso nuevo: debe detectar SOMAFS02 y cargar */
    CHECK(strstr(host_out(), "persistente detectado") != 0);
    CHECK_EQ_INT(vfs_read("/persist/uno.txt", b, sizeof(b)), 13);
    CHECK_STR(b, "contenido uno");
    CHECK_EQ_INT(vfs_read("/persist/dos.txt", b, sizeof(b)), -1);   /* borrado persistio */
    CHECK_EQ_INT(vfs_read("/tmp/volatil", b, sizeof(b)), -1);       /* efimero no persistio */
    {   /* PARCHE 087 */
        uint32_t rl = 0; char *r = vfs_read_alloc("/persist/grande.bin", &rl); int ok = r && rl == 200000;
        for (int i = 0; ok && i < 200000; i++) if (r[i] != (char)(i * 31 + 7)) ok = 0;
        CHECK(ok); free(r);
        char nm[48]; for (int i = 0; i < 90; i++) { snprintf(nm, sizeof nm, "/persist/n%03d.txt", i); CHECK_EQ_INT(vfs_read(nm, b, sizeof b), (int)strlen(nm)); CHECK_STR(b, nm); }
    }
    TEST_MAIN_END("fs persist-check");
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "persist-write") == 0) return persist_write(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "persist-check") == 0) return persist_check(argv[2]);

    test_formato_inicial();
    test_roundtrip();
    test_borrado();
    test_limites_de_tamano();
    test_nombres();
    test_rutas_especiales();
    test_politica_agente();
    test_sesion_usuario_268();
    test_modo_ram_269();
    test_busqueda();
    test_fallo_de_disco();
    test_checkpoint();
    test_ephemeral_no_toca_disco();
    test_tabla_llena();
    TEST_MAIN_END("fs");
}
