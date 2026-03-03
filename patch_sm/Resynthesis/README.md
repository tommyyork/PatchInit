# Resynthesis

This example implements a phase-vocoder-style resynthesis effect for Daisy Patch SM, inspired by the Resynthesis.hpp code from the All Electric Smart Grid project.

Incoming audio is analyzed into overlapping FFT grains, processed spectrally (phase propagation, V/OCT pitch, spectral flattening, bright/dark tilt, sparsity, and phase diffusion), and resynthesized with overlap-add back to the output. The V/OCT input (0–10 V) sets the fundamental frequency so the module can be used as an oscillator in a bass patch.

## Title: 化 (huà)

The panel uses the single character **化** (*huà*) as its primary title, from the *Daodejing* (道德經) in the sense of **transformation**—the change of one state into another, as in the phrase **化而欲作** (*huà ér yù zuò*, “when transformation arises, desire stirs”; ch. 37). Here it evokes the transformation of incoming sound through the phase vocoder into new timbres and textures.

**Panel typography:** All panel text is set in **all caps** using the open-source DIN-style font **[Gidole](https://github.com/larsenwork/Gidole)** (OFL licence; also available via [Google Fonts](https://fonts.google.com/specimen/Gidole)). The SVG uses the `Gidole` font family with fallbacks (DIN Alternate, DIN 2014, sans-serif) so the panel renders correctly even if Gidole is not installed.

## Controls (CV_1 – CV_8)

- **CV_1 – Dry/Wet**: Crossfade between the original input (dry) and the resynthesized signal (wet). The parameter ranges from **100% dry** (input only) to **100% wet** (resynthesized only).
- **CV_2 – Magnitude Smoothing**: Controls how quickly spectral magnitudes follow the input. Low values track transients closely; high values smear dynamics into pads. The engine’s **default** smoothing is around **0.4**, which tested as a good compromise between transient clarity and “pad‑like” musicality.
- **CV_3 – Spectral Flatten**: Blends each bin’s magnitude toward the frame mean. Higher settings flatten the spectrum for more “noisy”/even energy.
- **CV_4 – Bright/Dark Tilt**: Spectral tilt. Negative values emphasize low bins (darker), positive values emphasize high bins (brighter). Internally, tilt gain is clamped to a safe range so extreme settings do **not** drive the output into clipping or silence (as discovered in early `cv4_tilt` sweeps).
- **CV_5 – V/OCT (0–10 V, 1 V/oct)**: Volt-per-octave pitch control. The algorithm uses available grains to build a fundamental at the frequency specified by the voltage (e.g. 1 V ≈ 32.7 Hz / C1, 2 V ≈ 65.4 Hz / C2) and reinforces the first, second, and third harmonics in decreasing volume so the module can be used as an oscillator in a bass patch. 0 V corresponds to C0 (~16.35 Hz); 10 V reaches the upper range (internally clamped for stability).
- **CV_6 – Time‑Stretch / Grain Density (bipolar, -5 V to +5 V)**: Bipolar control around 1× time. Negative voltages slow down and smear the audio (down to roughly 0.25×), positive voltages increase grain density and motion (up to roughly 4×).
- **CV_7 – Spectral Sparsity (bipolar, -5 V to +5 V)**: Bipolar control mapped to the full **0–1** sparsity range (0 V ≈ 0.5). Lower values keep most bins active and sound fuller; higher values keep only the strongest bins, leading to more pronounced, metallic / ring‑mod‑like spectra (with internal energy preservation so the result stays present rather than just “thin”).
- **CV_8 – Phase Diffusion (bipolar, -5 V to +5 V)**: Bipolar control mapped to the full **0–1** diffusion range (0 V ≈ 0.5). Lower values keep phases more coherent (clearer tone); higher values introduce strong randomization for noisy, cloud‑like, frequency‑shift‑ish textures.

**Switch B_8 ("MAX COMP")**
When the toggle on B_8 is flipped to 1, two things happen: (1) the two banks of CVs are swapped (parameters normally driven by CV_1–CV_4 are then driven by CV_5–CV_8, and vice versa); (2) the compressor switches to **MAX COMP** mode. In MAX COMP mode the compressor uses a **negative compression ratio** (Eventide Omnipressor–style): quiet sections are boosted and peaks are tamed so the output is **near full volume** while **preserving input dynamics and transients**. **V/OCT (CV_5) remains 0–10 V; the bipolar -5 V to +5 V scaling for CV_6–CV_8 is preserved regardless of which bank currently controls which parameter.**

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

5. **Run offline test (optional)**:
   - **Purpose:** The test runs the phase vocoder resynthesis so that the **output length equals the duration of the V/OCT CV movement**: **14 quarter notes at 120 BPM** (7 seconds). The input is truncated or zero-padded to exactly that length, so one output file reflects the full pitch movement. A simulated **V/OCT CV** (0–10 V) steps **once per quarter note** through **two diatonic octaves** (14 steps: 0 V through ~2 V in 1/7 V steps, then scaled to the same diatonic scale). **Dry/wet is fixed at 100% wet** (output is resynthesized only; no dry mix). Other parameters use **musical defaults** chosen through listening tests: smoothing ≈ **0.4**, flatten = 0, tilt = 0, sparsity ≈ **0.2**, diffusion ≈ **0.2**, time scale = 1.0. Output is written to `test/out/<basename>_resynth_processed.wav` (e.g. `test/out/church_bells_resynth_processed.wav`).
   - **Test input sample:** Place a **royalty-free** WAV at `test/samples/church_bells.wav`. The test runs from the Resynthesis directory via `make tests`; the binary loads `samples/church_bells.wav` (i.e. this file).
     - **Format:** WAV, 48 kHz (mono or stereo).
     - **Suggested source:** [BBC Sound Effects](https://sound-effects.bbcrewind.co.uk/) — search for “church bells” (or “bells”, “cathedral”); download a clip and convert to 48 kHz WAV if needed. BBC Sound Effects are made available under the [RemArc licence](https://sound-effects.bbcrewind.co.uk/licensing) for personal, educational or research use.
   - From the project root:
     ```bash
     make tests
     ```
   - **V/OCT CV movement:** The test drives the resynth’s pitch as a volt-per-octave CV (0–10 V) that steps every quarter note at 120 BPM across **two octaves in one pass**: quarter 1 = 0 V (C0), quarter 2 = +2 semitones, …, quarter 8 = +12 st (first octave), quarter 9 = +14 st, …, quarter 14 = +23 st (second octave). The processed sample length is exactly 14/4 at 120 BPM (7 s), so the output file clearly shows this pitch movement in a single file.
   - If you use a sample that requires attribution, add it to the **Attribution** section below.

6. **Run CV parameter sweep test (optional)**:
   - **Purpose:** Process the same input (e.g. church bells) once per CV parameter, sweeping that parameter while keeping the others at neutral. Dry/wet is swept 0% → 100% wet **only** in the first test; all other sweeps run at 100% wet. **Default V/OCT is 2 V (C2)** for all sweeps except the V/OCT test. Produces **8 WAVs** in `test/out/`, each named for the parameter under test:
     - `church_bells_cv1_drywet.wav` — dry/wet 0% → 100% wet
     - `church_bells_cv2_smoothing.wav` — magnitude smoothing 0 → 1 (engine default ≈ 0.4)
     - `church_bells_cv3_flatten.wav` — spectral flatten 0 → 1
     - `church_bells_cv4_tilt.wav` — bright/dark tilt -1 → 1 (with internal tilt gain clamped to avoid clipping/silence)
     - `church_bells_cv5_voct.wav` — V/OCT linear sweep 1 V → 4 V over **30 s** (looped 220 Hz sine; output duration 30 s)
     - `church_bells_cv6_timestretch.wav` — time stretch 0.25× → 4×
     - `church_bells_cv7_sparsity.wav` — spectral sparsity sweep 0 → 1
     - `church_bells_cv8_phase_diffusion.wav` — phase diffusion sweep 0 → 1
   - Uses the same input as the offline test (`samples/church_bells.wav`). From the Resynthesis root:
     ```bash
     make test_cv_sweeps
     ```
     Or run as part of `make tests`.

## Design notes for recent DSP changes

- **Smoothing ≈ 0.4 for musicality**: Early versions used ~0.3 as a neutral smoothing value. Listening tests with bells and broadband material showed that slightly higher smoothing (~0.4) better balances transient clarity with sustained “pad” character, making the effect feel more musical and less brittle when driven hard.
- **Sparsity and diffusion back to 0–1 (with safeguards)**: The bipolar -5 V..+5 V range now maps to **0–1** for both sparsity and phase diffusion (0 V ≈ 0.5), restoring strong ring‑mod / frequency‑shift‑like metallic effects when desired. Spectral‑energy preservation, grain normalization, and tilt‑gain clamping keep these extremes loud and present rather than simply “thin” or silent.
- **Tilt gain clamping**: The bright/dark tilt originally allowed very large boosts/cuts at the top and bottom of the spectrum, which in practice caused clipping or near‑silence in edge cases (e.g. the `church_bells_cv4_tilt.wav` sweep). Internally, tilt gain is now clamped to a moderate range so full‑scale CV sweeps remain usable while still clearly changing timbre.
- **Spectral energy preservation**: After flatten/tilt/sparsity have been applied in the spectral domain, the total energy is renormalized to closely match the pre‑shaping energy. This reduces level jumps when turning controls and makes the resynth output easier to mix.
- **Grain overlap normalization and soft clip**: Wet grains are normalized by the number of active grains plus hop size so the overall level stays similar whether 1 or several grains overlap. A gentle soft clip on the output catches remaining peaks without harsh distortion, again aiming for a more stable and musical perceived loudness.
- **Fixed compressor (last stage)**: After the soft clip, a feedforward compressor with fixed settings runs so the output is **louder and more consistent** as a sound source for a filter and amplitude (e.g. VCF/VCA). Threshold ≈ -12 dB, ratio 2:1, fast attack (~0.3 ms), medium release (~50 ms), and make-up gain so the resynthesized signal sits at a useful level. This avoids the need to crank downstream gain and evens out dynamics so the module works well as an oscillator or texture source. *Alternative considered:* updating the algorithm so output volume closely follows input volume (e.g. envelope-following gain on the wet path); the compressor was chosen for fixed behaviour, predictable level, and one less dependency on input dynamics.
- **Grain launch jitter (“spray”)**: Grain launch timing is dithered around the nominal hop size (±30%), both on hardware and in the offline / CV sweep tests. This breaks the perfectly regular launch grid so grains overlap in a more scattered, “alien” way while still roughly tracking the requested time‑stretch factor.

## Attribution (test audio)

The offline test (`make tests`) uses an input audio file that you provide at `test/samples/church_bells.wav`. If you use a royalty-free sample that requires attribution, add it here. Example for BBC content:

- **Test input sample:** *Church bells* from [BBC Sound Effects](https://sound-effects.bbcrewind.co.uk/), © BBC, used under the [RemArc licence](https://sound-effects.bbcrewind.co.uk/licensing). Used as input to the phase vocoder resynthesis in this project’s test suite.

**Current test sample (fallback):** Church bells from [Soundcamp.org](https://soundcamp.org/sound-effects/church-bells-sound-wav), resampled to 48 kHz mono. You may replace with a BBC Sound Effects clip (search “church bells” at [sound-effects.bbcrewind.co.uk](https://sound-effects.bbcrewind.co.uk/)) for RemArc-licensed content.

