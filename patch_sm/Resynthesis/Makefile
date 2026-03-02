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

