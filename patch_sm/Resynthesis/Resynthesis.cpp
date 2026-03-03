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
Switch     cv_swap_switch;   // B_8: "MAX COMP" — when pressed, swap CV banks and use MAX COMP compressor mode (negative ratio, near-full volume, preserve dynamics)
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

// Compressor: makes output louder and more consistent as a sound source for filter/amplitude
// When "MAX COMP" switch (B_8) is engaged: negative ratio (Omnipressor-style), near-full volume, preserves dynamics/transients
static float  comp_env           = 0.0f;
static const float comp_thresh   = 0.25f;   // ~-12 dB, compress above this (normal mode)
static const float comp_ratio    = 2.0f;    // 2:1 normal
static const float comp_makeup   = 1.8f;    // make-up gain (normal)
// MAX COMP mode (when B_8 engaged): negative ratio, higher makeup, boost below threshold
static const float comp_thresh_max = 0.2f;   // slightly lower threshold
static const float comp_ratio_max  = -2.0f; // negative ratio (Eventide Omnipressor style): tame peaks, boost quiet -> near full volume, preserve transients
static const float comp_makeup_max = 2.6f;   // higher make-up so output is near full scale
static const float comp_attack   = 0.0003f; // 0.3 ms
static const float comp_release  = 0.05f;   // 50 ms

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
    RES_DEBUG_PRINTLN("B8 MAX COMP: %s", cv_swap_switch.Pressed() ? "ON" : "OFF");
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

    bool swap_cv = cv_swap_switch.Pressed();   // "MAX COMP" switch: CV swap + compressor mode
    bool plateau_on = plateau_switch.Pressed();
    bool max_comp_on = swap_cv;               // same switch: when engaged, compressor uses MAX COMP settings

    // Read all 8 CVs; when B_8 ("MAX COMP") is 1, swap bank CV_1..4 with CV_5..8
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

    // V/OCT: 0–10 V, 1 V/oct. 0 V = C0 (~16.35 Hz), 1 V = C1 (~32.7 Hz), 2 V = C2 (~65.4 Hz).
    float voct_volts = voct_cv * 10.0f;
    float fundamental_hz = 440.0f * powf(2.0f, voct_volts - 4.75f);
    resynth.SetFundamentalHz(fundamental_hz, patch.AudioSampleRate());

    // Normalize time/sparsity/diffusion (CV_6–CV_8) to bipolar -1..1 for -5 V .. +5 V
    float time_bi      = CvToBipolar(time_cv);
    float sparsity_bi  = CvToBipolar(sparsity_cv);
    float diffusion_bi = CvToBipolar(diffusion_cv);

    float drywet = fmap(drywet_knob, 0.0f, 1.0f);
    resynth.SetSmoothing(fmap(smooth_knob, 0.0f, 1.0f));
    resynth.SetSpectralFlatten(fmap(flatten_knob, 0.0f, 1.0f));
    resynth.SetBrightDark(fmap(tilt_knob, -1.0f, 1.0f));

    // Time-stretch / grain density: <1 = slower, >1 = denser
    // Map -5 V .. +5 V (~-1..1) to ~0.25x .. 4x around 1.0x using an exponential curve.
    time_scale = powf(2.0f, time_bi * 2.0f); // -1 -> 0.25, 0 -> 1.0, 1 -> 4.0

    // Sparsity and phase diffusion: restore a wide 0..1 range for strong, metallic effects.
    // -5 V..+5 V (bipolar) is mapped back to 0..1 (0 V = 0.5), with internal energy
    // preservation so the sound stays present even at extreme settings.
    resynth.SetSparsity(0.5f * (sparsity_bi + 1.0f));       // -1..1 -> 0..1
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
                // Jittered hop: around kHopSize with ±30% variation.
                float hop       = static_cast<float>(kHopSize);
                float jitterMul = resynth_engine::SimpleResynth::RandUniform(0.7f, 1.3f);
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
        if(active_count > 0)
            wet *= 1.0f / (static_cast<float>(kHopDenom) * static_cast<float>(active_count));

        // When no grains are active yet (first kFftSize samples), pass dry to avoid leading silence
        float out_mono = (active_count > 0)
            ? ((1.0f - drywet) * mono + drywet * wet)
            : mono;

        // Soft clip to reduce harsh peaks and further smooth level variation
        float lim = 0.95f;
        if (out_mono > lim)  out_mono = lim + (out_mono - lim) / (1.0f + (out_mono - lim));
        if (out_mono < -lim) out_mono = -lim + (out_mono + lim) / (1.0f - (out_mono + lim));

        // Compressor: normal or "MAX COMP" (B_8) — MAX COMP uses negative ratio (Omnipressor-style) for near-full volume while preserving dynamics
        float in_peak = fabsf(out_mono);
        float env_coeff = (in_peak > comp_env) ? (1.0f - expf(-1.0f / (comp_attack * 48000.0f)))
                                               : (1.0f - expf(-1.0f / (comp_release * 48000.0f)));
        comp_env += env_coeff * (in_peak - comp_env);
        float gain = 1.0f;
        if (comp_env > 1e-6f)
        {
            float thresh = max_comp_on ? comp_thresh_max : comp_thresh;
            float ratio  = max_comp_on ? comp_ratio_max  : comp_ratio;
            float makeup = max_comp_on ? comp_makeup_max : comp_makeup;
            if (max_comp_on)
            {
                // MAX COMP: negative ratio (Omnipressor-style). Below threshold: boost quiet (expansion). Above: compress peaks. Result: near full volume, dynamics preserved.
                if (comp_env <= thresh)
                    gain = powf(thresh / comp_env, 0.5f);  // boost below threshold so average level comes up
                else
                    gain = powf(thresh / comp_env, 1.0f - 1.0f / ratio);  // ratio < 0 -> exponent > 1, tame peaks
            }
            else
            {
                if (comp_env > thresh)
                    gain = powf(thresh / comp_env, 1.0f - 1.0f / ratio);
            }
            gain *= makeup;
        }
        out_mono *= gain;
        // Final soft clip after compressor (MAX COMP can push level high)
        if (out_mono > lim)  out_mono = lim + (out_mono - lim) / (1.0f + (out_mono - lim));
        if (out_mono < -lim) out_mono = -lim + (out_mono + lim) / (1.0f - (out_mono + lim));

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

