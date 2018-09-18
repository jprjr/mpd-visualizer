local vga
local sin = math.sin
local ceil = math.ceil

local sincounter = -1
local default_y = 30
local wiggleprops = {}

local function wiggle_letters(i, props)
  if i == 1 then
    sincounter = sincounter + 1
    props = {
      x = 10,
    }
  end
  if sincounter == (26) then
    sincounter = 0
  end

  return {
    x = props.x,
    y = default_y + ceil( sin((sincounter / 4) + i - 1) * 10),
    font = vga,
    scale = 3,
    r = 255,
    g = 255,
    b = 255,
  }
end

local function onload()
  vga = font.load('demos/fonts/7x14.bdf')
end

local function onframe()
  stream:stamp_string_adv("Do the wave", wiggle_letters )
end

return {
  onload = onload,
  onframe = onframe,
}
