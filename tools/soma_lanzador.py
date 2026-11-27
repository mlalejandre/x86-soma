#!/usr/bin/env python3
# soma_lanzador.py - SOMA_196: lanzador de llama-server para SOMA. SE EJECUTA EN EL SERVIDOR (el equipo Linux que tiene la GPU y
# los modelos), no en SOMA ni en el Mac.
#
# SOMA en un equipo real no tiene SSH: no puede hacer lo que hace tools/llm_server.py desde ejecutar.py. Este servicio hace lo
# mismo (misma orden, mismos parametros, mismos archivos de PID y de registro, mismas variables de entorno) cuando SOMA se lo
# pide por HTTP desde Ajustes > IA o con la orden llm-config:
#
#   GET  /v1/estado    {"estado": "listo" | "cargando" | "parado" | "error", "modelo": ..., "detalle": ...}
#   GET  /v1/modelos   {"data": [{"id": alias, "archivo": ruta relativa, "gib": tamano, "motor": "llama"|"strata", "vision": bool}, ...]}
#
# SOMA_355_STRATA: ademas de los .gguf (llama-server), cada perfil de Strata (~/strata-custom/*.json, o STRATA_PERFILES) sale como un
# modelo mas ("strata-<perfil>"). Se lanza con  strata serve --profile P --host 0.0.0.0 --port SERVER_PORT --parallel 1  en el mismo
# puerto que llama-server, asi que SOMA lo usa igual (API compatible con OpenAI en /v1). Los modelos con vision (un perfil que lo
# diga, o un .gguf con su mmproj*.gguf en la misma carpeta, que se pasa con --mmproj) aceptan imagenes desde SOMA.
#   POST /v1/lanzar    {"modelo": alias}   para lo que hubiera en el puerto y arranca llama-server con ese modelo
#   POST /v1/parar     para llama-server (libera la VRAM)
#
# SOMA_278_EMBEDDINGS: segundo servidor, independiente del anterior, para la memoria de SOMA (bge-m3, en CPU, puerto 8089). Arrancarlo
# o pararlo NO toca al del LLM:
#   GET  /v1/embeddings/estado   igual que /v1/estado, para el servidor de embeddings
#   POST /v1/embeddings/lanzar   arranca llama-server --embeddings con el primer bge-m3*.gguf que haya bajo MODELS_DIR
#   POST /v1/embeddings/parar    lo para
#
# Seguridad: solo atiende a equipos de la red local y toda peticion debe traer la clave (Authorization: Bearer <clave>).
# La clave se crea la primera vez y se guarda en ~/.config/soma/lanzador.clave (solo legible por el usuario).
#
# Uso:
#   python3 soma_lanzador.py                      arranca el servicio (puerto 8086) y muestra la clave
#   python3 soma_lanzador.py --clave              muestra la clave y sale
#   python3 soma_lanzador.py --nueva-clave        cambia la clave
#   python3 soma_lanzador.py --instalar-servicio  lo deja como servicio de usuario de systemd (arranca solo con el equipo)

from __future__ import annotations

import argparse
import hmac
import ipaddress
import json
import os
import re
import secrets
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# Las mismas variables de entorno (y valores por defecto) que tools/llm_server.py
SERVER_PORT = int(os.getenv("SERVER_PORT_4090", os.getenv("SERVER_PORT", "8087")))
LLAMA_SERVER_BIN = os.getenv("LLAMA_SERVER_BIN", "/home/mlalejandre/llama.cpp-qwen38-opt/build/bin/llama-server")
MODELS_DIR = os.getenv("MODELS_DIR", "/mnt/IA/models")
DEFAULT_MODEL_PATH = os.getenv("REMOTE_MODEL_PATH", "/mnt/IA/models/nail-35b/Nail-Qwen3.6-35B-A3B-UD-Q4_K_S.gguf")
DEFAULT_ALIAS = os.getenv("REMOTE_MODEL_ALIAS", "nail-35b")
PID_FILE = os.getenv("SOMA_PID_FILE", "/tmp/soma-llama.pid")
LOG_FILE = os.getenv("SOMA_LOG_FILE", "/tmp/soma-llama.log")
CTX = int(os.getenv("REMOTE_CTX", "32768"))
THREADS = int(os.getenv("REMOTE_THREADS", "8"))
PREDICT = int(os.getenv("REMOTE_PREDICT", "16384"))
CUDA_DEVICE = os.getenv("CUDA_DEVICE", "0")
EXTRA_ARGS = os.getenv("LLAMA_EXTRA_ARGS", "")
# SOMA_355_STRATA
STRATA_BIN = os.getenv("STRATA_BIN", "strata")              # nombre (se busca en el PATH, ~/.local/bin y ~/bin) o ruta
STRATA_DIR = os.path.expanduser(os.getenv("STRATA_PERFILES", "~/strata-custom"))
# SOMA_356_STRATA_UNO: que perfiles salen en la lista: el de tu script (por defecto), varios separados por comas, o "*" (todos)
STRATA_PERFIL = os.getenv("STRATA_PERFIL", "iq3s-latin-vision-120k.json")
STRATA_EXTRA_ARGS = os.getenv("STRATA_EXTRA_ARGS", "")

# SOMA_278_EMBEDDINGS
EMB_PORT = int(os.getenv("SOMA_EMB_PORT", "8089"))
EMB_BIN = os.getenv("SOMA_EMB_BIN", "")                    # vacio: /opt/llama.cpp/build/bin/llama-server si existe; si no, LLAMA_SERVER_BIN
EMB_MODEL = os.getenv("SOMA_EMB_MODEL", "")                # vacio: el primer bge-m3*.gguf bajo MODELS_DIR
EMB_THREADS = int(os.getenv("SOMA_EMB_THREADS", "4"))
EMB_CTX = int(os.getenv("SOMA_EMB_CTX", "8192"))
EMB_EXTRA_ARGS = os.getenv("SOMA_EMB_EXTRA_ARGS", "")
EMB_PID_FILE = os.getenv("SOMA_EMB_PID_FILE", "/tmp/soma-emb.pid")
EMB_LOG_FILE = os.getenv("SOMA_EMB_LOG_FILE", "/tmp/soma-emb.log")
EMB_ALIAS = "bge-m3"

LANZADOR_PORT = int(os.getenv("SOMA_LANZADOR_PORT", "8086"))
LANZADOR_HOST = os.getenv("SOMA_LANZADOR_HOST", "0.0.0.0")   # SOMA_303_CI: las pruebas escuchan solo en 127.0.0.1
KEY_FILE = Path(os.getenv("SOMA_LANZADOR_CLAVE", str(Path.home() / ".config" / "soma" / "lanzador.clave")))
KEY_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"      # sin l, i, o, 0, 1: hay que teclearla en SOMA
SPLIT_RE = re.compile(r"-(\d{5})-of-(\d{5})\.gguf$")

_lock = threading.Lock()
_proc: subprocess.Popen | None = None
_alias = ""
_started = 0.0
_eproc: subprocess.Popen | None = None           # SOMA_278_EMBEDDINGS
_estarted = 0.0


def info(msg: str) -> None:
    print(f"[LANZADOR] {msg}", flush=True)


# ---------------------------------------------------------------- clave

def read_key(create: bool = True) -> str:
    try:
        k = KEY_FILE.read_text().strip()
        if k:
            return k
    except OSError:
        pass
    return new_key() if create else ""


def new_key() -> str:
    k = "".join(secrets.choice(KEY_ALPHABET) for _ in range(16))
    KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(k + "\n")
    os.chmod(KEY_FILE, 0o600)
    return k


# ---------------------------------------------------------------- modelos (misma logica que llm_server.py)

def alias_for(path: str) -> str:
    if path == DEFAULT_MODEL_PATH:
        return DEFAULT_ALIAS
    name = SPLIT_RE.sub("", os.path.basename(path))
    name = re.sub(r"\.gguf$", "", name, flags=re.I)
    return re.sub(r"[^a-z0-9._-]+", "-", name.lower()).strip("-")[:48] or "model"


def strata_bin() -> str:
    """SOMA_355_STRATA: el ejecutable de Strata. Como servicio de systemd el PATH no trae ~/.local/bin: se mira ahi tambien."""
    if os.sep in STRATA_BIN:
        return STRATA_BIN if os.access(STRATA_BIN, os.X_OK) else ""
    path = os.pathsep.join([os.environ.get("PATH", ""), os.path.expanduser("~/.local/bin"), os.path.expanduser("~/bin")])
    return shutil.which(STRATA_BIN, path=path) or ""


def strata_elegido(nombre: str) -> bool:
    """SOMA_356_STRATA_UNO: en ~/strata-custom hay muchos perfiles de pruebas; solo se ofrecen los de STRATA_PERFIL."""
    want = [w.strip() for w in STRATA_PERFIL.split(",") if w.strip()]
    if "*" in want:
        return True
    return any(nombre == w or nombre == w + ".json" for w in want)


def strata_profiles() -> list[dict]:
    """SOMA_355_STRATA: un modelo por perfil JSON de Strata. "vision" si el perfil lo menciona (vision, mmproj, image)."""
    out = []
    try:
        names = sorted(n for n in os.listdir(STRATA_DIR) if n.lower().endswith(".json") and strata_elegido(n))
    except OSError:
        return out
    for n in names:
        path = os.path.join(STRATA_DIR, n)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                txt = f.read(1 << 20)
            json.loads(txt)                                   # un perfil roto no se ofrece
        except (OSError, ValueError):
            continue
        low = txt.lower()
        alias = "strata-" + (re.sub(r"[^a-z0-9._-]+", "-", n[:-5].lower()).strip("-")[:40] or "perfil")
        out.append({"id": alias, "archivo": "strata: " + n, "gib": 0, "ruta": path, "motor": "strata",
                    "vision": any(k in low for k in ("vision", "mmproj", "image"))})
    return out


def mmproj_for(path: str) -> str:
    """SOMA_355_STRATA: el proyector de vision (mmproj*.gguf) que haya en la carpeta del modelo, para lanzarlo con --mmproj."""
    d = os.path.dirname(path)
    try:
        cand = sorted(n for n in os.listdir(d) if n.lower().endswith(".gguf") and "mmproj" in n.lower())
    except OSError:
        return ""
    return os.path.join(d, cand[0]) if cand else ""


def list_models() -> list[dict]:
    groups: dict[str, dict] = {}
    for root, _dirs, files in os.walk(MODELS_DIR, followlinks=True):
        for fn in files:
            if not fn.lower().endswith(".gguf") or "mmproj" in fn.lower():
                continue                              # proyectores de vision: no son modelos autonomos
            path = os.path.join(root, fn)
            if "/embeddings/" in path.replace(os.sep, "/") or "bge-m3" in fn.lower():
                continue                              # SOMA_278: los modelos de embeddings no son LLM; se arrancan con /v1/embeddings/lanzar
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            m = SPLIT_RE.search(path)
            g = groups.setdefault(SPLIT_RE.sub(".gguf", path), {"size": 0, "first": None})
            g["size"] += size
            if m is None or m.group(1) == "00001":
                g["first"] = path                     # llama.cpp carga el resto de partes solo
    out, seen = [], set()
    for g in sorted((g for g in groups.values() if g["first"]), key=lambda g: g["first"].lower()):
        alias, n = alias_for(g["first"]), 2
        base = alias
        while alias in seen:                          # dos archivos con el mismo nombre en carpetas distintas
            alias = f"{base[:44]}-{n}"
            n += 1
        seen.add(alias)
        rel = g["first"][len(MODELS_DIR):].lstrip("/") if g["first"].startswith(MODELS_DIR) else g["first"]
        mm = mmproj_for(g["first"])
        out.append({"id": alias, "archivo": rel, "gib": round(g["size"] / 2**30, 1), "ruta": g["first"], "motor": "llama", "vision": bool(mm), "mmproj": mm})
    if not out and os.path.exists(DEFAULT_MODEL_PATH):
        mm = mmproj_for(DEFAULT_MODEL_PATH)
        out.append({"id": DEFAULT_ALIAS, "archivo": os.path.basename(DEFAULT_MODEL_PATH), "gib": 0, "ruta": DEFAULT_MODEL_PATH, "motor": "llama", "vision": bool(mm), "mmproj": mm})
    for m in strata_profiles():                               # SOMA_355_STRATA
        if m["id"] not in seen:
            seen.add(m["id"])
            out.append(m)
    return out


# ---------------------------------------------------------------- llama-server

def health() -> int | None:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{SERVER_PORT}/health", timeout=2.0) as r:
            return r.status
    except urllib.error.HTTPError as e:
        if e.code != 404:
            return e.code
    except Exception:
        return None
    try:                                                      # SOMA_355_STRATA: un servidor sin /health (Strata): listo si /v1/models contesta
        with urllib.request.urlopen(f"http://127.0.0.1:{SERVER_PORT}/v1/models", timeout=3.0) as r:
            return 200 if r.status == 200 else 503
    except urllib.error.HTTPError as e:
        return 503 if e.code in (503, 404) else e.code
    except Exception:
        return 503


def loaded_model() -> str:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{SERVER_PORT}/v1/models", timeout=3.0) as r:
            return str(json.load(r)["data"][0]["id"])
    except Exception:
        return ""


def log_tail(chars: int = 150) -> str:
    try:
        with open(LOG_FILE, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4000))
            lines = [l.strip() for l in f.read().decode("utf-8", "replace").splitlines() if l.strip()]
        return " | ".join(lines[-2:])[-chars:]
    except OSError:
        return ""


def kill_port() -> None:
    """Para lo que escuche en el puerto del LLM: el proceso lanzado por nosotros, el del archivo de PID (ejecutar.py) o cualquier otro."""
    global _proc
    pids: set[int] = set()
    if _proc is not None and _proc.poll() is None:
        pids.add(_proc.pid)
    try:
        pids.add(int(Path(PID_FILE).read_text().strip()))
    except (OSError, ValueError):
        pass
    try:
        r = subprocess.run(["lsof", f"-iTCP:{SERVER_PORT}", "-sTCP:LISTEN", "-t"], capture_output=True, text=True, timeout=8)
        pids.update(int(p) for p in r.stdout.split() if p.isdigit())
    except (OSError, subprocess.SubprocessError):
        pass
    pids.discard(os.getpid())
    for sig, wait in ((signal.SIGTERM, 3.0), (signal.SIGKILL, 0.5)):
        alive = False
        for p in pids:
            try:                                              # SOMA_355_STRATA: todo su grupo (Strata arranca strata-vision aparte)
                if os.getpgid(p) == p:
                    os.killpg(p, sig)
                else:
                    os.kill(p, sig)
                alive = True
            except OSError:
                pass
        if not alive:
            break
        time.sleep(wait)
    if _proc is not None:
        try:
            _proc.wait(timeout=3)
        except subprocess.SubprocessError:
            pass
        _proc = None
    try:
        os.remove(PID_FILE)
    except OSError:
        pass


def launch(alias: str) -> tuple[bool, str]:
    global _proc, _alias, _started
    model = next((m for m in list_models() if m["id"] == alias), None)
    if model is None:
        return False, f"no hay ningun modelo llamado '{alias}' en {MODELS_DIR}"
    if model.get("motor") == "strata":                        # SOMA_355_STRATA
        return launch_strata(alias, model)
    if not os.access(LLAMA_SERVER_BIN, os.X_OK):
        return False, f"no encuentro llama-server en {LLAMA_SERVER_BIN} (variable LLAMA_SERVER_BIN)"
    args = [
        LLAMA_SERVER_BIN,
        "-m", model["ruta"],
        "--alias", alias,
        "--host", "0.0.0.0",          # imprescindible: si no, la LAN recibe RST
        "--port", str(SERVER_PORT),
        "-ngl", "all",
        "--split-mode", "none",
        "--flash-attn", "on",
        "-c", str(CTX),
        "-n", str(PREDICT),
        "-np", "1",
        "-ctk", "q8_0", "-ctv", "q8_0",
        "--threads", str(THREADS),
    ]
    if model.get("mmproj"):                                   # SOMA_355_STRATA: con su proyector, el modelo ve imagenes
        args += ["--mmproj", model["mmproj"]]
    if EXTRA_ARGS:
        args += shlex.split(EXTRA_ARGS)
    with _lock:
        kill_port()
        env = dict(os.environ, CUDA_VISIBLE_DEVICES=CUDA_DEVICE)
        try:
            log = open(LOG_FILE, "wb")
            _proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env, start_new_session=True)
            log.close()
        except OSError as e:
            _proc = None
            return False, f"no se pudo lanzar llama-server: {e}"
        Path(PID_FILE).write_text(f"{_proc.pid}\n")
        _alias, _started = alias, time.time()
    info(f"llama-server lanzado con '{alias}' ({model['archivo']}), pid {_proc.pid}")
    return True, ""


def strata_ajenos() -> list[int]:
    """SOMA_355_STRATA: procesos strata o strata-vision de este usuario que no lanzo el lanzador (p. ej. el script del Mac en el
    puerto 18081). Ocupan las GPU: lanzar otro encima acabaria sin memoria."""
    mine: set[int] = set()
    if _proc is not None and _proc.poll() is None:
        try:
            mine.add(os.getpgid(_proc.pid))
        except OSError:
            pass
    out = []
    for name in ("strata", "strata-vision"):
        try:
            r = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", name], capture_output=True, text=True, timeout=5)
        except (OSError, subprocess.SubprocessError):
            continue
        for p in r.stdout.split():
            if not p.isdigit():
                continue
            try:
                if os.getpgid(int(p)) in mine:
                    continue
            except OSError:
                continue
            out.append(int(p))
    return out


def launch_strata(alias: str, model: dict) -> tuple[bool, str]:
    """SOMA_355_STRATA: strata serve con el perfil, escuchando en la red local en el puerto de siempre del LLM."""
    global _proc, _alias, _started
    exe = strata_bin()
    if not exe:
        return False, f"no encuentro Strata ({STRATA_BIN}) en el PATH, ~/.local/bin ni ~/bin (variable STRATA_BIN)"
    args = [exe, "serve", "--profile", model["ruta"], "--host", "0.0.0.0", "--port", str(SERVER_PORT), "--parallel", "1"]
    if STRATA_EXTRA_ARGS:
        args += shlex.split(STRATA_EXTRA_ARGS)
    with _lock:
        kill_port()                                           # lo que hubiera en el puerto (llama-server u otro Strata nuestro)
        otros = strata_ajenos()
        if otros:
            return False, ("ya hay un Strata en marcha en el servidor (pid " + ", ".join(map(str, otros[:4])) +
                           "), quiza el del Mac en el puerto 18081: detenlo primero (Control+C en su terminal)")
        env = dict(os.environ)                                # sin CUDA_VISIBLE_DEVICES: el perfil reparte las GPU
        env.pop("CUDA_VISIBLE_DEVICES", None)
        env["PATH"] = os.pathsep.join([os.path.dirname(exe), os.path.expanduser("~/.local/bin"), os.path.expanduser("~/bin"), env.get("PATH", "")])
        try:
            log = open(LOG_FILE, "wb")
            _proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env, start_new_session=True)
            log.close()
        except OSError as e:
            _proc = None
            return False, f"no se pudo lanzar Strata: {e}"
        Path(PID_FILE).write_text(f"{_proc.pid}\n")
        _alias, _started = alias, time.time()
    info(f"Strata lanzado con '{alias}' ({os.path.basename(model['ruta'])}), pid {_proc.pid}")
    return True, ""


def status() -> dict:
    h = health()
    if h == 200:
        return {"estado": "listo", "modelo": loaded_model() or _alias, "detalle": ""}
    with _lock:
        ours = _proc is not None
        dead = ours and _proc.poll() is not None
    if h == 503 or (ours and not dead):
        return {"estado": "cargando", "modelo": _alias, "detalle": f"{int(time.time() - _started)} s" if ours else ""}
    if dead:
        return {"estado": "error", "modelo": _alias, "detalle": "el servidor del modelo ha terminado: " + (log_tail() or "sin registro")}
    return {"estado": "parado", "modelo": "", "detalle": ""}


# ---------------------------------------------------------------- SOMA_278_EMBEDDINGS: servidor de embeddings (memoria de SOMA)

def emb_bin() -> str:
    if EMB_BIN:
        return EMB_BIN
    opt = "/opt/llama.cpp/build/bin/llama-server"
    return opt if os.access(opt, os.X_OK) else LLAMA_SERVER_BIN


def emb_model() -> str:
    if EMB_MODEL:
        return EMB_MODEL if os.path.isfile(EMB_MODEL) else ""
    found = []
    for root, _dirs, files in os.walk(MODELS_DIR, followlinks=True):
        for fn in files:
            if fn.lower().endswith(".gguf") and "bge-m3" in fn.lower():
                found.append(os.path.join(root, fn))
    q8 = [f for f in found if "q8" in os.path.basename(f).lower()]
    return sorted(q8 or found)[0] if found else ""


def emb_health() -> int | None:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{EMB_PORT}/health", timeout=2.0) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code
    except Exception:
        return None


def emb_log_tail(chars: int = 150) -> str:
    try:
        with open(EMB_LOG_FILE, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4000))
            lines = [l.strip() for l in f.read().decode("utf-8", "replace").splitlines() if l.strip()]
        return " | ".join(lines[-2:])[-chars:]
    except OSError:
        return ""


def emb_kill() -> None:
    """Para SOLO el servidor de embeddings (su proceso, su archivo de PID y lo que escuche en su puerto); nunca toca al del LLM."""
    global _eproc
    pids: set[int] = set()
    if _eproc is not None and _eproc.poll() is None:
        pids.add(_eproc.pid)
    try:
        pids.add(int(Path(EMB_PID_FILE).read_text().strip()))
    except (OSError, ValueError):
        pass
    try:
        r = subprocess.run(["lsof", f"-iTCP:{EMB_PORT}", "-sTCP:LISTEN", "-t"], capture_output=True, text=True, timeout=8)
        pids.update(int(p) for p in r.stdout.split() if p.isdigit())
    except (OSError, subprocess.SubprocessError):
        pass
    pids.discard(os.getpid())
    for sig, wait in ((signal.SIGTERM, 2.0), (signal.SIGKILL, 0.5)):
        alive = False
        for p in pids:
            try:
                os.kill(p, sig)
                alive = True
            except OSError:
                pass
        if not alive:
            break
        time.sleep(wait)
    if _eproc is not None:
        try:
            _eproc.wait(timeout=3)
        except subprocess.SubprocessError:
            pass
        _eproc = None
    try:
        os.remove(EMB_PID_FILE)
    except OSError:
        pass


def emb_launch() -> tuple[bool, str]:
    global _eproc, _estarted
    if EMB_PORT == SERVER_PORT:
        return False, f"el puerto de embeddings ({EMB_PORT}) es el mismo que el del LLM: cambia SOMA_EMB_PORT"
    model = emb_model()
    if not model:
        return False, f"no encuentro ningun bge-m3*.gguf bajo {MODELS_DIR} (o fija SOMA_EMB_MODEL)"
    binary = emb_bin()
    if not os.access(binary, os.X_OK):
        return False, f"no encuentro llama-server en {binary} (variable SOMA_EMB_BIN)"
    args = [binary, "-m", model, "--alias", EMB_ALIAS, "--embeddings", "--pooling", "cls", "-ngl", "0", "-c", str(EMB_CTX),
            "--threads", str(EMB_THREADS), "--host", "0.0.0.0", "--port", str(EMB_PORT)]
    if EMB_EXTRA_ARGS:
        args += shlex.split(EMB_EXTRA_ARGS)
    with _lock:
        emb_kill()
        env = dict(os.environ, CUDA_VISIBLE_DEVICES="")           # en CPU: no quita ni un MiB a las GPU del LLM
        try:
            log = open(EMB_LOG_FILE, "wb")
            _eproc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env, start_new_session=True)
            log.close()
        except OSError as e:
            _eproc = None
            return False, f"no se pudo lanzar llama-server (embeddings): {e}"
        Path(EMB_PID_FILE).write_text(f"{_eproc.pid}\n")
        _estarted = time.time()
    info(f"servidor de embeddings lanzado con {os.path.basename(model)}, pid {_eproc.pid}, puerto {EMB_PORT}")
    return True, ""


def emb_status() -> dict:
    h = emb_health()
    if h == 200:
        return {"estado": "listo", "modelo": EMB_ALIAS, "detalle": ""}
    with _lock:
        ours = _eproc is not None
        dead = ours and _eproc.poll() is not None
    if h == 503 or (ours and not dead):
        return {"estado": "cargando", "modelo": EMB_ALIAS, "detalle": f"{int(time.time() - _estarted)} s" if ours else ""}
    if dead:
        return {"estado": "error", "modelo": EMB_ALIAS, "detalle": "llama-server (embeddings) ha terminado: " + (emb_log_tail() or "sin registro")}
    return {"estado": "parado", "modelo": "", "detalle": ""}


# ---------------------------------------------------------------- HTTP

def client_allowed(addr: str) -> bool:
    try:
        ip = ipaddress.ip_address(addr.split("%")[0])
    except ValueError:
        return False
    if getattr(ip, "ipv4_mapped", None):
        ip = ip.ipv4_mapped
    return ip.is_loopback or ip.is_private or ip.is_link_local


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "soma-lanzador"

    def _send(self, code: int, obj: dict) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def _ok(self) -> bool:
        if not client_allowed(self.client_address[0]):
            self._send(403, {"error": "el lanzador solo atiende a equipos de la red local"})
            return False
        got = self.headers.get("Authorization", "")
        if not hmac.compare_digest(got.encode(), ("Bearer " + self.server.key).encode()):
            time.sleep(1.0)                           # frena los intentos de adivinar la clave
            self._send(401, {"error": "clave del lanzador incorrecta"})
            return False
        return True

    def do_GET(self) -> None:
        if not self._ok():
            return
        if self.path == "/v1/estado":
            self._send(200, status())
        elif self.path == "/v1/embeddings/estado":
            self._send(200, emb_status())
        elif self.path == "/v1/modelos":
            self._send(200, {"data": [{k: m.get(k) for k in ("id", "archivo", "gib", "motor", "vision")} for m in list_models()]})
        else:
            self._send(404, {"error": "no existe"})

    def do_POST(self) -> None:
        try:
            n = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            n = -1
        if n < 0 or n > 4096:
            self._send(400, {"error": "peticion no valida"})
            return
        raw = self.rfile.read(n)
        if not self._ok():
            return
        if self.path == "/v1/parar":
            with _lock:
                kill_port()
            info("llama-server parado a peticion de " + self.client_address[0])
            self._send(200, {"ok": True})
            return
        if self.path == "/v1/embeddings/parar":              # SOMA_278_EMBEDDINGS
            with _lock:
                emb_kill()
            info("servidor de embeddings parado a peticion de " + self.client_address[0])
            self._send(200, {"ok": True})
            return
        if self.path == "/v1/embeddings/lanzar":
            ok, why = emb_launch()
            self._send(200 if ok else 500, {"ok": True, "modelo": EMB_ALIAS} if ok else {"error": why})
            return
        if self.path != "/v1/lanzar":
            self._send(404, {"error": "no existe"})
            return
        try:
            alias = json.loads(raw or b"{}").get("modelo", "")
        except (ValueError, AttributeError):
            alias = ""
        if not isinstance(alias, str) or not alias:
            self._send(400, {"error": "falta el modelo"})
            return
        ok, why = launch(alias)
        self._send(200 if ok else 500, {"ok": True, "modelo": alias} if ok else {"error": why})

    def log_message(self, *a) -> None:
        pass


UNIT = """[Unit]
Description=Lanzador de llama-server para SOMA
After=network-online.target

[Service]
ExecStart={python} {script}
Restart=on-failure
{env}
[Install]
WantedBy=default.target
"""


def install_service() -> int:
    unit_dir = Path.home() / ".config" / "systemd" / "user"
    unit_dir.mkdir(parents=True, exist_ok=True)
    names = ("SERVER_PORT", "SERVER_PORT_4090", "LLAMA_SERVER_BIN", "MODELS_DIR", "REMOTE_MODEL_PATH", "REMOTE_MODEL_ALIAS", "SOMA_PID_FILE",
             "SOMA_LOG_FILE", "REMOTE_CTX", "REMOTE_THREADS", "REMOTE_PREDICT", "CUDA_DEVICE", "LLAMA_EXTRA_ARGS", "SOMA_LANZADOR_PORT",
             "SOMA_LANZADOR_CLAVE", "LD_LIBRARY_PATH", "SOMA_EMB_PORT", "SOMA_EMB_BIN", "SOMA_EMB_MODEL", "SOMA_EMB_THREADS",
             "SOMA_EMB_CTX", "SOMA_EMB_EXTRA_ARGS", "SOMA_EMB_PID_FILE", "SOMA_EMB_LOG_FILE", "STRATA_BIN", "STRATA_PERFILES", "STRATA_PERFIL", "STRATA_EXTRA_ARGS")
    env = "".join(f'Environment="{n}={os.environ[n]}"\n' for n in names if n in os.environ)   # conserva la configuracion de ahora
    unit = unit_dir / "soma-lanzador.service"
    unit.write_text(UNIT.format(python=sys.executable, script=Path(__file__).resolve(), env=env))
    print(f"Servicio escrito en {unit}\n\nPara activarlo (una sola vez):\n"
          "  systemctl --user daemon-reload\n"
          "  systemctl --user enable --now soma-lanzador\n"
          f"  sudo loginctl enable-linger {os.environ.get('USER', '$USER')}     # que arranque con el equipo, sin iniciar sesion\n\n"
          f"Clave para SOMA (Ajustes > IA > Lanzador, o  llm-config lanzador clave ...):  {read_key()}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Lanzador de llama-server para SOMA (se ejecuta en el servidor)")
    ap.add_argument("--clave", action="store_true", help="muestra la clave y sale")
    ap.add_argument("--nueva-clave", action="store_true", help="cambia la clave y sale")
    ap.add_argument("--instalar-servicio", action="store_true", help="instala el servicio de usuario de systemd")
    a = ap.parse_args()
    if a.nueva_clave:
        print(new_key())
        return 0
    if a.clave:
        print(read_key())
        return 0
    if a.instalar_servicio:
        return install_service()
    key = read_key()
    try:
        srv = ThreadingHTTPServer((LANZADOR_HOST, LANZADOR_PORT), Handler)
    except OSError as e:
        print(f"[LANZADOR] no puedo escuchar en el puerto {LANZADOR_PORT}: {e}", file=sys.stderr)
        return 1
    srv.key = key
    info(f"escuchando en el puerto {LANZADOR_PORT}; llama-server ira en el {SERVER_PORT}")
    info(f"modelos en {MODELS_DIR}: {len(list_models())}; llama-server: {LLAMA_SERVER_BIN}")
    em = emb_model()
    info(f"embeddings (memoria de SOMA): {os.path.basename(em) if em else 'NO hay bge-m3*.gguf'} en el puerto {EMB_PORT}")
    info(f"CLAVE para SOMA (Ajustes > IA > Lanzador, o  llm-config lanzador clave ...):  {key}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
