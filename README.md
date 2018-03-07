# mpd-visualizer

This is a program for creating videos from MPD, using Lua. It's suitable for using in
a never-ending livestream.

It reads audio data from an MPD FIFO, and runs one or more Lua scripts
to create a video.

Video is output to a FIFO as an AVI stream with raw audio and video. This
AVI FIFO can be read by ffmpeg and encoded to an appropriate format.

# Usage

```bash
visualizer \
  -w (width) \
  -h (height) \
  -f (framerate) \
  -r (audio samplerate) \
  -c (audio channels) \
  -s (audio samplesize (in bytes)) \
  -b (number of visualizer bars to calculate) \
  -i /path/to/audio.fifo \
  -o /path/to/video.fifo \
  -l /path/to/Lua/folder
```

## Option details

* `-w (width)`: Video width, ie, `-w 1280`
* `-h (height)`: Video height, ie, `-h 720`
* `-f (framerate)`: Video framerate, ie, `-f 30`
* `-r (samplerate)`: Audio samplerate, in Hz, ie: `-r 48000`
* `-c (channels)`: Audio channels, ie: `-c 2`
* `-s (samplesize)`: Audio samplesize in bytes, ie `-s 2` for 16-bit audio
* `-b (bars)`: number of visualizer bars to calculate
* `-i /path`: Path to your MPD FIFO
* `-o /path`: Path to your video FIFO
* `-l /path`: Path to folder of Lua scripts

## Requirements

* LuaJIT or Lua 5.3.
  * This may work with Lua 5.1 or Lua 5.2, so long as you have either Lua BitOp or Bit32, untested
* [FFTW](http://www.fftw.org/)
* [libmpdclient](https://www.musicpd.org/libs/libmpdclient/)
* [skalibs](http://skarnet.org/software/skalibs/)

## Installation

Hopefully, you can just type `make` and compile `mpd-visualizer`

If you need to customize your compiler, cflags, ldflags, etc
copy `config.mak.dist` to `config.mak` and edit as-needed.

## What happens

When `mpd-visualizer` starts up, it will start reading in audio from the MPD FIFO. As
soon as it has enough audio to generate frames of video, it will start doing so. If your
video FIFO does not exist, it will create it (and automatically delete it when it exits).
If the video FIFO already exists, it uses it, and does NOT delete it when it exits.

It also connects to MPD as a client to poll song metadata.

At startup, it will iterate through your Lua scripts folder and try loading scripts.
Your scripts should return either a Lua function, or a table of functions, like:

```lua
return function()
  print('making video frame')
end
```

Or for the table of functions:

```lua
return {
    onload = function()
      print('loaded!')
    end,
    onreload = function()
      print('reloading!')
    end,
    onframe = function()
      print('making video frame')
    end,
}
```

There's 3 functions that `mpd-visualizer` looks for when you return a table, the only required function is `onframe`.
If you only return a function, it's treated as the `onframe` function.

* `onload()` - this function is called only once, when the script is loaded while `mpd-visualizer` is starting up.
* `onreload()` - whenever `mpd-visualizer` receives a `USR1` signal, it will reload the Lua script and call `onreload()`
* `onframe()` - this function is called every time `mpd-visualizer` wants to make a frame of video.

On every frame, `mpd-visualizer` will calculate a Fast Fourier Transform on the available
audio samples, creating an array of frequencies and amplitudes for Lua. This is useful
for drawing a frequency visualization in your video. It will then call all loaded `onframe` functions
from the loaded Lua scripts.

When it receives a `USR1` signal, it will reload all `Lua` scripts.

`mpd-visualizer` will keep running until either:

* MPD closes the FIFO because it has quit
* `mpd-visualizer` receives a `INT` or `TERM` signal.

# The Lua environment


## Globals

Within your Lua script, you have a few pre-defined global variables:

* `stream` - a table representing the video stream
* `image` - a module for loading image files
* `font` - a module for loading BDF fonts
* `file` - a module for listing files
* `song` - a table of what's playing, from MPD.


### The global `stream` object

The `stream` table has two keys:

* `stream.video` - this represents the current frame of video, it's actually an instance of a `frame` which has more details below
  * `stream.video.framerate` - the video framerate
* `stream.audio` - a table of audio data
  * `stream.audio.samplerate` - audio samplerate, like `48000`
  * `stream.audio.channels` - audio channels, like `2`
  * `stream.audio.samplesize` - sample size in bytes, like `2` for 16-bit audio
  * `stream.audio.freqs` - an array of available frequencies, suitable for making a visualizer
  * `stream.audio.amps` - an array of available amplitudes, suitable for making a visualizer
  * `stream.audio.spectrum_len` - the number of available amplitudes/frequencies

### The global `image` object

The `image` module can load most images, including GIFs. All images have a 2-stage loading process. Initially, it
just probes the image for information like height, width, etc. You can then load the image synchronously or asynchronously.
If you're loading images in the `onload` function (that is, at the very beginning of the program's execution), its safe
to load images synchronously. Otherwise, you should load images asynchronously.

* `img = image.new(filename, width, height, channels)`
  * Either filename is required, or `width/height/channels` if you pass `nil` for the filename
  * If filename is given, this will probe an image file. Returns an image object on success, nil on failure
    * If width, height, or channels is 0 or nil, then the image will not be resized or processed
    * If width or height are set, the image will be resized
    * If channels is set, the image will be forced to use that number of channels
      * Basically, channels = 3 for most bitmaps, channels = 4 for transparent images.
    * The actual image data is NOT loaded, use `img:load()` to load data.
  * If filename is nil, then an empty image is created with the given width/height/channels

Scroll down to "Image Instances" for details on image methods like `img:load()`

### The global `font` object

The `font` object can load BDF (bitmap) fonts.

* `f = font.new(filename)`
  * Loads a BDF font and returns a font object

Scroll down to "Font Instances" for details on font methods

### The global `file` object

The `file` object has a simple method for listing files in a directory:

* `dir = font.ls(path)`
  * Lists files in a directory
  * Returns an array of file objects with two keys:
    * `file` - the actual file path
    * `mtime` - file modification time

### The global `song` object

The `song` object has metadata on the current song. The only guaranteed keys are `file`, `id`, `elapsed`, and `total`. Everything else can be nil.

* `song.file` - the filename of the playing song
* `song.id` - the id of the playing song
* `song.elapsed` - the elapsed time of the current song, in seconds
* `song.total` - the total time of the current song, in seconds
* `song.title` - the title of the current song
* `song.artist` - the artist of the current song
* `song.album` - the album of the current song
* `song.message` - `mpd-visualizer` uses MPD's [client-to-client](https://www.musicpd.org/doc/protocol/client_to_client.html) functionality, It listens on a channel named `visualizer`, if there's a new message on that channel, it will appear here in the song object.

## Image Instances

An image instance has the following methods and properties

* `img.state` - one of `error`, `unloaded`, `loading`, `loaded`, `fixed`
* `img.width` - the image width
* `img.height` - the image height
* `img.channels` - the image channels (3 for RGB, 4 for RGBA)
* `img.frames` - only available after calling `img:load`, an array of one or more frames
* `img.framecount` - only available after calling `img:load`, total number of frames in the `frames` array
* `img.delays` - only available afte calling `img:load` - an array of frame delays (only applicable to gifs)
* `img:load(async)` - loads an image into memory
  * If `async` is true, image is loaded in the background and available on some future iteration of `onframe`
  * else, image is loaded immediately
* `img:unload()` - unloads an image from memory

If `img:load()` fails, either asynchronously or synchronously, then the `state` key will be set to `error`

### Frame instances
Once the image is loaded, it will contain an array of frames. Additionally, `stream.video` is an instance of a `frame`

For convenience, most `frame` functions can be used on the `stream` object directly, instead of `stream.video`, ie,
`stream:get_pixel(x,y)` can be used in place of `stream.video:get_pixel(x,y)`

* `frame.width` - same as `img.width`
* `frame.height` - same as `img.height`
* `frame.channels` - same as `img.channels`
* `frame.state` - all frames are `fixed` images
* `r, g, b, a = frame:get_pixel(x,y)`
  * retrieves the red, green, blue, and alpha values for a given pixel
  * `x,y` starts at `1,1` for the top-left corner of the image
* `frame:set_pixel(x,y,r,g,b,a)` - sets an individual pixel of an image
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `r, g, b` represents the red, green, and blue values, 0 - 255
  * `a` is an optional alpha value, 0 - 255
* `frame:set_pixel_hsl(x,y,r,g,b,a)` - sets a pixel using Hue, Saturation, Lightness
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `h, s, l` represents hue (0-360), saturation (0-100), and lightness (0-100)
  * `a` is an optional alpha value, 0 - 255
* `frame:draw_rectangle(x1,y1,x2,y2,r,g,b,a)` - draws a rectangle from x1,y1 to x2, y2
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `r, g, b` represents the red, green, and blue values, 0 - 255
  * `a` is an optional alpha value, 0 - 255
* `frame:draw_rectangle_hsl(x1,y1,x2,y2,h,s,l,a)` - draws a rectangle from x1,y1 to x2, y2 using hue, saturation, and lightness
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `h, s, l` represents hue (0-360), saturation (0-100), and lightness (0-100)
  * `a` is an optional alpha value, 0 - 255
* `frame:set(frame)`
  * copies a whole frame as-is to the frame
  * the source and destination frame must have the same width, height, and channels values
* `frame:stamp(stamp,x,y,flip,mask,a)`
  * stamps a frame (`stamp`) on top of `frame` at `x,y`
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `flip` is an optional table with the following keys:
    * `hflip` - flip `stamp` horizontally
    * `vflip` - flip `stamp` vertically
  * `mask` is an optional table with the following keys:
    * `left` - mask `stamp`'s pixels left
    * `right` - mask `stamp`'s pixels right
    * `top` - mask `stamp`'s pixels top
    * `bottom` - mask `stamp`'s pixels bottom
  * `a` is an optional alpha value
    * if `stamp is an RGBA image, `a` is only applied for `stamp`'s pixels with >0 alpha
* `frame:blend(f,a)`
  * blends `f` onto `frame`, using `a` as the alpha paramter
* `frame:stamp_string(font,str,scale,x,y,r,g,b,max,lmask,rmask)`
  * renders `str` on top of the `frame`, using `font` (a font object)
  * `scale` controls how many pixels to scroll the font, ie, `1` for the default resolution, `2` for double resolution, etc.
  * `x,y` starts at `1,1` for the top-left corner of the image
  * `r, g, b` represents the red, green, and blue values, 0 - 255
  * `max` is the maximum pixel (width) to render the string at. If the would have gone past this pixel, it is truncated
  * `lmask` - mask the string by this many pixels on the left (before scaling)
  * `rmask` - mask the string by this many pixels on the right (before scaling)
* `frame:stamp_string_hsl(font,str,scale,x,y,h,s,l,max,lmask,rmask)`
  * same as `stamp_string`, but with hue, saturation, and lightness values instead of red, green, and blue
* `frame:stamp_letter(font,codepoint,scale,x,y,r,g,b,lmask,rmask,tmask,bmask)`
  * renders an individual letter
  * the letter is a UTF-8 codepoint, NOT a character. Ie, 'A' is 65
  * lmask specifies pixels to mask on the left (before scaling)
  * rmask specifies pixels to mask on the right (before scaling)
  * tmask specifies pixels to mask on the top (before scaling)
  * bmask specifies pixels to mask on the bottom (before scaling)
* `frame:stamp_letter(font,codepoint,scale,x,y,h,s,l,lmask,rmask,tmask,bmask)`
  * same as `stamp_letter`, but with hue, saturation, and lightness values instead of red, green, blue

## Font instances

Loaded fonts have the following properties/methods:

* `f:pixel(codepoint,x,y)`
  * returns true if the pixel at `x,y` is active
  * codepoint is UTF-8 codepoint, ie, 'A' is 65
* `f:pixeli(codepoint,x,y)`
  * same as `pixel()`, but inverted
* `f:get_string_width(str,scale)`
  * calculates the width of a rendered string
  * scale needs to be 1 or greater
* `f:utf8_to_table(str)`
  * converts a string to a table of UTF-8 codepoints

## Examples


### example: square

Draw a white square in the top-left corner:

```lua
return function()
  stream.video:draw_rectangle(1,1,200,200,255,255,255)
end
```

### example: stamp image

Load an image and stamp it over the video

```lua
-- register a global "img" to use
-- globals can presist across script reloads

img = img or nil

return {
    onload = function()
      img = image.new('something.jpg')
      img:load(false) -- load immediately
    end,
    onframe = function()
      stream.video:stamp_image(img.frames[1],1,1)
    end
}
```

### example: load a background

```lua
-- register a global 'bg' variable
bg = bg or nil

return {
    onload = function()
      bg = image.new('something.jpg',stream.video.width,stream.video.height,stream.video.channels)
      bg:load(false) -- load immediately
      -- image will be resized to fill the video frame
    end,
    onframe = function()
      stream.video:set(bg)
    end
}
```

### example: display song title

```lua
-- register a global 'f' to use for a font
f = f or nil

return {
    onload = function()
      f = font.new('some-font.bdf')
    end,
    onframe = function()
      if song.title then
          stream.video:stamp_string(f,song.title,3,1,1)
          -- places the song title at top-left (1,1), with a 3x scale
      end
    end
}
```

### example: draw visualizer bars

```lua
return {
    onframe = function()
        -- draws visualizer bars
        -- each bar is 10px wide
        -- bar height is between 0 and 90
        for i=1,stream.audio.spectrum_len,1 do
            stream.video:draw_rectangle((i-1)*20, 680 ,10 + (i-1)*20, 680 - (ceil(stream.audio.amps[i])) , 255, 255, 255)
        end

    end
}
```

### example: animate a gif

```lua
local frametime = 1000 / stream.video.framerate
-- frametime is how long each frame of video lasts in milliseconds
-- we'll use this to figure out when to advance to the next
-- frame of the gif

-- register a global 'gif' variable
gif = gif or nil

return {
    onload = function()
      gif = image.new('agif.gif')
      gif:load(false) -- load immediately

      -- initialize the gif with the first frame and frametime
      gif.frameno = 1
      gif.nextframe = gif.delays[gif.frameno]
    end,
    onframe = function()
      stream.video:stamp_image(gif.frames[gif.frameno],1,1)
      gif.nextframe = gif.nextframe - frametime
      if gif.nextframe <= 0 then
          -- advance to the next frame
          gif.frameno = gif.frameno + 1
          if gif.frameno > gif.framecount then
              gif.frameno = 1
          end
          gif.nextframe = gif.delays[gif.frameno]
      end
    end
}
```

## License

Unless otherwise states, all files are released under
an MIT-style license. Details in `LICENSE`

Some exceptions:

* `src/ringbuf.h` and `src/ringbuf.c` - retains their original licensing,
see `LICENSE.ringbuf` for full details.
* `src/tinydir.h` - retains original licensing (simplified BSD), details found
within the file.
* `src/stb_image.h` and `src/stb_image_resize.h` - remains in the public domain
* `src/thread.h` - available under an MIT-style license or Public Domain, see file
for details.

## Known users

* [Game That Tune's 24/7 VGM Stream](https://youtube.com/gamethattune)
