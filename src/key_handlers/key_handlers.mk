# Copyright 2017 jem@seethis.link
# Licensed under the MIT license (http://opensource.org/licenses/MIT)

MAKEFILE_INC += $(KEYPLUS_PATH)/key_handlers/key_handlers.mk

KEY_HANDLERS_PATH = key_handlers

C_SRC += \
	$(KEY_HANDLERS_PATH)/key_custom.c \
	$(KEY_HANDLERS_PATH)/key_handlers.c \
	$(KEY_HANDLERS_PATH)/key_hold.c \
	$(KEY_HANDLERS_PATH)/key_media.c \
	$(KEY_HANDLERS_PATH)/key_mouse.c \
	$(KEY_HANDLERS_PATH)/key_normal.c \


ifeq ($(SUPPORT_MACRO), 1)
    C_SRC += $(KEY_HANDLERS_PATH)/key_macro.c
endif
