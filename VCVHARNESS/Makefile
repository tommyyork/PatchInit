RACK_DIR ?= $(RACK_DIR)

ifeq ($(RACK_DIR),)
$(error RACK_DIR is not set. Point this to your Rack 2 SDK directory, e.g. `export RACK_DIR=/path/to/Rack-SDK`.)
endif

include $(RACK_DIR)/arch.mk

# Optional: give the name of the embedded binary / project you are virtualizing.
# This is used only for naming within the VCV UI and metadata, not for linking
# to the actual MCU firmware image.
VCV_BINARY_NAME ?= Resynthesis

FLAGS += -DVCV_BINARY_NAME=\"$(VCV_BINARY_NAME)\"

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += plugin.json
DISTRIBUTABLES += README.md
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk

