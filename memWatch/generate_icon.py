#!/usr/bin/env python3
"""generate_icon.py — produce all 10 AppIcon PNGs for memWatch.

Output: replaces the AppIcon.appiconset folder contents with:
    icon_16x16.png      icon_16x16@2x.png
    icon_32x32.png      icon_32x32@2x.png
    icon_128x128.png    icon_128x128@2x.png
    icon_256x256.png    icon_256x256@2x.png
    icon_512x512.png    icon_512x512@2x.png

Each PNG is rendered from the same vector design: a rounded-square
gradient background, a memory-bar glyph in white, and a soft shadow.
The largest (1024x1024) is rendered once and downsampled for the
smaller sizes so the small icons stay sharp.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# ----------------------------------------------------------------------------
# Paths
# ----------------------------------------------------------------------------
HERE = Path(__file__).resolve().parent
ICON_DIR = HERE / "Assets.xcassets" / "AppIcon.appiconset"

# ----------------------------------------------------------------------------
# Design constants — kept in a single block so the icon's mood is easy to tune.
# ----------------------------------------------------------------------------
# Background gradient: deep indigo at the top-left → warm magenta at the
# bottom-right. The exact match to the accent color (#2D6BE0) wasn't used
# because pure blue icons look generic; the magenta tail adds character.
BG_TOP = (45, 109, 224, 255)      # accent indigo
BG_BOTTOM = (192, 64, 192, 255)   # warm magenta

# Glyph color: pure white, slightly transparent at the edges for a softer feel.
GLYPH = (255, 255, 255, 255)

# Corner radius as a fraction of the icon side length. Apple uses ~22.37%.
CORNER_RADIUS_RATIO = 0.2237


# ----------------------------------------------------------------------------
# Rendering
# ----------------------------------------------------------------------------
def lerp_color(a, b, t: float) -> tuple[int, int, int, int]:
    """Linearly interpolate two RGBA colors."""
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(4))


def make_background(size: int) -> Image.Image:
    """Render the rounded-square gradient background."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    grad = Image.new("RGBA", (size, size), BG_TOP)

    # Per-pixel gradient. Cheap enough at 1024x1024.
    for y in range(size):
        t = y / max(1, size - 1)
        row_color = lerp_color(BG_TOP, BG_BOTTOM, t)
        for x in range(size):
            grad.putpixel((x, y), row_color)

    # Rounded mask
    mask = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(mask)
    radius = int(size * CORNER_RADIUS_RATIO)
    d.rounded_rectangle((0, 0, size - 1, size - 1),
                        radius=radius, fill=255)

    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def make_glyph(size: int) -> Image.Image:
    """Render the foreground glyph (memory bars + watch dot) in white.

    The glyph is composed in a fixed 1024-unit design space and then
    scaled to the requested icon size — that way every PNG renders
    from the same source of truth.
    """
    # Design at 1024 then downscale for crisp small icons.
    DESIGN = 1024
    glyph = Image.new("RGBA", (DESIGN, DESIGN), (0, 0, 0, 0))
    d = ImageDraw.Draw(glyph)

    # Memory bars: four vertical bars, increasing in height from left
    # to right, evenly spaced. Heights in design units.
    pad_x = 280       # left/right padding
    pad_y = 360       # top/bottom padding (baseline ~664 from top)
    bar_w = 96
    gap = 32
    heights = [400, 520, 640, 760]  # ascending — reads as "growing usage"

    bar_top_y = 624 - max(heights)  # baseline
    x = pad_x
    for h in heights:
        # Main bar rectangle
        d.rounded_rectangle(
            (x, bar_top_y + (max(heights) - h), x + bar_w, bar_top_y + max(heights)),
            radius=20,
            fill=GLYPH,
        )
        x += bar_w + gap

    # Watch dot — a small white circle in the top-right corner. Reads
    # as a "watch" indicator and gives the icon visual punctuation.
    d.ellipse(
        (820, 160, 940, 280),
        fill=GLYPH,
    )

    # Inner ring on the dot — creates the iris of a watch-eye / lens.
    d.ellipse(
        (855, 195, 905, 245),
        fill=BG_BOTTOM,  # same color as bottom of gradient → reads as hole
    )

    # Downscale to icon size with high-quality resampling so small
    # icons stay smooth.
    return glyph.resize((size, size), Image.LANCZOS)


def make_shadow(size: int, glyph: Image.Image) -> Image.Image:
    """Soft drop-shadow under the glyph, against the same rounded shape.

    We render the shadow at the design size then downscale, so the
    blur radius scales consistently across icon sizes.
    """
    DESIGN = 1024
    base = Image.new("RGBA", (DESIGN, DESIGN), (0, 0, 0, 0))
    base.paste(glyph, (0, 0))
    # Add an offset so the shadow falls down-right
    offset = (0, 24)
    shadow_layer = Image.new("RGBA", (DESIGN, DESIGN), (0, 0, 0, 0))
    shadow_layer.paste(base, offset, base)
    blurred = shadow_layer.filter(ImageFilter.GaussianBlur(radius=18))
    # Make the shadow black at ~35% alpha
    alpha = blurred.split()[3]
    black_shadow = Image.new("RGBA", (DESIGN, DESIGN), (0, 0, 0, 0))
    black_shadow.putalpha(alpha.point(lambda p: int(p * 0.35)))
    return black_shadow.resize((size, size), Image.LANCZOS)


def render_icon(size: int) -> Image.Image:
    bg = make_background(size)
    # Render glyph & shadow at design size, then downscale.
    glyph = make_glyph(1024)
    shadow = make_shadow(size, glyph)
    bg.alpha_composite(shadow)
    bg.alpha_composite(glyph.resize((size, size), Image.LANCZOS))
    return bg


# ----------------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------------
SIZES = [
    ("icon_16x16.png",      16),
    ("icon_16x16@2x.png",   32),
    ("icon_32x32.png",      32),
    ("icon_32x32@2x.png",   64),
    ("icon_128x128.png",   128),
    ("icon_128x128@2x.png",256),
    ("icon_256x256.png",   256),
    ("icon_256x256@2x.png",512),
    ("icon_512x512.png",   512),
    ("icon_512x512@2x.png",1024),
]

CONTENTS_JSON = """{
  "images" : [
    {
      "filename" : "icon_16x16.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "16x16"
    },
    {
      "filename" : "icon_16x16@2x.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "16x16"
    },
    {
      "filename" : "icon_32x32.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "32x32"
    },
    {
      "filename" : "icon_32x32@2x.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "32x32"
    },
    {
      "filename" : "icon_128x128.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "128x128"
    },
    {
      "filename" : "icon_128x128@2x.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "128x128"
    },
    {
      "filename" : "icon_256x256.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "256x256"
    },
    {
      "filename" : "icon_256x256@2x.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "256x256"
    },
    {
      "filename" : "icon_512x512.png",
      "idiom" : "mac",
      "scale" : "1x",
      "size" : "512x512"
    },
    {
      "filename" : "icon_512x512@2x.png",
      "idiom" : "mac",
      "scale" : "2x",
      "size" : "512x512"
    }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
"""


def main() -> int:
    ICON_DIR.mkdir(parents=True, exist_ok=True)

    # Render largest first; smaller ones are produced by down-sampling.
    # (We render each from scratch for clean per-size antialiasing.)
    rendered: dict[int, Image.Image] = {}
    for name, sz in SIZES:
        rendered[sz] = render_icon(sz)

    for name, sz in SIZES:
        out = ICON_DIR / name
        rendered[sz].save(out, "PNG", optimize=True)
        print(f"wrote {out.relative_to(HERE)} ({sz}x{sz})")

    (ICON_DIR / "Contents.json").write_text(CONTENTS_JSON)
    print(f"updated {ICON_DIR.relative_to(HERE)}/Contents.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())