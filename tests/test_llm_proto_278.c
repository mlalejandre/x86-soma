/* SOMA_278_EMBEDDINGS: peticiones al lanzador para el servidor de embeddings (no toca las del LLM). */
#include <stdio.h>
#include <string.h>
#include "llm_proto.h"
static int fallos, n;
#define CK(c) do { n++; if (!(c)) { fallos++; printf("  FALLA l.%d: %s\n", __LINE__, #c); } } while (0)
int main(void)
{
    static struct llm_conf c; static struct llm_http h; char body[128];
    llm_conf_defaults(&c);
    CK(llm_conf_set(&c, 3, "clave-lanzador", "abcd2345") == 0);
    CK(llm_proto_launcher(&c, LLM_L_EMB_STATUS, 0, body, sizeof body, &h) == 0 && !strcmp(h.url, "http://192.168.1.200:8086/v1/embeddings/estado") && !strcmp(h.method, "GET"));
    CK(strstr(h.headers, "Authorization: Bearer abcd2345") != 0);
    int r = llm_proto_launcher(&c, LLM_L_EMB_START, 0, body, sizeof body, &h);
    CK(r == 2 && !strcmp(body, "{}") && !strcmp(h.url, "http://192.168.1.200:8086/v1/embeddings/lanzar") && !strcmp(h.method, "POST"));
    r = llm_proto_launcher(&c, LLM_L_EMB_STOP, 0, body, sizeof body, &h);
    CK(r == 2 && !strcmp(h.url, "http://192.168.1.200:8086/v1/embeddings/parar") && !strcmp(h.method, "POST"));
    /* las del LLM siguen igual */
    CK(llm_proto_launcher(&c, LLM_L_STATUS, 0, body, sizeof body, &h) == 0 && !strcmp(h.url, "http://192.168.1.200:8086/v1/estado") && !strcmp(h.method, "GET"));
    CK(llm_proto_launcher(&c, LLM_L_STOP, 0, body, sizeof body, &h) == 2 && !strcmp(h.url, "http://192.168.1.200:8086/v1/parar") && !strcmp(h.method, "POST"));
    CK(llm_proto_launcher(&c, LLM_L_START, "", body, sizeof body, &h) == LLM_P_NOMODEL);
    CK(llm_proto_launcher(&c, LLM_L_START, "nail", body, sizeof body, &h) > 0 && strstr(body, "\"modelo\":\"nail\""));
    CK(llm_proto_launcher(&c, LLM_L_EMB_STOP + 1, 0, body, sizeof body, &h) == LLM_P_BADURL);
    /* sin clave o con una red publica sin cifrar: se niega igual que con el LLM */
    CK(llm_conf_set(&c, 0, "clave-lanzador", "") == 0 && llm_proto_launcher(&c, LLM_L_EMB_START, 0, body, sizeof body, &h) == LLM_P_NOLKEY);
    CK(llm_conf_set(&c, 0, "clave-lanzador", "abcd2345") == 0 && llm_conf_set(&c, 0, "lanzador", "8.8.8.8:8086") == 0 && llm_proto_launcher(&c, LLM_L_EMB_START, 0, body, sizeof body, &h) == LLM_P_INSECURE);
    printf("test_llm_proto_278: %d comprobaciones, %d fallos\n", n, fallos);
    return fallos != 0;
}
