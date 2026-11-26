#ifndef SOMA_IA_SYS_H
#define SOMA_IA_SYS_H
/* SOMA_329_IA_LOCAL: motor de IA local (llama.cpp en ring 3, src/user/ia/). Ver ia_sys.c y src/core/ia_abi.h. */
#include <stdint.h>

#define IA_DIR "/modelos"
int  ia_disponible(const char **why);              /* 1 = se puede usar (x86_64 con AVX2) */
/* Una orden al motor (IA_CMD_*): entrada de texto, salida de texto (JSON para EMBED y CHAT). Lanza el proceso si no esta vivo y
 * carga el modelo si hace falta (la primera vez tarda lo que se tarde en leerlo del almacen).
 * Devuelve los bytes escritos en out (con NUL) o < 0; en err, el motivo. cancel (puede ser 0) corta una generacion. */
int  ia_orden(int cmd, const char *modelo, const char *in, uint32_t in_len, char *out, uint32_t cap, uint32_t plazo_ms,
              const volatile int *cancel, char *err, uint32_t ecap);
int  ia_parcial(char *out, uint32_t cap);          /* el texto que lleva generado el chat en curso (o -1) */
void ia_para(void);                                /* para el proceso y libera su memoria */
int  ia_estado(char *out, uint32_t cap);           /* resumen para la orden "ia" */
int  ia_modelo_ruta(const char *nombre, char *ruta, uint32_t cap);   /* "bge-m3" -> "/modelos/bge-m3....gguf" (0 = existe) */
int  ia_modelos(char *out, uint32_t cap);          /* los .gguf de /modelos, uno por linea; cuantos */
int  ia_instala(const char *nombre, char *msg, uint32_t cap);        /* copia un .gguf del pendrive (MODELOS) o de QEMU a /modelos */
int  ia_cmd(const char *args, char *out, uint32_t cap);              /* orden "ia" del terminal */
#endif
