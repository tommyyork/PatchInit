// Phase-vocoder resynthesis engine (no hardware dependency).
// Used by Resynthesis.cpp (Daisy Patch SM) and by offline tests.

#ifndef RESYNTH_ENGINE_H
#define RESYNTH_ENGINE_H

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace resynth_engine {

static inline float Clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// ----------------------------------------------------------------------
// Simple complex type and FFT (radix-2 Cooley-Tukey)
// ----------------------------------------------------------------------

struct Complex
{
    float re;
    float im;
};

inline void FftInPlace(Complex *data, size_t n, bool inverse)
{
    size_t j = 0;
    for (size_t i = 1; i < n; ++i)
    {
        size_t bit = n >> 1;
        while (j & bit) { j ^= bit; bit >>= 1; }
        j |= bit;
        if (i < j)
        {
            Complex tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        float ang = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        if (!inverse) ang = -ang;
        float wlenRe = cosf(ang);
        float wlenIm = sinf(ang);
        for (size_t i = 0; i < n; i += len)
        {
            float wRe = 1.0f, wIm = 0.0f;
            for (size_t j2 = 0; j2 < len / 2; ++j2)
            {
                Complex u = data[i + j2];
                Complex v;
                v.re = data[i + j2 + len/2].re * wRe - data[i + j2 + len/2].im * wIm;
                v.im = data[i + j2 + len/2].re * wIm + data[i + j2 + len/2].im * wRe;
                data[i + j2].re = u.re + v.re;
                data[i + j2].im = u.im + v.im;
                data[i + j2 + len/2].re = u.re - v.re;
                data[i + j2 + len/2].im = u.im - v.im;
                float nextWRe = wRe * wlenRe - wIm * wlenIm;
                float nextWIm = wRe * wlenIm + wIm * wlenRe;
                wRe = nextWRe;
                wIm = nextWIm;
            }
        }
    }

    if (inverse)
    {
        float invN = 1.0f / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) { data[i].re *= invN; data[i].im *= invN; }
    }
}

// ----------------------------------------------------------------------
// Constants and phase-vocoder structures
// ----------------------------------------------------------------------

static constexpr size_t kFftBits   = 8;
static constexpr size_t kFftSize   = 1 << kFftBits;
static constexpr size_t kHopDenom   = 4;
static constexpr size_t kHopSize   = kFftSize / kHopDenom;
static constexpr size_t kNumBins    = kFftSize / 2;
static constexpr size_t kNumGrains  = 4;
static constexpr float  kTwoPi     = 2.0f * static_cast<float>(M_PI);

struct Grain
{
    float  buffer[kFftSize];
    size_t index;
    bool   running;

    void Start() { index = 0; running = true; }

    float Process()
    {
        if (!running) return 0.0f;
        float v = buffer[index];
        ++index;
        if (index >= kFftSize) { running = false; index = 0; }
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
    float pitch_ratio;
    float spectral_flatten;
    float bright_dark;
    float sparsity;
    float phase_diffusion;
    float fluff;
    float last_frame_spectral_energy;
    // V/oct mode: fundamental frequency (Hz) and sample rate for harmonic reinforcement
    float fundamental_hz_;
    float sample_rate_;
    // Pitch / harmonic mode: when true, grains are globally pitch-shifted so their
    // spectra lock to the requested fundamental; when false, the original spectrum is
    // preserved and the fundamental is reinforced by a partial-based model.
    bool  pitch_lock_mode_;
    // When true, bypass all pitch/harmonic tricks and perform a basic
    // phase-vocoder style resynthesis of the input spectrum: no
    // pitch-shift, no harmonic overlay, just shaped magnitudes and
    // propagated phases.
    bool  pure_resynth_mode_;

    void Init()
    {
        for (size_t n = 0; n < kFftSize; ++n)
            window[n] = 0.5f * (1.0f - cosf(kTwoPi * static_cast<float>(n) / static_cast<float>(kFftSize - 1)));
        for (size_t i = 0; i <= kNumBins; ++i)
        {
            prev_phase[i] = 0.0f;
            synth_phase[i] = 0.0f;
            mag_smooth[i] = 0.0f;
        }
        primed = false;
        mag_smooth_coeff = 0.4f;
        pitch_ratio = 1.0f;
        spectral_flatten = 0.0f;
        bright_dark = 0.0f;
        sparsity = 0.0f;
        phase_diffusion = 0.0f;
        fluff = 0.0f;
        last_frame_spectral_energy = 0.0f;
        fundamental_hz_ = 0.0f;
        sample_rate_ = 0.0f;
        pitch_lock_mode_ = true;
        pure_resynth_mode_ = false;
    }

    void SetSmoothing(float alpha)       { mag_smooth_coeff = Clamp(alpha, 0.0f, 1.0f); }
    void SetPitchRatio(float ratio)      { pitch_ratio = Clamp(ratio, 0.1f, 8.0f); }
    void SetSpectralFlatten(float amount){ spectral_flatten = Clamp(amount, 0.0f, 1.0f); }
    void SetBrightDark(float tilt)       { bright_dark = Clamp(tilt, -1.0f, 1.0f); }
    void SetSparsity(float amount)       { sparsity = Clamp(amount, 0.0f, 1.0f); }
    void SetPhaseDiffusion(float amount) { phase_diffusion = Clamp(amount, 0.0f, 1.0f); }
    void SetFluff(float amount)          { fluff = Clamp(amount, 0.0f, 1.0f); }
    void SetPitchLockMode(bool enable)   { pitch_lock_mode_ = enable; }
    void SetPureResynthMode(bool enable) { pure_resynth_mode_ = enable; }

    // 1 V/oct: 0 V = C0 (~16.35 Hz), 1 V = C1 (~32.7 Hz), 2 V = C2 (~65.4 Hz), etc.
    // Sets pitch_ratio so the resynthesized fundamental is at f0_hz, and enables
    // harmonic reinforcement (fundamental + 2nd and 3rd harmonics in decreasing level).
    void SetFundamentalHz(float f0_hz, float sample_rate_hz)
    {
        sample_rate_ = sample_rate_hz > 0.0f ? sample_rate_hz : 48000.0f;
        fundamental_hz_ = f0_hz > 0.0f ? f0_hz : 32.7f;
        float ref_hz = sample_rate_ / static_cast<float>(kFftSize);
        pitch_ratio = Clamp(fundamental_hz_ / ref_hz, 0.1f, 8.0f);
    }

    static float PrincArg(float x)
    {
        x = x - floorf(x);
        if (x > 0.5f) x -= 1.0f;
        return x;
    }

    static float RandUniform(float lo, float hi)
    {
        static uint32_t state = 1u;
        state = state * 1664525u + 1013904223u;
        float t = static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        return lo + (hi - lo) * t;
    }

    void StartGrainFromHistory(const float *history, size_t history_write_pos, Grain &grain)
    {
        Complex spectrum[kFftSize];
        size_t idx;
        // FLUFF stage 2 and above: jitter grain analysis start position within the
        // current history window for a denser, more "cloudy" texture. The history
        // buffer is one FFT window long, so this is effectively a circular shift of
        // the analysis window (safe for the ring buffer).
        int max_offset = 0;
        // Map fluff (0..1) to number of active "cloud" stages (0..4).
        const int kNumFluffStages = 4;
        int active_fluff_stages
            = static_cast<int>(fluff * static_cast<float>(kNumFluffStages) + 1e-3f);
        if(active_fluff_stages < 0)
            active_fluff_stages = 0;
        if(active_fluff_stages > kNumFluffStages)
            active_fluff_stages = kNumFluffStages;
        if(active_fluff_stages >= 2)
        {
            // Up to ±1/8 of the window for high FLUFF.
            float span = 0.125f * (static_cast<float>(active_fluff_stages - 1)
                                   / static_cast<float>(kNumFluffStages - 1));
            max_offset = static_cast<int>(span * static_cast<float>(kFftSize));
        }
        if(max_offset > 0)
        {
            int offset = static_cast<int>(
                RandUniform(static_cast<float>(-max_offset),
                            static_cast<float>(max_offset)));
            int start = static_cast<int>(history_write_pos) + offset;
            while(start < 0)
                start += static_cast<int>(kFftSize);
            while(start >= static_cast<int>(kFftSize))
                start -= static_cast<int>(kFftSize);
            idx = static_cast<size_t>(start);
        }
        else
        {
            idx = history_write_pos;
        }
        for (size_t n = 0; n < kFftSize; ++n)
        {
            float s = history[idx];
            spectrum[n].re = s * window[n];
            spectrum[n].im = 0.0f;
            idx = (idx + 1) % kFftSize;
        }

        FftInPlace(spectrum, kFftSize, false);

        for (size_t k = 0; k <= kNumBins; ++k)
        {
            float re = spectrum[k].re, im = spectrum[k].im;
            float mag = sqrtf(re * re + im * im);
            float phase = atan2f(im, re) / kTwoPi;

            if (!primed)
            {
                prev_phase[k] = phase;
                synth_phase[k] = phase;
                mag_smooth[k] = mag;
                continue;
            }

            float omega_bin = static_cast<float>(k) / static_cast<float>(kFftSize);
            float delta_expected = omega_bin * static_cast<float>(kHopSize);
            float delta = phase - prev_phase[k];
            delta -= delta_expected;
            delta = PrincArg(delta);
            float omega_instant = omega_bin + delta / static_cast<float>(kHopSize);

            mag_smooth[k] = mag_smooth[k] + (mag_smooth_coeff > 0.02f ? mag_smooth_coeff : 0.02f) * (mag - mag_smooth[k]);
            synth_phase[k] = synth_phase[k] + omega_instant * static_cast<float>(kHopSize);
            prev_phase[k] = phase;
        }
        primed = true;

        float sum_mag = 0.0f, max_mag = 0.0f;
        for (size_t k = 1; k < kNumBins; ++k)
        {
            sum_mag += mag_smooth[k];
            if (mag_smooth[k] > max_mag) max_mag = mag_smooth[k];
        }
        float mean_mag = sum_mag / static_cast<float>(kNumBins > 1 ? kNumBins - 1 : 1);

        // Preserve total spectral energy across shaping so level doesn't jump when changing flatten/tilt/sparsity
        float pre_sum_sq = 0.0f;
        for (size_t k = 0; k <= kNumBins; ++k) pre_sum_sq += mag_smooth[k] * mag_smooth[k];

        for (size_t k = 0; k <= kNumBins; ++k)
        {
            mag_smooth[k] = mag_smooth[k] * (1.0f - spectral_flatten) + mean_mag * spectral_flatten;
            float tilt_gain = 1.0f + bright_dark * (2.0f * static_cast<float>(k) / static_cast<float>(kNumBins) - 1.0f);
            // Limit tilt range so output doesn't clip or go silent (was 0.01..2, now 0.4..1.6)
            if (tilt_gain < 0.4f) tilt_gain = 0.4f;
            if (tilt_gain > 1.6f) tilt_gain = 1.6f;
            mag_smooth[k] *= tilt_gain;
        }

        if (sparsity > 0.0f && max_mag > 0.0f)
        {
            float thresh = max_mag * (0.9f * sparsity);
            for (size_t k = 0; k <= kNumBins; ++k)
                if (mag_smooth[k] < thresh) mag_smooth[k] = 0.0f;
        }

        // Restore spectral energy after shaping so output level stays consistent
        float post_sum_sq = 0.0f;
        for (size_t k = 0; k <= kNumBins; ++k) post_sum_sq += mag_smooth[k] * mag_smooth[k];
        if (post_sum_sq > 1e-12f && pre_sum_sq > 1e-12f)
        {
            float scale = sqrtf(pre_sum_sq / post_sum_sq);
            if (scale < 4.0f)  // avoid blowing up on very sparse frames
                for (size_t k = 0; k <= kNumBins; ++k) mag_smooth[k] *= scale;
        }

        // FLUFF stage 1: add extra, frequency-dependent phase diffusion on top of the
        // dedicated PHASE DIFFUSION control, starting subtly and increasing with FLUFF.
        float effective_phase_diffusion = phase_diffusion;
        if(active_fluff_stages >= 1)
        {
            float extra = 0.2f
                          * (static_cast<float>(active_fluff_stages)
                             / static_cast<float>(kNumFluffStages));
            effective_phase_diffusion = Clamp(phase_diffusion + extra, 0.0f, 1.0f);
        }

        if (effective_phase_diffusion > 0.0f)
        {
            for (size_t k = 0; k <= kNumBins; ++k)
            {
                float w = static_cast<float>(k) / static_cast<float>(kNumBins);
                float amount = effective_phase_diffusion * w;
                float jitter = RandUniform(-amount, amount);
                synth_phase[k] += jitter;
            }
        }

        {
            float sum_sq = 0.0f;
            for (size_t k = 0; k <= kNumBins; ++k) sum_sq += mag_smooth[k] * mag_smooth[k];
            float rms = sqrtf(sum_sq);
            float n = static_cast<float>(kNumBins + 1);
            last_frame_spectral_energy = (n > 0.0f) ? (rms / n) : 0.0f;
        }

        if(pure_resynth_mode_)
        {
            // Basic phase-vocoder style resynthesis: do not pitch-shift
            // or add harmonic scaffolding; simply reconstruct the
            // spectrum from the shaped magnitudes and propagated
            // phases.
            for(size_t k = 0; k <= kNumBins; ++k)
            {
                float mag   = mag_smooth[k];
                float phase = synth_phase[k] * kTwoPi;
                spectrum[k].re = mag * cosf(phase);
                spectrum[k].im = mag * sinf(phase);
            }
        }
        else
        {
            // FLUFF stage 3: introduce gentle micro‑pitch jitter per grain in pitch‑locked
            // mode only. This preserves overall tuning while creating a denser, more
            // animated cloud around the target fundamental.
            float pitch_ratio_used = pitch_lock_mode_ ? pitch_ratio : 1.0f;
            if(pitch_lock_mode_ && active_fluff_stages >= 3)
            {
                float max_cents = 12.0f
                                  * (static_cast<float>(active_fluff_stages - 2)
                                     / static_cast<float>(kNumFluffStages - 2));
                float cents = RandUniform(-max_cents, max_cents);
                float factor = powf(2.0f, cents / 1200.0f);
                pitch_ratio_used *= factor;
            }

            for (size_t k_out = 0; k_out <= kNumBins; ++k_out)
            {
                float k_src_f = static_cast<float>(k_out) / pitch_ratio_used;
                size_t lo = static_cast<size_t>(k_src_f);
                size_t hi = lo + 1;
                float frac = k_src_f - static_cast<float>(lo);

                float mag_out, phase_out;
                if (hi > kNumBins) { mag_out = 0.0f; phase_out = 0.0f; }
                else if (lo == 0 && hi == 1)
                {
                    mag_out   = (1.0f - frac) * mag_smooth[0] + frac * mag_smooth[1];
                    phase_out = (1.0f - frac) * synth_phase[0] + frac * synth_phase[1];
                }
                else
                {
                    mag_out   = (1.0f - frac) * mag_smooth[lo] + frac * mag_smooth[hi];
                    phase_out = (1.0f - frac) * synth_phase[lo] + frac * synth_phase[hi];
                }

                // FLUFF stage 4: subtle per‑bin magnitude jitter to create a noisier, more
                // granular cloud at high settings. Energy preservation above keeps level
                // stable even when this stage is active.
                if(active_fluff_stages >= 4 && mag_out > 0.0f)
                {
                    float depth = 0.25f;
                    float jitter = RandUniform(-depth, depth);
                    float scale = 1.0f + jitter;
                    if(scale < 0.2f) scale = 0.2f;
                    if(scale > 1.8f) scale = 1.8f;
                    mag_out *= scale;
                }

                float out_phase = phase_out * kTwoPi;
                spectrum[k_out].re = mag_out * cosf(out_phase);
                spectrum[k_out].im = mag_out * sinf(out_phase);
            }

            // V/OCT modes:
            // - Pitch‑locked grains (pitch_lock_mode_ = true): the entire spectrum is
            //   pitch‑shifted so grain content locks to the requested fundamental. The
            //   original timbre is preserved but follows the keyboard closely.
            // - Partial‑based / spectral model (pitch_lock_mode_ = false): the original
            //   spectrum stays at its analyzed pitch; we overlay a harmonic scaffold
            //   (fundamental + harmonics) tuned to the requested fundamental so simple
            //   inputs (e.g. a bell) behave like a full synth voice.
            //
            // In partial‑based mode only, reinforce fundamental and harmonics. Color
            // (bright_dark) selects which family is emphasized: fully CCW (-1) = even
            // harmonics only (2nd, 4th, 6th); fully CW (+1) = odd harmonics only
            // (fundamental, 3rd, 5th).
            if (!pitch_lock_mode_ && fundamental_hz_ > 0.0f && sample_rate_ > 0.0f)
            {
                float bins_per_hz = static_cast<float>(kFftSize) / sample_rate_;
                int k0 = static_cast<int>(fundamental_hz_ * bins_per_hz + 0.5f);
                if (k0 < 1) k0 = 1;
                if (k0 > static_cast<int>(kNumBins)) k0 = static_cast<int>(kNumBins);
                // Crossfade: CCW = even only, CW = odd only (1-based: 1=fundamental, 2=2nd, ...)
                float odd_amount  = 0.5f * (bright_dark + 1.0f);   // 0 at CCW, 1 at CW
                float even_amount = 1.0f - odd_amount;             // 1 at CCW, 0 at CW
                // Gains taper so partials sit with the resynthesis (harmonically rich but blended)
                const float gains[] = { 0.55f, 0.4f, 0.3f, 0.22f, 0.18f, 0.14f };  // h=1..6
                const int num_harmonics = 6;
                for (int h = 1; h <= num_harmonics; ++h)
                {
                    int k = k0 * h;
                    if (k > static_cast<int>(kNumBins)) break;
                    float amount = (h & 1) ? odd_amount : even_amount;  // odd h -> odd_amount, even h -> even_amount
                    float g = 1.0f + gains[h - 1] * amount;
                    spectrum[k].re *= g;
                    spectrum[k].im *= g;
                }
            }
        }

        for (size_t k = 1; k < kNumBins; ++k)
        {
            spectrum[kFftSize - k].re = spectrum[k].re;
            spectrum[kFftSize - k].im = -spectrum[k].im;
        }

        FftInPlace(spectrum, kFftSize, true);

        for (size_t n = 0; n < kFftSize; ++n)
            grain.buffer[n] = spectrum[n].re * window[n];

        // Per-grain level matching: scale grain so its RMS matches the analysis window RMS,
        // reducing level variation when different numbers of grains overlap.
        float rms_window = 0.0f;
        idx = history_write_pos;
        for (size_t n = 0; n < kFftSize; ++n)
        {
            float s = history[idx] * window[n];
            rms_window += s * s;
            idx = (idx + 1) % kFftSize;
        }
        rms_window = sqrtf(rms_window / static_cast<float>(kFftSize));
        float rms_grain = 0.0f;
        for (size_t n = 0; n < kFftSize; ++n) rms_grain += grain.buffer[n] * grain.buffer[n];
        rms_grain = sqrtf(rms_grain / static_cast<float>(kFftSize));
        if (rms_grain > 1e-8f && rms_window > 1e-8f)
        {
            float gain = rms_window / rms_grain;
            if (gain > 0.25f && gain < 4.0f)  // avoid extreme gains
            {
                for (size_t n = 0; n < kFftSize; ++n)
                    grain.buffer[n] *= gain;
            }
        }

        // Global attenuation: scale each grain way down so its samples fall between
        // 0.1% and 10% of their previous values. This keeps the algorithm identical
        // while substantially reducing raw grain level before overlap-add / compression.
        float global_scale = RandUniform(0.001f, 0.10f);
        for (size_t n = 0; n < kFftSize; ++n)
            grain.buffer[n] *= global_scale;

        grain.Start();
    }
};

} // namespace resynth_engine

#endif
