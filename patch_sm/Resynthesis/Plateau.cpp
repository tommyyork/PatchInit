#include "Plateau.h"

using namespace daisysp;

void Plateau::Init(float sample_rate)
{
    verb_.Init(sample_rate);

    // Configure for a large, lush hall-like plate.
    // Feedback close to 1.0 gives a long decay; tuned by ear for ~6s.
    verb_.SetFeedback(0.92f);

    // Damping: keep highs present but not harsh.
    verb_.SetLpFreq(8000.0f);
}

void Plateau::Process(float inL, float inR, float& outL, float& outR)
{
    float wetL, wetR;
    verb_.Process(inL, inR, &wetL, &wetR);
    outL = wetL;
    outR = wetR;
}

