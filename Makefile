.PHONY: all clean debug profile
.SUFFIXES:
.PRECIOUS: src/%.lh src/%.o

CC=gcc

CFLAGS_OPTIMIZE = -pedantic -g0 -O3 -fPIC -march=native

LUA=luajit

CFLAGS = $(shell pkg-config --cflags fftw3)
CFLAGS += $(shell pkg-config --cflags libmpdclient)
CFLAGS += $(shell pkg-config --cflags $(LUA))
CFLAGS += -Wall -Wextra $(CFLAGS_OPTIMIZE)
CFLAGS += -D_GNU_SOURCE

LDFLAGS = -lskarnet -lm
LDFLAGS += -Wl,-rpath,$(shell pkg-config --variable=libdir libmpdclient)
LDFLAGS += $(shell pkg-config --libs fftw3)
LDFLAGS += $(shell pkg-config --libs libmpdclient)
LDFLAGS += $(shell pkg-config --libs $(LUA))

HEADERS = \
  src/audio.h \
  src/font.h \
  src/image.h \
  src/lua-file.h \
  src/lua-image.h \
  src/ringbuf.h \
  src/shared.h \
  src/stb_image.h \
  src/stb_image_resize.h \
  src/thread.h \
  src/video.h \
  src/visualizer.h

LIBSRCS = \
  src/audio.c \
  src/avi_header.c \
  src/image.c \
  src/lua-file.c \
  src/lua-image.c \
  src/ringbuf.c \
  src/thread.c \
  src/video.c \
  src/visualizer.c

LIBOBJS = \
  src/audio.o \
  src/avi_header.o \
  src/image.o \
  src/lua-audio.o \
  src/lua-file.o \
  src/lua-image.o \
  src/ringbuf.o \
  src/thread.o \
  src/video.o \
  src/visualizer.o

LUALHS = \
  src/font.lua.lh \
  src/image.lua.lh \
  src/stream.lua.lh

#src/visualizer.lua.lh

MAINSRCS = src/main.c

BIN2CSRC = src/bin2c.c

all: mpd-visualizer

mpd-visualizer: src/libvisualizer.a src/main.c
	$(CC) $(CFLAGS) -o mpd-visualizer src/main.c -Lsrc -rdynamic -lvisualizer $(LDFLAGS) -pthread

src/%.o: src/%.c $(LUALHS)
	$(CC) $(CFLAGS) -o $@ -c $<

src/%.lh: lua/% src/bin2c
	./src/bin2c $< $@ $(patsubst %.lua,%_lua,$(notdir $<))

src/libvisualizer.a: $(LIBOBJS)
	ar rcs $@ $^

src/bin2c: src/bin2c.c
	$(CC) -o src/bin2c src/bin2c.c

clean:
	rm -f mpd-visualizer src/libvisualizer.a src/bin2c $(LIBOBJS) $(LUALHS)

debug:
	make clean
	make CFLAGS_OPTIMIZE="-g -fPIC" all

profile:
	make clean
	make CFLAGS_OPTIMIZE="-g -pg -no-pie -fPIC"

profile-clang:
	make clean
	make CFLAGS_OPTIMIZE="-g -pg -fPIC"
