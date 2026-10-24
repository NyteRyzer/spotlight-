import sys
from PIL import Image, ImageDraw, ImageFilter
S = 2048  # supersample, downscale to 512
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
k = S / 512
def sc(*v): return [x * k for x in v]
# background rounded square
bg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(bg).rounded_rectangle(sc(0, 0, 512, 512), radius=112 * k, fill=(20, 20, 23, 255))
img = Image.alpha_composite(img, bg)
# beam: trapezoid with vertical gradient (bright at bottom), masked
beam = Image.new("RGBA", (S, S), (0, 0, 0, 0))
grad = Image.new("RGBA", (S, S))
gd = ImageDraw.Draw(grad)
top, bot = 92 * k, 420 * k
for y in range(S):
    t = min(1, max(0, (y - top) / (bot - top)))
    a = int(40 + 200 * t)
    r, g, b = (255, int(196 + 50 * (1 - t) * 0.3), int(0 + 120 * t * 0.4))
    gd.line([(0, y), (S, y)], fill=(r, g, b, a))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).polygon(sc(206, 92, 306, 92, 420, 420, 92, 420), fill=255)
beam.paste(grad, (0, 0), mask)
beam = beam.filter(ImageFilter.GaussianBlur(3 * k))
img = Image.alpha_composite(img, beam)
d = ImageDraw.Draw(img)
# glow + lamp
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse(sc(256 - 70, 420 - 70, 256 + 70, 420 + 70), fill=(255, 196, 0, 140))
glow = glow.filter(ImageFilter.GaussianBlur(28 * k))
img = Image.alpha_composite(img, glow)
d = ImageDraw.Draw(img)
d.ellipse(sc(256 - 44, 420 - 44, 256 + 44, 420 + 44), fill=(255, 196, 0, 255))
# exclamation mark cut-out (dark)
d.rounded_rectangle(sc(236, 120, 276, 310), radius=20 * k, fill=(20, 20, 23, 255))
d.ellipse(sc(256 - 22, 352 - 22, 256 + 22, 352 + 22), fill=(20, 20, 23, 255))
# clip to the rounded square again
clip = Image.new("L", (S, S), 0)
ImageDraw.Draw(clip).rounded_rectangle(sc(0, 0, 512, 512), radius=112 * k, fill=255)
img.putalpha(Image.composite(img.getchannel("A"), Image.new("L", (S, S), 0), clip))
for size, path in [(512, sys.argv[1])] + [(int(a.split(':')[0]), a.split(':')[1]) for a in sys.argv[2:]]:
    img.resize((size, size), Image.LANCZOS).save(path)
