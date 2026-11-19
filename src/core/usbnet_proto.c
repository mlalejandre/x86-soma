/* SOMA_171_T480S: ver usbnet_proto.h. Solo enteros, sin estado, robusto ante datos malformados (se prueba con datos al azar). */
#include "usbnet_proto.h"

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void zero(uint8_t *p, uint32_t n) { for (uint32_t i = 0; i < n; i++) p[i] = 0; }
static void copy(uint8_t *d, const uint8_t *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = s[i]; }

const char *usbnet_kind_str(int k)
{
    return k == USBNET_RNDIS ? "RNDIS" : k == USBNET_ECM ? "CDC-ECM" : k == USBNET_NCM ? "CDC-NCM" : "ninguna";
}

/* Se recorre el descriptor. Una interfaz de control (RNDIS: E0/01/03 o 02/02/FF; ECM: 02/06; NCM: 02/0D) va seguida de su
 * interfaz de datos (clase 0A); el descriptor Union (24 06) dice cual es. Se toma el ajuste alternativo con dos bulk. */
int usbnet_find(const uint8_t *d, uint32_t len, struct usbnet_fn *o)
{
    zero((uint8_t *)o, sizeof *o);
    if (len < 4 || d[1] != 2) return 0;
    int kind = 0, have_union = 0; uint8_t comm = 0xFF, data = 0xFF, mac = 0; uint16_t seg = 0;
    int cur_if = -1, cur_alt = 0, cur_cls = -1;
    uint8_t ein = 0, eout = 0; uint16_t min_ = 0, mout = 0;
    uint32_t i = 0;
    while (i + 2 <= len) {
        uint32_t l = d[i];
        if (l < 2 || i + l > len) break;
        uint8_t t = d[i + 1];
        if (t == 4 && l >= 9) {                                          /* interfaz */
            if (kind && cur_if == data && ein && eout) break;            /* ya se tiene la de datos con sus extremos */
            cur_if = d[i + 2]; cur_alt = d[i + 3]; cur_cls = d[i + 5];
            uint8_t sc = d[i + 6], pr = d[i + 7];
            ein = eout = 0;
            if (!kind) {
                int k = 0;
                if ((cur_cls == 0xE0 && sc == 0x01 && pr == 0x03) || (cur_cls == 0x02 && sc == 0x02 && pr == 0xFF) ||
                    (cur_cls == 0xEF && sc == 0x04 && pr == 0x01)) k = USBNET_RNDIS;
                else if (cur_cls == 0x02 && sc == 0x06) k = USBNET_ECM;
                else if (cur_cls == 0x02 && sc == 0x0D) k = USBNET_NCM;
                if (k) { kind = k; comm = (uint8_t)cur_if; data = (uint8_t)(cur_if + 1); }
            }
        } else if (t == 0x24 && l >= 3 && kind && cur_if == comm) {      /* descriptores funcionales de la interfaz de control */
            uint8_t sub = d[i + 2];
            if (sub == 0x06 && l >= 5) { data = d[i + 4]; have_union = 1; }
            else if (sub == 0x0F && l >= 13) { mac = d[i + 3]; seg = le16(d + i + 8); }
        } else if (t == 5 && l >= 7 && kind && cur_if == data && cur_cls == 0x0A) {
            if ((d[i + 3] & 3) == 2) {
                if (d[i + 2] & 0x80) { ein = d[i + 2]; min_ = (uint16_t)(le16(d + i + 4) & 0x7FF); }
                else { eout = d[i + 2]; mout = (uint16_t)(le16(d + i + 4) & 0x7FF); }
                if (ein && eout) { o->data_alt = (uint8_t)cur_alt; }
            }
        }
        i += l;
    }
    (void)have_union;
    if (!kind || !ein || !eout) return 0;
    o->kind = kind; o->comm_if = comm; o->data_if = data; o->in_addr = ein; o->out_addr = eout; o->in_mps = min_; o->out_mps = mout;
    o->mac_str = mac; o->max_seg = seg;
    return 1;
}

int usbnet_mac_from_string(const uint8_t *s, uint32_t len, uint8_t mac[6])
{
    if (len < 2 + 24 || s[1] != 3 || s[0] < 2 + 24) return -1;
    for (int k = 0; k < 12; k++) {
        uint8_t c = s[2 + 2 * k]; int v;
        if (s[3 + 2 * k]) return -1;
        if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else return -1;
        if (k & 1) mac[k / 2] = (uint8_t)(mac[k / 2] | v); else mac[k / 2] = (uint8_t)(v << 4);
    }
    return 0;
}

/* ---- RNDIS ---- */
uint32_t rndis_build_init(uint8_t *b, uint32_t rid, uint32_t max_transfer)
{
    zero(b, 24); put32(b, RNDIS_MSG_INIT); put32(b + 4, 24); put32(b + 8, rid); put32(b + 12, 1); put32(b + 16, 0); put32(b + 20, max_transfer);
    return 24;
}
/* Como Linux (rndis_query): con un bufer de entrada a cero de in_len bytes (algunos dispositivos lo exigen dentro del mensaje) */
uint32_t rndis_build_query(uint8_t *b, uint32_t rid, uint32_t oid, uint32_t in_len)
{
    if (in_len > 256) in_len = 256;
    zero(b, 28 + in_len); put32(b, RNDIS_MSG_QUERY); put32(b + 4, 28 + in_len); put32(b + 8, rid); put32(b + 12, oid);
    put32(b + 16, in_len); put32(b + 20, 20);
    return 28 + in_len;
}
uint32_t rndis_build_set32(uint8_t *b, uint32_t rid, uint32_t oid, uint32_t v)
{
    zero(b, 32); put32(b, RNDIS_MSG_SET); put32(b + 4, 32); put32(b + 8, rid); put32(b + 12, oid); put32(b + 16, 4); put32(b + 20, 20); put32(b + 28, v);
    return 32;
}
uint32_t rndis_build_keepalive_cmplt(uint8_t *b, uint32_t rid)
{
    zero(b, 16); put32(b, RNDIS_MSG_KEEPALIVE | RNDIS_CMPLT); put32(b + 4, 16); put32(b + 8, rid); put32(b + 12, 0);
    return 16;
}
int rndis_parse_cmplt(const uint8_t *b, uint32_t len, uint32_t want, uint32_t rid, const uint8_t **info, uint32_t *ilen)
{
    if (len < 16) return -1;
    uint32_t type = le32(b), mlen = le32(b + 4);
    if (type != (want | RNDIS_CMPLT) || mlen > len || mlen < 16) return -2;
    if (le32(b + 8) != rid) return -3;
    if (le32(b + 12) != 0) return -4;                                    /* RNDIS_STATUS_SUCCESS */
    if (want == RNDIS_MSG_QUERY) {
        if (mlen < 24) return -5;
        uint32_t il = le32(b + 16), io = le32(b + 20);
        if (io > mlen || il > mlen - 8 || 8 + io > mlen || il > mlen - 8 - io) return -6;
        if (info) *info = b + 8 + io;
        if (ilen) *ilen = il;
    }
    return 0;
}
int rndis_parse_init_cmplt(const uint8_t *b, uint32_t len, uint32_t rid, uint32_t *max_xfer, uint32_t *align)
{
    if (rndis_parse_cmplt(b, len, RNDIS_MSG_INIT, rid, 0, 0) != 0 || len < 52 || le32(b + 4) < 52) return -1;
    *max_xfer = le32(b + 32); *align = le32(b + 36);
    return 0;
}
uint32_t rndis_wrap(uint8_t *o, const uint8_t *f, uint32_t n)
{
    zero(o, RNDIS_PKT_HDR);
    put32(o, RNDIS_MSG_PACKET); put32(o + 4, RNDIS_PKT_HDR + n); put32(o + 8, RNDIS_PKT_HDR - 8); put32(o + 12, n);
    copy(o + RNDIS_PKT_HDR, f, n);
    return RNDIS_PKT_HDR + n;
}
int rndis_unwrap(const uint8_t *b, uint32_t len, void (*cb)(const uint8_t *, uint32_t, void *), void *u)
{
    uint32_t off = 0; int n = 0;
    while (off + RNDIS_PKT_HDR <= len) {
        const uint8_t *p = b + off;
        uint32_t type = le32(p), mlen = le32(p + 4), doff = le32(p + 8), dlen = le32(p + 12);
        if (type != RNDIS_MSG_PACKET || mlen < RNDIS_PKT_HDR || mlen > len - off) break;
        if (doff > mlen - 8 || dlen > mlen - 8 - doff) break;
        if (dlen >= 14 && dlen <= 1518) { cb(p + 8 + doff, dlen, u); n++; }
        off += mlen;
    }
    return n;
}

/* ---- NCM ---- */
int ncm_parse_params(const uint8_t *b, uint32_t len, struct ncm_params *p)
{
    if (len < 28 || le16(b) < 28) return -1;
    p->in_max = le32(b + 4); p->out_max = le32(b + 16);
    p->out_div = le16(b + 20); p->out_rem = le16(b + 22); p->out_align = le16(b + 24); p->out_maxdg = le16(b + 26);
    if (p->out_div == 0 || p->out_div > 1024 || (p->out_div & (p->out_div - 1))) p->out_div = 4;
    if (p->out_rem >= p->out_div) p->out_rem = 0;
    if (p->out_align == 0 || p->out_align > 64 || (p->out_align & (p->out_align - 1))) p->out_align = 4;
    if (p->out_max < 2048 || p->out_max > 65536) p->out_max = p->out_max < 2048 ? 2048 : 65536;
    return 0;
}
uint32_t ncm_build_ntb(uint8_t *o, uint32_t cap, const struct ncm_params *p, uint16_t seq, const uint8_t *f, uint32_t n)
{
    uint32_t ndp = 12;                                                   /* NTH16 de 12 bytes y el NDP16 detras, alineado a 4 */
    ndp = (ndp + p->out_align - 1) & ~(uint32_t)(p->out_align - 1);
    uint32_t ndp_len = 16;                                               /* cabecera 8 + una entrada + el final (0,0) */
    uint32_t dg = ndp + ndp_len;
    uint32_t div = p->out_div, rem = p->out_rem;                         /* la trama empieza en un multiplo de div mas rem */
    if (dg % div != rem) dg = dg - (dg % div) + rem + (dg % div > rem ? div : 0);
    uint32_t total = dg + n;
    if (total > cap || total > p->out_max || n == 0 || n > 1518) return 0;
    zero(o, dg);
    put32(o, 0x484D434Eu); put16(o + 4, 12); put16(o + 6, seq); put16(o + 8, (uint16_t)total); put16(o + 10, (uint16_t)ndp);
    put32(o + ndp, 0x304D434Eu); put16(o + ndp + 4, (uint16_t)ndp_len); put16(o + ndp + 6, 0);
    put16(o + ndp + 8, (uint16_t)dg); put16(o + ndp + 10, (uint16_t)n);
    copy(o + dg, f, n);
    return total;
}
int ncm_parse_ntb(const uint8_t *b, uint32_t len, void (*cb)(const uint8_t *, uint32_t, void *), void *u)
{
    if (len < 12 || le32(b) != 0x484D434Eu || le16(b + 4) != 12) return 0;
    uint32_t blen = le16(b + 8); if (blen == 0 || blen > len) blen = len;
    uint32_t ndp = le16(b + 10); int n = 0, hops = 0;
    while (ndp && ndp + 8 <= blen && hops++ < 16) {
        uint32_t sig = le32(b + ndp), nl = le16(b + ndp + 4);
        if ((sig != 0x304D434Eu && sig != 0x314D434Eu) || nl < 16 || ndp + nl > blen) break;
        for (uint32_t e = ndp + 8; e + 4 <= ndp + nl; e += 4) {
            uint32_t di = le16(b + e), dl = le16(b + e + 2);
            if (!di || !dl) break;
            if (di >= blen || dl > blen - di) continue;
            if (sig == 0x314D434Eu) { if (dl < 4) continue; dl -= 4; }   /* NCM1: con CRC al final */
            if (dl >= 14 && dl <= 1518) { cb(b + di, dl, u); n++; }
        }
        uint32_t next = le16(b + ndp + 6);
        if (next <= ndp) break;                                          /* evita bucles */
        ndp = next;
    }
    return n;
}
