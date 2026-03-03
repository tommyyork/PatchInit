#include "rack.hpp"
#include <cmath>
#include <cstdint>

using namespace rack;

extern Plugin* pluginInstance;

// Allow passing a descriptive name for the embedded binary / project
#ifndef VCV_BINARY_NAME
#define VCV_BINARY_NAME "Resynthesis"
#endif

// --------------------------------------------------------------------------
// Simple helpers roughly mirroring common libDaisy utilities
// --------------------------------------------------------------------------

static inline float clampf(float x, float lo, float hi) {
	if (x < lo) return lo;
	if (x > hi) return hi;
	return x;
}

static inline float fmap01(float x, float lo, float hi) {
	float t = clampf(x, 0.0f, 1.0f);
	return lo + (hi - lo) * t;
}

// --------------------------------------------------------------------------
// FFT + resynthesis core (adapted from patch_sm/Resynthesis/Resynthesis.cpp)
// --------------------------------------------------------------------------

struct Complex {
	float re;
	float im;
};

static void FftInPlace(Complex* data, size_t n, bool inverse) {
	// Bit-reversal permutation
	size_t j = 0;
	for (size_t i = 1; i < n; ++i) {
		size_t bit = n >> 1;
		while (j & bit) {
			j ^= bit;
			bit >>= 1;
		}
		j |= bit;
		if (i < j) {
			Complex tmp = data[i];
			data[i] = data[j];
			data[j] = tmp;
		}
	}

	// Cooley-Tukey stages
	for (size_t len = 2; len <= n; len <<= 1) {
		float ang = 2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
		if (!inverse)
			ang = -ang;
		float wlenRe = cosf(ang);
		float wlenIm = sinf(ang);
		for (size_t i = 0; i < n; i += len) {
			float wRe = 1.0f;
			float wIm = 0.0f;
			for (size_t j2 = 0; j2 < len / 2; ++j2) {
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

				float nextWRe = wRe * wlenRe - wIm * wlenIm;
				float nextWIm = wRe * wlenIm + wIm * wlenRe;
				wRe = nextWRe;
				wIm = nextWIm;
			}
		}
	}

	// Scale for inverse transform
	if (inverse) {
		float invN = 1.0f / static_cast<float>(n);
		for (size_t i = 0; i < n; ++i) {
			data[i].re *= invN;
			data[i].im *= invN;
		}
	}
}

static constexpr size_t kFftBits   = 8;        // 2^8 = 256
static constexpr size_t kFftSize   = 1 << kFftBits;
static constexpr size_t kHopDenom  = 4;
static constexpr size_t kHopSize   = kFftSize / kHopDenom;
static constexpr size_t kNumBins   = kFftSize / 2;
static constexpr size_t kNumGrains = 4;
static constexpr float  kTwoPi     = 2.0f * static_cast<float>(M_PI);

struct Grain {
	float  buffer[kFftSize];
	size_t index = 0;
	bool   running = false;

	void start() {
		index   = 0;
		running = true;
	}

	float process() {
		if (!running)
			return 0.0f;
		float v = buffer[index];
		++index;
		if (index >= kFftSize) {
			running = false;
			index   = 0;
		}
		return v;
	}
};

struct SimpleResynth {
	float window[kFftSize];

	float prev_phase[kNumBins + 1];
	float synth_phase[kNumBins + 1];
	float mag_smooth[kNumBins + 1];

	bool  primed;
	float mag_smooth_coeff;
	float pitch_ratio;        // 1.f = unison, 2.f = +12st, 0.5f = -12st
	float spectral_flatten;   // 0 = no change, 1 = fully flat
	float bright_dark;        // -1 = dark, 0 = neutral, +1 = bright
	float sparsity;           // 0 = full spectrum, 1 = only strongest bins
	float phase_diffusion;    // 0 = coherent phase, 1 = noisy phase
	float last_frame_spectral_energy;  // RMS of mag_smooth (0..1 scale)

	void init() {
		for (size_t n = 0; n < kFftSize; ++n) {
			window[n] = 0.5f
			          * (1.0f - cosf(kTwoPi * static_cast<float>(n)
			                         / static_cast<float>(kFftSize - 1)));
		}

		for (size_t i = 0; i <= kNumBins; ++i) {
			prev_phase[i]  = 0.0f;
			synth_phase[i] = 0.0f;
			mag_smooth[i]  = 0.0f;
		}
		primed                     = false;
		mag_smooth_coeff           = 0.3f;
		pitch_ratio                = 1.0f;
		spectral_flatten           = 0.0f;
		bright_dark                = 0.0f;
		sparsity                   = 0.0f;
		phase_diffusion            = 0.0f;
		last_frame_spectral_energy = 0.0f;
	}

	void setSmoothing(float alpha) {
		mag_smooth_coeff = clampf(alpha, 0.0f, 1.0f);
	}

	void setPitchRatio(float ratio) {
		pitch_ratio = clampf(ratio, 0.25f, 4.0f);
	}

	void setSpectralFlatten(float amount) {
		spectral_flatten = clampf(amount, 0.0f, 1.0f);
	}

	void setBrightDark(float tilt) {
		bright_dark = clampf(tilt, -1.0f, 1.0f);
	}

	void setSparsity(float amount) {
		sparsity = clampf(amount, 0.0f, 1.0f);
	}

	void setPhaseDiffusion(float amount) {
		phase_diffusion = clampf(amount, 0.0f, 1.0f);
	}

	static float princArg(float x) {
		// x is in cycles
		x = x - floorf(x);
		if (x > 0.5f)
			x -= 1.0f;
		return x;
	}

	static float randUniform(float lo, float hi) {
		static uint32_t state = 1u;
		state = state * 1664525u + 1013904223u;
		float t = static_cast<float>(state & 0x00FFFFFFu)
		        / static_cast<float>(0x01000000u);
		return lo + (hi - lo) * t;
	}

	void startGrainFromHistory(const float* history, size_t history_write_pos, Grain& grain) {
		Complex spectrum[kFftSize];

		size_t idx = history_write_pos;
		for (size_t n = 0; n < kFftSize; ++n) {
			float s       = history[idx];
			spectrum[n].re = s * window[n];
			spectrum[n].im = 0.0f;
			idx            = (idx + 1) % kFftSize;
		}

		FftInPlace(spectrum, kFftSize, false);

		// Analysis and phase propagation
		for (size_t k = 0; k <= kNumBins; ++k) {
			float re = spectrum[k].re;
			float im = spectrum[k].im;

			float mag   = std::sqrt(re * re + im * im);
			float phase = std::atan2(im, re) / kTwoPi; // cycles

			if (!primed) {
				prev_phase[k]  = phase;
				synth_phase[k] = phase;
				mag_smooth[k]  = mag;
				continue;
			}

			float omega_bin      = static_cast<float>(k) / static_cast<float>(kFftSize);
			float delta_expected = omega_bin * static_cast<float>(kHopSize);
			float delta          = phase - prev_phase[k];
			delta -= delta_expected;
			delta = princArg(delta);

			float omega_instant = omega_bin + delta / static_cast<float>(kHopSize);

			// Magnitude smoothing
			mag_smooth[k] = mag_smooth[k]
			              + mag_smooth_coeff * (mag - mag_smooth[k]);

			synth_phase[k] = synth_phase[k] + omega_instant * static_cast<float>(kHopSize);
			prev_phase[k]  = phase;
		}

		primed = true;

		// Spectral shaping: flatten, bright/dark, sparsity, phase diffusion
		float sum_mag = 0.0f;
		float max_mag = 0.0f;
		for (size_t k = 1; k < kNumBins; ++k) {
			sum_mag += mag_smooth[k];
			if (mag_smooth[k] > max_mag)
				max_mag = mag_smooth[k];
		}
		float mean_mag = sum_mag / static_cast<float>(kNumBins > 1 ? kNumBins - 1 : 1);

		for (size_t k = 0; k <= kNumBins; ++k) {
			// Spectral flatten: blend toward equal magnitude
			mag_smooth[k] = mag_smooth[k] * (1.0f - spectral_flatten)
			              + mean_mag * spectral_flatten;
			// Bright/dark tilt: gain proportional to bin index
			float tilt_gain = 1.0f + bright_dark * (2.0f * static_cast<float>(k) / static_cast<float>(kNumBins) - 1.0f);
			if (tilt_gain < 0.01f)
				tilt_gain = 0.01f;
			mag_smooth[k] *= tilt_gain;
		}

		// Sparsity: zero out bins below a threshold relative to the strongest bin
		if (sparsity > 0.0f && max_mag > 0.0f) {
			float thresh = max_mag * (0.9f * sparsity);
			for (size_t k = 0; k <= kNumBins; ++k) {
				if (mag_smooth[k] < thresh)
					mag_smooth[k] = 0.0f;
			}
		}

		// Phase diffusion: add random phase offsets, stronger at higher bins
		if (phase_diffusion > 0.0f) {
			for (size_t k = 0; k <= kNumBins; ++k) {
				float w      = static_cast<float>(k) / static_cast<float>(kNumBins);
				float amount = phase_diffusion * w; // cycles
				float jitter = randUniform(-amount, amount);
				synth_phase[k] += jitter;
			}
		}

		// Per-frame spectral energy (RMS of magnitudes) for CV out
		{
			float sum_sq = 0.0f;
			for (size_t k = 0; k <= kNumBins; ++k)
				sum_sq += mag_smooth[k] * mag_smooth[k];
			float rms = std::sqrt(sum_sq);
			float n   = static_cast<float>(kNumBins + 1);
			last_frame_spectral_energy = (n > 0.0f) ? (rms / n) : 0.0f;
		}

		// Build output spectrum with pitch shift (bin remap)
		for (size_t k_out = 0; k_out <= kNumBins; ++k_out) {
			float  k_src_f = static_cast<float>(k_out) / pitch_ratio;
			size_t lo      = static_cast<size_t>(k_src_f);
			size_t hi      = lo + 1;
			float  frac    = k_src_f - static_cast<float>(lo);

			float mag_out, phase_out;
			if (hi > kNumBins) {
				mag_out   = 0.0f;
				phase_out = 0.0f;
			}
			else if (lo == 0 && hi == 1) {
				mag_out   = (1.0f - frac) * mag_smooth[0] + frac * mag_smooth[1];
				phase_out = (1.0f - frac) * synth_phase[0] + frac * synth_phase[1];
			}
			else {
				mag_out   = (1.0f - frac) * mag_smooth[lo] + frac * mag_smooth[hi];
				phase_out = (1.0f - frac) * synth_phase[lo] + frac * synth_phase[hi];
			}

			float out_phase   = phase_out * kTwoPi;
			spectrum[k_out].re = mag_out * cosf(out_phase);
			spectrum[k_out].im = mag_out * sinf(out_phase);
		}

		// Reconstruct negative frequencies for a real IFFT
		for (size_t k = 1; k < kNumBins; ++k) {
			spectrum[kFftSize - k].re = spectrum[k].re;
			spectrum[kFftSize - k].im = -spectrum[k].im;
		}

		FftInPlace(spectrum, kFftSize, true);

		// Window again and copy into grain buffer
		for (size_t n = 0; n < kFftSize; ++n) {
			grain.buffer[n] = spectrum[n].re * window[n];
		}
		grain.start();
	}
};

// --------------------------------------------------------------------------
// Rack module: virtualized Patch SM running SimpleResynth
// --------------------------------------------------------------------------

struct PatchSMHarnessModule : Module {
	enum ParamIds {
		B7_PARAM,   // Reverb toggle (currently unused; reserved)
		B8_PARAM,   // CV bank swap
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		IN_R_INPUT,
		CV1_INPUT,
		CV2_INPUT,
		CV3_INPUT,
		CV4_INPUT,
		CV5_INPUT,
		CV6_INPUT,
		CV7_INPUT,
		CV8_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		CVOUT1_OUTPUT,
		CVOUT2_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	SimpleResynth resynth;
	Grain         grains[kNumGrains];

	float  input_history[kFftSize];
	size_t history_write_pos  = 0;
	size_t total_samples_seen = 0;
	float  grain_phase        = 0.0f;
	float  time_scale         = 1.0f;
	float  cv_energy_smooth   = 0.0f;
	const float cv_energy_coeff = 0.002f;

	PatchSMHarnessModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(B7_PARAM, 0.0f, 1.0f, 0.0f, "Reverb (reserved)");
		configParam(B8_PARAM, 0.0f, 1.0f, 0.0f, "CV bank swap");

		configInput(IN_L_INPUT, "Audio L");
		configInput(IN_R_INPUT, "Audio R");
		configInput(CV1_INPUT, "CV 1");
		configInput(CV2_INPUT, "CV 2");
		configInput(CV3_INPUT, "CV 3");
		configInput(CV4_INPUT, "CV 4");
		configInput(CV5_INPUT, "CV 5");
		configInput(CV6_INPUT, "CV 6");
		configInput(CV7_INPUT, "CV 7");
		configInput(CV8_INPUT, "CV 8");

		configOutput(OUT_L_OUTPUT, "Audio L");
		configOutput(OUT_R_OUTPUT, "Audio R");
		configOutput(CVOUT1_OUTPUT, "Spectral energy (smoothed)");
		configOutput(CVOUT2_OUTPUT, "Spectral energy (raw)");

		onReset();
	}

	void onReset() override {
		resynth.init();
		for (size_t g = 0; g < kNumGrains; ++g) {
			grains[g].running = false;
			grains[g].index   = 0;
		}
		for (size_t i = 0; i < kFftSize; ++i) {
			input_history[i] = 0.0f;
		}
		history_write_pos  = 0;
		total_samples_seen = 0;
		grain_phase        = 0.0f;
		time_scale         = 1.0f;
		cv_energy_smooth   = 0.0f;
	}

	void startNextGrain() {
		size_t idx = 0;
		for (size_t g = 0; g < kNumGrains; ++g) {
			if (!grains[g].running) {
				idx = g;
				break;
			}
		}
		resynth.startGrainFromHistory(input_history, history_write_pos, grains[idx]);
	}

	void process(const ProcessArgs& args) override {
		// Normalize audio to roughly -1..1 from Rack's +/-5V convention
		float inL = inputs[IN_L_INPUT].isConnected()
		              ? clampf(inputs[IN_L_INPUT].getVoltage() / 5.0f, -1.5f, 1.5f)
		              : 0.0f;
		float inR = inputs[IN_R_INPUT].isConnected()
		              ? clampf(inputs[IN_R_INPUT].getVoltage() / 5.0f, -1.5f, 1.5f)
		              : inL;
		float mono = 0.5f * (inL + inR);

		// Read CVs in 0..1 space from 0..10V
		auto readCvNorm = [&](int inputId) -> float {
			if (!inputs[inputId].isConnected())
				return 0.5f; // mid position if unpatched
			return clampf(inputs[inputId].getVoltage() / 10.0f, 0.0f, 1.0f);
		};

		bool swap_cv    = params[B8_PARAM].getValue() > 0.5f;
		bool plateau_on = params[B7_PARAM].getValue() > 0.5f;
		(void)plateau_on; // reserved for future use

		float v1 = readCvNorm(CV1_INPUT);
		float v2 = readCvNorm(CV2_INPUT);
		float v3 = readCvNorm(CV3_INPUT);
		float v4 = readCvNorm(CV4_INPUT);
		float v5 = readCvNorm(CV5_INPUT);
		float v6 = readCvNorm(CV6_INPUT);
		float v7 = readCvNorm(CV7_INPUT);
		float v8 = readCvNorm(CV8_INPUT);

		float drywet_knob  = swap_cv ? v5 : v1;
		float smooth_knob  = swap_cv ? v6 : v2;
		float flatten_knob = swap_cv ? v7 : v3;
		float tilt_knob    = swap_cv ? v8 : v4;
		float voct_cv      = swap_cv ? v1 : v5;
		float time_cv      = swap_cv ? v2 : v6;
		float sparsity_cv  = swap_cv ? v3 : v7;
		float diffus_cv    = swap_cv ? v4 : v8;

		float drywet = fmap01(drywet_knob, 0.0f, 1.0f);
		resynth.setSmoothing(fmap01(smooth_knob, 0.0f, 1.0f));
		resynth.setSpectralFlatten(fmap01(flatten_knob, 0.0f, 1.0f));
		resynth.setBrightDark(fmap01(tilt_knob, -1.0f, 1.0f));

		// 1.2V/oct equivalent: map CV to semitones 0..60, center ~24 for unison
		float semitones   = fmap01(voct_cv, 0.0f, 60.0f);
		float pitch_ratio = std::pow(2.0f, (semitones - 24.0f) / 12.0f);
		resynth.setPitchRatio(pitch_ratio);

		// Time-stretch / grain density
		time_scale = fmap01(time_cv, 0.25f, 4.0f);

		// Spectral sparsity and phase diffusion
		resynth.setSparsity(fmap01(sparsity_cv, 0.0f, 1.0f));
		resynth.setPhaseDiffusion(fmap01(diffus_cv, 0.0f, 1.0f));

		// Push into input history ring buffer
		input_history[history_write_pos] = mono;
		history_write_pos = (history_write_pos + 1) % kFftSize;
		++total_samples_seen;

		// Launch new grains once we have a full buffer; rate set by time_scale
		if (total_samples_seen >= kFftSize) {
			grain_phase += time_scale;
			while (grain_phase >= static_cast<float>(kHopSize)) {
				startNextGrain();
				grain_phase -= static_cast<float>(kHopSize);
			}
		}

		// Sum active grains (overlap-add)
		float wet = 0.0f;
		for (size_t g = 0; g < kNumGrains; ++g) {
			if (grains[g].running)
				wet += grains[g].process();
		}
		wet *= 1.0f / static_cast<float>(kHopDenom);

		float out_mono = (1.0f - drywet) * mono + drywet * wet;

		// No external reverb here; out is mono -> stereo copy
		float outL = out_mono;
		float outR = out_mono;

		// Map back to Rack audio volts (~ +/-5V)
		outputs[OUT_L_OUTPUT].setVoltage(5.0f * clampf(outL, -1.5f, 1.5f));
		outputs[OUT_R_OUTPUT].setVoltage(5.0f * clampf(outR, -1.5f, 1.5f));

		// CV outputs: spectral energy (smoothed and raw), 0..5V
		float energy_in = std::fmin(1.0f, resynth.last_frame_spectral_energy * 5.0f);
		cv_energy_smooth += cv_energy_coeff * (energy_in - cv_energy_smooth);
		outputs[CVOUT1_OUTPUT].setVoltage(5.0f * clampf(cv_energy_smooth, 0.0f, 1.0f));
		outputs[CVOUT2_OUTPUT].setVoltage(5.0f * clampf(resynth.last_frame_spectral_energy * 5.0f, 0.0f, 5.0f));
	}
};

// --------------------------------------------------------------------------
// Simple, generic widget
// --------------------------------------------------------------------------

struct PatchSMHarnessWidget : ModuleWidget {
	PatchSMHarnessWidget(PatchSMHarnessModule* module) {
		setModule(module);

		// Panel SVG is designed for physical Eurorack (front-panel fabrication); used here for VCV display.
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PatchSMHarness.svg")));

		// Simple layout: audio I/O at top, CVs and buttons below.
		// Positions are approximate and purely cosmetic.

		// Audio inputs
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 25.0f)), module, PatchSMHarnessModule::IN_L_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.0f, 25.0f)), module, PatchSMHarnessModule::IN_R_INPUT));

		// Audio outputs
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.0f, 45.0f)), module, PatchSMHarnessModule::OUT_L_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.0f, 45.0f)), module, PatchSMHarnessModule::OUT_R_OUTPUT));

		// CV inputs (stacked)
		float cvY = 70.0f;
		const float cvStep = 7.0f;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 0)), module, PatchSMHarnessModule::CV1_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 1)), module, PatchSMHarnessModule::CV2_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 2)), module, PatchSMHarnessModule::CV3_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 3)), module, PatchSMHarnessModule::CV4_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 4)), module, PatchSMHarnessModule::CV5_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 5)), module, PatchSMHarnessModule::CV6_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 6)), module, PatchSMHarnessModule::CV7_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, cvY + cvStep * 7)), module, PatchSMHarnessModule::CV8_INPUT));

		// CV outputs
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.0f, cvY + cvStep * 6)), module, PatchSMHarnessModule::CVOUT1_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(30.0f, cvY + cvStep * 7)), module, PatchSMHarnessModule::CVOUT2_OUTPUT));

		// Buttons for B7 (reverb) and B8 (CV swap)
		addParam(createParamCentered<LEDButton>(mm2px(Vec(30.0f, 70.0f + cvStep * 8.5f)), module, PatchSMHarnessModule::B7_PARAM));
		addParam(createParamCentered<LEDButton>(mm2px(Vec(30.0f, 70.0f + cvStep * 9.5f)), module, PatchSMHarnessModule::B8_PARAM));
	}
};

// --------------------------------------------------------------------------
// Plugin init
// --------------------------------------------------------------------------

Plugin* pluginInstance;
Model*  modelPatchSMHarness;

void init(Plugin* p) {
	pluginInstance = p;
	p->slug = "VCVHARNESS";
	p->version = "2.0.0";

	// Expose model
	modelPatchSMHarness = createModel<PatchSMHarnessModule, PatchSMHarnessWidget>("PatchSMHarness");
	p->addModel(modelPatchSMHarness);
}

