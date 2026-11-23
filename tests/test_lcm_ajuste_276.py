"""SOMA_276_LCM: el ajuste fino hereda bien los pesos de un modelo anterior (palabras y ordenes ya conocidas intactas, las nuevas al final).
    python3 tests/test_lcm_ajuste_276.py   (necesita torch; si no esta, se omite)"""
import os, sys
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, RAIZ)
try:
    import torch
except ImportError:
    print("omitido: sin torch"); sys.exit(0)
from tools.lcm_ordenes import traduce as T
fallos = n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c: fallos += 1; print("FALLO:", m)
_, Tr, _ = T._red()
voc = {"hola": 2, "adios": 3}
ant_ints = T.INTENTS[:5] + ["orden_que_ya_no_existe"]
viejo = Tr(len(voc) + 2, len(ant_ints), len(T.TAGS))
ant = {"modelo": viejo.state_dict(), "voc": voc, "intents": ant_ints, "tags": T.TAGS}
voc2 = dict(voc); voc2["nueva"] = 4
nuevo = Tr(len(voc2) + 2, len(T.INTENTS), len(T.TAGS))
T._hereda(nuevo, ant, torch)
sd, so = nuevo.state_dict(), viejo.state_dict()
ck(torch.equal(sd["ew.weight"][:so["ew.weight"].shape[0]], so["ew.weight"]), "palabras conocidas intactas")
ck(torch.equal(sd["lstm.weight_ih_l0"], so["lstm.weight_ih_l0"]), "LSTM intacta")
for i in range(5):
    ck(torch.equal(sd["intent.weight"][i], so["intent.weight"][i]) and torch.equal(sd["intent.bias"][i:i + 1], so["intent.bias"][i:i + 1]), "orden %s heredada por nombre" % T.INTENTS[i])
ck(torch.equal(sd["tag.weight"], so["tag.weight"]), "etiquetas intactas")
ck(not torch.equal(sd["intent.weight"][5], so["intent.weight"][5]) , "una orden nueva no hereda la fila de otra")
print(("ok " if not fallos else "FALLO") + f" ajuste fino (276): {n} comprobaciones, {fallos} fallos"); sys.exit(1 if fallos else 0)
