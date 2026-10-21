CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Wmissing-prototypes -O2 -g
SDL = $(shell brew --prefix sdl3)
B = ../build
LAYERS = data engine ui control platform
INCLUDES = $(addsuffix /include,$(addprefix -I../src/,$(LAYERS))) -I../src/third_party/opl3
OPL3 = ../src/third_party/opl3/opl3.c
GAME = $(filter-out ../src/platform/main.c ../src/platform/platform.c,$(wildcard $(addsuffix /*.c,$(addprefix ../src/,$(LAYERS))))) $(OPL3)
SRC = $(wildcard $(addsuffix /*.[ch],$(addprefix ../src/,$(LAYERS))) $(addsuffix /include/*.h,$(addprefix ../src/,$(LAYERS))) ../src/third_party/opl3/*.[ch])

all: $(B)/dm2 $(B)/libdm2.dylib

$(B)/dm2: $(SRC)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(SDL)/include -o $@ ../src/platform/main.c ../src/platform/platform.c $(GAME) -L$(SDL)/lib -lSDL3

$(B)/libdm2.dylib: $(SRC)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $(INCLUDES) -dynamiclib -o $@ $(GAME)

clean:
	rm -rf $(B)

.PHONY: all clean
