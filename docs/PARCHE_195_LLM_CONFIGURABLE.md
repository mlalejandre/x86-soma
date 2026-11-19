# Parche 195: el modelo de lenguaje se elige desde SOMA

Requiere el parche 194 (las API en la nube van por el TLS propio con el certificado verificado).

## El problema

El servidor del LLM estaba fijo en el codigo (`192.168.1.200:8087`, modelo `nail-35b`). En QEMU funcionaba porque el script del
anfitrion ya dejaba ese servidor a mano; en un equipo real (T480s) no habia forma de decirle a SOMA con que modelo hablar.

## Lo que cambia

- **Ajustes > IA** (pagina nueva): proveedor (Local / red, ChatGPT, Claude, Gemini), servidor, modelo y clave; "Ver modelos" pide la
  lista al servidor y se elige con un clic; "Guardar y probar" guarda y hace una pregunta de prueba. La red va en un hilo aparte:
  el escritorio no se para mientras el modelo contesta.
- **Orden `llm-config`** (terminal): lo mismo sin raton.

      llm-config                               estado
      llm-config proveedor local|openai|claude|gemini
      llm-config servidor 192.168.1.50:8080    (o https://...)
      llm-config modelos                       lista numerada del servidor
      llm-config modelo 3                      (numero de la lista o nombre)
      llm-config clave sk-...                  (no queda en el historial)
      llm-config archivo-clave /ruta/clave.txt (la clave desde un archivo, p. ej. de un USB)
      llm-config probar

- Se guarda en `/etc/llm.conf`, que un agente no puede leer (parche 193). Cada proveedor conserva su servidor, modelo y clave.
- Sin `/etc/llm.conf` todo sigue como antes (192.168.1.200:8087, nail-35b). Un servidor local dado por IP y sin clave se pide por
  el mismo camino de siempre.

## Proveedores

| Proveedor | Servidor por defecto | Peticion | Cabecera de la clave |
|---|---|---|---|
| local | http://192.168.1.200:8087 | POST /v1/chat/completions (compatible con OpenAI: llama.cpp, Ollama, LM Studio, vLLM) | Authorization: Bearer (solo si hay clave) |
| openai | https://api.openai.com | POST /v1/chat/completions | Authorization: Bearer |
| claude | https://api.anthropic.com | POST /v1/messages (anthropic-version: 2023-06-01) | x-api-key |
| gemini | https://generativelanguage.googleapis.com | POST /v1beta/models/<modelo>:generateContent | x-goog-api-key |

El servidor se puede cambiar en cualquier proveedor (un proxy propio, OpenRouter y similares con el formato de OpenAI, etc.).
Ollama en el mismo equipo de la red: `llm-config servidor IP:11434`.

## Seguridad

- La clave solo viaja por HTTPS con certificado verificado, o por http dentro de la red local (10.x, 172.16-31.x, 192.168.x,
  127.x, 169.254.x). Por http a un servidor de fuera se rechaza.
- Las peticiones al LLM no siguen redirecciones, no llevan las cookies del navegador y no pasan por la pasarela web.
- `llm-config` no es una orden de agente; la clave se oculta en el historial y en pantalla solo se ven sus 4 ultimos caracteres.
- Aviso: con un proveedor en la nube, las misiones, lo que el agente lee y las respuestas salen del equipo hacia ese proveedor.

## Archivos

Nuevos: `src/agent/llm_proto.c/.h` (formatos de cada proveedor, sin dependencias del nucleo), `tests/test_llm_proto_195.c`.
Cambiados: `src/agent/llm.c/.h`, `src/agent/shell.c`, `src/apps/settings.c`, `src/apps/web_net.c/.h` (cabeceras extra y conexion
directa), `src/ui/console.c`, `Makefile`, `protected.txt`.
