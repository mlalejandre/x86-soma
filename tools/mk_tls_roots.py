#!/usr/bin/env python3
"""SOMA_194: genera src/core/tls_roots.c (autoridades de confianza del TLS del nucleo) a partir de un fichero PEM de raices.

    python3 tools/mk_tls_roots.py cacert.pem            # p. ej. el de Mozilla: https://curl.se/ca/cacert.pem (o el paquete certifi)

De cada raiz solo se guarda el nombre (subject, DER) y la clave publica (SubjectPublicKeyInfo, DER); el nucleo no necesita mas.
Se descartan las raices con claves que el nucleo no verifica (solo RSA >= 2048 y ECDSA P-256/P-384) y las caducadas.
Sin dependencias: lleva su propio lector de DER."""
import base64, datetime, re, sys
from pathlib import Path

OID_RSA = bytes.fromhex("2a864886f70d010101")
OID_EC = bytes.fromhex("2a8648ce3d0201")
OID_P256 = bytes.fromhex("2a8648ce3d030107")
OID_P384 = bytes.fromhex("2b81040022")


def tlv(b, o):
    """(etiqueta, inicio del contenido, fin del contenido)"""
    tag = b[o]; l = b[o + 1]; o += 2
    if l & 0x80:
        n = l & 0x7F; l = int.from_bytes(b[o:o + n], "big"); o += n
    return tag, o, o + l


def parse(der):
    _, o, _ = tlv(der, 0)                         # Certificate
    _, o, _ = tlv(der, o)                         # tbsCertificate
    tag, a, e = tlv(der, o)
    if tag == 0xA0:                               # version
        o = e; tag, a, e = tlv(der, o)
    o = e                                         # serial
    _, _, o = tlv(der, o)                         # signature
    _, _, o = tlv(der, o)                         # issuer
    _, va, o2 = tlv(der, o)                       # validity
    _, _, t1 = tlv(der, va); tt, ta, te = tlv(der, t1)
    s = der[ta:te].decode()
    year = int(s[:4]) if tt == 0x18 else (2000 + int(s[:2]) if int(s[:2]) < 50 else 1900 + int(s[:2]))
    na = datetime.datetime(year, int(s[-11:-9]), int(s[-9:-7]))
    o = o2
    _, _, e = tlv(der, o); subject = der[o:e]; o = e
    _, sa, e = tlv(der, o); spki = der[o:e]
    _, aa, ae = tlv(der, sa)                      # AlgorithmIdentifier
    _, oa, oe = tlv(der, aa); oid = der[oa:oe]
    kind = None
    if oid == OID_RSA:
        _, ba, be = tlv(der, ae); _, qa, _ = tlv(der, ba + 1); _, na_, ne = tlv(der, qa)
        bits = (int.from_bytes(der[na_:ne], "big")).bit_length()
        kind = "RSA" if 2048 <= bits <= 4096 else None
    elif oid == OID_EC:
        _, ca, ce = tlv(der, oe)
        kind = {OID_P256: "P-256", OID_P384: "P-384"}.get(der[ca:ce])
    return subject, spki, kind, na


def main():
    if len(sys.argv) < 2:
        print(__doc__); return 2
    pem = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "src/core/tls_roots.c"
    blob = bytearray(); n = 0; skipped = []; kinds = {}
    now = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None)
    for m in re.finditer(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S):
        der = base64.b64decode(m.group(1))
        subject, spki, kind, not_after = parse(der)
        if kind is None or not_after < now or len(subject) > 0xFFFF or len(spki) > 0xFFFF:
            skipped.append(kind or "clave no admitida"); continue
        blob += len(subject).to_bytes(2, "big") + subject + len(spki).to_bytes(2, "big") + spki
        n += 1; kinds[kind] = kinds.get(kind, 0) + 1
    lines = ["/* SOMA_194: autoridades de confianza del TLS del nucleo. GENERADO por tools/mk_tls_roots.py: no editar a mano.",
             f" * {n} raices ({', '.join(f'{v} {k}' for k, v in sorted(kinds.items()))}), {len(blob)} bytes. Origen: lista de raices de Mozilla (MPL 2.0).",
             " * Formato: [u16 longitud][nombre DER][u16 longitud][SubjectPublicKeyInfo DER], repetido. */",
             '#include "tls_roots.h"', "", "const uint8_t tls_roots_blob[] = {"]
    for i in range(0, len(blob), 32):
        lines.append("    " + ",".join(str(b) for b in blob[i:i + 32]) + ",")
    lines += ["};", "const uint32_t tls_roots_len = sizeof tls_roots_blob;", ""]
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"{out}: {n} raices, {len(blob)} bytes ({len(skipped)} descartadas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
