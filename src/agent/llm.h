#ifndef MYOS_LLM_H
#define MYOS_LLM_H

#include <stdint.h>

extern char llm_last_reply[4096];
extern int  llm_test_passed;

/* Comprueba /health en llama-server. 1 = 200 OK. */
int  llm_health(void);

/*
 * Envia prompt al LLM (POST /v1/chat/completions) y extrae el texto
 * de la respuesta en reply_out. Devuelve 1 si OK, 0 en fallo.
 */
int  llm_chat(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);
int  llm_diagnose(const char *user_question, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);

/* Envia prompt con grammar forzado para respuesta JSON estricta */
int  llm_chat_json(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);

/* Extrae un campo clave de un objeto JSON bare-metal */
int  llm_json_get(const char *json, const char *key, char *out, uint32_t max);

/* SOMA_195: configuracion del modelo de lenguaje (llm_proto.h): proveedor, servidor, modelo y clave. Se guarda en /etc/llm.conf. */
struct llm_conf;
const struct llm_conf *llm_conf_get(void);
int  llm_conf_save(const struct llm_conf *c);                 /* 0 = guardado */
int  llm_list_models(char *out, uint32_t max);                /* modelos del proveedor activo, uno por linea; devuelve cuantos o -1 */
/* SOMA_196: lanzador del servidor local (tools/soma_lanzador.py en el equipo que aloja llama.cpp) */
int  llm_launcher_models(char *out, uint32_t max);            /* modelos que puede arrancar, uno por linea; cuantos o -1 */
int  llm_launcher_status(char *model, uint32_t mmax);         /* 1 listo, 2 cargando, 0 parado, -1 error, -2 sin lanzador */
int  llm_launcher_start(const char *model, uint32_t max_wait_ms, void (*progress)(uint32_t secs));   /* 1 = arrancado y listo */
int  llm_launcher_stop(void);                                 /* 0 = parado */
extern char llm_last_error[200];                              /* motivo del ultimo fallo, en claro */

/* SOMA_248_DOC: chat con indicaciones de sistema propias e imagenes (struct llm_img de llm_proto.h). 1 = respuesta, 0 = fallo,
 * -2 = el modelo no admite imagenes (se recuerda para ese modelo: llm_vision_unsupported). */
struct llm_img;
int  llm_chat_ex(const char *system, const char *prompt, const struct llm_img *img, int nimg, int max_tokens,
                 char *reply_out, uint32_t reply_max, uint32_t timeout_ms);
int  llm_vision_unsupported(void);

/* Suite de verificacion HTTP + LLM. */
void net_run_llm_test(void);

#endif
