#!/usr/bin/env python3
"""SOMA_194: genera tests/x509_vectors_194.h (cadenas de certificados y firmas de prueba) con la biblioteca `cryptography`, que
sirve de referencia independiente del codigo del nucleo. Uso: python3 tests/x509_vectors_gen.py   (pip install cryptography)"""
import datetime, hashlib, os, sys
from pathlib import Path
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding
import ipaddress

T0 = datetime.datetime(2026, 1, 1); T1 = datetime.datetime(2027, 12, 31)
NOW = int(datetime.datetime(2026, 11, 17, 12, 0, 0, tzinfo=datetime.timezone.utc).timestamp())
H = {"sha256": hashes.SHA256(), "sha384": hashes.SHA384(), "sha512": hashes.SHA512(), "sha1": hashes.SHA1()}
_keys = {}
def key(kind, tag):
    k = (kind, tag)
    if k not in _keys:
        _keys[k] = (rsa.generate_private_key(65537, int(kind[3:])) if kind.startswith("rsa") else
                    ec.generate_private_key(ec.SECP256R1() if kind == "p256" else ec.SECP384R1()))
    return _keys[k]
def name(cn): return x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "SOMA pruebas"), x509.NameAttribute(NameOID.COMMON_NAME, cn)])
def cert(subject, skey, issuer, ikey, h="sha256", ca=False, san=None, nb=T0, na=T1, bc=True):
    b = (x509.CertificateBuilder().subject_name(name(subject)).issuer_name(name(issuer)).public_key(skey.public_key())
         .serial_number(x509.random_serial_number()).not_valid_before(nb).not_valid_after(na))
    if bc: b = b.add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
    if san:
        b = b.add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address(s[3:])) if s.startswith("ip:") else x509.DNSName(s) for s in san]), critical=False)
    return b.sign(ikey, H[h]).public_bytes(serialization.Encoding.DER)
def msg(*certs):
    body = b"".join(len(c).to_bytes(3, "big") + c + b"\x00\x00" for c in certs)
    return b"\x00" + len(body).to_bytes(3, "big") + body
def subject_spki(der):
    c = x509.load_der_x509_certificate(der)
    return c.subject.public_bytes(), c.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)

OK, FORMAT, ALG, SIG, UNTRUSTED, EXPIRED, HOST, NOTCA, TOOLONG, CV = range(10)
roots = []; cases = []; cvs = []
def root(kind, tag, h="sha256"):
    k = key(kind, tag); c = cert("Raiz " + tag, k, "Raiz " + tag, k, h, ca=True); roots.append(c); return k, c

rk1, rc1 = root("rsa2048", "R1")
rk2, rc2 = root("rsa4096", "R2", "sha512")
rk3, rc3 = root("p256", "E1")
rk4, rc4 = root("p384", "E2", "sha384")
uk = key("rsa2048", "U"); uc = cert("Raiz ajena", uk, "Raiz ajena", uk, ca=True)          # NO esta en la lista de confianza

# 1. RSA de extremo a extremo
ik1 = key("rsa2048", "I1"); ic1 = cert("Intermedia 1", ik1, "Raiz R1", rk1, ca=True)
lk1 = key("rsa2048", "L1"); lc1 = cert("hoja", lk1, "Intermedia 1", ik1, san=["www.ejemplo.test", "*.comodin.test", "ip:10.0.2.2"])
cases += [("RSA 2048, raiz-intermedia-hoja", msg(lc1, ic1), "www.ejemplo.test", NOW, OK),
          ("host en mayusculas y con punto final", msg(lc1, ic1), "WWW.Ejemplo.TEST.", NOW, OK),
          ("comodin de una etiqueta", msg(lc1, ic1), "a.comodin.test", NOW, OK),
          ("comodin: dos etiquetas no", msg(lc1, ic1), "a.b.comodin.test", NOW, HOST),
          ("comodin: el dominio pelado no", msg(lc1, ic1), "comodin.test", NOW, HOST),
          ("direccion IP en el certificado", msg(lc1, ic1), "10.0.2.2", NOW, OK),
          ("otra IP", msg(lc1, ic1), "10.0.2.3", NOW, HOST),
          ("otro sitio", msg(lc1, ic1), "www.otro.test", NOW, HOST),
          ("sufijo parecido", msg(lc1, ic1), "www.ejemplo.test.atacante.test", NOW, HOST),
          ("sin la intermedia", msg(lc1), "www.ejemplo.test", NOW, UNTRUSTED),
          ("intermedia y raiz enviadas, con un certificado ajeno en medio", msg(lc1, uc, ic1, rc1), "www.ejemplo.test", NOW, OK),
          ("reloj desconocido (0): no se miran fechas", msg(lc1, ic1), "www.ejemplo.test", 0, OK),
          ("fecha anterior a la validez", msg(lc1, ic1), "www.ejemplo.test", NOW - 400 * 86400, EXPIRED),
          ("fecha posterior a la validez", msg(lc1, ic1), "www.ejemplo.test", NOW + 800 * 86400, EXPIRED)]
# 2. manipulaciones
bad = bytearray(lc1); bad[len(bad) // 3] ^= 0x01
cases.append(("hoja alterada (un bit del contenido)", msg(bytes(bad), ic1), "www.ejemplo.test", NOW, None))     # SIG o FORMAT segun donde caiga
bad = bytearray(lc1); bad[-5] ^= 0x40
cases.append(("firma de la hoja alterada", msg(bytes(bad), ic1), "www.ejemplo.test", NOW, SIG))
lcx = cert("hoja", lk1, "Intermedia 1", key("rsa2048", "IX"), san=["www.ejemplo.test"])
cases.append(("hoja firmada por otra clave con el nombre de la intermedia", msg(lcx, ic1), "www.ejemplo.test", NOW, SIG))
# 3. raiz no fiable / intermedia que no es autoridad / caducada
ikU = key("rsa2048", "IU"); icU = cert("Intermedia U", ikU, "Raiz ajena", uk, ca=True)
lcU = cert("hoja", lk1, "Intermedia U", ikU, san=["www.ejemplo.test"])
cases.append(("cadena correcta hasta una raiz que no es de confianza", msg(lcU, icU, uc), "www.ejemplo.test", NOW, UNTRUSTED))
ikN = key("rsa2048", "IN"); icN = cert("Intermedia N", ikN, "Raiz R1", rk1, ca=False)
lcN = cert("hoja", lk1, "Intermedia N", ikN, san=["www.ejemplo.test"])
cases.append(("intermedia sin permiso de autoridad (un sitio firmando a otro)", msg(lcN, icN), "www.ejemplo.test", NOW, NOTCA))
icB = cert("Intermedia B", ikN, "Raiz R1", rk1, ca=True, bc=False)
lcB = cert("hoja", lk1, "Intermedia B", ikN, san=["www.ejemplo.test"])
cases.append(("intermedia sin basicConstraints", msg(lcB, icB), "www.ejemplo.test", NOW, NOTCA))
lcE = cert("hoja", lk1, "Intermedia 1", ik1, san=["www.ejemplo.test"], nb=datetime.datetime(2024, 1, 1), na=datetime.datetime(2025, 1, 1))
cases.append(("hoja caducada", msg(lcE, ic1), "www.ejemplo.test", NOW, EXPIRED))
icE = cert("Intermedia E", ikN, "Raiz R1", rk1, ca=True, nb=datetime.datetime(2024, 1, 1), na=datetime.datetime(2025, 1, 1))
lcE2 = cert("hoja", lk1, "Intermedia E", ikN, san=["www.ejemplo.test"])
cases.append(("intermedia caducada", msg(lcE2, icE), "www.ejemplo.test", NOW, EXPIRED))
lcC = cert("www.ejemplo.test", lk1, "Intermedia 1", ik1)
cases.append(("sin subjectAltName (solo CN)", msg(lcC, ic1), "www.ejemplo.test", NOW, HOST))
# 4. RSA 4096 / SHA-512 y SHA-384, hoja directa de la raiz
lk2 = key("rsa3072", "L2"); lc2 = cert("hoja", lk2, "Raiz R2", rk2, "sha384", san=["grande.test"])
cases.append(("RSA 4096 firmando con SHA-384 una hoja RSA 3072", msg(lc2), "grande.test", NOW, OK))
lc2b = cert("hoja", lk2, "Raiz R2", rk2, "sha512", san=["grande.test"])
cases.append(("RSA 4096 con SHA-512", msg(lc2b), "grande.test", NOW, OK))
# 5. ECDSA
ik3 = key("p256", "I3"); ic3 = cert("Intermedia 3", ik3, "Raiz E1", rk3, ca=True)
lk3 = key("p256", "L3"); lc3 = cert("hoja", lk3, "Intermedia 3", ik3, san=["ec.test"])
cases.append(("ECDSA P-256 de extremo a extremo", msg(lc3, ic3), "ec.test", NOW, OK))
lc3b = cert("hoja", lk3, "Intermedia 3", ik3, "sha384", san=["ec.test"])
cases.append(("P-256 firmando con SHA-384 (huella mas larga que la curva)", msg(lc3b, ic3), "ec.test", NOW, OK))
ik4 = key("p384", "I4"); ic4 = cert("Intermedia 4", ik4, "Raiz E2", rk4, "sha384", ca=True)
lk4 = key("p384", "L4"); lc4 = cert("hoja", lk4, "Intermedia 4", ik4, "sha384", san=["ec384.test"])
cases.append(("ECDSA P-384 de extremo a extremo", msg(lc4, ic4), "ec384.test", NOW, OK))
lc4b = cert("hoja", lk3, "Intermedia 4", ik4, "sha256", san=["mixto.test"])
cases.append(("P-384 firmando con SHA-256 una hoja P-256", msg(lc4b, ic4), "mixto.test", NOW, OK))
# 6. mezcla: raiz RSA -> intermedia P-384 -> hoja P-256 (como Let's Encrypt)
ik5 = key("p384", "I5"); ic5 = cert("Intermedia 5", ik5, "Raiz R2", rk2, "sha256", ca=True)
lc5 = cert("hoja", lk3, "Intermedia 5", ik5, "sha384", san=["le.test"])
cases.append(("raiz RSA 4096 -> intermedia P-384 -> hoja P-256", msg(lc5, ic5), "le.test", NOW, OK))
bad = bytearray(lc5); bad[-3] ^= 0x10
cases.append(("firma ECDSA alterada", msg(bytes(bad), ic5), "le.test", NOW, None))
# 7. autofirmado
sk = key("p256", "S"); sc = cert("propio", sk, "propio", sk, san=["llm.local", "ip:192.168.1.200"])
cases.append(("autofirmado que no esta en la lista", msg(sc), "llm.local", NOW, UNTRUSTED))
# 8. SHA-1
try:
    lcS = cert("hoja", lk1, "Intermedia 1", ik1, "sha1", san=["www.ejemplo.test"])
    cases.append(("firmado con SHA-1", msg(lcS, ic1), "www.ejemplo.test", NOW, ALG))
except Exception as e:
    print("aviso: no se pudo generar el caso SHA-1:", e, file=sys.stderr)
# 9. mensajes mal formados
good = msg(lc1, ic1)
cases += [("mensaje vacio", b"", "www.ejemplo.test", NOW, FORMAT), ("longitud de lista incoherente", good[:1] + b"\x00\x00\x05" + good[4:], "www.ejemplo.test", NOW, FORMAT),
          ("cortado a la mitad", good[:len(good) // 2], "www.ejemplo.test", NOW, FORMAT), ("lista vacia", b"\x00\x00\x00\x00", "www.ejemplo.test", NOW, FORMAT)]

# CertificateVerify
def cv_content(th): return b" " * 64 + b"TLS 1.3, server CertificateVerify\x00" + th
th = hashlib.sha256(b"huella del saludo").digest()
def sign(k, scheme):
    c = cv_content(th)
    if scheme in (0x0804, 0x0805, 0x0806):
        h = {0x0804: hashes.SHA256(), 0x0805: hashes.SHA384(), 0x0806: hashes.SHA512()}[scheme]
        return k.sign(c, padding.PSS(mgf=padding.MGF1(h), salt_length=h.digest_size), h)
    return k.sign(c, ec.ECDSA(hashes.SHA256() if scheme == 0x0403 else hashes.SHA384()))
def cvmsg(scheme, sig): return scheme.to_bytes(2, "big") + len(sig).to_bytes(2, "big") + sig
chains = {"rsa2048": (msg(lc1, ic1), "www.ejemplo.test", lk1), "rsa3072": (msg(lc2), "grande.test", lk2), "p256": (msg(lc3, ic3), "ec.test", lk3), "p384": (msg(lc4, ic4), "ec384.test", lk4)}
for kn, (m, host, k) in chains.items():
    schemes = (0x0804, 0x0805, 0x0806) if kn.startswith("rsa") else ((0x0403,) if kn == "p256" else (0x0503,))
    for s in schemes:
        sig = sign(k, s)
        cvs.append((f"{kn} esquema {s:04x}", m, host, cvmsg(s, sig), th, OK))
        cvs.append((f"{kn} esquema {s:04x}, otra huella", m, host, cvmsg(s, sig), hashlib.sha256(b"otra").digest(), CV))
        b = bytearray(sig); b[len(b) // 2] ^= 4
        cvs.append((f"{kn} esquema {s:04x}, firma alterada", m, host, cvmsg(s, bytes(b)), th, CV))
cvs.append(("firma de OTRA clave RSA (el servidor no tiene la clave del certificado)", chains["rsa2048"][0], "www.ejemplo.test", cvmsg(0x0804, sign(key("rsa2048", "IX"), 0x0804)), th, CV))
cvs.append(("firma de OTRA clave P-256", chains["p256"][0], "ec.test", cvmsg(0x0403, sign(key("p256", "S"), 0x0403)), th, CV))
cvs.append(("esquema ECDSA con clave RSA", chains["rsa2048"][0], "www.ejemplo.test", cvmsg(0x0403, sign(lk3, 0x0403)), th, CV))
cvs.append(("esquema no admitido (PKCS#1 v1.5 en el saludo)", chains["rsa2048"][0], "www.ejemplo.test", cvmsg(0x0401, sign(lk1, 0x0804)), th, ALG))
cvs.append(("mensaje cortado", chains["rsa2048"][0], "www.ejemplo.test", cvmsg(0x0804, sign(lk1, 0x0804))[:40], th, FORMAT))

def arr(b): return "{" + ",".join(str(x) for x in b) + "}" if b else "{0}"
out = ["/* GENERADO por tests/x509_vectors_gen.py (referencia: biblioteca cryptography de Python). No editar. */", "#include <stdint.h>"]
blob = b"".join(len(s).to_bytes(2, "big") + s + len(p).to_bytes(2, "big") + p for s, p in map(subject_spki, roots))
out.append(f"static const uint8_t V_ROOTS[] = {arr(blob)};")
out.append(f"static const uint8_t V_SELF[] = {arr(sc)};")
pem = x509.load_der_x509_certificate(uc).public_bytes(serialization.Encoding.PEM)
out.append(f"static const char V_PEM[] = {arr(pem)};")
out.append(f"#define V_NOW {NOW}ULL")
for i, (n, m, h, now, exp) in enumerate(cases): out.append(f"static const uint8_t V_C{i}[] = {arr(m)};")
out.append("static const struct { const char *name; const uint8_t *msg; uint32_t len; const char *host; uint64_t now; int expect; } V_CASES[] = {")
for i, (n, m, h, now, exp) in enumerate(cases): out.append(f'    {{ "{n}", V_C{i}, {len(m)}, "{h}", {now}ULL, {-1 if exp is None else exp} }},')
out.append("};")
uniq = {}
for n, m, h, cvb, t, exp in cvs: uniq.setdefault(m, len(uniq))
for m, i in uniq.items(): out.append(f"static const uint8_t V_M{i}[] = {arr(m)};")
for i, (n, m, h, cvb, t, exp) in enumerate(cvs): out.append(f"static const uint8_t V_S{i}[] = {arr(cvb)};\nstatic const uint8_t V_T{i}[] = {arr(t)};")
out.append("static const struct { const char *name; const uint8_t *msg; uint32_t len; const char *host; const uint8_t *cv; uint32_t cvlen; const uint8_t *th; int expect; } V_CVS[] = {")
for i, (n, m, h, cvb, t, exp) in enumerate(cvs): out.append(f'    {{ "{n}", V_M{uniq[m]}, {len(m)}, "{h}", V_S{i}, {len(cvb)}, V_T{i}, {exp} }},')
out.append("};")
Path(__file__).resolve().parent.joinpath("x509_vectors_194.h").write_text("\n".join(out) + "\n")
print(f"{len(cases)} cadenas, {len(cvs)} firmas de saludo, {len(roots)} raices")
