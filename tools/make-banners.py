import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont
root = sys.argv[1]
F = "/usr/share/fonts/opentype/inter/"
def font(name, size): return ImageFont.truetype(F + name, size)

def banner(W, H, out, shot_name, scale_title=1.0):
    img = Image.new("RGB", (W, H), (14, 14, 17))
    # warm glow top-left, cool glow bottom-right
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    g.ellipse([-W*0.25, -H*0.6, W*0.55, H*0.75], fill=(255, 170, 0, 70))
    g.ellipse([W*0.55, H*0.35, W*1.3, H*1.5], fill=(120, 60, 220, 60))
    glow = glow.filter(ImageFilter.GaussianBlur(W * 0.08))
    img.paste(glow, (0, 0), glow)

    # screenshot card on the right, bleeding off the edge
    shot = Image.open(f"{root}/docs/screenshots/{shot_name}").convert("RGB")
    sw = int(W * 0.60)
    sh = int(shot.height * sw / shot.width)
    shot = shot.resize((sw, sh), Image.LANCZOS)
    sx, sy = int(W * 0.47), int(H * 0.5 - sh * 0.5 + H * 0.06)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([sx + 10, sy + 18, sx + sw + 10, sy + sh + 18], 18, fill=(0, 0, 0, 200))
    shadow = shadow.filter(ImageFilter.GaussianBlur(22))
    img.paste(shadow, (0, 0), shadow)
    mask = Image.new("L", (sw, sh), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, sw, sh], 16, fill=255)
    img.paste(shot, (sx, sy), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([sx, sy, sx + sw, sy + sh], 16, outline=(255, 196, 0), width=2)

    # icon + title + copy on the left
    icon = Image.open(f"{root}/resources/icons/spotlight.png").convert("RGBA")
    isz = int(H * 0.17)
    icon = icon.resize((isz, isz), Image.LANCZOS)
    lx = int(W * 0.055)
    img.paste(icon, (lx, int(H * 0.13)), icon)
    d = ImageDraw.Draw(img)
    tf = font("InterDisplay-Black.otf", int(H * 0.135 * scale_title))
    d.text((lx, int(H * 0.33)), "SPOTLIGHT!", font=tf, fill=(255, 196, 0))
    bf = font("Inter-SemiBold.otf", int(H * 0.052))
    y = int(H * 0.52)
    for line in ["Edit video. Mix audio.", "Design thumbnails.", "One free app."]:
        d.text((lx, y), line, font=bf, fill=(236, 236, 242))
        y += int(H * 0.068)
    sf = font("Inter-Medium.otf", int(H * 0.034))
    d.rounded_rectangle([lx, int(H * 0.79), lx + int(W * 0.33), int(H * 0.79) + int(H * 0.075)], int(H * 0.04),
                        fill=(36, 36, 44))
    d.text((lx + int(H * 0.035), int(H * 0.79) + int(H * 0.017)), "macOS  ·  Linux  ·  Free download", font=sf,
           fill=(200, 200, 210))
    img.save(out, optimize=True)

banner(1280, 640, f"{root}/docs/screenshots/social-preview.png", "video-studio.png")
banner(1600, 700, f"{root}/docs/screenshots/hero.png", "video-studio.png")
