// Offline test for the phase vocoder resynthesis engine.
// Output length = duration of V/OCT CV movement: 14 quarter notes at 120 BPM (7 s).
// V/OCT CV steps once per quarter note across two diatonic octaves (14 steps total).
// Input is truncated or padded to that length so the test output reflects the full
// pitch movement in one file. Output: Resynthesis/out/<basename>_resynth_processed.wav

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
static constexpr unsigned kSamplesPerStep = (unsigned)(kSampleRate * 60.0f / kBpm + 0.5f);

static const char kOutDir[] = "out";
static const char kOutSuffix[] = "_resynth_processed.wav";

// Two diatonic octaves: 14 steps (one per quarter note), 0–24 semitones
static const int kDiatonicTwoOctaves[] = {
    0, 2, 4, 5, 7, 9, 11, 12,
    14, 16, 17, 19, 21, 23
};
static const size_t kNumSteps = sizeof(kDiatonicTwoOctaves) / sizeof(kDiatonicTwoOctaves[0]);

int main(int argc, char** argv)
{
    const char* inputPath = nullptr;
    if (argc >= 2)
        inputPath = argv[1];
    else
        inputPath = "samples/church_bells.wav";

    std::vector<float> inputSamples;
    WavInfo info;
    if (!LoadWav(inputPath, inputSamples, info))
    {
        fprintf(stderr, "No WAV at %s — using %.1f s test tone (48 kHz). Add a royalty-free sample (e.g. BBC church bells) for a better test.\n",
                inputPath, kDurationSec);
        inputSamples.resize(kNumFrames);
        for (size_t i = 0; i < kNumFrames; ++i)
            inputSamples[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / (float)kSampleRate);
        info.sampleRate = kSampleRate;
        info.numChannels = 1;
        info.numFrames = kNumFrames;
        inputPath = "samples/test_tone.wav";
    }
    else if (info.sampleRate != kSampleRate)
    {
        fprintf(stderr, "Expected %u Hz; got %u Hz. Resampling not implemented.\n", kSampleRate, info.sampleRate);
        return 1;
    }

    size_t numFramesIn = info.numFrames;
    std::vector<float> mono(kNumFrames);
    if (info.numChannels == 1)
    {
        for (size_t i = 0; i < kNumFrames; ++i)
            mono[i] = i < numFramesIn ? inputSamples[i] : 0.0f;
    }
    else
    {
        for (size_t i = 0; i < kNumFrames; ++i)
        {
            if (i < numFramesIn)
                mono[i] = 0.5f * (inputSamples[i * 2] + inputSamples[i * 2 + 1]);
            else
                mono[i] = 0.0f;
        }
    }

    using namespace resynth_engine;
    SimpleResynth resynth;
    Grain grains[kNumGrains];
    resynth.Init();
    for (size_t g = 0; g < kNumGrains; ++g)
    {
        grains[g].running = false;
        grains[g].index = 0;
    }

    float drywet = 1.0f;  // 100% wet so output is resynthesized only (no dry mix)
    resynth.SetSmoothing(0.4f);
    resynth.SetSpectralFlatten(0.0f);
    resynth.SetBrightDark(0.0f);
    // Defaults chosen to show some metallic character without being too thin
    resynth.SetSparsity(0.35f);
    resynth.SetPhaseDiffusion(0.4f);
    // V/OCT: 0 V = C0 (~16.35 Hz), 1 V = C1 (~32.7 Hz), 2 V = C2 (~65.4 Hz); semitones/12 = volts
    resynth.SetFundamentalHz(440.0f * powf(2.0f, -4.75f), kSampleRate);

    const float time_scale = 1.0f;  // neutral (no time stretch) for offline test

    float input_history[kFftSize];
    size_t history_write_pos = 0;
    size_t total_samples_seen = 0;
    float grain_phase = 0.0f;
    std::vector<float> output(kNumFrames);

    size_t stepIndex = 0;
    unsigned samplesInCurrentStep = 0;

    auto startNextGrain = [&]()
    {
        size_t idx = 0;
        for (size_t g = 0; g < kNumGrains; ++g)
        {
            if (!grains[g].running) { idx = g; break; }
        }
        resynth.StartGrainFromHistory(input_history, history_write_pos, grains[idx]);
    };

    for (size_t i = 0; i < kNumFrames; ++i)
    {
        if (samplesInCurrentStep >= kSamplesPerStep)
        {
            samplesInCurrentStep = 0;
            stepIndex = (stepIndex < kNumSteps - 1) ? (stepIndex + 1) : stepIndex;
            int semitones = kDiatonicTwoOctaves[stepIndex];
            // V/OCT: semitones/12 = volts (0 V = C0, 1 V = C1, 2 V = C2)
            float fundamental_hz = 440.0f * powf(2.0f, semitones / 12.0f - 4.75f);
            resynth.SetFundamentalHz(fundamental_hz, kSampleRate);
        }
        ++samplesInCurrentStep;

        float mono_in = mono[i];
        input_history[history_write_pos] = mono_in;
        history_write_pos = (history_write_pos + 1) % kFftSize;
        ++total_samples_seen;

        if (total_samples_seen >= kFftSize)
        {
            grain_phase += time_scale;
            while (grain_phase >= (float)kHopSize)
            {
                startNextGrain();
                float hop       = (float)kHopSize;
                float jitterMul = resynth_engine::SimpleResynth::RandUniform(0.7f, 1.3f);
                grain_phase -= hop * jitterMul;
            }
        }

        float wet = 0.0f;
        size_t active_count = 0;
        for (size_t g = 0; g < kNumGrains; ++g)
        {
            if (grains[g].running)
            {
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
        // Soft clip to reduce peaks and level variation
        float lim = 0.95f;
        if (out_mono > lim)  out_mono = lim + (out_mono - lim) / (1.0f + (out_mono - lim));
        if (out_mono < -lim) out_mono = -lim + (out_mono + lim) / (1.0f - (out_mono + lim));
        output[i] = out_mono;
    }

    const char* suffix = kOutSuffix;
    const char* lastSlash = strrchr(inputPath, '/');
    const char* base = lastSlash ? (lastSlash + 1) : inputPath;
    const char* lastDot = strrchr(base, '.');
    size_t baseLen = lastDot ? (size_t)(lastDot - base) : strlen(base);
    size_t outPathLen = strlen(kOutDir) + 1 + baseLen + strlen(suffix) + 1;
    std::vector<char> outPath(outPathLen);
    snprintf(outPath.data(), outPathLen, "%s/%.*s%s", kOutDir, (int)baseLen, base, suffix);

    if (mkdir(kOutDir, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Failed to create output directory %s\n", kOutDir);
        return 1;
    }

    if (!SaveWav(outPath.data(), output.data(), kNumFrames, kSampleRate, 1))
    {
        fprintf(stderr, "Failed to write %s\n", outPath.data());
        return 1;
    }

    FILE* verify = fopen(outPath.data(), "rb");
    if (!verify)
    {
        fprintf(stderr, "Failed to verify output file %s\n", outPath.data());
        return 1;
    }
    fseek(verify, 0, SEEK_END);
    long size = ftell(verify);
    fclose(verify);
    if (size <= 0)
    {
        fprintf(stderr, "Output file is empty or zero size: %s\n", outPath.data());
        return 1;
    }

    printf("Wrote %s (%ld bytes)\n", outPath.data(), (long)size);
    return 0;
}
