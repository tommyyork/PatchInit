# Resynthesis

This example implements a phase-vocoder-style resynthesis effect for Daisy Patch SM, inspired by the Resynthesis.hpp code from the All Electric Smart Grid project.

Incoming audio is analyzed into overlapping FFT grains, processed spectrally (phase propagation, pitch shift, spectral flattening, bright/dark tilt, sparsity, and phase diffusion), and resynthesized with overlap-add back to the output.

## Controls (CV_1 – CV_8)

- **CV_1 – Dry/Wet**: Crossfade between the original input (dry) and the resynthesized signal (wet).
- **CV_2 – Magnitude Smoothing**: Controls how quickly spectral magnitudes follow the input. Low values track transients closely; high values smear dynamics into pads.
- **CV_3 – Spectral Flatten**: Blends each bin’s magnitude toward the frame mean. Higher settings flatten the spectrum for more “noisy”/even energy.
- **CV_4 – Bright/Dark Tilt**: Spectral tilt. Negative values emphasize low bins (darker), positive values emphasize high bins (brighter).
- **CV_5 – Pitch Shift (bipolar, -5 V to +5 V)**: Bipolar pitch CV. -5 V corresponds to a large downward pitch shift, +5 V to a large upward shift, and 0 V is unison. Internally this is mapped to a wide semitone range using a phase‑vocoder pitch ratio.
- **CV_6 – Time‑Stretch / Grain Density (bipolar, -5 V to +5 V)**: Bipolar control around 1× time. Negative voltages slow down and smear the audio (down to roughly 0.25×), positive voltages increase grain density and motion (up to roughly 4×).
- **CV_7 – Spectral Sparsity (bipolar, -5 V to +5 V)**: Bipolar control mapped to 0–1. At 0 V the sparsity is centered; negative voltages reduce sparsity, positive voltages increase it so only the strongest bins remain.
- **CV_8 – Phase Diffusion (bipolar, -5 V to +5 V)**: Bipolar control mapped to 0–1. At 0 V diffusion is moderate; negative voltages reduce diffusion for clearer tone, positive voltages increase diffusion for noisy, cloud‑like textures.

**Switch B_8 (CV swap)**  
When the toggle on B_8 is flipped to 1, the two banks of CVs are swapped: parameters normally driven by CV_1–CV_4 are then driven by CV_5–CV_8, and vice versa. The **bipolar -5 V to +5 V scaling for CV_5–CV_8 is preserved regardless of which bank currently controls which parameter.**

**Switch B_7 (Plateau reverb)**  
Toggles a large-hall, plate-style reverb (implemented in `Plateau.cpp` using DaisySP’s `ReverbSc`). When B_7 is off, the output is completely dry (only the resynth signal). When B_7 is on, the signal is mixed 50/50 between dry and the Plateau reverb.

## Instructions

1. **Initialize submodules**:
   ```bash
   git submodule update --init --recursive
   ```
2. **Build the project (normal / release)**:
   ```bash
   make
   ```
3. **Program the device (optional)**:
   ```bash
   make program
   ```

4. **Debug build with JTAG/serial logging (optional)**:
   - Start OpenOCD in one terminal:
     ```bash
     make openocd
     ```
   - In another terminal, build and start a debug session:
     ```bash
     make debug
     ```
   In debug builds the firmware enables Daisy’s logger and:
   - Prints the startup values of all CV_1–CV_8 inputs and the states of switches B_7 and B_8.
   - Periodically prints diagnostic information while audio runs (active grains, spectral energy, current control values, etc.) over the JTAG/serial link.

