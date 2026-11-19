#!/usr/bin/env python3
"""Arranque en QEMU con VARIOS modelos de CPU (x86_64): sin panico, y el servidor HTTP responde 200.
Motivo (parche 136): un `rdrand` sin comprobar CPUID paso todas las pruebas de host y solo fallaba en CPUs sin esa instruccion (qemu64).
Uso: python3 tools/cpu_matrix.py [--cpus qemu64,Nehalem,max] [--iso build/soma.iso] [--wait 30]"""
import argparse, os, shutil, socket, subprocess, sys, tempfile, time, urllib.request


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


def run_cpu(qemu, iso, cpu, wait):
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="soma_cpu_") as td:
        ser = os.path.join(td, "serial.txt")
        cmd = [qemu, "-cpu", cpu, "-m", "512", "-cdrom", iso, "-display", "none", "-no-reboot", "-serial", "file:" + ser,
               "-netdev", f"user,id=n0,hostfwd=tcp:127.0.0.1:{port}-:80", "-device", "virtio-net-pci,netdev=n0"]
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            t0 = time.time(); text = ""
            while time.time() - t0 < wait:
                time.sleep(1)
                text = open(ser, errors="replace").read() if os.path.exists(ser) else ""
                if "KERNEL PANIC" in text or p.poll() is not None: break
                if "SSP:" in text and time.time() - t0 > 8: break
            if "KERNEL PANIC" in text:
                line = next((l for l in text.splitlines() if "Excepcion:" in l), "panico")
                return False, "PANICO: " + line.strip()
            if "SSP:" not in text: return False, "no llego a kernel_main (sin linea SSP:)"
            codes = []
            for _ in range(3):
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=15) as r: codes.append(r.status)
                except Exception as e: codes.append(type(e).__name__)
            text = open(ser, errors="replace").read()
            if "KERNEL PANIC" in text: return False, "PANICO durante las peticiones HTTP"
            if codes != [200, 200, 200]: return False, f"HTTP: {codes}"
            return True, "arranca y sirve HTTP (3 x 200)"
        finally:
            p.kill(); p.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpus", default="qemu64,Nehalem,max"); ap.add_argument("--iso", default="build/soma.iso"); ap.add_argument("--wait", type=int, default=40)
    a = ap.parse_args()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu: print("falta qemu-system-x86_64"); return 2
    if not os.path.exists(a.iso): print("falta " + a.iso + " (compila antes con make)"); return 2
    bad = 0
    for cpu in a.cpus.split(","):
        ok, msg = run_cpu(qemu, a.iso, cpu, a.wait)
        print(f"  [{'OK ' if ok else 'FALLO'}] -cpu {cpu}: {msg}"); bad += not ok
    print("[cpu-matrix] " + ("todo bien" if not bad else f"{bad} CPU(s) con fallo"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
