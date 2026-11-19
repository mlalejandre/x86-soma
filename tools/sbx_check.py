#!/usr/bin/env python3
"""PARCHE 138: pruebas de los SERVICIOS AISLADOS en QEMU, sin intervencion.
Arranca la imagen con -fw_cfg name=opt/soma/selftest,string=sbx: el kernel ejecuta sbx_selftest() (ataques al aislamiento,
decodificadores de imagen aislados comparados con la referencia de host, fuzzing, concurrencia, fugas) y sale de QEMU con
el resultado (isa-debug-exit: 97 = bien, 99 = fallo). Uso: python3 tools/sbx_check.py [--iso build/soma.iso] [--cpu qemu64] [--timeout 300]"""
import argparse, functools, http.server, os, re, shutil, subprocess, sys, tempfile, threading

EXT = lambda n: ".png" if n[0] == "p" else ".gif" if n[0] == "g" else ".jpg" if n[0] == "j" else ".bmp" if n.startswith("bm") else ".bin"


def write_site(d):
    """Extrae las imagenes de prueba de src/arch/x86_64/sbx_testimg.h y escribe una pagina que las usa todas (op=1)."""
    s = open("src/arch/x86_64/sbx_testimg.h").read()
    arrays = {m.group(1): bytes(int(x) for x in m.group(2).replace("\n", "").split(",") if x.strip())
              for m in re.finditer(r"static const uint8_t sbxti_(\w+)\[\d+\] = \{(.*?)\};", s, re.S)}
    imgs = []
    for m in re.finditer(r'\{ "(\w+)", sbxti_(\w+), \d+, (\d+),', s):
        name, arr, op = m.group(1), m.group(2), int(m.group(3))
        if op != 1: continue
        open(os.path.join(d, name + EXT(name)), "wb").write(arrays[arr]); imgs.append(name + EXT(name))
    html = "<html><body><h1>SOMA sbx</h1>" + "".join(f'<p>{i}<br><img src="{i}"></p>' for i in imgs) + "</body></html>"
    open(os.path.join(d, "index.html"), "w").write(html)
    # PARCHE 142: pagina fija para la HUELLA del navegador (la compara el parche 143 con el navegador aislado)
    open(os.path.join(d, "ref.html"), "w").write(REF_HTML)
    return imgs


REF_HTML = """<!doctype html><html><head><title>Referencia SOMA</title><style>
body{font-family:sans-serif;margin:16px;background:#f8fafc;color:#0f172a}
h1{color:#1d4ed8;font-size:28px}.caja{display:inline-block;width:120px;height:60px;margin:6px;border-radius:8px}
table{border-collapse:collapse}td,th{border:1px solid #94a3b8;padding:4px 10px}
.r{background:#ef4444}.v{background:#10b981}.a{background:#3b82f6}
</style></head><body><h1>Navegador de SOMA</h1><p>P&aacute;rrafo con <b>negrita</b>, <i>cursiva</i> y un <a href="/n/text">enlace</a>.</p>
<div><span class="caja r"></span><span class="caja v"></span><span class="caja a"></span></div>
<table><tr><th>Uno</th><th>Dos</th></tr><tr><td>1</td><td>2</td></tr></table>
<ul><li>lista a</li><li>lista b</li></ul><p><img src="png_rgba.png" width="74" height="46"> <input value="campo"> <button>Bot&oacute;n</button></p>
<script>document.getElementById && (document.title = "Referencia SOMA");</script></body></html>"""

NET_TEXT = b"".join(b"SOMA netsvc linea %04d\n" % i for i in range(500))          # PARCHE 142: el kernel conoce el patron
NET_BIN = bytes((i * 7 + 3) % 251 for i in range(300000))


def serve(d, port=0):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a, **k): pass
        protocol_version = "HTTP/1.1"
        def _send(self, code, body, ctype="text/plain", extra=()):
            self.send_response(code); self.send_header("Content-Type", ctype)
            for k, v in extra: self.send_header(k, v)
            self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        def do_GET(self):                                # PARCHE 139: la pagina va comprimida (gzip): la inflara el servicio zdec
            import gzip as _gz
            if self.path.startswith("/n/"):              # PARCHE 142: rutas para el cliente HTTP aislado (netsvc)
                if self.path == "/n/text": return self._send(200, NET_TEXT)
                if self.path == "/n/gz": return self._send(200, _gz.compress(NET_TEXT), extra=[("Content-Encoding", "gzip")])
                if self.path == "/n/redir": return self._send(302, b"", extra=[("Location", "/n/text")])
                if self.path == "/n/bin": return self._send(200, NET_BIN, "application/octet-stream")
                if self.path == "/n/chunk":
                    self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Transfer-Encoding", "chunked"); self.end_headers()
                    for i in range(0, len(NET_TEXT), 700): part = NET_TEXT[i:i + 700]; self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
                    self.wfile.write(b"0\r\n\r\n"); return
                return self._send(404, b"no existe")
            if self.path.startswith("/index.html"):
                import gzip as _gz
                body = _gz.compress(open(os.path.join(d, "index.html"), "rb").read())
                self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Encoding", "gzip")
                self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
            else:
                super().do_GET()
    h = functools.partial(Quiet, directory=d)
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), h)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="build/soma.iso"); ap.add_argument("--cpu", default="qemu64")
    ap.add_argument("--timeout", type=int, default=300); ap.add_argument("--tests", default="sbx+user"); ap.add_argument("--web", action="store_true", help="anade la prueba del navegador real por red")
    ap.add_argument("--srcfs", action="store_true", help="anade la lectura de fuentes comprimidas del disco (SRCFS v02)"); ap.add_argument("--mem", default="512")
    a = ap.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu: print("[sbx] falta qemu-system-x86_64"); return 2
    if not os.path.exists(a.iso): print("[sbx] falta " + a.iso); return 2
    with tempfile.TemporaryDirectory(prefix="soma_sbx_") as td:
        ser = os.path.join(td, "serial.txt")
        extra, tests = [], a.tests
        if a.web:
            site = os.path.join(td, "site"); os.mkdir(site); write_site(site)
            try: srv = serve(site, 18765)                 # PARCHE 142: puerto fijo (la URL sale en la barra del navegador: huella reproducible)
            except OSError: srv = serve(site)
            tests += "+web+net+wref"
            extra = ["-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0",
                     "-fw_cfg", f"name=opt/soma/url,string=http://10.0.2.2:{srv.server_address[1]}/index.html"]
        if a.srcfs:                                       # PARCHE 140: disco con las fuentes empaquetadas por el empaquetador real
            img = os.path.join(td, "hdd.img")
            subprocess.check_call([sys.executable, "tests/test_srcfs_pack.py", "--emit", img, os.path.join(td, "m.txt")], stdout=subprocess.DEVNULL)
            extra += ["-drive", f"file={img},format=raw,if=virtio"]
            tests += "+src"
        cmd = [qemu, "-cpu", a.cpu, "-m", a.mem, "-cdrom", a.iso, "-display", "none", "-no-reboot", "-serial", "file:" + ser,
               "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04", "-fw_cfg", "name=opt/soma/selftest,string=" + tests] + extra
        try:
            rc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=a.timeout).returncode
        except subprocess.TimeoutExpired:
            rc = None
        text = open(ser, errors="replace").read() if os.path.exists(ser) else ""
    i = min([k for k in (text.find("SBX: servicios aislados"), text.find("USERMODE: prueba"), text.find("SBXWEB:"), text.find("SBXSRC:"), text.find("SBXNET:"), text.find("SBXWREF:")) if k >= 0], default=-1)
    print(text[i:] if i >= 0 else text[-3000:])
    if "KERNEL PANIC" in text: print("[sbx] PANICO del kernel"); return 1
    if rc == 97: print("[sbx] todo bien"); return 0
    print(f"[sbx] FALLO (codigo de QEMU {rc})"); return 1


if __name__ == "__main__":
    sys.exit(main())
