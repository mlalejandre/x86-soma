"""LCM · TRADUCTOR DE PETICIONES A ÓRDENES DE SOMA, entrenado (no reglas).

Una red pequeña lee la petición y da dos cosas a la vez:
  · qué orden es (una de las del catálogo, o «LLM» si no es una orden directa), con su probabilidad;
  · qué palabras son las piezas de la orden (archivo, destino, carpeta, número, red, clave, equipo, web, texto), marcadas palabra a palabra.
Con eso se arma la línea del terminal. Si la probabilidad no llega al umbral, o falta una pieza, NO se ejecuta nada: se cede al LLM
o se pregunta. Las órdenes peligrosas siempre piden confirmación.

  python3 -m tools.lcm_ordenes.traduce entrena          (unos minutos; deja tools/lcm_ordenes/traductor.pt)
  python3 -m tools.lcm_ordenes.traduce "borra el archivo notas.txt"
"""
from __future__ import annotations

import os
import random
import re
import sys

from .catalogo import I, VALORES
try:
    from .catalogo import CONTEXTOS, CIERRES
except ImportError:
    CONTEXTOS, CIERRES = [], []
try:
    from .catalogo import PROPOSITO
except ImportError:
    PROPOSITO = []
try:
    from .catalogo import EXTRA, ENVUELVE_INF, ENVUELVE_IND
except ImportError:
    EXTRA, ENVUELVE_INF, ENVUELVE_IND = {}, [], []

AQUI = os.path.dirname(os.path.abspath(__file__))
RUTA = os.environ.get("LCM_TRAD_RUTA") or os.path.join(AQUI, "traductor.pt")
UMBRAL = 0.70            # fijado antes de medir: por debajo no se ejecuta nada
SLOTS = ["A", "B", "D", "N", "R", "K", "H", "U", "T"] + (["P"] if "P" in VALORES else [])
TAGS = ["O"] + [p + s for s in SLOTS for p in ("B-", "I-")]
INTENTS = [x[0] for x in I]
ORDEN = {x[0]: x[1] for x in I}
PELIGROSA = {x[0]: bool(x[2]) for x in I}
_SIN_TILDE = str.maketrans("áéíóúÁÉÍÓÚ", "aeiouAEIOU")


_COLA = "?!,;:)»\"'."


def trocea(texto):
    """Palabras: se respetan rutas, archivos con extensión, direcciones web, IP y números.
    Los signos pegados al final de un archivo o una dirección («notas.txt?», «web.com,») van aparte."""
    out = []
    for t in _trocea0(texto):
        if len(t) > 1 and ("." in t or "/" in t) and t[-1] in _COLA:
            k = len(t)
            while k > 1 and t[k - 1] in _COLA:
                k -= 1
            out.append(t[:k]); out.extend(t[k:])
        else:
            out.append(t)
    return out


def _trocea0(texto):
    return re.findall(r"https?://\S+|www\.\S+|[\w/~\-]+(?:\.[\w\-]+)+/?\S*|/[\w/\.\-]+|\w+(?:-\w+)+|\d+(?:[.,]\d+)*|[\wáéíóúüñÁÉÍÓÚÜÑ]+|[^\s\w]", texto)


# ---------------------------------------------------------------- frases de entrenamiento
def _expande(frase, rnd):
    while True:
        m = re.search(r"\(([^()]*)\)", frase)
        if not m:
            break
        frase = frase[:m.start()] + rnd.choice(m.group(1).split("|")) + frase[m.end():]
    while True:
        m = re.search(r"\[([^\[\]]*)\]", frase)
        if not m:
            break
        op = m.group(1)
        frase = frase[:m.start()] + (rnd.choice(op.split("|")) if rnd.random() < 0.5 else "") + frase[m.end():]
    return re.sub(r"\s+", " ", frase).strip()


_UD = []


def _fragmentos():
    """Trozos de frases reales (UD, AnCora/GSD) para los textos libres: largos, variados y ajenos a los exámenes."""
    if not _UD:
        import glob
        raiz = os.environ.get("LCM_UD") or os.path.join(AQUI, "datos", "ud")
        for f in sorted(glob.glob(os.path.join(raiz, "**", "*train*.conllu"), recursive=True))[:2]:
            ws = []
            for ln in open(f, encoding="utf-8"):
                c = ln.split("\t")
                if len(c) > 3 and c[0].isdigit():
                    if c[3] != "PUNCT":
                        ws.append(c[1].lower())
                elif not ln.strip() and ws:
                    _UD.append(ws); ws = []
        if not _UD:
            _UD.append([])
    return _UD


def valor(s, rnd):
    if s == "X" and rnd.random() < 0.4:
        return rnd.choice(VALORES["X"]) + " " + valor("T", rnd)
    if s == "T" and rnd.random() < 0.7:
        fr = rnd.choice(_fragmentos())
        if len(fr) >= 2:
            n = min(len(fr), rnd.choice([1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8]))
            i = rnd.randrange(len(fr) - n + 1)
            return " ".join(fr[i:i + n])
    return rnd.choice(VALORES[s])


_ANTES = ["", "", "", "por favor", "oye", "puedes", "me puedes", "podrías", "quiero que", "necesito que", "venga", "hola", "soma", "Soma", "eh", "mira", "a ver", "oye soma"]
_DESPUES = ["", "", "", "por favor", "porfa", "gracias", "ya", "ahora", "rápido", "ahora mismo", "si puedes", "cuando puedas", "muchas gracias"]


def _ruido(ws, rnd):
    out = []
    for w in ws:
        if rnd.random() < 0.35:
            w = w.translate(_SIN_TILDE)
        if w.lower() in ("qué", "que") and rnd.random() < 0.15:
            w = "q"
        out.append(w)
    return out


_FR = []


def _frases_ud():
    """Frases completas de UD (entrenamiento): ejemplos reales de cosas que NO son órdenes."""
    if not _FR:
        import glob
        raiz = os.environ.get("LCM_UD") or os.path.join(AQUI, "datos", "ud")
        for f in sorted(glob.glob(os.path.join(raiz, "**", "*train*.conllu"), recursive=True)):
            for ln in open(f, encoding="utf-8"):
                if ln.startswith("# text = "):
                    t = ln[9:].strip()
                    if 2 <= len(t.split()) <= 30:
                        _FR.append(t)
        if not _FR:
            _FR.append("")
    return _FR


def _errata(w, rnd):
    j = rnd.randrange(1, len(w) - 1)
    op = rnd.random()
    if op < 0.3:
        return w[:j] + w[j + 1] + w[j] + w[j + 2:] if j + 1 < len(w) else w
    if op < 0.6:
        return w[:j] + w[j + 1:]
    if op < 0.8:
        return w[:j] + w[j] + w[j:]
    return w[:j] + rnd.choice("aeiourstnlcdmp") + w[j + 1:]


_P_UD = float(os.environ.get("LCM_P_UD", 0.12))      # frases reales que no son órdenes
_P_LLM = float(os.environ.get("LCM_P_LLM", 0.12))    # plantillas de no-órdenes
_P_ERR = float(os.environ.get("LCM_P_ERR", 0.0))     # erratas por palabra (la ablación mostró que empeoran: 0)


def ejemplo(rnd):
    if rnd.random() < _P_UD:                      # frase real cualquiera: no es una orden
        fr = rnd.choice(_frases_ud())
        if fr:
            toks = trocea(fr)
            toks = [(_errata(w, rnd) if len(w) >= 4 and w.isalpha() and rnd.random() < 0.05 else w) for w in toks]
            return _ruido(toks, rnd), "LLM", ["O"] * len(toks)
    if rnd.random() < _P_LLM:
        x = next(x for x in I if x[0] == "LLM")
    else:
        x = rnd.choice(I)
    it, orden, _, frases = x
    f = _expande(rnd.choice(frases), rnd)
    ex = EXTRA.get(it)
    if ex and rnd.random() < 0.45:                       # formulas indirectas: "¿serías tan amable de...?", "¿sabrías decirme...?"
        inf, ind = ex
        if ind and (not inf or rnd.random() < 0.5):
            f = rnd.choice(ENVUELVE_IND).replace("{}", _expande(rnd.choice(ind), rnd))
        elif inf:
            f = rnd.choice(ENVUELVE_INF).replace("{}", _expande(rnd.choice(inf), rnd))
    toks, tags = [], []
    antes = rnd.choice(CONTEXTOS) if (CONTEXTOS and rnd.random() < 0.18) else rnd.choice(_ANTES)
    for w in trocea(antes):
        toks.append(w); tags.append("O")
    if antes and not antes.endswith(",") and not antes.endswith(":") and rnd.random() < 0.3:
        toks.append(","); tags.append("O")
    for parte in re.split(r"(\{[A-Z]\})", f):
        m = re.fullmatch(r"\{([A-Z])\}", parte)
        if m:
            s = m.group(1)
            ws = trocea(valor(s, rnd))
            for k, w in enumerate(ws):
                toks.append(w); tags.append(("O" if it == "LLM" else ("B-" if k == 0 else "I-") + s))
        else:
            for w in trocea(parte):
                if len(w) >= 4 and w.isalpha() and rnd.random() < _P_ERR:
                    w = _errata(w, rnd)
                toks.append(w); tags.append("O")
    if PROPOSITO and it != "LLM" and "{" not in orden and rnd.random() < 0.15:     # "... que quiero cambiar la transparencia"
        fr = rnd.choice(_frases_ud())
        for w in trocea(rnd.choice(PROPOSITO) + " " + " ".join(fr.split()[:rnd.randint(2, 7)]).lower()):
            toks.append(w); tags.append("O")
    despues = rnd.choice(CIERRES) if (CIERRES and rnd.random() < 0.15) else rnd.choice(_DESPUES)
    if despues and rnd.random() < 0.3:
        toks.append(","); tags.append("O")
    for w in trocea(despues):
        toks.append(w); tags.append("O")
    if rnd.random() < 0.3:
        toks.append(rnd.choice(["?", ".", "!"])); tags.append("O")
    if rnd.random() < 0.5 and toks:
        toks[0] = toks[0].lower()
    toks = _ruido(toks, rnd)
    return toks, it, tags


# ---------------------------------------------------------------- red
def _red():
    import torch
    import torch.nn as nn
    from .hash import _hs

    class Traductor(nn.Module):
        NB = 6000

        def __init__(self, nw, ni, nt, dw=64, h=128):
            super().__init__()
            self.ew = nn.Embedding(nw + 2, dw, padding_idx=0)
            self.eb = nn.ModuleList([nn.Embedding(self.NB, 24, padding_idx=0) for _ in range(4)])
            self.lstm = nn.LSTM(dw + 96 + 4, h, num_layers=1, bidirectional=True, batch_first=True)
            self.drop = nn.Dropout(0.25)
            self.intent = nn.Linear(4 * h, ni)
            self.tag = nn.Linear(2 * h, nt)

        def forward(self, X, F):
            e = torch.cat([self.ew(X[..., 0])] + [self.eb[i](X[..., i + 1]) for i in range(4)] + [F], -1)
            o, _ = self.lstm(self.drop(e))
            o = self.drop(o)
            mask = (X[..., 0] > 0).unsqueeze(-1)
            mx = o.masked_fill(~mask, -1e9).max(1).values
            me = (o * mask).sum(1) / mask.sum(1).clamp(min=1)
            return self.intent(torch.cat([mx, me], -1)), self.tag(o)

    def rasgos(w, voc):
        f = w.lower().translate(_SIN_TILDE)
        clase = "0" if f[:1].isdigit() else f
        wid = voc.get(clase, 1)
        b = [_hs("s3" + f[-3:], Traductor.NB), _hs("s4" + f[-4:], Traductor.NB), _hs("p3" + f[:3], Traductor.NB), _hs("p4" + f[:4], Traductor.NB)]
        g = [float("." in w and not w.endswith(".")), float("/" in w), float(any(c.isdigit() for c in w)), float(w[:1].isupper())]
        return [wid] + b, g

    return torch, Traductor, rasgos


def _lote(ejs, voc, rasgos, torch):
    n = max(len(t) for t, _, _ in ejs)
    X = torch.zeros(len(ejs), n, 5, dtype=torch.long)
    F = torch.zeros(len(ejs), n, 4)
    for k, (toks, _, _) in enumerate(ejs):
        for j, w in enumerate(toks):
            a, g = rasgos(w, voc)
            X[k, j] = torch.tensor(a); F[k, j] = torch.tensor(g)
    return X, F


def entrena(n=120000, epocas=8, semilla=11, log=print):
    torch, Traductor, rasgos = _red()
    import torch.nn.functional as Fn
    rnd = random.Random(semilla); torch.manual_seed(semilla)
    datos = [ejemplo(rnd) for _ in range(n)]
    from collections import Counter
    c = Counter(w.lower().translate(_SIN_TILDE) for t, _, _ in datos for w in t)
    voc = {w: i + 2 for i, w in enumerate(w for w, k in c.items() if k >= 3 and not w[:1].isdigit())}
    m = Traductor(len(voc) + 2, len(INTENTS), len(TAGS))
    opt = torch.optim.Adam(m.parameters(), lr=2e-3)
    i2 = {x: i for i, x in enumerate(INTENTS)}; t2 = {x: i for i, x in enumerate(TAGS)}
    for ep in range(epocas):
        m.train(); rnd.shuffle(datos); tot = 0.0
        for b in range(0, len(datos), 64):
            ejs = datos[b:b + 64]
            X, F = _lote(ejs, voc, rasgos, torch)
            # palabras poco vistas pasan a «desconocida» a veces: así aprende a fijarse en la forma (archivo.txt, /ruta, 192.168…)
            X[..., 0] = torch.where((torch.rand(X[..., 0].shape) < 0.12) & (X[..., 0] > 1), torch.ones_like(X[..., 0]), X[..., 0])
            yi = torch.tensor([i2[it] for _, it, _ in ejs])
            yt = torch.full(X.shape[:2], -100, dtype=torch.long)
            for k, (_, _, tg) in enumerate(ejs):
                yt[k, :len(tg)] = torch.tensor([t2[x] for x in tg])
            li, lt = m(X, F)
            loss = Fn.cross_entropy(li, yi) + Fn.cross_entropy(lt.reshape(-1, len(TAGS)), yt.reshape(-1), ignore_index=-100)
            opt.zero_grad(); loss.backward(); opt.step(); tot += float(loss.detach())
        if log:
            log(f"pasada {ep + 1}: pérdida {tot / (len(datos) / 64):.3f}")
    torch.save({"modelo": m.state_dict(), "voc": voc, "intents": INTENTS, "tags": TAGS}, RUTA)
    _C.clear()
    return m


_C = {}


def _carga():
    if "m" not in _C:
        torch, Traductor, rasgos = _red()
        mods = []
        for ruta in RUTA.split(":"):                       # varias rutas = conjunto: se promedian las probabilidades
            ck = torch.load(ruta, map_location="cpu")
            m = Traductor(len(ck["voc"]) + 2, len(ck["intents"]), len(ck["tags"]))
            m.load_state_dict(ck["modelo"]); m.eval()
            mods.append((m, ck["voc"]))
        _C.update(m=mods[0][0], voc=mods[0][1], mods=mods, torch=torch, rasgos=rasgos, intents=ck["intents"], tags=ck["tags"])
    return _C


# palabras que no pueden ser por si solas un dato (como en src/agent/lcm_ordenes.inc)
VACIAS = set("""de del la el los las un una unos unas a al en con por para que q se y o lo le me mi mis tu su es esta este eso esto ahi aqui alli
carpeta directorio archivo fichero documento red wifi clave contrasena password llamada llamado llame nombre nueva nuevo abierta abierto tiene borra
llamalo llamala como sitio algo todo nada favor porfa ya ahora""".split())
APPS = ("navegador", "internet", "browser", "web", "chrome", "firefox", "terminal", "consola", "shell", "archivos", "explorador", "carpetas", "ficheros",
        "imagen", "visor", "foto", "notas", "bloc", "editor", "notepad", "codigo", "studio", "coder", "programador", "memoria", "agentes", "maquetador",
        "latex", "paint", "dibujo", "pintar", "media", "reproductor", "video", "musica", "pelicula", "ajustes", "configuracion", "preferencias", "pdf",
        "lector", "monitor", "rendimiento", "grabadora", "microfono", "camara", "webcam", "captura", "youtube", "chat", "agente", "programa", "app")


EXT_IMAGEN = ("png", "jpg", "jpeg", "gif", "bmp", "webp", "ppm", "svg")


def _pl(w):
    return w.lower().translate(_SIN_TILDE)


def _forma(sl, w):
    if sl == "A":
        return bool(re.search(r"[^.\s]\.[^.\s]", w)) or w.startswith("/")
    if sl == "H":
        return "." in w or any(c.isdigit() for c in w)
    if sl == "U":
        return "." in w or "://" in w
    return True


def _vale(sl, ws):
    if all(_pl(w) in VACIAS or not re.search(r"\w", w) for w in ws):
        return False
    if sl == "P":
        return any(a in _pl(" ".join(ws)) for a in APPS)
    return True


CORTESIA = [["por", "favor"], ["porfa"], ["xfa"], ["plis"], ["gracias"], ["muchas", "gracias"], ["ya"], ["ahora"], ["ahora", "mismo"], ["anda"], ["venga"],
            ["si", "puedes"], ["cuando", "puedas"], ["rapido"], ["de", "una", "vez"], ["si", "no", "es", "molestia"], ["porfavor"]]


def _cortesia_final(toks):
    """Cuantas palabras del final de la peticion son cortesia ("... por favor", "... ya"), saltando signos."""
    ws = [_pl(w) for w in toks]
    n = len(ws)
    while n and not re.search(r"\w", ws[n - 1]):
        n -= 1
    quitadas = 0
    while True:
        for c in sorted(CORTESIA, key=len, reverse=True):
            if n - len(c) >= 0 and ws[n - len(c):n] == c:
                n -= len(c); quitadas = len(toks) - n
                while n and not re.search(r"\w", ws[n - 1]):
                    n -= 1
                break
        else:
            break
    return len(toks) - quitadas if quitadas else len(toks)


def _limpia(ws):
    """Fuera los signos sueltos al principio y al final de una pieza («: la cena está lista», «curriculum ?»)."""
    while ws and re.fullmatch(r"[^\w\s]", ws[0]):
        ws = ws[1:]
    while ws and re.fullmatch(r"[^\w\s]", ws[-1]):
        ws = ws[:-1]
    return ws


_UNI = {"cero": 0, "uno": 1, "un": 1, "una": 1, "dos": 2, "tres": 3, "cuatro": 4, "cinco": 5, "seis": 6, "siete": 7, "ocho": 8, "nueve": 9}
_NUM = dict(_UNI, **{"diez": 10, "once": 11, "doce": 12, "trece": 13, "catorce": 14, "quince": 15, "dieciseis": 16, "diecisiete": 17, "dieciocho": 18,
             "diecinueve": 19, "veinte": 20, "veintiuno": 21, "veintiun": 21, "veintidos": 22, "veintitres": 23, "veinticuatro": 24, "veinticinco": 25,
             "veintiseis": 26, "veintisiete": 27, "veintiocho": 28, "veintinueve": 29, "cien": 100, "ciento": 100})
_DEC = {"treinta": 30, "cuarenta": 40, "cincuenta": 50, "sesenta": 60, "setenta": 70, "ochenta": 80, "noventa": 90}


def numeros_en_cifras(texto):
    """'pon el brillo al cuarenta y cinco' -> 'pon el brillo al 45'. Solo se usa si la orden pide un numero y no lo encuentra.
    'un', 'una' y 'uno' sueltos no se tocan (son articulos): solo detras de una decena ('treinta y uno')."""
    ws = texto.split(" "); out = []; i = 0
    while i < len(ws):
        f = ws[i].lower().translate(_SIN_TILDE).strip(",.?!¿¡")
        if f in _DEC:
            v = _DEC[f]
            if i + 2 < len(ws) and ws[i + 1].lower() == "y" and ws[i + 2].lower().translate(_SIN_TILDE).strip(",.?!¿¡") in _UNI:
                v += _UNI[ws[i + 2].lower().translate(_SIN_TILDE).strip(",.?!¿¡")]; i += 2
            out.append(str(v))
        elif f in _NUM and f not in ("un", "una", "uno"):
            out.append(str(_NUM[f]))
        else:
            out.append(ws[i])
        i += 1
    return " ".join(out)


def traduce(peticion, umbral=None, _segunda=False):
    """{"orden": str|None, "confirmar": bool, "p": prob, "intent": id, "piezas": {...}, "motivo": str}"""
    umbral = UMBRAL if umbral is None else umbral
    C = _carga()
    toks = trocea(peticion)
    if not toks:
        return {"orden": None, "confirmar": False, "p": 0.0, "intent": "LLM", "piezas": {}, "motivo": "vacía"}
    torch = C["torch"]
    with torch.no_grad():
        pi = 0; pt = 0
        for m, voc in C["mods"]:
            X, F = _lote([(toks, None, None)], voc, C["rasgos"], torch)
            li, lt = m(X, F)
            pi = pi + li.softmax(-1)[0] / len(C["mods"])
            pt = pt + lt[0].softmax(-1) / len(C["mods"])
        k = int(pi.argmax()); p = float(pi[k])
        tg = [C["tags"][int(x)] for x in pt.argmax(-1)]
        # una pieza de número es un número: si la red marca otra palabra, se toma el número más probable (si lo hay)
        ib, ii = C["tags"].index("B-N"), C["tags"].index("I-N")
        if any(t.endswith("-N") for t in tg):
            tg = ["O" if t.endswith("-N") else t for t in tg]
            nums = [j for j, w in enumerate(toks) if re.fullmatch(r"\d+", w)]
            if nums:
                j = max(nums, key=lambda j: float(pt[j, ib] + pt[j, ii]))
                tg[j] = "B-N"
        # cada pieza con forma tiene esa forma (archivo con extension o ruta, equipo con punto o cifras, web con punto): si la red
        # marca otra palabra, se toma la palabra con esa forma mas probable para esa pieza; si no hay ninguna, falta y se pregunta
        for sl in ("A", "H", "U"):
            if ("B-" + sl) not in C["tags"]:
                continue
            marcadas = [j for j, t in enumerate(tg) if t.endswith("-" + sl)]
            if not marcadas or _forma(sl, toks[marcadas[0]]):
                continue
            ib2, ii2 = C["tags"].index("B-" + sl), C["tags"].index("I-" + sl)
            tg = ["O" if t.endswith("-" + sl) else t for t in tg]
            cand = [j for j, w in enumerate(toks) if tg[j] == "O" and _forma(sl, w)]
            if cand:
                j = max(cand, key=lambda j: float(pt[j, ib2] + pt[j, ii2]))
                tg[j] = "B-" + sl
    it = C["intents"][k]
    piezas = {}
    actual = None
    for j, t in enumerate(tg):
        if t.startswith("B-") or (t.startswith("I-") and actual != t[2:]):
            actual = t[2:]
            piezas.setdefault(actual, []).append([j])
        elif t.startswith("I-") and actual == t[2:]:
            piezas[actual][-1].append(j)
        else:
            actual = None
    corte = _cortesia_final(toks)          # si la pieza llega a la cortesia del final ("... por favor"), se corta ahi
    p2 = {}
    for sl, tramos in piezas.items():
        elegido = None
        for v in tramos:                     # el primer tramo que sea un dato de verdad (no "llamada", "abierta", "la"...)
            w = _limpia([toks[j] for j in v if j < corte])
            if w and _vale(sl, w):
                elegido = w; break
        if elegido is None and sl != "T":
            # "wifi abierta Cafe_Sol": si la pieza es solo una palabra vacia, el dato suele ser la siguiente
            idx = [j for j in tramos[0] if j < corte]
            if idx:
                j = idx[-1] + 1
                if j < corte and tg[j] == "O" and re.search(r"\w", toks[j]) and _pl(toks[j]) not in VACIAS and _forma(sl, toks[j]):
                    elegido = [toks[j]]
        if elegido:
            p2[sl] = " ".join(elegido)
    piezas = p2
    r = {"orden": None, "confirmar": False, "p": p, "intent": it, "piezas": piezas}
    if it == "LLM":
        r["motivo"] = "no es una orden directa: al LLM"
        return r
    if p < umbral:
        r["motivo"] = f"no estoy seguro ({p:.2f}): al LLM"
        return r
    faltan = [s for s in re.findall(r"\{([A-Z])\}", ORDEN[it]) if not piezas.get(s)]
    if "N" in faltan and not _segunda:
        otra = numeros_en_cifras(peticion)
        if otra != peticion:
            return traduce(otra, umbral, _segunda=True)
    if faltan:
        r["motivo"] = "falta " + ", ".join(faltan) + ": hay que preguntar"
        return r
    if any(re.search(r'[;|&<>`$"\\]', piezas[s]) for s in re.findall(r"\{([A-Z])\}", ORDEN[it])):
        r["motivo"] = "una pieza trae caracteres de shell: hay que preguntar (caracteres)"
        return r
    orden = ORDEN[it]
    for s in re.findall(r"\{([A-Z])\}", orden):
        v = piezas[s].replace(" .", ".").replace(" /", "/")
        libre = (s == "T" and (orden.endswith("{T}") or '"{T}"' in orden)) or (s == "P" and orden.endswith("{P}"))   # texto final o entre comillas, o la app al final
        if " " in v and not libre:
            r["motivo"] = "una pieza con espacios donde la orden no los admite: hay que preguntar"
            return r
        orden = orden.replace("{" + s + "}", v)
    if it in ("cat", "pdf", "imagen") and piezas.get("A"):          # el tipo de archivo decide con que se abre
        ext = piezas["A"].rsplit(".", 1)[-1].lower() if "." in piezas["A"] else ""
        nuevo = "pdf" if ext == "pdf" else "imagen" if ext in EXT_IMAGEN else "cat"
        if nuevo != it and "pdf" in ORDEN and "imagen" in ORDEN:
            it = nuevo; r["intent"] = it
            orden = ORDEN[it].replace("{A}", piezas["A"].replace(" .", ".").replace(" /", "/"))
    r["orden"] = orden
    r["confirmar"] = PELIGROSA[it]
    r["motivo"] = "ok"
    return r


if __name__ == "__main__":
    if sys.argv[1:] == ["entrena"]:
        entrena()
    else:
        print(traduce(" ".join(sys.argv[1:])))
