/* SOMA_176A: cliente TLS 1.3. Ver tls13.h.
 *
 * Seguridad: desde el parche 194 el certificado del servidor SE VALIDA (x509.c): la cadena debe llegar a una autoridad de confianza,
 * el certificado debe ser del sitio pedido y estar en fecha, y el servidor debe demostrar que tiene su clave (CertificateVerify).
 * Si algo falla, la conexion no se establece (t->err = TLS_E_CERT + motivo). tls13_verify = 0 vuelve al comportamiento anterior
 * (cifrado sin autenticar), solo para diagnostico. No hay revocacion (CRL/OCSP). */
#include "x509.h"
#include "tls13.h"
#include "tlscrypto.h"

static void mcp(void *d, const void *s, size_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for (size_t i = 0; i < n; i++) a[i] = b[i]; }
const char *tls13_err_name(int e)
{
    switch (e) {
    case TLS_E_IO: return "error de red durante TLS"; case TLS_E_ALERT: return "el servidor rechazo la conexion TLS (alerta)";
    case TLS_E_VERSION: return "el servidor no admite TLS 1.3"; case TLS_E_HELLO: return "ServerHello no valido";
    case TLS_E_DECRYPT: return "registro TLS corrupto (etiqueta GCM)"; case TLS_E_FINISHED: return "el Finished del servidor no cuadra";
    case TLS_E_PROTO: return "mensaje TLS inesperado"; case TLS_E_TIMEOUT: return "tiempo agotado en TLS";
    case TLS_E_CERTREQ: return "el servidor pide certificado de cliente"; case TLS_E_SIZE: return "registro TLS demasiado grande";
    default: return e >= TLS_E_CERT ? x509_err_name(e - TLS_E_CERT) : "error TLS";
    }
}

static void traffic_keys(const uint8_t secret[32], uint8_t key[16], uint8_t iv[12])
{
    hkdf_expand_label(secret, "key", 0, 0, key, 16);
    hkdf_expand_label(secret, "iv", 0, 0, iv, 12);
}
static void nonce_of(const uint8_t iv[12], uint64_t seq, uint8_t n[12])
{
    mcp(n, iv, 12);
    for (int i = 0; i < 8; i++) n[11 - i] ^= (uint8_t)(seq >> (8 * i));
}

/* ---- lectura de un registro completo en t->rec; 0 = bien, <0 error TLS_E_*, 1 = nada (timeout sin datos) */
static int read_record(struct tls13 *t, uint32_t timeout_ms)
{
    uint64_t end = t->io.now_ms() + timeout_ms;
    for (;;) {
        uint32_t need = 5;
        if (t->rec_n >= 5) { uint32_t l = (uint32_t)(t->rec[3] << 8 | t->rec[4]); if (l > TLS_MAX_REC) return -TLS_E_SIZE; need = 5 + l; }
        if (t->rec_n >= need && t->rec_n >= 5) return 0;
        uint64_t now = t->io.now_ms();
        uint32_t w = now >= end ? 0 : (uint32_t)(end - now); if (w > 100) w = 100;
        int n = t->io.recv(t->io.ctx, t->rec + t->rec_n, need - t->rec_n, w);
        if (n > 0) { t->rec_n += (uint32_t)n; continue; }
        if (n == TLS_IO_EOF) return t->rec_n ? -TLS_E_IO : -100;
        if (n == TLS_IO_ERR) return -TLS_E_IO;
        now = t->io.now_ms();
        if (now >= end) {
            if (!t->rec_n) return 1;
            if (now >= end + 15000) return -TLS_E_TIMEOUT;                          /* registro a medias: se espera un poco mas */
        }
        if (t->io.yield) t->io.yield();
    }
}
static void rec_done(struct tls13 *t) { uint32_t l = 5 + (uint32_t)(t->rec[3] << 8 | t->rec[4]); if (t->rec_n > l) { for (uint32_t i = l; i < t->rec_n; i++) t->rec[i - l] = t->rec[i]; t->rec_n -= l; } else t->rec_n = 0; }

/* descifra el registro actual (tipo 23) con la clave dada; devuelve el tipo interno (>=0) y deja el texto en t->plain; <0 error */
static int open_record(struct tls13 *t, const uint8_t key[16], const uint8_t iv[12], uint64_t *seq, uint32_t *plen)
{
    uint32_t cl = (uint32_t)(t->rec[3] << 8 | t->rec[4]);
    if (cl < 17) return -TLS_E_DECRYPT;
    uint8_t nonce[12]; nonce_of(iv, *seq, nonce);
    if (gcm128_open(key, nonce, t->rec, 5, t->rec + 5, cl - 16, t->rec + 5 + cl - 16, t->plain)) return -TLS_E_DECRYPT;
    (*seq)++;
    uint32_t n = cl - 16; while (n && t->plain[n - 1] == 0) n--;                       /* quitar relleno */
    if (!n) return -TLS_E_DECRYPT;
    int ty = t->plain[n - 1]; *plen = n - 1;
    return ty;
}
static int send_record(struct tls13 *t, int inner, const uint8_t *d, uint32_t n)       /* con claves de cliente actuales */
{
    if (n > 16384) return -TLS_E_SIZE;
    uint8_t *o = t->out; uint32_t cl = n + 1 + 16;
    o[0] = 23; o[1] = 3; o[2] = 3; o[3] = (uint8_t)(cl >> 8); o[4] = (uint8_t)cl;
    mcp(o + 5, d, n); o[5 + n] = (uint8_t)inner;
    uint8_t nonce[12]; nonce_of(t->civ, t->cseq, nonce);
    gcm128_seal(t->ck, nonce, o, 5, o + 5, n + 1, o + 5, o + 5 + n + 1);
    t->cseq++;
    return t->io.send(t->io.ctx, o, 5 + cl) ? -TLS_E_IO : 0;
}

/* ---- analizador de mensajes de handshake cifrados: solo conserva el Finished (32 bytes) y guarda las huellas */
struct hsp { struct sha256_ctx th; struct sha256_ctx before_fin; uint8_t hdr[4]; uint32_t hdr_n, body_left, ty; uint8_t cap[48]; uint32_t cap_n; int got_fin; int certreq;
             struct tls13 *t; struct sha256_ctx before_cv; int got_cert, got_cv, cert_over; };     /* SOMA_194: Certificate y CertificateVerify se guardan para validarlos */
int tls13_verify = 1;
uint64_t (*tls13_unix_time)(void);
static void hs_feed(struct hsp *h, const uint8_t *p, uint32_t n)
{
    while (n) {
        if (h->hdr_n < 4) {
            uint32_t k = 4 - h->hdr_n; if (k > n) k = n;
            mcp(h->hdr + h->hdr_n, p, k); h->hdr_n += k; p += k; n -= k;
            if (h->hdr_n == 4) {
                h->ty = h->hdr[0]; h->body_left = (uint32_t)h->hdr[1] << 16 | (uint32_t)h->hdr[2] << 8 | h->hdr[3]; h->cap_n = 0;
                if (h->ty == 20) h->before_fin = h->th;                          /* huella antes del Finished */
                if (h->ty == 13) h->certreq = 1;
                if (h->ty == 11) { if (h->got_cert) h->cert_over = 1; h->got_cert = 1; if (h->t) h->t->certmsg_n = 0; }   /* SOMA_194 */
                if (h->ty == 15) { h->before_cv = h->th; if (h->got_cv) h->cert_over = 1; h->got_cv = 1; if (h->t) h->t->cvmsg_n = 0; }
                sha256_update(&h->th, h->hdr, 4);
                if (!h->body_left) { h->hdr_n = 0; }
            }
            continue;
        }
        uint32_t k = h->body_left < n ? h->body_left : n;
        sha256_update(&h->th, p, k);
        if (h->ty == 20 && h->cap_n + k <= sizeof h->cap) { mcp(h->cap + h->cap_n, p, k); h->cap_n += k; }
        if (h->t && h->ty == 11) { if (h->t->certmsg_n + k <= TLS_CERT_MAX) { mcp(h->t->certmsg + h->t->certmsg_n, p, k); h->t->certmsg_n += k; } else h->cert_over = 1; }
        if (h->t && h->ty == 15) { if (h->t->cvmsg_n + k <= TLS_CV_MAX) { mcp(h->t->cvmsg + h->t->cvmsg_n, p, k); h->t->cvmsg_n += k; } else h->cert_over = 1; }
        p += k; n -= k; h->body_left -= k;
        if (!h->body_left) { if (h->ty == 20) h->got_fin = 1; h->hdr_n = 0; }
    }
}

static void derive_secret(const uint8_t secret[32], const char *label, const uint8_t hash[32], uint8_t out[32]) { hkdf_expand_label(secret, label, hash, 32, out, 32); }

int tls13_connect(struct tls13 *t, const struct tls_io *io, const char *sni, uint32_t timeout_ms)
{
    t->io = *io; t->rec_n = 0; t->plain_n = t->plain_off = 0; t->state = 0; t->err = 0; t->alert = 0; t->cert_verified = 0; t->cseq = t->sseq = 0;
    uint64_t t_end = t->io.now_ms() + timeout_ms;
    uint8_t priv[32], pub[32], crandom[32], sid[32];
    tls_random(priv, 32); tls_random(crandom, 32); tls_random(sid, 32);
    x25519_base(pub, priv);
    /* ---- ClientHello */
    uint8_t ch[600]; uint32_t n = 0;
    ch[n++] = 1; ch[n++] = 0; ch[n++] = 0; ch[n++] = 0;                            /* tipo y longitud (se rellena) */
    ch[n++] = 3; ch[n++] = 3; mcp(ch + n, crandom, 32); n += 32;
    ch[n++] = 32; mcp(ch + n, sid, 32); n += 32;
    ch[n++] = 0; ch[n++] = 2; ch[n++] = 0x13; ch[n++] = 0x01;                      /* TLS_AES_128_GCM_SHA256 */
    ch[n++] = 1; ch[n++] = 0;                                                      /* sin compresion */
    uint32_t ext_len_at = n; n += 2;
    uint32_t sl = 0; if (sni) while (sni[sl] && sl < 200) sl++;
    if (sl) {
        ch[n++] = 0; ch[n++] = 0; ch[n++] = (uint8_t)((sl + 5) >> 8); ch[n++] = (uint8_t)(sl + 5);
        ch[n++] = (uint8_t)((sl + 3) >> 8); ch[n++] = (uint8_t)(sl + 3); ch[n++] = 0; ch[n++] = (uint8_t)(sl >> 8); ch[n++] = (uint8_t)sl;
        mcp(ch + n, sni, sl); n += sl;
    }
    static const uint8_t ext_a[] = {
        0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d,                              /* supported_groups: x25519 */
        /* signature_algorithms. SOMA_194: solo los que x509.c verifica (fuera P-521 y SHA-1): PSS y ECDSA P-256/P-384 para el saludo, PKCS#1 para los certificados */
        0x00, 0x0d, 0x00, 0x12, 0x00, 0x10, 0x08, 0x04, 0x08, 0x05, 0x08, 0x06, 0x04, 0x03, 0x05, 0x03, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01,
        0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04,                                    /* supported_versions: TLS 1.3 */
        0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20 };               /* key_share: x25519 + 32 bytes */
    mcp(ch + n, ext_a, sizeof ext_a); n += sizeof ext_a; mcp(ch + n, pub, 32); n += 32;
    uint32_t el = n - ext_len_at - 2; ch[ext_len_at] = (uint8_t)(el >> 8); ch[ext_len_at + 1] = (uint8_t)el;
    uint32_t bl = n - 4; ch[1] = (uint8_t)(bl >> 16); ch[2] = (uint8_t)(bl >> 8); ch[3] = (uint8_t)bl;
    uint8_t rh[5] = { 22, 3, 1, (uint8_t)(n >> 8), (uint8_t)n };
    uint8_t *o = t->out; mcp(o, rh, 5); mcp(o + 5, ch, n);
    struct hsp hp; sha256_init(&hp.th); hp.hdr_n = 0; hp.body_left = 0; hp.got_fin = 0; hp.certreq = 0; hp.cap_n = 0;
    hp.t = t; hp.got_cert = hp.got_cv = hp.cert_over = 0; t->certmsg_n = t->cvmsg_n = 0;
    hs_feed(&hp, ch, n);
    if (t->io.send(t->io.ctx, o, 5 + n)) { t->err = TLS_E_IO; return -1; }
    /* ---- ServerHello */
    uint8_t shared[32], hs[32], c_hs[32], s_hs[32], th[32], emp[32], der[32], zero[32] = { 0 }, early[32];
    int r;
    for (;;) {
        uint64_t now = t->io.now_ms(); if (now >= t_end) { t->err = TLS_E_TIMEOUT; return -1; }
        r = read_record(t, (uint32_t)(t_end - now));
        if (r == 1) continue;
        if (r < 0) { t->err = r == -100 ? TLS_E_IO : -r; return -1; }
        if (t->rec[0] == 20) { rec_done(t); continue; }
        break;
    }
    if (t->rec[0] == 21) { t->alert = t->rec[6]; t->err = TLS_E_ALERT; return -1; }
    if (t->rec[0] != 22 || t->rec_n < 5 + 4 || t->rec[5] != 2) { t->err = TLS_E_HELLO; return -1; }
    {
        const uint8_t *s = t->rec + 5; uint32_t sl2 = (uint32_t)(t->rec[3] << 8 | t->rec[4]);
        uint32_t mlen = (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
        if (mlen + 4 > sl2 || mlen < 38) { t->err = TLS_E_HELLO; return -1; }
        static const uint8_t hrr[32] = { 0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C };
        int ishrr = 1; for (int i = 0; i < 32; i++) if (s[6 + i] != hrr[i]) ishrr = 0;
        if (ishrr) { t->err = TLS_E_HELLO; return -1; }
        uint32_t p = 4 + 2 + 32; uint32_t sidl = s[p]; p += 1 + sidl;
        if (p + 3 > mlen + 4) { t->err = TLS_E_HELLO; return -1; }
        if (!(s[p] == 0x13 && s[p + 1] == 0x01) || s[p + 2] != 0) { t->err = TLS_E_HELLO; return -1; }
        p += 3;
        if (p + 2 > mlen + 4) { t->err = TLS_E_VERSION; return -1; }
        uint32_t extl = (uint32_t)(s[p] << 8 | s[p + 1]); p += 2;
        int ver13 = 0, have_ks = 0; uint8_t spub[32];
        if (p + extl > mlen + 4) { t->err = TLS_E_HELLO; return -1; }
        for (uint32_t e = p; e + 4 <= p + extl; ) {
            uint32_t ty = (uint32_t)(s[e] << 8 | s[e + 1]), l = (uint32_t)(s[e + 2] << 8 | s[e + 3]);
            if (e + 4 + l > p + extl) { t->err = TLS_E_HELLO; return -1; }
            if (ty == 43 && l == 2 && s[e + 4] == 3 && s[e + 5] == 4) ver13 = 1;
            else if (ty == 51 && l == 36 && s[e + 4] == 0 && s[e + 5] == 0x1d && s[e + 6] == 0 && s[e + 7] == 32) { mcp(spub, s + e + 8, 32); have_ks = 1; }
            e += 4 + l;
        }
        if (!ver13) { t->err = TLS_E_VERSION; return -1; }
        if (!have_ks) { t->err = TLS_E_HELLO; return -1; }
        hs_feed(&hp, s, mlen + 4);
        x25519(shared, priv, spub);
        for (int i = 0; i < 32; i++) priv[i] = 0;
    }
    rec_done(t);
    /* ---- claves de handshake */
    sha256("", 0, emp);
    hkdf_extract(0, 0, zero, 32, early);
    derive_secret(early, "derived", emp, der);
    hkdf_extract(der, 32, shared, 32, hs);
    { struct sha256_ctx c = hp.th; sha256_final(&c, th); }
    derive_secret(hs, "c hs traffic", th, c_hs);
    derive_secret(hs, "s hs traffic", th, s_hs);
    uint8_t shk[16], shiv[12]; traffic_keys(s_hs, shk, shiv);
    uint64_t shseq = 0;
    /* ---- mensajes cifrados del servidor hasta el Finished */
    while (!hp.got_fin) {
        uint64_t now = t->io.now_ms(); if (now >= t_end) { t->err = TLS_E_TIMEOUT; return -1; }
        r = read_record(t, (uint32_t)(t_end - now));
        if (r == 1) continue;
        if (r < 0) { t->err = r == -100 ? TLS_E_IO : -r; return -1; }
        if (t->rec[0] == 20) { rec_done(t); continue; }                              /* ChangeCipherSpec de compatibilidad */
        if (t->rec[0] == 21) { t->alert = t->rec[6]; t->err = TLS_E_ALERT; return -1; }
        if (t->rec[0] != 23) { t->err = TLS_E_PROTO; return -1; }
        uint32_t pl; int ty = open_record(t, shk, shiv, &shseq, &pl);
        if (ty < 0) { t->err = -ty; return -1; }
        if (ty == 21) { t->alert = pl >= 2 ? t->plain[1] : 0; t->err = TLS_E_ALERT; return -1; }
        if (ty != 22) { t->err = TLS_E_PROTO; return -1; }
        hs_feed(&hp, t->plain, pl);
        rec_done(t);
    }
    if (hp.certreq) { t->err = TLS_E_CERTREQ; return -1; }
    /* ---- Finished del servidor */
    {
        uint8_t fk[32], vd[32], bf[32]; struct sha256_ctx c = hp.before_fin; sha256_final(&c, bf);
        hkdf_expand_label(s_hs, "finished", 0, 0, fk, 32);
        hmac_sha256(fk, 32, bf, 32, vd);
        uint8_t d = 0; if (hp.cap_n != 32) d = 1; else for (int i = 0; i < 32; i++) d |= (uint8_t)(vd[i] ^ hp.cap[i]);
        if (d) { t->err = TLS_E_FINISHED; return -1; }
    }
    /* ---- SOMA_194: con quien hablamos. El Finished ya demuestra que nadie altero el saludo; falta comprobar que la clave que lo firmo
     * (CertificateVerify) es la de un certificado valido para `sni` que llega a una autoridad de confianza. Sin esto, cualquiera en la red
     * podia hacerse pasar por el sitio. */
    int verified = 0;
    if (tls13_verify) {
        int xr; struct x509_key leaf;
        if (!hp.got_cert || !hp.got_cv) xr = X509_E_FORMAT;
        else if (hp.cert_over) xr = X509_E_TOOLONG;
        else {
            xr = x509_verify_tls_chain(t->certmsg, t->certmsg_n, sni, tls13_unix_time ? tls13_unix_time() : 0, &leaf);
            if (xr == X509_OK) { uint8_t thc[32]; struct sha256_ctx c = hp.before_cv; sha256_final(&c, thc); xr = x509_verify_tls_cv(&leaf, t->cvmsg, t->cvmsg_n, thc); }
        }
        if (xr != X509_OK) { t->err = TLS_E_CERT + xr; return -1; }
        verified = 1;
    }
    uint8_t th_sf[32]; { struct sha256_ctx c = hp.th; sha256_final(&c, th_sf); }
    /* ---- claves de aplicacion y Finished del cliente */
    uint8_t der2[32], master[32];
    derive_secret(hs, "derived", emp, der2);
    hkdf_extract(der2, 32, zero, 32, master);
    derive_secret(master, "c ap traffic", th_sf, t->cts);
    derive_secret(master, "s ap traffic", th_sf, t->sts);
    {
        uint8_t ccs[6] = { 20, 3, 3, 0, 1, 1 };
        if (t->io.send(t->io.ctx, ccs, 6)) { t->err = TLS_E_IO; return -1; }
        traffic_keys(c_hs, t->ck, t->civ); t->cseq = 0;
        uint8_t fk[32], vd[32], fin[36];
        hkdf_expand_label(c_hs, "finished", 0, 0, fk, 32);
        hmac_sha256(fk, 32, th_sf, 32, vd);
        fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 32; mcp(fin + 4, vd, 32);
        if (send_record(t, 22, fin, 36)) { t->err = TLS_E_IO; return -1; }
    }
    traffic_keys(t->cts, t->ck, t->civ); t->cseq = 0;
    traffic_keys(t->sts, t->sk, t->siv); t->sseq = 0;
    t->state = 1; t->cert_verified = verified;
    return 0;
}

int tls13_send(struct tls13 *t, const void *d, uint32_t n)
{
    if (t->state != 1) return -1;
    const uint8_t *p = (const uint8_t *)d;
    while (n) { uint32_t k = n > 16000 ? 16000 : n; if (send_record(t, 23, p, k)) { t->state = -1; t->err = TLS_E_IO; return -1; } p += k; n -= k; }
    return 0;
}

static void update_keys(struct tls13 *t, int ours)
{
    if (ours) { hkdf_expand_label(t->cts, "traffic upd", 0, 0, t->cts, 32); traffic_keys(t->cts, t->ck, t->civ); t->cseq = 0; }
    else { hkdf_expand_label(t->sts, "traffic upd", 0, 0, t->sts, 32); traffic_keys(t->sts, t->sk, t->siv); t->sseq = 0; }
}

int tls13_recv(struct tls13 *t, void *buf, uint32_t max, uint32_t timeout_ms)
{
    if (t->plain_off < t->plain_n) goto give;
    if (t->state == 2) return TLS_IO_EOF;
    if (t->state != 1) return TLS_IO_ERR;
    {
        uint64_t end = t->io.now_ms() + timeout_ms;
        for (;;) {
            uint64_t now = t->io.now_ms(); uint32_t w = now >= end ? 0 : (uint32_t)(end - now);
            int r = read_record(t, w);
            if (r == 1) return 0;
            if (r == -100) { t->state = 2; return TLS_IO_EOF; }                           /* el TCP se cerro sin close_notify */
            if (r < 0) { t->state = -1; t->err = -r; return TLS_IO_ERR; }
            if (t->rec[0] == 20) { rec_done(t); continue; }
            if (t->rec[0] == 21) { t->alert = t->rec[6]; rec_done(t); t->state = t->alert == 0 ? 2 : -1; if (t->alert) t->err = TLS_E_ALERT; return t->alert == 0 ? TLS_IO_EOF : TLS_IO_ERR; }
            if (t->rec[0] != 23) { t->state = -1; t->err = TLS_E_PROTO; return TLS_IO_ERR; }
            uint32_t pl; int ty = open_record(t, t->sk, t->siv, &t->sseq, &pl);
            rec_done(t);
            if (ty < 0) { t->state = -1; t->err = -ty; return TLS_IO_ERR; }
            if (ty == 23) { if (!pl) continue; t->plain_n = pl; t->plain_off = 0; break; }
            if (ty == 21) { t->alert = pl >= 2 ? t->plain[1] : 0; t->state = (pl >= 2 && t->plain[1] == 0) ? 2 : -1; if (t->state == -1) t->err = TLS_E_ALERT; return t->state == 2 ? TLS_IO_EOF : TLS_IO_ERR; }
            if (ty == 22) {                                                              /* NewSessionTicket (se ignora) o KeyUpdate */
                for (uint32_t p = 0; p + 4 <= pl; ) {
                    uint32_t l = (uint32_t)t->plain[p + 1] << 16 | (uint32_t)t->plain[p + 2] << 8 | t->plain[p + 3];
                    if (t->plain[p] == 24 && l == 1 && p + 5 <= pl) {
                        int req = t->plain[p + 4]; update_keys(t, 0);
                        if (req == 1) { uint8_t ku[5] = { 24, 0, 0, 1, 0 }; if (send_record(t, 22, ku, 5)) { t->state = -1; t->err = TLS_E_IO; return TLS_IO_ERR; } update_keys(t, 1); }
                    }
                    p += 4 + l;
                }
                continue;
            }
            t->state = -1; t->err = TLS_E_PROTO; return TLS_IO_ERR;
        }
    }
give:
    {
        uint32_t k = t->plain_n - t->plain_off; if (k > max) k = max;
        mcp(buf, t->plain + t->plain_off, k); t->plain_off += k;
        if (t->plain_off >= t->plain_n) t->plain_n = t->plain_off = 0;
        return (int)k;
    }
}

void tls13_close(struct tls13 *t)
{
    if (t->state == 1) { uint8_t al[2] = { 1, 0 }; send_record(t, 21, al, 2); }
    t->state = 0;
}
