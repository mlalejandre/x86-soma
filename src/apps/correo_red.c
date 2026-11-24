/* correo_red.c: SOMA_295_CORREO: sesiones IMAP4rev1 (RFC 3501) y SMTP (RFC 5321) sobre el transporte que se le de. Ver correo_red.h. */
#include "correo_red.h"

static void mcpy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (uint32_t i = 0; i < n; i++) a[i] = b[i]; }
static void mmov(char *d, const char *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = s[i]; }   /* d < s */
static void pon_err(char *e, uint32_t cap, const char *a, const char *b) { co_cpy(e, a, cap); if (b) co_cat(e, b, cap); }

/* ================================================================ IMAP: lectura */
static int asegura(struct co_imap *s, uint32_t need)
{
    if (need <= s->rcap) return 1;
    if (need > (48u << 20)) { pon_err(s->err, sizeof s->err, "La respuesta del servidor es demasiado grande", 0); return 0; }
    uint32_t nc = s->rcap ? s->rcap : 65536; while (nc < need) nc *= 2;
    char *nb = (char *)co_alloc(nc + 1);
    if (!nb) { pon_err(s->err, sizeof s->err, "Sin memoria para la respuesta del servidor", 0); return 0; }
    if (s->rb) { mcpy(nb, s->rb, s->rn); co_free(s->rb); }
    s->rb = nb; s->rcap = nc;
    return 1;
}
/* la siguiente respuesta completa queda en rb[0..*len) */
static int lee_resp(struct co_imap *s, uint32_t *len, uint32_t espera)
{
    uint64_t t0 = s->t->ms();
    for (;;) {
        uint32_t c = s->rn ? co_imap_completa(s->rb, s->rn) : 0;
        if (c) { *len = c; return 1; }
        if (!asegura(s, s->rn + 16384)) return 0;
        /* un literal anunciado: reservar de una vez lo que va a ocupar */
        { uint32_t i = s->rn; while (i > 0 && s->rb[i - 1] != '\n') i--;
          if (i >= 3 && s->rb[i - 2] == '\r' && s->rb[i - 3] == '}') {
              uint32_t k = i - 3, v = 0, mul = 1; while (k > 0 && s->rb[k - 1] >= '0' && s->rb[k - 1] <= '9') { v += (uint32_t)(s->rb[k - 1] - '0') * mul; mul *= 10; k--; }
              if (k > 0 && s->rb[k - 1] == '{' && !asegura(s, s->rn + v + 16384)) return 0;
          } }
        if (s->t->cancela && *s->t->cancela) { pon_err(s->err, sizeof s->err, "Cancelado", 0); return 0; }
        int r = s->t->recibe(s->t->ctx, s->rb + s->rn, s->rcap - s->rn, 250);
        if (r < 0) { pon_err(s->err, sizeof s->err, "El servidor de correo ha cerrado la conexi\303\263n", 0); s->abierta = 0; return 0; }
        if (r > 0) { s->rn += (uint32_t)r; t0 = s->t->ms(); continue; }
        if (s->t->ms() - t0 > espera) { pon_err(s->err, sizeof s->err, "El servidor de correo no responde", 0); return 0; }
    }
}
static void consume(struct co_imap *s, uint32_t n) { if (n >= s->rn) { s->rn = 0; return; } mmov(s->rb, s->rb + n, s->rn - n); s->rn -= n; }

typedef void (*co_cb)(struct co_imap *s, const char *r, uint32_t n, void *u);
static void etiqueta_nueva(struct co_imap *s, char *tag) { s->tag++; tag[0] = 'S'; tag[1] = 0; co_num(tag, 12, s->tag); }
/* espera la respuesta con esa etiqueta; las demas van a cb. 0 OK, -1 NO/BAD o error. lit: 1 = se espera un "+" (devuelve 1 al llegar) */
static int espera_etiqueta(struct co_imap *s, const char *tag, co_cb cb, void *u, int lit, uint32_t espera)
{
    uint32_t tl = co_len(tag);
    for (;;) {
        uint32_t n;
        if (!lee_resp(s, &n, espera)) return -1;
        const char *r = s->rb;
        if (lit && r[0] == '+') { consume(s, n); return 1; }
        char txt[200];
        int v = co_imap_etiqueta(r, n, tag, txt, sizeof txt);
        if (v) {
            if (v < 0) { pon_err(s->err, sizeof s->err, "El servidor de correo dice: ", txt); }
            else if (co_ibusca(r, n, "[CAPABILITY ")) { uint32_t k = n < sizeof s->caps - 1 ? n : sizeof s->caps - 1; co_cpyn(s->caps, r, k, sizeof s->caps); }
            consume(s, n);
            return v > 0 ? 0 : -1;
        }
        if (n > tl && r[0] == '*' && co_iempieza(r + 2, "BYE")) { co_cpyn(txt, r + 6, n - 6 < 190 ? n - 6 : 190, sizeof txt); pon_err(s->err, sizeof s->err, "El servidor de correo cierra: ", txt); }
        if (r[0] == '*' && co_iempieza(r + 2, "CAPABILITY")) { uint32_t k = n < sizeof s->caps - 1 ? n : sizeof s->caps - 1; co_cpyn(s->caps, r, k, sizeof s->caps); }
        if (cb && r[0] == '*') cb(s, r, n, u);
        consume(s, n);
    }
}
static int envia_txt(struct co_imap *s, const char *a, const char *b, const char *c)
{
    const char *p[3] = { a, b, c };
    for (int i = 0; i < 3; i++) if (p[i] && p[i][0] && s->t->envia(s->t->ctx, p[i], co_len(p[i])) != 0) { pon_err(s->err, sizeof s->err, "No se pudo hablar con el servidor de correo", 0); s->abierta = 0; return -1; }
    return 0;
}
static int orden(struct co_imap *s, const char *texto, co_cb cb, void *u, uint32_t espera)
{
    char tag[12]; etiqueta_nueva(s, tag);
    if (envia_txt(s, tag, " ", texto) || envia_txt(s, "\r\n", 0, 0)) return -1;
    return espera_etiqueta(s, tag, cb, u, 0, espera);
}
/* orden con un literal: "pre{n}\r\n" + dato + "post\r\n" */
static int orden_lit(struct co_imap *s, const char *pre, const char *dato, uint32_t dn, const char *post, co_cb cb, void *u)
{
    char tag[12]; etiqueta_nueva(s, tag);
    char l[24] = "{"; co_num(l, sizeof l, dn); co_cat(l, "}\r\n", sizeof l);
    if (envia_txt(s, tag, " ", pre) || envia_txt(s, l, 0, 0)) return -1;
    int r = espera_etiqueta(s, tag, cb, u, 1, CO_ESPERA_MS);
    if (r != 1) return -1;                                                  /* rechazado antes del literal */
    if (s->t->envia(s->t->ctx, dato, dn) != 0 || envia_txt(s, post, "\r\n", 0)) { pon_err(s->err, sizeof s->err, "No se pudo hablar con el servidor de correo", 0); return -1; }
    return espera_etiqueta(s, tag, cb, u, 0, CO_ESPERA_MS * 2);
}
static void quote(char *o, uint32_t cap, const char *s) { char q[400]; if (co_imap_cadena(s, q, sizeof q)) co_cat(o, q, cap); else co_cat(o, "\"\"", cap); }

/* ================================================================ IMAP: sesion */
static int capacidades(struct co_imap *s) { s->caps[0] = 0; return orden(s, "CAPABILITY", 0, 0, CO_ESPERA_MS); }
int co_imap_abre(struct co_imap *s, struct co_tr *t, const struct co_cuenta *c)
{
    char *rb = s->rb; uint32_t rcap = s->rcap;
    for (uint32_t i = 0; i < sizeof *s; i++) ((uint8_t *)s)[i] = 0;
    s->rb = rb; s->rcap = rcap; s->t = t;
    if (!c->imap[0] || !c->imap_p) { pon_err(s->err, sizeof s->err, "Falta el servidor IMAP en la cuenta", 0); return -1; }
    if (t->abre(t->ctx, c->imap, c->imap_p, c->imap_tls != CO_TLS_STARTTLS, s->err, sizeof s->err) != 0) return -1;   /* sin cifrar, nunca */
    s->abierta = 1;
    uint32_t n;
    if (!lee_resp(s, &n, CO_ESPERA_MS)) { co_imap_cierra(s); return -1; }
    if (!(s->rb[0] == '*' && (co_iempieza(s->rb + 2, "OK") || co_iempieza(s->rb + 2, "PREAUTH")))) {
        char txt[160]; co_cpyn(txt, s->rb, n < 150 ? n : 150, sizeof txt);
        pon_err(s->err, sizeof s->err, "El servidor no parece IMAP: ", txt); s->abierta = 0; co_imap_cierra(s); return -1;
    }
    if (co_imap_cap(s->rb, n, "CAPABILITY")) co_cpyn(s->caps, s->rb, n < sizeof s->caps - 1 ? n : sizeof s->caps - 1, sizeof s->caps);
    consume(s, n);
    if (!s->caps[0] && capacidades(s)) { co_imap_cierra(s); return -1; }
    if (c->imap_tls == CO_TLS_STARTTLS) {
        if (!co_imap_cap(s->caps, co_len(s->caps), "STARTTLS")) { pon_err(s->err, sizeof s->err, "El servidor IMAP no ofrece STARTTLS: SOMA no manda la contrase\303\261a sin cifrar", 0); co_imap_cierra(s); return -1; }
        if (orden(s, "STARTTLS", 0, 0, CO_ESPERA_MS)) { co_imap_cierra(s); return -1; }
        s->rn = 0;
        if (t->starttls(t->ctx, c->imap, s->err, sizeof s->err)) { co_imap_cierra(s); return -1; }
        if (capacidades(s)) { co_imap_cierra(s); return -1; }
    }
    int ok;
    if (co_imap_cap(s->caps, co_len(s->caps), "AUTH=PLAIN")) {
        char b[700]; co_smtp_plain(c->usuario[0] ? c->usuario : c->email, c->clave, b, sizeof b);
        if (co_imap_cap(s->caps, co_len(s->caps), "SASL-IR")) { char o[760] = "AUTHENTICATE PLAIN "; co_cat(o, b, sizeof o); ok = orden(s, o, 0, 0, CO_ESPERA_MS); }
        else {
            char tag[12]; etiqueta_nueva(s, tag);
            ok = -1;
            if (!envia_txt(s, tag, " AUTHENTICATE PLAIN\r\n", 0) && espera_etiqueta(s, tag, 0, 0, 1, CO_ESPERA_MS) == 1 && !envia_txt(s, b, "\r\n", 0))
                ok = espera_etiqueta(s, tag, 0, 0, 0, CO_ESPERA_MS);
        }
    } else if (co_imap_cap(s->caps, co_len(s->caps), "LOGINDISABLED")) {
        pon_err(s->err, sizeof s->err, "El servidor IMAP no admite entrar con contrase\303\261a", 0); co_imap_cierra(s); return -1;
    } else {
        char o[700] = "LOGIN "; quote(o, sizeof o, c->usuario[0] ? c->usuario : c->email); co_cat(o, " ", sizeof o);
        char q[400];
        if (co_imap_cadena(c->clave, q, sizeof q)) { co_cat(o, q, sizeof o); ok = orden(s, o, 0, 0, CO_ESPERA_MS); }
        else ok = orden_lit(s, o, c->clave, co_len(c->clave), "", 0, 0);
    }
    if (ok) {
        char srv[200]; co_cpy(srv, s->err, sizeof srv);
        pon_err(s->err, sizeof s->err, "El servidor no acepta el usuario o la contrase\303\261a. ", srv);
        co_imap_cierra(s); return -1;
    }
    (void)capacidades(s);                                                   /* despues de entrar cambian (MOVE, UIDPLUS...) */
    uint32_t cl = co_len(s->caps);
    s->move = co_imap_cap(s->caps, cl, "MOVE"); s->uidplus = co_imap_cap(s->caps, cl, "UIDPLUS"); s->gmail = co_imap_cap(s->caps, cl, "X-GM-EXT-1");
    return 0;
}
void co_imap_cierra(struct co_imap *s)
{
    char err[sizeof s->err]; co_cpy(err, s->err, sizeof err);              /* el BYE del LOGOUT no tapa el motivo de un fallo */
    if (s->abierta) { (void)orden(s, "LOGOUT", 0, 0, 3000); s->t->cierra(s->t->ctx); }
    else if (s->t) s->t->cierra(s->t->ctx);
    s->abierta = 0;
    if (s->rb) co_free(s->rb);
    s->rb = 0; s->rn = s->rcap = 0; s->sel[0] = 0;
    co_cpy(s->err, err, sizeof s->err);
}
struct lista_c { struct co_carpeta *out; int max, n; };
static void cb_list(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct lista_c *l = (struct lista_c *)u;
    if (l->n < l->max && co_imap_list(r, n, &l->out[l->n])) l->n++;
}
int co_imap_carpetas(struct co_imap *s, struct co_carpeta *out, int max)
{
    struct lista_c l = { out, max, 0 };
    if (orden(s, "LIST \"\" \"*\"", cb_list, &l, CO_ESPERA_MS)) return -1;
    return l.n;
}
struct estado_c { uint32_t nl, tot; };
static void cb_status(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct estado_c *e = (struct estado_c *)u;
    if (n > 9 && co_iempieza(r + 2, "STATUS")) { (void)co_imap_num(r, n, "UNSEEN", &e->nl); (void)co_imap_num(r, n, "MESSAGES", &e->tot); }
}
int co_imap_estado(struct co_imap *s, const char *imap, uint32_t *no_leidos, uint32_t *total)
{
    char o[300] = "STATUS "; quote(o, sizeof o, imap); co_cat(o, " (MESSAGES UNSEEN)", sizeof o);
    struct estado_c e = { 0, 0 };
    if (orden(s, o, cb_status, &e, CO_ESPERA_MS)) return -1;
    *no_leidos = e.nl; *total = e.tot;
    return 0;
}
static void cb_select(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)u; uint32_t v;
    if (co_imap_num(r, n, "EXISTS", &v)) s->exists = v;
    if (co_imap_num(r, n, "UIDVALIDITY", &v)) s->uidvalidity = v;
    if (co_imap_num(r, n, "UIDNEXT", &v)) s->uidnext = v;
}
int co_imap_selecciona(struct co_imap *s, const char *imap, int escribir)
{
    char o[300]; co_cpy(o, escribir ? "SELECT " : "EXAMINE ", sizeof o); quote(o, sizeof o, imap);
    s->exists = s->uidvalidity = s->uidnext = 0; s->sel[0] = 0;
    if (orden(s, o, cb_select, 0, CO_ESPERA_MS)) return -1;
    co_cpy(s->sel, imap, sizeof s->sel); s->sel_rw = escribir;
    return 0;
}

/* ---------------------------------------------------------------- mensajes */
#define ITEMS_LISTA "(UID FLAGS RFC822.SIZE BODY.PEEK[HEADER.FIELDS (FROM TO CC SUBJECT DATE MESSAGE-ID CONTENT-TYPE CONTENT-TRANSFER-ENCODING)] BODY.PEEK[TEXT]<0.3000>)"
struct col_m { struct co_msg *out; int max, n; uint32_t desde; };
static void msg_de_fetch(struct co_msg *m, const struct co_fetch *f)
{
    m->uid = f->uid; m->flags = f->flags; m->tam = f->tam;
    co_msg_de_cab(m, f->cab ? f->cab : "", f->cab ? f->cab_n : 0);
    if (!f->cab || !f->cuerpo) return;
    uint32_t n = f->cab_n + f->cuerpo_n + 4;
    char *raw = (char *)co_alloc(n + 1), *tx = (char *)co_alloc(4000); struct co_mime *mm = (struct co_mime *)co_alloc(sizeof *mm);
    if (raw && tx && mm) {
        uint32_t p = 0; mcpy(raw, f->cab, f->cab_n); p = f->cab_n;
        if (p < 2 || raw[p - 1] != '\n' || (raw[p - 2] != '\n' && !(p >= 4 && raw[p - 3] == '\n'))) { raw[p++] = '\r'; raw[p++] = '\n'; }
        mcpy(raw + p, f->cuerpo, f->cuerpo_n); p += f->cuerpo_n; raw[p] = 0;
        if (co_mime_lee(raw, p, mm, tx, 4000)) {
            co_extracto(tx, mm->texto_n, m->extracto, sizeof m->extracto);
            int reales = 0; for (int i = 0; i < mm->nadj; i++) if (!mm->adj[i].en_linea) reales++;
            if (reales) m->adj = 1;
        }
    }
    co_free(raw); co_free(tx); co_free(mm);
}
static void cb_fetch_m(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct col_m *c = (struct col_m *)u; struct co_fetch f;
    if (!co_imap_fetch(r, n, &f) || !f.uid || f.uid <= c->desde) return;
    struct co_msg tmp, *m;
    if (c->n < c->max) m = &c->out[c->n++];
    else {                                                                  /* lleno: sustituye al de UID mas bajo si este es mas nuevo */
        int k = 0; for (int i = 1; i < c->n; i++) if (c->out[i].uid < c->out[k].uid) k = i;
        if (c->out[k].uid > f.uid) return;
        m = &tmp;
        msg_de_fetch(m, &f); c->out[k] = tmp; return;
    }
    msg_de_fetch(m, &f);
}
static void ordena_uid(struct co_msg *v, int n)
{
    for (int i = 1; i < n; i++) { struct co_msg t = v[i]; int j = i; while (j > 0 && v[j - 1].uid < t.uid) { v[j] = v[j - 1]; j--; } v[j] = t; }
}
int co_imap_ultimos(struct co_imap *s, struct co_msg *out, int max, uint32_t desde_uid)
{
    struct col_m c = { out, max, 0, desde_uid };
    char o[400];
    if (desde_uid) { co_cpy(o, "UID FETCH ", sizeof o); co_num(o, sizeof o, desde_uid + 1); co_cat(o, ":* ", sizeof o); }
    else {
        if (!s->exists) return 0;
        uint32_t a = s->exists > (uint32_t)max ? s->exists - (uint32_t)max + 1 : 1;
        co_cpy(o, "FETCH ", sizeof o); co_num(o, sizeof o, a); co_cat(o, ":", sizeof o); co_num(o, sizeof o, s->exists); co_cat(o, " ", sizeof o);
    }
    co_cat(o, ITEMS_LISTA, sizeof o);
    if (orden(s, o, cb_fetch_m, &c, CO_ESPERA_MS * 2)) return -1;
    ordena_uid(out, c.n);
    return c.n;
}
int co_imap_estos(struct co_imap *s, const uint32_t *uids, int n, struct co_msg *out)
{
    if (n <= 0) return 0;
    uint32_t cap = (uint32_t)n * 12 + 400; char *o = (char *)co_alloc(cap);
    if (!o) return -1;
    co_cpy(o, "UID FETCH ", cap);
    for (int i = 0; i < n; i++) { if (i) co_cat(o, ",", cap); co_num(o, cap, uids[i]); }
    co_cat(o, " ", cap); co_cat(o, ITEMS_LISTA, cap);
    struct col_m c = { out, n, 0, 0 };
    int r = orden(s, o, cb_fetch_m, &c, CO_ESPERA_MS * 2);
    co_free(o);
    if (r) return -1;
    ordena_uid(out, c.n);
    return c.n;
}
struct col_f { uint32_t *uids, *flags; int max, n; uint32_t desde; };
static void cb_flags(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct col_f *c = (struct col_f *)u; struct co_fetch f;
    if (!co_imap_fetch(r, n, &f) || !f.uid || f.uid < c->desde || c->n >= c->max) return;
    c->uids[c->n] = f.uid; c->flags[c->n] = f.flags; c->n++;
}
int co_imap_banderas_todas(struct co_imap *s, uint32_t desde_uid, uint32_t *uids, uint32_t *flags, int max)
{
    char o[80] = "UID FETCH "; co_num(o, sizeof o, desde_uid ? desde_uid : 1); co_cat(o, ":* (UID FLAGS)", sizeof o);
    struct col_f c = { uids, flags, max, 0, desde_uid };
    if (orden(s, o, cb_flags, &c, CO_ESPERA_MS * 2)) return -1;
    return c.n;
}
struct col_s { uint32_t *uids; int max, n; };
static void cb_search(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct col_s *c = (struct col_s *)u;
    if (c->n < c->max) c->n += (int)co_imap_search(r, n, c->uids + c->n, (uint32_t)(c->max - c->n));
}
int co_imap_busca(struct co_imap *s, const char *criterios, uint32_t *uids, int max)
{
    uint32_t cap = co_len(criterios) + 32; char *o = (char *)co_alloc(cap);
    if (!o) return -1;
    co_cpy(o, "UID SEARCH ", cap); co_cat(o, criterios, cap);
    struct col_s c = { uids, max, 0 };
    int r = orden(s, o, cb_search, &c, CO_ESPERA_MS * 2);
    co_free(o);
    return r ? -1 : c.n;
}
struct col_1 { uint32_t uid; char *out; uint32_t cap, n, tam; int visto; };
static void cb_uno(struct co_imap *s, const char *r, uint32_t n, void *u)
{
    (void)s; struct col_1 *c = (struct col_1 *)u; struct co_fetch f;
    if (!co_imap_fetch(r, n, &f) || (f.uid && f.uid != c->uid)) return;
    if (f.tam) c->tam = f.tam;
    if (f.cuerpo) { uint32_t k = f.cuerpo_n < c->cap - 1 ? f.cuerpo_n : c->cap - 1; mcpy(c->out, f.cuerpo, k); c->out[k] = 0; c->n = k; c->visto = 1; }
}
uint32_t co_imap_mensaje(struct co_imap *s, uint32_t uid, char *out, uint32_t cap, int *cortado)
{
    if (cap < 2) return 0;
    char o[120] = "UID FETCH "; co_num(o, sizeof o, uid); co_cat(o, " (UID RFC822.SIZE BODY.PEEK[]<0.", sizeof o); co_num(o, sizeof o, cap - 1); co_cat(o, ">)", sizeof o);
    struct col_1 c = { uid, out, cap, 0, 0, 0 };
    if (orden(s, o, cb_uno, &c, CO_ESPERA_MS * 3)) return 0;
    if (!c.visto) { pon_err(s->err, sizeof s->err, "El mensaje ya no est\303\241 en el servidor", 0); return 0; }
    if (cortado) *cortado = c.tam > c.n;
    return c.n;
}
static const char *banderas_txt(uint32_t f, char *o, uint32_t cap)
{
    o[0] = 0;
    if (f & CO_F_VISTO) co_cat(o, " \\Seen", cap);
    if (f & CO_F_DESTACADO) co_cat(o, " \\Flagged", cap);
    if (f & CO_F_RESPONDIDO) co_cat(o, " \\Answered", cap);
    if (f & CO_F_BORRADO) co_cat(o, " \\Deleted", cap);
    if (f & CO_F_BORRADOR) co_cat(o, " \\Draft", cap);
    return o[0] ? o + 1 : o;
}
int co_imap_banderas(struct co_imap *s, uint32_t uid, uint32_t flags, int poner)
{
    char b[80], o[160] = "UID STORE "; co_num(o, sizeof o, uid); co_cat(o, poner ? " +FLAGS.SILENT (" : " -FLAGS.SILENT (", sizeof o);
    co_cat(o, banderas_txt(flags, b, sizeof b), sizeof o); co_cat(o, ")", sizeof o);
    return orden(s, o, 0, 0, CO_ESPERA_MS);
}
static int expunge(struct co_imap *s, uint32_t uid)
{
    if (s->uidplus) { char o[60] = "UID EXPUNGE "; co_num(o, sizeof o, uid); return orden(s, o, 0, 0, CO_ESPERA_MS); }
    return orden(s, "EXPUNGE", 0, 0, CO_ESPERA_MS);
}
int co_imap_mueve(struct co_imap *s, uint32_t uid, const char *destino)
{
    char o[400]; co_cpy(o, s->move ? "UID MOVE " : "UID COPY ", sizeof o); co_num(o, sizeof o, uid); co_cat(o, " ", sizeof o); quote(o, sizeof o, destino);
    if (orden(s, o, 0, 0, CO_ESPERA_MS)) return -1;
    if (s->move) return 0;
    if (co_imap_banderas(s, uid, CO_F_BORRADO, 1)) return -1;
    return expunge(s, uid);
}
int co_imap_borra(struct co_imap *s, uint32_t uid)
{
    if (co_imap_banderas(s, uid, CO_F_BORRADO, 1)) return -1;
    return expunge(s, uid);
}
int co_imap_anade(struct co_imap *s, const char *carpeta, const char *msg, uint32_t n, uint32_t flags)
{
    char b[80], o[400] = "APPEND "; quote(o, sizeof o, carpeta);
    co_cat(o, " (", sizeof o); co_cat(o, banderas_txt(flags, b, sizeof b), sizeof o); co_cat(o, ") ", sizeof o);
    return orden_lit(s, o, msg, n, "", 0, 0);
}

/* ================================================================ SMTP */
struct smtp { struct co_tr *t; char b[8192]; uint32_t n; char *err; uint32_t ecap; };
static int sm_lee(struct smtp *m, uint32_t espera)                  /* codigo de la respuesta (o -1); el texto queda en b */
{
    uint64_t t0 = m->t->ms();
    for (;;) {
        int code; uint32_t usado;
        if (m->n && co_smtp_resp(m->b, m->n, &code, &usado)) {
            if (code == 0) { pon_err(m->err, m->ecap, "El servidor no parece SMTP", 0); return -1; }
            m->b[usado < sizeof m->b ? usado : sizeof m->b - 1] = 0;
            m->n = 0;                                                       /* SMTP no encadena respuestas sin preguntar (sin PIPELINING) */
            return code;
        }
        if (m->n + 1 >= sizeof m->b) m->n = 0;
        if (m->t->cancela && *m->t->cancela) { pon_err(m->err, m->ecap, "Cancelado", 0); return -1; }
        int r = m->t->recibe(m->t->ctx, m->b + m->n, (uint32_t)sizeof m->b - 1 - m->n, 250);
        if (r < 0) { pon_err(m->err, m->ecap, "El servidor de env\303\255o ha cerrado la conexi\303\263n", 0); return -1; }
        if (r > 0) { m->n += (uint32_t)r; t0 = m->t->ms(); continue; }
        if (m->t->ms() - t0 > espera) { pon_err(m->err, m->ecap, "El servidor de env\303\255o no responde", 0); return -1; }
    }
}
static void texto_srv(struct smtp *m, char *o, uint32_t cap)          /* la ultima linea de la respuesta, sin el codigo */
{
    uint32_t n = co_len(m->b), e = n; while (e && (m->b[e - 1] == '\r' || m->b[e - 1] == '\n')) e--;
    uint32_t a = e; while (a && m->b[a - 1] != '\n') a--;
    a += 4 <= e - a ? 4 : 0;
    co_cpyn(o, m->b + a, e > a ? e - a : 0, cap);
}
static int sm_orden(struct smtp *m, const char *a, const char *b, uint32_t espera)
{
    const char *p[2] = { a, b };
    for (int i = 0; i < 2; i++) if (p[i] && p[i][0] && m->t->envia(m->t->ctx, p[i], co_len(p[i])) != 0) { pon_err(m->err, m->ecap, "No se pudo hablar con el servidor de env\303\255o", 0); return -1; }
    if (m->t->envia(m->t->ctx, "\r\n", 2) != 0) { pon_err(m->err, m->ecap, "No se pudo hablar con el servidor de env\303\255o", 0); return -1; }
    return sm_lee(m, espera);
}
static void falla(struct smtp *m, const char *que)
{
    char s[200]; texto_srv(m, s, sizeof s);
    co_cpy(m->err, que, m->ecap);
    if (s[0]) { co_cat(m->err, " (el servidor dice: ", m->ecap); co_cat(m->err, s, m->ecap); co_cat(m->err, ")", m->ecap); }
}
/* conecta, saluda, STARTTLS y AUTH. 0 dentro */
static int sm_entra(struct smtp *m, const struct co_cuenta *c)
{
    if (!c->smtp[0] || !c->smtp_p) { pon_err(m->err, m->ecap, "Falta el servidor SMTP en la cuenta", 0); return -1; }
    if (m->t->abre(m->t->ctx, c->smtp, c->smtp_p, c->smtp_tls != CO_TLS_STARTTLS, m->err, m->ecap) != 0) return -1;   /* sin cifrar, nunca */
    int r = sm_lee(m, CO_ESPERA_MS);
    if (r != 220) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta conexiones"); return -1; }
    char ehlo[80] = "EHLO soma.local";
    r = sm_orden(m, ehlo, 0, CO_ESPERA_MS);
    if (r != 250) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta EHLO"); return -1; }
    uint32_t caps = co_smtp_caps(m->b, co_len(m->b));
    if (c->smtp_tls == CO_TLS_STARTTLS) {
        if (!(caps & CO_S_STARTTLS)) { pon_err(m->err, m->ecap, "El servidor de env\303\255o no ofrece STARTTLS: SOMA no manda la contrase\303\261a sin cifrar", 0); return -1; }
        r = sm_orden(m, "STARTTLS", 0, CO_ESPERA_MS);
        if (r != 220) { if (r > 0) falla(m, "El servidor de env\303\255o no deja cifrar"); return -1; }
        if (m->t->starttls(m->t->ctx, c->smtp, m->err, m->ecap)) return -1;
        r = sm_orden(m, ehlo, 0, CO_ESPERA_MS);
        if (r != 250) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta EHLO"); return -1; }
        caps = co_smtp_caps(m->b, co_len(m->b));
    }
    const char *u = c->usuario[0] ? c->usuario : c->email;
    if (caps & CO_S_PLAIN) {
        char b[700]; co_smtp_plain(u, c->clave, b, sizeof b);
        r = sm_orden(m, "AUTH PLAIN ", b, CO_ESPERA_MS);
    } else if (caps & CO_S_LOGIN) {
        char b[400];
        r = sm_orden(m, "AUTH LOGIN", 0, CO_ESPERA_MS);
        if (r == 334) { co_b64_enc((const uint8_t *)u, co_len(u), b, sizeof b); r = sm_orden(m, b, 0, CO_ESPERA_MS); }
        if (r == 334) { co_b64_enc((const uint8_t *)c->clave, co_len(c->clave), b, sizeof b); r = sm_orden(m, b, 0, CO_ESPERA_MS); }
    } else { pon_err(m->err, m->ecap, "El servidor de env\303\255o no admite entrar con contrase\303\261a (solo OAuth)", 0); return -1; }
    if (r != 235) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta el usuario o la contrase\303\261a"); return -1; }
    return 0;
}
int co_smtp_envia(struct co_tr *t, const struct co_cuenta *c, const struct co_dir *rcpt, int nr, const char *msg, uint32_t n, char *err, uint32_t ecap)
{
    struct smtp *m = (struct smtp *)co_alloc(sizeof *m);
    if (ecap) err[0] = 0;
    if (!m) { pon_err(err, ecap, "Sin memoria para enviar", 0); return -1; }
    m->t = t; m->n = 0; m->err = err; m->ecap = ecap;
    int ok = -1;
    char *d = 0;
    if (nr <= 0) { pon_err(err, ecap, "No hay destinatarios", 0); goto fin; }
    if (sm_entra(m, c)) goto fin;
    {
        char l[200] = "MAIL FROM:<"; co_cat(l, c->email, sizeof l); co_cat(l, ">", sizeof l);
        int r = sm_orden(m, l, 0, CO_ESPERA_MS);
        if (r != 250) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta el remitente"); goto fin; }
        for (int i = 0; i < nr; i++) {
            co_cpy(l, "RCPT TO:<", sizeof l); co_cat(l, rcpt[i].email, sizeof l); co_cat(l, ">", sizeof l);
            r = sm_orden(m, l, 0, CO_ESPERA_MS);
            if (r != 250 && r != 251) { if (r > 0) { char q[200] = "El servidor no acepta el destinatario "; co_cat(q, rcpt[i].email, sizeof q); falla(m, q); } goto fin; }
        }
        r = sm_orden(m, "DATA", 0, CO_ESPERA_MS);
        if (r != 354) { if (r > 0) falla(m, "El servidor de env\303\255o no acepta el mensaje"); goto fin; }
        uint32_t cap = n + n / 30 + 16; d = (char *)co_alloc(cap);
        if (!d) { pon_err(err, ecap, "Sin memoria para enviar", 0); goto fin; }
        uint32_t dn = co_puntea(msg, n, d, cap);
        if (!dn || t->envia(t->ctx, d, dn) != 0) { pon_err(err, ecap, "No se pudo mandar el mensaje al servidor", 0); goto fin; }
        r = sm_lee(m, CO_ESPERA_MS * 2);
        if (r != 250) { if (r > 0) falla(m, r == 552 || r == 523 ? "El mensaje es demasiado grande para el servidor" : r == 554 || r == 550 ? "El servidor ha rechazado el mensaje" : "El servidor no ha aceptado el mensaje"); goto fin; }
        ok = 0;
        (void)sm_orden(m, "QUIT", 0, 3000);
    }
fin:
    if (d) co_free(d);
    t->cierra(t->ctx);
    co_free(m);
    return ok;
}
int co_smtp_prueba(struct co_tr *t, const struct co_cuenta *c, char *err, uint32_t ecap)
{
    struct smtp *m = (struct smtp *)co_alloc(sizeof *m);
    if (ecap) err[0] = 0;
    if (!m) { pon_err(err, ecap, "Sin memoria", 0); return -1; }
    m->t = t; m->n = 0; m->err = err; m->ecap = ecap;
    int ok = sm_entra(m, c);
    if (!ok) (void)sm_orden(m, "QUIT", 0, 3000);
    t->cierra(t->ctx);
    co_free(m);
    return ok;
}
