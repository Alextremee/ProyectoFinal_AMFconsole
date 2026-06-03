#!/bin/sh
TARGET_DIR="$1"

rm -f "${TARGET_DIR}/etc/init.d/S99frontend"
rm -f "${TARGET_DIR}/etc/init.d/S90splash"

# Inittab modo DEMO 100% silencioso
cat > "${TARGET_DIR}/etc/inittab" << 'INITEOF'
::sysinit:/etc/init.d/rcS
::shutdown:/etc/init.d/rcK
::ctrlaltdel:/sbin/reboot

# Bridge silencioso en tty4
tty4::respawn:/usr/local/bin/bridge-launcher.sh

# Frontend silencioso en tty1
tty1::respawn:/usr/local/bin/frontend-launcher.sh

# Solo serial para rescate
ttyAMA0::respawn:/sbin/getty -L ttyAMA0 115200 vt100
INITEOF

# Grupo input
if ! grep -q "^input:" "${TARGET_DIR}/etc/group" 2>/dev/null; then
    echo "input:x:1001:" >> "${TARGET_DIR}/etc/group"
fi

# Copiar cfg Mednafen
mkdir -p "${TARGET_DIR}/.mednafen" "${TARGET_DIR}/root/.mednafen"
cp "${TARGET_DIR}/home/pi/.mednafen/mednafen.cfg" \
   "${TARGET_DIR}/.mednafen/mednafen.cfg" 2>/dev/null
cp "${TARGET_DIR}/home/pi/.mednafen/mednafen.cfg" \
   "${TARGET_DIR}/root/.mednafen/mednafen.cfg" 2>/dev/null

# Permisos
chmod +x "${TARGET_DIR}/usr/local/bin/"*.sh 2>/dev/null
chmod +x "${TARGET_DIR}/etc/init.d/S05splash" 2>/dev/null
chmod +x "${TARGET_DIR}/etc/init.d/S95usbimporter" 2>/dev/null

mkdir -p "${TARGET_DIR}/home/pi/roms"
mkdir -p "${TARGET_DIR}/var/log"
mkdir -p "${TARGET_DIR}/mnt/usb"

exit 0
