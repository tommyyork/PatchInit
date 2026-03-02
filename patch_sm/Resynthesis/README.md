# Resynthesis

This example implements a phase-vocoder-style resynthesis effect for Daisy Patch SM, inspired by the Resynthesis.hpp code from the All Electric Smart Grid project.

Incoming audio is analyzed into overlapping FFT grains, processed spectrally (phase propagation, pitch shift, spectral flattening, bright/dark tilt, sparsity, and phase diffusion), and resynthesized with overlap-add back to the output.

## Controls (CV_1 – CV_8)

- **CV_1 – Dry/Wet**: Crossfade between the original input (dry) and the resynthesized signal (wet).
- **CV_2 – Magnitude Smoothing**: Controls how quickly spectral magnitudes follow the input. Low values track transients closely; high values smear dynamics into pads.
- **CV_3 – Spectral Flatten**: Blends each bin’s magnitude toward the frame mean. Higher settings flatten the spectrum for more “noisy”/even energy.
- **CV_4 – Bright/Dark Tilt**: Spectral tilt. Negative values emphasize low bins (darker), positive values emphasize high bins (brighter).
- **CV_5 – Pitch Shift (1.2 V/oct)**: Controls a phase‑vocoder pitch ratio derived from a 1.2 V/oct CV. Around mid‑range corresponds to unison; up/down shifts pitch in semitones.
- **CV_6 – Time‑Stretch / Grain Density**: Changes how often grains are launched relative to the analysis hop. Lower values smear and “freeze” audio; higher values increase grain density and motion.
- **CV_7 – Spectral Sparsity**: Removes bins below a threshold relative to the strongest bin. Higher values keep only the most energetic components for hollow, formant‑like, or vocoder‑style tones.
- **CV_8 – Phase Diffusion**: Adds random phase offsets (stronger at higher frequencies). Low values keep a clear, pitched sound; high values produce noisy, cloud‑like textures.

**Switch B_8 (CV swap)**  
When the toggle on B_8 is flipped to 1, the two banks of CVs are swapped: parameters normally driven by CV_1–CV_4 are then driven by CV_5–CV_8, and vice versa. So with B_8 on, CV_5 = dry/wet, CV_6 = smoothing, CV_7 = flatten, CV_8 = bright/dark, and CV_1–CV_4 = pitch, time-stretch, sparsity, phase diffusion.

**Switch B_7 (Plateau reverb)**  
Toggles a large-hall, plate-style reverb (implemented in `Plateau.cpp` using DaisySP’s `ReverbSc`). When B_7 is off, the output is completely dry (only the resynth signal). When B_7 is on, the signal is mixed 50/50 between dry and the Plateau reverb.

## Instructions

1. **Initialize submodules**:
   ```bash
   git submodule update --init --recursive
   ```
2. **Build the project**:
   ```bash
   make
   ```
3. **Program the device (optional)**:
   ```bash
   make program
   ```

