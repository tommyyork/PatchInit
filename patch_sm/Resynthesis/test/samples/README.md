# Test input samples

Place **royalty-free** WAVs here for the resynthesis offline and CV-sweep tests. See the main project **README** (`patch_sm/Resynthesis/README.md`) for full instructions.

- **`church_bells.wav`**: Cathedral bells test input (48 kHz mono). Default for offline resynth and CV sweeps.
  - **Preferred source:** [BBC Sound Effects](https://sound-effects.bbcrewind.co.uk/) — search for “church bells” (or “bells”, “cathedral”). Downloads are available under the [RemArc licence](https://sound-effects.bbcrewind.co.uk/licensing) for personal, educational or research use. Convert to 48 kHz WAV if needed.
  - **Current sample:** Church bells from [Soundcamp](https://soundcamp.org/sound-effects/church-bells-sound-wav) (resampled to 48 kHz mono). You may replace with a BBC clip for consistency with the suggested source.

- **`synth_bassline.wav`**: Generated monophonic synth bassline (~18 s, 48 kHz mono). Used as an additional input for the **CV sweep** tests (`make test_cv_sweeps`), which now run on both `church_bells.wav` and `synth_bassline.wav`.

- **`mother32_filter_sweep.wav`** (optional): A short slice from a **Moog Mother-32** demo (e.g. from YouTube or another source) for CV sweep tests. Ideal content: **multiple notes** and **varying low-pass filter cutoff**. The CV sweep test uses the **first 7 seconds** of each input, so a 7 s slice is enough; longer files are truncated. You must obtain and convert the audio yourself (see below).

- **Format:** WAV, 48 kHz (mono or stereo). Standard PCM; 16-, 24-, or 32-bit supported. WAVs with metadata chunks (e.g. LIST) are supported.
- **Offline test behavior:** The offline test uses **100% wet** (resynthesized signal only; no dry mix). Output is written to `out/<basename>_resynth_processed.wav`.
- **Attribution:** If your chosen sample requires attribution, add it in the main README’s **Attribution** section.

**How to add a Mother-32 slice (e.g. for `mother32_filter_sweep.wav`):**  
Download or record a Mother-32 demo (e.g. “Moog Mother-32 demo” or “Mother-32 filter sweep”) from a platform you’re allowed to use. Choose a segment with **multiple notes** and **filter cutoff movement**. Save the segment as a local file (e.g. `~/Downloads/mother32_clip.mp4` or `.webm`). Then extract a **7 second** slice at 48 kHz mono WAV (or 20 s if you want extra material; the test uses the first 7 s):

```bash
# Replace INPUT with your file path; adjust -ss (start time) and -t (duration) as needed.
ffmpeg -ss 0 -i INPUT -t 7 -ar 48000 -ac 1 -c:a pcm_s16le \
  -y "/Users/thomasyork/Desktop/DaisyExamples/patch_sm/Resynthesis/test/samples/mother32_filter_sweep.wav"
```

Then from the Resynthesis `test/` directory run:  
`./test_resynth_cv_sweeps samples/mother32_filter_sweep.wav`  
(or add this sample to the Makefile so `make test_cv_sweeps` runs it too).
