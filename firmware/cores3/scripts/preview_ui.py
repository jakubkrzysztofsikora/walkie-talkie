#!/usr/bin/env python3
"""
Render offline previews of the CoreS3 walkie-talkie UI.

Produces 320x240 PNGs for key states using the same sprite definitions as the
firmware generator. Useful for design review without flashing hardware.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

# Import the sprite art definitions from the generator.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_sprites import PALETTE, get_all_frames, get_themes

SCREEN_W, SCREEN_H = 320, 240

# Convert 16-bit RGB565 to (r, g, b).
def rgb565(c: int) -> tuple[int, int, int]:
    r = ((c >> 11) & 0x1F) << 3
    g = ((c >> 5) & 0x3F) << 2
    b = (c & 0x1F) << 3
    return (r, g, b)


# Semantic palette mapping similar to resolve_palette_color().
def resolve_color(idx: int, theme: tuple) -> tuple[int, int, int]:
    if idx == 0:
        return None  # transparent
    if idx == 1:
        return (0, 0, 0)
    if idx == 2:
        return (255, 255, 255)
    if idx == 3:
        return (255, 208, 160)  # skin
    if idx == 4:
        return rgb565(theme[1])  # accent
    if idx == 5:
        return rgb565(theme[2])  # accent_dark
    if idx == 6:
        return rgb565(theme[3])  # secondary
    if idx == 7:
        return rgb565(theme[4])  # secondary_dark
    if idx == 8:
        return rgb565(theme[5])  # background
    if idx == 9:
        return rgb565(theme[6])  # glow
    return (0, 0, 0)


def draw_sprite(img: Image.Image, draw: ImageDraw.ImageDraw,
                cx: int, cy: int, frame: list[str], theme: tuple, scale: int) -> None:
    w, h = 16 * scale, 16 * scale
    x0, y0 = cx - w // 2, cy - h // 2
    for y, row in enumerate(frame):
        for x, ch in enumerate(row):
            idx = PALETTE[ch]
            color = resolve_color(idx, theme)
            if color is None:
                continue
            draw.rectangle(
                [x0 + x * scale, y0 + y * scale,
                 x0 + (x + 1) * scale - 1, y0 + (y + 1) * scale - 1],
                fill=color,
            )


def draw_background(draw: ImageDraw.ImageDraw, theme: tuple, frame: int) -> None:
    bg = resolve_color(8, theme)
    draw.rectangle([0, 0, SCREEN_W, SCREEN_H], fill=bg)

    # Stars
    star_c = resolve_color(9, theme)
    for i in range(24):
        x = ((i * 53) + frame) % SCREEN_W
        y = 10 + ((i * 17) % 90)
        draw.rectangle([x, y, x + 2, y + 2], fill=star_c)

    # Hills
    hill_c = (max(0, bg[0] - 30), max(0, bg[1] - 30), max(0, bg[2] - 30))
    draw.rectangle([0, 150, SCREEN_W, SCREEN_H], fill=hill_c)
    for x in range(SCREEN_W):
        dx = (x + frame * 2) % 120
        hh = dx // 4 if dx < 60 else (120 - dx) // 4
        draw.line([(x, 150 - hh), (x, 150)], fill=hill_c)

    # Ground blocks
    ground_c = (max(0, bg[0] - 20), max(0, bg[1] - 20), max(0, bg[2] - 20))
    draw.rectangle([0, 190, SCREEN_W, SCREEN_H], fill=ground_c)
    for x in range(-16, SCREEN_W + 16, 32):
        gx = (x - (frame * 4) % 32)
        gh = 6 + ((x // 32) % 3) * 6
        draw.rectangle([gx, 190 - gh, gx + 24, 190 + 4], fill=ground_c, outline=star_c)


def draw_status_bar(draw: ImageDraw.ImageDraw, battery_pct: int, wifi_on: bool, theme: tuple) -> None:
    x, y = 6, 6
    # Battery
    draw.rectangle([x, y, x + 18, y + 10], outline=(255, 255, 255))
    draw.rectangle([x + 18, y + 2, x + 21, y + 8], fill=(255, 255, 255))
    fill = min(14, battery_pct * 14 // 100)
    color = (0, 255, 0) if battery_pct > 50 else (255, 255, 0) if battery_pct > 20 else (255, 0, 0)
    draw.rectangle([x + 2, y + 2, x + 2 + fill, y + 8], fill=color)

    # WiFi
    wx = x + 26
    if wifi_on:
        draw.ellipse([wx + 4, y + 6, wx + 8, y + 10], fill=(0, 255, 0))
        draw.arc([wx, y, wx + 12, y + 12], 225, 315, fill=(0, 255, 0), width=2)
        draw.arc([wx - 3, y - 3, wx + 15, y + 15], 225, 315, fill=(0, 255, 0), width=2)
    else:
        draw.line([(wx, y), (wx + 12, y + 10)], fill=(255, 0, 0), width=2)
        draw.line([(wx + 12, y), (wx, y + 10)], fill=(255, 0, 0), width=2)


def draw_ptt(draw: ImageDraw.ImageDraw, cx: int, cy: int, pressed: bool, theme: tuple) -> None:
    r = 28 if pressed else 24
    ring = (255, 0, 0) if pressed else resolve_color(4, theme)
    fill = (80, 0, 0) if pressed else resolve_color(8, theme)
    draw.ellipse([cx - r - 4, cy - r - 4, cx + r + 4, cy + r + 4], fill=ring)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    # Mic icon
    mx, my = cx - 3, cy - 8
    mic_color = (255, 255, 255) if pressed else ring
    draw.rounded_rectangle([mx, my, mx + 6, my + 10], radius=2, fill=mic_color)
    draw.rectangle([mx + 1, my + 10, mx + 5, my + 13], fill=mic_color)
    draw.rectangle([mx + 2, my + 13, mx + 4, my + 15], fill=mic_color)


def draw_waveform(draw: ImageDraw.ImageDraw, cx: int, y: int, frame: int, theme: tuple) -> None:
    color = resolve_color(4, theme)
    for i in range(9):
        h = 2 + ((frame + i * 3) % 7) * 2
        x = cx - 40 + i * 10
        draw.rectangle([x, y - h, x + 6, y + h], fill=color)


def draw_main_screen(char_idx: int, state: str, frame: int) -> Image.Image:
    themes = get_themes()
    char_id = themes[char_idx][0]
    theme = themes[char_idx]
    frames = get_all_frames()[char_id]

    img = Image.new("RGB", (SCREEN_W, SCREEN_H))
    draw = ImageDraw.Draw(img)

    draw_background(draw, theme, frame)
    draw_status_bar(draw, 75, True, theme)

    expr_idx = {"idle": 0, "listen": 1, "think": 2, "speak": 3}[state]
    # Bounce offset
    tick = frame
    bounce_frame = tick % 12
    if bounce_frame < 3:
        off = -bounce_frame
    elif bounce_frame < 6:
        off = -(6 - bounce_frame)
    elif bounce_frame < 9:
        off = bounce_frame - 6
    else:
        off = 12 - bounce_frame

    mx, my = 160, 95 + off
    draw.ellipse([mx - 58, my - 58, mx + 58, my + 58], outline=resolve_color(9, theme), width=2)
    draw_sprite(img, draw, mx, my, frames[expr_idx], theme, 4)

    # Carousel dots
    cy = 175
    for i in (-1, 0, 1):
        idx = (char_idx + i) % len(themes)
        r = 8 if i == 0 else 5
        c = resolve_color(4, theme) if i == 0 else (100, 100, 100)
        draw.ellipse([mx + i * 28 - r, cy - r, mx + i * 28 + r, cy + r], fill=c)
        if i == 0:
            bg = resolve_color(8, theme)
            draw.ellipse([mx - r + 2, cy - r + 2, mx + r - 2, cy + r - 2], fill=bg)

    draw.polygon([(mx - 18, cy), (mx - 10, cy - 4), (mx - 10, cy + 4)], fill=(100, 100, 100))
    draw.polygon([(mx + 18, cy), (mx + 10, cy - 4), (mx + 10, cy + 4)], fill=(100, 100, 100))

    if state == "speak":
        draw_waveform(draw, mx, 203, frame, theme)
    draw_ptt(draw, mx, 215, state == "listen", theme)

    return img


def draw_menu(char_idx: int) -> Image.Image:
    themes = get_themes()
    cur_theme = themes[char_idx]
    frames = get_all_frames()

    img = Image.new("RGB", (SCREEN_W, SCREEN_H))
    draw = ImageDraw.Draw(img)
    draw.rectangle([0, 0, SCREEN_W, SCREEN_H], fill=(0, 0, 0))
    banner = resolve_color(4, cur_theme)
    draw.rectangle([0, 0, SCREEN_W, 60], fill=banner)
    draw.text((110, 22), "PICK HERO", fill=(255, 255, 255))

    cols, rows = 6, 2
    cell_w = SCREEN_W // cols
    cell_h = (SCREEN_H - 40) // rows
    y0 = 80
    for i, t in enumerate(themes):
        col = i % cols
        row = i // cols
        x = col * cell_w + cell_w // 2
        y = y0 + row * cell_h + cell_h // 2
        r = 26
        if i == char_idx:
            draw.ellipse([x - r - 4, y - r - 4, x + r + 4, y + r + 4], fill=(255, 255, 255))
        draw.ellipse([x - r, y - r, x + r, y + r], fill=resolve_color(4, t))
        draw_sprite(img, draw, x, y, frames[t[0]][0], t, 2)

    return img


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    out_dir = script_dir.parent / "docs" / "ui_previews"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Render a few representative states for the default character (radek).
    for state in ("idle", "listen", "think", "speak"):
        img = draw_main_screen(0, state, frame=5 if state == "speak" else 0)
        img.save(out_dir / f"radek_{state}.png")
        print(f"Saved {out_dir / f'radek_{state}.png'}")

    # Render menu.
    menu_img = draw_menu(0)
    menu_img.save(out_dir / "menu.png")
    print(f"Saved {out_dir / 'menu.png'}")

    # Render each character in idle state.
    for idx, theme in enumerate(get_themes()):
        img = draw_main_screen(idx, "idle", frame=0)
        img.save(out_dir / f"{theme[0]}_idle.png")
        print(f"Saved {out_dir / f'{theme[0]}_idle.png'}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
