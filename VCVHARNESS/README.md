# VCVHARNESS – Daisy Patch SM Virtualization for VCV Rack

## Overview

`VCVHARNESS` is a small VCV Rack 2 plugin project that **virtualizes a Daisy Patch SM-style firmware** inside Rack.  
It is designed around the `patch_sm/Resynthesis` example (formerly `PhaseVocoder`) in this repository and exposes a **Rack module whose I/O layout matches the Patch SM board**:

- **Stereo audio input**: `IN_L`, `IN_R`
- **Stereo audio output**: `OUT_L`, `OUT_R`
- **8 CV inputs**: `CV_1` … `CV_8`
- **2 CV outputs**: `CV_OUT_1`, `CV_OUT_2`
- **2 switches**: `B7` and `B8` (implemented as buttons)

Internally, the module runs the **same grain-based FFT phase‑vocoder resynthesis algorithm** as `patch_sm/Resynthesis/Resynthesis.cpp`, but compiled directly for desktop and wrapped in VCV Rack instead of the Daisy microcontroller.

This gives you a **“virtual Patch SM + Resynthesis binary”** inside Rack, so you can audition and experiment with the algorithm using Rack’s patching environment, automation, and recording tools.

> **Note on “binary name”**  
> The `VCVHARNESS/Makefile` accepts a variable `VCV_BINARY_NAME`. This is intended as a **human‑readable name for the embedded binary/project** (for example `Resynthesis`, `MyGranulator`, etc.).  
> It is currently used for preprocessor metadata only and **does not attempt to execute a compiled MCU firmware image**—instead, the DSP code is compiled directly for the host CPU.

---

## Project structure

The `VCVHARNESS` folder lives at the **root of the DaisyExamples repository**. It contains:

| Path | Description |
|------|--------------|
| `Makefile` | Builds the VCV Rack 2 plugin; requires `RACK_DIR` and optionally `VCV_BINARY_NAME`. |
| `plugin.json` | VCV Rack plugin manifest (slug `VCVHARNESS`, module `PatchSMHarness`). |
| `src/VCVPatchSMHarness.cpp` | Single source file: Rack module, widget, and the virtualized Patch SM DSP (grain resynthesis / phase‑vocoder core). |
| `res/PatchSMHarness.svg` | Panel graphic: **designed for a physical Eurorack module** (front-panel fabrication); same file is used for display in VCV Rack. |
| `README.md` | This documentation. |
| `dep/` | Rack source (submodule or clone) and **`standalone-run-seconds.cpp`** (patched standalone for headless run-seconds). |
| `test/` | Test patch (`patch/patch.json`), `.vcv` builder, and **`run_harness_test.sh`** to run Rack headless with the plugin. |
| `Makefile.test` | Makefile for the **`make -f Makefile.test test`** target. |

After a successful `make`, the build system produces the plugin bundle (e.g. under `build/` or the current directory, depending on the Rack SDK). That folder is what you install into Rack’s plugins directory.

---

## How the virtualization works

- **I/O layout** matches Patch SM + `Resynthesis.cpp`: stereo audio in/out, eight CV inputs, two CV outputs, and two switches (B7, B8), with the same **B8 CV‑bank swap** behavior as on the hardware.
- **DSP core**: The grain resynthesis / phase‑vocoder algorithm from the embedded example is **reimplemented directly** in `src/VCVPatchSMHarness.cpp` (same FFT, grains, spectral shaping, pitch shift, etc.). It is **not** linking to or executing an STM32 binary.
- **Execution model**: On the hardware, `patch.StartAudio(AudioCallback)` drives the audio callback; in Rack, the same logical processing runs inside **`Module::process()`**, with Rack CV inputs replacing ADC reads and Rack outputs replacing `patch.WriteCvOut()`.
- **Result**: A **host‑native** virtual Patch SM running the Resynthesis algorithm inside VCV Rack. The make variable **`VCV_BINARY_NAME`** is only **metadata/labeling** for which embedded project this harness represents; it does **not** load or run an MCU firmware file.

---

## Test: dummy VCV Rack environment (10+ seconds)

A test runs the plugin in a **dummy VCV Rack environment** for at least 10 seconds, doing the same validation the **VCV Rack VST** gets in a DAW: load plugin → create module → run engine for a sustained time.

### Requirements

- **Rack source** in `VCVHARNESS/dep/Rack` (see **dep/README.md** for submodule or clone instructions).
- **`zstd`** to build the test patch (e.g. `brew install zstd` on macOS).

### Run the test

From `VCVHARNESS`:

```bash
make -f Makefile.test test
```

Optionally override the run duration (default 10 seconds):

```bash
make -f Makefile.test test RUN_SECONDS=15
```

To also capture a **timestamped screenshot** of the Patch SM Harness module and save it under `VCVHARNESS/out/`, use (requires a display):

```bash
make -f Makefile.test test-with-screenshot
```

Screenshots are named like `VCVHarness_PatchSMHarness_YYYY-MM-DD_HH-MM-SS.png`. The `out/` folder is listed in `.gitignore`.

### What it does

1. Copies **`dep/standalone-run-seconds.cpp`** into Rack's adapter so the standalone supports **`--run-seconds=N`** in headless mode.
2. Builds Rack (deps, libRack, standalone).
3. Builds the VCVHARNESS plugin with `RACK_DIR=dep/Rack`.
4. Builds a minimal **`.vcv`** patch that contains one **Patch SM Harness** module.
5. Runs **Rack headless** (`-d -h`) with a dedicated user dir, the built plugin, and the patch for **10 seconds**.
6. Exits **0** on success; non-zero on crash or failure.

So the plugin is loaded the same way as in the Rack app or VST, the engine runs with the module in the rack, and **`Module::process()`** is called every block for the full duration. Any init bug, crash in `process()`, or invalid I/O will cause the test to fail.

---

## Building the VCVHARNESS plugin

### 1. Install the Rack 2 SDK

1. Download the **VCV Rack 2 SDK** from the official site (Developer / SDK section).
2. Unpack it somewhere on disk, e.g.:
   - macOS: `/Users/you/SDK/Rack-SDK`

### 2. Point `RACK_DIR` at the SDK

From the root of this repository (`DaisyExamples`), set:

```bash
export RACK_DIR=/absolute/path/to/Rack-SDK
```

You only need to do this once per shell session.

### 3. Build the plugin

From the repo root:

```bash
cd VCVHARNESS
make
```

To label the module with a specific **binary/project name** (for example, “Resynthesis”), pass:

```bash
make VCV_BINARY_NAME=Resynthesis
```

The resulting plugin folder can then be copied or symlinked into your Rack 2 user `plugins` directory as usual.

### 4. Install and load the plugin in VCV Rack

1. **Locate the built plugin**  
   After `make`, the Rack SDK typically leaves the built plugin in a folder such as `build/` inside `VCVHARNESS`, or in the current directory. The folder will contain `plugin.json`, `plugin.so` / `plugin.dylib` / `plugin.dll`, and the `res/` assets.

2. **Install into Rack**  
   Copy or symlink that **entire plugin folder** into your VCV Rack 2 user plugins directory, for example:
   - **macOS**: `~/Documents/Rack2/plugins/`
   - **Linux**: `~/.local/share/Rack2/plugins/` or as shown in Rack’s *File → Open user folder*
   - **Windows**: `%USERPROFILE%\Documents\Rack2\plugins\`  
   The folder name (e.g. `VCVHARNESS`) becomes the plugin directory Rack scans.

3. **Load the module**  
   Start (or restart) VCV Rack 2. In the module browser, find the plugin **“VCV Daisy Patch SM Harness”** and add the module **“Patch SM Harness”**. That module is the virtualized Patch SM running the Resynthesis algorithm.

---

## Module interface and “virtual Patch SM” mapping

The `Patch SM Harness` module is intended to **mirror the signal and control layout of the Daisy Patch SM board** as used in `Resynthesis.cpp`.

- **Audio**
  - `Audio In L/R` → corresponds to the Patch SM’s stereo inputs.
  - `Audio Out L/R` → corresponds to the Patch SM’s stereo outputs.

- **CV Inputs (0–10 V, normalized to 0–1 internally)**
  - `CV_1` → Dry/Wet
  - `CV_2` → Spectral smoothing amount
  - `CV_3` → Spectral flatten amount
  - `CV_4` → Bright/Dark tilt
  - `CV_5` → Pitch (1.2 V/oct‑style semitone mapping)
  - `CV_6` → Time‑stretch / grain density
  - `CV_7` → Spectral sparsity
  - `CV_8` → Phase diffusion

- **CV‑bank swap (B8)**
  - Button `B8` toggles the same **bank swap** behavior as in the embedded firmware:
    - When **off**: `CV_1..4` → Dry/Wet, smoothing, flatten, tilt; `CV_5..8` → Pitch, time, sparsity, diffusion.
    - When **on**: the banks are swapped, so `CV_5..8` drive the first group and `CV_1..4` drive the second.

- **CV Outputs (0–5 V)**
  - `CV_OUT_1` → Smoothed spectral energy (RMS per frame) mapped to 0–5 V.
  - `CV_OUT_2` → Unsmoothed spectral energy (per‑frame RMS) mapped to 0–5 V.

- **B7 – Reverb toggle**
  - `B7` is exposed as a button and reserved for the reverb/Plateau behavior from the embedded example.
  - The current harness keeps the core resynthesis dry/wet and spectral controls; you can extend the module to add a Rack‑native reverb here if you want a closer match to the hardware’s Plateau effect.

In short, the **audio/CV wiring and control semantics** are designed to feel like plugging into the Patch SM + Resynthesis firmware, but inside Rack.

---

## Design notes and limitations

- **Panel design is for physical Eurorack**
  - The panel graphic **`res/PatchSMHarness.svg`** is explicitly designed for a **physical Eurorack module** (e.g. front-panel fabrication, PCB silkscreen, or faceplate printing), not for VCV Rack. The same file is used as the module’s panel in VCV Rack for display only. Layout, line weights, and the cell pattern are chosen for clear printing and vector fabrication.

- **No MCU binary execution**  
  - Running an STM32 firmware binary directly inside VCV Rack would require a **full CPU and hardware emulation layer**, which is out of scope for this harness.
  - Instead, the **DSP core (FFT + grains + spectral shaping)** is compiled natively for the host, and the Rack module **mimics the behavior and control mapping** of the Patch SM example.

- **“Same interface as `patch.Init()`”**  
  - The embedded `Resynthesis.cpp` ties into Daisy via:
    - `patch.Init()`, `patch.StartAudio(AudioCallback)`,
    - `patch.GetAdcValue(CV_n)`, `patch.WriteCvOut(CV_OUT_n)`,
    - and switches `B7`, `B8`.
  - The harness reproduces these **conceptual hooks**:
    - Audio callback → Rack’s `Module::process()`.
    - ADC reads → Rack CV inputs normalized to 0–1.
    - CV outputs → Rack output ports in 0–5 V.
    - B7/B8 switches → Rack `LEDButton` parameters.

- **Binary name vs. project code**
  - `VCV_BINARY_NAME` is meant as a **label** corresponding to the Daisy project or binary you are virtualizing (for example, the `Resynthesis` firmware you flash to Patch SM).
  - The harness does **not parse or run the MCU `.bin` or `.elf`**; if you want to virtualize a different Daisy project you will need to:
    1. Extract or adapt its DSP core into a form similar to `SimpleResynth`.
    2. Map its controls onto the same (or a similar) Patch SM interface.
    3. Integrate that into a new module or extend this one.

---

## Extending the harness

You can evolve `VCVHARNESS` in several directions:

- **Additional Patch SM firmwares**
  - Add new modules alongside `PatchSMHarness` for other Patch SM examples.
  - Share common “virtual Patch SM” plumbing (audio/CV mapping) and plug in different DSP cores.

- **Closer hardware matching**
  - Implement a Rack‑native approximation of the **Plateau** reverb and wire it to `B7`.
  - Model other Patch SM hardware features if you use them in your firmware (e.g. LEDs, additional switches).

- **Richer Rack integration**
  - The panel SVG is designed for **physical Eurorack** (front-panel fabrication); for VCV-only use you could add a more detailed SVG, custom knobs, and preset browser integration.

---

## References and resources used

- **Existing Daisy example in this repo**
  - `patch_sm/Resynthesis/Resynthesis.cpp`
  - `patch_sm/Resynthesis/Plateau.*` (for algorithmic inspiration only; the Rack harness does not depend on libDaisy/DaisySP directly).

- **Original Resynthesis inspiration**
  - All Electric Smart Grid project by jvictor0  
    (see `Resynthesis.hpp` referenced in the `Resynthesis.cpp` header comments).

- **VCV Rack 2 plugin development documentation**
  - Official Rack 2 manual sections on **Building** and **Plugin development**, and example plugins such as `VCV-Recorder` (for Makefile and `plugin.json` structure).

These resources guided the design so that the harness stays as close as is practical to the behavior of the Patch SM Resynthesis firmware while remaining simple to build and extend on desktop.

