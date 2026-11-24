#!/usr/bin/env python3
"""Prueba de fallos de SOMA en QEMU (x86_64, consola de texto, solo Linux/macOS con QEMU).

Uso:  python3 tools/fault_check.py [--root DIR]

Escenario 1 'crash': el comando provoca un fallo de pagina. Exige "KERNEL PANIC", un
  BACKTRACE cuyas direcciones se resuelven a las funciones reales (cmd_crash...) y que
  QEMU salga con el codigo 71 (panic observable por el host).
Escenario 2 'hang': el comando retiene la CPU 8 s sin ceder. Exige que el vigilante avise
  UNA vez con backtrace dentro de cmd_hang/hang_spin y que el sistema se recupere
  (el shell responde y 'watchdog' cuenta 1 aviso).
Escenario 3 'deadlock': dos hilos cogen dos mutex en orden opuesto. Exige que se detecte el ciclo
  con los nombres de hilos y mutex, que el sistema siga respondiendo y que matar UN hilo recupere
  su mutex, deje terminar al otro y no quede ocupado ni dl_A/dl_B ni hilo dl_* vivo.
Codigos de salida: 0 = OK, 1 = fallo, 2 = no se puede probar (falta QEMU o la imagen).
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def boot(root: Path, qemu: str, td: Path, tag: str, extra=(), disk_tag=None, disk_mb=16):
    disk, log = td / f"hdd_{disk_tag or tag}.img", td / f"serial_{tag}.log"
    if not (disk_tag and disk.exists()):          # disk_tag: reutiliza el disco de un arranque anterior
        with open(disk, "wb") as f:
            f.truncate(disk_mb * 1024 * 1024)
    out = open(log, "wb")
    proc = subprocess.Popen(
        [qemu, "-machine", "pc", "-m", "256M", "-no-reboot", "-cdrom", str(root / "build" / "soma.iso"),
         "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0",
         "-drive", f"file={disk},format=raw,if=virtio",
         "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
         "-vga", "none", "-serial", "stdio", "-display", "none", *extra],
        stdin=subprocess.PIPE, stdout=out, stderr=subprocess.DEVNULL)
    return proc, log, out


def text(log: Path) -> str:
    return ANSI.sub("", log.read_bytes().decode(errors="replace")).replace("\r", "")


def wait_for(log: Path, needle: str, timeout: float, proc=None) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        if needle in text(log):
            return True
        if proc is not None and proc.poll() is not None:
            return needle in text(log)
        time.sleep(0.3)
    return False


def symbolized(root: Path, log: Path) -> str:
    r = subprocess.run([sys.executable, str(root / "tools" / "symbolize.py"), str(log),
                        str(root / "build" / "x86_64" / "soma.elf")], capture_output=True, text=True)
    return ANSI.sub("", r.stdout)


def scenario_crash(root, qemu, td) -> list:
    fails = []
    proc, log, out = boot(root, qemu, td, "crash")
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        proc.stdin.write(b"crash\n"); proc.stdin.flush()
        try:
            rc = proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            return ["el kernel no se detuvo tras 'crash'"]
        t = text(log)
        if rc != 71:
            fails.append(f"codigo de salida {rc} (esperado 71 = panic observable)")
        for needle in ("KERNEL PANIC", "BACKTRACE (hilo 0 'kernel_main')", "Page Fault"):
            if needle not in t:
                fails.append(f"falta '{needle}' en la salida")
        s = symbolized(root, log)
        for fn in ("crash_level3", "cmd_crash", "dispatch_command", "kernel_main"):
            if fn not in s:
                fails.append(f"el backtrace no resuelve a '{fn}'")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_hang(root, qemu, td) -> list:
    fails = []
    proc, log, out = boot(root, qemu, td, "hang")
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        proc.stdin.write(b"hang 8\n"); proc.stdin.flush()
        if not wait_for(log, "[WATCHDOG]", 15):
            return ["el vigilante no aviso en 15 s"]
        if not wait_for(log, "hang: terminado", 15):
            fails.append("el sistema no se recupero tras el cuelgue")
        proc.stdin.write(b"watchdog\n"); proc.stdin.flush()
        if not wait_for(log, "avisos: 1", 10):
            fails.append("'watchdog' no cuenta exactamente 1 aviso")
        t = text(log)
        if t.count("[WATCHDOG]") != 1:
            fails.append(f"{t.count('[WATCHDOG]')} avisos en 8 s (esperado 1)")
        s = symbolized(root, log)
        if "cmd_hang" not in s and "hang_spin" not in s:
            fails.append("el backtrace del aviso no senala la funcion que acapara la CPU")
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("el shell no responde tras el cuelgue")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_ring3(root, qemu, td, extra=(), tag="ring3", expect_feat=None) -> list:
    """SOMA_PARCHE_014_RING3: programas en ring 3: syscalls, aislamiento (#PF/#GP), plazo; kernel sigue vivo.
    Con extra=-cpu ...,+smep,+smap (parche 017) se exige ademas que SMEP/SMAP queden activos y todo siga bien."""
    fails = []
    proc, log, out = boot(root, qemu, td, tag, extra)
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        proc.stdin.write(b"usertest\n"); proc.stdin.flush()
        if not wait_for(log, "USERMODE: OK", 20):
            t = text(log)
            i = t.find("USERMODE")
            return ["usertest no termino en OK: " + (t[i:i + 600].replace("\n", " | ") if i >= 0 else "sin salida")]
        t = text(log)
        if expect_feat and expect_feat not in text(log):
            fails.append(f"no se activo la caracteristica esperada: {expect_feat!r}")
        for needle in ("PML4 del kernel sin entradas de usuario", "hola desde ring 3", "vector 14", "vector 13", "limite de tiempo", "B: vivo mientras", "relee su valor 0xAA", "marcos libres antes/despues"):
            if needle not in t:
                fails.append(f"falta en la salida: {needle!r}")
        if "KERNEL PANIC" in t:
            fails.append("el kernel entro en panic")
        proc.stdin.write(b"usertest\n"); proc.stdin.flush()      # repetible (paginas liberadas y recreadas)
        t0 = time.time()
        while time.time() - t0 < 20 and text(log).count("USERMODE: OK") < 2:
            time.sleep(0.2)
        if text(log).count("USERMODE: OK") < 2:
            fails.append("la segunda ejecucion de usertest fallo")
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("el shell no responde tras ejecutar procesos de usuario")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_exec(root, qemu, td) -> list:
    """SOMA_PARCHE_016_EXEC: programas ELF desde el sistema de archivos: carga, .data/.bss, aislamiento,
    rechazo de archivos invalidos y dos procesos que se turnan de verdad."""
    import re
    fails = []
    proc, log, out = boot(root, qemu, td, "exec")
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        def send(c):
            proc.stdin.write((c + "\n").encode()); proc.stdin.flush()
        send("exec /bin/hello.elf")
        if not wait_for(log, "exec: terminado", 15) or ".data y .bss correctos" not in text(log):
            fails.append("hello.elf no se cargo/ejecuto bien (.data/.bss)")
        n0 = text(log).count("exec: terminado")
        send("exec /bin/fault.elf")
        if not wait_for(log, "vector 14", 15):
            fails.append("fault.elf no murio con #PF")
        send('write /tmp/corto.elf "esto no es un ELF"')
        time.sleep(1)
        send("exec /tmp/corto.elf")
        if not wait_for(log, "exec: rechazado: archivo demasiado corto", 10):
            fails.append("un archivo de 17 bytes no fue rechazado como demasiado corto")
        send('write /tmp/basura.elf "' + "x" * 90 + '"')
        time.sleep(1)
        send("exec /tmp/basura.elf")
        if not wait_for(log, "exec: rechazado: no es un ELF", 10):
            fails.append("un archivo largo que no es ELF no fue rechazado por su firma")
        send("exec /bin/no_existe.elf")
        time.sleep(1)
        # dos procesos a la vez: los 'tick 1' de ambos deben salir antes del primer 'tick 2'
        send("exec -b /bin/ticker.elf")
        send("exec -b /bin/ticker.elf")
        t0 = time.time()
        while time.time() - t0 < 15 and len(re.findall(r"tick 5", text(log))) < 2:
            time.sleep(0.3)
        t = text(log)
        ticks = re.findall(r"\[ticker\] pid=(\d+) tick (\d)", t)
        pids = sorted(set(p for p, _ in ticks))
        if len(pids) != 2:
            fails.append(f"se esperaban 2 procesos ticker, hubo {len(pids)}")
        else:
            seq = [(p, int(k)) for p, k in ticks]
            first_tick2 = next((i for i, (p, k) in enumerate(seq) if k == 2), len(seq))
            tick1_pids = set(p for p, k in seq[:first_tick2] if k == 1)
            if len(tick1_pids) != 2:
                fails.append("los dos procesos no se turnan (el segundo empieza tras acabar el primero)")
        send("procs")
        if not wait_for(log, "PROCESOS DE USUARIO", 10):
            fails.append("procs no responde")
        t = text(log)
        if "KERNEL PANIC" in t:
            fails.append("el kernel entro en panic")
        send("ps")
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("el shell no responde tras ejecutar programas")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_bigfs(root, qemu, td) -> list:
    """SOMA_PARCHE_018_BIGFS: almacen de archivos grandes en QEMU: formatear, directorios, escritura/lectura,
    rendimiento con verificacion byte a byte, fsck y PERSISTENCIA tras reiniciar con el mismo disco."""
    fails = []
    proc, log, out = boot(root, qemu, td, "bigfs1", disk_tag="bigfs", disk_mb=32)
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        if "sin formatear" not in text(log):
            fails.append("el arranque no avisa de que el BIGFS esta sin formatear")
        def send(c, needle=None, timeout=20):
            proc.stdin.write((c + "\n").encode()); proc.stdin.flush()
            return wait_for(log, needle, timeout) if needle else True
        if not send("big format yes", "BIGFS formateado"): fails.append("big format yes")
        if not send("big mkdir /musica", None): pass
        time.sleep(1)
        send("big write /musica/nota.txt hola desde bigfs"); time.sleep(1)
        send("big cat /musica/nota.txt"); time.sleep(1)
        if "hola desde bigfs" not in text(log).split("big cat /musica/nota.txt")[-1]:
            fails.append("big cat no devuelve lo escrito")
        send("big bench 4")
        if not wait_for(log, "bytes distintos: 0", 40):
            fails.append("big bench: fallo o bytes distintos")
        send("big fsck")
        if not wait_for(log, "fsck: 0 problemas", 15):
            fails.append("big fsck no sale limpio")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    proc, log, out = boot(root, qemu, td, "bigfs2", disk_tag="bigfs", disk_mb=32)
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return fails + ["el segundo arranque no llego a la consola"]
        if "formateada (SOMABIG1)" not in text(log):
            fails.append("tras reiniciar, el arranque no detecta el BIGFS formateado")
        proc.stdin.write(b"big cat /musica/nota.txt\n"); proc.stdin.flush(); time.sleep(2)
        if "hola desde bigfs" not in text(log).split("big cat /musica/nota.txt")[-1]:
            fails.append("el archivo no persistio tras reiniciar")
        proc.stdin.write(b"big fsck\n"); proc.stdin.flush()
        if not wait_for(log, "fsck: 0 problemas", 15):
            fails.append("big fsck tras reiniciar")
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("el shell no responde")
        if "KERNEL PANIC" in text(log):
            fails.append("el kernel entro en panic")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_bigexec(root, qemu, td, extra=(), tag="bigexec", expect_feat=None) -> list:
    """SOMA_PARCHE_019_FILES: la cadena completa Mac -> disco -> kernel -> programa de usuario:
    tools/bigfs.py mete un ELF grande y archivos de 3 y 2 MiB en el disco; el kernel ejecuta el ELF desde /big/
    y dos procesos leen en streaming (a la vez) y devuelven el FNV que calculo el host."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    fails = []
    elf = root / "build" / "x86_64" / "user" / "bigprog.elf"
    if not elf.is_file():
        return ["falta build/x86_64/user/bigprog.elf (compila con 'make')"]
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    d1 = bytes(((i * 2654435761 + 9 * 40503) >> 13) & 0xFF for i in range(3 * 1024 * 1024 + 123))
    d2 = bytes(((i * 2654435761 + 4 * 40503) >> 13) & 0xFF for i in range(2 * 1024 * 1024 + 7))
    fs.write_file("/data/uno.bin", d1)
    fs.write_file("/data/dos.bin", d2)
    fs.write_file("/bin/bigprog.elf", elf.read_bytes())
    fs.write_file("/bin/basura.elf", b"x" * 200)
    fs.close()
    proc, log, out = boot(root, qemu, td, tag, extra, disk_tag=tag)
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        def send(c):
            proc.stdin.write((c + "\n").encode()); proc.stdin.flush()
        send("exec /big/bin/bigprog.elf uno dos")
        if not wait_for(log, "[bigprog] OK", 30) or "args='uno dos'" not in text(log):
            fails.append("el ELF grande (271 KB, solo cabe en BIGFS) no se cargo/ejecuto bien")
        send("exec /bin/sumfile.elf /data/uno.bin")
        want = f"bytes={len(d1)} size={len(d1)} fnv={B.fnv64(d1):x} seek=ok"
        if not wait_for(log, want, 40):
            fails.append("sumfile: el FNV de un archivo de 3 MiB no coincide con el del host")
        for needle in ("ruta de kernel rechazada", "lectura a memoria del kernel rechazada"):
            if needle not in text(log):
                fails.append(f"el kernel no rechazo: {needle}")
        send("exec /bin/sumfile.elf /noexiste")
        if not wait_for(log, "no se puede abrir: -2", 15):
            fails.append("abrir un archivo inexistente no devolvio -2")
        send("exec /big/bin/noexiste.elf")
        if not wait_for(log, "exec: no se puede leer /big/bin/noexiste.elf", 10):
            fails.append("exec de un archivo inexistente de /big/ no informa")
        send("exec /big/bin/basura.elf")
        if not wait_for(log, "exec: ELF invalido", 10):
            fails.append("un archivo de /big/ que no es ELF no fue rechazado")
        # dos procesos a la vez, archivos distintos (cada uno con su descriptor y su bufer)
        send("exec -b /bin/sumfile.elf /data/uno.bin")
        send("exec -b /bin/sumfile.elf /data/dos.bin")
        w1 = f"/data/uno.bin bytes={len(d1)} size={len(d1)} fnv={B.fnv64(d1):x} seek=ok"
        w2 = f"/data/dos.bin bytes={len(d2)} size={len(d2)} fnv={B.fnv64(d2):x} seek=ok"
        t0 = time.time()
        while time.time() - t0 < 60 and not (w1 in text(log) and w2 in text(log)):
            time.sleep(0.3)
        if w1 not in text(log) or w2 not in text(log):
            fails.append("los dos procesos concurrentes no devolvieron cada uno el FNV correcto de SU archivo")
        send("procs"); time.sleep(1.5)
        send("ps")
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("el shell no responde")
        if "KERNEL PANIC" in text(log):
            fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in text(log):
            fails.append(f"no se activo la caracteristica esperada: {expect_feat!r}")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_audio(root, qemu, td, extra=(), tag="audio", device=True, expect_feat=None) -> list:
    """SOMA_PARCHE_020_AUDIO: audio de extremo a extremo con un HDA emulado y la salida capturada en un WAV:
    patron del kernel ('audio test'), un WAV estereo del almacen tocado por el kernel, un WAV MONO tocado por el
    programa de usuario wavplay (open/read/seek + audio_open/write/close), rechazo cuando el audio esta ocupado
    y, al final, la captura se compara BYTE A BYTE con lo esperado (silencio entre pistas, sin eco ni repeticion).
    Con device=False: sin HDA el sistema arranca y todo informa 'sin dispositivo'."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import wavcheck as W
    fails = []
    A = W.pattern_pcm(66150)                                   # 1,5 s estereo
    M = W.mono_pcm(44100)                                      # 1 s mono
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/musica/a.wav", W.build_wav(A, 44100, 2))
    fs.write_file("/musica/m.wav", W.build_wav(M, 44100, 1))
    fs.close()
    wav = td / f"captura_{tag}.wav"
    dev = ["-audiodev", f"wav,id=snd0,path={wav},out.frequency=44100,out.channels=2,out.format=s16",
           "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"] if device else []
    proc, log, out = boot(root, qemu, td, tag, [*extra, *dev], disk_tag=tag)
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        def send(c):
            proc.stdin.write((c + "\n").encode()); proc.stdin.flush()
        if not device:
            if "hda: no hay controlador de audio" not in text(log):
                fails.append("sin dispositivo: el arranque no lo dice")
            send("audio info")
            if not wait_for(log, "audio: sin dispositivo", 10): fails.append("audio info sin dispositivo")
            send("audio test")
            if not wait_for(log, "audio test: no se puede", 10): fails.append("audio test sin dispositivo no informa")
            send("exec -t 20000 /bin/wavplay.elf /musica/a.wav")
            if not wait_for(log, "no se pudo abrir el audio: -19", 15): fails.append("wavplay sin dispositivo debe recibir -19")
            send("ps")
            if not wait_for(log, "TABLA DE HILOS", 10): fails.append("el shell no responde")
            return fails
        if "hda: salida lista" not in text(log):
            return ["el HDA no se inicializo: " + text(log)[text(log).find("AUDIO"):][:300]]
        send("audio info")
        if not wait_for(log, "audio: Intel HDA", 10): fails.append("audio info")
        send("audio test")
        if not wait_for(log, "audio test: terminado", 20): fails.append("audio test no termino")
        send("audio play /musica/a.wav")
        time.sleep(0.6)                                        # a medias: el dispositivo esta ocupado
        send("audio play /musica/m.wav")
        if not wait_for(log, "audio play: no se puede (ocupado", 10): fails.append("un segundo audio play no fue rechazado por ocupado")
        send("exec -t 20000 /bin/wavplay.elf /musica/m.wav")
        if not wait_for(log, "no se pudo abrir el audio: -16", 15): fails.append("wavplay con el audio ocupado debe recibir -16")
        t0 = time.time()
        while time.time() - t0 < 20 and text(log).count("audio play: terminado") < 1:
            time.sleep(0.3)
        if text(log).count("audio play: terminado") < 1: fails.append("el reproductor del kernel no termino")
        send("exec -t 20000 /bin/wavplay.elf /musica/m.wav")
        if not wait_for(log, "[wavplay] /musica/m.wav: 176400 bytes tocados", 30): fails.append("wavplay (usuario) no toco el WAV mono")
        time.sleep(0.5)
        send("audio info")
        time.sleep(1.5)
        t = text(log)
        if "cerrado" not in t.split("audio info")[-1]: fails.append("el audio no quedo cerrado tras reproducir")
        if t.count("infrautilizaciones 0") < 2: fails.append("hubo infrautilizaciones (huecos audibles) en alguna reproduccion")
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in t: fails.append(f"no se activo: {expect_feat!r}")
        send("ps")
        if not wait_for(log, "TABLA DE HILOS", 10): fails.append("el shell no responde")
    finally:
        if proc.poll() is None:
            proc.terminate()                                   # cierre limpio: QEMU termina de escribir el WAV
            try:
                proc.wait(10)
            except Exception:
                proc.kill(); proc.wait()
        out.close()
    if device and not fails:
        cap, rate, ch = W.read_wav_pcm(wav)
        if rate != 44100 or ch != 2:
            fails.append(f"la captura no es 44100 Hz estereo ({rate}, {ch})")
        probs = W.check_multi(cap, [W.pattern_pcm(44100), A, W.mono_as_stereo(M)])
        fails.extend("captura: " + p for p in probs)
    return fails


def scenario_fpu(root, qemu, td) -> list:
    """SOMA_PARCHE_021_FPU: cuatro procesos de usuario a la vez hacen coma flotante (SSE2 y x87) con modos de redondeo
    distintos y se turnan por preemption: cada uno debe devolver BIT A BIT el resultado que calcula el host (aritmetica
    exacta con el redondeo de su modo), conservar su MXCSR y repetir el calculo identico. Sin guardar el estado por
    proceso, todos devolvian el mismo valor corrupto."""
    sys.path.insert(0, str(root / "tools"))
    import fpcheck as F
    fails = []
    jobs = [(0, 2000000), (0, 1500000), (1, 150000), (2, 120000)]
    proc, log, out = boot(root, qemu, td, "fpu")
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        for m, n in jobs:
            proc.stdin.write(f"exec -b -t 120000 /bin/ssetest.elf {m} {n}\n".encode()); proc.stdin.flush()
        t0 = time.time()
        while time.time() - t0 < 90 and text(log).count("[ssetest]") < len(jobs):
            time.sleep(0.3)
        t = text(log)
        for m, n in jobs:
            want = f"[ssetest] modo={m} N={n} sum={F.series_bits(m, n):016x} dos_pasadas=ok x87=ok mxcsr=ok"
            if want not in t:
                got = [l for l in t.splitlines() if f"modo={m} N={n} " in l]
                fails.append(f"modo {m}, N={n}: esperado {want!r}; salio {got[0] if got else 'nada'!r}")
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        if not wait_for(log, "TABLA DE HILOS", 10): fails.append("el shell no responde")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def screendump(sock: Path, dest: Path) -> None:
    import socket
    s = socket.socket(socket.AF_UNIX); s.connect(str(sock)); s.settimeout(3); time.sleep(0.2)
    s.sendall(f"screendump {dest}\n".encode()); time.sleep(1.2); s.close()


def scenario_fb(root, qemu, td, extra=(), tag="fb", expect_feat=None) -> list:
    """SOMA_PARCHE_021_FPU: pantalla desde un proceso de usuario. /autoexec.cmd (en el almacen BIGFS) lanza fbtest dos
    veces: una ANTES de la GUI (primer plano) y otra EN SEGUNDO PLANO cuando la GUI ya esta dibujando. Las capturas de
    pantalla de QEMU deben ser IDENTICAS pixel a pixel al patron, tambien con la GUI activa (bloqueo de dibujo), y al
    terminar la pantalla vuelve a ser la de la GUI (1920x1080)."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import fbcheck as C
    fails = []
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/autoexec.cmd", b"# SOMA: prueba de pantalla\nexec -t 30000 /bin/fbtest.elf 800 600 0 4000\n"
                                   b"exec -b -t 60000 /bin/fbtest.elf 800 600 6000 5000\n")
    fs.close()
    sock = td / f"mon_{tag}.sock"
    proc, log, out = boot(root, qemu, td, tag, [*extra, "-vga", "std", "-monitor", f"unix:{sock},server,nowait"], disk_tag=tag)
    try:
        if not wait_for(log, "[fbtest] listo 800x600 rechazos=ok", 60):
            return ["el primer fbtest no llego a 'listo' (autoexec/exec/pantalla)"]
        screendump(sock, td / f"{tag}_1.ppm")
        for p in C.check(td / f"{tag}_1.ppm", 800, 600): fails.append("antes de la GUI: " + p)
        if not wait_for(log, "[fbtest] cerrado", 15): fails.append("el primer fbtest no cerro")
        t0 = time.time()
        while time.time() - t0 < 40 and text(log).count("[fbtest] listo 800x600 rechazos=ok") < 2:
            time.sleep(0.2)
        if text(log).count("[fbtest] listo") < 2:
            return fails + ["el segundo fbtest (con la GUI ya activa) no llego a 'listo'"]
        screendump(sock, td / f"{tag}_2.ppm")
        for p in C.check(td / f"{tag}_2.ppm", 800, 600): fails.append("con la GUI activa: " + p)
        t0 = time.time()
        while time.time() - t0 < 20 and text(log).count("[fbtest] cerrado") < 2:
            time.sleep(0.2)
        time.sleep(2.5)
        screendump(sock, td / f"{tag}_3.ppm")
        w, h, _ = C.read_ppm(td / f"{tag}_3.ppm")
        if (w, h) != (1920, 1080): fails.append(f"al terminar, la pantalla es {w}x{h} (la GUI esperaba 1920x1080)")
        if "rechazos=ERROR" in text(log): fails.append("el kernel no rechazo un blit/fb_open invalido")
        if "KERNEL PANIC" in text(log): fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in text(log): fails.append(f"no se activo: {expect_feat!r}")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_video(root, qemu, td, extra=(), tag="video", expect_feat=None) -> list:
    """SOMA_PARCHE_022_VIDEO: video de extremo a extremo. tools/mkavi.py crea un AVI (MJPG 320x240 + PCM estereo 44,1 kHz);
    se mete en BIGFS junto con vidplay.elf y un /autoexec.cmd que lo reproduce con la GUI aun sin arrancar. Con un HDA
    emulado y la salida capturada en un WAV, y la pantalla volcada por el monitor de QEMU:
      - la captura de audio debe ser IDENTICA byte a byte al PCM del AVI (sin huecos ni repeticiones);
      - el ultimo fotograma en pantalla debe ser IDENTICO pixel a pixel al que decodifica el MISMO decodificador en el host,
        centrado en 800x600, y el resto de la pantalla negro;
      - todos los fotogramas mostrados, ninguno malo, y el reloj de audio mantiene el retraso maximo acotado."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import fbcheck as C
    import wavcheck as W
    fails = []
    elf = root / "build" / "x86_64" / "user" / "vidplay.elf"
    if not elf.is_file():
        return ["falta build/x86_64/user/vidplay.elf (compila con 'make')"]
    avi = td / "clip.avi"
    N = 40
    subprocess.run([sys.executable, str(root / "tools" / "mkavi.py"), str(avi), "--w", "320", "--h", "240", "--fps", "25", "--frames", str(N)],
                   check=True, stdout=subprocess.DEVNULL)
    fast = td / "rapido.avi"                                    # 500 fps: obliga a saltar fotogramas (si la maquina no da abasto)
    NF = 60
    subprocess.run([sys.executable, str(root / "tools" / "mkavi.py"), str(fast), "--w", "320", "--h", "240", "--fps", "500", "--frames", str(NF)],
                   check=True, stdout=subprocess.DEVNULL)
    pcm_fast = Path(f"{fast}.pcm").read_bytes()
    j2p = td / "jpeg2ppm"
    subprocess.run(["cc", "-O1", "-o", str(j2p), str(root / "tests" / "jpeg2ppm.c"), str(root / "src" / "core" / "jpeg.c")], check=True)
    last = td / "ultimo.ppm"
    subprocess.run([str(j2p), f"{avi}.f{N - 1}.jpg", str(last)], check=True)
    ref_w, ref_h, ref_rgb = C.read_ppm(last)
    pcm = Path(f"{avi}.pcm").read_bytes()
    cut = avi.read_bytes()[:30000]
    pcm_cut = b""                                            # audio de los chunks COMPLETOS del AVI cortado
    mv = cut.find(b"movi") + 4
    while mv + 8 <= len(cut):
        fcc, sz = cut[mv:mv + 4], int.from_bytes(cut[mv + 4:mv + 8], "little")
        if mv + 8 + sz > len(cut): break
        if fcc[2:] == b"wb": pcm_cut += cut[mv + 8:mv + 8 + sz]
        mv += 8 + sz + (sz & 1)
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/bin/vidplay.elf", elf.read_bytes())
    fs.write_file("/video/clip.avi", avi.read_bytes())
    fs.write_file("/video/roto.avi", cut)
    fs.write_file("/video/rapido.avi", fast.read_bytes())
    fs.write_file("/autoexec.cmd", b"# SOMA: prueba de video\nexec -t 60000 /big/bin/vidplay.elf /video/clip.avi 6000 nodrop\n"
                                   b"exec -t 30000 /big/bin/vidplay.elf /video/roto.avi 0\n"
                                   b"exec -t 30000 /big/bin/vidplay.elf /video/noexiste.avi\n"
                                   b"exec -t 30000 /big/bin/vidplay.elf /video/rapido.avi 0\n")
    fs.close()
    sock = td / f"mon_{tag}.sock"
    wav = td / f"captura_{tag}.wav"
    dev = ["-audiodev", f"wav,id=snd0,path={wav},out.frequency=44100,out.channels=2,out.format=s16",
           "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"]
    proc, log, out = boot(root, qemu, td, tag, [*extra, *dev, "-vga", "std", "-monitor", f"unix:{sock},server,nowait"], disk_tag=tag)
    try:
        if not wait_for(log, "[vidplay] reproduccion terminada", 90):
            return ["vidplay no llego a terminar el video (autoexec/exec/pantalla/audio): " + text(log)[-400:]]
        time.sleep(0.8)
        screendump(sock, td / f"{tag}_1.ppm")
        w, h, rgb = C.read_ppm(td / f"{tag}_1.ppm")
        if (w, h) != (800, 600):
            fails.append(f"la pantalla durante el video es {w}x{h} (800x600 esperado)")
        else:
            ox, oy = (800 - ref_w) // 2, (600 - ref_h) // 2
            bad_in = bad_out = 0
            for y in range(h):
                for x in range(w):
                    o = (y * w + x) * 3
                    inside = ox <= x < ox + ref_w and oy <= y < oy + ref_h
                    if inside:
                        r = ((y - oy) * ref_w + (x - ox)) * 3
                        if rgb[o:o + 3] != ref_rgb[r:r + 3]: bad_in += 1
                    elif rgb[o:o + 3] != b"\0\0\0": bad_out += 1
            if bad_in: fails.append(f"el ultimo fotograma difiere del decodificado en el host en {bad_in} pixeles")
            if bad_out: fails.append(f"{bad_out} pixeles fuera del video no son negros")
        if not wait_for(log, "[vidplay] /video/clip.avi:", 30):
            return fails + ["vidplay no imprimio sus estadisticas"]
        time.sleep(0.5)
        t = text(log)
        m = re.search(r"\[vidplay\] /video/clip\.avi: (\d+) fotogramas, (\d+) mostrados, (\d+) saltados, (\d+) malos, retraso max (\d+) ms, audio (\d+) bytes, huecos (\d+), (\d+) ms", t)
        if not m:
            fails.append("linea de estadisticas de vidplay no reconocida: " + t[t.find("[vidplay] /video"):][:200])
        else:
            frames, shown, dropped, badn, late, abytes, gaps, wall = map(int, m.groups())
            if frames != N or shown != N or dropped or badn: fails.append(f"fotogramas {frames}/{shown} mostrados/{dropped} saltados/{badn} malos (esperado {N}/{N}/0/0)")
            if abytes != len(pcm): fails.append(f"audio alimentado {abytes} bytes, esperado {len(pcm)}")
            if gaps: fails.append(f"{gaps} huecos de audio durante el video")
            if late > 250: fails.append(f"retraso maximo de imagen respecto al audio {late} ms (> 250 ms)")
            if wall < N * 40 + 6000 - 400 or wall > N * 40 + 6000 + 4000: fails.append(f"duracion total {wall} ms fuera de lo esperado ({N * 40 + 6000} ms)")
            print(f"   video: {frames} fotogramas, retraso max {late} ms, {wall} ms")
        m2 = re.search(r"\[vidplay\] /video/roto\.avi: (\d+) fotogramas, (\d+) mostrados, \d+ saltados, (\d+) malos", t)
        if not m2 and not wait_for(log, "[vidplay] /video/roto.avi:", 20):
            fails.append("un AVI cortado no termino limpiamente")
        else:
            m2 = re.search(r"\[vidplay\] /video/roto\.avi: (\d+) fotogramas, (\d+) mostrados, \d+ saltados, (\d+) malos", text(log))
            if m2 and not (0 < int(m2.group(1)) < N and int(m2.group(3)) == 0): fails.append("el AVI cortado debe tocar solo los fotogramas completos que haya: " + m2.group(0))
        if not wait_for(log, "[vidplay] /video/rapido.avi:", 40):
            fails.append("el video rapido no termino")
        else:
            m3 = re.search(r"\[vidplay\] /video/rapido\.avi: (\d+) fotogramas, (\d+) mostrados, (\d+) saltados, (\d+) malos, retraso max (\d+) ms, audio (\d+) bytes, huecos (\d+)", text(log))
            if not m3: fails.append("estadisticas del video rapido no reconocidas")
            else:
                fr, sh, dr, bd, lt, ab, gp = map(int, m3.groups())
                print(f"   video rapido: {fr} fotogramas, {sh} mostrados, {dr} saltados")
                if fr != NF or sh + dr != fr or bd or sh < 1: fails.append("video rapido: la cuenta de fotogramas no cuadra: " + m3.group(0))
                if ab != len(pcm_fast) or gp > 2: fails.append("video rapido: el audio no se alimento completo (o hubo mas de 2 huecos): " + m3.group(0))
        t = text(log)
        if "[vidplay] no se puede abrir el archivo" not in t: fails.append("un archivo inexistente no fue rechazado")
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in t: fails.append(f"no se activo: {expect_feat!r}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except Exception:
                proc.kill(); proc.wait()
        out.close()
    if not fails:
        cap, rate, ch = W.read_wav_pcm(wav)
        if rate != 44100 or ch != 2:
            fails.append(f"la captura no es 44100 Hz estereo ({rate}, {ch})")
        probs = W.check_multi(cap, [pcm, pcm_cut, pcm_fast])
        fails.extend("captura de audio: " + p for p in probs)
    return fails


def _media_consume(cap: bytes, cur: int, pcm: bytes, kind: str, seek_bytes: int = 0):
    """SOMA 023: busca la siguiente reproduccion en la captura a partir de `cur`. kind: 'full' (PCM completo y exacto),
    'pause' (PCM con un silencio en medio y sin perder ni repetir nada), 'seek' (empieza desde el principio, salta a
    seek_bytes y termina con pcm[seek_bytes:] exacto). Devuelve (nuevo cur, problemas)."""
    probe = pcm[:256]
    off = cap.find(probe, cur)
    while off != -1 and off % 4:
        off = cap.find(probe, off + 1)
    if off == -1:
        return cur, ["el inicio de la pista no aparece en la captura"]
    if any(cap[cur:off]):
        return cur, ["hay sonido ajeno antes de la pista"]
    if kind == "full":
        if cap[off:off + len(pcm)] != pcm:
            n = next((i for i in range(len(pcm)) if cap[off + i:off + i + 1] != pcm[i:i + 1]), len(pcm))
            return cur, [f"la pista difiere del PCM en el byte {n} de {len(pcm)}"]
        return off + len(pcm), []
    if kind == "prefix":                       # parado a medias: un trozo inicial exacto y despues solo silencio
        m = 0
        lim = min(len(cap) - off, len(pcm))
        while m < lim and cap[off + m] == pcm[m]:
            m += 1
        m -= m % 4
        if m < 4 * 4410:
            return cur, [f"la pista parada a medias solo tiene {m} bytes correctos"]
        return off + m, []
    seg = cap[off:off + len(pcm) + 4000000]
    m = 0
    lim = min(len(seg), len(pcm))
    while m < lim and seg[m] == pcm[m]:
        m += 1
    m -= m % 4
    if kind == "pause":
        z = m
        while z < len(seg) and seg[z] == 0:
            z += 1
        z -= (z - m) % 4
        if z == m:
            return cur, ["no hay silencio de pausa en la captura"]
        # el silencio puede empezar con ceros del propio PCM: se prueba cada posible corte entre m-64 y m
        for k in range(m, max(m - 256, 0), -4):
            if seg[k:k + 0] == b"" and seg[z:z + len(pcm) - k] == pcm[k:] and not any(seg[k:z]):
                return off + z + len(pcm) - k, []
        return cur, [f"tras la pausa el audio no continua exactamente donde se quedo (prefijo comun {m} bytes, silencio hasta {z})"]
    # seek
    if m == 0:
        return cur, ["el audio no empezo desde el principio"]
    tail = pcm[seek_bytes:]
    pos = seg.find(tail[:256], m)
    while pos != -1 and pos % 4:
        pos = seg.find(tail[:256], pos + 1)
    if pos == -1 or seg[pos:pos + len(tail)] != tail:
        return cur, ["tras el salto el audio no continua exactamente desde la nueva posicion"]
    if any(seg[m:pos]) and m < seek_bytes - 4 * 44100:
        pass
    return off + pos + len(tail), []


def _locate_frame(rgb: bytes, w: int, h: int, ref: bytes, rw: int, rh: int):
    """Busca la imagen de referencia (RGB, rw x rh) dentro de la captura exacta; devuelve (x, y, pixeles distintos) o None."""
    needle = ref[:rw * 3]
    for y in range(h - rh + 1):
        row = rgb[y * w * 3:(y + 1) * w * 3]
        i = row.find(needle)
        while i != -1 and i % 3:
            i = row.find(needle, i + 1)
        if i == -1:
            continue
        x = i // 3
        if x + rw > w:
            continue
        bad = 0
        for r in range(rh):
            a = rgb[((y + r) * w + x) * 3:((y + r) * w + x + rw) * 3]
            if a != ref[r * rw * 3:(r + 1) * rw * 3]:
                bad += sum(1 for q in range(0, rw * 3, 3) if a[q:q + 3] != ref[r * rw * 3 + q:r * rw * 3 + q + 3])
        return x, y, bad
    return None


def scenario_media(root, qemu, td, extra=(), tag="media", expect_feat=None) -> list:
    """SOMA_PARCHE_023_MEDIA: el reproductor del kernel sin GUI (orden `media`): lista, reproduce un AVI y un WAV, pausa y
    continua, salta de posicion, rechaza un archivo inexistente, uno cortado y uno que no es AVI/WAV. La captura de audio
    del HDA emulado se comprueba byte a byte: pista completa, pausa sin perder ni repetir nada, y salto exacto."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import wavcheck as W
    fails = []
    avi = td / "clip.avi"
    N = 40
    subprocess.run([sys.executable, str(root / "tools" / "mkavi.py"), str(avi), "--w", "320", "--h", "240", "--fps", "25", "--frames", str(N)],
                   check=True, stdout=subprocess.DEVNULL)
    pcm = Path(f"{avi}.pcm").read_bytes()
    wavpcm = W.pattern_pcm(66150)
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/video/clip.avi", avi.read_bytes())
    fs.write_file("/video/roto.avi", avi.read_bytes()[:30000])
    fs.write_file("/video/basura.avi", bytes((i * 7 + 3) & 255 for i in range(5000)))
    fs.write_file("/musica/a.wav", W.build_wav(wavpcm, 44100, 2))
    fs.close()
    wav = td / f"captura_{tag}.wav"
    dev = ["-audiodev", f"wav,id=snd0,path={wav},out.frequency=44100,out.channels=2,out.format=s16",
           "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"]
    proc, log, out = boot(root, qemu, td, tag, [*extra, *dev], disk_tag=tag)
    seek_s = 1
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        def send(c):
            proc.stdin.write((c + "\n").encode()); proc.stdin.flush()
        def stats(path, why="fin"):
            m = re.findall(r"\[media\] " + re.escape(path) + r": " + why + r", (\d+) fotogramas, (\d+) mostrados, (\d+) saltados, (\d+) malos, retraso max (\d+) ms, audio (\d+) bytes, huecos (\d+)", text(log))
            return [tuple(map(int, x)) for x in m]
        send("media list")
        if not wait_for(log, "4 archivo(s) de audio/video en BIGFS", 15): fails.append("media list no encuentra los 4 archivos")
        # 1) AVI completo
        send("media play /video/clip.avi")
        t0 = time.time()
        while time.time() - t0 < 30 and not stats("/video/clip.avi"):
            time.sleep(0.3)
        st = stats("/video/clip.avi")
        if not st: return fails + ["el AVI no llego al final: " + text(log)[-300:]]
        fr, sh, dr, bad, late, ab, gaps = st[0]
        print(f"   media (AVI): {fr} fotogramas, {sh} mostrados, {dr} saltados, retraso max {late} ms")
        if (fr, sh, dr, bad) != (N, N, 0, 0): fails.append(f"AVI sin GUI: {fr}/{sh}/{dr}/{bad} (esperado {N}/{N}/0/0)")
        if ab != len(pcm): fails.append(f"AVI: audio alimentado {ab}, esperado {len(pcm)}")
        if gaps: fails.append(f"AVI: {gaps} huecos de audio")
        # 2) WAV
        send("media play /musica/a.wav")
        t0 = time.time()
        while time.time() - t0 < 20 and not stats("/musica/a.wav"):
            time.sleep(0.3)
        st = stats("/musica/a.wav")
        if not st: fails.append("el WAV no llego al final")
        elif st[0][5] != len(wavpcm): fails.append(f"WAV: audio alimentado {st[0][5]}, esperado {len(wavpcm)}")
        # 3) pausa y continuar
        n_before = len(stats("/video/clip.avi"))
        send("media play /video/clip.avi")
        time.sleep(0.9)
        send("media pause")
        time.sleep(1.2)
        send("media status")
        if not wait_for(log, "media: en pausa", 10): fails.append("tras 'media pause' el estado no es 'en pausa'")
        tt = text(log)
        m1 = re.findall(r"pos (\d+) ms de", tt)
        time.sleep(0.8)
        send("media status")
        time.sleep(0.8)
        m2 = re.findall(r"pos (\d+) ms de", text(log))
        if len(m2) >= 2 and m2[-1] != m2[-2]: fails.append(f"en pausa la posicion sigue avanzando ({m2[-2]} -> {m2[-1]} ms)")
        send("media pause")
        t0 = time.time()
        while time.time() - t0 < 30 and len(stats("/video/clip.avi")) < n_before + 1:
            time.sleep(0.3)
        if len(stats("/video/clip.avi")) < n_before + 1: fails.append("tras continuar, la reproduccion no llego al final")
        # 4) salto
        n_before = len(stats("/video/clip.avi"))
        send("media play /video/clip.avi")
        time.sleep(0.6)
        send(f"media seek {seek_s}")
        t0 = time.time()
        while time.time() - t0 < 30 and len(stats("/video/clip.avi")) < n_before + 1:
            time.sleep(0.3)
        if len(stats("/video/clip.avi")) < n_before + 1: fails.append("tras el salto, la reproduccion no llego al final")
        else:
            fr, sh, dr, bad, late, ab, gaps = stats("/video/clip.avi")[-1]
            if bad: fails.append(f"salto: {bad} fotogramas malos")
            if sh + dr < N - 25 - 2: fails.append(f"salto: se mostraron/saltaron solo {sh}+{dr} fotogramas")
        # 5) errores
        send("media play /video/noexiste.avi")
        if not wait_for(log, "[media]", 5): pass
        time.sleep(1.0)
        send("media status")
        if not wait_for(log, "No se encuentra el archivo", 10): fails.append("un archivo inexistente no informa")
        send("media play /video/roto.avi")
        t0 = time.time()
        while time.time() - t0 < 20 and not stats("/video/roto.avi"):
            time.sleep(0.3)
        st = stats("/video/roto.avi")
        if not st: fails.append("el AVI cortado no termino limpiamente")
        elif not (0 < st[0][0] < N and st[0][3] == 0): fails.append(f"AVI cortado: {st[0]}")
        send("media play /video/basura.avi")
        time.sleep(1.0)
        send("media status")
        if not wait_for(log, "Formato no soportado", 10): fails.append("un archivo que no es AVI/WAV no fue rechazado con un mensaje")
        send("media status")
        time.sleep(1.0)
        t = text(log)
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in t: fails.append(f"no se activo: {expect_feat!r}")
        send("ps")
        if not wait_for(log, "TABLA DE HILOS", 10): fails.append("el shell no responde")
        if "mediad" in t.split("TABLA DE HILOS")[-1]: fails.append("queda un hilo mediad vivo")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except Exception:
                proc.kill(); proc.wait()
        out.close()
    if not fails:
        cap, rate, ch = W.read_wav_pcm(wav)
        if rate != 44100 or ch != 2:
            return [f"la captura no es 44100 Hz estereo ({rate}, {ch})"]
        pcm_cut = b""
        cut = avi.read_bytes()[:30000]
        mv = cut.find(b"movi") + 4
        while mv + 8 <= len(cut):
            fcc, sz = cut[mv:mv + 4], int.from_bytes(cut[mv + 4:mv + 8], "little")
            if mv + 8 + sz > len(cut): break
            if fcc[2:] == b"wb": pcm_cut += cut[mv + 8:mv + 8 + sz]
            mv += 8 + sz + (sz & 1)
        cur = 0
        for name, p, kind, sk in (("AVI completo", pcm, "full", 0), ("WAV", wavpcm, "full", 0), ("AVI con pausa", pcm, "pause", 0),
                                  ("AVI con salto", pcm, "seek", seek_s * 44100 * 4), ("AVI cortado", pcm_cut, "full", 0)):
            cur, probs = _media_consume(cap, cur, p, kind, sk)
            fails.extend(f"captura ({name}): {x}" for x in probs)
            if probs: break
        if not fails and any(cap[cur:]):
            fails.append(f"captura: hay sonido ajeno al final ({sum(1 for x in cap[cur:] if x)} bytes no nulos)")
    return fails


def scenario_media_gui(root, qemu, td, extra=(), tag="mediagui", expect_feat=None) -> list:
    """SOMA_PARCHE_023_MEDIA: la ventana del reproductor dentro de la GUI (con el agujero negro animandose a la vez).
    `media gui` en /autoexec.cmd abre la ventana y reproduce; al terminar, el ultimo fotograma en la captura de pantalla de
    QEMU debe ser IDENTICO pixel a pixel al decodificado en el host (se localiza dentro de la ventana), el audio capturado
    byte a byte y la ventana debe seguir viva (el shell responde despues de cerrarla con Esc)."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import fbcheck as C
    import wavcheck as W
    fails = []
    avi = td / "clip.avi"
    N = 40
    subprocess.run([sys.executable, str(root / "tools" / "mkavi.py"), str(avi), "--w", "320", "--h", "240", "--fps", "25", "--frames", str(N)],
                   check=True, stdout=subprocess.DEVNULL)
    pcm = Path(f"{avi}.pcm").read_bytes()
    j2p = td / "jpeg2ppm"
    subprocess.run(["cc", "-O1", "-o", str(j2p), str(root / "tests" / "jpeg2ppm.c"), str(root / "src" / "core" / "jpeg.c")], check=True)
    last = td / "ultimo.ppm"
    subprocess.run([str(j2p), f"{avi}.f{N - 1}.jpg", str(last)], check=True)
    rw, rh, ref = C.read_ppm(last)
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/video/clip.avi", avi.read_bytes())
    fs.write_file("/autoexec.cmd", b"# SOMA: prueba del reproductor en la GUI\nmedia gui /video/clip.avi\n")
    fs.close()
    sock = td / f"mon_{tag}.sock"
    wav = td / f"captura_{tag}.wav"
    dev = ["-audiodev", f"wav,id=snd0,path={wav},out.frequency=44100,out.channels=2,out.format=s16",
           "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0"]
    proc, log, out = boot(root, qemu, td, tag, [*extra, *dev, "-vga", "std", "-monitor", f"unix:{sock},server,nowait"], disk_tag=tag)
    clicks_ok = True
    try:
        if not wait_for(log, "[media] /video/clip.avi: fin", 120):
            return ["el reproductor en la GUI no llego al final: " + text(log)[-400:]]
        time.sleep(1.0)
        screendump(sock, td / f"{tag}_1.ppm")
        w, h, rgb = C.read_ppm(td / f"{tag}_1.ppm")
        hit = _locate_frame(rgb, w, h, ref, rw, rh)
        if hit is None: fails.append("el ultimo fotograma no aparece en la pantalla (ventana sin dibujar?)")
        elif hit[2]: fails.append(f"el ultimo fotograma en pantalla difiere del decodificado en el host en {hit[2]} pixeles")
        t = text(log)
        m = re.search(r"\[media\] /video/clip\.avi: fin, (\d+) fotogramas, (\d+) mostrados, (\d+) saltados, (\d+) malos, retraso max (\d+) ms, audio (\d+) bytes, huecos (\d+)", t)
        if not m: fails.append("sin linea de estadisticas")
        else:
            fr, sh, dr, bad, late, ab, gaps = map(int, m.groups())
            print(f"   media GUI: {fr} fotogramas, {sh} mostrados, {dr} saltados, retraso max {late} ms, huecos {gaps}")
            if fr != N or sh + dr != fr or bad or sh < 1: fails.append(f"GUI: la cuenta de fotogramas no cuadra ({fr}/{sh}/{dr}/{bad})")
            if ab != len(pcm): fails.append(f"GUI: audio alimentado {ab}, esperado {len(pcm)}")
            if gaps: fails.append(f"GUI: {gaps} huecos de audio con la GUI animandose")
        # clics reales (raton PS/2 por el monitor de QEMU): el cursor empieza en el centro de 1920x1080
        import socket
        ms = socket.socket(socket.AF_UNIX); ms.connect(str(sock)); ms.settimeout(3); time.sleep(0.3)
        try: ms.recv(4096)
        except Exception: pass
        cur = [960, 540]
        def hmp(c):
            ms.sendall((c + "\n").encode()); time.sleep(0.15)
        def click_at(x, y):
            dx, dy = x - cur[0], y - cur[1]
            while dx or dy:
                sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy))
                hmp(f"mouse_move {sx} {sy}"); dx -= sx; dy -= sy
            cur[0], cur[1] = x, y
            time.sleep(0.4)
            hmp("mouse_button 1"); hmp("mouse_button 0")
        def finals(): return len(re.findall(r"\[media\] /video/clip\.avi: fin,", text(log)))
        # 1) boton PLAY (el video ya termino: vuelve a reproducirse completo)
        click_at(585, 537)   # SOMA_299_CI: botones del diseno nuevo del reproductor
        t0 = time.time()
        while time.time() - t0 < 40 and finals() < 2: time.sleep(0.3)
        if finals() < 2: fails.append("clic en PLAY: no volvio a reproducir")
        # 2) clic en el archivo de la biblioteca
        click_at(94, 140)
        t0 = time.time()
        while time.time() - t0 < 40 and finals() < 3: time.sleep(0.3)
        if finals() < 3: fails.append("clic en la biblioteca: no reprodujo el archivo")
        # 3) PLAY y, a medias, STOP
        click_at(585, 537)   # SOMA_299_CI: botones del diseno nuevo del reproductor
        click_at(333, 537)
        if not wait_for(log, "clip.avi: parado,", 15): fails.append("clic en STOP: no paro la reproduccion")
        time.sleep(1.0)
        ms.close()
        t = text(log)
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in t: fails.append(f"no se activo: {expect_feat!r}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(10)
            except Exception:
                proc.kill(); proc.wait()
        out.close()
    if not fails:
        cap, rate, ch = W.read_wav_pcm(wav)
        cur = 0
        for name, kind in (("reproduccion inicial", "full"), ("clic en PLAY", "full"), ("clic en la biblioteca", "full"), ("PLAY + STOP", "prefix")):
            cur, probs = _media_consume(cap, cur, pcm, kind)
            fails.extend(f"captura de audio ({name}): {x}" for x in probs)
            if probs: break
        if not fails and any(cap[cur:]):
            fails.append(f"captura de audio: sonido ajeno al final ({sum(1 for x in cap[cur:] if x)} bytes no nulos)")
    return fails


def scenario_terminal(root, qemu, td, extra=(), tag="terminal", expect_feat=None) -> list:
    """SOMA_PARCHE_024_TERM: el terminal de la GUI con el raton y el teclado reales (PS/2 por el monitor de QEMU).
    `terminal debug` en /autoexec.cmd abre la ventana y activa trazas por serie (geometria, estado, portapapeles). Se comprueba:
    doble clic = palabra, triple clic = linea, arrastre entre lineas, Copiar/Pegar (botones y Ctrl+C/Ctrl+V: lo pegado se
    ejecuta y su salida se vuelve a copiar), menu contextual (Seleccionar todo, Esc lo cierra sin salir de la GUI), paleta de
    simbolos, zoom (botones y Ctrl +/-), RePag/AvPag, rueda, flechas, pista y pulgar de la barra de desplazamiento."""
    sys.path.insert(0, str(root / "tools"))
    import bigfs as B
    import fbcheck as C
    import socket
    fails = []
    disk = td / f"hdd_{tag}.img"
    with open(disk, "wb") as fh:
        fh.truncate(64 * 1024 * 1024)
    fs = B.BigFS(disk, 16384)
    fs.format()
    fs.write_file("/autoexec.cmd", b"# SOMA: prueba del terminal de la GUI\nterminal debug\n")
    fs.close()
    sock = td / f"mon_{tag}.sock"
    proc, log, out = boot(root, qemu, td, tag, [*extra, "-vga", "std", "-monitor", f"unix:{sock},server,nowait"], disk_tag=tag)
    mon = [None]
    def hmp(c, d=0.12):
        if mon[0] is None:
            mon[0] = socket.socket(socket.AF_UNIX); mon[0].connect(str(sock)); mon[0].settimeout(3); time.sleep(0.3)
            try: mon[0].recv(4096)
            except Exception: pass
        mon[0].sendall((c + "\n").encode()); time.sleep(d)
    def close_mon():
        if mon[0] is not None:
            mon[0].close(); mon[0] = None; time.sleep(0.3)
    KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", "/": "slash", ",": "comma"}
    def typ(t):
        for ch in t:
            hmp("sendkey " + KEYS.get(ch, ch), 0.08)
    cur = [960, 540]
    def move_to(x, y):
        dx, dy = x - cur[0], y - cur[1]
        while dx or dy:
            sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy)); hmp(f"mouse_move {sx} {sy}", 0.1); dx -= sx; dy -= sy
        cur[0], cur[1] = x, y
    def click(x, y):
        move_to(x, y); time.sleep(0.2); hmp("mouse_button 1"); hmp("mouse_button 0")
    def dclick(x, y):
        move_to(x, y); time.sleep(0.2)
        hmp("mouse_button 1", 0.08); hmp("mouse_button 0", 0.08); hmp("mouse_button 1", 0.08); hmp("mouse_button 0", 0.2)
    def tclick(x, y):
        move_to(x, y); time.sleep(0.2)
        for _ in range(3):
            hmp("mouse_button 1", 0.08); hmp("mouse_button 0", 0.08)
        time.sleep(0.2)
    def rclick(x, y):
        move_to(x, y); time.sleep(0.2); hmp("mouse_button 2"); hmp("mouse_button 0")
    def drag(x0, y0, x1, y1):
        move_to(x0, y0); time.sleep(0.2); hmp("mouse_button 1", 0.15)
        steps = 6
        for i in range(1, steps + 1):
            move_to(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
        time.sleep(0.2); hmp("mouse_button 0", 0.2)
    def states():
        return re.findall(r"\[term\] estado scroll (\d+) total (\d+) vista (\d+) zoom (\d+) (vector|bitmap) (con|sin)-seleccion ?(menu|simbolos)?", text(log))
    def last_state():
        st = states()
        return st[-1] if st else None
    def wait_state(pred, timeout=10):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = last_state()
            if st and pred(st): return st
            time.sleep(0.2)
        return last_state()
    def copied():
        return re.findall(r"\[term\] copiado (\d+) caracteres: (.*)", text(log))
    def wait_copied(n_before, timeout=8):
        t0 = time.time()
        while time.time() - t0 < timeout:
            c = copied()
            if len(c) > n_before: return c[-1]
            time.sleep(0.2)
        return None
    try:
        if not wait_for(log, "[term] geom ventana", 90):
            return ["el terminal no llego a dibujarse: " + text(log)[-300:]]
        time.sleep(1.5)
        gm = re.findall(r"\[term\] geom ventana (\S+) texto (\S+) celda (\d+)x(\d+) cols (\d+) filas (\d+) barra (\S+) copiar (\S+) pegar (\S+) menos (\S+) mas (\S+) menu (\S+)", text(log))[-1]
        wx, wy, ww, wh = map(int, gm[0].split(","))
        tx, ty, tw, th = map(int, gm[1].split(","))
        cw, ch, cols, rows = int(gm[2]), int(gm[3]), int(gm[4]), int(gm[5])
        sbx, sby, sbh, sth = map(int, gm[6].split(","))
        btn = {k: list(map(int, v.split(","))) for k, v in zip(("copiar", "pegar", "menos", "mas", "menu"), gm[7:12])}
        def cx(col): return tx + col * cw + cw // 2
        def cy(row): return ty + row * ch + ch // 2
        def bc(k): b = btn[k]; return b[0] + b[2] // 2, b[1] + b[3] // 2
        st0 = last_state()
        if not st0: return ["sin estado inicial del terminal"]
        base_total = int(st0[1])
        if int(st0[2]) != rows: fails.append(f"filas visibles {st0[2]} != {rows}")
        # 1) echo + doble clic en una palabra + Copiar
        typ("echo uno dos tres\n")
        st = wait_state(lambda s: int(s[1]) >= base_total + 2)
        if not st or int(st[1]) != base_total + 2: return fails + [f"el comando no anadio las 2 lineas esperadas ({st})"]
        banner = base_total - 1                         # lineas anteriores a la orden (el prompt vivo es la ultima)
        r_cmd, r_out = banner, banner + 1
        n0 = len(copied())
        dclick(cx(5), cy(r_out))
        st = wait_state(lambda s: s[5] == "con")
        if not st or st[5] != "con": fails.append("doble clic: no hay seleccion")
        click(*bc("copiar"))
        c = wait_copied(n0)
        if not c or c[1] != "dos" or c[0] != "3": fails.append(f"doble clic + Copiar: se esperaba 'dos' y fue {c}")
        # 2) pegar (boton y Ctrl+V) y ejecutar: la salida debe ser 'dosdos'
        typ("echo ")
        click(*bc("pegar")); time.sleep(0.4)
        hmp("sendkey ctrl-v"); time.sleep(0.4)
        if len(re.findall(r"\[term\] pegado 3 caracteres", text(log))) < 2: fails.append("Pegar (boton y Ctrl+V) no pego 2 veces 3 caracteres")
        typ("\n")
        st = wait_state(lambda s: int(s[1]) >= base_total + 4)
        n0 = len(copied())
        tclick(cx(2), cy(r_out + 2))                    # la salida de lo pegado
        c = wait_copied(n0)
        click(*bc("copiar"))
        c = wait_copied(n0)
        if not c or c[1] != "dosdos": fails.append(f"triple clic + Copiar tras pegar: se esperaba 'dosdos' y fue {c}")
        # 3) arrastre entre lineas (la orden 'echo uno dos tres' y su salida): 30 caracteres con salto de linea
        n0 = len(copied())
        drag(cx(6), cy(r_cmd), cx(11), cy(r_out))
        hmp("sendkey ctrl-c"); time.sleep(0.3)
        c = wait_copied(n0)
        if not c or c[1] != "echo uno dos tres|uno dos tres" or c[0] != "30": fails.append(f"arrastre entre lineas + Ctrl+C: {c}")
        # 4) menu contextual
        mx0, my0 = 300, 700
        rclick(mx0, my0)
        st = wait_state(lambda s: s[6] == "menu")
        if not st or st[6] != "menu": fails.append("clic derecho: no se abrio el menu")
        def mitem(i):
            y = my0 + 6
            for k in range(i): y += 9 if k in (4, 8) else 24
            return mx0 + 100, y + 12
        click(*mitem(2))                                 # Seleccionar todo
        st = wait_state(lambda s: s[6] != "menu" and s[5] == "con")
        if not st or st[6] == "menu" or st[5] != "con": fails.append(f"menu > Seleccionar todo: {st}")
        n0 = len(copied())
        click(*bc("copiar"))
        c = wait_copied(n0)
        if not c or "echo dosdos|dosdos|soma>" not in c[1] or "SOMA v0.2" not in c[1] or int(c[0]) < 300: fails.append(f"Seleccionar todo + Copiar: {c}")
        # 5) Esc cierra el menu sin salir de la GUI
        rclick(mx0, my0)
        wait_state(lambda s: s[6] == "menu")
        hmp("sendkey esc"); time.sleep(0.6)
        st = last_state()
        if st and st[6] == "menu": fails.append("Esc no cerro el menu")
        # 6) paleta de simbolos: se insertan < y |
        rclick(mx0, my0)
        wait_state(lambda s: s[6] == "menu")
        click(*mitem(9))
        st = wait_state(lambda s: s[6] == "simbolos")
        if not st or st[6] != "simbolos": fails.append("menu > Simbolos: no se abrio la paleta")
        sp_w, sp_h = 16 + 8 * 30, 36 + 4 * 30
        sp_x, sp_y = wx + ww - sp_w - 24, wy + 34 + 8
        click(sp_x + 8 + 14, sp_y + 28 + 14)                       # '<'
        click(sp_x + 8 + 2 * 30 + 14, sp_y + 28 + 14)              # '|'
        hmp("sendkey esc"); time.sleep(0.6)
        st = last_state()
        if st and st[6] == "simbolos": fails.append("Esc no cerro la paleta")
        n0 = len(copied())
        tclick(cx(2), cy(r_out + 3))                              # la linea viva: "soma> <|"
        click(*bc("copiar"))
        c = wait_copied(n0)
        if not c or c[1] != "soma> <|": fails.append(f"paleta de simbolos: la linea viva deberia ser 'soma> <|' y fue {c}")
        # 7) zoom
        z0 = int(last_state()[3])
        click(*bc("mas")); st = wait_state(lambda s: int(s[3]) == z0 + 1)
        if int(st[3]) != z0 + 1: fails.append("A+ no aumento el zoom")
        hmp("sendkey ctrl-equal"); st = wait_state(lambda s: int(s[3]) == z0 + 2)
        if int(st[3]) != z0 + 2: fails.append("Ctrl + no aumento el zoom")
        gm2 = re.findall(r"\[term\] geom ventana \S+ texto \S+ celda (\d+)x(\d+)", text(log))[-1]
        if int(gm2[0]) <= cw: fails.append(f"el zoom no cambio el tamano de celda ({cw} -> {gm2[0]})")
        hmp("sendkey ctrl-minus"); wait_state(lambda s: int(s[3]) == z0 + 1)
        click(*bc("menos")); st = wait_state(lambda s: int(s[3]) == z0)
        if int(st[3]) != z0: fails.append("A- / Ctrl - no devolvieron el zoom")
        # 8) desplazamiento: mucha salida y barra
        for _ in range(8): hmp("sendkey backspace", 0.06)          # vacia la linea (quedaba "<|")
        typ("help\n")
        st = wait_state(lambda s: int(s[1]) > int(s[2]) + 10)
        if not st or int(st[1]) <= int(st[2]): return fails + ["help no lleno la ventana"]
        tot, view = int(st[1]), int(st[2])
        if int(st[0]) != 0: fails.append("tras ejecutar una orden la vista debe estar al final")
        hmp("sendkey pgup"); st = wait_state(lambda s: int(s[0]) > 0)
        if int(st[0]) != view - 1: fails.append(f"RePag desplazo {st[0]} filas (esperado {view - 1})")
        hmp("sendkey pgdn"); st = wait_state(lambda s: int(s[0]) == 0)
        if int(st[0]) != 0: fails.append("AvPag no volvio al final")
        move_to(wx + 300, wy + 200); time.sleep(0.3)
        hmp("mouse_move 0 0 1"); st = wait_state(lambda s: int(s[0]) == 3)         # rueda hacia arriba
        if int(st[0]) != 3: fails.append(f"rueda hacia arriba: scroll {st[0]} (esperado 3)")
        hmp("mouse_move 0 0 -1"); st = wait_state(lambda s: int(s[0]) == 0)
        if int(st[0]) != 0: fails.append("rueda hacia abajo no volvio al final")
        click(sbx + 7, ty + 8); st = wait_state(lambda s: int(s[0]) == 3)         # flecha arriba
        if int(st[0]) != 3: fails.append(f"flecha arriba: scroll {st[0]}")
        click(sbx + 7, ty + sth - 8); st = wait_state(lambda s: int(s[0]) == 0)   # flecha abajo
        if int(st[0]) != 0: fails.append(f"flecha abajo: scroll {st[0]}")
        click(sbx + 7, sby + 20); st = wait_state(lambda s: int(s[0]) > 0)       # pista por encima del pulgar: pagina arriba
        if int(st[0]) != view - 1: fails.append(f"clic en la pista: scroll {st[0]} (esperado {view - 1})")
        move_to(sbx + 7, sby + sbh - 20); time.sleep(0.2)
        smax = tot - view
        # el pulgar esta ahora cerca del final de la pista: se agarra por su centro y se arrastra al principio
        thumb_h = max(24, sbh * view // tot); span = sbh - thumb_h
        thumb_y = span - span * st[0] // smax if False else None
        ty_now = span * (smax - int(st[0])) // smax
        move_to(sbx + 7, sby + ty_now + thumb_h // 2); time.sleep(0.2); hmp("mouse_button 1", 0.15)
        move_to(sbx + 7, sby + thumb_h // 2 - 2); hmp("mouse_button 0", 0.3)
        st = wait_state(lambda s: int(s[0]) == smax)
        if int(st[0]) != smax: fails.append(f"arrastrar el pulgar al principio: scroll {st[0]} (esperado {smax})")
        # 9) el sistema sigue vivo: se puede teclear y ejecutar
        hmp("sendkey pgdn")
        n_before = int(last_state()[1])
        typ("echo fin\n")
        st = wait_state(lambda s: int(s[1]) >= n_before + 2)
        if not st or int(st[1]) < n_before + 2: fails.append("tras todo, el terminal ya no ejecuta ordenes")
        close_mon()
        F = sys.modules[__name__]
        screendump(sock, td / f"{tag}_1.ppm")
        w, h, rgb = C.read_ppm(td / f"{tag}_1.ppm")
        if (w, h) != (1920, 1080): fails.append(f"pantalla {w}x{h}")
        t = text(log)
        if "KERNEL PANIC" in t: fails.append("el kernel entro en panic")
        if expect_feat and expect_feat not in t: fails.append(f"no se activo: {expect_feat!r}")
    finally:
        close_mon()
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def scenario_deadlock(root, qemu, td) -> list:
    fails = []
    proc, log, out = boot(root, qemu, td, "deadlock")
    try:
        if not wait_for(log, "Modo texto activado", 60):
            return ["no llego a la consola de texto"]
        proc.stdin.write(b"deadlock\n"); proc.stdin.flush()
        if not wait_for(log, "INTERBLOQUEO", 15):
            return ["no se detecto el interbloqueo en 15 s"]
        time.sleep(1.5)
        t = text(log)
        for needle in ("'dl_t2' espera 'dl_A' (de 'dl_t1')", "'dl_t1' espera 'dl_B' (de 'dl_t2')"):
            if needle not in t:
                fails.append(f"el aviso no contiene {needle}")
        s = symbolized(root, log)
        if "dl_thread" not in s:
            fails.append("el backtrace del aviso no resuelve a dl_thread1/dl_thread2")
        proc.stdin.write(b"mutex\n"); proc.stdin.flush()
        if not wait_for(log, "Interbloqueos detectados", 10):
            fails.append("'mutex' no responde")
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        if not wait_for(log, "TABLA DE HILOS", 10):
            fails.append("'ps' no responde con los hilos bloqueados")
        tids = re.findall(r"^\s*(\d+)\s+dl_t[12]\b", text(log), re.M)
        if len(tids) != 2:
            fails.append(f"no se ven los 2 hilos dl_t1/dl_t2 en 'ps' ({tids})")
        if tids:
            # Matar a UN participante libera su mutex; el otro lo obtiene, termina y se recoge.
            proc.stdin.write(f"kill {min(int(x) for x in tids)}\n".encode()); proc.stdin.flush()
            if not wait_for(log, "[MUTEX RECOVERY]", 10):
                fails.append("matar un hilo no libero su mutex (falta [MUTEX RECOVERY])")
            time.sleep(1.5)
        proc.stdin.write(b"mutex\n"); proc.stdin.flush()
        time.sleep(1.5)
        last = text(log).rsplit("MUTEX DEL KERNEL", 1)[-1]
        # Solo los mutex de la prueba: net_lock puede estar ocupado legitimamente por la prueba LLM de fondo.
        estados = dict(re.findall(r"^ (dl_[AB])\s+(\w+)", last, re.M))
        if set(estados) != {"dl_A", "dl_B"}:
            fails.append(f"'mutex' no muestra dl_A y dl_B ({estados})")
        elif any(v != "libre" for v in estados.values()):
            fails.append(f"tras la recuperacion quedan ocupados: {estados}")
        mark = len(text(log))
        proc.stdin.write(b"ps\n"); proc.stdin.flush()
        time.sleep(1.5)
        if re.search(r"dl_t[12]", text(log)[mark:]):
            fails.append("tras la recuperacion siguen vivos hilos dl_t1/dl_t2")
    finally:
        if proc.poll() is None:
            proc.kill(); proc.wait()
        out.close()
    return fails


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path.cwd())
    a = ap.parse_args()
    root = a.root.resolve()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu or not (root / "build" / "soma.iso").is_file() or not (root / "build" / "x86_64" / "soma.elf").is_file():
        print("SKIP: falta qemu-system-x86_64 o build/soma.iso y build/x86_64/soma.elf (compila con 'make')")
        return 2
    bad = 0
    with tempfile.TemporaryDirectory(prefix="soma_fault_") as td:
        for name, fn in (("crash", scenario_crash), ("hang/watchdog", scenario_hang),
                         ("interbloqueo", scenario_deadlock), ("ring3", scenario_ring3), ("exec-elf", scenario_exec), ("bigfs", scenario_bigfs), ("big-exec", scenario_bigexec), ("audio", scenario_audio), ("fpu", scenario_fpu), ("pantalla", scenario_fb), ("video", scenario_video), ("media", scenario_media), ("media GUI", scenario_media_gui), ("terminal GUI", scenario_terminal),
                         ("audio sin dispositivo", lambda r, q, t: scenario_audio(r, q, t, (), "audionodev", False)),
                         ("ring3+SMEP/SMAP", lambda r, q, t: scenario_ring3(r, q, t, ("-cpu", "qemu64,+smep,+smap"), "ring3smap", "SMEP activo, SMAP activo")),
                         ("big-exec+SMEP/SMAP", lambda r, q, t: scenario_bigexec(r, q, t, ("-cpu", "qemu64,+smep,+smap"), "bigexecsmap", "SMEP activo, SMAP activo")),
                         ("audio+SMEP/SMAP", lambda r, q, t: scenario_audio(r, q, t, ("-cpu", "qemu64,+smep,+smap"), "audiosmap", True, "SMEP activo, SMAP activo")),
                         ("pantalla+SMEP/SMAP", lambda r, q, t: scenario_fb(r, q, t, ("-cpu", "qemu64,+smep,+smap"), "fbsmap", "SMEP activo, SMAP activo")),
                         ("video+SMEP/SMAP", lambda r, q, t: scenario_video(r, q, t, ("-cpu", "qemu64,+smep,+smap"), "videosmap", "SMEP activo, SMAP activo"))):
            try:
                fails = fn(root, qemu, Path(td))
                # Reintento unico SOLO si falla unicamente la captura de audio. El QEMU 8.2 que sirve el HDA emulado pierde a veces
                # (~1 de cada 10 ejecuciones, mas con la maquina cargada) exactamente 8192 bytes en mitad de la salida, o el final
                # de la pista, sin que el kernel vea nada raro (0 huecos, todo alimentado, contador del DMA normal).
                if fails and all(f.startswith("captura") for f in fails):
                    print(f"   (reintento: {fails[0]})")
                    fails = fn(root, qemu, Path(td))
            except (BrokenPipeError, OSError) as exc:          # QEMU murio (p. ej. panic con isa-debug-exit)
                fails = [f"QEMU termino de forma inesperada ({type(exc).__name__}): probable panic del kernel"]
            if fails:
                bad += 1
                print(f"FAULT {name}: FALLO")
                for f in fails:
                    print(f"   - {f}")
            else:
                print(f"FAULT {name}: OK")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
