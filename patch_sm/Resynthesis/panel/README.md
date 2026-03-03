# Resynthesis / 化 Front Panel

All up‑to‑date information about **panel labels, control behaviour, jack mapping,
and mechanical tests** now lives in the **main Resynthesis README** under the
“Controls and panel mapping” and “Panel” sections:

- `../README.md` → **Controls and panel mapping**  
- `../README.md` → **Panel**

This `panel/README.md` is kept only as a thin pointer so there is a single source
of truth for documentation.

## Manufacturing notes

- The SVG uses **mm** for size and coordinates. Most PCB CAD tools can import
  this as a mechanical layer to generate Gerbers for:
  - Board outline (the outer rectangle)
  - Drill holes (circle centres and radii for jacks, knobs, switches, and
    mounting holes)
  - **SD card holder cutout** (rectangular cutout matching Patch.Init Edge_Cuts)
  - Silkscreen (text and decorative shapes)

- For a PCB front panel:
  - Use at least **1.6 mm** FR4 for stiffness (or thicker if desired).
  - Put the artwork on the **front silkscreen** and any copper artwork on the
    **front copper** layers as desired.
  - Make sure to align the drill centres with your chosen hardware footprints
    (jacks, pots, switches) in your CAD tool before fabrication.

Once imported and aligned in KiCad or Eagle, you can export a standard Gerber
set and send it directly to your PCB manufacturer.

## Panel drill alignment and tests

Earlier iterations of this panel were laid out by eye against the hardware,
which meant the **drill locations and diameters did not exactly match** the
stock Electrosmith Patch.Init front panel.

The current `ResynthesisPanel.svg` has been revised so that:

- The board outline is **50.8 × 128.5 mm** (10HP × 3U), matching
  `blank-Edge_Cuts.gbr`.
- There are **exactly 22 drill holes**, matching the non‑plated NPTH tools in
  `patch_init_gerbers/blank-NPTH.drl`:
  - 2 × 3.0 mm **mounting holes** (implemented on the panel as **four wide,
    rectangular screw slots** whose equivalent 3.0 mm drill centres match the
    Patch.Init NPTH file and sit 3 mm from the top and bottom edges, per the
    Eurorack rail standard),
  - 1 × 3.2 mm (T2),
  - 2 × 5.5 mm (T3) for switches,
  - 13 × 6.2 mm (T4) for jacks,
  - 4 × 7.2 mm (T5) for potentiometer shafts.
- A **rectangular SD card holder cutout** is present and matches
  `patch_init_gerbers/blank-Edge_Cuts.gbr` (position and size within 0.1 mm).
- Hole **positions and diameters are matched within 0.05 mm** to the NPTH
  drill file.

The helper script `test_panel_alignment.py` enforces that any panel design
passes the tests. **The first test run is the drill-identity check:**

- **`test_panel_drill_holes_match_patch_init`** *(run first)*  
  Verifies that the canonical panel SVG has drill hole **positions and
  diameters identical** to `patch_init_gerbers/blank-NPTH.drl` (position
  tolerance 0.05 mm, diameter tolerance 0.05 mm). The script exits with
  failure if this test does not pass. No other panel design or preview
  image should be used for fabrication unless the SVG passes this test.

- **`test_custom_panels_align_with_canonical`**  
  Any alternate panel SVGs named `ResynthesisPanel_*.svg` must share the
  **same hardware hole layout** as the canonical panel (same number of holes,
  positions matched within 0.15 mm). This ensures alternates remain
  swappable with the stock Patch.Init panel.

- **`test_canonical_panel_dimensions_and_diameters`**  
  Verifies the SVG has `width` and `height` in mm (50.8 × 128.5 mm),
  a `viewBox`, and a limited set of drill radii (internally consistent
  groups for pots, jacks, etc.). Confirms the Patch.Init PTH drill file
  exists and is metric so scale is consistent with the target PCB.

- **`test_panel_cutouts_four_pots_switches_and_sd_slot_match_patch_init`**  
  Verifies that the **4 potentiometer shaft cutouts** (7.2 mm), the **2 switch
  cutouts** (5.5 mm), and the **SD card holder cutout** are present and in the
  same place as the Patch.Init module. The four pots and two switches are
  already verified by the drill-identity test; this test adds verification
  that the rectangular SD card holder cutout from `blank-Edge_Cuts.gbr` is
  present in the panel SVG and matches position and size (within 0.1 mm).
  The panel can then be swapped with the stock Patch.Init front panel.

- **`test_screw_holes_eurorack_rail_distance`**  
  Verifies that the four corner screw cutouts (wide rectangular slots) have their
  **vertical** centers at the same distance from the panel top/bottom as the
  Eurorack standard: **3 mm** from the top edge and **3 mm** from the bottom edge
  (see `eurorack_spec/README.md`). This ensures the panel aligns with standard
  Eurorack rails (Doepfer A-100 / Gie-Tec). Drill positions for the two Patch.Init
  mounting holes are taken from the screw cutout rect centers.

- **`test_panel_passes_pcbway_style_validation`**  
  Runs local PCBWay-style mechanical checks so the panel would pass their
  online Gerber validation: minimum non-plated hole diameter ≥ 0.45 mm,
  minimum edge-to-edge spacing between holes ≥ 0.2 mm, minimum hole edge
  to board outline ≥ 0.5 mm, and board dimensions within manufacturer
  capability (e.g. ≤ 500 mm).

- **`test_knob_labels_not_obscured_by_rogan_knobs`**  
  Models Mutable Instruments–style Rogan knobs (12 mm diameter) with a
  small clearance (0.5 mm) around the four pot holes (CV_1–CV_4). Ensures
  the **primary** labels (DRY / WET, SMOOTH, FLUFF, BRIGHT /) do not
  intersect those knob footprints.

- **`test_no_overlapping_text`**  
  Ensures no two text elements overlap. Same-column multi-line labels
  (e.g. BRIGHT / and DARK, or DIFFU and SION) are allowed to touch; a
  small overlap margin is allowed for adjacent jack labels.

- **`test_text_within_printed_area`**  
  All text must stay within the printed panel bounds (50.8 × 128.5 mm)
  with a 1 mm margin from each edge so that no label runs off the
  physical panel.

- **`test_no_font_smaller_than_10pt`**  
  All text must use a font size of at least 10 point (SVG user units
  here are mm; 10 pt ≈ 3.53 mm) for readability.

- **`test_labels_beneath_drill_centered`**  
  For each label below the title area (y ≥ 16 mm), the relevant drill is
  the nearest hole (by position). The label must be either **beneath** that
  hole (text y ≥ hole bottom) or **above** it (text y ≤ hole top) and
  horizontally centered (text x within 2 mm of hole center when
  `text-anchor` is middle).

**Final step (after all tests):** The test script runs `render_eurorack_overlay.py`
to generate an **Eurorack standard overlay** SVG. The overlay shows:
- **Green dotted** vertical lines at 2HP, 4HP, 6HP, 8HP boundaries (1 HP = 5.08 mm).
- **Neon blue dotted** horizontal lines at the vertical center of the rails when
  the module is mounted (3 mm from top and bottom, per Doepfer A-100).
- **Neon pink** annotations: (x, y) at the center of each cut/hole, plus panel
  width × height. The overlay has the same viewBox as the panel so you can open
  both in Inkscape and stack the overlay on top for alignment reference. The
  script is reusable for other panel SVGs (see `render_eurorack_overlay.py --help`).

## External references and expected file formats

External mechanical references and design rules:

- **Eurorack / Doepfer A-100 mechanical standard** — `eurorack_spec/README.md` in this
  folder summarizes mounting hole positions (3 mm from top/bottom edge), panel
  height (128.5 mm), and links to Doepfer and Exploding Shed documentation. The
  test `test_screw_holes_eurorack_rail_distance` checks screw cutout positions
  against this standard.

- **Patch.Init blank front‑panel Gerbers** (including `blank-NPTH.drl` and
  `blank-Edge_Cuts.gbr`) from Electrosmith’s Patch.Init documentation.
- **KiCad 7** drill output format, as indicated in the NPTH file header, used
  as the source of hole coordinates and diameters.
- **PCBWay design‑rule documentation**, used to set the conservative minimums
  for hole size, spacing, and hole‑to‑edge clearance in
  `test_panel_passes_pcbway_style_validation`.

Expected file formats in this folder:

- `ResynthesisPanel.svg` — single source of truth for the mechanical and
  graphic layout of the panel (mm units).
- `patch_init_gerbers/blank-NPTH.drl` — Excellon NPTH drill file used to
  validate drill positions and sizes.
- `patch_init_gerbers/blank-Edge_Cuts.gbr` and companion Gerbers — reference
  for board outline and original panel.
- `ResynthesisPanel.jpg` — panel preview generated from the SVG by `make`
  (regenerate whenever the SVG changes).
- `ResynthesisPanel_eurorack_overlay.svg` — Eurorack overlay from
  `render_eurorack_overlay.py` (same viewBox as panel; stack in Inkscape for
  HP grid, rail centers, cut annotations).

