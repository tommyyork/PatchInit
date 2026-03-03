# Offline test for phase vocoder resynthesis (host build)
CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall -I..
TARGET = test_resynth_offline
TARGET_CV = test_resynth_cv_sweeps
SRCS = test_resynth_offline.cpp wav_io.cpp
SRCS_CV = test_resynth_cv_sweeps.cpp wav_io.cpp

.PHONY: all run cv_sweeps

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

$(TARGET_CV): $(SRCS_CV)
	$(CXX) $(CXXFLAGS) -o $(TARGET_CV) $(SRCS_CV)

run: $(TARGET)
	./$(TARGET)

cv_sweeps: $(TARGET_CV)
	./$(TARGET_CV)
	./$(TARGET_CV) samples/synth_bassline.wav
	@if [ -f samples/mother32_filter_sweep.wav ]; then ./$(TARGET_CV) samples/mother32_filter_sweep.wav; fi

clean:
	rm -f $(TARGET) $(TARGET_CV)
