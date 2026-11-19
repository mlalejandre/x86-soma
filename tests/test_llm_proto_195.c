/* SOMA_195: peticiones y respuestas de los proveedores de LLM (src/agent/llm_proto.c). */
#include <stdio.h>
#include <string.h>
#include "llm_proto.h"

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    static struct llm_conf c; static struct llm_http h; static char body[65536], out[8192], txt[4096];
    llm_conf_defaults(&c);
    CK(c.prov == LLM_LOCAL && !strcmp(c.base[LLM_LOCAL], "http://192.168.1.200:8087") && !strcmp(c.model[LLM_LOCAL], "nail-35b"), "valores por defecto (los de siempre)");
    CK(llm_prov_by_name("claude") == LLM_CLAUDE && llm_prov_by_name("ChatGPT") == LLM_OPENAI && llm_prov_by_name("google\n") == LLM_GEMINI && llm_prov_by_name("otro") == -1, "nombres de proveedor");

    /* --- local: lo mismo que enviaba el nucleo antes */
    int n = llm_proto_chat(&c, "hola \"mundo\"\nlinea 2\ttab \x01 fin", 1, body, sizeof body, &h);
    CK(n > 0 && !strcmp(h.url, "http://192.168.1.200:8087/v1/chat/completions") && !strcmp(h.method, "POST") && h.headers[0] == 0, "local: URL %s cabeceras [%s]", h.url, h.headers);
    CK(strstr(body, "\"model\":\"nail-35b\"") && strstr(body, "hola \\\"mundo\\\"\\nlinea 2\\ttab  fin") && strstr(body, "\"response_format\":{\"type\":\"json_object\",\"schema\"") && strstr(body, "\"max_tokens\":8192}"), "local: cuerpo %s", body);
    CK(llm_conf_set(&c, LLM_LOCAL, "clave", "sk-local") == 0 && llm_proto_chat(&c, "x", 0, body, sizeof body, &h) > 0 && !strcmp(h.headers, "Authorization: Bearer sk-local\r\n"), "local con clave en red privada");
    CK(llm_conf_set(&c, LLM_LOCAL, "servidor", "http://llm.ejemplo.com:8080") == 0 && llm_proto_chat(&c, "x", 0, body, sizeof body, &h) == LLM_P_INSECURE, "clave por http a internet: rechazado");
    CK(llm_conf_set(&c, LLM_LOCAL, "servidor", "https://llm.ejemplo.com") == 0 && llm_proto_chat(&c, "x", 0, body, sizeof body, &h) > 0, "clave por https a internet");
    CK(llm_conf_set(&c, LLM_LOCAL, "servidor", "192.168.1.50:11434/") == 0 && !strcmp(c.base[LLM_LOCAL], "http://192.168.1.50:11434"), "servidor sin esquema: %s", c.base[LLM_LOCAL]);
    CK(llm_conf_set(&c, LLM_LOCAL, "servidor", "") == 0 && !strcmp(c.base[LLM_LOCAL], "http://192.168.1.200:8087"), "servidor vacio = el de fabrica");
    CK(llm_conf_set(&c, LLM_LOCAL, "servidor", "http://a b") == -1 && llm_conf_set(&c, LLM_LOCAL, "servidor", "ftp://x.y") == -1 && llm_conf_set(&c, LLM_LOCAL, "servidor", "http://x.y:99999999") == -1, "servidores no validos");
    CK(llm_conf_set(&c, LLM_LOCAL, "clave", "a\r\nX-Inyectada: 1") == -1 && llm_conf_set(&c, LLM_LOCAL, "modelo", "m\"x") == -1 && llm_conf_set(&c, LLM_GEMINI, "modelo", "../otro") == -1, "inyeccion de cabeceras y de ruta");
    CK(llm_base_is_private("http://10.1.2.3:80") && llm_base_is_private("http://localhost:8080") && llm_base_is_private("http://nas.local") && llm_base_is_private("http://172.20.0.1") && !llm_base_is_private("http://172.32.0.1") && !llm_base_is_private("http://8.8.8.8") && !llm_base_is_private("http://192.168.1.evil.com"), "redes privadas");
    CK(llm_proto_reply(&c, "{\"id\":\"x\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"reasoning_content\":\"pienso \\\"content\\\": no\",\"content\":\"{\\\"thought\\\":\\\"ok\\\",\\\"action\\\":\\\"final\\\"}\"},\"finish_reason\":\"stop\"}],\"usage\":{}}", out, sizeof out) && !strcmp(out, "{\"thought\":\"ok\",\"action\":\"final\"}"), "local: respuesta [%s]", out);
    CK(llm_proto_model_list(&c, "{\"object\":\"list\",\"data\":[{\"id\":\"nail-35b\",\"object\":\"model\",\"owned_by\":\"llamacpp\"},{\"id\":\"qwen/qwen3-8b\",\"object\":\"model\"}]}", out, sizeof out) == 2 && !strcmp(out, "nail-35b\nqwen/qwen3-8b\n"), "local: modelos [%s]", out);

    /* --- OpenAI */
    c.prov = LLM_OPENAI;
    CK(llm_proto_chat(&c, "x", 0, body, sizeof body, &h) == LLM_P_NOMODEL, "openai: sin modelo");
    llm_conf_set(&c, LLM_OPENAI, "modelo", "gpt-prueba");
    CK(llm_proto_chat(&c, "x", 0, body, sizeof body, &h) == LLM_P_NOKEY, "openai: sin clave");
    llm_conf_set(&c, LLM_OPENAI, "clave", "sk-proj-abc");
    n = llm_proto_chat(&c, "dime algo", 1, body, sizeof body, &h);
    CK(n > 0 && !strcmp(h.url, "https://api.openai.com/v1/chat/completions") && !strcmp(h.headers, "Authorization: Bearer sk-proj-abc\r\n"), "openai: %s [%s]", h.url, h.headers);
    CK(strstr(body, "\"response_format\":{\"type\":\"json_object\"}") && strstr(body, "\"max_completion_tokens\":8192}") && !strstr(body, "temperature") && !strstr(body, "schema") && strstr(body, "JSON"), "openai: cuerpo %s", body);
    CK(llm_proto_reply(&c, "{\n  \"id\": \"chatcmpl-1\",\n  \"choices\": [\n    {\n      \"index\": 0,\n      \"message\": {\n        \"role\": \"assistant\",\n        \"content\": \"Hola \\u00e1 \\ud83d\\ude00\",\n        \"refusal\": null\n      }\n    }\n  ]\n}", out, sizeof out) && !strcmp(out, "Hola \xc3\xa1 \xf0\x9f\x98\x80"), "openai: respuesta con espacios y \\u [%s]", out);
    CK(llm_proto_error("{\n  \"error\": {\n    \"message\": \"Incorrect API key provided\",\n    \"type\": \"invalid_request_error\"\n  }\n}", out, sizeof out) && !strcmp(out, "Incorrect API key provided"), "openai: error [%s]", out);
    CK(llm_proto_models(&c, &h) == LLM_P_OK && !strcmp(h.url, "https://api.openai.com/v1/models") && !strcmp(h.method, "GET"), "openai: lista de modelos");

    /* --- Claude */
    c.prov = LLM_CLAUDE;
    CK(!strcmp(c.model[LLM_CLAUDE], "claude-sonnet-5-5"), "claude: modelo por defecto");
    llm_conf_set(&c, LLM_CLAUDE, "clave", "sk-ant-xyz");
    n = llm_proto_chat(&c, "dime algo", 1, body, sizeof body, &h);
    CK(n > 0 && !strcmp(h.url, "https://api.anthropic.com/v1/messages") && !strcmp(h.headers, "x-api-key: sk-ant-xyz\r\nanthropic-version: 2023-06-01\r\n"), "claude: %s [%s]", h.url, h.headers);
    CK(strstr(body, "{\"model\":\"claude-sonnet-5-5\",\"max_tokens\":8192,\"system\":\"") && strstr(body, "\"messages\":[{\"role\":\"user\",\"content\":\"dime algo\"}]}") && !strstr(body, "response_format") && strstr(body, "SOLO con el objeto JSON"), "claude: cuerpo %s", body);
    CK(llm_proto_reply(&c, "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-sonnet-5-5\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"el \\\"text\\\": no es esto\",\"signature\":\"x\"},{\"type\":\"text\",\"text\":\"{\\\"action\\\":\\\"final\\\"}\"}],\"stop_reason\":\"end_turn\",\"usage\":{\"input_tokens\":3}}", out, sizeof out) && !strcmp(out, "{\"action\":\"final\"}"), "claude: respuesta [%s]", out);
    CK(llm_proto_error("{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"},\"request_id\":\"r\"}", out, sizeof out) && !strcmp(out, "invalid x-api-key"), "claude: error [%s]", out);
    CK(llm_proto_models(&c, &h) == LLM_P_OK && !strcmp(h.url, "https://api.anthropic.com/v1/models?limit=100"), "claude: lista de modelos");
    CK(llm_proto_model_list(&c, "{\"data\":[{\"type\":\"model\",\"id\":\"claude-opus-5-5\",\"display_name\":\"Claude Opus\"},{\"type\":\"model\",\"id\":\"claude-haiku-5-5\"}],\"has_more\":false,\"first_id\":\"claude-opus-5-5\"}", out, sizeof out) >= 2 && strstr(out, "claude-opus-5-5\nclaude-haiku-5-5\n") == out, "claude: modelos [%s]", out);

    /* --- Gemini */
    c.prov = LLM_GEMINI;
    llm_conf_set(&c, LLM_GEMINI, "modelo", "models/gemini-prueba"); llm_conf_set(&c, LLM_GEMINI, "clave", "AIzaXYZ");
    n = llm_proto_chat(&c, "dime algo", 1, body, sizeof body, &h);
    CK(n > 0 && !strcmp(h.url, "https://generativelanguage.googleapis.com/v1beta/models/gemini-prueba:generateContent") && !strcmp(h.headers, "x-goog-api-key: AIzaXYZ\r\n") && !strstr(h.url, "AIza"), "gemini: %s [%s]", h.url, h.headers);
    CK(strstr(body, "{\"systemInstruction\":{\"parts\":[{\"text\":\"") && strstr(body, "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"dime algo\"}]}]") && strstr(body, "\"generationConfig\":{\"maxOutputTokens\":8192,\"responseMimeType\":\"application/json\"}}"), "gemini: cuerpo %s", body);
    CK(llm_proto_reply(&c, "{\n  \"candidates\": [\n    {\n      \"content\": {\n        \"parts\": [\n          {\n            \"text\": \"respuesta de gemini\"\n          }\n        ],\n        \"role\": \"model\"\n      },\n      \"finishReason\": \"STOP\"\n    }\n  ],\n  \"usageMetadata\": {}\n}", out, sizeof out) && !strcmp(out, "respuesta de gemini"), "gemini: respuesta [%s]", out);
    CK(llm_proto_error("{\n  \"error\": {\n    \"code\": 400,\n    \"message\": \"API key not valid.\",\n    \"status\": \"INVALID_ARGUMENT\"\n  }\n}", out, sizeof out) && !strcmp(out, "API key not valid."), "gemini: error [%s]", out);
    CK(llm_proto_model_list(&c, "{\"models\":[{\"name\":\"models/gemini-a\",\"displayName\":\"A\"},{\"name\":\"models/gemini-b\"}]}", out, sizeof out) == 2 && !strcmp(out, "gemini-a\ngemini-b\n"), "gemini: modelos [%s]", out);

    /* --- configuracion: ida y vuelta */
    uint32_t tl = llm_conf_format(&c, txt, sizeof txt);
    static struct llm_conf d; llm_conf_parse(&d, txt);
    { int same = tl > 0 && d.prov == LLM_GEMINI; for (int i = 0; i < LLM_NPROV; i++) same = same && !strcmp(c.base[i], d.base[i]) && !strcmp(c.model[i], d.model[i]) && !strcmp(c.key[i], d.key[i]);
      CK(same, "guardar y leer la configuracion"); }
    llm_conf_parse(&d, "basura sin igual\nproveedor=claude\nclave.claude=k1\nmodelo.nadie=x\nservidor.local=http://10.0.0.5:8080\n# comentario\nclave.openai=con espacio\n");
    CK(d.prov == LLM_CLAUDE && !strcmp(d.key[LLM_CLAUDE], "k1") && !strcmp(d.base[LLM_LOCAL], "http://10.0.0.5:8080") && d.key[LLM_OPENAI][0] == 0, "lectura tolerante");
    llm_conf_parse(&d, "");
    CK(d.prov == LLM_LOCAL && !strcmp(d.model[LLM_LOCAL], "nail-35b"), "sin archivo: lo de siempre");

    /* --- robustez: respuestas rotas, cortadas y limites */
    const char *broken[] = { "", "{", "{\"choices\":", "{\"choices\":[{\"message\":{\"content\":\"sin cerrar", "{\"content\":[{\"type\":\"text\",\"text\":\"a\\u12", "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":5}]}}]}", "\"\"\"\"\"", "{\"error\":null}", "[[[[\"x\":\"y\"]]]]", 0 };
    for (int p = 0; p < LLM_NPROV; p++) { c.prov = p; for (int i = 0; broken[i]; i++) { (void)llm_proto_reply(&c, broken[i], out, sizeof out); (void)llm_proto_error(broken[i], out, sizeof out); (void)llm_proto_model_list(&c, broken[i], out, sizeof out); } }
    c.prov = LLM_OPENAI;
    CK(!llm_proto_reply(&c, "{\"choices\":[{\"message\":{\"content\":null,\"refusal\":\"no\"}}]}", out, sizeof out), "contenido nulo");
    char small[8]; CK(llm_proto_reply(&c, "{\"choices\":[{\"message\":{\"content\":\"0123456789abcdef\"}}]}", small, sizeof small) && strlen(small) == 7, "salida pequena: se corta sin desbordar [%s]", small);
    static char huge[70000]; memset(huge, 'a', sizeof huge - 1);
    CK(llm_proto_chat(&c, huge, 0, body, sizeof body, &h) == LLM_P_TOOBIG, "peticion que no cabe");
    {   /* SOMA_196: lanzador */
        static struct llm_conf c; static struct llm_http h; char body[256], b[160], st[32], mo[80], de[200];
        llm_conf_defaults(&c);
        llm_launcher_base(&c, b, sizeof b); CK(!strcmp(b, "http://192.168.1.200:8086"));
        CK(llm_proto_launcher(&c, LLM_L_MODELS, 0, body, sizeof body, &h) == LLM_P_NOLKEY);
        CK(llm_conf_set(&c, 3, "clave-lanzador", "abcd2345") == 0);
        CK(llm_conf_set(&c, 0, "clave-lanzador", "con espacio") == -1);
        CK(llm_proto_launcher(&c, LLM_L_MODELS, 0, body, sizeof body, &h) == 0 && !strcmp(h.url, "http://192.168.1.200:8086/v1/modelos") && !strcmp(h.method, "GET") && strstr(h.headers, "Authorization: Bearer abcd2345\r\n"));
        int n = llm_proto_launcher(&c, LLM_L_START, "qwen\"x", body, sizeof body, &h);
        CK(n > 0 && !strcmp(body, "{\"modelo\":\"qwen\\\"x\"}") && !strcmp(h.method, "POST") && strstr(h.url, "/v1/lanzar"));
        CK(llm_proto_launcher(&c, LLM_L_START, "", body, sizeof body, &h) == LLM_P_NOMODEL);
        CK(llm_conf_set(&c, 0, "lanzador", "8.8.8.8:8086") == 0 && llm_proto_launcher(&c, LLM_L_STATUS, 0, body, sizeof body, &h) == LLM_P_INSECURE);
        CK(llm_conf_set(&c, 0, "lanzador", "10.0.0.7:9000") == 0);
        static char t[4096]; static struct llm_conf d; llm_conf_format(&c, t, sizeof t); llm_conf_parse(&d, t);
        CK(!strcmp(d.launcher, "http://10.0.0.7:9000") && !strcmp(d.launcher_key, "abcd2345") && !strcmp(d.base[0], c.base[0]));
        CK(llm_conf_set(&c, 0, "lanzador", "") == 0 && !c.launcher[0]);
        CK(llm_proto_launcher_status("{\"estado\": \"cargando\", \"modelo\": \"nail-35b\", \"detalle\": \"12 s\"}", st, sizeof st, mo, sizeof mo, de, sizeof de) && !strcmp(st, "cargando") && !strcmp(mo, "nail-35b") && !strcmp(de, "12 s"));
        CK(!llm_proto_launcher_status("{\"x\":1}", st, sizeof st, mo, sizeof mo, de, sizeof de));
    }
    printf("test_llm_proto_195: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
