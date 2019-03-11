.PHONY: all clean debug profile dist
.SUFFIXES:
.PRECIOUS: src/%.lh src/%.o

VERSION = 2.1.0

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
  src/lua-audio.c \
  src/lua-file.c \
  src/lua-image.c \
  src/ringbuf.c \
  src/thread.c \
  src/video.c \
  src/visualizer.c

OBJS = \
  src/audio.o \
  src/avi_header.o \
  src/image.o \
  src/lua-audio.o \
  src/lua-file.o \
  src/lua-image.o \
  src/main.o \
  src/ringbuf.o \
  src/thread.o \
  src/video.o \
  src/visualizer.o

LUALHS = \
  src/font.lua.lh \
  src/image.lua.lh \
  src/stream.lua.lh

MAINSRCS = src/main.c

BIN2CSRC = src/bin2c.c

all: mpd-visualizer

mpd-visualizer: $(OBJS)
	$(CC) -o mpd-visualizer $(OBJS) $(LDFLAGS) -pthread

src/%.o: src/%.c $(LUALHS)
	$(CC) $(CFLAGS) -o $@ -c $<

src/%.lh: lua/% src/bin2c
	./src/bin2c $< $@ $(patsubst %.lua,%_lua,$(notdir $<))

src/bin2c: src/bin2c.c
	$(HOSTCC) -o src/bin2c src/bin2c.c

clean:
	rm -f mpd-visualizer src/libvisualizer.a src/bin2c $(OBJS) $(LUALHS)

dist:
	rm -rf dist/mpd-visualizer-$(VERSION)
	rm -rf dist/mpd-visualizer-$(VERSION).tar.gz
	rm -rf dist/mpd-visualizer-$(VERSION).tar.xz
	mkdir -p dist/mpd-visualizer-$(VERSION)
	cp -r demos dist/mpd-visualizer-$(VERSION)/demos
	cp -r gifs dist/mpd-visualizer-$(VERSION)/gifs
	cp -r lua dist/mpd-visualizer-$(VERSION)/lua
	cp -r src dist/mpd-visualizer-$(VERSION)/src
	cp LICENSE dist/mpd-visualizer-$(VERSION)/LICENSE
	cp LICENSE.ringbuf dist/mpd-visualizer-$(VERSION)/LICENSE.ringbuf
	cp Makefile dist/mpd-visualizer-$(VERSION)/Makefile
	cp README.md dist/mpd-visualizer-$(VERSION)/README.md
	cp config.mak.dist dist/mpd-visualizer-$(VERSION)/config.mak.dist
	tar cvf dist/mpd-visualizer-$(VERSION).tar dist/mpd-visualizer-$(VERSION)
	gzip -k dist/mpd-visualizer-$(VERSION).tar
	xz dist/mpd-visualizer-$(VERSION).tar
