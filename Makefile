# Makefile for ESP8266 projects
#
# Thanks to:
# - zarya
# - Jeroen Domburg (Sprite_tm)
# - Christian Klippel (mamalala)
# - Tommie Gannert (tommie)
#
# Changelog:
# - 2014-10-06: Changed the variables to include the header file directory
# - 2014-10-06: Added global var for the Xtensa tool root
# - 2014-11-23: Updated for SDK 0.9.3
# - 2014-12-25: Replaced esptool by esptool.py

# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build
FW_BASE		= firmware

# base directory for the compiler
XTENSA_TOOLS_ROOT ?= /opt/Espressif/crosstool-NG/builds/xtensa-lx106-elf/bin

# base directory of the ESP8266 SDK package, absolute
SDK_BASE	?= /opt/Espressif/ESP8266_SDK

# esptool.py path and port
ESPTOOL		?= esptool.py
ESPPORT		?= /dev/ttyUSB0

# name for the target project
TARGET		= app

WISH_MODULES = wish-c99/src wish-c99/deps/mbedtls-2.1.2/library wish-c99/deps/bson \
wish-c99/deps/ed25519/src  wish-c99/deps/wish-rpc-c99/src

WISH_INCLUDES = wish-c99/deps/mbedtls-2.1.2/include wish-c99/deps/uthash/include \
port/esp8266

MIST_MODULES = mist-c99/src mist-c99/wish_app wish_app_deps_esp8266 
MIST_INCLUDES = mist-c99/deps/uthash/include mist-c99/deps/wish-rpc-c99/src mist-c99/deps/mbedtls-2.1.2/include mist-c99/deps/ed25519/src port/esp8266 wish-c99/deps/bson


PORT_MODULES = port/esp8266/ port/esp8266/driver port/esp8266/spiffs/src 
PORT_INCLUDES = port/esp8266/include wish-c99/src wish-c99/deps/wish-rpc-c99/src wish-c99/deps/bson wish-c99/deps/uthash/include \
apps/mist-config apps/mist-esp8266-sonoff-app port/esp8266/libesphttpd/include

APP_MODULES = apps/mist-config apps/mist-esp8266-sonoff-app 
APP_INCLUDES = port/esp8266 mist-c99/src mist-c99/deps/wish-rpc-c99/src wish-c99/deps/bson wish-c99/deps/uthash/include mist-c99/wish_app \
wish_app_deps_esp8266 

#libesphttpd; note: you will need to compile the .a file separately
HTTPDLIB = esphttpd
HTTPDLIBPATH = libesphttpd/

# libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal pp phy net80211 lwip wpa main crypto

# compiler flags using during compilation of source files
#CFLAGS		= -Os -g -O2 -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH
# Remove -Werror for now, as our port of mbedTLS produces som many
# warnings
CFLAGS		= -Os -std=c99 -Wall -Wno-pointer-sign -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals -D__ets__ -DICACHE_FLASH -ffunction-sections -fdata-sections -DCOMPILING_FOR_ESP8266 -DWITHOUT_STRTOIMAX -DWITHOUT_STRDUP
CFLAGS += -DRELEASE_BUILD

CFLAGS += -DMIST_API_VERSION_STRING=\"esp8266\"
CFLAGS += -DMIST_RPC_REPLY_BUF_LEN=1400
CFLAGS += -DMIST_API_MAX_UIDS=4
CFLAGS += -DMIST_API_REQUEST_POOL_SIZE=4
CFLAGS += -DWISH_PORT_RPC_BUFFER_SZ=1400
CFLAGS += -DNUM_MIST_APPS=2 -DNUM_WISH_APPS=2
CFLAGS += -DPORT_VERSION=\"$(shell git describe --tags --dirty)\"

# linker flags used to generate the main object file
LDFLAGS		= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

# linker script used for the above linkier step
LD_SCRIPT	= -Tport/esp8266/ld/eagle.app.v6.ld

# various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json

# we create two different files for uploading into the flash
# these are the names and options to generate them
# Note that these are for "no bootloader" configuration.
FW_FILE_1_ADDR	= 0x00000
FW_FILE_2_ADDR	= 0x40000
BLANK_FILE = /opt/Espressif/ESP8266_SDK/bin/blank.bin
ESP_INIT_DATA_FILE = /opt/Espressif/ESP8266_SDK/bin/esp_init_data_default.bin

BLANK_ADDR = 0xFE000	#setting for 1024 KB flash
ESP_INIT_DATA_ADDR = 0xFC000 #setting for 1024 KB flash
ESP_RF_CAL_SEC_ADDR = 0xFA000 #setting for 1024 KB flash
#Export the RF_CAL_SEC as macro to C code
CFLAGS += -DRF_CAL_SEC_ADDR=$(ESP_RF_CAL_SEC_ADDR)

# select which tools to use as compiler, librarian and linker
CC		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
SIZE	:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-size



####
#### no user configurable options below here
####
SRC_DIR		:= $(WISH_MODULES) $(MIST_MODULES) $(PORT_MODULES) $(APP_MODULES)
WISH_BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(WISH_MODULES))
MIST_BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MIST_MODULES))
PORT_BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(PORT_MODULES))
APP_BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(APP_MODULES))
BUILD_DIRS	:= $(WISH_BUILD_DIR) $(MIST_BUILD_DIR) $(PORT_BUILD_DIR) $(APP_BUILD_DIR)
	
SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))

SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
SRC := $(filter-out mist-c99/src/mist_api.c,$(SRC))
SRC := $(filter-out mist-c99/src/mist_api_sandbox.c,$(SRC))
SRC := $(filter-out mist-c99/src/sandbox.c,$(SRC))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)
TARGET_OUT	:= $(addprefix $(BUILD_BASE)/,$(TARGET).out)

#LD_SCRIPT	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_SCRIPT))

WISH_INCDIR	:= $(addprefix -I,$(WISH_MODULES))
WISH_INCDIR	+= $(addprefix -I,$(WISH_INCLUDES))
MIST_INCDIR	:= $(addprefix -I,$(MIST_MODULES))
MIST_INCDIR	+= $(addprefix -I,$(MIST_INCLUDES))
PORT_INCDIR	:= $(addprefix -I,$(PORT_MODULES))
PORT_INCDIR	+= $(addprefix -I,$(PORT_INCLUDES))
APP_INCDIR	:= $(addprefix -I,$(APP_MODULES))	
APP_INCDIR	+= $(addprefix -I,$(APP_INCLUDES))

FW_FILE_1	:= $(addprefix $(FW_BASE)/,$(FW_FILE_1_ADDR).bin)
FW_FILE_2	:= $(addprefix $(FW_BASE)/,$(FW_FILE_2_ADDR).bin)

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.c $(SRC_DIR)

define compile-wish-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(WISH_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

define compile-mist-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(MIST_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

define compile-port-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(PORT_INCDIR) $(WISH_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

define compile-app-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(APP_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

.PHONY: all checkdirs flash clean

all: checkdirs $(TARGET_OUT) $(FW_FILE_1) $(FW_FILE_2)

$(FW_BASE)/%.bin: $(TARGET_OUT) | $(FW_BASE)
	$(vecho) "FW $(FW_BASE)/"
	$(Q) $(ESPTOOL) elf2image --flash_size 8m --flash_mode qio -o $(FW_BASE)/ $(TARGET_OUT)

$(TARGET_OUT): $(APP_AR)
	$(vecho) "LD $@ :" $(APP_AR) 
	$(Q) $(LD) -L$(SDK_LIBDIR)  -L$(HTTPDLIBPATH) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -l$(HTTPDLIB) -Wl,-Map=$(TARGET).map -Wl,--end-group -o $@
	$(Q) $(SIZE) -A $@

$(APP_AR): $(OBJ)
	$(vecho) "AR $@ "
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIRS) $(FW_BASE)

$(BUILD_DIRS):
	$(Q) mkdir -p $@

$(FW_BASE):
	$(Q) mkdir -p $@

flash_all:
	$(vecho) "Flashing program, blank.bin and esp_init_data.bin, also \
blank.bin to RF_CAL sector"
	$(ESPTOOL) --port $(ESPPORT) write_flash --flash_size 8m --flash_mode qio $(FW_FILE_1_ADDR) $(FW_FILE_1) $(FW_FILE_2_ADDR) $(FW_FILE_2) $(BLANK_ADDR) $(BLANK_FILE) $(ESP_INIT_DATA_ADDR) $(ESP_INIT_DATA_FILE) $(ESP_RF_CAL_SEC_ADDR) $(BLANK_FILE)

flash: $(FW_FILE_1) $(FW_FILE_2)
	$(vecho) "Flashing just the program"
	$(ESPTOOL) --port $(ESPPORT) write_flash --flash_size 8m --flash_mode qio $(FW_FILE_1_ADDR) $(FW_FILE_1) $(FW_FILE_2_ADDR) $(FW_FILE_2)

flash_erase:
	$(vecho) "Erasing all of flash"
	$(ESPTOOL) --port $(ESPPORT) erase_flash

clean:
	$(Q) rm -rf $(FW_BASE) $(BUILD_BASE)

#A target for just starting the program when chip is in bootloader mode.
#Useful if you want to prevent the chip from rebooting automatically
#after a system fail
run:
	$(ESPTOOL) --port $(ESPPORT) run

$(foreach bdir,$(WISH_BUILD_DIR),$(eval $(call compile-wish-objects,$(bdir))))
$(foreach bdir,$(MIST_BUILD_DIR),$(eval $(call compile-mist-objects,$(bdir))))
$(foreach bdir,$(PORT_BUILD_DIR),$(eval $(call compile-port-objects,$(bdir))))
$(foreach bdir,$(APP_BUILD_DIR),$(eval $(call compile-app-objects,$(bdir))))
