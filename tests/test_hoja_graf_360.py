"""SOMA_360_HOJA_GRAFICOS: graficos de la hoja en XLSX. SOMA escribe un libro con los seis tipos (tambien uno cuyos datos estan en
otra hoja) y comprueba: XML bien formado, que SOMA los vuelve a leer igual, que openpyxl los ve con su tipo y, si hay LibreOffice,
que LibreOffice los abre, los guarda y SOMA los lee de nuevo (tipos, rangos y titulos). Tambien el formato propio (.hoja) y fuzz.
    python3 tests/test_hoja_graf_360.py"""
import os, shutil, subprocess, sys, tempfile, zipfile
import xml.dom.minidom
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
cc = os.environ.get("HOST_CC", "cc")
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
S = ["src/apps/hoja_core.c", "src/apps/hoja_graf.c", "src/apps/hoja_zip.c", "src/apps/hoja_zip_lee.c", "src/apps/hoja_xlsx.c", "src/apps/hoja_xlsx_lee.c",
     "src/apps/hoja_ods.c", "src/apps/hoja_ods_lee.c", "src/apps/web_f64.c", "src/apps/web_inflate.c", "src/core/png_enc.c"]
d = tempfile.mkdtemp(prefix="graf360_")
exe = os.path.join(d, "hc")
r = subprocess.run([cc, "-std=gnu11", "-g", "-O1", "-w", "-fsanitize=address,undefined", "-DWEB_HOST_TEST", "-Isrc/apps", "-Isrc/core", "tests/hoja_cli_358.c"] + S + ["-o", exe],
                   cwd=RAIZ, capture_output=True, text=True)
ck(r.returncode == 0, "compila: " + r.stderr[:500])
if r.returncode:
    print(f"test_hoja_graf_360: {n} comprobaciones, {fallos} fallos"); sys.exit(1)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
def run(*a):
    p = subprocess.run([exe] + list(a), cwd=d, capture_output=True, text=True, env=env)
    return p.returncode, p.stdout + p.stderr
def graficos(salida):
    return [l for l in salida.splitlines() if l.startswith("GRAF ")]
x = os.path.join(d, "g.xlsx")
rc, _ = run("demograf", x); ck(rc == 0, "escribe el XLSX con graficos")
rc, s = run("lee", x); g = graficos(s)
esperado = ["GRAF 1|0|0,0,4,3|0,5|520,320|Ventas por zona|1", "GRAF 3|0|0,0,4,1|20,5|520,320|Norte|1", "GRAF 2|0|0,0,4,3|40,5|520,320||0",
            "GRAF 5|0|10,0,16,1|60,5|520,320||1", "GRAF 0|0|0,0,4,3|1,1|520,320||1", "GRAF 4|0|0,0,4,2|22,1|520,320||1"]
ck(g == esperado, "SOMA vuelve a leer sus graficos: " + str(g))
with zipfile.ZipFile(x) as z:
    nombres = z.namelist()
    ck(sum(1 for m in nombres if m.startswith("xl/charts/chart")) == 6, "seis charts")
    ck("xl/drawings/drawing1.xml" in nombres and "xl/drawings/drawing2.xml" in nombres, "un dibujo por hoja")
    tipos = z.read("[Content_Types].xml").decode()
    ck(tipos.count("drawingml.chart+xml") == 6 and tipos.count("drawing+xml") == 2, "Content_Types")
    for m in nombres:
        if m.endswith(".xml") or m.endswith(".rels"):
            try: xml.dom.minidom.parseString(z.read(m))
            except Exception as ex: ck(False, f"XML mal formado: {m}: {ex}")
    c1 = z.read("xl/charts/chart1.xml").decode()
    ck("'Ventas 2026'!$B$1" in c1 and "'Ventas 2026'!$A$2:$A$5" in c1 and "'Ventas 2026'!$B$2:$B$5" in c1, "referencias de la serie")
    ck('<c:barDir val="col"/>' in c1 and "Ventas por zona" in c1, "columnas con titulo")
    ck("<drawing r:id=\"rId1\"/>" in z.read("xl/worksheets/sheet1.xml").decode(), "la hoja apunta al dibujo")
try:
    import openpyxl
    wb = openpyxl.load_workbook(x)
    t1 = [type(c).__name__ for c in wb["Ventas 2026"]._charts]; t2 = [type(c).__name__ for c in wb["Otra hoja"]._charts]
    ck(t1 == ["BarChart", "PieChart", "LineChart", "ScatterChart"] and t2 == ["BarChart", "AreaChart"], "openpyxl ve los tipos: " + str((t1, t2)))
except ImportError:
    print("  (sin openpyxl: se salta)")
so = shutil.which("soffice") or shutil.which("libreoffice")
if so:
    lo = os.path.join(d, "lo"); os.makedirs(lo)
    p = subprocess.run([so, "--headless", "--convert-to", "xlsx", "--outdir", lo, x], capture_output=True, text=True, timeout=240)
    y = os.path.join(lo, "g.xlsx")
    ck(os.path.exists(y), "LibreOffice convierte: " + p.stderr[-300:])
    if os.path.exists(y):
        rc, s = run("lee", y); g2 = graficos(s)
        ck([l.split("|")[0:3] + [l.split("|")[5]] for l in g2] == [l.split("|")[0:3] + [l.split("|")[5]] for l in esperado], "SOMA lee los graficos de LibreOffice: " + str(g2))
else:
    print("  (sin LibreOffice: se salta)")
h = os.path.join(d, "g.hoja")
rc, _ = run("demograf", h); rc2, s = run("lee", h); ck(graficos(s) == esperado, "formato propio con graficos")
rc, s = run("fuzz", x, "300"); ck(rc == 0, "fuzz de un XLSX con graficos: " + s[-300:])
shutil.rmtree(d, ignore_errors=True)
print(f"test_hoja_graf_360: {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
