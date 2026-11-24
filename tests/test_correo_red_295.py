#!/usr/bin/env python3
# SOMA_295_CORREO: prueba de extremo a extremo del correo: arranca los servidores IMAP y SMTP de prueba (TLS 1.3, directo y STARTTLS),
# lanza el cliente de SOMA compilado en el anfitrion (build/tests/test_correo_red_295) y despues comprueba en los servidores lo que ha
# quedado: banderas, mensajes movidos, borrados y anadidos, que no se dijo nada en claro antes de cifrar, y los correos que llegaron
# por SMTP leidos con el analizador de correo de Python (cabeceras, destinatarios, Cco y cuerpo).
# Uso: python3 tests/test_correo_red_295.py build/tests/test_correo_red_295
import base64, email, email.policy, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import correo_servidores_295 as S

def crlf(s): return s.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")

PDF = b"%PDF-1.4 presupuesto SOMA\n"
MENSAJES = [
    (crlf("From: Banco <avisos@banco.es>\nTo: pepe@prueba.es\nSubject: Tu extracto de noviembre\nDate: Mon, 16 Nov 2026 08:00:00 +0100\nMessage-ID: <1@banco.es>\n\nYa tienes disponible tu extracto.\n"), ["\\Seen"]),
    (crlf("From: =?UTF-8?Q?Mar=C3=ADa_L=C3=B3pez?= <maria@ejemplo.es>\nTo: Pepe <pepe@prueba.es>\nSubject: =?UTF-8?Q?Presupuesto_reforma_ba=C3=B1o?=\n"
          "Date: Mon, 23 Nov 2026 18:05:00 +0100\nMessage-ID: <abc123@ejemplo.es>\nMIME-Version: 1.0\nContent-Type: multipart/mixed; boundary=\"MIX\"\n\n"
          "--MIX\nContent-Type: text/plain; charset=windows-1252\nContent-Transfer-Encoding: quoted-printable\n\n"
          "Hola Pepe,\n\nTe adjunto el presupuesto del ba=F1o: 4.300 =80 con IVA.\n\nUn saludo,\nMar=EDa\n"
          "--MIX\nContent-Type: application/pdf\nContent-Disposition: attachment; filename*=UTF-8''presupuesto%20ba%C3%B1o.pdf\nContent-Transfer-Encoding: base64\n\n"
          + base64.b64encode(PDF).decode() + "\n--MIX--\n"), []),
    (crlf("From: Tienda <no-reply@tienda.es>\nTo: pepe@prueba.es\nSubject: Ofertas del Black Friday\nDate: Fri, 20 Nov 2026 07:00:00 +0100\n"
          "Content-Type: text/html; charset=utf-8\n\n<html><body><p>S&oacute;lo hoy: <b>50%</b> en todo.</p></body></html>\n"), []),
    (("From: Juan García <juan@obra.es>\r\nTo: pepe@prueba.es\r\nSubject: Re: cena del sábado\r\nDate: Sat, 21 Nov 2026 12:00:00 +0100\r\n"
     "Content-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\nPerfecto, nos vemos a las 9.\r\n\r\nEl vie, 20 nov 2026, Pepe escribió:\r\n> Te parece bien a las 9?\r\n").encode("utf-8"), ["\\Seen"]),
    (crlf("From: Juan <juan@obra.es>\nTo: pepe@prueba.es\nSubject: Fotos de la obra\nDate: Sun, 22 Nov 2026 12:00:00 +0100\nMIME-Version: 1.0\n"
          "Content-Type: multipart/mixed; boundary=\"B\"\n\n--B\nContent-Type: text/plain\n\nVan las fotos.\n--B\nContent-Type: application/zip; name=\"fotos.zip\"\n"
          "Content-Transfer-Encoding: base64\n\n") + base64.encodebytes(bytes(range(256)) * 1200).replace(b"\n", b"\r\n") + b"--B--\r\n", []),
    (crlf("From: Jefa <jefa@empresa.es>\nTo: pepe@prueba.es\nSubject: =?UTF-8?B?UmV1bmnDs24gZWwgbHVuZXM=?=\nDate: Tue, 24 Nov 2026 09:00:00 +0100\n\nEl lunes a las 10 en la sala grande.\n"), ["\\Flagged"]),
]

ok = total = 0
def chk(c, que):
    global ok, total
    total += 1
    if c: ok += 1
    else: print("FALLO:", que)

def main():
    if len(sys.argv) < 2: print("uso: test_correo_red_295.py CLIENTE [APP [CARPETA_PNG]]"); return 2
    d = tempfile.mkdtemp()
    cert, clave = os.path.join(d, "c.pem"), os.path.join(d, "k.pem")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", clave, "-out", cert, "-days", "2", "-subj", "/CN=localhost"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    cliente(cert, clave)
    if len(sys.argv) > 2: aplicacion(cert, clave, sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    print("== test_correo_red_295: %d/%d" % (ok, total))
    return 0 if ok == total else 1

def aplicacion(cert, clave, binario, capturas):
    """la aplicacion Correo entera, usada como un usuario; despues, lo que quedo en los servidores"""
    est = S.Estado(MENSAJES)
    sv = S.arranca(est, cert, clave)
    p = lambda n: str(sv[n].server_address[1])
    args = [binario, p("imap"), p("smtp")] + ([capturas] if capturas else [])
    r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=400)
    print(r.stdout.decode("utf-8", "replace"), end="")
    chk(r.returncode == 0, "la aplicacion Correo pasa sus comprobaciones")
    b = est.buzones
    uids = {m.uid: m for m in b["INBOX"].msgs}
    chk(sorted(uids) == [1, 2, 4, 5, 6], "app: en Recibidos falta solo el borrado")
    chk(2 in uids and {"\\Seen", "\\Answered"} <= uids[2].flags, "app: el respondido queda leido y respondido")
    chk(1 in uids and "\\Seen" not in uids[1].flags, "app: marcar como no leido llega al servidor")
    pap = [m for m in b["Papelera"].msgs]
    chk(len(pap) == 1 and S._cab(pap[0].raw, "Subject") == "Ofertas del Black Friday" and "\\Flagged" in pap[0].flags, "app: el destacado y borrado esta en la Papelera con su estrella")
    chk(len(b["Enviados"].msgs) == 3 and all("\\Seen" in m.flags for m in b["Enviados"].msgs), "app: los tres enviados se guardaron en Enviados")
    chk(len(est.smtp) == 3, "app: tres correos enviados por SMTP")
    if len(est.smtp) == 3:
        m0 = email.message_from_bytes(est.smtp[0]["datos"], policy=email.policy.default)
        c0 = m0.get_content().replace("\r\n", "\n")
        chk(str(m0["Subject"]) == "Re: Presupuesto reforma ba\u00f1o" and m0["In-Reply-To"] == "<abc123@ejemplo.es>", "app: la respuesta, en el hilo")
        chk(c0.startswith("Estimada Mar\u00eda:") and "> Hola Pepe," in c0 and "4.300 \u20ac" in c0 and m0["From"].addresses[0].display_name == "Jos\u00e9 Ram\u00f3n", "app: el cuerpo mejorado y la cita del original")
        m1 = email.message_from_bytes(est.smtp[1]["datos"], policy=email.policy.default)
        chk(est.smtp[1]["para"] == ["juan@obra.es"] and str(m1["Subject"]) == "Cambio al martes" and "\u00bflo pasamos al martes?" in m1.get_content(), "app: el redactado con IA, a Juan")
        m2 = email.message_from_bytes(est.smtp[2]["datos"], policy=email.policy.default)
        chk(est.smtp[2]["para"] == ["maria@ejemplo.es"] and str(m2["Subject"]) == "Llego tarde" and "10 minutos tarde" in m2.get_content(), "app: el del orbe, despues del \u00abenv\u00edalo\u00bb")
    for s in sv.values(): s.shutdown()

def cliente(cert, clave):
    est = S.Estado(MENSAJES)
    sv = S.arranca(est, cert, clave)
    p = lambda n: str(sv[n].server_address[1])
    r = subprocess.run([sys.argv[1], p("imap"), p("imap_starttls"), p("smtp"), p("smtp_starttls")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
    salida = r.stdout.decode("utf-8", "replace")
    print(salida, end="")
    chk(r.returncode == 0, "el cliente de SOMA pasa sus comprobaciones")
    # ---- lo que queda en el servidor IMAP
    b = est.buzones
    chk([m.uid for m in b["INBOX"].msgs] == [2, 5, 6], "Recibidos: quedan el 2, el 5 y el 6")
    m2 = [m for m in b["INBOX"].msgs if m.uid == 2]
    m6 = [m for m in b["INBOX"].msgs if m.uid == 6]
    chk(m2 and "\\Seen" in m2[0].flags, "el 2 quedo leido")
    chk(m6 and "\\Flagged" not in m6[0].flags, "al 6 se le quito la estrella")
    pap = sorted(S._cab(m.raw, "Subject") for m in b["Papelera"].msgs)
    chk(pap == ["Ofertas del Black Friday", "Re: cena del sábado"], "Papelera: el movido con MOVE y el movido con COPY")
    chk(len(b["Enviados"].msgs) == 1 and S._cab(b["Enviados"].msgs[0].raw, "Subject") == "Copia en Enviados" and "\\Seen" in b["Enviados"].msgs[0].flags, "Enviados: la copia anadida, leida")
    en_claro = [o for o in est.ordenes if o.startswith("claro ")]
    chk(en_claro and all(len(o.split(" ")) > 2 and o.split(" ")[2].upper() in ("CAPABILITY", "STARTTLS") for o in en_claro), "antes de cifrar solo CAPABILITY y STARTTLS (nada de contrasenas en claro)")
    if not en_claro or not all(len(o.split(" ")) > 2 and o.split(" ")[2].upper() in ("CAPABILITY", "STARTTLS") for o in en_claro): print("   en claro:", en_claro)
    chk(not any("cl@ve" in o for o in est.ordenes), "la contrasena no aparece en ninguna orden")
    # ---- lo que llego por SMTP
    chk(len(est.smtp) == 2, "llegaron dos correos (el del destinatario rechazado no)")
    if len(est.smtp) >= 1:
        e = est.smtp[0]
        chk(e["puerto"] == "465" and e["auth"] == "PLAIN" and e["de"] == "pepe@prueba.es", "el primero: puerto 465, AUTH PLAIN, remitente")
        chk(sorted(e["para"]) == sorted(["maria@ejemplo.es", "ana@x.com", "obra@x.com", "oculto@x.com"]), "destinatarios del sobre (con el Cco)")
        datos = e["datos"]
        chk(all(x < 128 for x in datos) and max(len(l) for l in datos.split(b"\r\n")) <= 78, "el mensaje es ASCII y sus lineas cortas")
        msg = email.message_from_bytes(datos, policy=email.policy.default)
        chk(str(msg["Subject"]) == "Presupuesto reforma baño: ¡adelante!", "Python lee el asunto")
        chk(msg["From"].addresses[0].display_name == "José Ramón" and msg["From"].addresses[0].addr_spec == "pepe@prueba.es", "Python lee el remitente")
        para = [(a.display_name, a.addr_spec) for a in msg["To"].addresses]
        chk(para == [("María López", "maria@ejemplo.es"), ("García, Ana", "ana@x.com")], "Python lee los destinatarios (nombre con coma incluido)")
        chk(str(msg["Cc"]) == "obra@x.com" and msg["Bcc"] is None, "Cc si; Cco no va en las cabeceras")
        chk(msg["In-Reply-To"] == "<abc123@ejemplo.es>" and "<abc123@ejemplo.es>" in str(msg["References"]), "en el hilo del mensaje respondido")
        chk(msg["Message-ID"].endswith("@prueba.es>") and msg["Date"].datetime.utcoffset().total_seconds() == 3600, "Message-ID y fecha con su zona")
        cuerpo = msg.get_content().replace("\r\n", "\n")
        esperado = "Hola María:\n\nPerfecto, adelante con el presupuesto de 4.300 €.\n.Esta línea empieza por punto.\n\nUn saludo,\nJosé\n"
        chk(cuerpo == esperado, "Python lee el cuerpo igual (tildes, euro y la linea que empieza por punto)")
        if cuerpo != esperado: print("   cuerpo:", repr(cuerpo))
    if len(est.smtp) >= 2:
        e = est.smtp[1]
        msg = email.message_from_bytes(e["datos"], policy=email.policy.default)
        chk(e["puerto"] == "587" and e["auth"] == "LOGIN" and e["para"] == ["juan@obra.es"] and str(msg["Subject"]) == "Por el 587", "el segundo: STARTTLS y AUTH LOGIN")
    for s in sv.values(): s.shutdown()

if __name__ == "__main__":
    sys.exit(main())
