// Offline test: process each sample in the samples folder with each of the 8 CV
// parameters swept in turn. Writes to separate output directories per test type.
// Output names: {input_basename}_{test_suffix}.wav
// Build: make cv_sweeps (from test/) or make test_cv_sweeps (from Resynthesis/).

#include "../ResynthEngine.h"
#include "wav_io.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
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

// Separate output directory per test type
static const char kOutCvSweep[]       = "out/cv_sweep";
static const char kOutCvSweepMaxcomp[]= "out/cv_sweep_maxcomp";

// MAX COMP compressor (matches firmware when B_7 is on)
static const float kCompThreshMax = 0.2f;
static const float kCompRatioMax  = -2.0f;
static const float kCompMakeupMax = 2.6f;
static const float kCompAttack    = 0.0003f;
static const float kCompRelease   = 0.05f;
static const float kSoftClipLim   = 0.95f;
static const float kMinAvgLevelDbFs = -60.0f;

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
    { "cv2_smoothing",      "Magnitude smoothing 0.10 -> 0.95 (clear transients -> glassy pads)" },
    { "cv3_flatten",        "Spectral flatten 0.10 -> 1 (original spectrum -> whitened / formant-rich)" },
    { "cv4_tilt",           "Bright/dark tilt -1 -> 1 (even vs odd harmonic emphasis in partial-based mode)" },
    { "cv5_voct",           "V/OCT sweep 1 V -> 4 V over 30 s (looped sine)" },
    { "cv6_timestretch",    "Time stretch 0.5x -> 4x (avoids too-few-grains near-silence)" },
    { "cv7_sparsity",       "Spectral sparsity 0 -> 0.9 (ring-mod / formant-like at high end)" },
    { "cv8_phase_diffusion","Phase diffusion 0 -> 1 (clear to noisy/metallic)" },
};
static const size_t kNumCvTests = sizeof(kCvTests) / sizeof(kCvTests[0]);

// Discover all .wav files in a directory. Returns paths like "samples/foo.wav".
static std::vector<std::string> discover_wav_files(const char* dir)
{
    std::vector<std::string> out;
#ifdef _WIN32
    std::string pattern = std::string(dir) + "\\*.wav";
    struct _finddata_t fd;
    intptr_t h = _findfirst(pattern.c_str(), &fd);
    if (h == -1) return out;
    do {
        if (!(fd.attrib & _A_SUBDIR) && strstr(fd.name, ".wav"))
            out.push_back(std::string(dir) + "/" + fd.name);
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        size_t len = strlen(e->d_name);
        if (len > 4 && strcmp(e->d_name + len - 4, ".wav") == 0)
            out.push_back(std::string(dir) + "/" + e->d_name);
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

static void apply_max_comp(float* buf, size_t num_frames)
{
    float env = 0.0f;
    const float attack_coeff  = 1.0f - std::exp(-1.0f / (kCompAttack * (float)kSampleRate));
    const float release_coeff = 1.0f - std::exp(-1.0f / (kCompRelease * (float)kSampleRate));
    for (size_t i = 0; i < num_frames; ++i) {
        float x = buf[i];
        if (x > kSoftClipLim)  x = kSoftClipLim + (x - kSoftClipLim) / (1.0f + (x - kSoftClipLim));
        if (x < -kSoftClipLim) x = -kSoftClipLim + (x + kSoftClipLim) / (1.0f - (x + kSoftClipLim));
        float in_peak = std::fabs(x);
        float coeff = (in_peak > env) ? attack_coeff : release_coeff;
        env += coeff * (in_peak - env);
        float gain = 1.0f;
        if (env > 1e-6f) {
            if (env <= kCompThreshMax)
                gain = std::pow(kCompThreshMax / env, 0.5f);
            else
                gain = std::pow(kCompThreshMax / env, 1.0f - 1.0f / kCompRatioMax);
            gain *= kCompMakeupMax;
        }
        x *= gain;
        if (x > kSoftClipLim)  x = kSoftClipLim + (x - kSoftClipLim) / (1.0f + (x - kSoftClipLim));
        if (x < -kSoftClipLim) x = -kSoftClipLim + (x + kSoftClipLim) / (1.0f - (x + kSoftClipLim));
        buf[i] = x;
    }
}

static float compute_rms_dbfs(const float* buf, size_t n)
{
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; ++i) { double x = (double)buf[i]; sum_sq += x * x; }
    double rms = (n > 0 && sum_sq > 0.0) ? std::sqrt(sum_sq / (double)n) : 1e-10;
    return 20.0f * (float)std::log10(rms > 1e-10 ? rms : 1e-10);
}

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
    const char* out_dir,
    bool max_comp_on)
{
    using namespace resynth_engine;
    SimpleResynth resynth;
    Grain grains[kNumGrains];
    resynth.Init();
    // For these tests we force the engine into the same mode as when
    // the PITCH LOCK (B_8) switch is ON on hardware: pitch‑locked
    // grains that follow the V/OCT input.
    resynth.SetPitchLockMode(true);
    for (size_t g = 0; g < kNumGrains; ++g) {
        grains[g].running = false;
        grains[g].index = 0;
    }

    float input_history[kFftSize];
    size_t history_write_pos = 0;
    size_t total_samples_seen = 0;
    float grain_phase = 0.0f;
    std::vector<float> output(num_frames);
    float feedback_state = 0.0f;

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

        // Neutral "glassy" defaults; override the one under test. Default V/OCT = 2 V (C2).
        // Dry/wet 100% wet for sweeps 2–8 so the parameter under test is heard clearly.
        float drywet = 1.0f;
        float smoothing = 0.35f;       // slightly fast smoothing for clear attacks
        float flatten = 0.15f;         // mostly original spectral shape
        float tilt = 0.1f;             // gently bright by default
        float fundamental_hz = 440.0f * powf(2.0f, 2.0f - 4.75f);  // 2 V = C2 (~65.4 Hz)
        float time_scale = 1.0f;
        float sparsity = 0.15f;        // dense spectrum for glassy tones
        float phase_diffusion = 0.1f;  // mostly coherent phase for clarity

        switch (cv_index) {
            case 0: drywet = t; break;  // CV1 sweep: 0% -> 100% wet
            case 1: smoothing = 0.10f + t * 0.85f; break;  // 0.10 -> 0.95 (clear transients -> glassy pads)
            case 2: flatten = 0.10f + t * 0.90f; break;    // 0.10 -> 1 (avoid totally unflattened corner case)
            case 3: tilt = 2.0f * t - 1.0f; break;  // -1..1 (tilt gain clamped in engine)
            case 4: {
                // V/OCT linear sweep 1 V -> 4 V over full duration (e.g. 30 s for cv5)
                float voct_volts = 1.0f + t * 3.0f;
                fundamental_hz = 440.0f * powf(2.0f, voct_volts - 4.75f);
                break;
            }
            case 5: time_scale = 0.5f + t * (4.0f - 0.5f); break;  // 0.5 -> 4 (avoid 0.25x: too few grains → near-silence)
            case 6: sparsity = t; break;          // 0 -> 1 (full spectrum -> very sparse, formant-like clusters)
            case 7:
                phase_diffusion = t;             // 0..1 sweep (coherent -> noisy / diffused)
                break;
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

        // Feedback driven by "smoothing" (CV_2) just like on hardware:
        // fully CCW = 0, fully CW ≈ full feedback, clamped below runaway.
        float max_feedback = 0.85f;
        float feedback = max_feedback * smoothing * smoothing;
        float wet_fb = wet + feedback * feedback_state;
        feedback_state = wet_fb;

        // When no grains are active yet (first kFftSize samples), pass dry to avoid leading silence
        float out_mono = (active_count > 0)
            ? ((1.0f - drywet) * mono_in + drywet * wet_fb)
            : mono_in;

        float rev = reverb.process(out_mono);
        float out_with_plateau = 0.5f * (out_mono + rev);

        output[i] = out_with_plateau;
    }

    if (max_comp_on)
        apply_max_comp(output.data(), num_frames);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%s.wav", out_dir, out_basename, kCvTests[cv_index].name);
    if (!SaveWav(path, output.data(), num_frames, kSampleRate, 1)) {
        fprintf(stderr, "Failed to write %s\n", path);
        return false;
    }
    if (max_comp_on) {
        float rms_dbfs = compute_rms_dbfs(output.data(), num_frames);
        printf("  %s  %.1f dBFS\n", path, (double)rms_dbfs);
        if (rms_dbfs < kMinAvgLevelDbFs) {
            fprintf(stderr, "FAIL: %s avg level %.1f dBFS < %.1f dBFS\n",
                    path, (double)rms_dbfs, (double)kMinAvgLevelDbFs);
            return false;
        }
    } else {
        printf("  %s\n", path);
    }
    return true;
}

int main(int argc, char** argv)
{
    const char* samples_dir = (argc >= 2) ? argv[1] : "samples";
    std::vector<std::string> sample_paths = discover_wav_files(samples_dir);
    if (sample_paths.empty()) {
        fprintf(stderr, "No WAV files in %s. Add 48 kHz WAVs to run tests.\n", samples_dir);
        return 1;
    }

    if (mkdir("out", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create out/\n");
        return 1;
    }
    if (mkdir(kOutCvSweep, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create %s\n", kOutCvSweep);
        return 1;
    }
    if (mkdir(kOutCvSweepMaxcomp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create %s\n", kOutCvSweepMaxcomp);
        return 1;
    }

    static const float kTwoPi = 6.283185307179586f;
    std::vector<float> sine_30s(kVoctSweepNumFrames);
    for (size_t i = 0; i < kVoctSweepNumFrames; ++i)
        sine_30s[i] = 0.3f * sinf(kTwoPi * kSineLoopFreqHz * (float)(i % kSineLoopSamples) / (float)kSampleRate);

    printf("CV sweep tests: %zu sample(s) from %s\n", sample_paths.size(), samples_dir);
    printf("  Output dirs: %s (no MAX COMP), %s (MAX COMP, avg > %.1f dBFS)\n",
           kOutCvSweep, kOutCvSweepMaxcomp, (double)kMinAvgLevelDbFs);
    printf("  cv1–cv4, cv6–cv8: %.1f s each; cv5_voct: %.1f s\n\n", kDurationSec, kVoctSweepDurationSec);

    for (const std::string& inputPath : sample_paths) {
        std::vector<float> mono;
        if (!load_input(inputPath.c_str(), mono, kSampleRate)) {
            fprintf(stderr, "Skip %s (wrong format or not 48 kHz).\n", inputPath.c_str());
            continue;
        }
        const char* path = inputPath.c_str();
        const char* lastSlash = strrchr(path, '/');
        const char* base = lastSlash ? (lastSlash + 1) : path;
        const char* lastDot = strrchr(base, '.');
        size_t baseLen = lastDot ? (size_t)(lastDot - base) : strlen(base);
        char out_basename[256];
        snprintf(out_basename, sizeof(out_basename), "%.*s", (int)baseLen, base);

        printf("[ %s ] -> %s_*.wav\n", inputPath.c_str(), out_basename);

        for (int pass = 0; pass < 2; ++pass) {
            bool max_comp_on = (pass == 1);
            const char* out_dir = max_comp_on ? kOutCvSweepMaxcomp : kOutCvSweep;
            if (max_comp_on) printf("  MAX COMP:\n");
            else printf("  no MAX COMP:\n");
            for (size_t c = 0; c < kNumCvTests; ++c) {
                if (c == 4) {
                    if (!run_one_cv_test(c, sine_30s.data(), kVoctSweepNumFrames, out_basename, out_dir, max_comp_on)) {
                        fprintf(stderr, "CV sweep test %zu (V/OCT) failed\n", c + 1);
                        return 1;
                    }
                } else {
                    if (!run_one_cv_test(c, mono.data(), kNumFrames, out_basename, out_dir, max_comp_on)) {
                        fprintf(stderr, "CV sweep test %zu failed\n", c + 1);
                        return 1;
                    }
                }
            }
        }
    }
    printf("\nDone. Outputs in %s/ and %s/\n", kOutCvSweep, kOutCvSweepMaxcomp);
    return 0;
}
