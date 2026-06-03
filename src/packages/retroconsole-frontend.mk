################################################################################
#
# retroconsole-frontend (con gamepad-bridge)
#
################################################################################

RETROCONSOLE_FRONTEND_VERSION = 1.0.0
RETROCONSOLE_FRONTEND_SITE = ./package/retroconsole-frontend/src
RETROCONSOLE_FRONTEND_SITE_METHOD = local
RETROCONSOLE_FRONTEND_LICENSE = MIT

RETROCONSOLE_FRONTEND_DEPENDENCIES = sdl2 sdl2_ttf

define RETROCONSOLE_FRONTEND_BUILD_CMDS
	$(TARGET_CXX) -Wall -O2 -std=c++17 \
		$(@D)/gallery.cpp \
		`$(STAGING_DIR)/usr/bin/sdl2-config --cflags` \
		`$(STAGING_DIR)/usr/bin/sdl2-config --libs` \
		-lSDL2_ttf \
		-o $(@D)/retroconsole-frontend
	$(TARGET_CC) -Wall -O2 \
		$(@D)/gamepad-bridge.c \
		-o $(@D)/gamepad-bridge
endef

define RETROCONSOLE_FRONTEND_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/retroconsole-frontend \
		$(TARGET_DIR)/usr/bin/retroconsole-frontend
	$(INSTALL) -D -m 0755 $(@D)/gamepad-bridge \
		$(TARGET_DIR)/usr/bin/gamepad-bridge
endef

$(eval $(generic-package))
