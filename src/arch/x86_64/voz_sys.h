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
/* SOMA_246_VOZ_MODELOS: nivel de modelo (0 Rapido = base, 1 Preciso = small, 2 Maxima precision = large-v3-turbo) */
#define VOZ_TIERS 3
int  voz_pref(void);                                      /* nivel elegido (/etc/voz.conf; por defecto 0) */
void voz_set_pref(int tier);
int  voz_tier_of(const char *name);                       /* nivel de un .bin por su nombre */
int  voz_tier_from_key(const char *key);                  /* "rapido" / "preciso" / "maximo" -> nivel, -1 si no */
const char *voz_tier_file(int tier);                      /* nombre recomendado del .bin */
const char *voz_tier_name(int tier);
const char *voz_tier_key(int tier);
uint32_t voz_tier_mb(int tier);                           /* tamano aproximado del .bin recomendado */
int  voz_installed(int tier, char *path, uint32_t cap, uint64_t *size);    /* 0 = hay un modelo de ese nivel en /modelos */
int  voz_on_stick(int tier, char *name, uint32_t cap, uint32_t *size);     /* 0 = esta en MODELOS del pendrive; -2 sin pendrive */
int  voz_model_tier(char *path, uint32_t cap, uint64_t *size, int *tier);  /* el que se usara (el elegido o el mas cercano) */
int  voz_install_tier(int tier, char *msg, uint32_t cap);                  /* copia el modelo de ese nivel a /modelos */
int  voz_install_progress(void);                          /* -1 = no se esta instalando; si no, 0..100 */
/* SOMA_247_HABLA: texto a voz con una voz de Piper (/voces: el .onnx y su .onnx.json) */
int  voz_voice(char *path, uint32_t cap, uint64_t *size);  /* 0 = hay voz instalada */
int  voz_voice_install(char *msg, uint32_t cap);           /* copia la voz de MODELOS del pendrive a /voces */
int  voz_speak_run(const char *text, uint32_t len, char *out, uint32_t cap);   /* lee en voz alta (sincrono) */
int  voz_speak_start(const char *text, uint32_t len);      /* lo mismo en segundo plano: 0 = empezo; -16 ya esta leyendo */
void voz_speak_stop(void);
int  voz_speak_state(void);                                /* 1 = preparando o leyendo */
uint32_t voz_speak_ver(void);                              /* cambia al empezar y al terminar (para repintar) */
const char *voz_speak_msg(void);                           /* motivo del ultimo fallo ("" si fue bien) */
/* SOMA_250_TTS: preferencias de la lectura en voz alta (/etc/habla.conf), varias voces y lectura de las respuestas del orbe */
struct voz_tts_conf {
    int  leer_orbe;                                        /* 1 = el orbe lee sus respuestas (por defecto) */
    int  velocidad;                                        /* % (70..160; 100 = la de la voz) */
    int  hablante;                                         /* voces con varios hablantes: cual (0 = el primero) */
    char voz[96];                                          /* ruta de la voz elegida en /voces ("" = la primera) */
};
void voz_tts_get(struct voz_tts_conf *c);
void voz_tts_set(const struct voz_tts_conf *c);
#define VOZ_VOICES_MAX 12
int  voz_voice_list(char (*paths)[96], uint64_t *sizes, int max);   /* voces instaladas (.onnx de /voces) */
int  voz_voice_speakers(const char *path);                 /* hablantes de una voz (num_speakers de su .json; 1 si no dice) */
void voz_voice_label(const char *path, char *out, uint32_t cap);   /* "Davefx (Espana, calidad media)" */
int  voz_speak_reply(const char *text);                    /* el orbe lee su respuesta si esta activado (0 = empezo) */
#endif
