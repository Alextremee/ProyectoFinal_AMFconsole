################################################################################
#
# mednafen
#
################################################################################

MEDNAFEN_VERSION = 1.32.1
MEDNAFEN_SOURCE = mednafen-$(MEDNAFEN_VERSION).tar.xz
MEDNAFEN_SITE = https://mednafen.github.io/releases/files
MEDNAFEN_LICENSE = GPL-2.0+
MEDNAFEN_LICENSE_FILES = COPYING

MEDNAFEN_DEPENDENCIES = \
	zlib \
	libsndfile \
	libsamplerate \
	flac \
	libvorbis \
	libogg \
	zstd \
	sdl2 \
	alsa-lib


MEDNAFEN_CONF_OPTS = \
	--disable-debugger \
	--disable-cjk-fonts \
	--with-sdl2

$(eval $(autotools-package))
