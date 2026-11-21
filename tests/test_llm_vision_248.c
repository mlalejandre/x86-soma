/* SOMA_248_DOC: peticiones con imagenes (modelos multimodales) para los cuatro proveedores, base64 y deteccion de "sin vision". */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "llm_proto.h"
static int f, n;
#define CK(c, m) do { n++; if (!(c)) { f++; printf("FALLO: %s\n", m); } } while (0)
int main(void)
{
    struct llm_conf c; llm_conf_defaults(&c);
    static char body[8192]; struct llm_http h;
    const uint8_t png[3] = { 'a', 'b', 'c' }, png2[4] = { 'a', 'b', 'c', 'd' };
    struct llm_img im[2] = { { png, 3, "image/png" }, { png2, 4, 0 } };
    /* local (OpenAI compatible) */
    int k = llm_proto_chat_ex(&c, "SISTEMA \"x\"", "mira", im, 2, 500, body, sizeof body, &h);
    CK(k > 0 && strstr(h.url, "/v1/chat/completions"), "local: url");
    CK(strstr(body, "\"content\":\"SISTEMA \\\"x\\\"\""), "local: sistema propio escapado");
    CK(strstr(body, "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,YWJj\"}}"), "local: imagen como data URI");
    CK(strstr(body, "base64,YWJjZA==\""), "local: relleno base64");
    CK(strstr(body, "\"max_tokens\":500"), "local: limite");
    k = llm_proto_chat_ex(&c, 0, "hola", 0, 0, 0, body, sizeof body, &h);
    CK(k > 0 && strstr(body, "{\"role\":\"user\",\"content\":\"hola\"}") && strstr(body, "\"max_tokens\":8192"), "local sin imagenes: contenido de texto");
    /* claude */
    llm_conf_set(&c, LLM_CLAUDE, "clave", "sk-ant-x"); c.prov = LLM_CLAUDE;
    k = llm_proto_chat_ex(&c, "S", "mira", im, 1, 0, body, sizeof body, &h);
    CK(k > 0 && strstr(h.url, "/v1/messages") && strstr(body, "\"system\":\"S\""), "claude: sistema");
    CK(strstr(body, "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/png\",\"data\":\"YWJj\"}}"), "claude: imagen");
    CK(strstr(body, "{\"type\":\"text\",\"text\":\"mira\"}]}]}"), "claude: texto tras la imagen");
    /* gemini */
    llm_conf_set(&c, LLM_GEMINI, "clave", "AIza-x"); llm_conf_set(&c, LLM_GEMINI, "modelo", "gemini-x"); c.prov = LLM_GEMINI;
    k = llm_proto_chat_ex(&c, "S", "mira", im, 1, 0, body, sizeof body, &h);
    CK(k > 0 && strstr(h.url, ":generateContent") && strstr(body, "{\"inline_data\":{\"mime_type\":\"image/png\",\"data\":\"YWJj\"}}"), "gemini: imagen");
    /* openai */
    llm_conf_set(&c, LLM_OPENAI, "clave", "sk-x"); llm_conf_set(&c, LLM_OPENAI, "modelo", "gpt-x"); c.prov = LLM_OPENAI;
    k = llm_proto_chat_ex(&c, "S", "mira", im, 1, 900, body, sizeof body, &h);
    CK(k > 0 && strstr(body, "\"max_completion_tokens\":900") && strstr(body, "image_url"), "openai: imagen y limite");
    /* cuerpo pequeno: no cabe */
    CK(llm_proto_chat_ex(&c, "S", "mira", im, 1, 0, body, 60, &h) == LLM_P_TOOBIG, "no cabe");
    CK(llm_proto_chat_ex_size("S", "mira", im, 2) > 4096 + 8, "tamano necesario");
    /* sin vision */
    CK(llm_proto_no_vision(400, "Invalid content type. image_url is only supported by certain models."), "openai: no admite imagenes");
    CK(llm_proto_no_vision(500, "image input is not supported - hint: if this is unexpected, you may need to provide the mmproj"), "llama.cpp sin mmproj");
    CK(!llm_proto_no_vision(401, "invalid api key"), "clave mala no es falta de vision");
    CK(!llm_proto_no_vision(400, "max_tokens too large"), "otro error 400");
    printf("llm vision 248: %d/%d comprobaciones correctas\n", n - f, n);
    return f ? 1 : 0;
}
