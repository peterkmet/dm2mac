CC = clang
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
SDL = $(shell brew --prefix sdl3)
B = ../build
LAYERS = data engine ui platform
INCLUDES = $(addprefix -Isrc/,$(LAYERS))
GAME = $(filter-out src/platform/main.c src/platform/platform.c,$(wildcard $(addsuffix /*.c,$(addprefix src/,$(LAYERS)))))
SRC = $(wildcard $(addsuffix /*.[ch],$(addprefix src/,$(LAYERS))))

all: $(B)/dm2 $(B)/libdm2.dylib

$(B)/dm2: $(SRC)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(SDL)/include -o $@ src/platform/main.c src/platform/platform.c $(GAME) -L$(SDL)/lib -lSDL3

$(B)/libdm2.dylib: $(SRC)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -dynamiclib -o $@ $(GAME)

clean:
	rm -rf $(B)

.PHONY: all clean
