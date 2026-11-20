#!/usr/bin/env python3
"""exportar_txt.py - exporta el proyecto SOMA a archivos .txt para pasarselo a un asistente (Claude) en una conversacion.

Uso (desde la raiz del proyecto):
    python3 exportar_txt.py                     todo el codigo propio, en partes de ~400 KB
    python3 exportar_txt.py --indice            solo el MAPA del proyecto: archivos, lineas y para que sirve cada uno (~60 KB)
    python3 exportar_txt.py --indice --funciones    el mapa con las funciones de cada archivo (~200 KB)
    python3 exportar_txt.py --tema escritorio   un tema completo (ver --temas): escritorio, navegador, nucleo, consola, lenguaje...
    python3 exportar_txt.py src/ui src/apps/settings.c     solo esas carpetas o archivos (admite comodines: 'src/agent/shell_*.c')
    python3 exportar_txt.py --cambiados         solo lo que ha cambiado desde la ultima exportacion completa (o desde git HEAD)

Como usarlo con el asistente: el proyecto completo son ~6 MB de codigo (~1,8 millones de tokens), mas de lo que cabe en una
conversacion. Lo practico es pasar primero el indice (--indice) y despues el tema o los archivos de la tarea; tras cambios, --cambiados.
Opciones:
    --tamano KB        tamano maximo de cada parte (por defecto 400; ~110 000 tokens)
    --salida CARPETA   donde se escriben las partes (por defecto exportacion/; se vacia antes)
    --con-datos        incluye los datos generados (fuentes en mapas de bits, certificados, imagenes de prueba...)
    --con-terceros     incluye las bibliotecas de terceros (third_party/, simplewebp.h)
    --con-docs         incluye docs/ (notas de cada parche, plan del T480s...)
    --con-pruebas      incluye tests/ en la exportacion completa (en un tema o con rutas se incluyen si se piden)
    --temas            lista los temas disponibles

Que se exporta y que no:
  - Codigo fuente propio (.c .h .s .inc .ld .py .cfg Makefile Dockerfile .md de la raiz, .soma, .yml) con su ruta, lineas y huella.
  - Nunca: binarios (imagenes de disco, fuentes .ttf, firmware), build/, copias de seguridad, parches ya aplicados, la carpeta de
    la imagen y la propia exportacion.
  - Secretos: no se exportan archivos de claves (llm.conf, .env, *.key, *.pem privados) y cualquier cadena con forma de clave de API
    (sk-..., sk-ant-..., AIza..., ghp_..., Bearer ...) se sustituye por [CLAVE OCULTA]. El resumen dice cuantas se ocultaron.
  - Los archivos de datos generados y las bibliotecas de terceros aparecen en el indice con su tamano, sin contenido (salvo opcion).

Cada parte empieza con una cabecera y cada archivo va entre marcas faciles de localizar:
    ===== ARCHIVO: src/ui/gui.c | 1646 lineas | sha256 1a2b3c4d5e6f =====
    ...contenido...
    ===== FIN: src/ui/gui.c =====
Un archivo mas grande que una parte se trocea por lineas ("trozo 1/3"). La parte 1 lleva ademas el indice completo.
"""
import argparse, fnmatch, hashlib, json, os, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MANIFEST = ".manifiesto_exportacion.json"

SKIP_DIRS = {"build", ".git", ".soma_parches", ".parche_backup", ".parches_backup", "backups", "parches", "exportacion", "imagen",
             "__pycache__", "snapshots", "isodir", "build_backup_src", ".vscode", ".idea", "firmware", "workspace", "node_modules"}
SKIP_GLOBS = ["parche_*.py", "parches_*", "subir_github.py", "*.img", "*.img.xz", "*.iso", "*.whl", "*.ucode", "*.ttf", "*.otf",
              "*.bmp", "*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp", "*.pdf", "*.avi", "*.wav", "*.o", "*.elf", "*.bin", "*.pyc",
              "*.log", "*.pcap", "*.sha256", ".DS_Store", "._*", "*~", "*.swp", "hdd.img*", "estructura*.txt"]
TEXT_EXT = {".c", ".h", ".s", ".S", ".inc", ".ld", ".py", ".cfg", ".md", ".txt", ".soma", ".yml", ".yaml", ".json", ".js", ".sh",
            ".html", ".css", ".toml", ".ini", ".conf"}
TEXT_NAMES = {"Makefile", "Dockerfile", "LICENSE", ".gitignore", "protected.txt"}
SECRET_NAMES = {"llm.conf", ".env", "credentials.json", "secrets.json", "wifi.conf", "redes_wifi.conf"}
SECRET_GLOBS = ["*.key", "*_key", "*.p12", "*.pfx", "id_rsa*", "*.pem"]
# datos generados: aparecen en el indice, sin contenido (salvo --con-datos)
DATA_FILES = {"src/fonts/vfont_data.h", "src/ui/soma_fonts.c", "src/core/tls_roots.c", "src/arch/x86_64/sbx_testimg.h",
              "tests/x509_vectors_194.h"}
THIRD_PARTY = ["third_party/*", "src/apps/simplewebp.h"]
# temas: conjuntos de archivos que suelen hacer falta juntos
BASE = ["Makefile", "README.md", "CHANGELOG.md", "src/lang.h", "src/core/arch.h"]
TEMAS = {
    "nucleo":     ("arranque, memoria, hilos, interrupciones y drivers", ["src/core/*", "src/arch/*", "src/drivers/*", "linker_*.ld", "grub/*"]),
    "escritorio": ("escritorio, ventanas, orbe, Ajustes y fuentes de la interfaz", ["src/ui/*", "src/apps/settings.*", "src/apps/sysmon.*",
                   "src/drivers/fb.*", "src/drivers/mouse.*", "src/fonts/vfont.h", "src/fonts/vicon.h", "src/apps/web_font.*"]),
    "navegador":  ("navegador web: HTML, CSS, JavaScript, imagenes y red", ["src/apps/web_*", "src/apps/browser.*", "src/user/websvc.c",
                   "src/user/unet.c", "src/net/*", "src/core/tls*", "src/core/x509.*"]),
    "consola":    ("consola, ordenes y agentes", ["src/agent/*", "src/ui/console.*"]),
    "lenguaje":   ("SOMA-LANG y el programador (SOMA Studio)", ["src/lang/*", "src/apps/coder_*", "src/apps/codeedit.*", "tests/test_lang.c",
                   "tests/test_coder_*.c"]),
    "red":        ("TCP/IP, DHCP, DNS, HTTP, WiFi y Ethernet", ["src/net/*", "src/drivers/virtio_net.*", "src/drivers/e1000e.*",
                   "src/arch/x86_64/iwl*", "src/core/wpa.*", "src/core/ccmp.*"]),
    "archivos":   ("sistemas de archivos y disco", ["src/fs/*", "src/drivers/virtio_blk.*", "src/drivers/sysdisk.h", "src/arch/x86_64/usbdisk*",
                   "src/core/fat.*"]),
    "apps":       ("aplicaciones: notas, imagenes, PDF, archivos, paint, camara, grabadora", ["src/apps/notepad.*", "src/apps/imgview.*",
                   "src/apps/image.*", "src/apps/pdf*", "src/apps/fm.*", "src/apps/paint.*", "src/apps/camapp.*", "src/apps/recorder.*",
                   "src/apps/capapp.*", "src/apps/media.*", "src/apps/memview.*", "src/apps/layout.*", "src/apps/savedlg.*", "src/apps/tex.*"]),
    "python":     ("Python en SOMA (MicroPython en ring 3): interprete, llamadas al sistema y orden python", ["src/user/python.c",
                   "src/user/python/*", "src/user/ulib.h", "src/arch/x86_64/usermode.*", "src/agent/shell_procesos.c", "src/ui/gui_agente.c",
                   "third_party/micropython/LEEME_SOMA.md", "tests/prueba_python_237.py",
                   "tests/prueba_python_239.py", "tests/test_coder_py_239.c", "src/apps/coder_core.*", "src/ui/gui_lang.c"]),
    "t480s":      ("portatil ThinkPad T480s: ACPI, energia, teclado, USB, audio, WiFi, arranque", ["src/arch/x86_64/*", "src/ui/console.c",
                   "construyeimagen.py", "ejecutar.py", "grub/*"]),
    "herramientas": ("scripts del anfitrion y pruebas", ["*.py", "tools/*", "tests/*", "Makefile", "Dockerfile"]),
}
SECRET_RE = re.compile(r"(sk-ant-[A-Za-z0-9_\-]{16,}|sk-[A-Za-z0-9_\-]{20,}|AIza[0-9A-Za-z_\-]{30,}|gh[pousr]_[A-Za-z0-9]{30,}"
                       r"|xox[abpr]-[A-Za-z0-9\-]{10,}|(?i:bearer)\s+[A-Za-z0-9_\-\.=]{20,})")


def rel(p):
    return p.relative_to(ROOT).as_posix()


def is_text_file(p):
    if p.name in TEXT_NAMES or p.suffix in TEXT_EXT:
        try:
            b = p.read_bytes()[:65536]
        except OSError:
            return False
        if b"\x00" in b:
            return False
        try:
            b.decode("utf-8")
        except UnicodeDecodeError:
            try:
                p.read_bytes().decode("latin-1")
            except Exception:
                return False
        return True
    return False


def looks_like_data(text):
    """Arrays enormes de numeros (tablas, mapas de bits): mucho digito/hex/coma y poco identificador."""
    if len(text) < 20000:
        return False
    sample = text[:200000]
    nums = len(re.findall(r"\b(?:0x[0-9a-fA-F]+|\d+)\b", sample))
    words = len(re.findall(r"\b[A-Za-z_][A-Za-z_0-9]{2,}\b", sample))
    return nums > 6 * max(words, 1)


def classify(p, opts):
    """(incluir_contenido, motivo_si_no) para un archivo de texto"""
    r = rel(p)
    if p.name in SECRET_NAMES or any(fnmatch.fnmatch(p.name, g) for g in SECRET_GLOBS):
        return False, "posibles secretos: no se exporta"
    if not opts.con_terceros and any(fnmatch.fnmatch(r, g) for g in THIRD_PARTY):
        return False, "biblioteca de terceros (--con-terceros para incluirla)"
    if not opts.con_docs and r.startswith("docs/"):
        return False, "documentacion (--con-docs para incluirla)"
    if not opts.con_datos:
        if r in DATA_FILES:
            return False, "datos generados (--con-datos para incluirlos)"
        try:
            t = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return False, "no se pudo leer"
        if looks_like_data(t):
            return False, "tabla de datos (--con-datos para incluirla)"
    return True, ""


SKIP_TOP = set()


def walk():
    out = []
    for dp, dn, fn in os.walk(ROOT):
        d = Path(dp)
        dn[:] = sorted(x for x in dn if x not in SKIP_DIRS and not (d == ROOT and (x in SKIP_TOP or (x.startswith(".") and x != ".github"))))
        for f in sorted(fn):
            p = d / f
            if any(fnmatch.fnmatch(f, g) for g in SKIP_GLOBS):
                continue
            if p.is_symlink() or not p.is_file():
                continue
            out.append(p)
    return out


def select(files, pats):
    if not pats:
        return files
    sel = []
    for p in files:
        r = rel(p)
        for pat in pats:
            pat = pat.rstrip("/")
            if r == pat or r.startswith(pat + "/") or fnmatch.fnmatch(r, pat):
                sel.append(p)
                break
    return sel


def read_clean(p):
    t = p.read_bytes()
    try:
        s = t.decode("utf-8")
    except UnicodeDecodeError:
        s = t.decode("latin-1")
    s = s.replace("\r\n", "\n")
    s, n = SECRET_RE.subn("[CLAVE OCULTA]", s)
    return s, n


def sha(s):
    return hashlib.sha256(s.encode("utf-8")).hexdigest()[:12]


# ------------------------------------------------------------------ indice: lo que hay en cada archivo
C_FUNC = re.compile(r"^(?!\s)(?!#)(?!typedef)(?!return)[A-Za-z_][\w\s\*,]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(\{.*)?$")
C_KW = {"if", "while", "for", "switch", "return", "sizeof", "defined", "else"}


def summarize(path, text):
    r = path
    lines = text.split("\n")
    names = []
    if r.endswith((".c", ".h", ".inc")):
        for l in lines:
            if l.rstrip().endswith(";") or l.startswith((" ", "\t")):
                continue
            m = C_FUNC.match(l)
            if m and m.group(1) not in C_KW:
                names.append(m.group(1) + "()")
        defs = sum(1 for l in lines if l.startswith("#define "))
        if defs:
            names.append(f"[{defs} #define]")
    elif r.endswith(".py"):
        names = [m.group(1) + ("()" if m.group(0).startswith("def") else "") for m in re.finditer(r"^(?:def|class)\s+(\w+)", text, re.M)]
    elif r.endswith(".md"):
        names = [m.group(1).strip() for m in re.finditer(r"^#{1,3}\s+(.+)$", text, re.M)][:30]
    elif r.endswith(".soma"):
        names = [m.group(1) + "()" for m in re.finditer(r"^\s*fn\s+(\w+)", text, re.M)]
    first = ""
    for l in lines[:15]:                                   # la primera linea de comentario dice para que sirve el archivo
        s = l.strip().lstrip("/*#").strip()
        if s and not s.startswith(("include", "ifndef", "define", "!")) and len(s) > 12:
            first = s[:150]
            break
    return first, names


def build_index(entries, omitted, title, funcs=True):
    out = [f"===== INDICE: {title} =====",
           f"{len(entries)} archivos con contenido, {len(omitted)} solo listados. Formato: ruta | lineas | KB | para que sirve | funciones", ""]
    cur = None
    for e in entries:
        d = e["path"].rsplit("/", 1)[0] if "/" in e["path"] else "."
        if d != cur:
            out.append(f"[{d}/]")
            cur = d
        out.append(f"  {e['path']} | {e['lines']} lin | {e['size'] // 1024} KB | {e['first']}")
        if funcs and e["names"]:
            s, row = "      ", ""
            for n in e["names"]:
                if len(row) + len(n) > 140:
                    out.append(s + row.rstrip(", "))
                    row = ""
                row += n + ", "
            if row:
                out.append(s + row.rstrip(", "))
    if omitted:
        out += ["", "Listados sin contenido:"]
        out += [f"  {o['path']} | {o['size'] // 1024} KB | {o['why']}" for o in omitted]
    out += ["===== FIN DEL INDICE =====", ""]
    return "\n".join(out)


# ------------------------------------------------------------------ cabecera para el asistente
def project_header(mode, nparts, k, stats):
    ver = "?"
    m = ROOT / "src/core/kernel.c"
    if m.exists():
        mm = re.search(r"SOMA [0-9.]+ [a-z-]*", m.read_text(errors="replace"))
        if mm:
            ver = mm.group(0)
    git = ""
    try:
        git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], capture_output=True, text=True, timeout=5).stdout.strip()
    except Exception:
        pass
    h = [f"########## SOMA - exportacion del proyecto en texto | parte {k}/{nparts} ##########",
         f"Version: {ver}" + (f" | commit {git}" if git else "") + f" | generado {time.strftime('%Y-%m-%d %H:%M')} | modo: {mode}"]
    if k == 1:
        h += ["",
              "PARA EL ASISTENTE:",
              "- SOMA es un sistema operativo bare-metal en C para x86_64 y aarch64 (QEMU, Lenovo ThinkPad T480s desde USB y Raspberry Pi 5).",
              "  Autor: Marcos Luis Alvarez Alejandre. Licencia MIT. Repositorio: https://github.com/mlalejandre/x86-soma",
              "- Responde en espanol. Los cambios se entregan como scripts autocontenidos parche_NNN_nombre.py que se ejecutan en la raiz",
              "  del proyecto: aplicar / --comprobar / --revertir, todo o nada, con copia de seguridad, y comprobados (aplicar sobre una copia",
              "  limpia, compilar, revertir). Compilar: make (x86_64) o python3 ejecutar.py. Pruebas en el anfitrion: make test.",
              f"- Esta exportacion tiene {nparts} parte(s). Cada archivo va entre '===== ARCHIVO: ruta | ... =====' y '===== FIN: ruta ====='.",
              "  Los que figuran en el indice como 'solo listados' existen en el proyecto pero no se han incluido (datos, terceros...).",
              f"- Resumen: {stats}",
              ""]
    return "\n".join(h) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Exporta el proyecto SOMA a .txt para pasarlo a un asistente.")
    ap.add_argument("rutas", nargs="*", help="carpetas o archivos (por defecto, todo)")
    ap.add_argument("--indice", action="store_true", help="solo el mapa del proyecto (arbol, lineas, funciones)")
    ap.add_argument("--cambiados", action="store_true", help="solo lo cambiado desde la ultima exportacion completa (o git HEAD)")
    ap.add_argument("--tamano", type=int, default=400, help="KB maximos por parte (por defecto 400)")
    ap.add_argument("--salida", default="exportacion", help="carpeta de salida (por defecto exportacion/)")
    ap.add_argument("--con-datos", action="store_true")
    ap.add_argument("--con-terceros", action="store_true")
    ap.add_argument("--con-docs", action="store_true")
    ap.add_argument("--con-pruebas", action="store_true")
    ap.add_argument("--tema", action="append", default=[], help="tema (repetible): " + ", ".join(TEMAS))
    ap.add_argument("--temas", action="store_true", help="lista los temas")
    ap.add_argument("--funciones", action="store_true", help="con --indice: incluye las funciones de cada archivo")
    o = ap.parse_args()
    if o.temas:
        for k, (d, pats) in TEMAS.items():
            print(f"  {k:<13} {d}")
        return 0
    for t in o.tema:
        if t not in TEMAS:
            print(f"Tema desconocido: {t}. Temas: {', '.join(TEMAS)}")
            return 1
        o.rutas += BASE + TEMAS[t][1]
    if not o.rutas and not o.con_pruebas and not o.indice and not o.cambiados:
        SKIP_TOP.add("tests")

    files = [p for p in select(walk(), o.rutas) if is_text_file(p)]
    if not files:
        print("No hay archivos que exportar con esos filtros.")
        return 1
    out_dir = (ROOT / o.salida).resolve()
    if ROOT not in out_dir.parents and out_dir != ROOT:
        pass
    out_dir.mkdir(parents=True, exist_ok=True)
    # cada modo tiene su prefijo y solo borra sus propios archivos anteriores (una exportacion de cambios no borra la completa)
    prefix = ("soma_indice" if o.indice else "soma_cambios" if o.cambiados else ("soma_" + "_".join(o.tema)) if o.tema
              else "soma_seleccion" if o.rutas else "soma_parte")
    for old in out_dir.glob(prefix + ("*.txt" if not o.indice else ".txt")):
        old.unlink()

    entries, omitted, hidden = [], [], 0
    for p in files:
        r = rel(p)
        inc, why = classify(p, o)
        size = p.stat().st_size
        if not inc:
            omitted.append({"path": r, "size": size, "why": why})
            continue
        text, n = read_clean(p)
        hidden += n
        first, names = summarize(r, text)
        entries.append({"path": r, "text": text, "lines": text.count("\n") + (0 if text.endswith("\n") else 1), "size": size,
                        "sha": sha(text), "first": first, "names": names})

    mpath = out_dir / MANIFEST
    mode = "completa" + ("" if o.con_pruebas else " (sin tests/)") if not o.rutas else (
        "tema: " + ", ".join(o.tema) if o.tema else "seleccion: " + " ".join(o.rutas))
    if o.cambiados:
        prev = {}
        if mpath.exists():
            prev = json.loads(mpath.read_text())
            changed = [e for e in entries if prev.get(e["path"]) != e["sha"]]
            gone = sorted(k for k in set(prev) - {e["path"] for e in entries} - {x["path"] for x in omitted} if not k.startswith("__"))
            mode = f"cambios desde la exportacion completa del {prev.get('__fecha__', '?')}"
        else:
            try:
                res = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain"], capture_output=True, text=True, timeout=10)
                names = {l[3:].strip().split(" -> ")[-1] for l in res.stdout.splitlines() if l.strip()}
            except Exception:
                names = set()
            if not names:
                print("No hay exportacion completa anterior ni cambios en git: haz primero una exportacion completa.")
                return 1
            changed = [e for e in entries if e["path"] in names or any(e["path"].startswith(n.rstrip("/") + "/") for n in names)]
            gone = []
            mode = "cambios respecto a git HEAD"
        if not changed and not gone:
            print("No hay cambios desde la ultima exportacion completa.")
            return 0
        entries = changed
        if gone:
            omitted = [{"path": g, "size": 0, "why": "BORRADO desde la ultima exportacion"} for g in gone]
        else:
            omitted = []

    if o.indice:
        idx = build_index(entries, omitted, "mapa del proyecto (sin contenido)", o.funciones)
        stats = f"{len(entries)} archivos, {sum(e['lines'] for e in entries)} lineas (solo el indice)"
        f = out_dir / "soma_indice.txt"
        f.write_text(project_header("indice", 1, 1, stats) + "\n" + idx, encoding="utf-8")
        print(f"[OK] {f.relative_to(ROOT) if ROOT in f.parents else f}  ({f.stat().st_size // 1024} KB, ~{f.stat().st_size // 3500}k tokens)")
        return 0

    # ---- partes
    limit = max(64, o.tamano) * 1024
    blocks = []                                                   # (ruta, texto del bloque)
    for e in entries:
        body = e["text"] if e["text"].endswith("\n") else e["text"] + "\n"
        head = f"===== ARCHIVO: {e['path']} | {e['lines']} lineas | sha256 {e['sha']} =====\n"
        tail = f"===== FIN: {e['path']} =====\n\n"
        if len(head) + len(body) + len(tail) <= limit - 4096:
            blocks.append((e["path"], head + body + tail))
            continue
        chunks, cur = [], ""
        for line in body.splitlines(keepends=True):               # trocear por lineas
            if len(cur) + len(line) > limit - 8192 and cur:
                chunks.append(cur)
                cur = ""
            cur += line
        if cur:
            chunks.append(cur)
        for i, c in enumerate(chunks, 1):
            blocks.append((e["path"], f"===== ARCHIVO: {e['path']} | trozo {i}/{len(chunks)} | {e['lines']} lineas | sha256 {e['sha']} =====\n"
                                      + c + f"===== FIN: {e['path']} | trozo {i}/{len(chunks)} =====\n\n"))
    index = build_index(entries, omitted, mode)
    parts, cur, cur_n = [], [], len(index) + 2048
    for path, b in blocks:
        if cur and cur_n + len(b) > limit:
            parts.append(cur)
            cur, cur_n = [], 2048
        cur.append((path, b))
        cur_n += len(b)
    if cur:
        parts.append(cur)
    total_lines = sum(e["lines"] for e in entries)
    stats = (f"{len(entries)} archivos, {total_lines} lineas, {sum(len(b) for _, b in blocks) // 1024} KB de texto"
             + (f"; {hidden} posibles claves ocultadas" if hidden else "") + f"; {len(omitted)} archivos solo listados")
    width = len(str(len(parts)))
    written = []
    for k, part in enumerate(parts, 1):
        f = out_dir / f"{prefix}_{k:0{width}d}_de_{len(parts)}.txt"
        content = project_header(mode, len(parts), k, stats)
        if k == 1:
            content += "\n" + index + "\n"
        paths = sorted({p for p, _ in part})
        content += f"Archivos en esta parte ({len(paths)}): " + ", ".join(paths[:12]) + (" ..." if len(paths) > 12 else "") + "\n\n"
        content += "".join(b for _, b in part)
        content += f"########## FIN DE LA PARTE {k}/{len(parts)} ##########\n"
        f.write_text(content, encoding="utf-8")
        written.append(f)

    if not o.cambiados and not o.rutas:                         # la referencia para --cambiados: TODO el texto (tests/ incluido)
        SKIP_TOP.clear()
        man = {}
        for p in walk():
            if is_text_file(p):
                man[rel(p)] = sha(read_clean(p)[0])
        man["__fecha__"] = time.strftime("%Y-%m-%d %H:%M")
        mpath.write_text(json.dumps(man, indent=0))

    print(f"[OK] {stats}")
    for f in written:
        n = f.stat().st_size
        print(f"   {f.relative_to(ROOT) if ROOT in f.parents else f}   {n // 1024} KB  (~{n // 3500}k tokens)")
    print("Pasalas en orden, una por mensaje. Para una pregunta concreta suele bastar --indice y luego solo los archivos que hagan falta.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
