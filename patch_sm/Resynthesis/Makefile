# Project Name
TARGET = Resynthesis

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = Resynthesis.cpp \
              Plateau.cpp \
              Compression.cpp \

# Library Locations (use local submodules)
LIBDAISY_DIR = ./libDaisy
DAISYSP_DIR = ./DaisySP

# Ensure libraries are built before building this project
# `all` builds:
# - libDaisy and DaisySP
# - the firmware target (from the core Makefile)
# - host test binaries (via the `test` subdirectory)
# - panel preview PNGs
# - SVG/PDF block-diagram documentation
all: build_libdaisy build_daisysp panel svg test_binaries

build_libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

build_daisysp:
	$(MAKE) -C $(DAISYSP_DIR)


# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Panel previews: generate PNGs from the SVG panel and Eurorack overlay.
# Requires Inkscape. Set INKSCAPE=/path/to/inkscape if not in PATH (e.g. macOS: /Applications/Inkscape.app/Contents/MacOS/inkscape).
INKSCAPE ?= inkscape

.PHONY: panel svg tests test_resynth test_cv_sweeps test_fluff test_panel resynthesis-clean clean

# High-level panel target:
# 1. Regenerate the canonical ResynthesisPanel.svg from the generator script.
# 2. Regenerate the Eurorack overlay SVG from the panel SVG.
# 3. Export PNGs for the panel and the overlay.
panel: panel/ResynthesisPanel.svg panel/ResynthesisPanel_eurorack_overlay.svg panel/ResynthesisPanel.png panel/ResynthesisPanel_eurorack_overlay.png

# Generate the canonical panel SVG from Python.
panel/ResynthesisPanel.svg: panel/generate_resynthesis_panel_svg.py
	@echo "Generating canonical ResynthesisPanel.svg from generator..."
	$(PYTHON) panel/generate_resynthesis_panel_svg.py

# Generate Eurorack overlay SVG (panel + HP grid + rail centers + annotations).
panel/ResynthesisPanel_eurorack_overlay.svg: panel/ResynthesisPanel.svg panel/render_eurorack_overlay.py
	@echo "Generating Eurorack overlay SVG..."
	$(PYTHON) panel/render_eurorack_overlay.py panel/ResynthesisPanel.svg -o panel/ResynthesisPanel_eurorack_overlay.svg

# Ensure the Eurorack overlay SVG is up to date before exporting PNGs from the SVGs.
panel/ResynthesisPanel.png: panel/ResynthesisPanel.svg panel/ResynthesisPanel_eurorack_overlay.svg
	@echo "Generating panel PNG from SVG (1200 dpi, high-res)..."
	$(INKSCAPE) panel/ResynthesisPanel.svg --export-type=png --export-dpi=1200 --export-filename=panel/ResynthesisPanel.png

panel/ResynthesisPanel_eurorack_overlay.png: panel/ResynthesisPanel_eurorack_overlay.svg
	@echo "Generating Eurorack overlay PNG (1200 dpi, high-res)..."
	$(INKSCAPE) panel/ResynthesisPanel_eurorack_overlay.svg --export-type=png --export-dpi=1200 --export-filename=panel/ResynthesisPanel_eurorack_overlay.png

# Offline test (host build): run phase vocoder on a WAV with diatonic pitch steps.
# CV sweep test: run 8 tests, one per CV parameter, with descriptive output filenames.
# Panel test: run panel alignment / mechanical checks (Python).
# Run all when you invoke make tests from the Resynthesis module root.
PYTHON ?= python3
DOC_DIR := doc

# Build-only target for host test binaries (does not run tests).
test_binaries:
	$(MAKE) -C test all

tests: test_resynth test_cv_sweeps test_fluff test_panel

test_resynth:
	$(MAKE) -C test run

test_cv_sweeps:
	$(MAKE) -C test cv_sweeps

test_fluff:
	$(MAKE) -C test fluff

test_panel:
	$(PYTHON) panel/test_panel_alignment.py

# Documentation: US Letter–sized block diagram of the audio path and controls.
# Invoke from the Resynthesis module root:
#   make svg
.PHONY: svg
svg:
	$(PYTHON) $(DOC_DIR)/generate_block_diagram.py

resynthesis-clean:
	rm -rf build
	rm -rf test/out
	rm -f panel/ResynthesisPanel.svg \
	      panel/ResynthesisPanel.png \
	      panel/ResynthesisPanel_eurorack_overlay.png \
	      panel/ResynthesisPanel_eurorack_overlay.svg
	rm -f $(DOC_DIR)/Resynthesis_BlockDiagram_USLetter.pdf \
	      $(DOC_DIR)/Resynthesis_BlockDiagram_USLetter.svg \
	      $(DOC_DIR)/Resynthesis_BlockDiagram.dot
	$(MAKE) -C test clean

clean: resynthesis-clean
