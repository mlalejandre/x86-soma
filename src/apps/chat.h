#ifndef SOMA_CHAT_H
#define SOMA_CHAT_H
/* SOMA_285_CHAT: la aplicacion SOMA, un chat moderno con el agente (al estilo de Open WebUI).
 *   - Historial de conversaciones a la izquierda (hoy, ayer, 7 dias...), busqueda, fijar, renombrar, borrar, exportar a Markdown.
 *   - Dos modos por conversacion: "Solo LLM" (el modelo configurado, con toda la conversacion como contexto) y "Agente" (el agente
 *     SOMA con sus herramientas: ordenes, web, archivos, documentos, programas...). Interruptor de Memoria (la memoria nueva: contexto
 *     y aprendizaje; apagado, la conversacion ni consulta ni deja recuerdos).
 *   - Respuestas en Markdown con LaTeX (motor propio), tablas, codigo resaltado con boton Copiar, enlaces (se abren en el Navegador),
 *     imagenes locales y de la web. Adjuntos: textos (van dentro de la pregunta), imagenes (al modelo, si tiene vision) y cualquier
 *     archivo para el agente.
 *   - Copiar, regenerar, editar y reenviar, leer en voz alta. Indicador de trabajo con los pasos del agente en vivo.
 * Se guarda en /chat/historial.txt. */
#include <stdint.h>

void chat_init(void);
void chat_render_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int focused);
/* clic: 0 nada, 1 repintar, 3 abrir chat_url() en el Navegador, 4 abrir chat_path() en Notas, 5 abrir chat_path() en el Visor de imagenes */
int  chat_handle_click(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  chat_mouse(int32_t mx, int32_t my, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int wheel, int held);   /* 1 = repintar */
void chat_handle_key(char c);
int  chat_tick(void);                    /* 1 = hay algo nuevo que pintar */
uint32_t chat_version(void);
int  chat_clip_op(int op);
int  chat_escape(void);                  /* 1 = Esc usado (cierra una ventana, para la respuesta...) */
const char *chat_url(void);
const char *chat_path(void);
/* desde el agente (cualquier hilo): fases del trabajo y ordenes; desde el bucle de la GUI: fin del turno del agente */
void chat_fase(const char *que);
void chat_fase_orden(const char *linea);
int  chat_agente_fin(const char *respuesta);   /* 1 = el turno era del chat */
#endif
