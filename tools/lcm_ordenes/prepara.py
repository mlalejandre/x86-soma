"""Descarga los corpus UD de español que usa el entrenamiento (frases reales).  python3 -m tools.lcm_ordenes.prepara"""
import os
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
BASE = "https://raw.githubusercontent.com/UniversalDependencies/{repo}/master/{f}"
CORPUS = [("UD_Spanish-AnCora", "es_ancora-ud-train.conllu"), ("UD_Spanish-AnCora", "es_ancora-ud-test.conllu"),
          ("UD_Spanish-GSD", "es_gsd-ud-train.conllu"), ("UD_Spanish-GSD", "es_gsd-ud-test.conllu"), ("UD_Spanish-PUD", "es_pud-ud-test.conllu")]

if __name__ == "__main__":
    d = os.environ.get("LCM_UD") or os.path.join(AQUI, "datos", "ud")
    os.makedirs(d, exist_ok=True)
    for repo, f in CORPUS:
        p = os.path.join(d, f)
        if os.path.exists(p):
            print("ya está", f); continue
        print("descargando", f)
        urllib.request.urlretrieve(BASE.format(repo=repo, f=f), p)
    print("listo:", d)
