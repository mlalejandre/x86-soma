#!/usr/bin/env python3
"""PARCHE 138: genera src/arch/x86_64/sbx_testimg.h (imagenes de prueba del servicio aislado imgdec + resultado esperado).
El resultado esperado lo calculan los decodificadores reales compilados en el host (tests/test_sbx_ref.c --gen).
Requiere Pillow solo para REGENERAR; la cabecera generada va en el repositorio. Uso: python3 tools/gen_sbx_testimg.py"""
import gzip, io, os, random, struct, subprocess, sys, tempfile, zlib
from PIL import Image

OUT = "src/arch/x86_64/sbx_testimg.h"

def png_rgba():
    im = Image.new("RGBA", (37, 23))
    im.putdata([(x * 7 % 256, y * 11 % 256, (x * y) % 256, (x + y * 3) % 256) for y in range(23) for x in range(37)])
    b = io.BytesIO(); im.save(b, "PNG"); return b.getvalue()

def png_pal_trns():
    im = Image.new("P", (29, 17)); im.putpalette([v for i in range(16) for v in (i * 16, 255 - i * 16, i * 8)])
    im.putdata([(x + y) % 16 for y in range(17) for x in range(29)])
    b = io.BytesIO(); im.save(b, "PNG", transparency=3); return b.getvalue()

def png_gray16():
    im = Image.new("I;16", (21, 13)); im.putdata([(x * 3000 + y * 500) % 65536 for y in range(13) for x in range(21)])
    b = io.BytesIO(); im.save(b, "PNG"); return b.getvalue()

def gif():
    im = Image.new("P", (33, 19)); im.putpalette([v for i in range(32) for v in (i * 8, i * 4, 255 - i * 8)])
    im.putdata([(x * y + x) % 32 for y in range(19) for x in range(33)])
    b = io.BytesIO(); im.save(b, "GIF"); return b.getvalue()

def photo(w, h, seed):
    r = random.Random(seed); im = Image.new("RGB", (w, h))
    noise = 20 if w * h < 10000 else 0                   # la grande, sin ruido: comprime ~10x (la cabecera va tambien al disco de fuentes)
    im.putdata([((x * 255 // w + r.randint(0, noise)) % 256, (y * 255 // h + r.randint(0, noise)) % 256, ((x // 8 ^ y // 8) * 9 & 255)) for y in range(h) for x in range(w)])
    return im

def jpeg(prog=False, cmyk=False, w=48, h=40):
    im = photo(w, h, 7)
    if cmyk: im = im.convert("CMYK")
    b = io.BytesIO(); im.save(b, "JPEG", quality=85, progressive=prog); return b.getvalue()

def bmp(mode):
    if mode == "RGBA":
        im = Image.new("RGBA", (19, 11)); im.putdata([(x * 13 % 256, y * 23 % 256, 77, (x * 20) % 256) for y in range(11) for x in range(19)])
    else:
        im = photo(23, 15, 3)
    b = io.BytesIO(); im.save(b, "BMP"); return b.getvalue()

def bmp8_short_palette():
    """8 bpp con clr_used=4 e indices hasta 255: los que no tienen color deben salir negros opacos (antes: pila del kernel)."""
    w, h = 16, 4; stride = 16
    pal = b"".join(struct.pack("<BBBB", 50 * i, 40 * i, 30 * i, 0) for i in range(4))
    px = bytes((x * 17 + y * 64) % 256 for y in range(h) for x in range(w))
    off = 14 + 40 + len(pal)
    hdr = b"BM" + struct.pack("<IHHI", off + len(px), 0, 0, off)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(px), 2835, 2835, 4, 0)
    return hdr + dib + pal + px

IMAGES = [  # nombre, bytes, op (1 decode, 2 fit, 3 bmp con alfa), a0, a1
    ("png_rgba", png_rgba(), 1, 0, 0),
    ("png_pal_trns", png_pal_trns(), 1, 0, 0),
    ("png_gray16", png_gray16(), 1, 0, 0),
    ("gif", gif(), 1, 0, 0),
    ("jpeg_base", jpeg(), 1, 0, 0),
    ("jpeg_prog", jpeg(prog=True), 1, 0, 0),
    ("jpeg_cmyk", jpeg(cmyk=True), 1, 0, 0),
    ("jpeg_big_fit", jpeg(w=640, h=480), 2, 100, 80),
    ("jpeg_big", jpeg(w=640, h=480), 1, 0, 0),
    ("bmp24", bmp("RGB"), 1, 0, 0),
    ("bmp32_raw", bmp("RGBA"), 3, 0, 0),
    ("bmp8_short_pal", bmp8_short_palette(), 3, 0, 0),
    ("basura", bytes(random.Random(1).randrange(256) for _ in range(300)), 1, 0, 0),
    # PARCHE 139: fotogramas MJPEG (op 4 = jpeg_decode crudo: a0 = stride, a1 = alto maximo)
    ("mjpeg_frame", jpeg(), 4, 64, 40),
    ("mjpeg_no_cabe", jpeg(), 4, 32, 40),
    ("png_truncado", png_rgba()[:60], 1, 0, 0),
]

TEXT = b"".join(b"SOMA linea %d: el inflate corre aislado en ring 3. " % i for i in range(400))
ZTESTS = [  # nombre, bytes, fmt (0 crudo, 1 zlib, 2 gzip, 3 auto), max_out
    ("z_gzip", gzip.compress(TEXT, mtime=0), 2, 1 << 20),
    ("z_zlib", zlib.compress(TEXT, 9), 1, 1 << 20),
    ("z_raw", zlib.compressobj(6, zlib.DEFLATED, -15).compress(TEXT) + zlib.compressobj(6, zlib.DEFLATED, -15).flush()[:0], 0, 1 << 20),
    ("z_auto_gzip", gzip.compress(TEXT, mtime=0), 3, 1 << 20),
    ("z_truncado", zlib.compress(TEXT, 9)[:200], 1, 1 << 20),
    ("z_bomba", zlib.compress(b"\0" * (8 << 20), 9), 1, 1 << 20),
]
def _raw(data):
    c = zlib.compressobj(6, zlib.DEFLATED, -15); return c.compress(data) + c.flush()
ZTESTS[2] = ("z_raw", _raw(TEXT), 0, 1 << 20)

def pdf_multi():
    """PARCHE 141: 3 paginas (una girada 90), imagen JPEG (DCTDecode), trayecto con curva, texto con acentos y titulo."""
    jp = jpeg(w=24, h=16)
    pages = [b"BT /F1 18 Tf 50 750 Td (Pagina uno) Tj ET 0 0.5 0 rg 50 600 m 150 700 250 500 350 600 c f",
             b"q 120 0 0 80 60 500 cm /Im1 Do Q BT /F1 12 Tf 60 480 Td (Imagen JPEG) Tj ET",
             b"BT /F1 14 Tf 40 300 Td (\\341rbol girado) Tj ET 1 0 0 RG 2 w 40 280 m 300 280 l S"]
    objs = {1: b"<< /Type /Catalog /Pages 2 0 R >>", 2: b"<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
            9: b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>",
            10: b"<< /Type /XObject /Subtype /Image /Width 24 /Height 16 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %d >>\nstream\n" % len(jp) + jp + b"\nendstream"}
    for i, c in enumerate(pages):
        rot = b" /Rotate 90" if i == 2 else b""
        objs[3 + i] = b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842]%s /Contents %d 0 R /Resources << /Font << /F1 9 0 R >> /XObject << /Im1 10 0 R >> >> >>" % (rot, 6 + i)
        z = zlib.compress(c)
        objs[6 + i] = b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(z) + z + b"\nendstream"
    d = b"%PDF-1.4\n"; offs = {}
    for k in sorted(objs):
        offs[k] = len(d); d += b"%d 0 obj\n" % k + objs[k] + b"\nendobj\n"
    x = len(d); n = max(objs) + 1
    d += b"xref\n0 %d\n0000000000 65535 f \n" % n
    for k in range(1, n): d += (b"%010d 00000 n \n" % offs[k]) if k in offs else b"0000000000 65535 f \n"
    d += b"trailer\n<< /Size %d /Root 1 0 R /Info << /Title (Documento de prueba \\351) >> >>\nstartxref\n%d\n%%%%EOF\n" % (n, x)
    return d

def pdfs():
    with tempfile.TemporaryDirectory() as td:
        subprocess.check_call([sys.executable, "tests/pdf_seeds.py", td], stdout=subprocess.DEVNULL)
        cl = open(os.path.join(td, "clasico.pdf"), "rb").read(); xs = open(os.path.join(td, "xrefstm.pdf"), "rb").read()
    mu = pdf_multi()
    return [("pdf_clasico", cl), ("pdf_xrefstm", xs), ("pdf_multi", mu), ("pdf_truncado", mu[:len(mu) // 2]),
            ("pdf_basura", b"%PDF-1.4\n" + bytes(random.Random(9).randrange(256) for _ in range(2000))),
            ("pdf_sin_cabecera", b"hola, esto no es un PDF")]

def emit(results, zres=None, pres=None):
    o = ["/* GENERADO por tools/gen_sbx_testimg.py: no editar a mano. PARCHE 138: imagenes de prueba del servicio aislado imgdec */",
         "#ifndef SOMA_SBX_TESTIMG_H", "#define SOMA_SBX_TESTIMG_H", "#include <stdint.h>",
         "struct sbx_testimg { const char *name; const uint8_t *data; uint32_t len; int op, a0, a1; int exp_r, exp_w, exp_h; uint32_t exp_hash; };"]
    arr = {}
    for n, d, *_ in IMAGES:
        if d in arr: continue                            # mismos bytes: un solo array
        arr[d] = n
        o.append(f"static const uint8_t sbxti_{n}[{len(d)}] = {{")
        for i in range(0, len(d), 24): o.append("    " + ",".join(str(x) for x in d[i:i + 24]) + ",")
        o.append("};")
    o.append("static const struct sbx_testimg sbx_testimg[] = {")
    for (n, d, op, a0, a1), res in zip(IMAGES, results):
        r, w, h, hs = res
        o.append(f'    {{ "{n}", sbxti_{arr[d]}, {len(d)}, {op}, {a0}, {a1}, {r}, {w}, {h}, {hs} }},')
    o.append("};")
    o.append(f"#define SBX_TESTIMG_N {len(IMAGES)}")
    o.append("struct sbx_testz { const char *name; const uint8_t *data; uint32_t len; int fmt; uint32_t max_out; int exp_r; uint32_t exp_len; uint32_t exp_hash; };")
    for n, d, *_ in ZTESTS:
        o.append(f"static const uint8_t sbxtz_{n}[{len(d)}] = {{")
        for i in range(0, len(d), 24): o.append("    " + ",".join(str(x) for x in d[i:i + 24]) + ",")
        o.append("};")
    o.append("static const struct sbx_testz sbx_testz[] = {")
    for i, (n, d, fmt, mx) in enumerate(ZTESTS):
        r, ln, hs = zres[i] if zres else (0, 0, "0u")
        o.append(f'    {{ "{n}", sbxtz_{n}, {len(d)}, {fmt}, {mx}u, {r}, {ln}u, {hs} }},')
    o.append("};")
    o.append(f"#define SBX_TESTZ_N {len(ZTESTS)}")
    P = pdfs()
    o.append("struct sbx_testpdf { const char *name; const uint8_t *data; uint32_t len; int exp_open; int exp_pages; uint32_t exp_hash; };")
    for n, d in P:
        o.append(f"static const uint8_t sbxtp_{n}[{len(d)}] = {{")
        for i in range(0, len(d), 24): o.append("    " + ",".join(str(x) for x in d[i:i + 24]) + ",")
        o.append("};")
    o.append("static const struct sbx_testpdf sbx_testpdf[] = {")
    for i, (n, d) in enumerate(P):
        ok, np, hs = pres[i] if pres else (0, 0, "0u")
        o.append(f'    {{ "{n}", sbxtp_{n}, {len(d)}, {ok}, {np}, {hs} }},')
    o.append("};")
    o.append(f"#define SBX_TESTPDF_N {len(P)}")
    o.append("#endif")
    open(OUT, "w").write("\n".join(o) + "\n")

def main():
    emit([(0, 0, 0, "0u")] * len(IMAGES))               # primero sin esperados, para compilar la referencia
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, "ref")
        cc = ["gcc", "-O1", "-w", "-DWEB_HOST_TEST", "-Isrc", "-Isrc/core", "-Isrc/apps", "-Isrc/arch/x86_64", "tests/test_sbx_ref.c",
              "src/apps/web_img.c", "src/apps/bmp_dec.c", "src/apps/web_jpeg.c", "src/apps/web_inflate.c", "src/core/jpeg.c", "src/apps/pdf.c", "-o", exe]
        subprocess.check_call(cc)
        lines = subprocess.check_output([exe, "--gen"], text=True).split("\n")
    res, zres, pres = [], [], []
    for l in lines:
        if not l.strip(): continue
        p = l.split()
        if p[0] == "P": pres.append((int(p[2]), int(p[3]), p[4] + "u")); continue
        if p[0] == "Z": zres.append((int(p[2]), int(p[3]), p[4] + "u"))
        else: res.append((int(p[1]), int(p[2]), int(p[3]), p[4] + "u"))
    emit(res, zres, pres)
    for (n, _), r in zip(pdfs(), pres): print(f"  {n:16s} abre={r[0]} paginas={r[1]} {r[2]}")
    for (n, *_), r in zip(ZTESTS, zres): print(f"  {n:16s} r={r[0]:3d} len={r[1]} {r[2]}")
    for (n, *_), r in zip(IMAGES, res): print(f"  {n:16s} r={r[0]:3d} {r[1]}x{r[2]} {r[3]}")

if __name__ == "__main__":
    main()
