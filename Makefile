# Optional Theos build, using the same C source as the standalone dylib.
ARCHS = arm64
ifeq ($(THEOS_PACKAGE_SCHEME),rootless)
TARGET = iphone:clang:latest:15.0
else
TARGET = iphone:clang:latest:14.0
endif
include $(THEOS)/makefiles/common.mk
TWEAK_NAME = PVZ2AudioFix
PVZ2AudioFix_FILES = src/PVZ2AudioFix.c
PVZ2AudioFix_FRAMEWORKS = AudioToolbox
PVZ2AudioFix_CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -fvisibility=hidden
include $(THEOS_MAKE_PATH)/tweak.mk
