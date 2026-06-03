#!/usr/bin/env python3
# RetroConsole - Daemon USB v2: mata Mednafen + splash + sobreescribe
import os, sys, time, shutil, subprocess, signal

CONF_PATH = "/etc/retroconsole/retroconsole.conf"

def load_conf(path):
    conf = {}
    try:
        with open(path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line: continue
                k, v = line.split("=", 1)
                conf[k.strip()] = v.strip()
    except FileNotFoundError: pass
    return conf

CONF = load_conf(CONF_PATH)
ROMS_DIR        = CONF.get("ROMS_DIR", "/home/pi/roms")
USB_MOUNT       = CONF.get("USB_MOUNT", "/mnt/usb")
USB_ROMS_SUBDIR = CONF.get("USB_ROMS_SUBDIR", "")  # vacio = buscar en raiz
PAUSE_FLAG      = "/tmp/retro_pause.flag"
REFRESH_FLAG    = "/tmp/retro_refresh.flag"
SPLASH_BIN      = "/usr/bin/retroconsole-splash"
SPLASH_PNG      = "/etc/retroconsole/assets/splash_copiando.png"

VALID_EXT = {".nes", ".fds", ".unf", ".smc", ".sfc", ".swc",
             ".fig", ".gba", ".agb", ".gb", ".gbc", ".zip"}
POLL_SECONDS = 2.0

def log(msg):
    print("[usb-importer] %s" % msg, flush=True)

def kill_emulator_and_frontend():
    """Mata Mednafen Y frontend para liberar el framebuffer"""
    subprocess.call(["killall", "-9", "mednafen"], stderr=subprocess.DEVNULL)
    subprocess.call(["killall", "-9", "retroconsole-frontend"], stderr=subprocess.DEVNULL)
    time.sleep(1)

def show_splash():
    """Muestra splash 'COPIANDO ROMs' en pantalla. Devuelve PID."""
    if not os.path.exists(SPLASH_BIN) or not os.path.exists(SPLASH_PNG):
        log("splash no disponible")
        return None
    try:
        # Splash con duracion larga (999s) - lo matamos cuando terminamos
        p = subprocess.Popen([SPLASH_BIN, SPLASH_PNG, "999"],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        return p
    except Exception as e:
        log("error splash: %s" % e)
        return None

def kill_splash(p):
    if p is None: return
    try:
        p.terminate()
        time.sleep(0.3)
        if p.poll() is None: p.kill()
    except Exception: pass

def list_block_partitions():
    parts = []
    try:
        with open("/proc/partitions", "r") as f:
            for line in f.readlines()[2:]:
                cols = line.split()
                if len(cols) < 4: continue
                name = cols[3]
                if name.startswith("sd") and name[-1].isdigit():
                    parts.append(name)
    except OSError: pass
    return set(parts)

def mount_device(devname):
    dev = "/dev/" + devname
    os.makedirs(USB_MOUNT, exist_ok=True)
    try:
        subprocess.check_call(["mount", "-o", "ro", dev, USB_MOUNT],
                              stderr=subprocess.DEVNULL)
        log("montado %s en %s" % (dev, USB_MOUNT))
        return True
    except subprocess.CalledProcessError:
        for fstype in ("vfat", "exfat", "ext4", "ext3", "ntfs"):
            try:
                subprocess.check_call(["mount", "-t", fstype, "-o", "ro", dev, USB_MOUNT],
                                      stderr=subprocess.DEVNULL)
                log("montado %s (%s)" % (dev, fstype))
                return True
            except subprocess.CalledProcessError: continue
    log("no se pudo montar %s" % dev)
    return False

def umount_device():
    try:
        subprocess.call(["sync"])
        subprocess.check_call(["umount", USB_MOUNT], stderr=subprocess.DEVNULL)
        log("desmontado")
    except subprocess.CalledProcessError:
        subprocess.call(["umount", "-l", USB_MOUNT])

def find_source_roms():
    """Busca ROMs en USB (en raiz Y en subcarpeta 'roms' si existe)"""
    found = []
    # Buscar en raiz y subcarpetas
    for root, _d, files in os.walk(USB_MOUNT):
        for fn in files:
            if fn.startswith("."): continue
            ext = os.path.splitext(fn)[1].lower()
            if ext in VALID_EXT:
                found.append(os.path.join(root, fn))
    return found

def import_roms_overwrite():
    """Copia todas las ROMs encontradas, SOBREESCRIBE si existe"""
    os.makedirs(ROMS_DIR, exist_ok=True)
    sources = find_source_roms()
    log("ROMs encontradas en USB: %d" % len(sources))
    copied = 0
    for src in sources:
        fn = os.path.basename(src)
        dest = os.path.join(ROMS_DIR, fn)
        try:
            shutil.copy2(src, dest)
            # Crear marcador .new para que el frontend lo etiquete
            try:
                with open(dest + ".new", "w") as f:
                    f.write("new\n")
            except Exception:
                pass
            copied += 1
            log("copiada (sobrescrita): %s" % fn)
        except Exception as e:
            log("error copiando %s: %s" % (fn, e))
    log("importacion terminada: %d archivos" % copied)
    return copied

def set_flag(path):
    try:
        with open(path, "w") as f: f.write(str(time.time()))
    except Exception: pass

def clear_flag(path):
    try:
        if os.path.exists(path): os.remove(path)
    except OSError: pass

def handle_new_device(devname):
    log("USB DETECTADA: %s" % devname)

    # 1. Marcar pausa (frontend lo lee)
    set_flag(PAUSE_FLAG)

    # 2. Matar Mednafen y frontend para liberar pantalla
    log("Matando emulador y frontend...")
    kill_emulator_and_frontend()

    # 3. Mostrar splash de copiando
    log("Mostrando splash...")
    splash_proc = show_splash()
    time.sleep(0.5)  # dar tiempo al splash a aparecer

    # 4. Montar y copiar
    copied = 0
    try:
        if mount_device(devname):
            try:
                copied = import_roms_overwrite()
            finally:
                umount_device()
        else:
            log("ERROR: no se pudo montar USB")
    finally:
        # 5. Quitar splash
        log("Quitando splash...")
        time.sleep(1)  # dejar splash visible 1 seg mas
        kill_splash(splash_proc)

        # 6. Limpiar flags
        clear_flag(PAUSE_FLAG)
        set_flag(REFRESH_FLAG)

        # 7. Frontend se relanzara automaticamente por inittab respawn
        log("COMPLETADO. %d ROMs importadas. Frontend se relanzara." % copied)

def main():
    log("daemon v2 iniciado")
    clear_flag(PAUSE_FLAG)
    baseline = list_block_partitions()
    log("particiones base: %s" % (sorted(baseline) or "ninguna"))
    while True:
        try:
            current = list_block_partitions()
            new_parts = current - baseline
            for dev in sorted(new_parts):
                for _ in range(5):
                    if os.path.exists("/dev/" + dev): break
                    time.sleep(0.4)
                handle_new_device(dev)
            # Detectar tambien USBs removidas para actualizar baseline
            removed = baseline - current
            if removed:
                for r in removed:
                    log("USB removida: %s" % r)
            baseline = current
        except Exception as e:
            log("error: %s" % e)
        time.sleep(POLL_SECONDS)

if __name__ == "__main__":
    main()
