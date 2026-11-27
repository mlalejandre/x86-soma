#!/usr/bin/env python3
"""SOMA_355_STRATA: el lanzador (tools/soma_lanzador.py, el REAL como proceso) y tools/llm_server.py con perfiles de Strata.

Un "strata" falso hace lo peor que podria hacer el de verdad: no tiene /health (404), sirve su modelo con su propio nombre y rechaza
cualquier otro, arranca un proceso hijo "strata-vision" y tarda en cargar. Un "llama-server" falso apunta sus argumentos.
Comprueba: la lista (perfiles de Strata y .gguf con mmproj, con su vision), lanzar Strata (argumentos, sin CUDA_VISIBLE_DEVICES, en la
red local), el estado "listo" sin /health y con el nombre del modelo de Strata, cambiar a llama-server (Strata y su hijo se paran,
--mmproj), negarse si ya hay otro Strata que no es suyo, y en llm_server.py la lista y el lanzamiento por "SSH" (aqui, bash local).
"""
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

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


# strata falso: "strata serve --profile P --host H --port N --parallel 1"
STRATA = r'''#!PY
import json, os, sys, time, signal, subprocess, base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
a = sys.argv
port = int(a[a.index("--port") + 1]); prof = a[a.index("--profile") + 1]
open(prof + ".args", "w").write(json.dumps({"args": a[1:], "cuda": os.environ.get("CUDA_VISIBLE_DEVICES"), "path": os.environ.get("PATH", "")}))
hijo = subprocess.Popen([os.path.join(os.path.dirname(os.path.abspath(a[0])), "strata-vision")])
open(prof + ".hijo", "w").write(str(hijo.pid))
t0 = time.time(); MODELO = "qwen3.8-flash-iq3s"
class H(BaseHTTPRequestHandler):
    def log_message(self, *x): pass
    def _j(self, c, o):
        b = json.dumps(o).encode(); self.send_response(c); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == "/v1/models":
            if time.time() - t0 < 1.5: self._j(503, {"error": "cargando"})
            else: self._j(200, {"object": "list", "data": [{"id": MODELO, "object": "model"}]})
        else: self._j(404, {"error": "no existe"})
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0")); body = json.loads(self.rfile.read(n) or b"{}")
        if self.path != "/v1/chat/completions": return self._j(404, {"error": "no existe"})
        if body.get("model") != MODELO: return self._j(404, {"error": {"message": "model '%s' not found" % body.get("model")}})
        imgs = []
        for m in body.get("messages", []):
            c = m.get("content")
            if isinstance(c, list):
                for part in c:
                    if part.get("type") == "image_url":
                        u = part["image_url"]["url"]; head, _, b64 = u.partition(",")
                        raw = base64.b64decode(b64); imgs.append((head, raw[:4] == b"\x89PNG", len(raw)))
        txt = "VEO %d IMAGEN(ES)%s" % (len(imgs), "".join(" %s png=%s bytes=%d" % (h, p, l) for h, p, l in imgs)) if imgs else "HOLA SIN IMAGEN"
        self._j(200, {"choices": [{"message": {"role": "assistant", "content": txt}}], "usage": {"prompt_tokens": 9, "completion_tokens": 4}})
def fin(*x):
    try: hijo.terminate()
    except Exception: pass
    sys.exit(0)
signal.signal(signal.SIGTERM, fin)
ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
'''
VISION = '''#!PY
import time, signal, sys
signal.signal(signal.SIGTERM, lambda *x: sys.exit(0))
while True: time.sleep(1)
'''
LLAMA = r'''#!PY
import json, sys, time, signal
from http.server import BaseHTTPRequestHandler, HTTPServer
a = sys.argv; port = int(a[a.index("--port") + 1])
open(a[a.index("-m") + 1] + ".args", "w").write(" ".join(a[1:]))
class H(BaseHTTPRequestHandler):
    def log_message(self, *x): pass
    def do_GET(self):
        b = json.dumps({"data": [{"id": a[a.index("--alias") + 1]}]} if self.path == "/v1/models" else {"status": "ok"}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
signal.signal(signal.SIGTERM, lambda *x: sys.exit(0))
HTTPServer(("127.0.0.1", port), H).serve_forever()
'''


def escribe(ruta, texto):
    with open(ruta, "w") as f:
        f.write(texto.replace("#!PY", "#!" + sys.executable, 1))
    os.chmod(ruta, 0o755)


def pide(base, key, metodo, ruta, datos=None):
    rq = urllib.request.Request(base + ruta, method=metodo, data=json.dumps(datos or {}).encode() if metodo == "POST" else None,
                                headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(rq, timeout=15) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def escucha(port, proc, seg=40):
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


def espera(base, key, quiere, seg=40):
    t0 = time.time(); j = {}
    while time.time() - t0 < seg:
        _, j = pide(base, key, "GET", "/v1/estado")
        if j.get("estado") == quiere:
            return j
        time.sleep(0.3)
    return j


def vivo(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    try:                                                     # un zombi (sin recoger) ya no cuenta
        with open(f"/proc/{pid}/stat") as f:
            return f.read().split(")")[-1].split()[0] != "Z"
    except OSError:
        return True


def main():
    if sys.platform != "linux" or not shutil.which("pgrep"):
        print("test_strata_355: el lanzador corre en el servidor Linux; aqui no hay pgrep/Linux: SE OMITE")
        return 0
    with tempfile.TemporaryDirectory() as d:
        bind = d + "/bin"; os.makedirs(bind); os.makedirs(d + "/models/qwen-vl"); os.makedirs(d + "/strata-custom")
        escribe(bind + "/strata", STRATA); escribe(bind + "/strata-vision", VISION); escribe(bind + "/llama-server", LLAMA)
        open(d + "/models/qwen-vl/Qwen3-VL-8B-Q4_K_M.gguf", "wb").write(b"x")
        open(d + "/models/qwen-vl/mmproj-Qwen3-VL-8B-F16.gguf", "wb").write(b"x")
        open(d + "/models/texto-Q4.gguf", "wb").write(b"x")
        perfil = d + "/strata-custom/iq3s-latin-vision-120k.json"
        open(perfil, "w").write(json.dumps({"model": "GSQ-RCO IQ3_S", "context": 120000, "vision": {"dtype": "bf16"}, "spec": 8}))
        open(d + "/strata-custom/roto.json", "w").write("{ esto no es json")
        open(d + "/strata-custom/q4-helper-latin.json", "w").write(json.dumps({"vision": True}))   # SOMA_356: perfil de pruebas: no sale
        os.makedirs(d + "/models/embeddings"); open(d + "/models/embeddings/bge-m3-Q8_0.gguf", "wb").write(b"x")
        p_l, p_llm = libre(), libre()
        env = dict(os.environ, SOMA_LANZADOR_PORT=str(p_l), SERVER_PORT=str(p_llm), SERVER_PORT_4090=str(p_llm), MODELS_DIR=d + "/models",
                   LLAMA_SERVER_BIN=bind + "/llama-server", STRATA_BIN="strata", STRATA_PERFILES=d + "/strata-custom", CUDA_DEVICE="0",
                   SOMA_LANZADOR_CLAVE=d + "/clave", SOMA_LANZADOR_HOST="127.0.0.1", SOMA_PID_FILE=d + "/llm.pid", SOMA_LOG_FILE=d + "/llm.log",
                   PATH=bind + os.pathsep + os.environ.get("PATH", ""), CUDA_VISIBLE_DEVICES="0")
        srv = subprocess.Popen([sys.executable, os.path.join(R, "tools", "soma_lanzador.py")], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            ck(escucha(p_l, srv), "el lanzador escucha")
            key = open(d + "/clave").read().strip()
            base = f"http://127.0.0.1:{p_l}"
            c, j = pide(base, key, "GET", "/v1/modelos")
            m = {x["id"]: x for x in j.get("data", [])}
            ck(c == 200 and "strata-iq3s-latin-vision-120k" in m, f"el perfil de Strata sale en la lista: {list(m)}")
            ck(m.get("strata-iq3s-latin-vision-120k", {}).get("motor") == "strata" and m["strata-iq3s-latin-vision-120k"].get("vision") is True, "Strata, con vision")
            ck(m.get("qwen3-vl-8b-q4_k_m", {}).get("vision") is True and m.get("texto-q4", {}).get("vision") is False, f"los .gguf: vision si tienen mmproj: {m}")
            ck(not any("mmproj" in k or "roto" in k for k in m), "ni el mmproj ni un perfil roto salen como modelos")
            ck(not any("q4-helper" in k for k in m) and sum(1 for k in m if k.startswith("strata-")) == 1, f"SOMA_356: solo el perfil elegido: {list(m)}")
            ck(not any("bge" in k for k in m), "bge-m3 no sale como LLM")
            c, j = pide(base, key, "POST", "/v1/lanzar", {"modelo": "strata-iq3s-latin-vision-120k"})
            ck(c == 200 and j.get("ok"), f"lanzar Strata: {c} {j}")
            j = espera(base, key, "listo")
            ck(j.get("estado") == "listo", f"listo sin /health (por /v1/models): {j}")
            ck(j.get("modelo") == "qwen3.8-flash-iq3s", f"el estado da el nombre con el que Strata sirve el modelo: {j}")
            a = json.load(open(perfil + ".args"))
            args = a["args"]
            ck(args[:3] == ["serve", "--profile", perfil] and args[args.index("--host") + 1] == "0.0.0.0" and args[args.index("--port") + 1] == str(p_llm)
               and args[args.index("--parallel") + 1] == "1", f"argumentos de Strata: {args}")
            ck(a["cuda"] is None, f"sin CUDA_VISIBLE_DEVICES (el perfil reparte las GPU): {a['cuda']}")
            hijo = int(open(perfil + ".hijo").read())
            ck(vivo(hijo), "strata-vision en marcha")
            c, j = pide(base, key, "POST", "/v1/lanzar", {"modelo": "qwen3-vl-8b-q4_k_m"})
            ck(c == 200, f"cambiar a llama-server: {c} {j}")
            j = espera(base, key, "listo")
            ck(j.get("estado") == "listo" and j.get("modelo") == "qwen3-vl-8b-q4_k_m", f"llama-server listo: {j}")
            time.sleep(0.5)
            ck(not vivo(hijo), "al cambiar, strata-vision tambien se para (todo el grupo)")
            la = open(d + "/models/qwen-vl/Qwen3-VL-8B-Q4_K_M.gguf.args").read()
            ck("--mmproj " + d + "/models/qwen-vl/mmproj-Qwen3-VL-8B-F16.gguf" in la, f"llama-server con --mmproj: {la}")
            # un Strata que no es suyo (el del Mac): no se lanza otro encima
            otro = subprocess.Popen([bind + "/strata", "serve", "--profile", d + "/otro.json", "--host", "127.0.0.1", "--port", str(libre()), "--parallel", "1"])
            time.sleep(0.8)
            c, j = pide(base, key, "POST", "/v1/lanzar", {"modelo": "strata-iq3s-latin-vision-120k"})
            ck(c == 500 and "ya hay un Strata" in j.get("error", ""), f"con otro Strata en marcha se niega: {c} {j}")
            ck(otro.poll() is None, "y no toca el Strata ajeno")
            otro.terminate(); otro.wait(5)
            c, j = pide(base, key, "POST", "/v1/parar")
            j = espera(base, key, "parado", 10)
            ck(j.get("estado") == "parado", f"parar: {j}")
        finally:
            srv.send_signal(signal.SIGTERM)
            try:
                srv.wait(timeout=5)
            except subprocess.TimeoutExpired:
                srv.kill()
            subprocess.run(["pkill", "-f", d], check=False)

        # tools/llm_server.py (ejecutar.py en el Mac): la lista y el lanzamiento, con "SSH" = bash local
        if shutil.which("setsid") and shutil.which("lsof"):
            sys.path.insert(0, os.path.join(R, "tools"))
            os.environ.update(MODELS_DIR=d + "/models", STRATA_PERFILES=d + "/strata-custom", SERVER_IP="127.0.0.1", SERVER_PORT=str(p_llm),
                              SERVER_PORT_4090=str(p_llm), SOMA_PID_FILE=d + "/llm2.pid", SOMA_LOG_FILE=d + "/llm2.log", STRATA_BIN="strata",
                              LLAMA_SERVER_BIN=bind + "/llama-server", PATH=bind + os.pathsep + os.environ.get("PATH", ""))
            import importlib
            import llm_server as L
            importlib.reload(L)
            L.run_ssh = lambda cmd, timeout=15: subprocess.run(["bash", "-c", cmd], capture_output=True, text=True, timeout=timeout)
            ms = L.list_models()
            st = [x for x in ms if x.motor == "strata"]
            ck(len(st) == 1 and st[0].vision and L.alias_for(st[0].path) == "strata-iq3s-latin-vision-120k", f"llm_server: solo el perfil elegido: {ms}")
            ck(not any("bge-m3" in x.path for x in ms), "llm_server: bge-m3 no sale en el menu")
            L.STRATA_PERFIL = "*"; todos = [x for x in L.list_models() if x.motor == "strata"]; L.STRATA_PERFIL = "iq3s-latin-vision-120k.json"
            ck(len(todos) == 3, f"llm_server: STRATA_PERFIL=* los ofrece todos: {todos}")
            ck(any(x.mmproj.endswith("mmproj-Qwen3-VL-8B-F16.gguf") for x in ms if x.motor == "llama"), "llm_server: el mmproj del .gguf")
            elegido = next(x for x in st if "vision" in x.path)
            ck(L.launch_strata(elegido, L.alias_for(elegido.path)), "llm_server: lanza Strata")
            ck(L.wait_ready(30), "llm_server: espera a que este listo sin /health")
            ck(L.health_status() == 200, "llm_server: sano por /v1/models")
            L.stop_server()
            time.sleep(1)
            hijo = int(open(perfil + ".hijo").read())
            ck(not vivo(hijo) and L.health_status() is None, "llm_server: al salir para Strata y su hijo")
            subprocess.run(["pkill", "-f", d], check=False)
        else:
            print("  (sin setsid/lsof: se omite la parte de llm_server.py)")
    print(f"test_strata_355: {N} comprobaciones, {FALLOS} fallos")
    return 1 if FALLOS else 0


if __name__ == "__main__":
    sys.exit(main())
