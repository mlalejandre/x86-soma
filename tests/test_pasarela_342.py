#!/usr/bin/env python3
"""SOMA_342_NAVEGADOR_AGIL: la pasarela (tools/web_gateway.py) reutiliza las conexiones con cada servidor.
Sin Internet: un servidor HTTP/1.1 local hace de origen y cuenta las conexiones que recibe.

    python3 tests/test_pasarela_342.py
"""
import http.client, http.server, os, socket, socketserver, sys, threading, time, urllib.parse

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RAIZ, "tools"))
for k in list(os.environ):                     # sin proxies del entorno: la pasarela usaria urllib (como debe)
    if k.lower().endswith("_proxy"):
        del os.environ[k]
import web_gateway as gw                          # noqa: E402

conexiones = [0]
cerrar_la_proxima = [False]


class Origen(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    disable_nagle_algorithm = True

    def setup(self):
        conexiones[0] += 1
        super().setup()

    def log_message(self, *a):
        pass

    def do_GET(self):
        p = urllib.parse.urlsplit(self.path)
        if p.path == "/trozos":
            self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Transfer-Encoding", "chunked"); self.end_headers()
            for i in range(5):
                part = b"trozo%d;" % i
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            return
        body = ("recurso %s" % p.path).encode() * 20
        self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Content-Length", str(len(body)))
        if cerrar_la_proxima[0]:
            cerrar_la_proxima[0] = False
            self.send_header("Connection", "close"); self.close_connection = True
        self.end_headers(); self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        d = self.rfile.read(n)
        self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Content-Length", str(len(d))); self.end_headers(); self.wfile.write(d)


def main():
    socketserver.TCPServer.allow_reuse_address = True
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origen)
    puerto = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % puerto
    fallos = 0

    def ck(c, m):
        nonlocal fallos
        print(("  ok    " if c else "  FALLO ") + m)
        fallos += not c

    st, h, ct, data, ck_, loc = gw.fetch(base + "/a")
    ck(st == 200 and data == b"recurso /a" * 20, "primera peticion")
    for i in range(30):
        st, h, ct, data, ck_, loc = gw.fetch(base + "/r%d" % i)
        if st != 200 or data != ("recurso /r%d" % i).encode() * 20:
            ck(False, "peticion %d" % i)
    ck(conexiones[0] == 1, "31 peticiones seguidas por una sola conexion (hubo %d)" % conexiones[0])

    cerrar_la_proxima[0] = True
    gw.fetch(base + "/cierra")                      # el servidor avisa de que cierra: no se guarda
    st, h, ct, data, ck_, loc = gw.fetch(base + "/despues")
    ck(st == 200 and data == b"recurso /despues" * 20 and conexiones[0] == 2, "si el servidor cierra, la siguiente abre otra")

    # el servidor corta la conexion guardada sin avisar (como al pasar su tiempo de espera): se repite con una nueva
    with gw._pool_lock:
        guardadas = [c for lst in gw._pool.values() for c, t in lst]
    for c in guardadas:
        try:
            c.sock.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
    st, h, ct, data, ck_, loc = gw.fetch(base + "/tras_corte")
    ck(st == 200 and data == b"recurso /tras_corte" * 20, "conexion guardada cortada: se repite con otra sin error")

    antes = conexiones[0]
    st, h, ct, data, ck_, loc = gw.fetch(base + "/eco", "POST", b"hola=1", {"Content-Type": "application/x-www-form-urlencoded"})
    ck(st == 200 and data == b"hola=1" and conexiones[0] == antes + 1, "un POST va por una conexion propia (nunca se repite)")

    st, h, ct, data, ck_, loc = gw.fetch(base + "/trozos")
    ck(st == 200 and data == b"trozo0;trozo1;trozo2;trozo3;trozo4;", "respuesta por trozos (chunked)")
    st, h, ct, data, ck_, loc = gw.fetch(base + "/tras_trozos")
    ck(st == 200 and data == b"recurso /tras_trozos" * 20, "y despues la conexion sigue sirviendo")

    hilos, res = [], []
    def una(i):
        res.append(gw.fetch(base + "/p%d" % i)[3] == ("recurso /p%d" % i).encode() * 20)
    for i in range(24):
        t = threading.Thread(target=una, args=(i,)); t.start(); hilos.append(t)
    for t in hilos:
        t.join()
    ck(len(res) == 24 and all(res), "24 peticiones a la vez desde varios hilos (como la precarga)")
    with gw._pool_lock:
        n = sum(len(v) for v in gw._pool.values())
    ck(n <= gw.POOL_PER_HOST, "como mucho %d conexiones guardadas por servidor (hay %d)" % (gw.POOL_PER_HOST, n))
    srv.shutdown()
    print("test_pasarela_342:", "todo bien" if not fallos else "%d fallos" % fallos)
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
