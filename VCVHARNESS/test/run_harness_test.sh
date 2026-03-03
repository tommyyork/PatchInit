#!/usr/bin/env bash
# Run VCV Rack headless with the VCVHARNESS plugin for N seconds.
# Same validation a DAW does when loading the VCV Rack VST and running audio.
# Note: The panel graphic (res/PatchSMHarness.svg) is designed for a physical Eurorack module, not VCV Rack.
set -e

RUN_SECONDS="${RUN_SECONDS:-10}"
RACK_BIN="${RACK_BIN}"
RACK_SYSTEM_DIR="${RACK_SYSTEM_DIR}"
TEST_USER_DIR="${TEST_USER_DIR}"
PATCH_VCV="${PATCH_VCV}"
PLUGIN_BUILD_DIR="${PLUGIN_BUILD_DIR}"
# Optional: set to non-empty to run a second Rack pass with --screenshot=1 (requires display)
CAPTURE_SCREENSHOT="${CAPTURE_SCREENSHOT:-}"
# Where to save timestamped screenshot(s); used when CAPTURE_SCREENSHOT=1
OUT_DIR="${OUT_DIR:-$(dirname "$PLUGIN_BUILD_DIR")/out}"

if [[ -z "$RACK_BIN" || -z "$TEST_USER_DIR" || -z "$PATCH_VCV" || -z "$PLUGIN_BUILD_DIR" ]]; then
  echo "Usage: env RACK_BIN=... TEST_USER_DIR=... PATCH_VCV=... PLUGIN_BUILD_DIR=... $0"
  echo "  RACK_BIN         Path to Rack binary (e.g. dep/Rack/Rack)"
  echo "  RACK_SYSTEM_DIR Optional; Rack system dir containing res/ (default: dir of RACK_BIN)"
  echo "  TEST_USER_DIR   Empty user dir; will contain plugins/VCVHARNESS"
  echo "  PATCH_VCV       Path to .vcv patch file (one Patch SM Harness module)"
  echo "  PLUGIN_BUILD_DIR Path to built plugin directory (plugin.json, plugin.so/dylib, res/)"
  exit 1
fi

mkdir -p "$TEST_USER_DIR/plugins/VCVHARNESS"
rm -rf "$TEST_USER_DIR/plugins/VCVHARNESS"
mkdir -p "$TEST_USER_DIR/plugins/VCVHARNESS"
cp "$PLUGIN_BUILD_DIR/plugin.json" "$TEST_USER_DIR/plugins/VCVHARNESS/"
cp -R "$PLUGIN_BUILD_DIR/res" "$TEST_USER_DIR/plugins/VCVHARNESS/"
for ext in so dylib dll; do
  if [[ -f "$PLUGIN_BUILD_DIR/plugin.$ext" ]]; then
    cp "$PLUGIN_BUILD_DIR/plugin.$ext" "$TEST_USER_DIR/plugins/VCVHARNESS/"
    break
  fi
done

if [[ ! -f "$PATCH_VCV" ]]; then
  echo "Patch file not found: $PATCH_VCV"
  exit 1
fi

EXTRA_ARGS=()
if [[ -n "$RACK_SYSTEM_DIR" ]]; then
  EXTRA_ARGS+=(-s "$RACK_SYSTEM_DIR")
fi

# So the Rack binary finds libRack.dylib (macOS)
RACK_BIN_DIR="$(dirname "$RACK_BIN")"
export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:+$DYLD_LIBRARY_PATH:}$RACK_BIN_DIR"

echo "Running Rack headless for ${RUN_SECONDS}s with patch $PATCH_VCV ..."
"$RACK_BIN" -d -h -u "$TEST_USER_DIR" "${EXTRA_ARGS[@]}" --run-seconds="$RUN_SECONDS" "$PATCH_VCV"
HEADLESS_EXIT=$?
if [[ $HEADLESS_EXIT -ne 0 ]]; then
  exit $HEADLESS_EXIT
fi

# Optional: capture screenshot of the VCV unit (Patch SM Harness) via Rack --screenshot (requires display)
if [[ -n "$CAPTURE_SCREENSHOT" ]]; then
  echo "Capturing screenshot of VCV unit (requires display)..."
  mkdir -p "$OUT_DIR"
  SCREENSHOTS_USER="$TEST_USER_DIR/screenshots"
  rm -rf "$SCREENSHOTS_USER"
  "$RACK_BIN" -d -u "$TEST_USER_DIR" "${EXTRA_ARGS[@]}" --screenshot=1 || true
  TIMESTAMP="$(date +%Y-%m-%d_%H-%M-%S)"
  if [[ -d "$SCREENSHOTS_USER/VCVHARNESS" ]]; then
    for f in "$SCREENSHOTS_USER/VCVHARNESS"/*.png; do
      [[ -f "$f" ]] || continue
      base="$(basename "$f" .png)"
      dest="$OUT_DIR/VCVHarness_${base}_${TIMESTAMP}.png"
      cp "$f" "$dest"
      echo "Saved screenshot: $dest"
    done
  else
    echo "Warning: no screenshots found under $SCREENSHOTS_USER/VCVHARNESS (display may be required)"
  fi
fi

exit 0
