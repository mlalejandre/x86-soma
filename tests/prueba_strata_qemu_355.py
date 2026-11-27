#!/usr/bin/env python3
"""SOMA_355_STRATA: de punta a punta en QEMU: SOMA lista los modelos del lanzador, arranca un perfil de Strata, le pregunta con una
imagen (mirar pantalla: captura + image_url) y adopta el nombre del modelo de Strata. En el anfitrion corren el lanzador REAL
(tools/soma_lanzador.py) y el Strata falso de tests/test_strata_355.py (sin /health, rechaza otro nombre de modelo, cuenta las
imagenes que le llegan).

    python3 tests/prueba_strata_qemu_355.py        (necesita build/x86_64/soma.iso)
"""
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(R, "tests"))
import test_strata_355 as T  # noqa: E402

ISO = os.path.join(R, "build", "x86_64", "soma.iso")
fallos = 0


def ck(ok, desc):
    global fallos
    fallos += not ok
    print(("  ok   " if ok else "  FALLO"), desc)


def bloque(sal, orden, n=0):
    partes = sal.split("[CMD] " + orden + "\n")
    return partes[n + 1].split("[CMD-FIN]")[0] if len(partes) > n + 1 else ""


def main():
    if not os.path.exists(ISO):
        print("falta", ISO, "(make ARCH=x86_64)")
        return 2
    with tempfile.TemporaryDirectory() as d:
        bind = d + "/bin"; os.makedirs(bind); os.makedirs(d + "/models"); os.makedirs(d + "/strata-custom")
        T.escribe(bind + "/strata", T.STRATA); T.escribe(bind + "/strata-vision", T.VISION); T.escribe(bind + "/llama-server", T.LLAMA)
        open(d + "/models/texto-Q4.gguf", "wb").write(b"x")
        perfil = d + "/strata-custom/iq3s-latin-vision-120k.json"
        open(perfil, "w").write(json.dumps({"model": "GSQ-RCO IQ3_S", "context": 120000, "vision": {"dtype": "bf16"}}))
        p_l, p_llm = T.libre(), T.libre()
        env = dict(os.environ, SOMA_LANZADOR_PORT=str(p_l), SERVER_PORT=str(p_llm), SERVER_PORT_4090=str(p_llm), MODELS_DIR=d + "/models",
                   LLAMA_SERVER_BIN=bind + "/llama-server", STRATA_BIN="strata", STRATA_PERFILES=d + "/strata-custom",
                   SOMA_LANZADOR_CLAVE=d + "/clave", SOMA_LANZADOR_HOST="127.0.0.1", SOMA_PID_FILE=d + "/llm.pid", SOMA_LOG_FILE=d + "/llm.log",
                   PATH=bind + os.pathsep + os.environ.get("PATH", ""))
        srv = subprocess.Popen([sys.executable, os.path.join(R, "tools", "soma_lanzador.py")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            if not T.escucha(p_l, srv):
                print("el lanzador no arranca")
                return 1
            key = open(d + "/clave").read().strip()
            ordenes = ["llm-config proveedor local", f"llm-config servidor 10.0.2.2:{p_llm}", "llm-config modelo nail-35b",
                       f"llm-config lanzador servidor 10.0.2.2:{p_l}", f"llm-config lanzador clave {key}", "llm-config lanzador modelos",
                       "llm-config lanzar strata-iq3s-latin-vision-120k", "llm-config", "mirar pantalla que ves",
                       "mirar /no/existe.png", "llm-config modelo otro-nombre", "health", "llm-config", "mirar pantalla", "@60"]
            hdd, raw = d + "/hdd.img", d + "/serie.txt"
            with open(hdd, "wb") as f:
                f.truncate(64 << 20)
            a = ["qemu-system-x86_64", "-machine", "pc", "-m", "2G", "-cdrom", ISO, "-drive", f"file={hdd},format=raw,if=virtio",
                 "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0", "-vga", "std",
                 "-fw_cfg", "name=opt/soma/cmds,string=big format yes;" + ";".join(ordenes),
                 "-serial", f"file:{raw}", "-display", "none", "-no-reboot"]
            try:
                subprocess.run(a, timeout=int(os.environ.get("PLAZO", "240")), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except subprocess.TimeoutExpired:
                pass
            sal = open(raw, "rb").read().decode("utf-8", "replace").replace("\r", "")
            sal = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", sal)
        finally:
            srv.send_signal(signal.SIGTERM)
            try:
                srv.wait(timeout=5)
            except subprocess.TimeoutExpired:
                srv.kill()
            subprocess.run(["pkill", "-f", d], check=False)
        ck("strata-iq3s-latin-vision-120k" in bloque(sal, "llm-config lanzador modelos"), "SOMA ve el perfil de Strata en la lista del lanzador")
        ck("LISTO" in bloque(sal, "llm-config lanzar strata-iq3s-latin-vision-120k"), "lo arranca y espera a que este listo (sin /health)")
        ck("modelo: qwen3.8-flash-iq3s" in bloque(sal, "llm-config"), "queda elegido con el nombre de Strata (que rechaza otros)")
        m = bloque(sal, "mirar pantalla que ves")
        ck(re.search(r"VEO 1 IMAGEN\(ES\) data:image/png;base64 png=True bytes=\d{4,}", m) is not None, f"mirar: la captura llega a Strata como PNG: {m.strip()[:160]}")
        ck("no se puede leer" in bloque(sal, "mirar /no/existe.png"), "mirar: archivo que no existe")
        ck("adoptado" in sal and "Servidor LLM: OK" in bloque(sal, "health"), "health: sano sin /health y adopta el nombre del servidor")
        ck("modelo: qwen3.8-flash-iq3s" in bloque(sal, "llm-config", 1), "tras un nombre equivocado (ruta de ejecutar.py), vuelve al de Strata")
        ck("VEO 1 IMAGEN" in bloque(sal, "mirar pantalla"), "y sigue funcionando")
    print(f"prueba_strata_qemu_355: {fallos} fallos")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
