#pragma once
#include <algorithm>
#include <cmath>

// Combined physical-model voicing selected in treble-model audition 4.
// The reference scale is an audition choice, not a measured piano model.
namespace PianoTrebleModel
{
    // The selected balance audition shortens hammer contact above C5. Its
    // level compensation is independent of MIDI velocity and calculated once
    // per voice; it must not turn into a gain/compressor driven by the music.
    inline float balanceRange (int note)
    {
        const float x = std::max (0.0f, (note - 72.0f) / 12.0f);
        return x < 1.0f ? x * x * (2.0f - x) : x;
    }

    inline float balancedHammerMass (int note)
    {
        return std::pow (2.0f, -1.7f * balanceRange (note));
    }

    inline float balancedOutputGain (int note)
    {
        return std::pow (10.0f, 2.5f * std::min (1.0f, balanceRange (note)) / 20.0f)
             * std::pow (balancedHammerMass (note), -0.65f);
    }

    inline float weight (int note)
    {
        const float x = std::clamp ((note - 60.0f) / 12.0f, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    inline void scale (int note, float frequency, float density, float amount,
                       float radiusMultiplier, float lengthMultiplier,
                       float& radius, float& length)
    {
        if (amount == 0.0f) return;
        const float top = std::clamp ((note - 72.0f) / 36.0f, 0.0f, 1.0f);
        const float targetRadius = (0.48f - 0.08f * top) * 0.001f * radiusMultiplier;
        radius += (targetRadius - radius) * amount;
        const float massPerLength = 3.14159265358979323846f * radius * radius * density;
        const float targetLength = std::sqrt (900.0f / massPerLength) / (2.0f * frequency) * lengthMultiplier;
        length += (targetLength - length) * amount;
    }
}
