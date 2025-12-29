SHELL = /bin/sh

ifeq ($(DEVICE), RG35XX)
    ARCH = -march=armv7-a -mtune=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=softfp
else ifeq ($(DEVICE), RG35XXPLUS)
    ARCH = -march=armv8-a+simd -mtune=cortex-a53
else ifeq ($(DEVICE), ARM64)
    ARCH = -march=armv8-a
else ifeq ($(DEVICE), NATIVE)
    ARCH = -march=native
else
    $(error Unsupported Device: $(DEVICE))
endif

BIN_DIR = ./bin

MODULE_DIR = module
MODULES = muparentlock

DEPENDENCIES = common font lvgl lookup module

DEBUG ?= 0
VERBOSE = $(if $(filter 2,$(DEBUG)),, @)
QUIET = $(if $(filter 1,$(DEBUG)),,>/dev/null 2>&1)

CC = ccache $(CROSS_COMPILE)gcc -O3

CFLAGS = $(ARCH) -flto=auto -ffunction-sections -fdata-sections \
         -flto -finline-functions -Wall -Wno-format-zero-length

MUXLIB = $(CFLAGS) -I./module/ui -I./font -I./lookup -I./common \
         -I./common/img -I./common/input -I./common/json \
         -I./common/mini -I./common/miniz

LDFLAGS = $(MUXLIB) -L./bin/lib -lui -llookup -lmux -lmuxmodule \
          -lnotosans_big -lnotosans_big_hd \
		  -lnotosans_medium -lnotosans_sc_medium -lnotosans_tc_medium -lnotosans_jp_medium -lnotosans_kr_medium -lnotosans_ar_medium\
          -lSDL2 -lSDL2_mixer -lSDL2_ttf -lSDL2_image -lpng -Wl,--gc-sections -s -Wl,-rpath,'./lib'

# Linking with dynamic font since that bloat the executable by a large amount if linked statically (more than 30MB)
LDFLAGS_STATIC = $(MUXLIB) -L./bin/lib \
		  -Wl,-Bstatic -llookup -lmux -lmuxmodule -lui -lnotosans_medium -lnotosans_big \
          -Wl,-Bdynamic -lSDL2 -lSDL2_mixer -lSDL2_ttf -lSDL2_image -lpng -lm \
		  -Wl,--gc-sections -s -Wl,-rpath,'./lib'


EXTRA = $(LDFLAGS) -fno-exceptions -fno-stack-protector -fomit-frame-pointer \
        -fno-unroll-loops -fmerge-all-constants -fno-ident -ffast-math \
        -funroll-loops -falign-functions

EXTRA_STATIC = $(LDFLAGS_STATIC) -fno-exceptions -fno-stack-protector -fomit-frame-pointer \
        -fno-unroll-loops -fmerge-all-constants -fno-ident -ffast-math \
        -funroll-loops -falign-functions

.PHONY: all $(MODULES) prebuild clean notify

all: info prebuild $(MODULES) clean notify

info:
	@echo "======== muOS Frontend Builder ========"
	@echo "Targeting: $(DEVICE)"
	@echo "Modules: $(MODULES)"
	@echo "Dependencies: $(DEPENDENCIES)"

prebuild:
	$(VERBOSE)for DEP in $(DEPENDENCIES); do \
		echo "Building Dependency: $$DEP"; \
		$(MAKE) -C $$DEP $(QUIET) || { echo "Error building dependency $$DEP"; exit 1; }; \
	done

clean:
	$(VERBOSE)rm -rf .build_count

%.o: $(MODULE_DIR)/%.c
	@echo "Compiling $< to $@"
	$(VERBOSE)$(CC) -D$(DEVICE) $(CFLAGS) -c $< -o $@ $(EXTRA) $(QUIET)

$(MODULES):
	@echo "Building Module: $@"
	$(VERBOSE)UI_FILE="$(MODULE_DIR)/ui/ui_$@.c"; \
	UI_OBJ="$(MODULE_DIR)/ui/ui_$@.o"; \
	if [ -f "$$UI_FILE" ]; then \
		rm -f "$$UI_OBJ"; \
		$(MAKE) -C $(MODULE_DIR)/ui ui_$@.o $(QUIET) || { echo "Error building UI object"; exit 1; }; \
	else \
		UI_OBJ=""; \
	fi; \
	$(CC) -D$(DEVICE) $(MODULE_DIR)/$@.c $$UI_OBJ -o $@ $(EXTRA_STATIC) $(QUIET) || { echo "Error building $@"; exit 1; }; \
	mkdir -p $(BIN_DIR); mv $@ $(BIN_DIR) || { echo "Error moving $@ to $(BIN_DIR)"; exit 1; }
	$(VERBOSE)find ./$(MODULE_DIR) -name "*.o" -exec rm -f {} +

notify:
	@printf "Compiled %d Modules\n============== Complete! ==============\n" "$(words $(MODULES))"

archive:
	-rm -r archive
	-mkdir -p archive/application/ParentLock/
	-cp bin/muparentlock archive/application/ParentLock/
	-cp install.md archive/application/ParentLock/
	-cp parent_lock.ini archive/application/ParentLock/
	-cp 5mnLeft.png archive/application/ParentLock/
	-mkdir -p archive/init/
	-cp plock.sh archive/init/
	@printf "Creating archive for MuOS now\n"
	-(cd archive && zip ../parentlock.muxzip -r .)
