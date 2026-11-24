#!/usr/bin/env python3
"""tools/exporta_para_claude.py (SOMA_262_GITHUB) - prepara un .txt para pasarselo a Claude cuando hay que hacer un parche.

Desde la raiz de SOMA:

    python3 tools/exporta_para_claude.py                      lo cambiado desde el ultimo commit (lo mas util casi siempre)
    python3 tools/exporta_para_claude.py --tema lcm           un tema (--temas los lista); se pueden poner varios: --tema voz,orbe
    python3 tools/exporta_para_claude.py src/ui/gui.c "src/lang/*.c"     archivos o patrones concretos
    python3 tools/exporta_para_claude.py --tema llm --cambiados          se combinan
    python3 tools/exporta_para_claude.py --funcion lang_compile          y el archivo donde se define una funcion

Siempre lleva una cabecera con el estado del repositorio (rama, ultimos commits, que hay sin subir), los parches aplicados (con
respaldo), la tabla de ordenes del terminal y los archivos de mas de 20 MB. Crea para_claude_AAAAMMDD_HHMM.txt (no se sube a
GitHub). No incluye nunca .conf, claves ni binarios; tapa con [OCULTO] tokens de API y credenciales dentro de URL.

Es distinto de exportar_txt.py (el proyecto entero por partes): este es para una conversacion concreta."""
import argparse
import glob
import os
import re
import subprocess
import sys
from datetime import datetime

MAX_LINEAS = 8000
SECRETO = re.compile(r"(gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-ant-[A-Za-z0-9_\-]{20,}|sk-(proj-)?[A-Za-z0-9_\-]{20,}"
                     r"|AIza[0-9A-Za-z_\-]{30,}|xox[abprs]-[A-Za-z0-9\-]{10,})")
URL_CRED = re.compile(r"(https?://)[^/\s:@]+(:[^/\s@]*)?@")
CLAVE_PRIVADA = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----.*?-----END [A-Z ]*PRIVATE KEY-----", re.S)
NUNCA = (".conf", ".key", ".pem", ".env", ".p12", ".pfx")
BINARIO = (".o", ".bin", ".img", ".iso", ".elf", ".efi", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".wav", ".mp3", ".ogg", ".avi",
           ".mp4", ".mkv", ".zip", ".gz", ".xz", ".7z", ".tar", ".pyc", ".pt", ".onnx", ".gguf", ".a", ".so", ".dylib", ".ttf",
           ".otf", ".psf", ".pdf", ".webp", ".raw", ".dat", ".npz", ".npy", ".safetensors", ".ucode", ".whl")
IGNORAR_DIR = {".git", "build", "out", "__pycache__", "node_modules", "toolchain", ".parche_backup", "exportacion", "modelos",
               "third_party", "isodir", "snapshots", "backups"}

TEMAS = {
    "lcm": (["src/agent/lcm_*.inc", "src/agent/agent.c", "src/agent/shell.c", "src/apps/settings.c", "tests/test_lcm_*.c",
             "tools/lcm_ordenes/*.py", "tools/lcm_ordenes/README.md", "docs/PARCHE_25[378]_LCM*.md"],
            "el LCM (traductor de peticiones a ordenes, charla, Ajustes > LCM)"),
    "voz": (["src/ui/gui_voz.c", "src/arch/x86_64/voz_sys.[ch]", "src/core/audio_sys.[ch]", "src/core/micrec.[ch]", "src/core/habla_texto.h",
             "src/user/voz/*.h", "src/user/voz/*.c", "src/user/voz/*.cpp", "src/user/voz/*.ld", "tests/test_tts_*.c", "tests/test_mic*.c"],
            "voz: dictado (whisper), lectura en voz alta (Piper), microfono y sonido"),
    "orbe": (["src/ui/gui_prompter.c", "src/ui/gui_vortice.c", "src/ui/gui_cosmos.c", "src/ui/gui_agente.c", "src/ui/gui_fotograma.c",
              "src/ui/gui.c", "src/ui/sysicon.[ch]", "tests/test_orbe_*.c"],
             "el orbe: texto de las respuestas, barra de fases, turnos del agente"),
    "llm": (["src/agent/llm.[ch]", "src/agent/llm_proto.[ch]", "src/agent/llm_limites*.inc", "src/agent/agent.[ch]", "src/net/http.[ch]",
             "src/apps/web_net.h", "tests/test_llm_*.c"],
            "el dialogo con el modelo de lenguaje (proveedores, tamanos, llm-limites)"),
    "agente": (["src/agent/agent.[ch]", "src/agent/agent_ruta.[ch]", "src/agent/shell.[ch]", "src/agent/shell_agente.c", "src/agent/lcm_soma.inc"],
               "el orquestador, las rutas de una peticion y el terminal"),
    "maquetador": (["src/agent/docagent.[ch]", "src/apps/docgen.[ch]", "src/apps/layout.[ch]", "tests/test_docgen_*.c"],
                   "documentos: redaccion, paginacion, revision y PDF"),
    "programador": (["src/apps/coder_core.[ch]", "src/apps/coder_app.[ch]", "src/apps/coder_demo.h", "tests/test_coder_*.c", "tests/stubs_lang.c"],
                    "el programador y SOMA Studio"),
    "lang": (["src/lang/*.[ch]", "src/ui/gui_lang.c", "src/user/python.c", "src/user/python/mpconfigport.h"],
             "SOMA-LANG (compilador y maquina virtual) y Python"),
    "github": (["README.md", "CHANGELOG.md", ".gitignore", ".gitattributes", "subir_github.py", ".github/workflows/*.yml", "Makefile",
                "construyeimagen.py", "imagen/LEEME.md", "tools/exporta_para_claude.py"],
               "publicacion: README, CHANGELOG, .gitignore, subir_github.py, CI"),
    "correo": (["src/apps/correo.[ch]", "src/apps/correo_core.[ch]", "src/apps/correo_red.[ch]", "src/agent/shell_correo.c",
                "tests/test_correo*_295.*", "tests/correo_servidores_295.py", "docs/PARCHE_295_CORREO.md"],
               "el correo: IMAP/SMTP, la IA del correo y el orbe"),   # SOMA_297_GITHUB
    "ayuda": (["src/ui/ayuda.h", "src/ui/gui_ayuda.c", "tests/test_ayuda*_294.c", "docs/PARCHE_294_AYUDA.md"],
              "la bienvenida del orbe y la ayuda rapida (F1)"),   # SOMA_297_GITHUB
    "compilar": (["Makefile", "ejecutar.py", "construyeimagen.py", "Dockerfile", "linker_*.ld", "protected.txt"],
                 "compilacion y arranque"),
}


def tapa(s):
    return URL_CRED.sub(r"\1[OCULTO]@", SECRETO.sub("[OCULTO]", CLAVE_PRIVADA.sub("[CLAVE PRIVADA OCULTA]", s)))


def ok_texto(r):
    b = r.lower()
    return not b.endswith(NUNCA) and not b.endswith(BINARIO) and os.path.basename(b) not in ("llm.conf", ".env")


def git(*a):
    try:
        r = subprocess.run(["git", *a], capture_output=True, text=True, timeout=60)
        return r.returncode, tapa((r.stdout + r.stderr).strip())
    except Exception as e:
        return 1, f"(no se pudo: {e})"


def volcar(out, ruta):
    if not ok_texto(ruta) or not os.path.isfile(ruta):
        return False
    try:
        if os.path.getsize(ruta) > 3_000_000:
            out.write(f"\n===== ARCHIVO: {ruta} | demasiado grande ({os.path.getsize(ruta) >> 20} MB): no se incluye =====\n")
            return False
        with open(ruta, encoding="utf-8", errors="replace") as f:
            lin = f.read().split("\n")
    except OSError:
        return False
    cortado = len(lin) > MAX_LINEAS
    out.write(f"\n===== ARCHIVO: {ruta} | {len(lin)} lineas{' (CORTADO a ' + str(MAX_LINEAS) + ')' if cortado else ''} =====\n")
    out.write(tapa("\n".join(lin[:MAX_LINEAS])))
    out.write(f"\n===== FIN: {ruta} =====\n")
    return True


def recorre():
    for raiz, dirs, fs in os.walk("."):
        dirs[:] = [d for d in dirs if d not in IGNORAR_DIR and not (d.startswith(".parche_") and d.endswith("_respaldo"))]
        for f in fs:
            yield os.path.relpath(os.path.join(raiz, f))


def cambiados():
    """lo modificado desde el ultimo commit y lo nuevo sin ignorar (como lo veria git add -A)"""
    try:                                                   # sin strip: la primera columna puede ser un espacio
        r = subprocess.run(["git", "status", "--porcelain", "-z", "--untracked-files=all"], capture_output=True, text=True, timeout=60)
    except Exception:
        return []
    if r.returncode != 0:
        return []
    s = r.stdout
    res = []
    for e in s.split("\0"):
        if len(e) > 3 and e[:2] != " D" and e[0] != "D":
            r = e[3:]
            if os.path.isfile(r):
                res.append(r)
    return res


def definida_en(funciones):
    if not funciones:
        return {}
    rx = re.compile(r"^[A-Za-z_][\w \*]*\b(" + "|".join(map(re.escape, funciones)) + r")\s*\([^;]*$")
    d = {}
    for r in recorre():
        if not r.startswith("src/") or not r.endswith((".c", ".h", ".inc", ".cpp")):
            continue
        try:
            for i, ln in enumerate(open(r, encoding="utf-8", errors="replace"), 1):
                m = rx.match(ln)
                if m:
                    d.setdefault(m.group(1), []).append((r, i))
        except OSError:
            pass
    return d


def main():
    ap = argparse.ArgumentParser(description="Prepara un .txt para Claude (sin claves ni binarios)")
    ap.add_argument("rutas", nargs="*", help="archivos o patrones (src/lang/*.c)")
    ap.add_argument("--tema", help="uno o varios temas separados por comas (--temas los lista)")
    ap.add_argument("--temas", action="store_true", help="lista los temas")
    ap.add_argument("--cambiados", action="store_true", help="lo cambiado desde el ultimo commit (por defecto si no se pide nada)")
    ap.add_argument("--funcion", action="append", default=[], help="incluye el archivo donde se define esta funcion (se puede repetir)")
    ap.add_argument("-o", "--salida", help="nombre del archivo (por defecto para_claude_FECHA.txt)")
    a = ap.parse_args()
    if a.temas:
        for k, (_, d) in TEMAS.items():
            print(f"  {k:12s} {d}")
        return
    if not os.path.isfile("src/agent/agent.c"):
        sys.exit("Ejecutalo desde la raiz de SOMA (donde esta src/agent/agent.c).")
    temas = [t.strip() for t in (a.tema or "").split(",") if t.strip()]
    malos = [t for t in temas if t not in TEMAS]
    if malos:
        sys.exit("Tema desconocido: " + ", ".join(malos) + ". Usa --temas para verlos.")
    pedir_cambiados = a.cambiados or not (temas or a.rutas or a.funcion)
    rutas = []
    for t in temas:
        for p in TEMAS[t][0]:
            rutas += sorted(glob.glob(p))
    for p in a.rutas:
        g = sorted(glob.glob(p))
        if not g and not os.path.exists(p):
            print("Aviso: no existe " + p)
        for x in g:
            rutas += sorted(recorre_dir(x)) if os.path.isdir(x) else [x]
    defs = definida_en(a.funcion)
    for f in a.funcion:
        rutas += [r for r, _ in defs.get(f, [])]
    if pedir_cambiados:
        rutas += cambiados()
    salida = a.salida or datetime.now().strftime("para_claude_%Y%m%d_%H%M.txt")
    vistos, n = set(), 0
    with open(salida, "w", encoding="utf-8") as out:
        out.write("########## SOMA -> Claude ##########\n")
        out.write(f"pedido: {' '.join(sys.argv[1:]) or '(lo cambiado desde el ultimo commit)'}\n")
        out.write("\n===== GIT =====\n")
        for t, args in (("rama", ("branch", "--show-current")), ("ultimos commits", ("log", "--oneline", "-12")),
                        ("ultima etiqueta", ("describe", "--tags", "--abbrev=0")), ("sin subir / sin confirmar", ("status", "--short"))):
            c, s = git(*args)
            ls = s.split("\n")
            out.write(f"--- {t}:\n" + "\n".join(ls[:200]) + (f"\n... ({len(ls) - 200} mas)" if len(ls) > 200 else "") + "\n")
        out.write("\n===== PARCHES APLICADOS CON RESPALDO (los .py de parche_NNN) =====\n")
        for d in sorted(os.listdir(".")):
            if d.startswith(".parche_") and d.endswith("_respaldo") and os.path.isfile(os.path.join(d, "manifiesto.json")):
                out.write(d + "\n")
        if os.path.isfile("parches/APLICADOS.txt"):
            out.write("\n===== parches/APLICADOS.txt (ultimas 25 lineas) =====\n")
            out.write(tapa("\n".join(open("parches/APLICADOS.txt", encoding="utf-8", errors="replace").read().splitlines()[-25:])) + "\n")
        out.write("\n===== ARCHIVOS DE MAS DE 20 MB =====\n")
        for r in recorre():
            try:
                tam = os.path.getsize(r)
            except OSError:
                continue
            if tam >= 20_000_000:
                c, s = git("check-ignore", "-q", r)
                out.write(f"{tam / 1e6:.1f} MB  {r}  ({'ignorado' if c == 0 else 'NO ignorado: se subiria'})\n")
        if a.funcion:
            out.write("\n===== FUNCIONES =====\n")
            for f in a.funcion:
                out.write(f"{f}: " + (", ".join(f"{r}:{i}" for r, i in defs[f]) if f in defs else "(no encontrada)") + "\n")
        out.write("\n===== TABLA DE ORDENES (shell.c) =====\n")
        try:
            s = open("src/agent/shell.c", encoding="utf-8", errors="replace").read()
            for m in re.finditer(r'^\s*\{\s*"[^"]+",\s*cmd_\w+,.*$', s, re.M):
                out.write(m.group(0).strip()[:240] + "\n")
        except OSError:
            pass
        for r in rutas:
            r = os.path.normpath(r)
            if r not in vistos and volcar(out, r):
                vistos.add(r); n += 1
    tam = os.path.getsize(salida)
    print(f"Creado {salida}: {tam / 1e6:.1f} MB, {n} archivos" + (" (lo cambiado desde el ultimo commit)" if pedir_cambiados else ""))
    if tam > 20_000_000:
        print("Es muy grande: mejor por temas (--tema) o archivos concretos.")
    print("No lleva claves ni binarios. Pasaselo a Claude.")


def recorre_dir(d):
    for raiz, dirs, fs in os.walk(d):
        dirs[:] = [x for x in dirs if x not in IGNORAR_DIR]
        for f in fs:
            yield os.path.join(raiz, f)


if __name__ == "__main__":
    main()
