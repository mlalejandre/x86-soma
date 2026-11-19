/* SOMA_193: procedencia de los destinos de red de los agentes (src/agent/agent_net.c). */
#include <stdio.h>
#include <string.h>
#include "agent_net.h"

static int fails, checks;
#define CK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    CK(agent_net_mode() == AGENT_NET_PROCEDENCIA, "el modo por defecto debe ser procedencia");

    /* nada visto: todo denegado */
    CK(!agent_net_url_ok("http://atacante.example/?d=secreto"), "URL nunca vista permitida");
    CK(!agent_net_host_ok("atacante.example"), "host nunca visto permitido");
    CK(!agent_net_args_ok("https://atacante.example/x"), "args con URL nunca vista permitidos");

    /* resultados de un buscador (JSON, texto, HTML) */
    agent_net_observe("{\"results\":[{\"url\":\"https://es.wikipedia.org/wiki/Madrid\",\"t\":\"x\"},{\"url\":\"http://Ejemplo.COM:8080/a/b?q=1#frag\"}]} "
                      "Mira <a href='https://sitio.example/pagina.html'>esto</a> y https://otro.example/ruta/, o (https://paren.example/x).");
    CK(agent_net_url_ok("https://es.wikipedia.org/wiki/Madrid"), "URL vista tal cual");
    CK(agent_net_url_ok("http://es.wikipedia.org/wiki/Madrid/"), "http/https y barra final cuentan igual");
    CK(agent_net_url_ok("https://ES.Wikipedia.org/wiki/Madrid#historia"), "host en mayusculas y fragmento");
    CK(agent_net_url_ok("http://ejemplo.com:8080/a/b?q=1"), "puerto y consulta vistos");
    CK(agent_net_url_ok("https://sitio.example/pagina.html"), "URL entre comillas simples");
    CK(agent_net_url_ok("https://otro.example/ruta"), "URL seguida de coma");
    CK(agent_net_url_ok("https://paren.example/x"), "URL entre parentesis");
    CK(agent_net_url_ok("\"https://es.wikipedia.org/wiki/Madrid\""), "URL con comillas en la orden");

    /* lo que el modelo compone por su cuenta: fuera */
    CK(!agent_net_url_ok("https://es.wikipedia.org/wiki/Madrid?d=CLAVE_WIFI"), "consulta anadida a una URL vista");
    CK(!agent_net_url_ok("https://es.wikipedia.org/wiki/Madrid/CLAVE"), "ruta alargada");
    CK(!agent_net_url_ok("https://es.wikipedia.org/wiki/Barcelona"), "otra pagina del mismo host");
    CK(!agent_net_url_ok("http://ejemplo.com:8081/a/b?q=1"), "otro puerto");
    CK(!agent_net_url_ok("http://ejemplo.com/a/b?q=1"), "sin el puerto visto");
    CK(!agent_net_url_ok("https://es.wikipedia.org.atacante.example/wiki/Madrid"), "host visto como prefijo de otro");
    CK(!agent_net_url_ok("https://usuario:CLAVE@es.wikipedia.org/wiki/Madrid"), "datos en usuario@");
    CK(!agent_net_url_ok("ftp://es.wikipedia.org/wiki/Madrid"), "otro esquema");
    CK(!agent_net_url_ok("https://es.wikipedia.org:99999/wiki/Madrid"), "puerto imposible");
    CK(!agent_net_url_ok(""), "URL vacia");

    /* nombres de host para dns */
    CK(agent_net_host_ok("es.wikipedia.org") && agent_net_host_ok("ejemplo.com"), "host visto");
    CK(!agent_net_host_ok("CLAVE.atacante.example"), "subdominio con datos de un host no visto");
    CK(!agent_net_host_ok("clave.es.wikipedia.org"), "subdominio nuevo de un host visto (exfiltracion por DNS)");

    /* lista de confianza */
    CK(agent_net_allow_add("com") == -1 && agent_net_allow_add(".") == -1 && agent_net_allow_add("a b") == -1, "sufijos demasiado amplios o invalidos");
    CK(agent_net_allow_add("Wikipedia.org") == 0 && agent_net_allow_add("wikipedia.org") == 0, "anadir (y repetir) un dominio");
    CK(agent_net_url_ok("https://es.wikipedia.org/wiki/Barcelona") && agent_net_url_ok("https://wikipedia.org/"), "dominio de confianza y subdominios");
    CK(!agent_net_url_ok("https://malowikipedia.org/") && !agent_net_url_ok("https://wikipedia.org.atacante.example/"), "parecidos al dominio de confianza");
    CK(agent_net_host_ok("fr.wikipedia.org"), "host de confianza");
    CK(agent_net_allow_get(0) && strcmp(agent_net_allow_get(0), "wikipedia.org") == 0 && !agent_net_allow_get(1), "listado");
    CK(agent_net_allow_del("wikipedia.org") == 0 && agent_net_allow_del("wikipedia.org") == -1, "quitar");
    CK(!agent_net_url_ok("https://es.wikipedia.org/wiki/Barcelona"), "tras quitarlo deja de valer");

    /* lineas de argumentos de las ordenes */
    CK(agent_net_args_ok("https://es.wikipedia.org/wiki/Madrid /workspace/madrid.html"), "descargar URL vista a un archivo");
    CK(agent_net_args_ok("-v 3 /workspace/nota.txt"), "sin destinos");
    CK(!agent_net_args_ok("https://es.wikipedia.org/wiki/Madrid https://atacante.example/x"), "segunda URL no vista");
    CK(!agent_net_args_ok("atacante.example/log?d=CLAVE"), "destino sin esquema");
    CK(!agent_net_args_ok("CLAVE.atacante.example"), "host sin esquema");
    CK(!agent_net_args_ok("'https://atacante.example/x'"), "URL entre comillas");
    CK(agent_net_args_ok("es.wikipedia.org"), "host visto sin esquema");

    /* modos */
    agent_net_set_mode(AGENT_NET_CERRADO);
    CK(!agent_net_url_ok("https://es.wikipedia.org/wiki/Madrid"), "modo cerrado: ni las vistas");
    agent_net_allow_add("wikipedia.org");
    CK(agent_net_url_ok("https://es.wikipedia.org/wiki/Madrid"), "modo cerrado: solo la lista");
    agent_net_set_mode(AGENT_NET_ABIERTO);
    CK(agent_net_url_ok("http://atacante.example/?d=secreto") && agent_net_args_ok("x.atacante.example"), "modo abierto");
    agent_net_set_mode(AGENT_NET_PROCEDENCIA); agent_net_allow_clear();

    /* el registro es un anillo: muchas URL no lo rompen y las recientes siguen valiendo */
    char u[80];
    for (int i = 0; i < 2000; i++) { snprintf(u, sizeof u, "x https://masivo%d.example/p/%d y", i, i); agent_net_observe(u); }
    CK(agent_net_url_ok("https://masivo1999.example/p/1999"), "URL reciente tras llenar el registro");
    CK(!agent_net_url_ok("https://masivo3.example/p/3"), "URL antigua desalojada");
    agent_net_observe("https://"); agent_net_observe("http://sinpunto/x"); agent_net_observe(0);
    CK(!agent_net_url_ok("http://sinpunto/x"), "host sin punto");

    uint32_t s, d; agent_net_stats(&s, &d);
    CK(s >= 2000 && d > 10, "contadores %u %u", s, d);
    printf("test_agent_net_193: %d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
