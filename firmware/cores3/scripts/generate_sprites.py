#!/usr/bin/env python3
"""
DEPRECATED — superseded by scripts/generate_sprites_png.py.

This generated the OLD 16x16 indexed-palette sprite model. The firmware now uses
64x64 RGB565 + 1bpp alpha mask art generated from assets/sprites/*.png. Running
this script would overwrite the new sprites.cpp with the obsolete format — do
NOT run it. Kept only for reference / history.

Generate C sprite data for the CoreS3 walkie-talkie UI.

Each character has four 16x16 expression frames drawn as ASCII art below.
Characters are rendered against a shared semantic palette:
    ' ' = transparent
    '.' = black
    '-' = white
    's' = skin
    'a' = accent (character main colour)
    'A' = accent_dark
    'b' = secondary
    'B' = secondary_dark
    'g' = background (unused in sprite, kept for completeness)
    'x' = glow

Output: ../src/ui/sprites.cpp
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List, Tuple

PALETTE: Dict[str, int] = {
    " ": 0,  # TRANSPARENT
    ".": 1,  # BLACK
    "-": 2,  # WHITE
    "s": 3,  # SKIN
    "a": 4,  # ACCENT
    "A": 5,  # ACCENT_DARK
    "b": 6,  # SECONDARY
    "B": 7,  # SECONDARY_DARK
    "g": 8,  # BACKGROUND
    "x": 9,  # GLOW
}

# RGB565 colours for each character theme.
THEMES: List[Tuple[str, int, int, int, int, int, int]] = [
    # (id, accent, accent_dark, secondary, secondary_dark, background, glow)
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

# Helpers to draw a 16x16 head shape with common features.
BLANK = ["                "] * 16


def head_outline(base: List[str], color: str = "a", shadow: str = "A") -> List[str]:
    """Draw a rounded square head outline."""
    out = [list(row) for row in base]
    for y in range(2, 14):
        out[y][2] = shadow
        out[y][13] = shadow
    for x in range(2, 14):
        out[2][x] = shadow
        out[13][x] = shadow
    for y in range(3, 13):
        for x in range(3, 13):
            out[y][x] = color
    return ["".join(row) for row in out]


def eyes(frame: List[str], open_eyes: bool = True, color: str = ".") -> List[str]:
    out = [list(row) for row in frame]
    if open_eyes:
        out[6][5] = color
        out[6][10] = color
        out[7][5] = color
        out[7][10] = color
    else:
        for x in (5, 6, 10, 11):
            out[7][x] = color
    return ["".join(row) for row in out]


def mouth_smile(frame: List[str], color: str = ".") -> List[str]:
    out = [list(row) for row in frame]
    out[10][6] = color
    out[10][7] = color
    out[10][8] = color
    out[10][9] = color
    out[11][7] = color
    out[11][8] = color
    return ["".join(row) for row in out]


def mouth_open(frame: List[str], color: str = ".") -> List[str]:
    out = [list(row) for row in frame]
    for y in (9, 10, 11):
        for x in (6, 7, 8, 9):
            out[y][x] = color
    out[10][7] = "-"
    out[10][8] = "-"
    return ["".join(row) for row in out]


def mouth_o(frame: List[str], color: str = ".") -> List[str]:
    out = [list(row) for row in frame]
    out[9][7] = color
    out[9][8] = color
    out[10][6] = color
    out[10][9] = color
    out[11][7] = color
    out[11][8] = color
    return ["".join(row) for row in out]


def apply_overlay(frame: List[str], overlays: List[Tuple[int, int, str]]) -> List[str]:
    out = [list(row) for row in frame]
    for y, x, ch in overlays:
        if 0 <= y < 16 and 0 <= x < 16:
            out[y][x] = ch
    return ["".join(row) for row in out]


def build_character_frames(spec: Dict) -> List[List[str]]:
    base = head_outline(BLANK, color=spec["main"], shadow=spec["shadow"])
    idle = eyes(base, open_eyes=True)
    idle = mouth_smile(idle)
    listen = eyes(base, open_eyes=True)
    listen = mouth_o(listen)
    think = eyes(base, open_eyes=True)
    think = mouth_o(think)
    speak = eyes(base, open_eyes=True)
    speak = mouth_open(speak)

    # Apply character-specific overlays.
    idle = apply_overlay(idle, spec.get("idle_overlay", []))
    listen = apply_overlay(listen, spec.get("listen_overlay", []))
    think = apply_overlay(think, spec.get("think_overlay", []))
    speak = apply_overlay(speak, spec.get("speak_overlay", []))
    return [idle, listen, think, speak]


CHARACTER_SPECS: List[Dict] = [
    {
        "id": "radek",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 5, "a"), (1, 6, "a"), (1, 7, "a"), (1, 8, "a"), (1, 9, "a"),
                         (0, 6, "a"), (0, 7, "a"), (0, 8, "a"), (2, 4, "a"), (2, 10, "a")],
    },
    {
        "id": "steve",
        "main": "s",
        "shadow": ".",
        "idle_overlay": [(1, 4, "a"), (1, 5, "a"), (1, 6, "a"), (1, 7, "a"), (1, 8, "a"),
                         (1, 9, "a"), (1, 10, "a"), (1, 11, "a"), (2, 4, "a"), (2, 11, "a"),
                         (3, 4, "A"), (3, 11, "A"), (4, 4, "a"), (4, 11, "a")],
    },
    {
        "id": "simba",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 3, "a"), (1, 4, "a"), (1, 11, "a"), (1, 12, "a"),
                         (2, 2, "A"), (2, 13, "A"), (3, 2, "a"), (3, 13, "a")],
    },
    {
        "id": "ryder",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 4, "a"), (1, 5, "a"), (1, 6, "a"), (1, 7, "a"), (1, 8, "a"),
                         (1, 9, "a"), (1, 10, "a"), (1, 11, "a"), (2, 4, "a"), (2, 11, "a")],
    },
    {
        "id": "creeper",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(3, 4, "."), (3, 5, "."), (3, 10, "."), (3, 11, "."),
                         (5, 4, "."), (5, 11, "."), (7, 6, "."), (7, 9, "."),
                         (9, 5, "."), (9, 6, "."), (9, 9, "."), (9, 10, ".")],
    },
    {
        "id": "pimpek",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 3, "a"), (1, 4, "a"), (1, 11, "a"), (1, 12, "a"),
                         (2, 2, "A"), (2, 13, "A"), (12, 4, "s"), (12, 11, "s")],
    },
    {
        "id": "crewmate",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 5, "b"), (1, 6, "b"), (1, 7, "b"), (1, 8, "b"), (1, 9, "b"), (1, 10, "b"),
                         (2, 5, "B"), (2, 10, "B"), (3, 5, "b"), (3, 10, "b")],
    },
    {
        "id": "sonic",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 1, "a"), (1, 2, "a"), (1, 13, "a"), (1, 14, "a"),
                         (2, 0, "a"), (2, 15, "a"), (3, 0, "a"), (3, 15, "a")],
    },
    {
        "id": "pikachu",
        "main": "a",
        "shadow": "b",
        "idle_overlay": [(0, 3, "a"), (0, 4, "a"), (0, 11, "a"), (0, 12, "a"),
                         (1, 2, "b"), (1, 13, "b"), (7, 5, "b"), (7, 10, "b")],
    },
    {
        "id": "mario",
        "main": "a",
        "shadow": "A",
        "idle_overlay": [(1, 4, "a"), (1, 5, "a"), (1, 6, "a"), (1, 7, "a"), (1, 8, "a"),
                         (1, 9, "a"), (1, 10, "a"), (1, 11, "a"), (2, 4, "a"), (2, 11, "a")],
    },
    {
        "id": "roblox_noob",
        "main": "a",
        "shadow": "b",
        "idle_overlay": [(1, 4, "b"), (1, 5, "b"), (1, 6, "b"), (1, 7, "b"), (1, 8, "b"),
                         (1, 9, "b"), (1, 10, "b"), (1, 11, "a"), (2, 4, "b"), (2, 11, "a")],
    },
]


def frame_to_bytes(frame: List[str]) -> bytes:
    assert len(frame) == 16 and all(len(row) == 16 for row in frame)
    return bytes(PALETTE[ch] for row in frame for ch in row)


def c_array_name(char_id: str, expr: str) -> str:
    return f"k_{char_id}_{expr}_frame"


def emit_cpp(themes: List[Tuple], frames: Dict[str, List[bytes]], out: Path) -> None:
    lines = [
        '// Auto-generated by scripts/generate_sprites.py — do not edit by hand.',
        '#include "sprites.h"',
        '',
        'namespace walkie_ui {',
        '',
    ]

    for char_id, expr_frames in frames.items():
        for expr_name, data in zip(["idle", "listen", "think", "speak"], expr_frames):
            name = c_array_name(char_id, expr_name)
            lines.append(f'constexpr uint8_t {name}[SPRITE_BYTES] = {{')
            row = "    "
            for i, b in enumerate(data):
                row += f"0x{b:02X},"
                if (i + 1) % 16 == 0:
                    lines.append(row)
                    row = "    "
            if row.strip():
                lines.append(row)
            lines.append('};')
            lines.append('')

    lines.append('static const CharacterTheme k_themes[NCHARS] = {')
    for t in themes:
        lines.append(f'    {{"{t[0]}", 0x{t[1]:04X}, 0x{t[2]:04X}, 0x{t[3]:04X}, 0x{t[4]:04X}, 0x{t[5]:04X}, 0x{t[6]:04X}}},')
    lines.append('};')
    lines.append('')

    lines.append('static const CharacterSprites k_sprites[NCHARS] = {')
    for t in themes:
        char_id = t[0]
        frames_names = [c_array_name(char_id, e) for e in ("idle", "listen", "think", "speak")]
        lines.append(f'    {{"{char_id}", {{{", ".join(frames_names)}}}}},')
    lines.append('};')
    lines.append('')

    lines.extend([
        'const CharacterTheme* get_character_theme(size_t idx) {',
        '    return (idx < NCHARS) ? &k_themes[idx] : nullptr;',
        '}',
        '',
        'const CharacterSprites* get_character_sprites(size_t idx) {',
        '    return (idx < NCHARS) ? &k_sprites[idx] : nullptr;',
        '}',
        '',
        'uint16_t resolve_palette_color(SpriteColor idx, const CharacterTheme& theme) {',
        '    switch (idx) {',
        '        case SpriteColor::TRANSPARENT:    return 0x0000;',
        '        case SpriteColor::BLACK:          return 0x0000;',
        '        case SpriteColor::WHITE:          return 0xFFFF;',
        '        case SpriteColor::SKIN:           return 0xFEA0;',
        '        case SpriteColor::ACCENT:         return theme.accent;',
        '        case SpriteColor::ACCENT_DARK:    return theme.accent_dark;',
        '        case SpriteColor::SECONDARY:      return theme.secondary;',
        '        case SpriteColor::SECONDARY_DARK: return theme.secondary_dark;',
        '        case SpriteColor::BACKGROUND:     return theme.background;',
        '        case SpriteColor::GLOW:           return theme.glow;',
        '        default:                          return 0x0000;',
        '    }',
        '}',
        '',
        'size_t character_index_by_id(const char* id) {',
        '    if (!id) return NCHARS;',
        '    for (size_t i = 0; i < NCHARS; ++i) {',
        '        const char* c = k_themes[i].id;',
        '        const char* p = id;',
        '        while (*c && *c == *p) { ++c; ++p; }',
        '        if (*c == \'\\0\' && *p == \'\\0\') return i;',
        '    }',
        '    return NCHARS;',
        '}',
        '',
        '} // namespace walkie_ui',
        '',
    ])
    out.write_text("\n".join(lines))


def get_all_frames() -> Dict[str, List[List[str]]]:
    """Return raw ASCII frames for every character."""
    return {spec["id"]: build_character_frames(spec) for spec in CHARACTER_SPECS}


def get_themes() -> List[Tuple[str, int, int, int, int, int, int]]:
    return THEMES


def main() -> int:
    # HARD GATE: this deprecated generator emits the obsolete 16x16 indexed
    # format to the SAME sprites.cpp path used by generate_sprites_png.py. Running
    # it would clobber the current 64x64 RGB565 + 1bpp-mask sprites.cpp. Refuse
    # to run BEFORE any file write so it can never destroy the live file.
    sys.exit(
        "DEPRECATED: use generate_sprites_png.py; this writes the obsolete 16x16 "
        "indexed format and would clobber sprites.cpp. Refusing to run."
    )


if __name__ == "__main__":
    sys.exit(main())
