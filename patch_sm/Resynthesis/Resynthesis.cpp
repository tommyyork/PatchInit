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
#include "ResynthEngine.h"
#include "Compression.h"

#include <cmath>
#include <cstdint>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;
using namespace resynth_engine;

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
    return v * 2.0f - 1.0f;
}

// ----------------------------------------------------------------------
// Daisy Patch SM integration
// ----------------------------------------------------------------------

DaisyPatchSM patch;
// B_8: Mode select — when pressed, the engine runs in partial‑based / spectral‑model
// mode; when released, grains are pitch‑locked to V/OCT.
Switch     mode_switch;
// B_7: MAX COMP — toggles a stronger Omnipressor-style compressor on the output.
Switch     plateau_switch;

static SimpleResynth resynth;
static Grain         grains[kNumGrains];
static Plateau       plateau;

// Input history ring buffer for analysis
static float  input_history[kFftSize];
static size_t history_write_pos  = 0;
static size_t total_samples_seen = 0;
static float  grain_phase        = 0.0f;
static float  time_scale         = 0.1f;
// One-pole smoothed spectral energy for CV_OUT_1 (0–1)
static float  cv_energy_smooth   = 0.1f;
static const float cv_energy_coeff = 0.01f;  // smoothing (~0.1s at 48kHz block rate)
// Smoothed wet gain to reduce level pumping when the number of active grains changes.
static float  wet_gain_state     = 1.0f;

// Compressor: normal (2:1, make output consistent as a sound source)
static patch_sm::Compressor comp_normal;

// Compressor parameter sets: "normal" vs "MAX COMP" (Omnipressor-style).
static constexpr float kCompThreshNormal = 0.25f;
static constexpr float kCompRatioNormal  = 2.0f;
static constexpr float kCompMakeupNormal = 1.8f;
static constexpr float kCompThreshMax    = 0.2f;
static constexpr float kCompRatioMax     = -2.0f;
static constexpr float kCompMakeupMax    = 2.6f;
static constexpr float kCompAttack       = 0.0003f;
static constexpr float kCompRelease      = 0.05f;

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
    mode_switch.Debounce();
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
    RES_DEBUG_PRINTLN("B7 MAX COMP: %s", plateau_switch.Pressed() ? "ON" : "OFF");
    RES_DEBUG_PRINTLN("B8 Partial-based mode: %s", mode_switch.Pressed() ? "ON" : "OFF");
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
    mode_switch.Debounce();
    plateau_switch.Debounce();

    bool partial_mode_on = mode_switch.Pressed();  // B_8: partial‑based / spectral model when ON
    bool pitch_lock_on   = !partial_mode_on;       // OFF = classic pitch‑lock mode
    bool max_comp_on   = plateau_switch.Pressed(); // B_7: MAX COMP + reverb when ON

    // Read all 8 CVs (CV_1..CV_8).
    float v1 = patch.GetAdcValue(CV_1);
    float v2 = patch.GetAdcValue(CV_2);
    float v3 = patch.GetAdcValue(CV_3);
    float v4 = patch.GetAdcValue(CV_4);
    float v5 = patch.GetAdcValue(CV_5);
    float v6 = patch.GetAdcValue(CV_6);
    float v7 = patch.GetAdcValue(CV_7);
    float v8 = patch.GetAdcValue(CV_8);

    float drywet_knob   = v1;
    float smooth_knob   = v2;
    float fluff_knob    = v3;
    float tilt_knob     = v4;
    float voct_cv       = v5;
    float time_cv       = v6;
    float sparsity_cv   = v7;
    float diffusion_cv  = v8;

    // V/OCT: 0–10 V, 1 V/oct. 0 V = C0 (~16.35 Hz), 1 V = C1 (~32.7 Hz), 2 V = C2 (~65.4 Hz).
    float voct_volts = voct_cv * 10.0f;
    float fundamental_hz = 440.0f * powf(2.0f, voct_volts - 4.75f);
    resynth.SetFundamentalHz(fundamental_hz, patch.AudioSampleRate());
    // In hardware, B_8 OFF = pitch‑locked grains; B_8 ON = partial‑based model.
    resynth.SetPitchLockMode(pitch_lock_on);

    // Normalize time/sparsity/diffusion (CV_6–CV_8) to bipolar -1..1 for -5 V .. +5 V
    float time_bi      = CvToBipolar(time_cv);
    float sparsity_bi  = CvToBipolar(sparsity_cv);
    float diffusion_bi = CvToBipolar(diffusion_cv);

    float drywet = fmap(drywet_knob, 0.0f, 1.0f);
    // Emphasise extremes for more dramatic range; keep mid region usable.
    float smoothing = powf(fmap(smooth_knob, 0.0f, 1.0f), 0.5f);
    resynth.SetSmoothing(smoothing);
    // FLUFF: keep 0..1 but use a slightly curved response so higher values
    // ramp up more quickly from mid travel.
    float fluff = fmap(fluff_knob, 0.0f, 1.0f);
    resynth.SetFluff(powf(fluff, 1.2f));
    // Bright/dark tilt: extend slightly beyond -1..1 for a more obvious tonal shift.
    resynth.SetBrightDark(fmap(tilt_knob, -1.5f, 1.5f));

    // Time-stretch / grain density: <1 = slower, >1 = denser. Use a wider, more
    // dramatic range from ~0.125x to ~8x for ±5 V, still centred at 1.0x.
    time_scale = powf(2.0f, time_bi * 3.0f); // -1 -> 0.125, 0 -> 1.0, 1 -> 8.0

    // Sparsity and phase diffusion: map -5 V..+5 V (bipolar) back to 0..1 with a
    // squaring curve so most of the travel produces pronounced changes.
    float sparsity_norm  = 0.5f * (sparsity_bi + 1.0f);       // -1..1 -> 0..1
    float diffusion_norm = 0.5f * (diffusion_bi + 1.0f);      // -1..1 -> 0..1
    resynth.SetSparsity(sparsity_norm * sparsity_norm);       // emphasise high values
    resynth.SetPhaseDiffusion(diffusion_norm * diffusion_norm);

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

        RES_DEBUG_PRINTLN("DBG: V/OCT fundamental_hz=" FLT_FMT3 ", time_scale=" FLT_FMT3
                          ", sparsity=" FLT_FMT3 ", phase_diff=" FLT_FMT3,
                          FLT_VAR3(fundamental_hz),
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
        // Grain launch rate is controlled by time_scale, with a bit of random
        // jitter in the effective hop size so launches are not strictly on a
        // grid (more alien / scattered texture).
        if(total_samples_seen >= kFftSize)
        {
            grain_phase += time_scale;
            while(grain_phase >= static_cast<float>(kHopSize))
            {
                StartNextGrain();
                // Jittered hop: around kHopSize with mild variation to avoid
                // a rigid launch grid but without creating strong level swings.
                float hop       = static_cast<float>(kHopSize);
                float jitterMul = resynth_engine::SimpleResynth::RandUniform(0.9f, 1.1f);
                grain_phase -= hop * jitterMul;
            }
        }

        // Sum all active grains (overlap-add), then normalize by active count so level is
        // consistent whether 1 or several grains are playing (reduces volume variation).
        float wet = 0.0f;
        size_t active_count = 0;
        for(size_t g = 0; g < kNumGrains; ++g)
        {
            if(grains[g].running)
            {
                wet += grains[g].Process();
                ++active_count;
            }
        }
        // Smooth the effective gain so changes in the number of overlapping
        // grains do not immediately translate into audible pumping.
        if(active_count > 0)
        {
            float target_gain = 1.0f
                                / (static_cast<float>(kHopDenom)
                                   * static_cast<float>(active_count));
            const float alpha = 0.01f; // ~100-frame time constant
            wet_gain_state += alpha * (target_gain - wet_gain_state);
        }
        wet *= wet_gain_state;

        // When no grains are active yet (first kFftSize samples), pass dry to avoid leading silence
        float out_mono = (active_count > 0)
            ? ((1.0f - drywet) * mono + drywet * wet)
            : mono;

        // Soft clip to reduce harsh peaks and further smooth level variation
        float lim = 0.95f;
        if (out_mono > lim)  out_mono = lim + (out_mono - lim) / (1.0f + (out_mono - lim));
        if (out_mono < -lim) out_mono = -lim + (out_mono + lim) / (1.0f - (out_mono + lim));

        // Compressor after soft clip to give a stable level. When B_7 (MAX COMP)
        // is on, use a stronger Omnipressor-style setting; otherwise use the
        // gentler "normal" 2:1 compression.
        if(max_comp_on)
        {
            comp_normal.SetParams(
                kCompThreshMax,
                kCompRatioMax,
                kCompMakeupMax,
                kCompAttack,
                kCompRelease);
        }
        else
        {
            comp_normal.SetParams(
                kCompThreshNormal,
                kCompRatioNormal,
                kCompMakeupNormal,
                kCompAttack,
                kCompRelease);
        }
        out_mono = comp_normal.Process(out_mono);

        // Final soft clip after compressor.
        if (out_mono > lim)  out_mono = lim + (out_mono - lim) / (1.0f + (out_mono - lim));
        if (out_mono < -lim) out_mono = -lim + (out_mono + lim) / (1.0f - (out_mono + lim));

        // Plateau reverb behaviour:
        // - Only active in pitch‑locked mode (B_8 ON), so the reverb
        //   reinforces the pitched resynth voice.
        // - B_7 (MAX COMP) toggles both the stronger compressor above
        //   and the reverb itself. When MAX COMP is off, output stays
        //   dry regardless of B_8.
        float outL = out_mono;
        float outR = out_mono;
        if(pitch_lock_on && max_comp_on)
        {
            float verbL, verbR;
            plateau.Process(out_mono, out_mono, verbL, verbR);
            // 50/50 dry/wet blend.
            outL = 0.5f * (out_mono + verbL);
            outR = 0.5f * (out_mono + verbR);
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
    mode_switch.Init(patch.B8);
    plateau_switch.Init(patch.B7);
    resynth.Init();
    plateau.Init(patch.AudioSampleRate());

    // Single compressor: defaults to "normal" 2:1; B_7 (MAX COMP) engages a
    // stronger Omnipressor-style setting.
    comp_normal.Init(patch.AudioSampleRate());
    comp_normal.SetParams(
        kCompThreshNormal,
        kCompRatioNormal,
        kCompMakeupNormal,
        kCompAttack,
        kCompRelease);
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
    grain_phase        = 0.1f;
    time_scale         = 0.1f;
    total_samples_seen = 0;
    history_write_pos  = 0;

    patch.StartAudio(AudioCallback);
    while(1) {}
}

