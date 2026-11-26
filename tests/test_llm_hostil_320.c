/* SOMA_320_WIFI_HOSTIL: lo que el nucleo lee de las respuestas del servidor del LLM (src/agent/llm_proto.c) con datos hostiles: la
 * respuesta de los cuatro proveedores, los errores, la lista de modelos y el estado del lanzador. El servidor lo configura el
 * usuario, pero puede ser un servidor local en la red de casa (http sin cifrar) o uno comprometido.
 * Respuestas reales mutadas (comillas, barras, \u, pares subrogados, llaves) y cortadas, cada una en un bufer de su tamano exacto
 * con su NUL (asi las recibe el nucleo); salidas de varios tamanos. ASan ve cualquier lectura o escritura de mas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soma_test.h"
#include "llm_proto.h"

static uint64_t semilla = 3201;
static uint32_t rnd(void) { semilla = semilla * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(semilla >> 33); }

static const char *const SEM[] = {
    "{\"id\":\"x\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"reasoning_content\":\"pienso \\\"content\\\": no\",\"content\":\"{\\\"thought\\\":\\\"ok\\\",\\\"action\\\":\\\"final\\\"}\"},\"finish_reason\":\"stop\"}],\"usage\":{}}",
    "{\n  \"id\": \"chatcmpl-1\",\n  \"choices\": [\n    {\n      \"index\": 0,\n      \"message\": {\n        \"role\": \"assistant\",\n        \"content\": \"Hola \\u00e1 \\ud83d\\ude00\",\n        \"refusal\": null\n      }\n    }\n  ]\n}",
    "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"el \\\"text\\\": no\",\"signature\":\"x\"},{\"type\":\"text\",\"text\":\"{\\\"action\\\":\\\"final\\\"}\"}],\"stop_reason\":\"end_turn\"}",
    "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"respuesta de gemini\"},{\"text\":\" y mas\"}],\"role\":\"model\"},\"finishReason\":\"STOP\"}]}",
    "{\"error\":{\"message\":\"Invalid API key \\\"sk-***\\\"\",\"type\":\"invalid_request_error\",\"code\":401}}",
    "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}",
    "{\"object\":\"list\",\"data\":[{\"id\":\"gpt-4o\",\"object\":\"model\"},{\"id\":\"o3-mini\"},{\"id\":\"nail-35b\"}]}",
    "{\"models\":[{\"name\":\"models/gemini-2.5-pro\",\"displayName\":\"Gemini\"},{\"name\":\"models/gemini-flash\"}]}",
    "{\"estado\":\"cargando\",\"modelo\":\"nail-35b.gguf\",\"detalle\":\"capa 12 de 64\"}",
};
static const char RAROS[] = "\"\\{}[]:,u0dD8\n ";

static char *mutada(const char *s, uint32_t *on)
{
    uint32_t l = (uint32_t)strlen(s), cap = l + 64;
    char *t = (char *)malloc(cap);
    memcpy(t, s, l);
    int cambios = 1 + (int)(rnd() % 10);
    for (int c = 0; c < cambios && l; ++c) {
        uint32_t i = rnd() % l, k = rnd() % 4;
        if (k == 0) t[i] = RAROS[rnd() % (sizeof RAROS - 1)];
        else if (k == 1) t[i] = (char)(rnd() | 0x80);
        else if (k == 2 && l + 8 < cap) {
            static const char *const ins[] = { "\\u", "\\ud83d", "\\udc00", "\\", "\"", "\\u00", "{\"text\":\"", "]" };
            const char *x = ins[rnd() % 8]; uint32_t xl = (uint32_t)strlen(x);
            memmove(t + i + xl, t + i, l - i); memcpy(t + i, x, xl); l += xl;
        } else if (l > 1) { memmove(t + i, t + i + 1, l - i - 1); l--; }
    }
    if (rnd() % 4 == 0) l = rnd() % (l + 1);
    char *e = (char *)malloc(l + 1);
    memcpy(e, t, l); e[l] = 0;
    free(t);
    *on = l;
    return e;
}

int main(void)
{
    static struct llm_conf c; llm_conf_defaults(&c);
    int malos = 0;
    for (int it = 0; it < 200000; ++it) {
        uint32_t n; char *j = mutada(SEM[rnd() % (sizeof SEM / sizeof SEM[0])], &n);
        uint32_t cap = 1 + rnd() % 300;
        char *o = (char *)malloc(cap);
        c.prov = (int)(rnd() % LLM_NPROV);
        memset(o, 'Z', cap);
        if (llm_proto_reply(&c, j, o, cap) && !memchr(o, 0, cap)) malos++;
        memset(o, 'Z', cap);
        if (llm_proto_error(j, o, cap) && !memchr(o, 0, cap)) malos++;
        memset(o, 'Z', cap);
        (void)llm_proto_model_list(&c, j, o, cap);
        if (!memchr(o, 0, cap)) malos++;
        char st[16], mo[24], de[40];
        (void)llm_proto_launcher_status(j, st, sizeof st, mo, sizeof mo, de, sizeof de);
        (void)llm_proto_no_vision(400, j);
        free(o); free(j);
    }
    CHECK_EQ_INT(malos, 0);
    printf("[llm_hostil_320] 200000 respuestas mutadas\n");
    TEST_MAIN_END("llm_hostil_320");
}
