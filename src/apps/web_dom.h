#ifndef SOMA_WEB_DOM_H
#define SOMA_WEB_DOM_H
#include <stdint.h>
#include "web_util.h"

enum { WN_ELEM = 1, WN_TEXT = 3, WN_COMMENT = 8, WN_DOC = 9 };

enum wtag {
    WT_NONE = 0, WT_HTML, WT_HEAD, WT_BODY, WT_TITLE, WT_META, WT_LINK, WT_STYLE, WT_SCRIPT, WT_NOSCRIPT, WT_BASE,
    WT_DIV, WT_SPAN, WT_P, WT_A, WT_IMG, WT_BR, WT_HR, WT_H1, WT_H2, WT_H3, WT_H4, WT_H5, WT_H6,
    WT_UL, WT_OL, WT_LI, WT_DL, WT_DT, WT_DD, WT_TABLE, WT_THEAD, WT_TBODY, WT_TFOOT, WT_TR, WT_TD, WT_TH,
    WT_CAPTION, WT_COLGROUP, WT_COL, WT_FORM, WT_INPUT, WT_BUTTON, WT_SELECT, WT_OPTION, WT_OPTGROUP,
    WT_TEXTAREA, WT_LABEL, WT_FIELDSET, WT_LEGEND, WT_PRE, WT_CODE, WT_BLOCKQUOTE, WT_B, WT_STRONG, WT_I,
    WT_EM, WT_U, WT_S, WT_SMALL, WT_SUB, WT_SUP, WT_IFRAME, WT_SVG, WT_CANVAS, WT_VIDEO, WT_AUDIO,
    WT_NAV, WT_HEADER, WT_FOOTER, WT_MAIN, WT_SECTION, WT_ARTICLE, WT_ASIDE, WT_FIGURE, WT_FIGCAPTION,
    WT_DETAILS, WT_SUMMARY, WT_CENTER, WT_FONT, WT_WBR, WT_SOURCE, WT_EMBED, WT_PARAM, WT_TRACK,
    WT_ADDRESS, WT_MENU, WT_DIALOG, WT_TEMPLATE, WT_OBJECT, WT_PICTURE, WT_AREA, WT_MAP, WT_KBD, WT_SAMP,
    WT_MARK, WT_Q, WT_CITE, WT_ABBR, WT_DEL, WT_INS, WT_BIG, WT_STRIKE, WT_TT, WT_DFN, WT_VAR, WT_TIME,
    WT_OUTPUT, WT_PROGRESS, WT_METER, WT_DATALIST, WT_FRAME, WT_FRAMESET, WT_RUBY, WT_RT, WT_RP, WT_BDI, WT_BDO,
    WT_COUNT
};

#define WNF_VOID     0x01
#define WNF_INDOC    0x02   /* conectado al arbol del documento */
#define WNF_CHECKED  0x04
#define WNF_FOCUS    0x08
#define WNF_DISABLED 0x10
#define WNF_HOVER    0x20
#define WNF_DIRTY    0x40   /* estilo/maquetacion a recalcular */
#define WNF_OPEN     0x80
#define WNF_EDITED   0x100  /* valor de formulario modificado por el usuario */
#define WNF_PSEUDO   0x200
#define WNF_IMGEV    0x400  /* SOMA_346: ya se le disparo load/error a esta <img> con su src actual */  /* SOMA_188: ::before/::after sintetico (fuera de la lista de hijos; invisible para el DOM de JS) */

struct wattr { struct wattr *next; const char *name; const char *val; };

struct wnode {
    uint8_t  type, tag;
    uint16_t flags;
    struct wnode *parent, *first, *last, *next, *prev;
    const char *name;              /* elemento: nombre en minusculas; texto/comentario: datos */
    uint32_t tlen;                 /* longitud del texto */
    struct wattr *attrs;
    struct wstyle *st;             /* estilo calculado (web_css) */
    struct wbox *box;              /* caja de maquetacion (web_layout) */
    void *js;                      /* envoltorio JS */
    const char *value;             /* valor actual de input/textarea/select (NULL = usar atributo) */
    int32_t sel;                   /* cursor / opcion elegida */
    int32_t scy;                   /* SOMA_181: desplazamiento vertical interno (overflow:auto/scroll) */
    uint32_t uid;
    uint32_t cbloom_gen;           /* SOMA_152: generacion del filtro de clases (web_css) */
    uint64_t cbloom;               /* SOMA_152: filtro de Bloom de las clases del elemento */
    uint32_t trsig, ansig;         /* SOMA_188: firma de propiedades animables y de animation-name (transitionend/animationend) */
    uint8_t trop, trsig_ok;
    struct wnode *shadow;          /* SOMA_188: raiz sombra ("#shadow-root", parent = anfitrion; no esta en la lista de hijos) */
};
/* SOMA_188: recorrido del arbol compuesto: anfitrion, su arbol sombra y despues sus hijos (los asignados a <slot> incluidos) */
static inline int wd_is_shadow_root(const struct wnode *n) { return n && n->name && n->name[0] == '#' && n->name[1] == 's' && n->parent && n->parent->shadow == n; }
static inline struct wnode *wd_walk_next(struct wnode *n, const struct wnode *stop)
{
    if (n->type == 1 && n->shadow && n->shadow->first) return n->shadow->first;
    if (n->first) return n->first;
    while (n && n != stop) {
        if (n->next) return n->next;
        struct wnode *p = n->parent;
        if (wd_is_shadow_root(p)) { struct wnode *h = p->parent; if (h->first) return h->first; n = h; continue; }
        n = p;
    }
    return 0;
}

struct wdoc {
    struct wa_arena ar;
    struct wnode *root, *html, *head, *body;
    const char *title;
    char base[4096];               /* URL base del documento */
    uint32_t next_uid;
    struct wnode *focus;
    void *sheets;                  /* hojas de estilo (web_css) */
    uint32_t nodes;
};

/* creacion / DOM */
struct wdoc *wd_new(void);
void wd_free(struct wdoc *d);
struct wnode *wd_new_elem(struct wdoc *d, const char *name, uint32_t nlen);
struct wnode *wd_new_text(struct wdoc *d, const char *s, uint32_t n);
void wd_append(struct wnode *parent, struct wnode *c);
void wd_insert_before(struct wnode *parent, struct wnode *c, struct wnode *ref);
void wd_remove(struct wnode *c);
const char *wd_attr(const struct wnode *n, const char *name);
int web_feat_off(const char *name);   /* SOMA_188: soma.<name>=off */
void wd_set_attr(struct wdoc *d, struct wnode *n, const char *name, const char *val);
void wd_del_attr(struct wnode *n, const char *name);
int  wd_tag_of(const char *name, uint32_t n);
struct wnode *wd_by_id(struct wnode *root, const char *id);
int  wd_has_class(const struct wnode *n, const char *cls);
void wd_text_content(const struct wnode *n, struct wb *out);
void wd_set_text(struct wdoc *d, struct wnode *n, const char *s);
void wd_outer_html(const struct wnode *n, struct wb *out, int inner_only);
struct wnode *wd_clone(struct wdoc *d, const struct wnode *n, int deep);
int  wd_is_ancestor(const struct wnode *a, const struct wnode *n);

/* analisis */
void wd_parse(struct wdoc *d, const char *html, uint32_t len);
void wd_parse_fragment(struct wdoc *d, struct wnode *ctx, const char *html, uint32_t len);
void wd_decode_entities(const char *s, uint32_t n, struct wb *out);
uint32_t wd_latin1_to_utf8(const uint8_t *in, uint32_t n, char *out, uint32_t max);
#endif
