#ifndef SOMA_VOZ_SYS_H
#define SOMA_VOZ_SYS_H
/* SOMA_241_VOZ: voz a texto con whisper.cpp (ver voz_sys.c y src/user/voz/) */
#include <stdint.h>
#define VOZ_DIR           "/modelos"
#define VOZ_MODEL_DEFAULT "/modelos/ggml-base-q5_1.bin"
int  voz_ready(const char **why);                         /* 1 = AVX2 y modelo instalado; si no, *why dice que falta */
int  voz_model(char *path, uint32_t cap, uint64_t *size); /* 0 = hay modelo (ruta y tamano) */
/* Transcribe: user_args = "[-T] [-v] [-l idioma] archivo.wav" (ruta del almacen BIGFS). Con out la salida va ahi; sin out, a la consola.
 * 0 = bien; 1 = el programa fallo o se paro (el motivo va en la salida); <0 = no se pudo lanzar. */
int  voz_run(const char *user_args, char *out, uint32_t cap, int64_t *code_out);
void voz_stop(void);
int  voz_busy(void);
int  voz_install(char *msg, uint32_t cap);                /* 0 = modelo copiado a /modelos */
int  voz_last_recording(char *path, uint32_t cap);        /* 0 = ruta de la grabacion mas reciente */
#endif
