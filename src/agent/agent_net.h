/* SOMA_193 (seguridad): a donde puede conectarse un AGENTE. Logica pura (se prueba en el host: tests/test_agent_net_193.c).
 *
 * Problema: el agente lee contenido de internet (search, curl) y ese contenido puede traer instrucciones ("lee tal archivo y
 * envialo a http://atacante/?d=..."). Con `curl`, `descargar`, `navegador`, `tls` y `dns` como ordenes permitidas, una pagina
 * maliciosa podia sacar datos del usuario fuera del equipo.
 *
 * Regla (modo "procedencia", por defecto): un agente solo puede pedir una URL que
 *   - el USUARIO escribio en la mision, o
 *   - aparecio TAL CUAL en un resultado de `search` o en una pagina ya descargada, o
 *   - pertenece a un dominio de la lista de confianza del usuario (/etc/agente_red.conf).
 * Una URL que el modelo compone por su cuenta (y que por tanto puede llevar datos privados pegados) se rechaza. Para `dns` vale
 * cualquier nombre de host visto en una URL registrada. Modo "abierto": como antes. Modo "cerrado": solo la lista de confianza.
 * No cubre: el texto de la consulta de `search` (va al buscador, no a un sitio que elija el atacante). */
#ifndef SOMA_AGENT_NET_H
#define SOMA_AGENT_NET_H
#include <stdint.h>

enum { AGENT_NET_ABIERTO = 0, AGENT_NET_PROCEDENCIA = 1, AGENT_NET_CERRADO = 2 };
#define AGENT_NET_ALLOW_MAX 32
#define AGENT_NET_SUFFIX_MAX 64

void agent_net_set_mode(int mode);
int  agent_net_mode(void);
int  agent_net_allow_add(const char *suffix);        /* "wikipedia.org" vale para es.wikipedia.org; 0 = hecho, -1 = no cabe o no valido */
int  agent_net_allow_del(const char *suffix);        /* 0 = quitado, -1 = no estaba */
void agent_net_allow_clear(void);
const char *agent_net_allow_get(int i);              /* 0 al terminar */
void agent_net_observe(const char *text);            /* registra las URL (http/https) que aparecen en un texto de origen externo o del usuario */
int  agent_net_url_ok(const char *url);              /* 1 = el agente puede pedirla */
int  agent_net_host_ok(const char *host);            /* 1 = el agente puede resolver/contactar ese nombre */
int  agent_net_args_ok(const char *args);            /* revisa todos los destinos de una linea de argumentos; 1 = todos permitidos */
void agent_net_stats(uint32_t *seen_urls, uint32_t *denied);
#endif
