#!/usr/bin/env python3
"""Generate every launcher / store icon from one vector description.

The icons are the game itself in miniature: a blackjack, the ace of spades
over the jack of hearts, drawn the way render.c draws cards (the same corner
radius, index layout, pip curves and colours) on the green felt. Keeping them
generated rather than hand-drawn means the palette can never drift from
src/render.c, and every size is produced from the same geometry. The index
letters use the Nunito font the game embeds.

Outputs (run from the repo root, needs Pillow):
    android/res/mipmap-*/ic_launcher.png            legacy square launcher icon
    android/res/mipmap-*/ic_launcher_foreground.png adaptive-icon foreground
    android/play-assets/icon-512.png                Play store listing icon
    android/play-assets/feature-graphic-1024x500.png Play store feature graphic
    ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png  iOS app icon
    ios/app-store-assets/icon-1024.png              App Store listing icon

    scripts/gen_icons.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

# Palette, copied from the constants at the top of src/render.c.
TABLE = (12, 92, 52, 255)          # FELT: openklondike's green table
TABLE_DARK = (10, 76, 44, 255)     # FELT_DARK
CARD_FACE = (248, 248, 242, 255)
CARD_EDGE = (40, 40, 40, 255)
RED_PIP = (200, 30, 40, 255)
BLACK_PIP = (20, 20, 24, 255)

FONT = "third_party/fonts/nunito/Nunito-SemiBold.ttf"
SS = 4  # supersample factor; every shape is drawn large and downscaled


def heart_outline(cx, cy, s, xsquash, flip, seg=60):
    """render.c's heart curve, height `s`, centred on (cx, cy)."""
    pts = []
    for i in range(seg):
        t = i / seg * 2 * math.pi
        st = math.sin(t)
        pts.append((16 * st ** 3,
                    -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    sc = s / (max(ys) - min(ys))
    mx, my = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    out = []
    for x, y in pts:
        nx, ny = (x - mx) * sc * xsquash, (y - my) * sc
        out.append((cx + nx, cy + (-ny if flip else ny)))
    return out


def stem(d, cx, top, s, col):
    """render.c's flared pedestal under the spade."""
    nw, fw, h = s * 0.05, s * 0.34, s * 0.26
    d.polygon([(cx - nw, top), (cx + nw, top), (cx + nw * 1.4, top + h * 0.55),
               (cx + fw, top + h), (cx - fw, top + h), (cx - nw * 1.4, top + h * 0.55)], fill=col)


def pip(d, cx, cy, s, suit):
    if suit == "hearts":
        d.polygon(heart_outline(cx, cy, s, 0.86, False), fill=RED_PIP)
    else:  # spades
        body, byc = s * 0.74, cy - s * 0.10
        d.polygon(heart_outline(cx, byc, body, 0.96, True), fill=BLACK_PIP)
        stem(d, cx, byc + body * 0.30, s, BLACK_PIP)


def card(w, rank, suit):
    """One face-up card as an RGBA image, laid out as render.c lays it out."""
    h = w * 7 // 5
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], int(w * 0.12), fill=CARD_FACE,
                        outline=CARD_EDGE, width=max(1, int(w * 0.03)))
    col = RED_PIP if suit == "hearts" else BLACK_PIP
    # The corner index is larger than in the game, so it still reads at 48 px.
    fs = int(w * 0.30)
    pad = int(w * 0.08)
    font = ImageFont.truetype(FONT, fs)
    d.text((pad, pad * 0.4), rank, font=font, fill=col)
    pip(d, pad + w * 0.09, pad * 0.4 + fs * 1.25 + w * 0.06, w * 0.17, suit)
    pip(d, w / 2 + w * 0.08, h / 2 + h * 0.10, w * 0.46, suit)
    return img


def compose(size, background, content_scale):
    """The icon at `size` px. `background` is None for the adaptive foreground.

    `content_scale` is the fraction of the canvas the card pair spans, so the
    adaptive foreground can stay inside its 66% safe zone while the legacy
    square icon fills more of its tile.
    """
    n = size * SS
    img = Image.new("RGBA", (n, n), background if background else (0, 0, 0, 0))

    span = int(n * content_scale)
    cw = int(span * 0.56)
    ch = cw * 7 // 5
    cx, cy = n // 2, n // 2

    # The jack sits behind and to the left, tilted the other way, so the pair
    # reads as two cards -- a blackjack -- rather than one at every size.
    back = card(cw, "J", "hearts").rotate(12, resample=Image.BICUBIC, expand=True)
    img.alpha_composite(back, (cx - back.width // 2 - int(cw * 0.30),
                               cy - back.height // 2 - int(ch * 0.04)))
    front = card(cw, "A", "spades").rotate(-8, resample=Image.BICUBIC, expand=True)
    img.alpha_composite(front, (cx - front.width // 2 + int(cw * 0.28),
                                cy - front.height // 2 + int(ch * 0.05)))
    return img.resize((size, size), Image.LANCZOS)


def feature_graphic(w, h):
    """Play's 1024x500 feature graphic: the icon art on a table gradient."""
    img = Image.new("RGBA", (w * 2, h * 2), TABLE)
    d = ImageDraw.Draw(img)
    for y in range(h * 2):  # subtle vertical shade, dark at the bottom
        t = y / (h * 2)
        d.line([0, y, w * 2, y],
               fill=tuple(int(TABLE[i] + (TABLE_DARK[i] - TABLE[i]) * t) for i in range(3)))
    art = compose(h * 2, None, 0.72)
    img.alpha_composite(art, ((w * 2 - art.width) // 2, 0))
    return img.resize((w, h), Image.LANCZOS)


def save(img, path, opaque=False):
    """Write `img`, flattening away the alpha channel when `opaque` is set.

    Apple rejects an app icon that has an alpha channel outright -- it masks the
    corners itself -- so the iOS icons must be flat RGB. The Android adaptive
    foreground is the opposite case and must keep its transparency.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if opaque:
        flat = Image.new("RGB", img.size, TABLE[:3])
        flat.paste(img, mask=img.split()[3])
        img = flat
    img.save(path)
    print("gen_icons: wrote %s (%dx%d %s)" % (path, img.width, img.height, img.mode))


def main():
    # Legacy square launcher icon and the adaptive foreground, per density. The
    # adaptive foreground canvas is 108dp to the legacy 48dp, and its content
    # must stay inside the central 72dp, hence the smaller content scale.
    for suffix, legacy in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                           ("xxhdpi", 144), ("xxxhdpi", 192)):
        d = "android/res/mipmap-%s" % suffix
        save(compose(legacy, TABLE, 0.82), "%s/ic_launcher.png" % d)
        save(compose(legacy * 108 // 48, None, 0.55),
             "%s/ic_launcher_foreground.png" % d)

    save(compose(512, TABLE, 0.76), "android/play-assets/icon-512.png")
    save(feature_graphic(1024, 500), "android/play-assets/feature-graphic-1024x500.png",
         opaque=True)

    ios_icon = compose(1024, TABLE, 0.76)
    save(ios_icon, "ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png", opaque=True)
    save(ios_icon, "ios/app-store-assets/icon-1024.png", opaque=True)


if __name__ == "__main__":
    main()
