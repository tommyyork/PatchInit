// Grain resynth for Daisy Patch SM.
//
// Inspired by the Resynthesis algorithm in the All Electric Smart Grid
// project by jvictor0:
//   https://github.com/jvictor0/theallelectricsmartgrid
//   (see private/src/Resynthesis.hpp)
//
// This implementation (FFT-based grains, phase propagation, spectral
// shaping, pitch shift, flatten, bright/dark) was written by GPT 5.1
// in Cursor in March 2026.

#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "Plateau.h"

#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ----------------------------------------------------------------------
// Optional debug logging over JTAG/serial (enabled when DEBUG is set)
// ----------------------------------------------------------------------
#ifdef DEBUG
#define RES_DEBUG 1
#endif

#ifdef RES_DEBUG
#define RES_DEBUG_PRINTLN(...) patch.PrintLine(__VA_ARGS__)
#define RES_DEBUG_PRINT(...) patch.Print(__VA_ARGS__)
#else
#define RES_DEBUG_PRINTLN(...)
#define RES_DEBUG_PRINT(...)
#endif

// ----------------------------------------------------------------------
// Helpers for bipolar CV scaling (-5 V .. +5 V)
// ----------------------------------------------------------------------

static inline float CvToBipolar(float v)
{
    // Daisy Patch SM CV inputs are bipolar -5..+5 V and exposed as 0..1.
    // Map 0..1 -> -1..1 where -1 ≈ -5 V, 0 ≈ 0 V, +1 ≈ +5 V.
    return v * 2.0f - 1.0f;
}

// ----------------------------------------------------------------------
// Simple complex type and FFT implementation (radix-2 Cooley-Tukey)
// ----------------------------------------------------------------------

struct Complex
{
    float re;
    float im;
};

static void FftInPlace(Complex *data, size_t n, bool inverse)
{
    // Bit-reversal permutation
    size_t j = 0;
    for(size_t i = 1; i < n; ++i)
    {
        size_t bit = n >> 1;
        while(j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if(i < j)
        {
            Complex tmp = data[i];
            data[i]     = data[j];
            data[j]     = tmp;
        }
    }

    // Cooley-Tukey stages
    for(size_t len = 2; len <= n; len <<= 1)
    {
        float  ang   = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        if(!inverse)
            ang = -ang;
        float   wlenRe = cosf(ang);
        float   wlenIm = sinf(ang);
        for(size_t i = 0; i < n; i += len)
        {
            float wRe = 1.0f;
            float wIm = 0.0f;
            for(size_t j2 = 0; j2 < len / 2; ++j2)
            {
                Complex u = data[i + j2];
                Complex v;
                v.re = data[i + j2 + len / 2].re * wRe
                       - data[i + j2 + len / 2].im * wIm;
                v.im = data[i + j2 + len / 2].re * wIm
                       + data[i + j2 + len / 2].im * wRe;

                data[i + j2].re = u.re + v.re;
                data[i + j2].im = u.im + v.im;
                data[i + j2 + len / 2].re = u.re - v.re;
                data[i + j2 + len / 2].im = u.im - v.im;

                // w *= wlen
                float nextWRe = wRe * wlenRe - wIm * wlenIm;
                float nextWIm = wRe * wlenIm + wIm * wlenRe;
                wRe           = nextWRe;
                wIm           = nextWIm;
            }
        }
    }

    // Scale for inverse transform
    if(inverse)
    {
        float invN = 1.0f / static_cast<float>(n);
        for(size_t i = 0; i < n; ++i)
        {
            data[i].re *= invN;
            data[i].im *= invN;
        }
    }
}

// ----------------------------------------------------------------------
// Phase-vocoder style resynthesizer
// ----------------------------------------------------------------------

static constexpr size_t kFftBits     = 8; // 2^8 = 256
static constexpr size_t kFftSize     = 1 << kFftBits;
static constexpr size_t kHopDenom    = 4;
static constexpr size_t kHopSize     = kFftSize / kHopDenom;
static constexpr size_t kNumBins     = kFftSize / 2;
static constexpr size_t kNumGrains   = 4;
static constexpr float  kTwoPi       = 2.0f * static_cast<float>(M_PI);

struct Grain
{
    float  buffer[kFftSize];
    size_t index;
    bool   running;

    void Start()
    {
        index   = 0;
        running = true;
    }

    float Process()
    {
        if(!running)
            return 0.0f;

        float v = buffer[index];
        ++index;
        if(index >= kFftSize)
        {
            running = false;
            index   = 0;
        }
        return v;
    }
};

struct SimpleResynth
{
    float window[kFftSize];

    float prev_phase[kNumBins + 1];
    float synth_phase[kNumBins + 1];
    float mag_smooth[kNumBins + 1];

    bool  primed;
    float mag_smooth_coeff;
    float pitch_ratio;       // 1.f = unison, 2.f = +12st, 0.5f = -12st
    float spectral_flatten;   // 0 = no change, 1 = fully flat
    float bright_dark;       // -1 = dark, 0 = neutral, +1 = bright
    float sparsity;          // 0 = full spectrum, 1 = only strongest bins
    float phase_diffusion;   // 0 = coherent phase, 1 = noisy phase
    float last_frame_spectral_energy;  // RMS of mag_smooth (0..1 scale), for CV_OUT_1

    void Init()
    {
        // Hann window
        for(size_t n = 0; n < kFftSize; ++n)
        {
            window[n] = 0.5f
                        * (1.0f
                           - cosf(kTwoPi * static_cast<float>(n)
                                  / static_cast<float>(kFftSize - 1)));
        }

        for(size_t i = 0; i <= kNumBins; ++i)
        {
            prev_phase[i]  = 0.0f;
            synth_phase[i] = 0.0f;
            mag_smooth[i]  = 0.0f;
        }
        primed            = false;
        mag_smooth_coeff  = 0.3f;
        pitch_ratio       = 1.0f;
        spectral_flatten  = 0.0f;
        bright_dark       = 0.0f;
        sparsity          = 0.0f;
        phase_diffusion   = 0.0f;
        last_frame_spectral_energy = 0.0f;
    }

    void SetSmoothing(float alpha)
    {
        mag_smooth_coeff = fclamp(alpha, 0.0f, 1.0f);
    }

    void SetPitchRatio(float ratio)
    {
        pitch_ratio = fclamp(ratio, 0.25f, 4.0f);
    }

    void SetSpectralFlatten(float amount)
    {
        spectral_flatten = fclamp(amount, 0.0f, 1.0f);
    }

    void SetBrightDark(float tilt)
    {
        bright_dark = fclamp(tilt, -1.0f, 1.0f);
    }

    void SetSparsity(float amount)
    {
        sparsity = fclamp(amount, 0.0f, 1.0f);
    }

    void SetPhaseDiffusion(float amount)
    {
        phase_diffusion = fclamp(amount, 0.0f, 1.0f);
    }

    static float PrincArg(float x)
    {
        // x is in cycles
        x = x - floorf(x);
        if(x > 0.5f)
            x -= 1.0f;
        return x;
    }

    static float RandUniform(float lo, float hi)
    {
        static uint32_t state = 1u;
        state                 = state * 1664525u + 1013904223u;
        float t = static_cast<float>(state & 0x00FFFFFFu)
                  / static_cast<float>(0x01000000u);
        return lo + (hi - lo) * t;
    }

    void StartGrainFromHistory(const float *history, size_t history_write_pos, Grain &grain)
    {
        // Build windowed frame from ring buffer into spectrum
        Complex spectrum[kFftSize];

        size_t idx = history_write_pos;
        for(size_t n = 0; n < kFftSize; ++n)
        {
            float s = history[idx];
            spectrum[n].re = s * window[n];
            spectrum[n].im = 0.0f;
            idx            = (idx + 1) % kFftSize;
        }

        FftInPlace(spectrum, kFftSize, false);

        // Analysis and phase propagation (basic phase vocoder)
        for(size_t k = 0; k <= kNumBins; ++k)
        {
            float re = spectrum[k].re;
            float im = spectrum[k].im;

            float mag   = sqrtf(re * re + im * im);
            float phase = atan2f(im, re) / kTwoPi; // cycles

            if(!primed)
            {
                prev_phase[k]  = phase;
                synth_phase[k] = phase;
                mag_smooth[k]  = mag;
                continue;
            }

            float omega_bin      = static_cast<float>(k) / static_cast<float>(kFftSize);
            float delta_expected = omega_bin * static_cast<float>(kHopSize);
            float delta          = phase - prev_phase[k];
            delta -= delta_expected;
            delta = PrincArg(delta);

            float omega_instant = omega_bin + delta / static_cast<float>(kHopSize);

            // Magnitude smoothing roughly like SlewUp
            mag_smooth[k] = mag_smooth[k]
                            + mag_smooth_coeff * (mag - mag_smooth[k]);

            synth_phase[k] = synth_phase[k] + omega_instant * static_cast<float>(kHopSize);
            prev_phase[k]  = phase;
        }

        primed = true;

        // --- Spectral shaping: flatten, bright/dark, sparsity, phase diffusion ---
        float sum_mag = 0.0f;
        float max_mag = 0.0f;
        for(size_t k = 1; k < kNumBins; ++k)
        {
            sum_mag += mag_smooth[k];
            if(mag_smooth[k] > max_mag)
                max_mag = mag_smooth[k];
        }
        float mean_mag = sum_mag / static_cast<float>(kNumBins > 1 ? kNumBins - 1 : 1);

        for(size_t k = 0; k <= kNumBins; ++k)
        {
            // Spectral flatten: blend toward equal magnitude
            mag_smooth[k] = mag_smooth[k] * (1.0f - spectral_flatten)
                            + mean_mag * spectral_flatten;
            // Bright/dark tilt: gain proportional to bin index
            float tilt_gain = 1.0f + bright_dark * (2.0f * static_cast<float>(k) / static_cast<float>(kNumBins) - 1.0f);
            mag_smooth[k] *= fmaxf(tilt_gain, 0.01f);
        }

        // Sparsity: zero out bins below a threshold relative to the strongest bin
        if(sparsity > 0.0f && max_mag > 0.0f)
        {
            float thresh = max_mag * (0.9f * sparsity); // higher sparsity -> fewer surviving bins
            for(size_t k = 0; k <= kNumBins; ++k)
            {
                if(mag_smooth[k] < thresh)
                    mag_smooth[k] = 0.0f;
            }
        }

        // Phase diffusion: add random phase offsets, stronger at higher bins
        if(phase_diffusion > 0.0f)
        {
            for(size_t k = 0; k <= kNumBins; ++k)
            {
                float w      = static_cast<float>(k) / static_cast<float>(kNumBins);
                float amount = phase_diffusion * w; // cycles
                float jitter = RandUniform(-amount, amount);
                synth_phase[k] += jitter;
            }
        }

        // Per-frame spectral energy (RMS of magnitudes) for CV_OUT_1
        {
            float sum_sq = 0.0f;
            for(size_t k = 0; k <= kNumBins; ++k)
                sum_sq += mag_smooth[k] * mag_smooth[k];
            float rms = sqrtf(sum_sq);
            float n   = static_cast<float>(kNumBins + 1);
            last_frame_spectral_energy = (n > 0.0f) ? (rms / n) : 0.0f;
        }

        // --- Build output spectrum with pitch shift (bin remap) ---
        for(size_t k_out = 0; k_out <= kNumBins; ++k_out)
        {
            float k_src_f = static_cast<float>(k_out) / pitch_ratio;
            size_t lo     = static_cast<size_t>(k_src_f);
            size_t hi     = lo + 1;
            float  frac   = k_src_f - static_cast<float>(lo);

            float mag_out, phase_out;
            if(hi > kNumBins)
            {
                mag_out   = 0.0f;
                phase_out = 0.0f;
            }
            else if(lo == 0 && hi == 1)
            {
                mag_out   = (1.0f - frac) * mag_smooth[0] + frac * mag_smooth[1];
                phase_out = (1.0f - frac) * synth_phase[0] + frac * synth_phase[1];
            }
            else
            {
                mag_out   = (1.0f - frac) * mag_smooth[lo] + frac * mag_smooth[hi];
                phase_out = (1.0f - frac) * synth_phase[lo] + frac * synth_phase[hi];
            }

            float out_phase = phase_out * kTwoPi;
            spectrum[k_out].re = mag_out * cosf(out_phase);
            spectrum[k_out].im = mag_out * sinf(out_phase);
        }

        // Reconstruct negative frequencies for a real IFFT
        for(size_t k = 1; k < kNumBins; ++k)
        {
            spectrum[kFftSize - k].re = spectrum[k].re;
            spectrum[kFftSize - k].im = -spectrum[k].im;
        }

        FftInPlace(spectrum, kFftSize, true);

        // Window again and copy into grain buffer
        for(size_t n = 0; n < kFftSize; ++n)
        {
            grain.buffer[n] = spectrum[n].re * window[n];
        }
        grain.Start();
    }
};

// ----------------------------------------------------------------------
// Daisy Patch SM integration
// ----------------------------------------------------------------------

DaisyPatchSM patch;
Switch     cv_swap_switch;   // B_8: when pressed, swap CV_1..4 with CV_5..8
Switch     plateau_switch;   // B_7: toggle Plateau reverb dry/wet

static SimpleResynth resynth;
static Grain         grains[kNumGrains];
static Plateau       plateau;

// Input history ring buffer for analysis
static float  input_history[kFftSize];
static size_t history_write_pos  = 0;
static size_t total_samples_seen = 0;
static float  grain_phase        = 0.0f;
static float  time_scale         = 1.0f;
// One-pole smoothed spectral energy for CV_OUT_1 (0–1)
static float  cv_energy_smooth   = 0.0f;
static const float cv_energy_coeff = 0.002f;  // smoothing (~0.1s at 48kHz block rate)

#ifdef RES_DEBUG
// Debug-logging cadence in audio samples (set from runtime sample rate)
static float    g_sample_rate            = 48000.0f;
static uint32_t g_debug_interval_samples = 48000; // ~1s by default
static uint32_t g_debug_sample_accum     = 0;
#endif

#ifdef RES_DEBUG
static void PrintStartupStatus()
{
    patch.ProcessAnalogControls();
    cv_swap_switch.Debounce();
    plateau_switch.Debounce();

    RES_DEBUG_PRINTLN("Resynthesis (Patch SM) debug build starting");

    RES_DEBUG_PRINTLN("CV inputs at startup:");
    RES_DEBUG_PRINT("CV_1: " FLT_FMT3 "\tCV_2: " FLT_FMT3 "\tCV_3: " FLT_FMT3 "\tCV_4: " FLT_FMT3 "\n",
                    FLT_VAR3(patch.GetAdcValue(CV_1)),
                    FLT_VAR3(patch.GetAdcValue(CV_2)),
                    FLT_VAR3(patch.GetAdcValue(CV_3)),
                    FLT_VAR3(patch.GetAdcValue(CV_4)));
    RES_DEBUG_PRINT("CV_5: " FLT_FMT3 "\tCV_6: " FLT_FMT3 "\tCV_7: " FLT_FMT3 "\tCV_8: " FLT_FMT3 "\n",
                    FLT_VAR3(patch.GetAdcValue(CV_5)),
                    FLT_VAR3(patch.GetAdcValue(CV_6)),
                    FLT_VAR3(patch.GetAdcValue(CV_7)),
                    FLT_VAR3(patch.GetAdcValue(CV_8)));

    RES_DEBUG_PRINTLN("Buttons / switches at startup:");
    RES_DEBUG_PRINTLN("B7 Plateau: %s", plateau_switch.Pressed() ? "ON" : "OFF");
    RES_DEBUG_PRINTLN("B8 CV swap: %s", cv_swap_switch.Pressed() ? "PRESSED" : "RELEASED");
}
#endif

void StartNextGrain()
{
    // Find an available grain (or reuse the first one)
    size_t idx = 0;
    for(size_t g = 0; g < kNumGrains; ++g)
    {
        if(!grains[g].running)
        {
            idx = g;
            break;
        }
    }

    resynth.StartGrainFromHistory(input_history, history_write_pos, grains[idx]);
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    patch.ProcessAnalogControls();
    cv_swap_switch.Debounce();
    plateau_switch.Debounce();

    bool swap_cv = cv_swap_switch.Pressed();
    bool plateau_on = plateau_switch.Pressed();

    // Read all 8 CVs; when B_8 is 1, swap bank CV_1..4 with CV_5..8
    float v1 = patch.GetAdcValue(CV_1);
    float v2 = patch.GetAdcValue(CV_2);
    float v3 = patch.GetAdcValue(CV_3);
    float v4 = patch.GetAdcValue(CV_4);
    float v5 = patch.GetAdcValue(CV_5);
    float v6 = patch.GetAdcValue(CV_6);
    float v7 = patch.GetAdcValue(CV_7);
    float v8 = patch.GetAdcValue(CV_8);

    float drywet_knob   = swap_cv ? v5 : v1;
    float smooth_knob   = swap_cv ? v6 : v2;
    float flatten_knob  = swap_cv ? v7 : v3;
    float tilt_knob     = swap_cv ? v8 : v4;
    float voct_cv       = swap_cv ? v1 : v5;
    float time_cv       = swap_cv ? v2 : v6;
    float sparsity_cv   = swap_cv ? v3 : v7;
    float diffusion_cv  = swap_cv ? v4 : v8;

    // Normalize CV_5..CV_8 (and their swapped counterparts) to bipolar -1..1
    // so that a physical range of -5 V .. +5 V is fully utilized and centered.
    float voct_bi      = CvToBipolar(voct_cv);
    float time_bi      = CvToBipolar(time_cv);
    float sparsity_bi  = CvToBipolar(sparsity_cv);
    float diffusion_bi = CvToBipolar(diffusion_cv);

    float drywet = fmap(drywet_knob, 0.0f, 1.0f);
    resynth.SetSmoothing(fmap(smooth_knob, 0.0f, 1.0f));
    resynth.SetSpectralFlatten(fmap(flatten_knob, 0.0f, 1.0f));
    resynth.SetBrightDark(fmap(tilt_knob, -1.0f, 1.0f));

    // Bipolar pitch CV on CV_5 (or its swapped input):
    // -5 V .. +5 V ~= -60 .. +60 semitones around unison.
    float semitones   = voct_bi * 60.0f;
    float pitch_ratio = powf(2.0f, semitones / 12.0f);
    resynth.SetPitchRatio(pitch_ratio);

    // Time-stretch / grain density: <1 = slower, >1 = denser
    // Map -5 V .. +5 V (~-1..1) to ~0.25x .. 4x around 1.0x using an exponential curve.
    time_scale = powf(2.0f, time_bi * 2.0f); // -1 -> 0.25, 0 -> 1.0, 1 -> 4.0

    // Spectral sparsity and phase diffusion, centered at 0.5 when CV = 0 V
    resynth.SetSparsity(0.5f * (sparsity_bi + 1.0f));   // -1..1 -> 0..1
    resynth.SetPhaseDiffusion(0.5f * (diffusion_bi + 1.0f)); // -1..1 -> 0..1

#ifdef RES_DEBUG
    // Periodic debug dump of control and spectral state
    g_debug_sample_accum += size;
    if(g_debug_interval_samples == 0)
        g_debug_interval_samples = 48000;
    if(g_debug_sample_accum >= g_debug_interval_samples)
    {
        g_debug_sample_accum = 0;

        size_t active_grains = 0;
        for(size_t g = 0; g < kNumGrains; ++g)
        {
            if(grains[g].running)
                ++active_grains;
        }

        float smooth_amt    = fmap(smooth_knob, 0.0f, 1.0f);
        float flatten_amt   = fmap(flatten_knob, 0.0f, 1.0f);
        float tilt_amt      = fmap(tilt_knob, -1.0f, 1.0f);
        float sparsity_amt  = 0.5f * (sparsity_bi + 1.0f);
        float diffusion_amt = 0.5f * (diffusion_bi + 1.0f);

        RES_DEBUG_PRINTLN("DBG: drywet=" FLT_FMT3 ", smooth=" FLT_FMT3
                          ", flatten=" FLT_FMT3 ", tilt=" FLT_FMT3,
                          FLT_VAR3(drywet),
                          FLT_VAR3(smooth_amt),
                          FLT_VAR3(flatten_amt),
                          FLT_VAR3(tilt_amt));

        RES_DEBUG_PRINTLN("DBG: pitch_ratio=" FLT_FMT3 ", time_scale=" FLT_FMT3
                          ", sparsity=" FLT_FMT3 ", phase_diff=" FLT_FMT3,
                          FLT_VAR3(pitch_ratio),
                          FLT_VAR3(time_scale),
                          FLT_VAR3(sparsity_amt),
                          FLT_VAR3(diffusion_amt));

        RES_DEBUG_PRINTLN("DBG: spectral_energy=" FLT_FMT3 ", active_grains=%u",
                          FLT_VAR3(resynth.last_frame_spectral_energy),
                          static_cast<unsigned>(active_grains));
    }
#endif

    for(size_t i = 0; i < size; i++)
    {
        float inL  = IN_L[i];
        float inR  = IN_R[i];
        float mono = 0.5f * (inL + inR);

        // Push into input history ring buffer
        input_history[history_write_pos] = mono;
        history_write_pos                = (history_write_pos + 1) % kFftSize;
        ++total_samples_seen;

        // Launch new grains once we have a full buffer.
        // Grain launch rate is controlled by time_scale.
        if(total_samples_seen >= kFftSize)
        {
            grain_phase += time_scale;
            while(grain_phase >= static_cast<float>(kHopSize))
            {
                StartNextGrain();
                grain_phase -= static_cast<float>(kHopSize);
            }
        }

        // Sum all active grains (simple overlap-add)
        float wet = 0.0f;
        for(size_t g = 0; g < kNumGrains; ++g)
        {
            if(grains[g].running)
                wet += grains[g].Process();
        }

        // Normalize a bit to avoid clipping when several grains overlap
        wet *= 1.0f / static_cast<float>(kHopDenom);

        float out_mono = (1.0f - drywet) * mono + drywet * wet;

        // Plateau reverb: when B_7 is on, 50/50 dry/wet; when off, completely dry.
        float outL, outR;
        if(plateau_on)
        {
            float wetL, wetR;
            plateau.Process(out_mono, out_mono, wetL, wetR);
            outL = 0.5f * (out_mono + wetL);
            outR = 0.5f * (out_mono + wetR);
        }
        else
        {
            outL = out_mono;
            outR = out_mono;
        }

        OUT_L[i] = outL;
        OUT_R[i] = outR;
    }

    // CV_OUT_1: smoothed spectral energy (RMS per frame), 0–5 V
    float energy_in = fminf(1.0f, resynth.last_frame_spectral_energy * 5.0f);
    cv_energy_smooth += cv_energy_coeff * (energy_in - cv_energy_smooth);
    patch.WriteCvOut(CV_OUT_1, 5.0f * fminf(1.0f, cv_energy_smooth));

    // CV_OUT_2: unsmoothed spectral energy (RMS per frame), 0–5 V
    patch.WriteCvOut(CV_OUT_2, 5.0f * fminf(1.0f, resynth.last_frame_spectral_energy * 5.0f));
}

int main(void)
{
    patch.Init();
    cv_swap_switch.Init(patch.B8);
    plateau_switch.Init(patch.B7);
    resynth.Init();
    plateau.Init(patch.AudioSampleRate());
#ifdef RES_DEBUG
    g_sample_rate            = patch.AudioSampleRate();
    g_debug_interval_samples = static_cast<uint32_t>(g_sample_rate);
    patch.StartLog(true);
    PrintStartupStatus();
#endif
    for(size_t g = 0; g < kNumGrains; ++g)
    {
        grains[g].running = false;
        grains[g].index   = 0;
    }
    grain_phase        = 0.0f;
    time_scale         = 1.0f;
    total_samples_seen = 0;
    history_write_pos  = 0;

    patch.StartAudio(AudioCallback);
    while(1) {}
}

