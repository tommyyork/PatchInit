// Offline test: process church bells (or default sample) with each of the 8 CV
// parameters swept in turn. For the first sweep (CV_1) dry/wet goes 0→100% wet;
// for all other sweeps dry/wet is fixed at 100% wet. Outputs one WAV per
// parameter with a descriptive name.
// Build: make cv_sweeps (from test/) or make test_cv_sweeps (from Resynthesis/).

#include "../ResynthEngine.h"
#include "wav_io.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static constexpr unsigned kSampleRate = 48000;
static constexpr float kBpm = 120.0f;
static constexpr unsigned kQuarterNotes = 14;
static constexpr float kDurationSec = (kQuarterNotes * 60.0f) / kBpm;
static constexpr size_t kNumFrames = (size_t)(kSampleRate * kDurationSec + 0.5f);

// V/OCT sweep test: 30 s, sine loop, 1 V to 4 V
static constexpr float kVoctSweepDurationSec = 30.0f;
static constexpr size_t kVoctSweepNumFrames = (size_t)(kSampleRate * kVoctSweepDurationSec + 0.5f);
static constexpr float kSineLoopFreqHz = 220.0f;
static constexpr size_t kSineLoopSamples = kSampleRate;  // 1 s loop

static const char kOutDir[] = "out";

// Very simple mono reverb to approximate having Plateau (B_7) engaged during tests.
// Uses a feedback delay with a one-pole lowpass in the feedback path.
struct SimplePlateauSim {
    static constexpr float kDelaySeconds = 0.7f;
    static constexpr float kFeedback = 0.75f;
    static constexpr float kDamp = 0.3f;

    std::vector<float> buffer;
    size_t index = 0;
    float lpState = 0.0f;

    void init(unsigned sampleRate) {
        size_t delaySamples = static_cast<size_t>(kDelaySeconds * sampleRate);
        if (delaySamples < 1)
            delaySamples = 1;
        buffer.assign(delaySamples, 0.0f);
        index = 0;
        lpState = 0.0f;
    }

    float process(float in) {
        float y = buffer[index];
        // Lowpass the feedback path
        lpState = (1.0f - kDamp) * y + kDamp * lpState;
        buffer[index] = in + kFeedback * lpState;
        index++;
        if (index >= buffer.size())
            index = 0;
        return y;
    }
};

struct CvSweepTest {
    const char* name;           // short name for filename
    const char* description;    // one-line description
};

static const CvSweepTest kCvTests[] = {
    { "cv1_drywet",         "Dry/wet crossfade 0% -> 100% wet" },
    { "cv2_smoothing",      "Magnitude smoothing 0.25 -> 1 (avoids freeze-first-frame silence)" },
    { "cv3_flatten",        "Spectral flatten 0 -> 1" },
    { "cv4_tilt",           "Bright/dark tilt -1 -> 1 (tilt gain clamped)" },
    { "cv5_voct",           "V/OCT sweep 1 V -> 4 V over 30 s (looped sine)" },
    { "cv6_timestretch",    "Time stretch 0.5x -> 4x (avoids too-few-grains near-silence)" },
    { "cv7_sparsity",       "Spectral sparsity 0 -> 0.9 (ring-mod / formant-like at high end)" },
    { "cv8_phase_diffusion","Phase diffusion 0 -> 1 (clear to noisy/metallic)" },
};
static const size_t kNumCvTests = sizeof(kCvTests) / sizeof(kCvTests[0]);

static bool load_input(const char* path, std::vector<float>& mono, unsigned sampleRate)
{
    std::vector<float> inputSamples;
    WavInfo info;
    if (!LoadWav(path, inputSamples, info))
        return false;
    if (info.sampleRate != sampleRate)
        return false;
    size_t n = info.numFrames;
    mono.resize(kNumFrames);
    if (info.numChannels == 1) {
        for (size_t i = 0; i < kNumFrames; ++i)
            mono[i] = i < n ? inputSamples[i] : 0.0f;
    } else {
        for (size_t i = 0; i < kNumFrames; ++i)
            mono[i] = i < n ? 0.5f * (inputSamples[i * 2] + inputSamples[i * 2 + 1]) : 0.0f;
    }
    return true;
}

static bool run_one_cv_test(
    size_t cv_index,
    const float* mono,
    size_t num_frames,
    const char* out_basename,
    const char* out_dir)
{
    using namespace resynth_engine;
    SimpleResynth resynth;
    Grain grains[kNumGrains];
    resynth.Init();
    for (size_t g = 0; g < kNumGrains; ++g) {
        grains[g].running = false;
        grains[g].index = 0;
    }

    float input_history[kFftSize];
    size_t history_write_pos = 0;
    size_t total_samples_seen = 0;
    float grain_phase = 0.0f;
    std::vector<float> output(num_frames);

    // Simulate B_7 (Plateau) toggled on: run output through a simple reverb and
    // mix 50/50 dry/wet, similar to the hardware path.
    SimplePlateauSim reverb;
    reverb.init(kSampleRate);

    auto startNextGrain = [&]() {
        size_t idx = 0;
        for (size_t g = 0; g < kNumGrains; ++g) {
            if (!grains[g].running) { idx = g; break; }
        }
        resynth.StartGrainFromHistory(input_history, history_write_pos, grains[idx]);
    };

    for (size_t i = 0; i < num_frames; ++i) {
        float t = (float)i / (float)num_frames;  // 0 .. 1 over this test's duration

        // Neutral values; override the one under test. Default V/OCT = 2 V (C2).
        // Dry/wet 100% wet for sweeps 2–8 so the parameter under test is heard clearly.
        float drywet = 1.0f;
        float smoothing = 0.5f;
        float flatten = 0.5f;
        float tilt = 0.0f;
        float fundamental_hz = 440.0f * powf(2.0f, 2.0f - 4.75f);  // 2 V = C2 (~65.4 Hz)
        float time_scale = 1.0f;
        float sparsity = 0.5f;
        float phase_diffusion = 0.5f;

        switch (cv_index) {
            case 0: drywet = t; break;  // CV1 sweep: 0% -> 100% wet
            case 1: smoothing = 0.25f + t * 0.75f; break;  // 0.25 -> 1 (avoid 0: would freeze first frame → silence)
            case 2: flatten = t; break;
            case 3: tilt = 2.0f * t - 1.0f; break;  // -1..1 (tilt gain clamped in engine)
            case 4: {
                // V/OCT linear sweep 1 V -> 4 V over full duration (e.g. 30 s for cv5)
                float voct_volts = 1.0f + t * 3.0f;
                fundamental_hz = 440.0f * powf(2.0f, voct_volts - 4.75f);
                break;
            }
            case 5: time_scale = 0.5f + t * (4.0f - 0.5f); break;  // 0.5 -> 4 (avoid 0.25x: too few grains → near-silence)
            case 6: sparsity = t * 0.9f; break;   // 0 -> 0.9 (avoid 1: can sound like dropouts on sparse material)
            case 7: phase_diffusion = t; break;   // 0..1 sweep
            default: break;
        }

        resynth.SetSmoothing(smoothing);
        resynth.SetSpectralFlatten(flatten);
        resynth.SetBrightDark(tilt);
        resynth.SetFundamentalHz(fundamental_hz, kSampleRate);
        resynth.SetSparsity(sparsity);
        resynth.SetPhaseDiffusion(phase_diffusion);

        float mono_in = mono[i];
        input_history[history_write_pos] = mono_in;
        history_write_pos = (history_write_pos + 1) % kFftSize;
        ++total_samples_seen;

        if (total_samples_seen >= kFftSize) {
            grain_phase += time_scale;
            while (grain_phase >= (float)kHopSize) {
                startNextGrain();
                float hop       = (float)kHopSize;
                float jitterMul = resynth_engine::SimpleResynth::RandUniform(0.7f, 1.3f);
                grain_phase -= hop * jitterMul;
            }
        }

        float wet = 0.0f;
        size_t active_count = 0;
        for (size_t g = 0; g < kNumGrains; ++g) {
            if (grains[g].running) {
                wet += grains[g].Process();
                ++active_count;
            }
        }
        if (active_count > 0)
            wet *= 1.0f / ((float)kHopDenom * (float)active_count);

        // When no grains are active yet (first kFftSize samples), pass dry to avoid leading silence
        float out_mono = (active_count > 0)
            ? ((1.0f - drywet) * mono_in + drywet * wet)
            : mono_in;

        float rev = reverb.process(out_mono);
        float out_with_plateau = 0.5f * (out_mono + rev);

        output[i] = out_with_plateau;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%s.wav", out_dir, out_basename, kCvTests[cv_index].name);
    if (!SaveWav(path, output.data(), num_frames, kSampleRate, 1)) {
        fprintf(stderr, "Failed to write %s\n", path);
        return false;
    }
    printf("  %s\n", path);
    return true;
}

int main(int argc, char** argv)
{
    const char* inputPath = (argc >= 2) ? argv[1] : "samples/church_bells.wav";
    std::vector<float> mono;
    if (!load_input(inputPath, mono, kSampleRate)) {
        fprintf(stderr, "No WAV at %s or wrong format (need 48 kHz). Use samples/church_bells.wav.\n", inputPath);
        return 1;
    }

    const char* lastSlash = strrchr(inputPath, '/');
    const char* base = lastSlash ? (lastSlash + 1) : inputPath;
    const char* lastDot = strrchr(base, '.');
    size_t baseLen = lastDot ? (size_t)(lastDot - base) : strlen(base);
    char out_basename[256];
    snprintf(out_basename, sizeof(out_basename), "%.*s", (int)baseLen, base);

    if (mkdir(kOutDir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create output directory %s\n", kOutDir);
        return 1;
    }

    static const float kTwoPi = 6.283185307179586f;
    std::vector<float> sine_30s(kVoctSweepNumFrames);
    for (size_t i = 0; i < kVoctSweepNumFrames; ++i)
        sine_30s[i] = 0.3f * sinf(kTwoPi * kSineLoopFreqHz * (float)(i % kSineLoopSamples) / (float)kSampleRate);

    printf("CV parameter sweep tests (8 files):\n");
    printf("  cv1–cv4, cv6–cv8: %.1f s each; cv5_voct: %.1f s (V/OCT 1 V -> 4 V, looped sine)\n",
           kDurationSec, kVoctSweepDurationSec);

    for (size_t c = 0; c < kNumCvTests; ++c) {
        if (c == 4) {
            if (!run_one_cv_test(c, sine_30s.data(), kVoctSweepNumFrames, out_basename, kOutDir)) {
                fprintf(stderr, "CV sweep test %zu (V/OCT) failed\n", c + 1);
                return 1;
            }
        } else {
            if (!run_one_cv_test(c, mono.data(), kNumFrames, out_basename, kOutDir)) {
                fprintf(stderr, "CV sweep test %zu failed\n", c + 1);
                return 1;
            }
        }
    }
    printf("Done. Outputs in %s/\n", kOutDir);
    return 0;
}
