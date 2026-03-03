# Offline test for phase vocoder resynthesis (host build)
CXX = g++
CXXFLAGS      = -std=c++14 -O2 -Wall -I..
TARGET        = test_resynth_offline
TARGET_CV     = test_resynth_cv_sweeps
TARGET_FLUFF  = test_resynth_fluff
TARGET_HARMONIC = test_resynth_harmonic
SRCS          = test_resynth_offline.cpp wav_io.cpp
SRCS_CV       = test_resynth_cv_sweeps.cpp wav_io.cpp
SRCS_FLUFF    = test_resynth_fluff.cpp wav_io.cpp
SRCS_HARMONIC = test_resynth_harmonic.cpp wav_io.cpp

.PHONY: all run cv_sweeps fluff harmonic tests

all: $(TARGET) $(TARGET_FLUFF) $(TARGET_HARMONIC)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

$(TARGET_CV): $(SRCS_CV)
	$(CXX) $(CXXFLAGS) -o $(TARGET_CV) $(SRCS_CV)

$(TARGET_FLUFF): $(SRCS_FLUFF)
	$(CXX) $(CXXFLAGS) -o $(TARGET_FLUFF) $(SRCS_FLUFF)

$(TARGET_HARMONIC): $(SRCS_HARMONIC)
	$(CXX) $(CXXFLAGS) -o $(TARGET_HARMONIC) $(SRCS_HARMONIC)

# V/OCT sweep: all WAVs in samples/ -> out/voct_sweep/{basename}_voct_sweep.wav
run: $(TARGET)
	./$(TARGET)

# CV sweeps: all WAVs in samples/ -> out/cv_sweep/ (no MAX COMP) and out/cv_sweep_maxcomp/ (MAX COMP)
cv_sweeps: $(TARGET_CV)
	./$(TARGET_CV)

# FLUFF scale test: all WAVs in samples/ -> out/fluff/{basename}_fluff_combo{0..4}.wav
fluff: $(TARGET_FLUFF)
	./$(TARGET_FLUFF)

# Harmonic-stack simplified algorithm tests:
#   out/harmonic/{basename}_harmonic_C2.wav
#   out/harmonic/{basename}_harmonic_voct_sweep.wav
harmonic: $(TARGET_HARMONIC)
	./$(TARGET_HARMONIC)

# Run all tests (voct_sweep, cv_sweeps, fluff, harmonic)
tests: run cv_sweeps fluff harmonic

clean:
	rm -f $(TARGET) $(TARGET_CV) $(TARGET_FLUFF)
