# The Dungeon Master II port: the game, the test library, and the Windows build.
CC          = clang
BASE_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla \
              -Wimplicit-fallthrough -O2 -g -fwrapv
CFLAGS      = $(BASE_CFLAGS)
CPPFLAGS    = -isystem src/third_party -MMD -MP
# The include path of a layer: its own public headers and those of the layers before it. A private *_internal.h
# stands next to its .c file. A wrong layer include does not compile.
INC_data     = -Isrc/data/include
INC_engine   = $(INC_data) -Isrc/engine/include
INC_ui       = $(INC_engine) -Isrc/ui/include
INC_control  = $(INC_ui) -Isrc/control/include
INC_platform = $(INC_control) -Isrc/platform/include
layer_inc    = $(INC_$(word 2,$(subst /, ,$(1))))
LDFLAGS     =
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif
# Extra flags for one build, for example a developer check: make lib CFLAGS_EXTRA=-DX
CFLAGS += $(CFLAGS_EXTRA)

# Host flags stay separate from BASE_CFLAGS, which the Windows cross-build also uses.
HOST_OS ?= $(shell uname -s)
PKG_CONFIG ?= pkg-config
ifeq ($(HOST_OS),Linux)
NATIVE_CPPFLAGS = -D_DEFAULT_SOURCE
CFLAGS += -fPIC
NATIVE_LIBS = -lm
LIB_EXT = so
LIB_LINK = -shared
# Recursively expanded: library-only builds never query SDL or pkg-config.
SDL_PC = $(if $(strip $(SDL_PREFIX)),PKG_CONFIG_PATH="$(SDL_PREFIX)/lib/pkgconfig:$(SDL_PREFIX)/lib64/pkgconfig:$(PKG_CONFIG_PATH)") $(PKG_CONFIG)
SDL_CFLAGS = $(shell $(SDL_PC) --cflags 'sdl3 >= 3.4.0' 2>/dev/null)
SDL_LIBS = $(shell $(SDL_PC) --libs 'sdl3 >= 3.4.0' 2>/dev/null)
ifneq ($(strip $(SDL_PREFIX)),)
SDL_LIBS += -Wl,-rpath,$(SDL_PREFIX)/lib -Wl,-rpath,$(SDL_PREFIX)/lib64
endif
else
LIB_EXT = dylib
LIB_LINK = -dynamiclib
# SDL3 from Homebrew; queried only by targets that compile or link SDL code.
SDL_PREFIX ?= $(shell brew --prefix sdl3)
SDL_CFLAGS = -I$(SDL_PREFIX)/include
SDL_LIBS = -L$(SDL_PREFIX)/lib -lSDL3
endif

B   = build
OBJ = $(B)/obj

SOURCES      = $(wildcard src/*/*.c src/third_party/*/*.c)
SDL_SOURCES  = src/platform/main.c src/platform/platform.c
WINDOWS_ONLY = src/platform/transport_none.c
POSIX_ONLY   = src/platform/transport.c
GAME_SOURCES = $(filter-out $(WINDOWS_ONLY),$(SOURCES))
LIB_SOURCES  = $(filter-out $(SDL_SOURCES) $(WINDOWS_ONLY),$(SOURCES))
WIN_SOURCES  = $(filter-out $(POSIX_ONLY),$(SOURCES))
C_FILES      = $(wildcard src/*/*.[ch] src/*/include/*.h)

objects = $(patsubst src/%.c,$(1)/%.o,$(2))

all: $(B)/dm2 $(B)/libdm2.$(LIB_EXT)

lib: $(B)/libdm2.$(LIB_EXT)

# The stamp of the compile flags. When the flags change (SANITIZE, CFLAGS_EXTRA), the stamp deletes the objects,
# so all recompile. Simply expanded: the SDL include path of two objects stays out.
OBJ_FLAGS := $(strip $(CC) $(CPPFLAGS) $(NATIVE_CPPFLAGS) $(CFLAGS))
stamp = $(strip $(shell cat $(1) 2>/dev/null))
write_flags = @mkdir -p $(@D); rm -f $(@D)/*/*.o $(@D)/*/*/*.o; echo '$(1)' > $@

ifneq ($(OBJ_FLAGS),$(call stamp,$(OBJ)/flags.txt))
$(OBJ)/flags.txt: FORCE
endif
$(OBJ)/flags.txt:
	$(call write_flags,$(OBJ_FLAGS))

$(OBJ)/%.o: src/%.c Makefile $(OBJ)/flags.txt
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(NATIVE_CPPFLAGS) $(call layer_inc,$<) $(CFLAGS) -c -o $@ $<

$(call objects,$(OBJ),$(SDL_SOURCES)): CPPFLAGS += $(SDL_CFLAGS)
# SDL headers and runtime paths can change without the general compiler flags changing.
# Compare at recipe time so make lib never performs SDL discovery.
$(OBJ)/sdl-flags.txt: FORCE | check-sdl
	@mkdir -p $(@D)
	@echo '$(strip $(SDL_CFLAGS) $(SDL_LIBS))' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(call objects,$(OBJ),$(SDL_SOURCES)): $(OBJ)/sdl-flags.txt | check-sdl

$(B)/dm2: $(call objects,$(OBJ),$(GAME_SOURCES)) $(OBJ)/sdl-flags.txt | check-sdl
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.o,$^) $(SDL_LIBS) $(NATIVE_LIBS)

$(B)/libdm2.$(LIB_EXT): $(call objects,$(OBJ),$(LIB_SOURCES))
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIB_LINK) -o $@ $^ $(NATIVE_LIBS)

# Used by the test harness too: one dependency probe for native executable builds.
check-sdl:
ifeq ($(HOST_OS),Linux)
	@$(SDL_PC) --exists 'sdl3 >= 3.4.0' || { echo "SDL3 >= 3.4.0 development files are required (pkg-config sdl3); see docs/development.md" >&2; exit 1; }
else
	@test -f "$(SDL_PREFIX)/include/SDL3/SDL.h" || { echo "SDL3 development files are required; see docs/development.md" >&2; exit 1; }
endif

# Windows: cross-build with MinGW-w64 (brew install mingw-w64) and the SDL3 MinGW package, copied into build/ from
# the dm port. Windows has no control socket: transport_none.c replaces transport.c. -mwindows: no console window; the
# messages go to dm2.log in the game folder.
WIN_CC            = x86_64-w64-mingw32-gcc
WIN_OBJ           = $(B)/obj-win
DM_DIR           ?= ../dm
SDL_MINGW_VERSION = 3.4.18
SDL_MINGW         = $(B)/sdl-mingw/SDL3-$(SDL_MINGW_VERSION)/x86_64-w64-mingw32
WIN_OBJ_FLAGS := $(strip $(WIN_CC) $(CPPFLAGS) $(BASE_CFLAGS))

windows: $(B)/dm2.exe

ifneq ($(WIN_OBJ_FLAGS),$(call stamp,$(WIN_OBJ)/flags.txt))
$(WIN_OBJ)/flags.txt: FORCE
endif
$(WIN_OBJ)/flags.txt:
	$(call write_flags,$(WIN_OBJ_FLAGS))

$(WIN_OBJ)/%.o: src/%.c Makefile $(WIN_OBJ)/flags.txt | $(SDL_MINGW)
	@mkdir -p $(@D)
	$(WIN_CC) $(CPPFLAGS) $(call layer_inc,$<) -I$(SDL_MINGW)/include $(BASE_CFLAGS) -c -o $@ $<

$(B)/dm2.exe: $(call objects,$(WIN_OBJ),$(WIN_SOURCES))
	$(WIN_CC) $(BASE_CFLAGS) -mwindows -o $@ $^ -L$(SDL_MINGW)/lib -lSDL3

$(SDL_MINGW):
	@test -d $(DM_DIR)/build/sdl-mingw || { echo "copy the SDL3 MinGW package to build/sdl-mingw"; exit 1; }
	@mkdir -p $(B)
	cp -R $(DM_DIR)/build/sdl-mingw $(B)/

test: ; tools/test-suite

# Release zips: dist (macOS arm64), dist-win (Windows x64; DM_DIR names the dm folder).
dist: ; tools/dist/make-dist.sh
dist-win: ; tools/dist/make-dist-win.sh

# Lint, the coding standard check, then the full suite.
check:
	uv run ruff check tests tools/dm2tools
	uv run pytest -q tests/standard
	$(MAKE) test

clean:
	rm -rf $(B)

-include $(wildcard $(OBJ)/*/*.d $(OBJ)/*/*/*.d $(WIN_OBJ)/*/*.d $(WIN_OBJ)/*/*/*.d)

FORCE:

.PHONY: all lib windows dist dist-win test check check-sdl clean format FORCE

format:
	uv run clang-format -i $(C_FILES)
