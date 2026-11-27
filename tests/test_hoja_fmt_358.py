"""SOMA_358_HOJA: formatos de la hoja de calculo (XLSX, ODS, CSV y el propio de SOMA).
  - ida y vuelta con el lector y el escritor de SOMA (lo que se ve y las formulas tienen que salir iguales)
  - si hay LibreOffice: abre lo que escribe SOMA (y calcula lo mismo) y SOMA abre lo que escribe LibreOffice
  - si hay openpyxl: SOMA abre un XLSX hecho por openpyxl (formulas, estilos, varias hojas)
  - fuzz de los lectores con archivos rotos (con ASan)
    python3 tests/test_hoja_fmt_358.py"""
import os, shutil, subprocess, sys, tempfile
RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
fallos = 0; n = 0
def ck(c, m):
    global fallos, n
    n += 1
    if not c:
        fallos += 1; print("FALLO:", m)
cc = os.environ.get("HOST_CC", "cc")
d = tempfile.mkdtemp(prefix="hoja358_")
exe = os.path.join(d, "hoja_cli")
S = ["src/apps/hoja_core.c", "src/apps/hoja_graf.c", "src/apps/hoja_zip.c", "src/apps/hoja_zip_lee.c", "src/apps/hoja_xlsx.c", "src/apps/hoja_xlsx_lee.c", "src/apps/hoja_ods.c",
     "src/apps/hoja_ods_lee.c", "src/apps/web_f64.c", "src/apps/web_inflate.c", "src/core/png_enc.c"]
san = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"]
r = subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-w"] + san + ["-DWEB_HOST_TEST", "-Isrc/apps", "-Isrc/core", "tests/hoja_cli_358.c"] + S + ["-o", exe], cwd=RAIZ, capture_output=True, text=True)
if r.returncode:
    r = subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-w", "-DWEB_HOST_TEST", "-Isrc/apps", "-Isrc/core", "tests/hoja_cli_358.c"] + S + ["-o", exe], cwd=RAIZ, capture_output=True, text=True)
ck(r.returncode == 0, "compila la herramienta: " + r.stderr[:500])
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
def run(*a):
    return subprocess.run([exe] + list(a), cwd=d, capture_output=True, text=True, env=env)
if r.returncode == 0:
    for e in ("hoja", "xlsx", "ods", "csv"):
        s = run("demo", "demo." + e); ck(s.returncode == 0, "demo." + e + ": " + s.stderr[:200])
    base = run("lee", "demo.hoja").stdout
    ck("B5|934,90 €|=SUMA(B2:B4)|" in base and "B4|no hay|" in base, "el libro de ejemplo se calcula bien")
    for e in ("xlsx", "ods"):
        s = run("lee", "demo." + e); ck(s.returncode == 0 and s.stdout == base, "ida y vuelta " + e + ":\n" + "\n".join(l for l in s.stdout.splitlines() if l not in base.splitlines())[:600])
    s = run("lee", "demo.csv"); ck(s.returncode == 0 and "934,90" in s.stdout and "=1+1" not in s.stdout, "CSV de vuelta")
    # el XLSX: piezas que exige Excel
    import zipfile
    z = zipfile.ZipFile(os.path.join(d, "demo.xlsx"))
    nombres = z.namelist()
    for p in ("[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels", "xl/styles.xml", "xl/sharedStrings.xml", "xl/worksheets/sheet1.xml", "xl/worksheets/sheet2.xml"):
        ck(p in nombres, "falta " + p + " en el XLSX")
    ck(z.testzip() is None, "CRC del XLSX")
    import xml.dom.minidom
    for p in nombres:
        if p.endswith(".xml") or p.endswith(".rels"):
            try: xml.dom.minidom.parseString(z.read(p))
            except Exception as ex: ck(False, "XML mal formado en " + p + ": " + str(ex))
    hoja1 = z.read("xl/worksheets/sheet1.xml").decode()
    ck("<f>B2*21%</f>" in hoja1 and "<f>SUM(B2:B4)</f>" in hoja1 and "<f>IF(COUNTIF(A2:A4,&quot;Agua*&quot;)&gt;0,TRUE,FALSE)</f>" in hoja1, "formulas en ingles en el XLSX")
    ck("<f>IFERROR(_xlfn.XLOOKUP(" in z.read("xl/worksheets/sheet2.xml").decode(), "_xlfn. en las funciones nuevas")
    ck('state="frozen"' in hoja1 and "<autoFilter" in hoja1, "inmovilizar y autofiltro en el XLSX")
    zo = zipfile.ZipFile(os.path.join(d, "demo.ods"))
    ck(zo.namelist()[0] == "mimetype" and zo.getinfo("mimetype").compress_type == 0, "ODS: mimetype el primero y sin comprimir")
    for p in zo.namelist():
        if p.endswith(".xml"):
            try: xml.dom.minidom.parseString(zo.read(p))
            except Exception as ex: ck(False, "XML mal formado en ODS " + p + ": " + str(ex))
    ck('table:formula="of:=SUM([.B2:.B4])"' in zo.read("content.xml").decode(), "OpenFormula en el ODS")
    # openpyxl
    try:
        import openpyxl
        from openpyxl.styles import Font, PatternFill
        wb = openpyxl.Workbook(); ws = wb.active; ws.title = "Datos"
        ws["A1"] = "Producto"; ws["B1"] = "Precio"; ws["A1"].font = Font(bold=True); ws["B1"].fill = PatternFill("solid", fgColor="FFFF00")
        for i, (p, v) in enumerate([("Pan", 1.2), ("Leche", 0.95), ("Huevos", 2.5)], start=2): ws.cell(i, 1, p); ws.cell(i, 2, v)
        ws["B5"] = "=SUM(B2:B4)"; ws["B6"] = "=AVERAGE(B2:B4)"; ws["B2"].number_format = "0.00"; ws.freeze_panes = "A2"; ws.column_dimensions["A"].width = 30
        ws2 = wb.create_sheet("Otra hoja"); ws2["A1"] = "=Datos!B5*2"; ws2["A2"] = True; ws2["A3"] = "=1/0"
        wb.save(os.path.join(d, "openpyxl.xlsx"))
        s = run("lee", "openpyxl.xlsx")
        ck(s.returncode == 0 and "B5|4,65|=SUMA(B2:B4)|" in s.stdout and "A1|9,3|=Datos!B5*2|" in s.stdout and "HOJA Datos|1|0" in s.stdout and "A1|Producto|Producto|N" in s.stdout
           and "B2|1,20|" in s.stdout and "ANCHO 0 215" in s.stdout and "A2|VERDADERO|" in s.stdout, "openpyxl:\n" + s.stdout[:900])
    except ImportError:
        print("== openpyxl no esta: se salta esa parte")
    # LibreOffice
    so = shutil.which("soffice") or shutil.which("libreoffice")
    if so:
        lo = os.path.join(d, "lo"); os.makedirs(lo, exist_ok=True)
        filtro = "csv:Text - txt - csv (StarCalc):59,34,76,1,,0,false,true,false,false,false,-1"
        for e in ("xlsx", "ods"):
            sub = os.path.join(lo, e); os.makedirs(sub, exist_ok=True)
            subprocess.run([so, "--headless", "--convert-to", filtro, "--outdir", sub, os.path.join(d, "demo." + e)], capture_output=True, timeout=180)
            g = os.path.join(sub, "demo-Gastos.csv"); rr = os.path.join(sub, "demo-Resumen 2026.csv")
            ck(os.path.exists(g) and os.path.exists(rr), "LibreOffice abre demo." + e)
            if os.path.exists(g):
                t = open(g, encoding="utf-8").read(); u = open(rr, encoding="utf-8").read()
                ck("Total;934.9;" in t and ";193.964" in t and "Media;311.63" in t and "Hay agua;TRUE" in t and "Error;#DIV/0!" in t, "LibreOffice calcula lo mismo (" + e + "):\n" + t)
                ck("Con IVA;1128.864" in u and "Buscar;63.4" in u and "Hoy;no hay" in u, "LibreOffice: referencias entre hojas (" + e + "):\n" + u)
        # y al reves
        for e, de in (("xlsx", "ods"), ("ods", "xlsx")):
            sub = os.path.join(lo, "re_" + e); os.makedirs(sub, exist_ok=True)
            subprocess.run([so, "--headless", "--convert-to", e, "--outdir", sub, os.path.join(d, "demo." + de)], capture_output=True, timeout=180)
            f = os.path.join(sub, "demo." + e)
            ck(os.path.exists(f), "LibreOffice convierte a " + e)
            if os.path.exists(f):
                s = run("lee", f)
                ck(s.returncode == 0 and "B5|934,90 €|=SUMA(B2:B4)|" in s.stdout and "B3|63,4|=BUSCARV(\"Luz\";Gastos!A2:B4;2;FALSO)|" in s.stdout and "A4|Agua \"fría\" & más|" in s.stdout,
                   "SOMA abre el " + e + " de LibreOffice:\n" + s.stdout[:900])
    else:
        print("== LibreOffice no esta: se salta esa parte")
    for e in ("xlsx", "ods", "csv"):
        s = run("fuzz", "demo." + e, "300"); ck(s.returncode == 0 and "bien" in s.stdout, "fuzz " + e + ": " + s.stdout[-200:] + s.stderr[-600:])
shutil.rmtree(d, ignore_errors=True)
print(f"test_hoja_fmt_358: {n} comprobaciones, {fallos} fallos")
sys.exit(1 if fallos else 0)
