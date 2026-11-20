#!/usr/bin/env python3
"""SOMA_225: genera las dos fuentes propias de SOMA a partir de esqueletos de trazo dibujados aqui mismo.

  SOMA Orbital  - sans geometrica de trazo uniforme: circulos perfectos, remates redondos.
  SOMA Tecnica  - sans tecnica: curvas en superelipse (cuadradas con esquinas suaves), remates rectos, algo estrecha.

Las dos salen del mismo esqueleto (lineas y arcos en una rejilla de 1000 unidades por em) con distinto dibujo de las curvas, grosor,
remates y anchura. El resultado son TTF validos (glyf, cmap 4) que el rasterizador del nucleo lee igual que DejaVu o Liberation.
Se emiten como un .c con los bytes (no se reparten archivos .ttf). Licencia: las mismas del proyecto SOMA (dibujo original).
"""
import math, sys
from shapely.geometry import LineString, Point, Polygon, MultiPolygon, box
from shapely.ops import unary_union
from shapely.geometry.polygon import orient
from shapely import affinity
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
import io

XH, CAP, ASC, DSC = 500, 700, 740, -220

# ------------------------------------------------------------------ esqueletos
class Path:
    def __init__(self, *pt): self.p = [tuple(pt)] if pt else []
    def L(self, x, y): self.p.append((x, y)); return self
    def A(self, cx, cy, rx, ry, a0, a1): self.p.append(('arc', cx, cy, rx, ry, a0, a1)); return self

def P(*pts):
    p = Path(); p.p = [tuple(q) for q in pts]; return p
def A(cx, cy, rx, ry, a0, a1):
    p = Path(); p.p = [('arc', cx, cy, rx, ry, a0, a1)]; return p
def O(cx, cy, rx, ry): return A(cx, cy, rx, ry, 90, 450)
def D(x, y): return ('dot', x, y)

G = {}
def g(ch, *strokes): G[ch] = list(strokes)

# minusculas (altura x 500)
g('o', O(240, 250, 240, 250))
g('a', O(230, 250, 230, 250), P((460, 500), (460, 0)))
g('b', P((0, 720), (0, 0)), O(240, 250, 240, 250))
g('c', A(240, 250, 240, 250, 42, 318))
g('d', O(240, 250, 240, 250), P((480, 720), (480, 0)))
g('e', Path(0, 250).L(480, 250).A(240, 250, 240, 250, 0, 318))
g('f', Path(110, 0).L(110, 600).A(240, 600, 130, 130, 180, 40), P((0, 480), (270, 480)))
g('g', O(230, 250, 230, 250), Path(460, 500).L(460, -50).A(230, -50, 230, 180, 0, -160))
g('h', P((0, 720), (0, 0)), Path(0, 280).A(220, 280, 220, 220, 180, 0).L(440, 0))
g('i', P((0, 500), (0, 0)), D(0, 660))
g('ı', P((0, 500), (0, 0)))
g('j', Path(140, 500).L(140, -60).A(20, -60, 120, 140, 0, -150), D(140, 660))
g('k', P((0, 720), (0, 0)), P((390, 500), (0, 190)), P((150, 300), (410, 0)))
g('l', P((0, 720), (0, 0)))
g('m', P((0, 500), (0, 0)), Path(0, 320).A(170, 320, 170, 180, 180, 0).L(340, 0), Path(340, 320).A(510, 320, 170, 180, 180, 0).L(680, 0))
g('n', P((0, 500), (0, 0)), Path(0, 280).A(220, 280, 220, 220, 180, 0).L(440, 0))
g('p', P((0, 500), (0, -220)), O(240, 250, 240, 250))
g('q', O(240, 250, 240, 250), P((480, 500), (480, -220)))
g('r', P((0, 500), (0, 0)), Path(0, 280).A(220, 280, 220, 220, 180, 60))
g('s', Path(400, 440).A(205, 375, 205, 125, 40, 270).A(205, 125, 205, 125, 90, -140))
g('t', Path(110, 650).L(110, 140).A(250, 140, 140, 140, 180, 270).L(290, 0), P((0, 480), (300, 480)))
g('u', Path(0, 500).L(0, 220).A(220, 220, 220, 220, 180, 360).L(440, 500), P((440, 500), (440, 0)))
g('v', P((0, 500), (230, 0), (460, 500)))
g('w', P((0, 500), (170, 0), (340, 420), (510, 0), (680, 500)))
g('x', P((0, 500), (430, 0)), P((0, 0), (430, 500)))
g('y', P((0, 500), (230, 40)), P((460, 500), (140, -220)))
g('z', P((0, 500), (430, 500), (0, 0), (430, 0)))

# mayusculas (700)
g('A', P((0, 0), (310, 700), (620, 0)), P((115, 240), (505, 240)))
g('B', Path(0, 360).L(0, 700).L(250, 700).A(250, 530, 170, 170, 90, -90).L(0, 360), Path(0, 360).L(270, 360).A(270, 180, 190, 180, 90, -90).L(0, 0).L(0, 360))
g('C', A(350, 350, 350, 350, 42, 318))
g('D', Path(0, 0).L(0, 700).L(230, 700).A(230, 350, 360, 350, 90, -90).L(0, 0))
g('E', P((520, 700), (0, 700), (0, 0), (520, 0)), P((0, 360), (440, 360)))
g('F', P((520, 700), (0, 700), (0, 0)), P((0, 360), (440, 360)))
g('G', Path(610, 600).A(350, 350, 350, 350, 45, 360).L(700, 330).L(420, 330))
g('H', P((0, 0), (0, 700)), P((560, 0), (560, 700)), P((0, 360), (560, 360)))
g('I', P((0, 0), (0, 700)))
g('J', Path(420, 700).L(420, 230).A(210, 230, 210, 230, 0, -180))
g('K', P((0, 0), (0, 700)), P((520, 700), (0, 260)), P((190, 420), (550, 0)))
g('L', P((0, 700), (0, 0), (450, 0)))
g('M', P((0, 0), (0, 700), (330, 160), (660, 700), (660, 0)))
g('N', P((0, 0), (0, 700), (560, 0), (560, 700)))
g('O', O(350, 350, 350, 350))
g('P', Path(0, 0).L(0, 700).L(270, 700).A(270, 520, 180, 180, 90, -90).L(0, 340))
g('Q', O(350, 350, 350, 350), P((440, 200), (720, -60)))
g('R', Path(0, 0).L(0, 700).L(270, 700).A(270, 520, 180, 180, 90, -90).L(0, 340), P((250, 340), (540, 0)))
g('S', Path(520, 600).A(275, 525, 265, 175, 30, 270).A(275, 175, 285, 175, 90, -145))
g('T', P((0, 700), (600, 700)), P((300, 700), (300, 0)))
g('U', Path(0, 700).L(0, 270).A(290, 270, 290, 270, 180, 360).L(580, 700))
g('V', P((0, 700), (310, 0), (620, 700)))
g('W', P((0, 700), (220, 0), (440, 560), (660, 0), (880, 700)))
g('X', P((0, 700), (570, 0)), P((0, 0), (570, 700)))
g('Y', P((0, 700), (300, 330), (600, 700)), P((300, 330), (300, 0)))
g('Z', P((0, 700), (570, 700), (0, 0), (570, 0)))

# cifras (tabulares)
g('0', O(270, 350, 270, 350))
g('1', P((60, 560), (230, 700), (230, 0)))
g('2', Path(20, 520).A(260, 490, 245, 210, 160, -30).L(0, 0).L(520, 0))
g('3', Path(30, 620).A(250, 525, 230, 175, 140, -90).A(255, 175, 265, 175, 90, -150))
g('4', P((390, 0), (390, 700), (0, 200), (550, 200)))
g('5', Path(480, 700).L(70, 700).L(40, 400).A(260, 235, 270, 235, 140, -150))
g('6', O(265, 230, 265, 230), P((30, 300), (340, 700)))
g('7', P((0, 700), (530, 700), (170, 0)))
g('8', O(265, 535, 215, 165), O(265, 190, 265, 190))
g('9', O(265, 470, 265, 230), P((500, 400), (190, 0)))

# signos
g('!', P((0, 700), (0, 200)), D(0, 0))
g('"', P((0, 720), (0, 520)), P((170, 720), (170, 520)))
g('#', P((150, 0), (230, 700)), P((370, 0), (450, 700)), P((0, 230), (560, 230)), P((40, 470), (600, 470)))
g('$', Path(500, 590).A(270, 525, 250, 170, 30, 270).A(270, 180, 270, 180, 90, -145), P((270, -80), (270, 780)))
g('%', O(120, 580, 120, 120), O(540, 120, 120, 120), P((40, 0), (620, 700)))
g('&', Path(600, 0).L(130, 470).A(250, 570, 130, 130, 220, -40).L(110, 250).A(250, 190, 250, 190, 150, 290).L(560, 330))
g("'", P((0, 720), (0, 520)))
g('(', A(320, 330, 320, 450, 115, 245))
g(')', A(0, 330, 320, 450, 65, -65))
g('*', P((200, 700), (200, 420)), P((70, 630), (330, 490)), P((70, 490), (330, 630)))
g('+', P((0, 300), (480, 300)), P((240, 60), (240, 540)))
g(',', P((50, 70), (0, -130)))
g('-', P((0, 280), (320, 280)))
g('.', D(0, 0))
g('/', P((0, -100), (420, 760)))
g(':', D(0, 0), D(0, 440))
g(';', D(50, 440), P((50, 70), (0, -130)))
g('<', P((480, 560), (0, 300), (480, 40)))
g('=', P((0, 190), (480, 190)), P((0, 410), (480, 410)))
g('>', P((0, 560), (480, 300), (0, 40)))
g('?', Path(20, 540).A(230, 520, 210, 190, 160, -60).L(230, 260).L(230, 190), D(230, 0))
g('@', O(420, 300, 150, 160), Path(570, 460).L(570, 200).A(670, 200, 100, 100, 180, 360).A(420, 300, 350, 400, 0, 315))
g('[', P((220, 780), (0, 780), (0, -120), (220, -120)))
g('\\', P((0, 760), (420, -100)))
g(']', P((0, 780), (220, 780), (220, -120), (0, -120)))
g('^', P((0, 420), (230, 700), (460, 420)))
g('_', P((0, -110), (520, -110)))
g('`', P((0, 760), (150, 620)))
g('{', Path(260, 780).A(260, 680, 120, 100, 90, 180).L(140, 430).A(20, 430, 120, 100, 0, -90).A(20, 230, 120, 100, 90, 0).L(140, -20).A(260, -20, 120, 100, 180, 270))
g('|', P((0, -120), (0, 780)))
g('}', Path(0, 780).A(0, 680, 120, 100, 90, 0).L(120, 430).A(240, 430, 120, 100, 180, 270).A(240, 230, 120, 100, 90, 180).L(120, -20).A(0, -20, 120, 100, 0, -90))
g('~', Path(0, 260).A(120, 260, 120, 90, 180, 0).A(360, 260, 120, 90, 180, 360))
g('¡', P((0, 500), (0, 0)), D(0, 700))
g('¿', Path(420, 160).A(210, 180, 210, 190, -20, -240).L(210, 440).L(210, 510), D(210, 700))
g('°', O(120, 600, 120, 120))
g('·', D(0, 300))
g('«', P((230, 460), (0, 260), (230, 60)), P((480, 460), (250, 260), (480, 60)))
g('»', P((0, 460), (230, 260), (0, 60)), P((250, 460), (480, 260), (250, 60)))
g('€', A(400, 350, 360, 350, 45, 315), P((-40, 420), (420, 420)), P((-40, 280), (380, 280)))
g('–', P((0, 280), (500, 280)))
g('—', P((0, 280), (900, 280)))
g('‘', P((60, 520), (0, 720)))
g('’', P((60, 720), (0, 520)))
g('“', P((60, 520), (0, 720)), P((230, 520), (170, 720)))
g('”', P((60, 720), (0, 520)), P((230, 720), (170, 520)))
g('…', D(0, 0), D(260, 0), D(520, 0))
g('•', ('bigdot', 0, 300))
g('º', O(150, 560, 150, 150), P((0, 330), (300, 330)))
g('ª', O(140, 560, 140, 150), P((280, 710), (280, 410)), P((0, 330), (300, 330)))

# acentos: (marca, base). La marca se coloca centrada sobre la base.
MARKS = {
    'acute': [P((-70, 0), (80, 140))],
    'grave': [P((70, 0), (-80, 140))],
    'diaer': [D(-110, 60), D(110, 60)],
    'circ':  [P((-130, 0), (0, 130), (130, 0))],
    'tilde': [Path(-170, 50).A(-85, 50, 85, 60, 180, 0).A(85, 50, 85, 60, 180, 360)],
}
COMBOS = {}
for base, lo, up in (('a', 'áàâäã', 'ÁÀÂÄÃ'), ('e', 'éèêë', 'ÉÈÊË'), ('i', 'íìîï', 'ÍÌÎÏ'),
                     ('o', 'óòôöõ', 'ÓÒÔÖÕ'), ('u', 'úùûü', 'ÚÙÛÜ'), ('n', 'ñ', 'Ñ')):
    kinds = {'á': 'acute', 'à': 'grave', 'â': 'circ', 'ä': 'diaer', 'ã': 'tilde'}
    order = ['acute', 'grave', 'circ', 'diaer', 'tilde'] if base in 'ao' else (['acute', 'grave', 'circ', 'diaer'] if base in 'eiu' else ['tilde'])
    for ch, k in zip(lo, order): COMBOS[ch] = ('ı' if base == 'i' else base, k, 0)
    for ch, k in zip(up, order): COMBOS[ch] = (base.upper(), k, 1)
COMBOS['ç'] = ('c', 'cedil', 0); COMBOS['Ç'] = ('C', 'cedil', 1)

# ------------------------------------------------------------------ dibujo
def sample(path, style):
    pts = []
    for it in path.p:
        if it[0] == 'arc':
            _, cx, cy, rx, ry, a0, a1 = it
            n = max(8, int(abs(a1 - a0) / 4))
            for k in range(n + 1):
                t = math.radians(a0 + (a1 - a0) * k / n)
                c, s = math.cos(t), math.sin(t)
                if style == 'tec':                       # superelipse: curvas cuadradas con esquinas suaves
                    e = 2.0 / 4.6
                    c = math.copysign(abs(c) ** e, c); s = math.copysign(abs(s) ** e, s)
                pts.append((cx + rx * c, cy + ry * s))
        else:
            pts.append(it)
    return pts

def stroke_geom(st, style, w, xs):
    cap = 1 if style == 'orb' else 2
    join = 1 if style == 'orb' else 2
    if isinstance(st, tuple):
        kind, x, y = st
        r = w * (0.62 if kind == 'dot' else 1.1)
        x *= xs
        if style == 'orb': return Point(x, y).buffer(r, resolution=12)
        return box(x - r * 0.9, y - r * 0.9, x + r * 0.9, y + r * 0.9)
    pts = [(x * xs, y) for x, y in sample(st, style)]
    clean = [pts[0]]
    for q in pts[1:]:
        if abs(q[0] - clean[-1][0]) > 0.01 or abs(q[1] - clean[-1][1]) > 0.01: clean.append(q)
    if len(clean) == 1: return Point(*clean[0]).buffer(w / 2, resolution=12)
    return LineString(clean).buffer(w / 2, cap_style=cap, join_style=join, mitre_limit=2.0, resolution=10)

def glyph_shape(ch, style, w, xs):
    if ch in COMBOS:
        base, mark, upper = COMBOS[ch]
        b = glyph_shape(base, style, w, xs)
        bb = b.bounds; cx = (bb[0] + bb[2]) / 2
        if mark == 'cedil':
            m = stroke_geom(Path(cx, 0).L(cx, -80).A(cx - 40, -120, 90, 60, 0, -150), style, w * 0.8, 1.0)
            return unary_union([b, m])
        y0 = (CAP + 70) if upper else (XH + 110)
        ms = [affinity.translate(stroke_geom(s, style, w * 0.85, 1.0), cx, y0) for s in MARKS[mark]]
        return unary_union([b] + ms)
    return unary_union([stroke_geom(s, style, w, xs) for s in G[ch]])

def to_glyph(shape, dx):
    pen = TTGlyphPen(None)
    geoms = shape.geoms if isinstance(shape, MultiPolygon) else [shape]
    for poly in geoms:
        if poly.is_empty: continue
        poly = orient(poly.simplify(1.2, preserve_topology=True), sign=-1.0)   # exterior en el sentido de las agujas (TrueType)
        for ring in [poly.exterior] + list(poly.interiors):
            pts = [(int(round(x + dx)), int(round(y))) for x, y in list(ring.coords)[:-1]]
            ded = [pts[0]]
            for q in pts[1:]:
                if q != ded[-1]: ded.append(q)
            if len(ded) < 3: continue
            pen.moveTo(ded[0])
            for q in ded[1:]: pen.lineTo(q)
            pen.closePath()
    return pen.glyph()

def build(style, family, wmain, xs):
    sb = 62 if style == 'orb' else 56
    chars = sorted((set(G) | set(COMBOS)) - {'ı'}, key=ord) + ['ı']
    order = ['.notdef', 'space', 'nbsp']; cmap = {32: 'space', 0xA0: 'nbsp'}
    glyphs = {}; metrics = {}
    e = TTGlyphPen(None); glyphs['.notdef'] = e.glyph(); metrics['.notdef'] = (500, 0)
    glyphs['space'] = TTGlyphPen(None).glyph(); metrics['space'] = (int(260 * (1 if style == 'orb' else 0.95)), 0)
    glyphs['nbsp'] = TTGlyphPen(None).glyph(); metrics['nbsp'] = metrics['space']
    digit_w = 0
    shapes = {}
    for ch in chars:
        shp = glyph_shape(ch, style, wmain, xs)
        shapes[ch] = shp
        if ch.isdigit(): digit_w = max(digit_w, shp.bounds[2] - shp.bounds[0])
    for ch in chars:
        shp = shapes[ch]
        x0, y0, x1, y1 = shp.bounds
        if ch.isdigit():
            adv = int(digit_w + 2 * sb); dx = (adv - (x1 - x0)) / 2 - x0
        else:
            rk = {'f': 0.15, 'r': 0.45, 't': 0.5, 'T': 0.6}.get(ch, 1.0)          # remates que vuelan hacia la derecha
            lk = {'j': -1.2, 'J': 0.4}.get(ch, 1.0)                                 # el gancho de la j se mete bajo la letra anterior
            adv = int((x1 - x0) + sb * lk + sb * rk); dx = sb * lk - x0
        name = 'u%04X' % ord(ch)
        glyphs[name] = to_glyph(shp, dx)
        metrics[name] = (adv, int(round(x0 + dx)))
        order.append(name); cmap[ord(ch)] = name
    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=930, descent=-250)
    fb.setupNameTable({'familyName': family, 'styleName': 'Regular', 'copyright': 'SOMA (dibujo original)', 'version': '1.0'})
    fb.setupOS2(sTypoAscender=930, sTypoDescender=-250, usWinAscent=930, usWinDescent=250, sxHeight=XH, sCapHeight=CAP)
    fb.setupPost()
    buf = io.BytesIO(); fb.save(buf)
    return buf.getvalue(), len(order)

def c_array(name, data):
    out = [f'const unsigned char {name}[] __attribute__((aligned(4))) = {{']
    for i in range(0, len(data), 20):
        out.append('    ' + ','.join(str(b) for b in data[i:i + 20]) + ',')
    out.append('};')
    out.append(f'const unsigned int {name}_len = {len(data)}u;')
    return '\n'.join(out)

if __name__ == '__main__':
    orb, n1 = build('orb', 'SOMA Orbital', 84, 1.0)
    tec, n2 = build('tec', 'SOMA Tecnica', 78, 0.86)
    if len(sys.argv) > 2:
        open(sys.argv[2] + '/soma_orbital.ttf', 'wb').write(orb); open(sys.argv[2] + '/soma_tecnica.ttf', 'wb').write(tec)
    src = ['/* SOMA_225_FUENTES: fuentes propias de SOMA (dibujo original, generadas por tools/mkfonts.py a partir de esqueletos de trazo).',
           ' * SOMA Orbital: sans geometrica de trazo uniforme. SOMA Tecnica: sans tecnica de curvas cuadradas. TrueType (glyf + cmap 4).',
           f' * {n1} y {n2} glifos: ASCII, letras del castellano y del resto de lenguas latinas comunes, comillas, guiones y euro.',
           ' * No editar a mano: se regenera con  python3 tools/mkfonts.py src/ui/soma_fonts.c  */',
           c_array('soma_ttf_orbital', orb), '', c_array('soma_ttf_tecnica', tec), '']
    open(sys.argv[1], 'w').write('\n'.join(src))
    print('orbital', len(orb), 'bytes', n1, 'glifos; tecnica', len(tec), 'bytes', n2, 'glifos')
