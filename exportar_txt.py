#!/usr/bin/env python3
"""exportar_txt.py - exporta el proyecto SOMA a archivos .txt para pasarselo a un asistente (Claude) en una conversacion.

Uso (desde la raiz del proyecto):
    python3 exportar_txt.py                     todo el codigo propio, en partes de ~400 KB
    python3 exportar_txt.py --indice            solo el MAPA del proyecto: archivos, lineas y para que sirve cada uno (~60 KB)
    python3 exportar_txt.py --indice --funciones    el mapa con las funciones de cada archivo (~200 KB)
    python3 exportar_txt.py --tema escritorio   un tema completo (ver --temas): escritorio, navegador, nucleo, consola, lenguaje...
    python3 exportar_txt.py src/ui src/apps/settings.c     solo esas carpetas o archivos (admite comodines: 'src/agent/shell_*.c')
    python3 exportar_txt.py --cambiados         solo lo que ha cambiado desde la ultima exportacion completa (o desde git HEAD)
    python3 exportar_txt.py --parche 295        los archivos que toco un parche (los que llevan su marca SOMA_295_...), con su nota
    python3 exportar_txt.py --parche 290-300    lo mismo para un intervalo de parches
    python3 exportar_txt.py --tema correo --tema ia     varios temas juntos (los archivos repetidos van una sola vez)

Como usarlo con el asistente: el proyecto completo son ~6 MB de codigo (~1,8 millones de tokens), mas de lo que cabe en una
conversacion. Lo practico es pasar primero el indice (--indice) y despues el tema o los archivos de la tarea; tras cambios, --cambiados.
Opciones:
    --tamano KB        tamano maximo de cada parte (por defecto 400; ~110 000 tokens)
    --salida CARPETA   donde se escriben las partes (por defecto exportacion/; se vacia antes)
    --con-datos        incluye los datos generados (fuentes en mapas de bits, certificados, imagenes de prueba...)
    --con-terceros     incluye las bibliotecas de terceros (third_party/, simplewebp.h)
    --con-docs         incluye docs/ (notas de cada parche, plan del T480s...)
    --con-pruebas      incluye tests/ en la exportacion completa (en un tema o con rutas se incluyen si se piden)
    --temas            lista los temas disponibles (con cuantos archivos y KB tiene cada uno)

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
             "memoria-export",                                    # SOMA_301_EXPORTAR: tu memoria personal exportada
             
             "__pycache__", "snapshots", "isodir", "build_backup_src", ".vscode", ".idea", "firmware", "workspace", "node_modules"}
SKIP_GLOBS = ["parche_*.py", "parches_*", "subir_github.py", "*.img", "*.img.xz", "*.iso", "*.whl", "*.ucode", "*.ttf", "*.otf",
              "*.bmp", "*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp", "*.pdf", "*.avi", "*.wav", "*.o", "*.elf", "*.bin", "*.pyc",
              "*.log", "*.pcap", "*.sha256", ".DS_Store", "._*", "*~", "*.swp", "hdd.img*", "estructura*.txt",
              "*.smem", "*.mp4", "*.mkv", "*.mp3", "*.gguf", "*.sqlite", "*.db", ".parche_*"]          # SOMA_301_EXPORTAR
TEXT_EXT = {".c", ".h", ".s", ".S", ".inc", ".ld", ".py", ".cfg", ".md", ".txt", ".soma", ".yml", ".yaml", ".json", ".js", ".sh",
            ".html", ".css", ".toml", ".ini", ".conf"}
TEXT_NAMES = {"Makefile", "Dockerfile", "LICENSE", ".gitignore", "protected.txt"}
SECRET_NAMES = {"llm.conf", ".env", "credentials.json", "secrets.json", "wifi.conf", "redes_wifi.conf",
                "correo.conf", "search.conf", "token.json", ".netrc"}                 # SOMA_301_EXPORTAR: correo.conf guarda tu clave
SECRET_GLOBS = ["*.key", "*_key", "*.p12", "*.pfx", "id_rsa*", "*.pem",
                "*.conf", ".env*"]   # SOMA_339_GITHUB: ningun .conf (cualquiera puede llevar una clave), no solo los de la lista
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
                   "tests/test_coder_*.c", "tests/prueba_salida_333.py"]),
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
    # SOMA_301_EXPORTAR: temas de los ultimos avances (parches 250-300)
    "ia":         ("la IA de SOMA: el agente, el camino de cada peticion (LCM o modelo), el LLM y la memoria",
                   ["src/agent/agent.*", "src/agent/agent_ruta.*", "src/agent/agent_policy.h", "src/agent/llm*", "src/agent/lcm_*",
                    "src/agent/memoria*", "src/agent/shell_agente.c", "llm_server.py", "tools/llm_server.py"]),
    "lcm":        ("LCM: entender ordenes sin llamar al modelo (pesos, ordenes, entrenamiento y pruebas)",
                   ["src/agent/lcm_*", "src/agent/agent_ruta.*", "tools/lcm_ordenes/*", "tests/test_lcm_*", "docs/PARCHE_25[3-8]_*"]),
    "memoria":    ("memoria infinita: hechos, arbol de temas, sueno y exportacion",
                   ["src/agent/memoria*", "tools/exporta_memoria.py", "tests/test_memoria_*"]),
    "chat":       ("la aplicacion SOMA: chat con el agente (historial, Markdown, adjuntos, voz)",
                   ["src/apps/chat*", "tests/test_chat*_285.c"]),
    "correo":     ("Correo: IMAP/SMTP con TLS 1.3, la IA del correo y el orbe",
                   ["src/apps/correo*", "src/agent/shell_correo.c", "tests/test_correo*", "tests/correo_servidores_295.py",
                    "src/core/tls13.*", "docs/PARCHE_29[56]_*"]),
    "ayuda":      ("la bienvenida del orbe y la ayuda rapida (F1)",
                   ["src/ui/ayuda.h", "src/ui/gui_ayuda.c", "src/ui/gui_voz.c", "tests/test_ayuda*_294.c", "docs/PARCHE_294_*"]),
    "orbe":       ("el orbe: dibujo, voz (pulsar Alt), avisos y frases",
                   ["src/ui/gui_orbe*", "src/ui/gui_voz.c", "src/ui/gui_ayuda.c", "src/ui/ayuda.h", "src/agent/prog_voz.*", "src/user/voz/*"]),
    "investiga":  ("investigar en internet: busquedas, lectura de paginas e informe con fuentes",
                   ["src/agent/shell_investiga.c", "src/agent/busca_web.*", "src/agent/web_lee.*", "tools/search_proxy.py",
                    "tests/test_investiga_288.c", "src/agent/inv_tema.*", "tests/test_inv_tema_336.c"]),
    "youtube":    ("YouTube: buscar, ver y hablar con los videos",
                   ["src/apps/youtube*", "src/apps/yt_*", "tools/yt_service.py", "tests/test_youtube*", "tests/*yt_*335*"]),
    "paint":      ("Paint y el dibujo con IA", ["src/apps/paint*", "tests/test_paint_ia*_292.c"]),
    "agenda":     ("Agenda: citas, avisos, frases y .ics", ["src/apps/agenda*", "src/agent/shell_agenda.c", "tests/*agenda*"]),
    "video":      ("video y audio: reproductor, H.264/AAC en ring 3, grabadora y camara",
                   ["src/apps/media.*", "src/user/vplay/*", "src/user/vidplay.c", "src/core/avi*", "src/core/audio*", "src/core/wav.*",
                    "tests/test_vplay_252.py"]),
    # SOMA_339_GITHUB: temas de los parches 311-338
    "servicios":  ("servicios aislados de ring 3: netsvc, tlstun, leesvc, mimesvc, imgdec, pdfsvc, zdec y el sandbox del nucleo",
                   ["src/user/*svc.c", "src/user/tlstun.c", "src/user/zdec.c", "src/user/imgdec.c", "src/user/sbxrt.h", "src/user/unet.c",
                    "src/arch/x86_64/sandbox*", "src/arch/x86_64/usermode*", "src/agent/web_lee.*"]),
    "iallocal":   ("motor de IA local (llama.cpp en ring 3): embeddings de la memoria y LLM pequenos",
                   ["src/user/ia/*.c", "src/user/ia/*.cpp", "src/user/ia/*.ld", "src/arch/x86_64/ia_sys.*", "src/core/ia_abi.h",
                    "tools/ia_modelos_prueba.py", "tests/prueba_ia_329.py"]),
    "noticias":   ("Noticias: canales RSS/Atom, categorias, resumen con IA y memoria",
                   ["src/apps/noticias*", "src/user/leesvc.c", "tests/*noticias_337*"]),
    "agentes":    ("la aplicacion Agentes y el registro de misiones",
                   ["src/agent/agentes_reg.*", "src/apps/agentes_gui.*", "tests/*agentes_338*"]),
    "discos":     ("Discos: particiones, formato e imagenes", ["src/core/discos_core.*", "src/apps/discos*", "tests/*discos_331*"]),
    "hora":       ("zona horaria y hora del sistema", ["src/core/hora.*", "src/core/zona.*", "tests/*zona_332*", "tests/*hora_332*"]),
    "ci":         ("CI de GitHub y comprobaciones: workflow, auditoria, arranque en QEMU y fallos",
                   [".github/*", "Makefile", "tools/audit.py", "tools/boot_smoke.py", "tools/fault_check.py", "tools/cpu_matrix.py",
                    "tools/sbx_check.py", "tools/idle_check.py", "protected.txt", "docs/PARCHE_29[89]_*", "docs/PARCHE_30*"]),
}
SECRET_RE = re.compile(r"(sk-ant-[A-Za-z0-9_\-]{16,}|sk-[A-Za-z0-9_\-]{20,}|AIza[0-9A-Za-z_\-]{30,}|gh[pousr]_[A-Za-z0-9]{30,}"
                       r"|xox[abpr]-[A-Za-z0-9\-]{10,}|(?i:bearer)\s+[A-Za-z0-9_\-\.=]{20,}"
                       # SOMA_301_EXPORTAR: tokens nuevos de GitHub, Hugging Face, GitLab, AWS y claves privadas pegadas en un texto
                       r"|github_pat_[A-Za-z0-9_]{30,}|hf_[A-Za-z0-9]{30,}|glpat-[A-Za-z0-9_\-]{20,}|AKIA[0-9A-Z]{16}"
                       r"|-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)")


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
def ultimo_parche():
    """SOMA_301_EXPORTAR: el numero mas alto entre los titulos '(NNN)' del CHANGELOG."""
    c = ROOT / "CHANGELOG.md"
    if not c.exists():
        return ""
    nums = [int(n) for n in re.findall(r"^#{2,3} .*\((\d{2,4})\)\s*$", c.read_text(errors="replace"), re.M)]
    return str(max(nums)) if nums else ""


def archivos_de_parches(files, desde, hasta):
    """SOMA_301_EXPORTAR: archivos con la marca SOMA_NNN_ de algun parche del intervalo, y sus notas docs/PARCHE_NNN_*.md."""
    marca = re.compile(r"SOMA_(\d{3})_[A-Z0-9_]+")
    sel = []
    for p in files:
        r = rel(p)
        m = re.match(r"docs/PARCHE_(\d{3})_", r)
        if m and desde <= int(m.group(1)) <= hasta:
            sel.append(p)
            continue
        try:
            t = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if any(desde <= int(n) <= hasta for n in marca.findall(t)):
            sel.append(p)
    return sel


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
    last = ultimo_parche()                                         # SOMA_301_EXPORTAR
    h = [f"########## SOMA - exportacion del proyecto en texto | parte {k}/{nparts} ##########",
         f"Version: {ver}" + (f" | ultimo parche: {last}" if last else "") + (f" | commit {git}" if git else "")
         + f" | generado {time.strftime('%Y-%m-%d %H:%M')} | modo: {mode}"]
    if k == 1:
        h += ["",
              "PARA EL ASISTENTE:",
              "- SOMA es un sistema operativo bare-metal en C para x86_64 y aarch64 (QEMU, Lenovo ThinkPad T480s desde USB y Raspberry Pi 5).",
              "  Autor: Marcos Luis Alvarez Alejandre. Licencia MIT. Repositorio: https://github.com/mlalejandre/x86-soma",
              "- Responde en espanol. Los cambios se entregan como scripts autocontenidos parche_NNN_nombre.py que se ejecutan en la raiz",
              "  del proyecto: aplicar / --comprobar / --revertir, todo o nada, con copia de seguridad, y comprobados (aplicar sobre una copia",
              "  limpia, compilar, revertir). Compilar: make (x86_64) o python3 ejecutar.py. Pruebas en el anfitrion: make test.",
              # SOMA_301_EXPORTAR: lo aprendido en los ultimos parches, para que el asistente no lo repita
              "- El autor trabaja en macOS (Apple clang, -Werror): en cadenas de C, los caracteres no ASCII van como escapes octales de 3",
              "  cifras; nada de simbolos debiles; ningun char[N] relleno exactamente por su cadena (-Wunterminated-string-initialization);",
              "  ninguna llamada a una funcion sin declarar (clang y gcc 14 lo dan como error). Las pruebas que usan cabeceras del nucleo",
              "  solo se exigen en Linux x86_64 (HOST_REF; si no, 'SE OMITE'). El --comprobar de un parche no debe necesitar el",
              "  compilador del nucleo (en el Mac se compila con Docker desde ejecutar.py).",
              "- La CI de GitHub (.github/workflows/ci.yml) compila con gcc 14 y -Werror en x86_64 y aarch64, arranca en QEMU sin KVM y pasa",
              "  make ci. Nucleo: sin coma flotante, pilas de hilo de 60 KiB. Los exportadores nunca incluyen .conf, claves ni secretos.",
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
    ap.add_argument("--parche", default="", metavar="NNN[-MMM]",
                    help="los archivos que toco ese parche o intervalo (marca SOMA_NNN_...), con su nota de docs/")   # SOMA_301_EXPORTAR
    o = ap.parse_args()
    if o.temas:
        todos = [p for p in walk() if is_text_file(p)]
        for k, (d, pats) in TEMAS.items():
            sel = [p for p in select(todos, BASE + pats) if classify(p, o)[0] or (p.as_posix().find("/docs/") >= 0)]
            print(f"  {k:<13} {d}  [{len(sel)} archivos, ~{sum(p.stat().st_size for p in sel) // 1024} KB con lo comun]")
        return 0
    for t in o.tema:
        if t not in TEMAS:
            print(f"Tema desconocido: {t}. Temas: {', '.join(TEMAS)}")
            return 1
        o.rutas += BASE + TEMAS[t][1]
        if any(x.startswith("docs/") for x in TEMAS[t][1]):        # SOMA_301_EXPORTAR: las notas que el tema pide expresamente
            o.con_docs = True
    if not o.rutas and not o.con_pruebas and not o.indice and not o.cambiados:
        SKIP_TOP.add("tests")

    if o.parche:                                                   # SOMA_301_EXPORTAR
        m = re.fullmatch(r"(\d{1,4})(?:-(\d{1,4}))?", o.parche.strip())
        if not m:
            print("--parche espera un numero (295) o un intervalo (290-300).")
            return 1
        desde = int(m.group(1)); hasta = int(m.group(2) or m.group(1))
        o.con_docs = True
        o.rutas = [rel(p) for p in archivos_de_parches([p for p in walk() if is_text_file(p)], desde, hasta)]
        if not o.rutas:
            print(f"Ningun archivo lleva la marca de los parches {o.parche}.")
            return 1
        o.tema = []
        o.parche_txt = f"parches {o.parche}"
    else:
        o.parche_txt = ""
    o.rutas = list(dict.fromkeys(o.rutas))                         # SOMA_301_EXPORTAR: sin repetidos al juntar temas
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
              else ("soma_parche_" + o.parche.replace("-", "_")) if o.parche else "soma_seleccion" if o.rutas else "soma_parte")
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
        "tema: " + ", ".join(o.tema) if o.tema else o.parche_txt if o.parche_txt else "seleccion: " + " ".join(o.rutas))
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
