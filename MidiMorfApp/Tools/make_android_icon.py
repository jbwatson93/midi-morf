"""Builds the Android launcher icon from the iOS 1024 px artwork.

Android masks a launcher icon to its own shape (circle, squircle...) and shows
only the middle 72 of 108 dp, so the full-bleed iOS art would lose its corner
lettering. This puts the art in the middle with a feathered edge, over a plain
velvet background, so every mask shape gets the whole logo.

Run from MidiMorfApp/:  python3 Tools/make_android_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SOURCE = Path("Resources/iOS/Images.xcassets/AppIcon.appiconset/Icon-AppStore-1024.png")
RES = Path("Resources/Android/res")

CANVAS_DP = 108
ART_DP = 60        # fits a 72 dp circle mask with the lettering intact
FEATHER = 0.035    # fraction of the art's width that fades into the background
CENTRE = (58, 8, 12)   # velvet colours sampled from the art
EDGE = (12, 1, 2)
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}

ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@mipmap/ic_launcher_background"/>
    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>
</adaptive-icon>
"""


def layers(size):
    art = Image.open(SOURCE).convert("RGBA")

    # Background: the velvet's own colours, lit in the middle like the art is.
    glow = Image.radial_gradient("L").resize((size, size), Image.LANCZOS)
    background = Image.composite(Image.new("RGB", (size, size), EDGE),
                                 Image.new("RGB", (size, size), CENTRE), glow)

    art_px = round(size * ART_DP / CANVAS_DP)
    art = art.resize((art_px, art_px), Image.LANCZOS)

    fade = max(1, round(art_px * FEATHER))
    mask = Image.new("L", (art_px, art_px), 0)
    ImageDraw.Draw(mask).rounded_rectangle((fade, fade, art_px - fade, art_px - fade),
                                           radius=fade * 2, fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(fade / 2))
    art.putalpha(mask)

    foreground = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    offset = (size - art_px) // 2
    foreground.paste(art, (offset, offset), art)
    return background, foreground


def main():
    for name, scale in DENSITIES.items():
        folder = RES / f"mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        background, foreground = layers(round(CANVAS_DP * scale))
        background.save(folder / "ic_launcher_background.png", optimize=True)
        foreground.save(folder / "ic_launcher_foreground.png", optimize=True)

    folder = RES / "mipmap-anydpi-v26"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "ic_launcher.xml").write_text(ADAPTIVE_XML)


if __name__ == "__main__":
    main()
