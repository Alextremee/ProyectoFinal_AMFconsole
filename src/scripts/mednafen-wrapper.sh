#!/bin/sh
# Wrapper Mednafen silencioso

for i in 1 2 3 4 5; do
    [ -f /tmp/gamepad-bridge-ready ] && break
    sleep 1
done

export SDL_INPUT_LINUXEV=1
export SDL_JOYSTICK_DISABLE_LINUX_HOTPLUG=0
export SDL_VIDEODRIVER=kmsdrm
export SDL_KMSDRM_REQUIRE_DRM_MASTER=0
export HOME=/home/pi
export MEDNAFEN_ALLOWMULTI=1

exec /usr/bin/mednafen "$@" > /dev/null 2>&1
