#!/usr/bin/env python3
# SOMA_278_EMBEDDINGS: el lanzador arranca y para el servidor de embeddings sin tocar al del LLM.
# Usa un "llama-server" falso (un script que atiende /health y /v1/embeddings) y el lanzador REAL como proceso.
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FALLOS = 0
N = 0


def ck(cond, msg):
    global FALLOS, N
    N += 1
    if not cond:
        FALLOS += 1
        print("  FALLA:", msg)


def libre():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


FALSO = r'''#!/usr/bin/env python3
import json, sys, time, signal
from http.server import BaseHTTPRequestHandler, HTTPServer
a = sys.argv
port = int(a[a.index("--port") + 1])
ok = ("--embeddings" in a, a[a.index("--pooling") + 1] == "cls", a[a.index("-ngl") + 1] == "0", a[a.index("--host") + 1] == "0.0.0.0")
open(a[a.index("-m") + 1] + ".args", "w").write(" ".join(a))
t0 = time.time()
class H(BaseHTTPRequestHandler):
    def log_message(self, *x): pass
    def _j(self, c, o):
        b = json.dumps(o).encode(); self.send_response(c); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == "/health": self._j(503 if time.time() - t0 < 1.0 else 200, {"status": "ok"})
        else: self._j(404, {})
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0")); self.rfile.read(n)
        self._j(200, {"data": [{"embedding": [0.1] * 4}]} if all(ok) else {"error": "args"})
signal.signal(signal.SIGTERM, lambda *x: sys.exit(0))
HTTPServer(("127.0.0.1", port), H).serve_forever()
'''


class Dummy(BaseHTTPRequestHandler):                 # hace de llama-server del LLM: debe seguir vivo pase lo que pase con los embeddings
    def log_message(self, *x): pass

    def do_GET(self):
        self.send_response(200); self.send_header("Content-Length", "2"); self.end_headers(); self.wfile.write(b"ok")


def pide(base, key, metodo, ruta):
    rq = urllib.request.Request(base + ruta, method=metodo, data=b"{}" if metodo == "POST" else None,
                                headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(rq, timeout=10) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def escucha(port, proc, seg=40):
    """SOMA_303_CI: espera a que el lanzador acepte conexiones de verdad (en la CI de macOS, Python tarda en arrancar)."""
    t0 = time.time()
    while time.time() - t0 < seg:
        if proc.poll() is not None:
            return False
        try:
            socket.create_connection(("127.0.0.1", port), 0.5).close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def espera(base, key, quiere, seg=40):   # SOMA_304_CI: la CI de macOS es lenta
    t0 = time.time()
    while time.time() - t0 < seg:
        _, j = pide(base, key, "GET", "/v1/embeddings/estado")
        if j.get("estado") == quiere:
            return j
        time.sleep(0.3)
    return j


def main():
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(d + "/models/embeddings")
        modelo = d + "/models/embeddings/bge-m3-Q8_0.gguf"
        open(modelo, "wb").write(b"x")
        open(d + "/models/otro-llm-Q4.gguf", "wb").write(b"x")
        falso = d + "/llama-server"
        # SOMA_304_CI: con el Python que corre la prueba; en macOS "/usr/bin/env python3" puede dar con el python3 de Xcode, que no arranca
        open(falso, "w").write(FALSO.replace("#!/usr/bin/env python3", "#!" + sys.executable, 1))
        os.chmod(falso, 0o755)
        p_l, p_e, p_llm = libre(), libre(), libre()
        dummy = HTTPServer(("127.0.0.1", p_llm), Dummy)
        threading.Thread(target=dummy.serve_forever, daemon=True).start()
        env = dict(os.environ, SOMA_LANZADOR_PORT=str(p_l), SOMA_EMB_PORT=str(p_e), SERVER_PORT=str(p_llm), SERVER_PORT_4090=str(p_llm),
                   MODELS_DIR=d + "/models", SOMA_EMB_BIN=falso, LLAMA_SERVER_BIN=falso, SOMA_LANZADOR_CLAVE=d + "/clave", SOMA_LANZADOR_HOST="127.0.0.1",
                   SOMA_EMB_PID_FILE=d + "/emb.pid", SOMA_EMB_LOG_FILE=d + "/emb.log", SOMA_PID_FILE=d + "/llm.pid", SOMA_LOG_FILE=d + "/llm.log")
        env.pop("SOMA_EMB_MODEL", None)
        srv = subprocess.Popen([sys.executable, os.path.join(R, "tools", "soma_lanzador.py")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            listo = escucha(p_l, srv)                           # SOMA_303_CI
            key = open(d + "/clave").read().strip() if os.path.exists(d + "/clave") else ""
            base = f"http://127.0.0.1:{p_l}"
            ck(key != "", "el lanzador arranca y crea la clave")
            if not listo:
                srv.kill()
                print("  FALLA: el lanzador no llega a escuchar en 127.0.0.1:%d. Lo que dijo:\n%s" % (p_l, srv.communicate()[0]))
                print(f"test_lanzador_emb_278: {N + 1} comprobaciones, {FALLOS + 1} fallos")
                return 1
            c, j = pide(base, "mala", "GET", "/v1/embeddings/estado")
            ck(c == 401, "sin la clave buena no hay acceso a los embeddings")
            c, j = pide(base, key, "GET", "/v1/embeddings/estado")
            ck(c == 200 and j["estado"] == "parado", f"al principio esta parado: {j}")
            c, j = pide(base, key, "GET", "/v1/modelos")
            ids = [m["id"] for m in j.get("data", [])]
            ck(c == 200 and len(ids) == 1 and "bge" not in ids[0], f"bge-m3 no sale como modelo de LLM: {ids}")
            c, j = pide(base, key, "POST", "/v1/embeddings/lanzar")
            ck(c == 200 and j.get("ok") is True, f"lanzar: {c} {j}")
            c, j = pide(base, key, "GET", "/v1/embeddings/estado")
            ck(j["estado"] in ("cargando", "listo"), f"tras lanzar esta cargando o listo: {j}")
            j = espera(base, key, "listo")
            ck(j["estado"] == "listo" and j["modelo"] == "bge-m3", f"acaba listo: {j}")
            args = open(modelo + ".args").read()
            ck("--embeddings" in args and "--pooling cls" in args and "-ngl 0" in args and str(p_e) in args and "--host 0.0.0.0" in args, f"parametros: {args}")
            rq = urllib.request.Request(f"http://127.0.0.1:{p_e}/v1/embeddings", data=b'{"input":"hola"}', headers={"Content-Type": "application/json"})
            ck(json.load(urllib.request.urlopen(rq, timeout=5))["data"][0]["embedding"] == [0.1] * 4, "el servidor de embeddings contesta")
            with urllib.request.urlopen(f"http://127.0.0.1:{p_llm}/", timeout=3) as r:
                ck(r.read() == b"ok", "el servidor del LLM sigue vivo mientras estan los embeddings")
            c, j = pide(base, key, "GET", "/v1/estado")
            ck(c == 200 and j["estado"] == "listo", f"el estado del LLM no se ve afectado: {j}")
            c, j = pide(base, key, "POST", "/v1/embeddings/lanzar")
            ck(c == 200, "lanzar otra vez reinicia sin romper nada")
            j = espera(base, key, "listo")
            ck(j["estado"] == "listo", f"listo tras relanzar: {j}")
            c, j = pide(base, key, "POST", "/v1/embeddings/parar")
            ck(c == 200 and j.get("ok") is True, "parar")
            j = espera(base, key, "parado", 8)
            ck(j["estado"] == "parado", f"queda parado: {j}")
            with urllib.request.urlopen(f"http://127.0.0.1:{p_llm}/", timeout=3) as r:
                ck(r.read() == b"ok", "el servidor del LLM sigue vivo despues de parar los embeddings")
            ck(not os.path.exists(d + "/emb.pid"), "se borra el archivo de PID")
            # sin modelo bge-m3: error claro
            os.remove(modelo)
            c, j = pide(base, key, "POST", "/v1/embeddings/lanzar")
            ck(c == 500 and "bge-m3" in j.get("error", ""), f"sin modelo: {c} {j}")
        finally:
            srv.send_signal(signal.SIGTERM)
            try:
                srv.wait(timeout=5)
            except subprocess.TimeoutExpired:
                srv.kill()
            subprocess.run(["pkill", "-f", d], check=False)
            dummy.shutdown()
        # mismo puerto para el LLM y los embeddings: se niega
        p1 = libre()
        env2 = dict(env, SOMA_LANZADOR_PORT=str(libre()), SOMA_EMB_PORT=str(p1), SERVER_PORT=str(p1), SERVER_PORT_4090=str(p1))
        open(modelo, "wb").write(b"x")
        srv = subprocess.Popen([sys.executable, os.path.join(R, "tools", "soma_lanzador.py")], env=env2, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            ck(escucha(int(env2["SOMA_LANZADOR_PORT"]), srv), "el segundo lanzador escucha")   # SOMA_303_CI
            b2 = f"http://127.0.0.1:{env2['SOMA_LANZADOR_PORT']}"
            c, j = pide(b2, key, "POST", "/v1/embeddings/lanzar")
            ck(c == 500 and "mismo" in j.get("error", ""), f"mismo puerto: {c} {j}")
        finally:
            srv.send_signal(signal.SIGTERM)
            srv.wait(timeout=5)
    print(f"test_lanzador_emb_278: {N} comprobaciones, {FALLOS} fallos")
    return 1 if FALLOS else 0


if __name__ == "__main__":
    sys.exit(main())
