RETROCONSOLE_SPLASH_VERSION = 1.0.0
RETROCONSOLE_SPLASH_SITE = ./package/retroconsole-splash/src
RETROCONSOLE_SPLASH_SITE_METHOD = local
RETROCONSOLE_SPLASH_LICENSE = MIT
RETROCONSOLE_SPLASH_DEPENDENCIES = libpng

define RETROCONSOLE_SPLASH_BUILD_CMDS
	$(TARGET_CC) -Wall -O2 \
		$(@D)/splash.c \
		-lpng -lz \
		-o $(@D)/retroconsole-splash
endef

define RETROCONSOLE_SPLASH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/retroconsole-splash \
		$(TARGET_DIR)/usr/bin/retroconsole-splash
endef

$(eval $(generic-package))
