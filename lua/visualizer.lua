-- luacheck globals: font
local font = font
local utf8_to_table = font.utf8_to_table

local image_mt = debug.getregistry()["image"]
local image_funcs = image_mt["__index"]

local inspect = require'inspect'

local band, bor, lshift
local ok, bit = pcall(require,'bit')
if ok then
  band, bor, lshift = bit.band, bit.bor, bit.lshift
else
  ok, bit = pcall(require,'bit32')
  if ok then
    band, bor, lshift = bit.band, bit.bor, bit.lshift
  else
    error('Unable to load bit library')
  end
end

local ffi = require("ffi");

local len = string.len
local insert = table.insert
local byte = string.byte
local type = type
local assert = assert
local error = error
local ipairs = ipairs

local ceil = math.ceil
local abs = math.abs

local function hsl_to_rgb(h,s,l)
  -- accepts 0 <= h < 360
  -- accepts 0 <= s <= 100
  -- accepts 0 <= l <= 100

  if l == 100 then
    return 255, 255, 255
  end

  if l == 0 then
    return 0, 0 ,0
  end

  if s == 0 then
    l = ceil(255 * (l / 100) )
    return l, l, l
  end

  while(h >= 360) do
    h = h - 360
  end

  l = l / 100
  s = s / 100

  local c = (1 - abs(2*l - 1)) * s
  local hp = h / 60
  local x = c * (1 - abs((hp % 2) - 1))
  local m = l - (c / 2)
  local r1 = 0
  local g1 = 0
  local b1 = 0

  if hp <= 1 then
    r1 = c
    g1 = x
  elseif hp <= 2 then
    r1 = x
    g1 = c
  elseif hp <= 3 then
    g1 = c
    b1 = x
  elseif hp <= 4 then
    g1 = x
    b1 = c
  elseif hp <= 5 then
    r1 = x
    b1 = c
  elseif hp <= 6 then
    r1 = c
    b1 = x
  end

  return ceil((r1 + m) * 255), ceil((g1 + m) * 255), ceil((b1 + m) * 255)
end


ffi.cdef[[

void *memcpy(void *str1, const void *str2, size_t n);

typedef struct frange {
    double freq;
    double amp;
    unsigned int first_bin;
    unsigned int last_bin;
} frange;

typedef struct song {
    int id;
    int elapsed;
    int duration;
    const char *title;
    const char *album;
    const char *file;
    const char *message;
} song;

typedef struct video_frame {
    unsigned int width;
    unsigned int height;
    unsigned int channels;

    unsigned int image_len;
    uint8_t *image;
    void *userdata;
} video_frame;

typedef struct video_stream {
    unsigned int width;
    unsigned int height;
    unsigned int framerate;

    uint8_t frame_header[8];
    video_frame *frame;
    void *userdata;
} video_stream;

typedef struct lua_audio {
    unsigned int samplerate;
    unsigned int channels;
    unsigned int samplesize;

    unsigned int samples_len;
    uint8_t *samples;
    frange *spectrum;
    unsigned int spectrum_len;
} lua_audio;

typedef struct lua_visualizer {
    song *_song;
    video_stream *video;
    lua_audio *audio;
    void *Lua;
} lua_visualizer;

typedef struct gif_list {
    int framecount;
    video_frame **frames;
    int *delays;
} gif_list;

void video_frame_set_pixel_rgba(
      uint8_t *out,
      unsigned int width,
      unsigned int x,
      unsigned int y,
      uint8_t r,
      uint8_t g,
      uint8_t b,
      uint8_t a);

void video_frame_blend (video_frame *out, video_frame *a, video_frame *b, uint8_t factor);
gif_list *gif_list_from_file(const char *path);
video_frame *video_frame_from_file(unsigned int width, unsigned int height, unsigned int channels,
                           const char *path, void *loadfuncs, void *state);
void video_frame_free(video_frame *);
void gif_list_free(gif_list *);
]]

local misc_funcs = {}
misc_funcs.__index = misc_funcs

function misc_funcs.get_codepoints_width(font,codepoints,scale)
  local w = 0
  for _,c in ipairs(codepoints) do
    if font.widths[c] then
      w = w + (font.widths[c] * scale)
    end
  end
  return w
end

misc_funcs.utf8_to_table = utf8_to_table

function misc_funcs.get_string_width(font,str,scale)
  local codepoints = utf8_to_table(str)
  return misc_funcs.get_codepoints_width(font,codepoints,scale)
end

function misc_funcs.cast_frame(framedata)
  return ffi.cast("video_frame *",framedata)
end


local frame_funcs = {}
frame_funcs.__index = frame_funcs

function frame_funcs:set_pixel_rgb(x,y,r,g,b)
  -- uses 1,1 in the top-left corner
  -- transposes to 0,0 in bottom-left corner
  if x > 0 and y > 0 and x <= self.width and y <= self.height then
    local xy = ((self.height - y) * self.width * 3) + ( (x - 1) * 3)
    self.image[xy] = b;
    self.image[xy + 1] = g;
    self.image[xy + 2] = r;
  end
end

function frame_funcs:set_pixel_rgba(x,y,r,g,b,a)
  if a == 0 then return end
  if a == 255 then return self:set_pixel_rgb(x,y,r,g,b) end
  if x > 0 and y > 0 and  x <= self.width and y <= self.height then
    ffi.C.video_frame_set_pixel_rgba(self.image,self.width,x - 1,self.height - y,r,g,b,a)
  end
end

function frame_funcs:set_pixel_hsl(x,y,h,s,l)
  local r,g,b = hsl_to_rgb(h,s,l)
  self:set_pixel_rgb(x,y,r,g,b)
end

function frame_funcs:set_pixel_hsla(x,y,h,s,l,a)
  local r,g,b = hsl_to_rgb(h,s,l)
  self:set_pixel_rgba(x,y,r,g,b,a)
end

function frame_funcs:stamp_letter_rgb(font,codepoint,scale,x,y,r,g,b,hloffset,hroffset,ytoffset,yboffset)

  if not font.widths[codepoint] then
    codepoint = 32
  end

  hloffset = hloffset or 0 -- "masks" the first pixels of the left of the string
  hroffset = hroffset or 0 -- "masks" the end pixels of the right of the string
  ytoffset = ytoffset or 0
  yboffset = yboffset or 0
  local pixel_hroffset = font.widths[codepoint] * scale - hroffset
  local pixel_yboffset = font.height * scale - yboffset
  local xcc
  local ycc

  for xc=1,font.widths[codepoint],1 do
    for yc=font.height,1,-1 do
      if font:pixel(codepoint,xc,yc) then
        for xi=0,scale-1,1 do
          for yi=0,scale-1,1 do
            xcc = (xc - 1) * scale + xi
            ycc = (yc - 1) * scale + yi
            if xcc >= hloffset and xcc <= pixel_hroffset and
               ycc >= ytoffset and ycc <= pixel_yboffset then
              self:set_pixel_rgb(x+xcc,y+((yc - 1) * scale)+yi,r,g,b)
            end
          end
        end
      end
    end
  end

  return font.widths[codepoint] * scale
end

function frame_funcs:stamp_letter_hsl(font,codepoint,scale,x,y,h,s,l,hloffset,hroffset)
  local r, g, b = hsl_to_rgb(h,s,l)
  return self:stamp_letter_rgb(font,codepoint,scale,x,y,r,g,b,hloffset,hroffset)
end

function frame_funcs.get_string_width(font,codepoints,scale)
  return misc_funcs.get_string_width(font,codepoints,scale)
end

function frame_funcs:stamp_string_rgb(font,str,scale,x,y,r,g,b,max,lmask)
  local lmask_applied = false
  if not lmask then
    lmask_applied = true
  end
  local rmask = 0

  rmask = rmask or 0

  local codepoints = utf8_to_table(str)
  local xi = x
  for _,codepoint in ipairs(codepoints) do
    if not font.widths[codepoint] then
      codepoint = 32
    end
    if lmask and lmask >= (font.widths[codepoint] * scale) then
      lmask = lmask - (font.widths[codepoint] * scale)
      xi = xi + (font.widths[codepoint] * scale)
    else
      if max and  xi + (font.widths[codepoint] * scale) > max then
        rmask =  (font.widths[codepoint] * scale) - (max - xi)
      end
      xi = xi + self:stamp_letter(font,codepoint,scale,xi,y,r,g,b,
        lmask_applied == false and lmask or 0,
        rmask)
      if lmask_applied == false then
        lmask_applied = true
      end
    end
  end
end

function frame_funcs:stamp_string_hsl(font,str,scale,x,y,h,s,l,max,lmask,rmask)
  local r, g, b = hsl_to_rgb(h, s, l)
  self:stamp_string_rgb(font,str,scale,x,y,r,g,b,max,lmask,rmask)
end

function frame_funcs:draw_rectangle_rgb(x1,y1,x2,y2,r,g,b)
  if x1 <= x2 then
    for x=x1,x2,1 do
      if y1 <= y2 then
        for y=y1,y2,1 do
          self:set_pixel_rgb(x,y,r,g,b)
        end
      else
        for y=y2,y1,1 do
          self:set_pixel_rgb(x,y,r,g,b)
        end
      end
    end
  else -- x1 > x2
    for x=x2,x1,1 do
      if y1 <= y2 then
        for y=y1,y2,1 do
          self:set_pixel_rgb(x,y,r,g,b)
        end
      else  -- y1 > y2
        for y=y2,y1,1 do
          self:set_pixel_rgb(x,y,r,g,b)
        end
      end
    end
  end
end

function frame_funcs:draw_rectangle_rgba(x1,y1,x2,y2,r,g,b,a)
  if x1 <= x2 then
    for x=x1,x2,1 do
      if y1 <= y2 then
        for y=y1,y2,1 do
          self:set_pixel_rgba(x,y,r,g,b,a)
        end
      else
        for y=y2,y1,1 do
          self:set_pixel_rgba(x,y,r,g,b,a)
        end
      end
    end
  else -- x1 > x2
    for x=x2,x1,1 do
      if y1 <= y2 then
        for y=y1,y2,1 do
          self:set_pixel_rgba(x,y,r,g,b,a)
        end
      else  -- y1 > y2
        for y=y2,y1,1 do
          self:set_pixel_rgba(x,y,r,g,b,a)
        end
      end
    end
  end
end

function frame_funcs:set_frame(a,b,alpha)
  if not a then return end
  ffi.C.memcpy(self.image,a.image,a.image_len)
  -- if b then
  --   image_funcs.stamp_image(self,b,1,1,{},{},alpha)
  --   -- ffi.C.video_frame_blend(self,a,b,alpha)
  -- end
end

function frame_funcs:get_pixel(x,y)
  if y > self.height or x > self.width then return nil end
  x = x - 1
  y = self.height - y
  local r,g,b,a
  local byt = (y * self.width * self.channels) + (x * self.channels)
  if self.channels == 4 then
    b = self.image[byt]
    g = self.image[byt+1]
    r = self.image[byt+2]
    a = self.image[byt+3]
  elseif self.channels == 3 then
    b = self.image[byt]
    g = self.image[byt+1]
    r = self.image[byt+2]
  end
  return r, g, b, a
end

-- expects
-- f: a video_frame object, required
-- x: left x to stamp the image (can go off-screen), required
-- y: top y to stamp the image (can go off-screen), required,
-- flip: table with hflip and vflip parameters, optional
-- mask: table the left,right,top,bottom mask parameters, optional
-- alpha: override alpha of image, optional
--   -- if a pixel's alpha value is already 00, it keeps the 00
function frame_funcs:stamp_image(f,x,y,flip,mask,alpha)
  local xi = 1;
  local yi = 1;
  local xm = f.width
  local ym = f.height
  local xoffset = 0
  local yoffset = 0
  local startx = x - 1
  local starty = y - 1
  flip = flip or {}
  mask = mask or {}

  if startx < 0 then
    xi = xi + (-1 * x) + 1
    xoffset = xi
    startx  = 0
  end

  if starty < 0 then
    yi = yi + (-1 * y) + 1
    yoffset = yi
    starty = 0
  end

  if mask.left then
    xi = xi + mask.left
  end
  if mask.right then
    xm = xm - mask.right
  end
  if mask.top then
    yi = yi + mask.top
  end
  if mask.bottom then
    ym = ym - mask.bottom
  end

  for yii=yi, ym, 1 do
    for xii=xi, xm, 1 do
      local xt, yt = xii, yii

      if flip.hflip then
        xt = f.width - xii + 1
      end

      if flip.vflip then
        yt = f.height - yii + 1
      end
      local r, g, b, a = f:get_pixel(xt,yt)

      if r and a then
        if alpha and a > 0 then
          a = alpha
        end
        self:set_pixel_rgba((startx + xii - xoffset), (starty + yii - yoffset),r,g,b,a)
      elseif r then
        if alpha then
          self:set_pixel_rgba((startx + xii - xoffset), (starty + yii - yoffset),r,g,b,alpha)
        else
          self:set_pixel_rgb((startx + xii - xoffset), (starty + yii - yoffset),r,g,b)
        end
      end
    end
  end
end

frame_funcs.stamp_letter = frame_funcs.stamp_letter_rgb
frame_funcs.stamp_string = frame_funcs.stamp_string_rgb
frame_funcs.hsl_to_rgb = hsl_to_rgb
frame_funcs.get_string_width = misc_funcs.get_string_width
frame_funcs.utf8_to_table = utf8_to_table
frame_funcs.cast_frame = misc_funcs.cast_frame

local stream_funcs = {}
stream_funcs.__index = stream_funcs

function stream_funcs.load_image(path,width,height,channels)
  width = width or 0
  height = height or 0
  channels = channels or 0
  local frame = ffi.C.video_frame_from_file(width,height,channels,path,nil,nil)
  if frame == ffi.NULL then return nil end
  return ffi.gc(frame,ffi.C.video_frame_free)
end

function stream_funcs.load_gif(path)
  local framelist = ffi.C.gif_list_from_file(path)
  if framelist == ffi.NULL then return nil end
  return ffi.gc(framelist,ffi.C.gif_list_free)
end

function stream_funcs:song_title()
  if self._song.title == ffi.NULL then return nil end
  return ffi.string(self._song.title)
end

function stream_funcs:song_album()
  if self._song.album == ffi.NULL then return nil end
  return ffi.string(self._song.album)
end

function stream_funcs:song_file()
  if self._song.file == ffi.NULL then return nil end
  return ffi.string(self._song.file)
end

function stream_funcs:song_message()
  if self._song.message == ffi.NULL then return nil end
  return ffi.string(self._song.message)
end

function stream_funcs:song_elapsed()
  return self._song.elapsed
end
function stream_funcs:song_duration()
  return self._song.duration
end
function stream_funcs:song_id()
  return self._song.id
end

function stream_funcs:set_pixel_rgb(x,y,r,g,b)
  return frame_funcs.set_pixel_rgb(self.video.frame,x,y,r,g,b)
end

function stream_funcs:set_pixel_rgba(x,y,r,g,b,a)
  return frame_funcs.set_pixel_rgba(self.video.frame,x,y,r,g,b,a)
end

function stream_funcs:set_pixel_hsl(x,y,h,s,l)
  return frame_funcs.set_pixel_hsl(self.video.frame,x,y,h,s,l)
end

function stream_funcs:set_pixel_hsla(x,y,h,s,l,a)
  return frame_funcs.set_pixel_hsla(self.video.frame,x,y,h,s,l,a)
end

function stream_funcs:stamp_letter_rgb(font,codepoint,scale,x,y,r,g,b,hloffset,hroffset)
  return frame_funcs.stamp_letter_rgb(self.video.frame,font,codepoint,scale,x,y,r,g,b,hloffset,hroffset)
end

function stream_funcs:stamp_letter_hsl(font,codepoint,scale,x,y,h,s,l,hloffset,hroffset)
  return frame_funcs.stamp_letter_hsl(self.video.frame,font,codepoint,scale,x,y,h,s,l,hloffset,hroffset)
end

function stream_funcs.get_string_width(font,codepoints,scale)
  return misc_funcs.get_string_width(font,codepoints,scale)
end

function stream_funcs:stamp_string_rgb(font,str,scale,x,y,r,g,b,max,lmask,rmask)
  return frame_funcs.stamp_string_rgb(self.video.frame,font,str,scale,x,y,r,g,b,max,lmask,rmask)
end

function stream_funcs:stamp_string_hsl(font,str,scale,x,y,h,s,l,max,lmask,rmask)
  return frame_funcs.stamp_string_hsl(self.video.frame,font,str,scale,x,y,h,s,l,max,lmask,rmask)
end

function stream_funcs:draw_rectangle_rgb(x1,y1,x2,y2,r,g,b)
  return frame_funcs.draw_rectangle_rgb(self.video.frame,x1,y1,x2,y2,r,g,b)
end

function stream_funcs:draw_rectangle_rgba(x1,y1,x2,y2,r,g,b,a)
  return frame_funcs.draw_rectangle_rgba(self.video.frame,x1,y1,x2,y2,r,g,b,a)
end

function stream_funcs:set_frame(a,b,alpha)
  return frame_funcs.set_frame(self.video.frame,a,b,alpha)
end

function stream_funcs:stamp_image(f,x,y,flip,mask,alpha)
  return frame_funcs.stamp_image(self.video.frame,f,x,y,flip,mask,alpha)
end

stream_funcs.stamp_letter = stream_funcs.stamp_letter_rgb
stream_funcs.stamp_string = stream_funcs.stamp_string_rgb
stream_funcs.hsl_to_rgb = hsl_to_rgb

stream_funcs.get_string_width = misc_funcs.get_string_width
stream_funcs.utf8_to_table = utf8_to_table
stream_funcs.cast_frame = misc_funcs.cast_frame

ffi.metatype("video_frame",frame_funcs)
ffi.metatype("lua_visualizer",stream_funcs)


-- function called by app to create the video_frame object
return function(lua_vis)
    local vis = ffi.cast("lua_visualizer *",lua_vis)
    return vis
end

