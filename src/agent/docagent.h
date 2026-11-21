#ifndef SOMA_DOCAGENT_H
#define SOMA_DOCAGENT_H
/* SOMA_248_DOC: agente de documentos con acabado profesional.
 *
 *   peticion ("redacta un informe... en PDF", "prepara una presentacion de...", "maqueta un paper sobre...")
 *     -> tipo (informe, paper, revista, email, libro, presentacion, carta, cv)
 *     -> el LLM escribe HTML con la hoja de estilo de la casa (o LaTeX si el paper lleva matematicas)
 *     -> docgen lo maqueta y pagina como en papel (A4, A5, carta, 16:9, email) y revisa lo medible
 *     -> revision de director de arte: si el modelo ve imagenes (multimodal) recibe las paginas renderizadas;
 *        si no, el informe de la maquetacion. Devuelve el documento corregido (1-2 rondas, solo se acepta si no empeora)
 *     -> /workspace/docs/<nombre>.html|.tex + .pdf (y lo abre en el Maquetador)
 * Tambien: exportar a PDF/HTML/PNG y revisar con IA un documento que ya existe. */
#include <stdint.h>

int  docagent_should_handle(const char *mission);
/* progreso (opcional): texto corto de lo que esta haciendo */
void docagent_set_progress(void (*cb)(const char *msg));
/* Escribe el documento. out: resumen para el usuario (rutas). path_out (opcional): ruta del documento. 1 = bien. */
int  docagent_run(const char *mission, char *out, uint32_t max, char *path_out, uint32_t path_max);
/* Exporta un .html/.tex: fmt "pdf", "html" (autonomo) o "png" (una imagen por pagina). dst = ruta (0 = misma base). 1 = bien. */
int  docagent_export(const char *path, const char *fmt, const char *dst, char *out, uint32_t max);
/* Revisa con IA (con vision si el modelo la tiene) un documento existente y lo reescribe si mejora. 1 = bien (aunque no cambie). */
int  docagent_review(const char *path, const char *wishes, char *out, uint32_t max);
/* Tipo de documento que pide una mision (para pruebas): "informe", "paper", "presentacion"... */
const char *docagent_kind(const char *mission);
/* Indicaciones de sistema para escribir un documento de ese tipo (para pruebas y para el maquetador) */
const char *docagent_system_prompt(void);

/* piezas puras (pruebas en el host) */
uint32_t docagent_extract(const char *reply, char *out, uint32_t cap, int *is_tex);   /* el documento de una respuesta del LLM */
int  docagent_verdict(const char *reply);                 /* 1 = publicable / bien, 0 = corregir, -1 = sin veredicto */
void docagent_slug(const char *title, char *out, int max);   /* nombre de archivo: minusculas, sin acentos, guiones bajos */

/* Revision visual de programas (SOMA_248_DOC): critica de la interfaz de una app a partir de su captura.
 * png/len: captura en PNG. out: lista de mejoras (vacia si esta bien). 1 = hay mejoras, 0 = bien, <0 = sin vision/LLM. */
int  docagent_ui_review(const char *mission, const uint8_t *png, uint32_t len, char *out, uint32_t max);
#endif
