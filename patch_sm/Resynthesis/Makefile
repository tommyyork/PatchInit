# Project Name
TARGET = Resynthesis

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = Resynthesis.cpp \
              Plateau.cpp \

# Library Locations (use local submodules)
LIBDAISY_DIR = ./libDaisy
DAISYSP_DIR = ./DaisySP

# Ensure libraries are built before building this project
all: build_libdaisy build_daisysp

build_libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

build_daisysp:
	$(MAKE) -C $(DAISYSP_DIR)


# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Panel preview: regenerate JPEG from SVG when SVG is newer (run make to update).
# Requires Inkscape. Set INKSCAPE=/path/to/inkscape if not in PATH (e.g. macOS: /Applications/Inkscape.app/Contents/MacOS/inkscape).
INKSCAPE ?= inkscape
all: panel/ResynthesisPanel.jpg
panel/ResynthesisPanel.jpg: panel/ResynthesisPanel.svg
	@echo "Generating panel preview from SVG..."
	$(INKSCAPE) panel/ResynthesisPanel.svg --export-type=jpeg --export-filename=panel/ResynthesisPanel.jpg

# Offline test (host build): run phase vocoder on a WAV with diatonic pitch steps.
# CV sweep test: run 8 tests, one per CV parameter, with descriptive output filenames.
# Panel test: run panel alignment / mechanical checks (Python).
# Run all when you invoke make tests from the Resynthesis module root.
PYTHON ?= python3

tests: test_resynth test_cv_sweeps test_panel

test_resynth:
	$(MAKE) -C test run

test_cv_sweeps:
	$(MAKE) -C test cv_sweeps

test_panel:
	$(PYTHON) panel/test_panel_alignment.py

