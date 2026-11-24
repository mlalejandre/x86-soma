/* SOMA_195: con que modelo de lenguaje habla SOMA. Logica pura (se prueba en el host: tests/test_llm_proto_195.c).
 *
 * Hasta ahora el servidor del LLM estaba fijo en el codigo (192.168.1.200:8087, modelo nail-35b): funcionaba en QEMU porque
 * ejecutar.py prepara ese servidor, pero en un equipo real no habia forma de elegir otro. Ahora hay cuatro proveedores:
 *   local   servidor compatible con OpenAI en el equipo o la red local (llama.cpp, Ollama, LM Studio...)  POST /v1/chat/completions
 *   openai  ChatGPT            https://api.openai.com                      POST /v1/chat/completions   Authorization: Bearer
 *   claude  Claude (Anthropic) https://api.anthropic.com                   POST /v1/messages           x-api-key + anthropic-version
 *   gemini  Gemini (Google)    https://generativelanguage.googleapis.com   POST /v1beta/models/<m>:generateContent   x-goog-api-key
 * Este modulo construye la peticion de cada uno y saca el texto de su respuesta. Configuracion: /etc/llm.conf. */
#ifndef SOMA_LLM_PROTO_H
#define SOMA_LLM_PROTO_H
#include <stdint.h>

enum { LLM_LOCAL = 0, LLM_OPENAI, LLM_CLAUDE, LLM_GEMINI, LLM_NPROV };
#define LLM_BASE_MAX  128
#define LLM_MODEL_MAX 80
#define LLM_KEY_MAX   256

struct llm_conf {
    int prov;                                  /* proveedor activo */
    char base[LLM_NPROV][LLM_BASE_MAX];        /* URL base (esquema://host[:puerto]) */
    char model[LLM_NPROV][LLM_MODEL_MAX];
    char key[LLM_NPROV][LLM_KEY_MAX];          /* clave de la API (vacia = sin clave) */
    char launcher[LLM_BASE_MAX];               /* SOMA_196: lanzador del servidor local (tools/soma_lanzador.py); vacio = mismo equipo que servidor.local, puerto 8086 */
    char launcher_key[LLM_KEY_MAX];            /* SOMA_196: clave del lanzador */
};
struct llm_http { const char *method; char url[400]; char headers[LLM_KEY_MAX + 160]; };

const char *llm_prov_name(int prov);                    /* "local", "openai", "claude", "gemini" */
const char *llm_prov_label(int prov);                   /* nombre para la interfaz */
int  llm_prov_by_name(const char *name);                /* -1 si no existe (admite tambien chatgpt, anthropic, google) */
void llm_conf_defaults(struct llm_conf *c);
void llm_conf_parse(struct llm_conf *c, const char *text);                    /* sobre los valores por defecto */
uint32_t llm_conf_format(const struct llm_conf *c, char *out, uint32_t max);
/* Cambia un campo del proveedor `prov` ("servidor", "modelo", "clave"). 0 = bien, -1 = valor no valido. */
int  llm_conf_set(struct llm_conf *c, int prov, const char *field, const char *value);

enum { LLM_P_OK = 0, LLM_P_NOMODEL = -1, LLM_P_NOKEY = -2, LLM_P_TOOBIG = -3, LLM_P_INSECURE = -4, LLM_P_BADURL = -5, LLM_P_NOLKEY = -6 };
const char *llm_proto_strerror(int e);
/* Peticion de chat. Deja el cuerpo JSON en body (devuelve su longitud, o un LLM_P_* negativo) y URL y cabeceras en h. */
int  llm_proto_chat(const struct llm_conf *c, const char *prompt, int json_mode, char *body, uint32_t max, struct llm_http *h);
/* SOMA_248_DOC: chat con indicaciones de sistema propias e imagenes (PNG/JPEG) para modelos multimodales. max_tokens 0 = 8192.
 * llm_proto_chat_ex_size: bytes de cuerpo que hacen falta. llm_proto_no_vision: el error dice que el modelo no admite imagenes. */
struct llm_img { const uint8_t *data; uint32_t len; const char *mime; };
uint32_t llm_proto_chat_ex_size(const char *system, const char *prompt, const struct llm_img *img, int nimg);
int  llm_proto_chat_ex(const struct llm_conf *c, const char *system, const char *prompt, const struct llm_img *img, int nimg, int max_tokens,
                       char *body, uint32_t max, struct llm_http *h);
int  llm_proto_no_vision(int status, const char *err);
/* SOMA_285_CHAT: conversacion de varios turnos. rol LLM_ROL_USER / LLM_ROL_ASSISTANT; las imagenes van con el ultimo mensaje del usuario. */
enum { LLM_ROL_USER = 0, LLM_ROL_ASSISTANT = 1 };
struct llm_msg { int rol; const char *text; };
uint32_t llm_proto_chat_hist_size(const char *system, const struct llm_msg *m, int nm, const struct llm_img *img, int nimg);
int  llm_proto_chat_hist(const struct llm_conf *c, const char *system, const struct llm_msg *m, int nm, const struct llm_img *img, int nimg,
                         int max_tokens, char *body, uint32_t max, struct llm_http *h);
int  llm_proto_models(const struct llm_conf *c, struct llm_http *h);         /* peticion GET de la lista de modelos; LLM_P_* */
/* Texto de la respuesta del modelo. 1 = extraido. */
int  llm_proto_reply(const struct llm_conf *c, const char *json, char *out, uint32_t max);
int  llm_proto_error(const char *json, char *out, uint32_t max);             /* mensaje de error del proveedor; 1 = habia */
/* Nombres de modelo de la respuesta de la lista, uno por linea. Devuelve cuantos. */
int  llm_proto_model_list(const struct llm_conf *c, const char *json, char *out, uint32_t max);
/* SOMA_196: lanzador. El servidor llama.cpp de la red local no siempre esta encendido; en el equipo que lo aloja corre un servicio
 * pequeno (tools/soma_lanzador.py) que lista los modelos .gguf y arranca o para llama-server. Campos de llm_conf_set (prov se ignora):
 * "lanzador" (direccion) y "clave-lanzador". */
enum { LLM_L_STATUS = 0, LLM_L_MODELS, LLM_L_START, LLM_L_STOP,
       LLM_L_EMB_STATUS, LLM_L_EMB_START, LLM_L_EMB_STOP };      /* SOMA_278_EMBEDDINGS: servidor de embeddings de la memoria */
void llm_launcher_base(const struct llm_conf *c, char *out, uint32_t max);    /* direccion efectiva del lanzador */
/* Peticion al lanzador. Devuelve la longitud del cuerpo (0 en los GET) o un LLM_P_* negativo. */
int  llm_proto_launcher(const struct llm_conf *c, int what, const char *model, char *body, uint32_t max, struct llm_http *h);
/* Respuesta de /v1/estado: estado ("listo", "cargando", "parado", "error"), modelo y detalle. 1 = leida. */
int  llm_proto_launcher_status(const char *json, char *state, uint32_t smax, char *model, uint32_t mmax, char *detail, uint32_t dmax);
/* 1 si la URL base es http(s) hacia este equipo o una red privada (10.x, 172.16-31.x, 192.168.x, 127.x, 169.254.x, localhost, *.local) */
int  llm_base_is_private(const char *base);
#endif
extern uint32_t llm_proto_max_tokens;                 /* SOMA_260_LLM_TAM: max_tokens de llm_proto_chat (8192 como antes; lo fija llm.c) */
