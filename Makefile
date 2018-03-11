.PHONY: all clean debug profile
.SUFFIXES:
.PRECIOUS: src/%.lh src/%.o

include config.mak.dist
-include config.mak

HEADERS = \
  src/audio.h \
  src/font.h \
  src/image.h \
  src/lua-file.h \
  src/lua-image.h \
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
  src/thread.o \
  src/video.o \
  src/visualizer.o

LUALHS = \
  src/font.lua.lh \
  src/image.lua.lh \
  src/stream.lua.lh

MAINSRCS = src/main.c

BIN2CSRC = src/bin2c.c

all: mpd-visualizer$(EXE_EXT)

mpd-visualizer$(EXE_EXT): $(SO_PREFIX)visualizer$(SO_EXT) src/main.c src/whereami.c
	$(CC) $(CFLAGS) -DSO_PREFIX=\"$(SO_PREFIX)\" -DSO_EXT=\"$(SO_EXT)\" -o mpd-visualizer$(EXE_EXT) -I src src/main.c src/whereami.c $(LDFLAGS) -pthread

src/%.o: src/%.c $(LUALHS)
	$(CC) $(CFLAGS) -o $@ -c $<

src/%.lh: lua/% src/bin2c
	./src/bin2c $< $@ $(patsubst %.lua,%_lua,$(notdir $<))

$(SO_PREFIX)visualizer$(SO_EXT): $(LIBOBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS_SO)

src/bin2c$(EXE_EXT): src/bin2c.c
	$(HOSTCC) -o src/bin2c$(EXE_EXT) src/bin2c.c

clean:
	rm -f mpd-visualizer$(EXE_EXT) $(SO_PREFIX)visualizer$(SO_EXT) src/bin2c$(EXE_EXT) $(LIBOBJS) $(LUALHS)

