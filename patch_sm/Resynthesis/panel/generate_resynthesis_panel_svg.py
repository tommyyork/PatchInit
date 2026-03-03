#!/usr/bin/env python3
"""
Generate the canonical Resynthesis front-panel SVG (`ResynthesisPanel.svg`).

This script is the single mechanical source-of-truth for the Resynthesis panel.
It emits a 3U × 10HP SVG (128.5 × 50.8 mm) whose drill centres, SD-card cutout,
and Eurorack mounting screw slots match:

- The Electrosmith Patch.Init NPTH / Edge_Cuts Gerbers used elsewhere
  in this folder.
- The Eurorack / Doepfer A-100 mechanical standard summarized in
  `eurorack_spec/README.md` (panel height 128.5 mm, mounting rows 3 mm from
  top and bottom edges).

Run this script whenever you need to regenerate `ResynthesisPanel.svg`:

  python3 generate_resynthesis_panel_svg.py

By default it overwrites `ResynthesisPanel.svg` next to this script.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
from string import Template


HERE = Path(__file__).parent
DEFAULT_OUTPUT = HERE / "ResynthesisPanel.svg"

# 3U × 10HP panel geometry (mm), matching eurorack_spec/README.md.
PANEL_WIDTH_MM = 50.8
PANEL_HEIGHT_MM = 128.5


def _format_screw_slots() -> str:
    """
    Return four wide, black-filled rectangular screw slots as SVG <rect> lines.

    Requirements and assumptions:
    - Mounting rows are 3 mm from the top and bottom edges (Eurorack standard),
      so the slot centres lie at y = 3 mm and y = 128.5 − 3 = 125.5 mm.
    - The left/right X positions for two of the slots are aligned with the
      Patch.Init NPTH 3.0 mm mounting holes, whose panel-local centres are
      (7.50, 3.00) and (43.10, 125.50) mm in the canonical layout.
    - We add the complementary two slots at the remaining corners so the panel
      can be mounted with four screws while still matching the original board
      hardware for the two stock mounting holes.

    Slots are “wide rather than tall”: width > height in panel coordinates.
    The tests in `test_panel_alignment.py` treat these as black-filled rects
    with small dimensions in mm, and verify that their vertical centres are
    3 mm from the top/bottom edges (rail alignment).
    """
    screw_width_mm = 5.0
    screw_height_mm = 3.0

    # Canonical mounting row Y positions from Eurorack spec.
    y_top = 3.0
    y_bottom = PANEL_HEIGHT_MM - 3.0

    # X positions: left side matches the existing canonical panel (7.50 mm);
    # right side matches the existing Patch.Init mounting hole centre at 43.10 mm.
    x_left = 7.50
    x_right = 43.10

    centers = [
        (x_left, y_top),
        (x_right, y_top),
        (x_left, y_bottom),
        (x_right, y_bottom),
    ]

    lines: list[str] = []
    for cx, cy in centers:
        x = cx - screw_width_mm / 2.0
        y = cy - screw_height_mm / 2.0
        lines.append(
            (
                f'  <rect x="{x:.3f}" y="{y:.3f}" '
                f'width="{screw_width_mm:.3f}" height="{screw_height_mm:.3f}" '
                f'rx="1.0" fill="#000000" stroke="#ffffff" stroke-width="0.2" />'
            )
        )
    return "\n".join(lines)


# NOTE: This template is intentionally very close to the checked-in
# `ResynthesisPanel.svg`. Only the mounting screw geometry is parameterised
# via $SCREW_SLOTS so that the Eurorack rail alignment and rectangular-slot
# requirement are explicit and testable from code.
PANEL_TEMPLATE = Template(
    """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<!--
  Front-panel PCB artwork for the Resynthesis module (Patch.Init format).
  Units: mm. Size: 3U x 10HP (128.5 x 50.8 mm).
  All labels centered on their drill; jack labels above each jack.
  Font: open-source DIN-style (see README). All text all caps.
-->
<svg
  xmlns="http://www.w3.org/2000/svg"
  width="50.8mm"
  height="128.5mm"
  viewBox="0 0 50.8 128.5"
>
  <defs>
    <style type="text/css">
      .panel-text { font-family: Gidole, 'DIN Alternate', 'DIN 2014', sans-serif; }
    </style>
    <!-- Copper-only background patterns:
         - Left: large broken squares
         - Middle: debris/fragmented lines (letters washing off)
         - Right: large broken circles
         All strokes use a single copper colour with no fills or tones. -->

    <!-- Left: large broken squares (rect only, no circles) -->
    <pattern id="patternSquares" width="4" height="4" patternUnits="userSpaceOnUse">
      <rect x="0" y="0" width="4" height="4"
            fill="none"
            stroke="#d4af37"
            stroke-width="0.2"
            stroke-dasharray="2 1" />
    </pattern>

    <!-- Middle: debris / falling fragments -->
    <pattern id="patternDebris" width="4" height="4" patternUnits="userSpaceOnUse">
      <!-- Short, staggered segments suggesting pieces falling off -->
      <line x1="0.5" y1="0.4" x2="2.0" y2="0.4"
            stroke="#d4af37"
            stroke-width="0.18" />
      <line x1="2.2" y1="1.4" x2="3.5" y2="1.4"
            stroke="#d4af37"
            stroke-width="0.18"
            stroke-dasharray="0.6 0.4" />
      <line x1="0.2" y1="2.1" x2="1.4" y2="2.7"
            stroke="#d4af37"
            stroke-width="0.18" />
      <line x1="2.0" y1="2.8" x2="3.8" y2="3.2"
            stroke="#d4af37"
            stroke-width="0.18"
            stroke-dasharray="0.4 0.6" />
    </pattern>

    <!-- Right: circle-like fragments built from short line segments (no <circle> elements) -->
    <pattern id="patternCircles" width="4" height="4" patternUnits="userSpaceOnUse">
      <!-- Four short chords hinting at a circle outline -->
      <line x1="1.0" y1="0.8" x2="2.4" y2="0.6"
            stroke="#d4af37"
            stroke-width="0.2" />
      <line x1="2.6" y1="1.0" x2="3.2" y2="2.0"
            stroke="#d4af37"
            stroke-width="0.2" />
      <line x1="3.0" y1="2.6" x2="1.8" y2="3.2"
            stroke="#d4af37"
            stroke-width="0.2" />
      <line x1="1.0" y1="3.0" x2="0.6" y2="1.8"
            stroke="#d4af37"
            stroke-width="0.2" />
    </pattern>

    <!-- Masks to grade the background from squares (left) through debris (middle) to circles (right).
         Squares are present across the full panel; debris and circle fragments become denser toward the right. -->
    <mask id="maskLeft">
      <!-- Squares everywhere -->
      <rect x="0" y="0" width="50.8" height="128.5" fill="white" />
    </mask>
    <mask id="maskMid">
      <!-- Debris mainly in the centre, with a soft-edged, irregular band -->
      <rect x="10" y="0" width="14" height="128.5" fill="white" />
      <rect x="18" y="0" width="12" height="128.5" fill="white" />
      <rect x="24" y="0" width="10" height="128.5" fill="white" />
    </mask>
    <mask id="maskRight">
      <!-- Circle fragments sparse in the mid-right, dense at the far right -->
      <rect x="26" y="0" width="8" height="128.5" fill="white" />
      <rect x="32" y="0" width="10" height="128.5" fill="white" />
      <rect x="38" y="0" width="12.8" height="128.5" fill="white" />
    </mask>
  </defs>

  <rect x="0" y="0" width="50.8" height="128.5" fill="#050505" />
  <!-- Background transition: squares -> debris -> circles (all rects full-size, sliced by masks) -->
  <rect x="0" y="0" width="50.8" height="128.5" fill="url(#patternSquares)" mask="url(#maskLeft)" />
  <rect x="0" y="0" width="50.8" height="128.5" fill="url(#patternDebris)" mask="url(#maskMid)" />
  <rect x="0" y="0" width="50.8" height="128.5" fill="url(#patternCircles)" mask="url(#maskRight)" />
  <rect x="0.15" y="0.15" width="50.5" height="128.2" fill="none" stroke="#d4af37" stroke-width="0.3" />

  <!-- Title (above all drills) -->
  <text x="25.4" y="8" class="panel-text" font-size="4" text-anchor="middle" fill="#ffffff">&#21270;</text>
  <text x="25.4" y="13" class="panel-text" font-size="3.6" text-anchor="middle" fill="#f5e3a1">RESYNTHESIS</text>

  <!-- Mounting screw slots: wide rectangular, aligned with Eurorack rails (3 mm from top/bottom) -->
$SCREW_SLOTS

  <!-- SD card holder cutout (matches patch_init_gerbers/blank-Edge_Cuts.gbr) -->
  <rect x="24.14" y="33.493" width="3.208" height="12.802" fill="none" stroke="#ffffff" stroke-width="0.2" />

  <!-- Pots CV_1-CV_4 (7.2 mm) -->
  <circle cx="11.176" cy="22.904" r="3.6" fill="none" stroke="#ffffff" stroke-width="0.3" />
  <circle cx="39.65" cy="22.904" r="3.6" fill="none" stroke="#ffffff" stroke-width="0.3" />
  <circle cx="11.176" cy="42.027" r="3.6" fill="none" stroke="#ffffff" stroke-width="0.3" />
  <circle cx="39.65" cy="42.027" r="3.6" fill="none" stroke="#ffffff" stroke-width="0.3" />

  <!-- Labels beneath pots row 1 (y >= 26.5), font 3.6 mm (>= 10 pt); clear of 12mm knob -->
  <text x="11.176" y="33" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">DRY / WET</text>
  <text x="39.65" y="33" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">SMOOTH</text>

  <!-- Labels beneath pots row 2 (y >= 45.6); clear of knob. CV_3 labeled FLUFF, CV_4 labeled COLOR -->
  <text x="11.176" y="52" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">FLUFF</text>
  <text x="39.65" y="52" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">COLOR</text>

  <!-- T2 LED; T3/T4 jacks and switches -->
  <circle cx="25.4" cy="19.252" r="1.6" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="8.65" cy="59.288" r="2.75" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="7.15" cy="84.562" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="7.15" cy="98.312" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="7.15" cy="111.9" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="19.317" cy="84.562" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="19.317" cy="98.312" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="19.317" cy="111.9" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="25.503" cy="61.957" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="31.483" cy="84.562" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="31.483" cy="98.312" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="31.483" cy="111.9" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="42.155" cy="59.288" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="43.65" cy="84.562" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="43.65" cy="98.312" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />
  <circle cx="43.65" cy="111.9" r="3.1" fill="none" stroke="#ffffff" stroke-width="0.2" />

  <!-- Left switch (B_7, reserved) at (8.65, 59.288): panel label RESERVED for future use -->
  <text x="8.65" y="66" class="panel-text" font-size="3.53" text-anchor="middle" fill="#ffffff">RESERVED</text>

  <!-- Centre switch (B_8, mode) at (25.503, 61.957): panel label PITCH LOCK -->
  <text x="25.503" y="66" class="panel-text" font-size="3.53" text-anchor="middle" fill="#ffffff">PITCH LOCK</text>

  <!-- CV_OUT_1 / C10 jack at (42.155, 59.288): panel label THOUGHTS -->
  <text x="42.155" y="66" class="panel-text" font-size="3.53" text-anchor="middle" fill="#ffffff">THOUGHTS</text>

  <!-- 12 jacks: labels OVER each jack, centered. Top row (y=84.562) ? B10, B9, B5, B6 -->
  <text x="7.15" y="79.5" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">B10</text>
  <text x="19.317" y="79.5" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">B9</text>
  <text x="31.483" y="79.5" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">B5</text>
  <text x="43.65" y="79.5" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">B6</text>

  <!-- Middle row (y=98.312) ? V/OCT, TIME, DENSITY, D (italic, diffusion) -->
  <text x="7.15" y="93.3" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">V/OCT</text>
  <text x="19.317" y="93.3" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">TIME</text>
  <text x="31.483" y="93.3" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">DENSITY</text>
  <text x="43.65" y="93.3" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff" font-style="italic">D</text>

  <!-- Bottom row (y=111.9) ? IN L, IN R, OUT L, OUT R -->
  <text x="7.15" y="106.9" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">IN L</text>
  <text x="19.317" y="106.9" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">IN R</text>
  <text x="31.483" y="106.9" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">OUT L</text>
  <text x="43.65" y="106.9" class="panel-text" font-size="3.6" text-anchor="middle" fill="#ffffff">OUT R</text>

</svg>
"""
)


def build_panel_svg() -> str:
    """Render the full panel SVG as a string."""
    screw_slots = _format_screw_slots()
    svg = PANEL_TEMPLATE.substitute(SCREW_SLOTS=screw_slots)

    # Ensure all circular drill holes render as solid black rather than letting
    # the background pattern show through.
    svg = re.sub(
        r'(<circle\b[^>]*?)\s+fill="none"([^>]*?stroke="#ffffff"[^>]*?/>)',
        r'\1 fill="#000000"\2',
        svg,
    )
    return svg


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate the canonical Resynthesis panel SVG (ResynthesisPanel.svg)."
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Output SVG path (default: ResynthesisPanel.svg in this directory)",
    )
    args = parser.parse_args()

    svg = build_panel_svg()
    output_path = args.output
    output_path.write_text(svg, encoding="utf-8")
    print(f"Wrote Resynthesis panel SVG \u2192 {output_path}")


if __name__ == "__main__":
    main()

