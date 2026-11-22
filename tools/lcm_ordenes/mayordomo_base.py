"""LÍNEA BASE: el camino rápido del mayordomo de SOMA (src/agent/agent.c, mayordomo_run), reproducido en Python.
Mismo orden de reglas y mismas palabras clave. Devuelve la orden que ejecutaría, o "NADA" si no identifica la operación
(en SOMA contesta «No he identificado la operación»; no ejecuta nada).
Aproximaciones: no se reproduce el enrutador del orbe (ar_mayordomo), solo lo que haría el mayordomo si le llega la petición."""
import json, os, re

_T = json.load(open(os.path.join(os.path.dirname(__file__), "soma_ordenes.json"), encoding="utf-8"))
ORD = {e["nombre"]: e for e in _T["ordenes"]}
SKILLS = _T["mayordomo_habilidades"]


def fold(s):
    return s.lower().translate(str.maketrans("áéíóúüñ", "aeiouun"))


def has(m, n):
    return n in m.lower()


def word(m, w):
    return re.search(r"(?<![a-z0-9])" + re.escape(w) + r"(?![a-z0-9])", fold(m)) is not None


def extract_value(m):
    for mk in ("llamado ", "llamada ", "denominado ", "denominada ", "ruta ", "nombre ", "archivo ", "fichero ", "carpeta ", "directorio ", "file ", "folder ", "directory "):
        i = m.lower().find(mk)
        if i >= 0:
            p = m[i:].split(" ", 1)
            rest = p[1] if len(p) > 1 else ""
            break
    else:
        rest = None
        for tok in re.split(r"[\s'\",;]+", m):
            if tok and (tok[0] == "/" or "." in tok):
                rest = tok
                break
    if not rest:
        return None
    v = re.split(r"[\s'\",;]", rest.strip(" '\""))[0].rstrip(".:!?")
    if not v or any(x in v.lower() for x in ("llamado", "llamada", "denominado", "denominada")):
        return None
    return v


def is_request(f):
    return any(w in f for w in ("muestra", "mostrar", "ensena", "dime", "indica", "lista", "listar", "consulta", "comprueba", "revisa", "diagnostica", "cuant",
                                "que ", "cual", "como ", "ver ", "dame", "necesito", "quiero", "hay ", "show", "check", "status", "estado", "verifica", "ejecuta",
                                "haz ", "crea ", "sincroniza", "genera"))


def blocked(f):
    return any(b in f for b in ("noticia", "internet", " web", "google", "clima", "meteorolog", "archivo ", "fichero ", "carpeta ", ".txt", ".log", "/workspace", "/mis_archivos"))


def mayordomo(mission):
    m = mission.strip()
    if not m:
        return "NADA"
    if m.lower().startswith("mkdir"):
        return "mkdir " + m[5:].strip()
    name = m.split()[0].lower()
    if name in ORD and name not in ("find", "delegate", "vsoma", "gui", "source", "sh", "spawn", "bg"):
        return m if "SAFE" in ORD[name]["flags"] else "NADA (no autorizado)"
    f = fold(m)
    if ("ayuda" in f or "comandos" in f or "herramientas" in f) and ("que comandos" in f or "lista de comandos" in f):
        return "help"
    if not blocked(f) and is_request(f):
        for s in SKILLS:
            if any(k and k in f for k in s["claves"]):
                return s["orden"]
    if any(has(m, x) for x in ("ejecuta", "run ", "corre ", "lanza ")):
        v = extract_value(m)
        if v:
            return "run " + v
    if word(m, "ram") or word(m, "heap") or word(m, "memoria"):
        if any(x in f for x in ("detall", "desglos", "completo", "completa", "a fondo")):
            return "ram -v"
        if any(x in f for x in ("resumen", "breve", "rapido", "corto", "una linea")):
            return "ram -s"
        return "ram"
    if any(has(m, x) for x in ("espacio", "almacenamiento", "capacidad", "df")):
        return "df"
    if any(has(m, x) for x in ("busca", "buscar", "encuentra", "localiza", "find ")) and any(has(m, x) for x in ("archivo", "fichero", "file ", ".txt", ".c", ".h", ".log")):
        v = extract_value(m)
        return "find " + (v or "")
    if any(has(m, x) for x in ("crea", "crear", "create ")) and any(has(m, x) for x in ("carpeta", "directorio", "folder ", "directory ")):
        v = extract_value(m)
        return ("mkdir " + v) if v else "NADA (pide nombre)"
    if any(has(m, x) for x in ("elimina", "borrar", "borra", "delete ", "remove ")) and any(has(m, x) for x in ("archivo", "fichero", "ruta ", "/workspace/")):
        v = extract_value(m)
        return ("rm " + v) if v else "NADA (pide ruta)"
    if any(has(m, x) for x in ("contenido del archivo", "contenido del fichero", "lee archivo", "leer archivo", "cat ")) and any(has(m, x) for x in (".txt", ".log", ".c", "/workspace/", "/mis_archivos/")):
        v = extract_value(m)
        return ("cat " + v) if v else "NADA (pide ruta)"
    if any(has(m, x) for x in ("crea", "crear", "create ")) and has(m, "archivo") and not any(has(m, x) for x in ("contenido", "texto", "sobre ", "informe")):
        v = extract_value(m)
        return ("touch " + v) if v else "NADA (pide nombre)"
    if any(has(m, x) for x in ("disco", "archivos", "ficheros", "contenido del disco", "listar", "lista los archivos", "list files", "ls ")):
        return "ls -a /"
    if any(has(m, x) for x in ("sistema", "hardware", "estado", "uptime")):
        return "status"
    return "NADA"
