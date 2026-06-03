# Consola de Videojuegos Retro

Consola retro con emulacion de NES, SNES, Game Boy y Game Boy Advance sobre Raspberry Pi 4 (AArch64), construida con Buildroot 2025.02.

Proyecto final de Fundamentos de Sistemas Embebidos - Facultad de Ingenieria UNAM.

## Equipo

- Alejandro Martinez Jimenez (319130865) - Mednafen y bridge DualSense
- Francisco Silva Castro (319266399) - Frontend, splash, USB importer
- Esteban Arellanes Conde (319322743) - Sistema Linux base con Buildroot

## Caracteristicas

- Boot directo a galeria sin escritorio.
- Splash personalizado con imagen y audio.
- Control completo con DualSense via USB.
- Emulacion Mednafen 1.32.1 (NES, SNES, GBA, Game Boy).
- Bridge userspace que traduce DualSense a teclado virtual via /dev/uinput.
- Importacion automatica de ROMs desde USB con etiqueta NUEVO.
- Compatible HDMI y adaptador HDMI a VGA a 1366x768.

## Compilacion

    git clone https://github.com/Alextremee/ProyectoFinal_AMFconsole.git
    cd ProyectoFinal_AMFconsole
    chmod +x build.sh
    ./build.sh

El script descarga Buildroot 2025.02, integra los archivos de src/ y compila la imagen. Tarda entre 90 minutos y 4 horas.

## Grabacion en microSD

    sudo dd if=buildroot-2025.02/output/images/sdcard.img of=/dev/mmcblk0 bs=4M status=progress conv=fsync oflag=sync
    sync

## Estructura

- src/frontend/  gallery.cpp y gamepad-bridge.c
- src/splash/    splash.c
- src/daemon/    usb_importer.py
- src/scripts/   wrappers e init scripts
- src/config/    inittab, mednafen.cfg, udev rules
- src/packages/  archivos .mk de Buildroot
- doc/           reportes PDF
- vid/           enlace al video

## Licencia

MIT - ver LICENSE
