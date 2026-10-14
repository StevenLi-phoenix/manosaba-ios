# Host build. GAME selects the game directory games/<GAME>/ (default manosaba): its game.json becomes gen/game_profile.c (tools/gen_game.py)
# and its C / Objective-C sources, if any, are built in. ios/tools/gen_build.py takes the iOS app's sources, include paths and frameworks from the
# compile command below (make -n), so the iOS app builds the same host.
GAME ?= manosaba
DM_ROOT ?= $(HOME)/dm-work
GAMEDIR := ../../games/$(GAME)
ifeq ($(wildcard $(GAMEDIR)/game.json),)
$(error no game profile $(GAMEDIR)/game.json)
endif
# Minimum system version: the graphics backend (gfx/nvn/nvn_mtl.m) is built on Metal 4, which exists from macOS 26. tools/mac_app.sh reads the same
# number through print-macos-min for Info.plist.
MACOS_MIN ?= 26.0
CFLAGS = -O1 -g -Wall -Wno-unused-function -mmacosx-version-min=$(MACOS_MIN)
# Host sources, one per line in alphabetical order; the hle_*.c service files are taken by wildcard
SRC = \
	app_mac.m \
	cloud.m \
	cloudsync.c \
	crash.c \
	diag.c \
	entry.S \
	front_mac.m \
	game.c \
	gamepad.m \
	gen/game_profile.c \
	hle.c \
	loader.c \
	main.c \
	memstat.m \
	movie_av.m \
	romfs_validate.c \
	thread.c \
	$(sort $(wildcard hle_*.c)) \
	$(sort $(wildcard $(GAMEDIR)/*.c $(GAMEDIR)/*.m))
# NVN → Metal and nn::vi (gfx/): sources under gfx, gfx/nvn and gfx/tex by wildcard (tests/ and metal/ are not part of the host)
GFX = $(sort $(wildcard ../gfx/*.c ../gfx/nvn/*.c ../gfx/nvn/*.m ../gfx/nvn/*.S ../gfx/tex/*.c ../gfx/tex/*.m))
GFX_H = $(sort $(wildcard ../gfx/nvn/*.h ../gfx/nvn/include/*.h ../gfx/tex/*.h))
HDR = $(wildcard *.h) $(GFX_H) $(wildcard $(GAMEDIR)/*.h)
FRAMEWORKS = -framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox -framework CoreAudio -framework ImageIO \
	-framework UniformTypeIdentifiers -framework GameController -framework Network -framework AVFoundation -framework CoreMedia -framework CoreVideo
INCLUDES = -I. -I../gfx/nvn/include -I$(GAMEDIR)
.DEFAULT_GOAL := dmhost

gen/game_profile.c: $(GAMEDIR)/game.json ../tools/gen_game.py
	@mkdir -p gen
	python3 ../tools/gen_game.py $< $@.tmp && mv $@.tmp $@

dmhost: $(SRC) $(GFX) $(HDR)
	clang $(CFLAGS) $(INCLUDES) -fobjc-arc -o $@ $(SRC) $(GFX) $(FRAMEWORKS)

# Older-API host (graphics backend without Metal 4, macOS 15; see DM_NVN_M3 in gfx/nvn/nvn_mtl.m): same sources, one more switch. make dmhost_m3
MACOS_MIN_M3 ?= 15.0
CFLAGS_M3 = $(subst -mmacosx-version-min=$(MACOS_MIN),-mmacosx-version-min=$(MACOS_MIN_M3),$(CFLAGS)) -DDM_NVN_M3=1
dmhost_m3: $(SRC) $(GFX) $(HDR)
	clang $(CFLAGS_M3) $(INCLUDES) -fobjc-arc -o $@ $(SRC) $(GFX) $(FRAMEWORKS)

print-macos-min:
	@echo $(MACOS_MIN)
print-macos-min-m3:
	@echo $(MACOS_MIN_M3)
print-game:
	@echo $(GAME)

.PHONY: print-macos-min print-macos-min-m3 print-game
