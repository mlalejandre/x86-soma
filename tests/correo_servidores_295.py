#!/usr/bin/env python3
# SOMA_295_CORREO: servidores IMAP4rev1 y SMTP de prueba, con TLS 1.3 de verdad (directo y STARTTLS), para tests/test_correo_red_295.py.
# Cubren lo que usa el cliente de SOMA y se portan como los servidores reales en lo que importa:
#   IMAP en "directo": AUTH=PLAIN SASL-IR, MOVE, UIDPLUS.  IMAP con STARTTLS: LOGINDISABLED antes de cifrar, despues solo LOGIN
#   (con la contrasena en un literal, porque lleva una enie) y sin MOVE ni UIDPLUS (el cliente tiene que copiar y borrar).
#   SMTP en 465 (TLS directo, AUTH PLAIN y LOGIN) y en 587 (STARTTLS, despues solo AUTH LOGIN). Rechaza "rechaza@..." con 550.
import base64, email, email.header, email.policy, re, socket, socketserver, ssl, threading, time

USUARIO = "pepe@prueba.es"
CLAVE = "cl@ve \"con\" \u00f1"          # comillas y una enie: obliga a codificar bien (literal en LOGIN, UTF-8 en PLAIN)

def _cab(raw, nombre):
    m = email.message_from_bytes(raw, policy=email.policy.SMTPUTF8)     # cabeceras en UTF-8 de 8 bits (RFC 6532) incluidas
    v = m.get(nombre, "")
    try:
        return str(email.header.make_header(email.header.decode_header(v)))
    except Exception:
        return str(v)

class Msg:
    def __init__(self, uid, raw, flags=()):
        self.uid, self.raw, self.flags = uid, raw, set(flags)

class Buzon:
    def __init__(self, nombre, uso=None):
        self.nombre, self.uso, self.uidvalidity, self.uidnext, self.msgs = nombre, uso, 4242, 1, []
    def mete(self, raw, flags=()):
        m = Msg(self.uidnext, raw, flags); self.uidnext += 1; self.msgs.append(m); return m

class Estado:
    def __init__(self, mensajes):
        self.lock = threading.Lock()
        self.buzones = {b.nombre: b for b in [Buzon("INBOX"), Buzon("Enviados", "\\Sent"), Buzon("Borradores", "\\Drafts"),
                                               Buzon("Papelera", "\\Trash"), Buzon("Facturas/2026 &AOk-t&AOk-"), Buzon('Con "comillas"')]}
        for raw, fl in mensajes:
            self.buzones["INBOX"].mete(raw, fl)
        self.ordenes = []                 # lo que ha pedido el cliente (sin contrasenas)
        self.smtp = []                    # mensajes recibidos por SMTP: {de, para, datos, puerto, auth}

# ---------------------------------------------------------------- IMAP
def _tokens(s):
    """argumentos IMAP: atomos, "cadenas", (listas) y literales ya leidos (bytes)."""
    out, i = [], 0
    def uno(i):
        while i < len(s) and s[i] == " ": i += 1
        if i >= len(s): return None, i
        c = s[i]
        if isinstance(c, bytes): return c, i + 1
        if c == '"':
            j, v = i + 1, ""
            while j < len(s) and s[j] != '"':
                if s[j] == "\\": j += 1
                v += s[j]; j += 1
            return v, j + 1
        if c == "(":
            lst, j = [], i + 1
            while True:
                while j < len(s) and s[j] == " ": j += 1
                if j >= len(s) or s[j] == ")": return lst, j + 1
                t, j = uno(j); lst.append(t)
        j, prof = i, 0
        while j < len(s) and not isinstance(s[j], bytes) and (prof or (s[j] not in " )")):
            if s[j] == "[": prof += 1
            if s[j] == "]": prof -= 1
            if s[j] == "(" and not prof: break
            j += 1
        return "".join(s[i:j]), j
    while True:
        t, i = uno(i)
        if t is None: break
        out.append(t)
    return out

def _conjunto(spec, maximo):
    r = set()
    for parte in spec.split(","):
        a, _, b = parte.partition(":")
        a = maximo if a == "*" else int(a)
        b = a if not b else (maximo if b == "*" else int(b))
        if a > b: a, b = b, a
        r.update(range(a, b + 1))
    return r

FL = {"\\seen": "\\Seen", "\\flagged": "\\Flagged", "\\answered": "\\Answered", "\\deleted": "\\Deleted", "\\draft": "\\Draft"}

class ManejadorIMAP(socketserver.BaseRequestHandler):
    directo = True
    def setup(self):
        self.est = self.server.est
        self.sock = self.request
        if self.directo:
            self.sock = self.server.ctx.wrap_socket(self.sock, server_side=True)
        self.tls = self.directo
        self.dentro, self.sel, self.buf = False, None, b""
    def envia(self, b):
        self.sock.sendall(b if isinstance(b, bytes) else b.encode())
    def linea(self):
        while b"\r\n" not in self.buf:
            d = self.sock.recv(65536)
            if not d: raise EOFError
            self.buf += d
        l, _, self.buf = self.buf.partition(b"\r\n")
        return l
    def exacto(self, n):
        while len(self.buf) < n:
            d = self.sock.recv(65536)
            if not d: raise EOFError
            self.buf += d
        d, self.buf = self.buf[:n], self.buf[n:]
        return d
    def orden(self):
        """una orden entera (con sus literales): lista de caracteres y bytes"""
        partes = []
        while True:
            l = self.linea()
            m = re.search(rb"\{(\d+)(\+?)\}$", l)
            if not m:
                partes += list(l.decode("utf-8", "replace")); return partes
            partes += list(l[:m.start()].decode("utf-8", "replace"))
            if not m.group(2): self.envia("+ Adelante\r\n")
            partes.append(self.exacto(int(m.group(1))))
    def caps(self):
        if not self.directo and not self.tls: return "IMAP4rev1 STARTTLS LOGINDISABLED"
        if self.directo: return "IMAP4rev1 AUTH=PLAIN SASL-IR MOVE UIDPLUS LITERAL+"
        return "IMAP4rev1"
    def handle(self):
        try:
            self.envia("* OK [CAPABILITY %s] Servidor IMAP de prueba listo\r\n" % self.caps() if self.directo else "* OK Servidor IMAP de prueba listo\r\n")
            while True:
                o = self.orden()
                txt = "".join(c if isinstance(c, str) else "{lit}" for c in o)
                tag, _, resto = txt.partition(" ")
                toks = _tokens(o[len(tag) + 1:])
                cmd = (toks[0] if toks else "").upper()
                args = toks[1:]
                if cmd not in ("LOGIN", "AUTHENTICATE"):
                    with self.est.lock: self.est.ordenes.append(("tls" if self.tls else "claro") + " " + txt)
                else:
                    with self.est.lock: self.est.ordenes.append(("tls" if self.tls else "claro") + " " + tag + " " + cmd)
                uid = False
                if cmd == "UID": uid, cmd, args = True, args[0].upper(), args[1:]
                f = getattr(self, "c_" + cmd.lower(), None)
                if f is None: self.envia("%s BAD orden desconocida\r\n" % tag); continue
                if cmd not in ("CAPABILITY", "NOOP", "LOGOUT", "STARTTLS", "LOGIN", "AUTHENTICATE") and not self.dentro:
                    self.envia("%s NO primero hay que entrar\r\n" % tag); continue
                if f(tag, args, uid) == "fin": return
        except (EOFError, ConnectionError, ssl.SSLError, OSError):
            return
    # --- ordenes
    def c_capability(self, tag, a, u):
        self.envia("* CAPABILITY %s\r\n%s OK CAPABILITY hecho\r\n" % (self.caps(), tag))
    def c_noop(self, tag, a, u): self.envia("%s OK\r\n" % tag)
    def c_logout(self, tag, a, u):
        self.envia("* BYE hasta luego\r\n%s OK LOGOUT hecho\r\n" % tag); return "fin"
    def c_starttls(self, tag, a, u):
        if self.tls or self.directo: self.envia("%s BAD ya cifrado\r\n" % tag); return
        self.envia("%s OK Empieza TLS\r\n" % tag)
        self.sock = self.server.ctx.wrap_socket(self.sock, server_side=True); self.tls = True; self.buf = b""
    def entra(self, tag, u, k):
        if not self.tls: self.envia("%s NO [PRIVACYREQUIRED] sin cifrar no\r\n" % tag); return
        if u == USUARIO and k == CLAVE:
            self.dentro = True; self.envia("%s OK [CAPABILITY %s] Dentro\r\n" % (tag, self.caps()))
        else: self.envia("%s NO [AUTHENTICATIONFAILED] Invalid credentials (Failure)\r\n" % tag)
    def c_login(self, tag, a, u):
        if not self.directo and not self.tls: self.envia("%s NO LOGINDISABLED\r\n" % tag); return
        us = a[0].decode() if isinstance(a[0], bytes) else a[0]
        k = a[1].decode("utf-8") if isinstance(a[1], bytes) else a[1]
        self.entra(tag, us, k)
    def c_authenticate(self, tag, a, u):
        if "AUTH=PLAIN" not in self.caps(): self.envia("%s NO no hay AUTHENTICATE\r\n" % tag); return
        if len(a) > 1: datos = a[1]
        else:
            self.envia("+ \r\n"); datos = self.linea().decode()
        try: _, us, k = base64.b64decode(datos).decode("utf-8").split("\0")
        except Exception: self.envia("%s BAD base64\r\n" % tag); return
        self.entra(tag, us, k)
    def c_list(self, tag, a, u):
        with self.est.lock:
            for b in self.est.buzones.values():
                fl = "\\HasNoChildren" + (" " + b.uso if b.uso else "")
                nombre = b.nombre.replace("\\", "\\\\").replace('"', '\\"')
                self.envia('* LIST (%s) "/" "%s"\r\n' % (fl, nombre))
            self.envia('* LIST (\\Noselect \\HasChildren) "/" "Facturas"\r\n')
        self.envia("%s OK LIST hecho\r\n" % tag)
    def c_status(self, tag, a, u):
        with self.est.lock:
            b = self.est.buzones.get(a[0])
            if not b: self.envia("%s NO no existe\r\n" % tag); return
            nl = sum(1 for m in b.msgs if "\\Seen" not in m.flags)
            self.envia('* STATUS "%s" (MESSAGES %d UNSEEN %d)\r\n%s OK STATUS hecho\r\n' % (a[0], len(b.msgs), nl, tag))
    def c_select(self, tag, a, u, rw=True):
        with self.est.lock:
            b = self.est.buzones.get(a[0])
            if not b: self.envia("%s NO [NONEXISTENT] no existe\r\n" % tag); return
            self.sel = b
            self.envia("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n* %d EXISTS\r\n* 0 RECENT\r\n* OK [UIDVALIDITY %d] UIDs\r\n* OK [UIDNEXT %d] Predicho\r\n%s OK [%s] SELECT hecho\r\n"
                       % (len(b.msgs), b.uidvalidity, b.uidnext, tag, "READ-WRITE" if rw else "READ-ONLY"))
    def c_examine(self, tag, a, u): self.c_select(tag, a, u, False)
    def elegidos(self, spec, uid):
        b = self.sel
        if uid:
            mx = b.msgs[-1].uid if b.msgs else 0
            s = _conjunto(spec, mx)
            if b.msgs and spec.endswith("*"): s.add(mx)
            return [(i + 1, m) for i, m in enumerate(b.msgs) if m.uid in s]
        s = _conjunto(spec, len(b.msgs))
        return [(i + 1, m) for i, m in enumerate(b.msgs) if (i + 1) in s]
    def c_fetch(self, tag, a, u):
        items = a[1] if isinstance(a[1], list) else [a[1]]
        with self.est.lock:
            for seq, m in self.elegidos(a[0], u):
                partes, lits = [], []
                if u: partes.append("UID %d" % m.uid)
                cab, _, cuerpo = m.raw.partition(b"\r\n\r\n"); cab += b"\r\n\r\n"
                pone_visto = False
                for it in items:
                    up = it.upper() if isinstance(it, str) else ""
                    if up == "UID" and not u: partes.append("UID %d" % m.uid)
                    elif up == "FLAGS": partes.append("FLAGS (%s)" % " ".join(sorted(m.flags)))
                    elif up == "RFC822.SIZE": partes.append("RFC822.SIZE %d" % len(m.raw))
                    elif up.startswith("BODY"):
                        if not up.startswith("BODY.PEEK"): pone_visto = True
                        sec = re.search(r"\[(.*)\]", it).group(1)
                        rango = re.search(r"<(\d+)\.(\d+)>$", it)
                        if sec.upper().startswith("HEADER.FIELDS"):
                            pedidas = [x.lower() for x in re.search(r"\((.*)\)", sec).group(1).split()]
                            lineas, act = [], False
                            for l in cab.split(b"\r\n"):
                                if l[:1] in (b" ", b"\t"):
                                    if act: lineas.append(l)
                                    continue
                                act = l.split(b":")[0].decode("latin-1").lower() in pedidas
                                if act: lineas.append(l)
                            dato = b"\r\n".join(lineas) + b"\r\n\r\n"
                            nombre = "BODY[%s]" % sec.upper()
                        elif sec.upper() == "TEXT": dato, nombre = cuerpo, "BODY[TEXT]"
                        elif sec == "": dato, nombre = m.raw, "BODY[]"
                        else: continue
                        if rango:
                            a0, n0 = int(rango.group(1)), int(rango.group(2)); dato = dato[a0:a0 + n0]; nombre += "<%d>" % a0
                        partes.append(nombre + " {%d}" % len(dato)); lits.append(dato)
                if pone_visto: m.flags.add("\\Seen")
                # el literal va justo despues de su nombre
                out = b"* %d FETCH (" % seq
                li = 0
                for i, p in enumerate(partes):
                    if i: out += b" "
                    out += p.encode()
                    if p.endswith("}"): out += b"\r\n" + lits[li]; li += 1
                self.envia(out + b")\r\n")
        self.envia("%s OK FETCH hecho\r\n" % tag)
    def c_store(self, tag, a, u):
        modo, fl = a[1].upper(), [FL.get(x.lower(), x) for x in (a[2] if isinstance(a[2], list) else [a[2]])]
        with self.est.lock:
            for seq, m in self.elegidos(a[0], u):
                if modo.startswith("+"): m.flags.update(fl)
                elif modo.startswith("-"): m.flags.difference_update(fl)
                else: m.flags = set(fl)
                if not modo.endswith(".SILENT"): self.envia("* %d FETCH (UID %d FLAGS (%s))\r\n" % (seq, m.uid, " ".join(sorted(m.flags))))
        self.envia("%s OK STORE hecho\r\n" % tag)
    def c_copy(self, tag, a, u, mover=False):
        with self.est.lock:
            d = self.est.buzones.get(a[1])
            if not d: self.envia("%s NO [TRYCREATE] no existe\r\n" % tag); return
            el = self.elegidos(a[0], u)
            for seq, m in el: d.mete(m.raw, m.flags - {"\\Deleted"})
            if mover:
                for seq, m in sorted(el, key=lambda x: -x[0]):
                    self.sel.msgs.remove(m); self.envia("* %d EXPUNGE\r\n" % seq)
        self.envia("%s OK %s hecho\r\n" % (tag, "MOVE" if mover else "COPY"))
    def c_move(self, tag, a, u):
        if "MOVE" not in self.caps(): self.envia("%s BAD no hay MOVE\r\n" % tag); return
        self.c_copy(tag, a, u, True)
    def c_expunge(self, tag, a, u):
        if u and "UIDPLUS" not in self.caps(): self.envia("%s BAD no hay UIDPLUS\r\n" % tag); return
        with self.est.lock:
            uids = _conjunto(a[0], 10 ** 9) if u else None
            i = 0
            while i < len(self.sel.msgs):
                m = self.sel.msgs[i]
                if "\\Deleted" in m.flags and (uids is None or m.uid in uids):
                    self.sel.msgs.pop(i); self.envia("* %d EXPUNGE\r\n" % (i + 1))
                else: i += 1
        self.envia("%s OK EXPUNGE hecho\r\n" % tag)
    def c_search(self, tag, a, u):
        res = []
        with self.est.lock:
            for seq, m in enumerate(self.sel.msgs, 1):
                if self.cumple(m, list(a)): res.append(m.uid if u else seq)
        self.envia("* SEARCH%s\r\n%s OK SEARCH hecho\r\n" % ("".join(" %d" % x for x in res), tag))
    def cumple(self, m, a):
        meses = "jan feb mar apr may jun jul aug sep oct nov dec".split()
        def dia(s):
            d, mo, y = s.split("-"); return (int(y), meses.index(mo.lower()) + 1, int(d))
        msg = email.message_from_bytes(m.raw, policy=email.policy.default)
        fecha = msg["Date"].datetime if msg["Date"] else None
        texto = (m.raw.decode("utf-8", "replace") + " " + _cab(m.raw, "Subject") + " " + _cab(m.raw, "From")).lower()
        i = 0
        while i < len(a):
            k = a[i].upper(); i += 1
            if k == "CHARSET": i += 1
            elif k == "ALL": pass
            elif k == "UNSEEN":
                if "\\Seen" in m.flags: return False
            elif k == "SEEN":
                if "\\Seen" not in m.flags: return False
            elif k == "FLAGGED":
                if "\\Flagged" not in m.flags: return False
            elif k in ("FROM", "TO", "SUBJECT", "TEXT"):
                v = a[i].lower(); i += 1
                campo = texto if k == "TEXT" else _cab(m.raw, {"FROM": "From", "TO": "To", "SUBJECT": "Subject"}[k]).lower()
                if v not in campo: return False
            elif k in ("SINCE", "BEFORE"):
                d = dia(a[i]); i += 1
                if not fecha: return False
                f = (fecha.year, fecha.month, fecha.day)
                if k == "SINCE" and f < d: return False
                if k == "BEFORE" and f >= d: return False
        return True
    def c_append(self, tag, a, u):
        with self.est.lock:
            d = self.est.buzones.get(a[0])
            if not d: self.envia("%s NO [TRYCREATE] no existe\r\n" % tag); return
            fl = a[1] if isinstance(a[1], list) else []
            raw = a[-1]
            m = d.mete(raw, [FL.get(x.lower(), x) for x in fl])
        self.envia("%s OK [APPENDUID %d %d] APPEND hecho\r\n" % (tag, d.uidvalidity, m.uid))

class ManejadorIMAPStarttls(ManejadorIMAP):
    directo = False

# ---------------------------------------------------------------- SMTP
class ManejadorSMTP(socketserver.BaseRequestHandler):
    directo = True
    def setup(self):
        self.est = self.server.est
        self.sock = self.request
        if self.directo: self.sock = self.server.ctx.wrap_socket(self.sock, server_side=True)
        self.tls, self.buf, self.auth = self.directo, b"", None
    def envia(self, s): self.sock.sendall(s.encode())
    def linea(self):
        while b"\r\n" not in self.buf:
            d = self.sock.recv(65536)
            if not d: raise EOFError
            self.buf += d
        l, _, self.buf = self.buf.partition(b"\r\n")
        return l.decode("utf-8", "replace")
    def ehlo(self):
        r = ["smtp.prueba saluda a SOMA", "SIZE 10000000", "8BITMIME"]
        if not self.tls: r.append("STARTTLS")
        if self.tls: r.append("AUTH PLAIN LOGIN" if self.directo else "AUTH LOGIN")
        self.envia("".join("250%s%s\r\n" % ("-" if i < len(r) - 1 else " ", x) for i, x in enumerate(r)))
    def handle(self):
        try:
            self.envia("220 smtp.prueba ESMTP listo\r\n")
            de, para = None, []
            while True:
                l = self.linea(); up = l.upper()
                if up.startswith("EHLO"): self.ehlo()
                elif up == "STARTTLS" and not self.tls:
                    self.envia("220 2.0.0 Adelante\r\n"); self.sock = self.server.ctx.wrap_socket(self.sock, server_side=True); self.tls = True; self.buf = b""
                elif up.startswith("AUTH PLAIN") and self.tls and self.directo:
                    datos = l.split(" ", 2)[2] if len(l.split(" ")) > 2 else (self.envia("334 \r\n") or self.linea())
                    _, us, k = base64.b64decode(datos).decode("utf-8").split("\0")
                    self.fin_auth(us, k, "PLAIN")
                elif up == "AUTH LOGIN" and self.tls:
                    self.envia("334 VXNlcm5hbWU6\r\n"); us = base64.b64decode(self.linea()).decode("utf-8")
                    self.envia("334 UGFzc3dvcmQ6\r\n"); k = base64.b64decode(self.linea()).decode("utf-8")
                    self.fin_auth(us, k, "LOGIN")
                elif up.startswith("MAIL FROM:"):
                    if not self.auth: self.envia("530 5.7.0 Authentication required\r\n"); continue
                    de, para = l[10:].strip().strip("<>"), []; self.envia("250 2.1.0 OK\r\n")
                elif up.startswith("RCPT TO:"):
                    d = l[8:].strip().strip("<>")
                    if d.startswith("rechaza@"): self.envia("550 5.1.1 <%s> Usuario desconocido\r\n" % d)
                    else: para.append(d); self.envia("250 2.1.5 OK\r\n")
                elif up == "DATA":
                    if not para: self.envia("503 5.5.1 RCPT primero\r\n"); continue
                    self.envia("354 Adelante; termina con <CRLF>.<CRLF>\r\n")
                    while b"\r\n.\r\n" not in self.buf:
                        d = self.sock.recv(65536)
                        if not d: raise EOFError
                        self.buf += d
                    datos, _, self.buf = self.buf.partition(b"\r\n.\r\n")
                    if datos.startswith(b".\r\n"): datos = b""
                    lineas = [x[1:] if x.startswith(b".") else x for x in datos.split(b"\r\n")]
                    with self.est.lock:
                        self.est.smtp.append({"de": de, "para": list(para), "datos": b"\r\n".join(lineas) + b"\r\n", "puerto": "465" if self.directo else "587", "auth": self.auth})
                    self.envia("250 2.0.0 OK id=%d\r\n" % len(self.est.smtp))
                elif up == "QUIT": self.envia("221 2.0.0 Adios\r\n"); return
                elif up == "RSET": de, para = None, []; self.envia("250 OK\r\n")
                else: self.envia("502 5.5.2 No entiendo\r\n")
        except (EOFError, ConnectionError, ssl.SSLError, OSError, ValueError):
            return
    def fin_auth(self, us, k, mec):
        if us == USUARIO and k == CLAVE: self.auth = mec; self.envia("235 2.7.0 Accepted\r\n")
        else: self.envia("535 5.7.8 Username and Password not accepted\r\n")

class ManejadorSMTPStarttls(ManejadorSMTP):
    directo = False

class Servidor(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

def arranca(est, cert, clave):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(cert, clave)
    servs = {}
    for nombre, man in (("imap", ManejadorIMAP), ("imap_starttls", ManejadorIMAPStarttls), ("smtp", ManejadorSMTP), ("smtp_starttls", ManejadorSMTPStarttls)):
        s = Servidor(("127.0.0.1", 0), man); s.est, s.ctx = est, ctx
        threading.Thread(target=s.serve_forever, daemon=True).start()
        servs[nombre] = s
    return servs
