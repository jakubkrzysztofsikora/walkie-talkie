#!/usr/bin/env python3
"""
Generate C sprite data for the CoreS3 walkie-talkie UI from 64x64 RGBA PNGs.

This is the RGB565 successor to generate_sprites.py (which produced 16x16
indexed-palette bytes). The data model is now full-colour:

    assets/sprites/<char>_<expr>.png   (64x64 RGBA, alpha = opacity)
        -->  const uint16_t <char>_<expr>[64*64]          (RGB565, LE-native)
        -->  const uint8_t  <char>_<expr>_mask[(64*64+7)/8] (1bpp, bit=1 opaque)

RGB565 packing: ((r>>3)<<11) | ((g>>2)<<5) | (b>>3).
A pixel is opaque (mask bit = 1) when PNG alpha >= 128.

Expressions: idle, listen, think, speak.
Characters (roster order, matches lib k_themes / sprites.h NCHARS=11):
    radek, steve, simba, ryder, creeper, pimpek,
    crewmate, sonic, pikachu, mario, roblox_noob

The per-character THEME metadata (accent/bg/glow + id) is preserved verbatim
from the original generator because the UI chrome (background, carousel,
status bar, menu banner) still needs get_character_theme(). Only the FRAME art
changed to RGB565 + mask.

=====================================================================
 ASSET DRIFT GATE
=====================================================================
 lib/walkie_ui_logic/walkie_ui_logic/sprites.cpp is GENERATED from
 assets/sprites/*.png by this script. It MUST be regenerated whenever the
 PNGs change. Commit both the PNGs and the regenerated sprites.cpp together.
 To verify no drift:
     python3 scripts/generate_sprites_png.py
     git diff --exit-code lib/walkie_ui_logic/walkie_ui_logic/sprites.cpp
 (see the `make sprites` / drift-check note in scripts/README or Makefile)
=====================================================================

The PNGs shipped here are PLACEHOLDER art (a themed face per character +
per-expression mouth) so the firmware compiles and renders. Real character
likenesses are an out-of-scope human/art-tool deliverable; drop replacement
64x64 RGBA PNGs into assets/sprites/ and re-run this script.

Run locally (Pillow required):  python3 scripts/generate_sprites_png.py
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List, Tuple

from PIL import Image, ImageDraw

SPRITE_W = 64
SPRITE_H = 64
EXPRESSIONS = ("idle", "listen", "think", "speak")

# RGB565 colours for each character theme (id, accent, accent_dark,
# secondary, secondary_dark, background, glow). Carried over unchanged from
# generate_sprites.py — the UI chrome still consumes these.
THEMES: List[Tuple[str, int, int, int, int, int, int]] = [
    ("radek", 0x07FF, 0x047F, 0xFCE0, 0xC600, 0x10E2, 0x3DEF),
    ("steve", 0x9E33, 0x5106, 0x03E0, 0x0200, 0x30A2, 0x6EDD),
    ("simba", 0xFC60, 0xC400, 0xF986, 0xC000, 0x30A2, 0xFE66),
    ("ryder", 0xF800, 0xA000, 0x03FF, 0x001F, 0x10E2, 0xF999),
    ("creeper", 0x07E0, 0x0200, 0x0000, 0x0000, 0x0200, 0x27E2),
    ("pimpek", 0xF986, 0xC000, 0xFFE0, 0xC600, 0x30A2, 0xFE19),
    ("crewmate", 0xF800, 0xA000, 0x39FF, 0x0010, 0x18C3, 0xF999),
    ("sonic", 0x04BF, 0x0010, 0xF986, 0xC000, 0x10E2, 0x3DDF),
    ("pikachu", 0xFFE0, 0xC600, 0xF800, 0xA000, 0x30A2, 0xFF80),
    ("mario", 0xF800, 0xA000, 0x03E0, 0x0200, 0x10E2, 0xF999),
    ("roblox_noob", 0xFFE0, 0xC600, 0x03FF, 0x001F, 0x30A2, 0xFF80),
]


# ---------------------------------------------------------------------------
# RGB565 <-> RGB888 helpers
# ---------------------------------------------------------------------------
def rgb565_pack(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_to_rgb(c: int) -> Tuple[int, int, int]:
    r = ((c >> 11) & 0x1F) << 3
    g = ((c >> 5) & 0x3F) << 2
    b = (c & 0x1F) << 3
    return (r, g, b)


# ---------------------------------------------------------------------------
# Placeholder art generation
# ---------------------------------------------------------------------------
def _shade(rgb: Tuple[int, int, int], factor: float) -> Tuple[int, int, int]:
    return tuple(max(0, min(255, int(v * factor))) for v in rgb)


def make_placeholder(theme: Tuple, expr: str) -> Image.Image:
    """Build a distinct 64x64 RGBA placeholder face for (character, expression).

    Each character gets a rounded-square face in its theme accent colour, simple
    eyes, and a per-expression mouth, on a fully transparent background. This is
    deliberately schematic art — real likenesses replace the PNGs later.
    """
    accent = rgb565_to_rgb(theme[1])
    accent_dark = rgb565_to_rgb(theme[2])
    img = Image.new("RGBA", (SPRITE_W, SPRITE_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Rounded-square head, leaving a transparent border so the alpha mask is
    # meaningful (not a full opaque square).
    d.rounded_rectangle([5, 5, 58, 58], radius=14, fill=accent + (255,),
                        outline=accent_dark + (255,), width=3)
    # A lighter top highlight band for a touch of shading.
    d.rounded_rectangle([10, 10, 53, 26], radius=10, fill=_shade(accent, 1.18) + (255,))

    # Eyes (whites + pupils). "think" looks up-left.
    white = (255, 255, 255, 255)
    black = (20, 20, 20, 255)
    ex_l, ex_r, ey = 21, 43, 30
    for ex in (ex_l, ex_r):
        d.ellipse([ex - 7, ey - 7, ex + 7, ey + 7], fill=white, outline=black, width=1)
    if expr == "think":
        pdx, pdy = -2, -2
    else:
        pdx, pdy = 0, 0
    for ex in (ex_l, ex_r):
        d.ellipse([ex - 3 + pdx, ey - 3 + pdy, ex + 3 + pdx, ey + 3 + pdy], fill=black)

    # Mouth per expression.
    mx, my = 32, 46
    if expr == "idle":      # smile
        d.arc([mx - 12, my - 8, mx + 12, my + 8], start=20, end=160, fill=black, width=3)
    elif expr == "listen":  # small "o"
        d.ellipse([mx - 4, my - 4, mx + 4, my + 4], outline=black, width=3)
    elif expr == "think":   # flat line
        d.line([mx - 9, my, mx + 9, my], fill=black, width=3)
    elif expr == "speak":   # open mouth
        d.ellipse([mx - 9, my - 7, mx + 9, my + 9], fill=(60, 20, 20, 255), outline=black, width=2)
        d.ellipse([mx - 4, my, mx + 4, my + 7], fill=(200, 80, 90, 255))

    return img


def write_placeholders(assets_dir: Path) -> int:
    """(Re)write any missing placeholder PNGs. Existing PNGs are NOT overwritten,
    so hand-authored / real art is preserved across re-runs."""
    assets_dir.mkdir(parents=True, exist_ok=True)
    written = 0
    for theme in THEMES:
        char_id = theme[0]
        for expr in EXPRESSIONS:
            path = assets_dir / f"{char_id}_{expr}.png"
            if path.exists():
                continue
            make_placeholder(theme, expr).save(path)
            written += 1
    return written


# ---------------------------------------------------------------------------
# PNG -> RGB565 + mask
# ---------------------------------------------------------------------------
def load_frame(path: Path) -> Tuple[List[int], bytes]:
    """Return (rgb565[64*64], mask[(64*64+7)//8]) for one PNG.

    Real art MUST be authored at native 64x64 RGBA. A wrong size or non-RGBA
    source is an authoring error: we ERROR rather than silently NEAREST-resize
    or mode-convert, because that would mask broken/mis-exported art that looks
    wrong on the device.
    """
    img = Image.open(path)
    if img.mode != "RGBA":
        raise SystemExit(
            f"ERROR: {path} is mode {img.mode!r}, expected 'RGBA' "
            f"(author 64x64 RGBA with a real alpha channel)."
        )
    if img.size != (SPRITE_W, SPRITE_H):
        raise SystemExit(
            f"ERROR: {path} is {img.size[0]}x{img.size[1]}, expected "
            f"{SPRITE_W}x{SPRITE_H}. Author art at native 64x64 (no resize)."
        )
    px = img.load()
    rgb565: List[int] = []
    mask = bytearray((SPRITE_W * SPRITE_H + 7) // 8)
    for y in range(SPRITE_H):
        for x in range(SPRITE_W):
            r, g, b, a = px[x, y]
            rgb565.append(rgb565_pack(r, g, b))
            if a >= 128:
                i = y * SPRITE_W + x
                mask[i >> 3] |= (1 << (i & 7))
    return rgb565, bytes(mask)


# ---------------------------------------------------------------------------
# C++ emission
# ---------------------------------------------------------------------------
def c_frame_name(char_id: str, expr: str) -> str:
    return f"k_{char_id}_{expr}"


def c_mask_name(char_id: str, expr: str) -> str:
    return f"k_{char_id}_{expr}_mask"


def emit_cpp(themes: List[Tuple],
             frames: Dict[str, Dict[str, Tuple[List[int], bytes]]],
             out: Path) -> None:
    L: List[str] = [
        '// Auto-generated by scripts/generate_sprites_png.py — do not edit by hand.',
        '// Regenerate from assets/sprites/*.png; commit PNGs + this file together.',
        '#include "sprites.h"',
        '',
        'namespace walkie_ui {',
        '',
    ]

    for theme in themes:
        char_id = theme[0]
        for expr in EXPRESSIONS:
            rgb565, mask = frames[char_id][expr]
            # RGB565 frame
            L.append(f'const uint16_t {c_frame_name(char_id, expr)}[SPRITE_PIXELS] = {{')
            row = "    "
            for i, v in enumerate(rgb565):
                row += f"0x{v:04X},"
                if (i + 1) % 12 == 0:
                    L.append(row)
                    row = "    "
            if row.strip():
                L.append(row)
            L.append('};')
            # 1bpp alpha mask
            L.append(f'const uint8_t {c_mask_name(char_id, expr)}[SPRITE_MASK_BYTES] = {{')
            row = "    "
            for i, v in enumerate(mask):
                row += f"0x{v:02X},"
                if (i + 1) % 16 == 0:
                    L.append(row)
                    row = "    "
            if row.strip():
                L.append(row)
            L.append('};')
            L.append('')

    # Theme table (unchanged shape).
    L.append('static const CharacterTheme k_themes[NCHARS] = {')
    for t in themes:
        L.append(f'    {{"{t[0]}", 0x{t[1]:04X}, 0x{t[2]:04X}, 0x{t[3]:04X}, '
                 f'0x{t[4]:04X}, 0x{t[5]:04X}, 0x{t[6]:04X}}},')
    L.append('};')
    L.append('')

    # Sprite table: RGB565 frames[] + masks[].
    L.append('static const CharacterSprites k_sprites[NCHARS] = {')
    for t in themes:
        char_id = t[0]
        fnames = ", ".join(c_frame_name(char_id, e) for e in EXPRESSIONS)
        mnames = ", ".join(c_mask_name(char_id, e) for e in EXPRESSIONS)
        L.append(f'    {{"{char_id}", {{{fnames}}}, {{{mnames}}}}},')
    L.append('};')
    L.append('')

    L.extend([
        'const CharacterTheme* get_character_theme(size_t idx) {',
        '    return (idx < NCHARS) ? &k_themes[idx] : nullptr;',
        '}',
        '',
        'const CharacterSprites* get_character_sprites(size_t idx) {',
        '    return (idx < NCHARS) ? &k_sprites[idx] : nullptr;',
        '}',
        '',
        'size_t character_index_by_id(const char* id) {',
        '    if (!id) return NCHARS;',
        '    for (size_t i = 0; i < NCHARS; ++i) {',
        '        const char* c = k_themes[i].id;',
        '        const char* p = id;',
        '        while (*c && *c == *p) { ++c; ++p; }',
        "        if (*c == '\\0' && *p == '\\0') return i;",
        '    }',
        '    return NCHARS;',
        '}',
        '',
        '} // namespace walkie_ui',
        '',
    ])
    out.write_text("\n".join(L))


# ---------------------------------------------------------------------------
# Public helpers (used by preview_ui.py)
# ---------------------------------------------------------------------------
def get_themes() -> List[Tuple[str, int, int, int, int, int, int]]:
    return THEMES


def assets_dir() -> Path:
    return Path(__file__).resolve().parent.parent / "assets" / "sprites"


def png_path(char_id: str, expr: str) -> Path:
    return assets_dir() / f"{char_id}_{expr}.png"


def expected_png_names() -> List[str]:
    """The exact 44 PNG filenames (11 chars x 4 expressions) the manifest requires."""
    return [f"{t[0]}_{e}.png" for t in THEMES for e in EXPRESSIONS]


def check_manifest(adir: Path) -> int:
    """Verify the set of PNGs in `adir` matches the expected 44 names exactly.

    Returns 0 if the set matches, 1 otherwise (printing missing/extra files).
    Catches misnamed files (e.g. radek_idel.png) and stray PNGs, which a simple
    per-name existence loop would miss.
    """
    expected = set(expected_png_names())
    present = {p.name for p in adir.glob("*.png")}
    missing = sorted(expected - present)
    extra = sorted(present - expected)
    ok = True
    if missing:
        ok = False
        print(f"ERROR: {len(missing)} expected PNG(s) missing from {adir}:",
              file=sys.stderr)
        for n in missing:
            print(f"    - {n}", file=sys.stderr)
    if extra:
        ok = False
        print(f"ERROR: {len(extra)} unexpected PNG(s) in {adir} "
              f"(misnamed or stray?):", file=sys.stderr)
        for n in extra:
            print(f"    + {n}", file=sys.stderr)
    return 0 if ok else 1


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate sprites.cpp from assets/sprites/*.png (RGB565 + 1bpp mask)."
    )
    parser.add_argument(
        "--check", "--no-placeholders", dest="check", action="store_true",
        help="Drift/manifest gate: do NOT write placeholder PNGs. ERROR if any "
             "of the 44 expected PNGs is missing (or extras/misnamed exist), "
             "then regenerate sprites.cpp and exit. Use this in CI so missing "
             "real art fails the build instead of silently producing "
             "placeholders.",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    out = (script_dir.parent / "lib" / "walkie_ui_logic"
           / "walkie_ui_logic" / "sprites.cpp")
    adir = assets_dir()

    if args.check:
        # No bootstrapping: the PNGs must already exist and match the manifest.
        if check_manifest(adir) != 0:
            print("ERROR: assets/sprites/*.png manifest mismatch — refusing to "
                  "generate placeholders in --check mode.", file=sys.stderr)
            return 1
    else:
        # Normal/bootstrap mode: fill in any missing placeholder PNGs so the
        # firmware always compiles, then validate the resulting set.
        n = write_placeholders(adir)
        if n:
            print(f"Wrote {n} placeholder PNG(s) into {adir}")
        if check_manifest(adir) != 0:
            return 1

    frames: Dict[str, Dict[str, Tuple[List[int], bytes]]] = {}
    for theme in THEMES:
        char_id = theme[0]
        frames[char_id] = {}
        for expr in EXPRESSIONS:
            path = png_path(char_id, expr)
            if not path.exists():
                print(f"ERROR: missing PNG {path}", file=sys.stderr)
                return 1
            frames[char_id][expr] = load_frame(path)

    emit_cpp(THEMES, frames, out)
    n_frames = len(THEMES) * len(EXPRESSIONS)
    frame_bytes = SPRITE_W * SPRITE_H * 2
    mask_bytes = (SPRITE_W * SPRITE_H + 7) // 8
    total = n_frames * (frame_bytes + mask_bytes)
    print(f"Generated {out}")
    print(f"  {n_frames} frames ({SPRITE_W}x{SPRITE_H} RGB565 + 1bpp mask)")
    print(f"  approx flash: {total} bytes "
          f"({n_frames}*({frame_bytes}+{mask_bytes}))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
