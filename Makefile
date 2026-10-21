# The Dungeon Master II port: the game and the test library.
CC          = clang
BASE_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla \
              -Wimplicit-fallthrough -O2 -g
CFLAGS      = $(BASE_CFLAGS)
CPPFLAGS    = -isystem src/third_party -MMD -MP
LAYERS      = data engine ui control platform
INCLUDES    = $(addprefix -Isrc/,$(addsuffix /include,$(LAYERS))) -Isrc/third_party/opl3
LDFLAGS     =
ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined
LDFLAGS += -fsanitize=address,undefined
endif
# Extra flags for one build, for example a developer check: make lib CFLAGS_EXTRA=-DX
CFLAGS += $(CFLAGS_EXTRA)

# SDL3 from Homebrew. Recursively expanded: only the targets that compile or link SDL code run brew.
SDL_PREFIX ?= $(shell brew --prefix sdl3)

B   = build
OBJ = $(B)/obj

SOURCES      = $(wildcard src/*/*.c src/third_party/*/*.c)
SDL_SOURCES  = src/platform/main.c src/platform/platform.c
LIB_SOURCES  = $(filter-out $(SDL_SOURCES),$(SOURCES))
C_FILES      = $(wildcard src/*/*.[ch] src/*/include/*.h src/third_party/*/*.[ch])

objects = $(patsubst src/%.c,$(1)/%.o,$(2))

all: $(B)/dm2 $(B)/libdm2.dylib

lib: $(B)/libdm2.dylib

# The stamp of the compile flags. When the flags change (SANITIZE, CFLAGS_EXTRA), the stamp deletes the objects,
# so all recompile. Simply expanded: the SDL include path of two objects stays out.
OBJ_FLAGS := $(strip $(CC) $(CPPFLAGS) $(CFLAGS))
stamp = $(strip $(shell cat $(1) 2>/dev/null))
write_flags = @mkdir -p $(@D); rm -f $(@D)/*/*.o $(@D)/*/*/*.o; echo '$(1)' > $@

ifneq ($(OBJ_FLAGS),$(call stamp,$(OBJ)/flags.txt))
$(OBJ)/flags.txt: FORCE
endif
$(OBJ)/flags.txt:
	$(call write_flags,$(OBJ_FLAGS))

$(OBJ)/%.o: src/%.c Makefile $(OBJ)/flags.txt
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(INCLUDES) $(CFLAGS) -c -o $@ $<

$(call objects,$(OBJ),$(SDL_SOURCES)): CPPFLAGS += -I$(SDL_PREFIX)/include

$(B)/dm2: $(call objects,$(OBJ),$(SOURCES))
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -L$(SDL_PREFIX)/lib -lSDL3

$(B)/libdm2.dylib: $(call objects,$(OBJ),$(LIB_SOURCES))
	$(CC) $(CFLAGS) $(LDFLAGS) -dynamiclib -o $@ $^

test: ; tools/test-suite

clean:
	rm -rf $(B)

-include $(wildcard $(OBJ)/*/*.d $(OBJ)/*/*/*.d)

FORCE:

.PHONY: all lib test clean FORCE
