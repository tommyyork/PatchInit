// Simple wrapper for a lush plate-style reverb using DaisySP's ReverbSc.
// Tuned for a large hall / plate feel with a long (~6s) decay.

#pragma once

#include "daisysp.h"

class Plateau
{
  public:
    Plateau() {}

    void Init(float sample_rate);

    // Process stereo input and return stereo wet signal.
    // Dry/wet mixing is handled by the caller.
    void Process(float inL, float inR, float& outL, float& outR);

  private:
    daisysp::ReverbSc verb_;
};

